#include "io/DicomReader.h"

#include "core/Volume.h"
#include "io/DicomGeometry.h"
#include "io/DicomSeries.h"

#include <gdcmDataElement.h>
#include <gdcmReader.h>
#include <gdcmWriter.h>
#include <itkGDCMImageIO.h>
#include <itkImage.h>
#include <itkImageFileWriter.h>
#include <itkImageRegionIterator.h>
#include <itkImageSeriesWriter.h>
#include <itkMetaDataObject.h>
#include <itkNumericSeriesFileNames.h>

#include <chrono>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace
{

std::string pathForItk(const std::filesystem::path& path)
{
    const auto utf8 = path.u8string();
    return std::string(utf8.begin(), utf8.end());
}

bool expectTrue(const bool condition, const std::string_view field)
{
    if(condition)
    {
        return true;
    }
    std::cerr << field << ": expected true\n";
    return false;
}

bool expectNear(
    const double actual,
    const double expected,
    const std::string_view field,
    const double tolerance = 1.0e-5)
{
    if(std::abs(actual - expected) <= tolerance)
    {
        return true;
    }
    std::cerr << field << ": expected " << expected << ", got " << actual << '\n';
    return false;
}

template<typename Function>
bool expectThrows(Function&& function, const std::string_view field)
{
    try
    {
        function();
    }
    catch(const std::exception&)
    {
        return true;
    }
    std::cerr << field << ": expected an exception\n";
    return false;
}

void addPrivateTestMetadata(const std::filesystem::path& path)
{
    {
        gdcm::Reader reader;
        reader.SetFileName(pathForItk(path).c_str());
        if(!reader.Read())
        {
            throw std::runtime_error("Unable to reopen generated DICOM test file");
        }

        auto& dataSet = reader.GetFile().GetDataSet();
        const auto insertText = [&dataSet](
                                    const gdcm::Tag& tag,
                                    const gdcm::VR& vr,
                                    const std::string_view value) {
            gdcm::DataElement element(tag);
            element.SetVR(vr);
            element.SetByteValue(
                value.data(), static_cast<std::uint32_t>(value.size()));
            dataSet.Replace(element);
        };
        insertText(gdcm::Tag(0x0011, 0x0010), gdcm::VR::LO, "RADMARKY");
        insertText(
            gdcm::Tag(0x0011, 0x1010),
            gdcm::VR::LO,
            "PRIVATE-TEST-VALUE");

        const auto output = path.string() + ".private";
        gdcm::Writer writer;
        writer.SetFile(reader.GetFile());
        writer.SetFileName(output.c_str());
        if(!writer.Write())
        {
            throw std::runtime_error("Unable to write private DICOM test metadata");
        }
    }
    const auto output = path.string() + ".private";
    std::filesystem::remove(path);
    std::filesystem::rename(output, path);
}

std::filesystem::path writePaletteColorDicom(
    const std::filesystem::path& directory)
{
    using PaletteImage = itk::Image<unsigned char, 2>;
    auto image = PaletteImage::New();
    PaletteImage::RegionType region;
    region.SetSize({{2, 2}});
    image->SetRegions(region);
    image->Allocate();
    image->SetPixel({{0, 0}}, 0);
    image->SetPixel({{1, 0}}, 1);
    image->SetPixel({{0, 1}}, 2);
    image->SetPixel({{1, 1}}, 3);

    const auto path = directory / "palette-color.dcm";
    auto dicomIo = itk::GDCMImageIO::New();
    itk::EncapsulateMetaData<std::string>(
        dicomIo->GetMetaDataDictionary(), "0008|0060", "OT");
    itk::EncapsulateMetaData<std::string>(
        dicomIo->GetMetaDataDictionary(),
        "0020|000d",
        "1.2.826.0.1.3680043.2.1125.6010");
    itk::EncapsulateMetaData<std::string>(
        dicomIo->GetMetaDataDictionary(),
        "0020|000e",
        "1.2.826.0.1.3680043.2.1125.6011");
    using Writer = itk::ImageFileWriter<PaletteImage>;
    auto writer = Writer::New();
    writer->SetInput(image);
    writer->SetImageIO(dicomIo);
    writer->SetFileName(pathForItk(path));
    writer->Update();

    gdcm::Reader reader;
    reader.SetFileName(pathForItk(path).c_str());
    if(!reader.Read())
    {
        throw std::runtime_error("Unable to reopen palette DICOM fixture");
    }
    auto& dataSet = reader.GetFile().GetDataSet();
    const auto replaceBytes = [&dataSet](
                                  const gdcm::Tag& tag,
                                  const gdcm::VR& vr,
                                  const void* const bytes,
                                  const std::size_t size) {
        gdcm::DataElement element(tag);
        element.SetVR(vr);
        element.SetByteValue(
            static_cast<const char*>(bytes),
            static_cast<std::uint32_t>(size));
        dataSet.Replace(element);
    };
    constexpr std::string_view paletteColor = "PALETTE COLOR";
    replaceBytes(
        gdcm::Tag(0x0028, 0x0004),
        gdcm::VR::CS,
        paletteColor.data(),
        paletteColor.size());

    const std::array<std::uint16_t, 3> descriptor{{256, 0, 16}};
    std::array<std::uint16_t, 256> red{};
    std::array<std::uint16_t, 256> green{};
    std::array<std::uint16_t, 256> blue{};
    for(std::size_t index = 0; index < red.size(); ++index)
    {
        const auto gray = static_cast<std::uint16_t>(index << 8);
        red[index] = gray;
        green[index] = gray;
        blue[index] = gray;
    }
    red[1] = 0xff00;
    green[2] = 0xff00;
    blue[3] = 0xff00;
    for(const auto element : {0x1101, 0x1102, 0x1103})
    {
        replaceBytes(
            gdcm::Tag(0x0028, static_cast<std::uint16_t>(element)),
            gdcm::VR::US,
            descriptor.data(),
            sizeof(descriptor));
    }
    const std::array paletteData{&red, &green, &blue};
    for(std::size_t component = 0; component < paletteData.size(); ++component)
    {
        replaceBytes(
            gdcm::Tag(
                0x0028, static_cast<std::uint16_t>(0x1201 + component)),
            gdcm::VR::OW,
            paletteData[component]->data(),
            sizeof(red));
    }

    const auto outputPath = directory / "palette-color-updated.dcm";
    gdcm::Writer gdcmWriter;
    gdcmWriter.SetFile(reader.GetFile());
    gdcmWriter.SetFileName(pathForItk(outputPath).c_str());
    if(!gdcmWriter.Write())
    {
        throw std::runtime_error("Unable to write palette DICOM fixture");
    }
    return outputPath;
}

} // namespace

int main()
{
    using SourceImage = itk::Image<short, 3>;
    using SliceImage = itk::Image<short, 2>;
    const std::string seriesUid = "1.2.826.0.1.3680043.2.1125.6001";
    const auto unique = std::chrono::high_resolution_clock::now()
                            .time_since_epoch()
                            .count();
    const auto directory = std::filesystem::temp_directory_path()
        / ("radmarky-dicom-reader-" + std::to_string(unique));
    std::filesystem::create_directories(directory);

    try
    {
        auto image = SourceImage::New();
        SourceImage::RegionType region;
        region.SetSize({{4, 3, 12}});
        image->SetRegions(region);
        SourceImage::SpacingType spacing;
        spacing[0] = 0.7;
        spacing[1] = 0.8;
        spacing[2] = 2.5;
        image->SetSpacing(spacing);
        SourceImage::PointType origin;
        origin[0] = 10.0;
        origin[1] = -20.0;
        origin[2] = 30.0;
        image->SetOrigin(origin);
        constexpr double gantryTiltX = 0.2;
        const double gantryTiltZ = std::sqrt(1.0 - gantryTiltX * gantryTiltX);
        SourceImage::DirectionType direction;
        direction.SetIdentity();
        direction[0][2] = gantryTiltX;
        direction[2][2] = gantryTiltZ;
        image->SetDirection(direction);
        image->Allocate();
        itk::ImageRegionIterator<SourceImage> iterator(image, region);
        short value = -100;
        for(iterator.GoToBegin(); !iterator.IsAtEnd(); ++iterator)
        {
            iterator.Set(value++);
        }

        auto names = itk::NumericSeriesFileNames::New();
        names->SetSeriesFormat(pathForItk(directory / "slice%03d.dcm"));
        names->SetStartIndex(0);
        names->SetEndIndex(11);
        names->SetIncrementIndex(1);

        auto dicomIo = itk::GDCMImageIO::New();
        dicomIo->KeepOriginalUIDOn();
        auto& dictionary = dicomIo->GetMetaDataDictionary();
        itk::EncapsulateMetaData<std::string>(
            dictionary, "0008|0060", "CT");
        itk::EncapsulateMetaData<std::string>(
            dictionary, "0008|0008", "DERIVED\\SECONDARY");
        itk::EncapsulateMetaData<std::string>(
            dictionary, "0008|0064", "DV");
        itk::EncapsulateMetaData<std::string>(
            dictionary, "0020|000d", "1.2.826.0.1.3680043.2.1125.6000");
        itk::EncapsulateMetaData<std::string>(
            dictionary, "0020|000e", seriesUid);
        itk::EncapsulateMetaData<std::string>(
            dictionary, "0008|103e", "Axial chest");
        itk::EncapsulateMetaData<std::string>(
            dictionary, "0020|0012", "7");

        using Writer = itk::ImageSeriesWriter<SourceImage, SliceImage>;
        auto writer = Writer::New();
        writer->SetInput(image);
        writer->SetImageIO(dicomIo);
        writer->SetFileNames(names->GetFileNames());
        writer->Update();
        addPrivateTestMetadata(names->GetFileNames().front());

        std::vector<std::filesystem::path> filePaths;
        for(const auto& name : names->GetFileNames())
        {
            filePaths.emplace_back(name);
        }
        const auto records = radmarky::io::DicomReader::scan(filePaths);
        bool passed = expectTrue(records.size() == 12, "DICOM scan count");
        for(const auto& record : records)
        {
            passed &= expectTrue(record.readable, "generated DICOM readable");
            passed &= expectTrue(
                record.seriesInstanceUid == seriesUid, "Series Instance UID");
            passed &= expectTrue(
                record.seriesDescription == "Axial chest",
                "Series Description scanned");
            passed &= expectTrue(
                record.acquisitionNumber == std::optional<long long>{7},
                "Acquisition Number scanned");
            passed &= expectTrue(
                record.imagePositionPatient.has_value(),
                "Image Position Patient scanned");
            passed &= expectTrue(
                record.imageOrientationPatient.has_value(),
                "Image Orientation Patient scanned");
            passed &= expectTrue(
                record.pixelSpacing.has_value(), "Pixel Spacing scanned");
            passed &= expectTrue(
                record.rows == std::optional<std::size_t>{3}, "Rows scanned");
            passed &= expectTrue(
                record.columns == std::optional<std::size_t>{4},
                "Columns scanned");
        }
        const auto junkPath = directory / "not-dicom.txt";
        {
            std::ofstream junk(junkPath);
            junk << "not a DICOM object";
        }
        auto mixedPaths = filePaths;
        mixedPaths.push_back(junkPath);
        const auto mixedRecords = radmarky::io::DicomReader::scan(mixedPaths);
        passed &= expectTrue(
            mixedRecords.size() == mixedPaths.size(), "mixed scan count");
        passed &= expectTrue(
            mixedRecords.front().readable, "valid file survives mixed batch scan");
        passed &= expectTrue(
            !mixedRecords.back().readable, "non-DICOM file rejected in batch scan");
        const auto extensionlessPath = directory / "extensionless-dicom";
        std::filesystem::copy_file(filePaths.front(), extensionlessPath);
        const auto extensionlessRecords = radmarky::io::DicomReader::scan(
            std::vector<std::filesystem::path>{extensionlessPath});
        passed &= expectTrue(
            extensionlessRecords.size() == 1
                && extensionlessRecords.front().readable
                && extensionlessRecords.front().seriesInstanceUid == seriesUid,
            "extensionless DICOM detected by contents");

        const auto nestedDirectory = directory / "nested" / "deeper";
        std::filesystem::create_directories(nestedDirectory);
        const auto nestedDicomPath = nestedDirectory / "slice-without-extension";
        std::filesystem::copy_file(filePaths[1], nestedDicomPath);
        const auto emptyDirectory = directory / "empty";
        std::filesystem::create_directories(emptyDirectory);
        const auto directoryRecords = radmarky::io::DicomReader::scan(
            std::vector<std::filesystem::path>{directory, extensionlessPath});
        const auto readableDirectoryRecords = std::count_if(
            directoryRecords.begin(), directoryRecords.end(), [](const auto& record) {
                return record.readable;
            });
        passed &= expectTrue(
            directoryRecords.size() == 15,
            "recursive DICOM folder scan includes every regular file once");
        passed &= expectTrue(
            readableDirectoryRecords == 14,
            "recursive DICOM folder scan retains extensionless nested slices");
        passed &= expectTrue(
            std::any_of(
                directoryRecords.begin(),
                directoryRecords.end(),
                [&nestedDicomPath](const auto& record) {
                    return record.filePath == nestedDicomPath && record.readable;
                }),
            "nested extensionless DICOM discovered");
        passed &= expectThrows(
            [&] {
                (void)radmarky::io::DicomReader::scan(
                    std::vector<std::filesystem::path>{emptyDirectory});
            },
            "empty DICOM folder rejected clearly");
        const auto analysis = radmarky::io::analyzeDicomSeries(records);
        passed &= expectTrue(
            analysis.series.size() == 1 && analysis.series.front().consistent(),
            "one consistent series detected");
        const auto geometryAnalysis =
            radmarky::io::analyzeDicomGeometry(records);
        passed &= expectTrue(geometryAnalysis.valid(), "DICOM geometry valid");
        passed &= expectTrue(
            geometryAnalysis.gantryTilt, "DICOM gantry tilt detected");

        auto irregularRecords = records;
        const auto& previousPosition = *irregularRecords[5].imagePositionPatient;
        const auto& originalPosition = *irregularRecords[6].imagePositionPatient;
        for(std::size_t axis = 0; axis < 3; ++axis)
        {
            (*irregularRecords[6].imagePositionPatient)[axis] +=
                0.25 * (originalPosition[axis] - previousPosition[axis]);
        }
        const auto irregularGeometry =
            radmarky::io::analyzeDicomGeometry(irregularRecords);
        passed &= expectTrue(
            irregularGeometry.canOverrideNonUniformSpacing(),
            "reader fixture has overridable non-uniform spacing");
        passed &= expectThrows(
            [&] { (void)radmarky::io::DicomReader::read(irregularRecords); },
            "strict reader rejects non-uniform spacing");
        const auto overriddenVolume = radmarky::io::DicomReader::read(
            irregularRecords,
            {},
            {},
            nullptr,
            radmarky::io::DicomReadGeometryPolicy::AllowSliceSpacingOverride);
        passed &= expectTrue(
            overriddenVolume->geometry().dimensions()
                == radmarky::core::ImageGeometry::Dimensions{{4, 3, 12}},
            "explicit spacing override permits DICOM import");

        auto spacingMetadataMismatchRecords = records;
        for(auto& record : spacingMetadataMismatchRecords)
        {
            record.spacingBetweenSlices = 9.0;
        }
        const auto spacingMetadataMismatchGeometry =
            radmarky::io::analyzeDicomGeometry(
                spacingMetadataMismatchRecords);
        passed &= expectTrue(
            spacingMetadataMismatchGeometry.canOverrideSpacingMetadataMismatch(),
            "reader fixture has overridable spacing metadata disagreement");
        passed &= expectThrows(
            [&] {
                (void)radmarky::io::DicomReader::read(
                    spacingMetadataMismatchRecords);
            },
            "strict reader rejects spacing metadata disagreement");
        const auto metadataOverrideVolume = radmarky::io::DicomReader::read(
            spacingMetadataMismatchRecords,
            {},
            {},
            nullptr,
            radmarky::io::DicomReadGeometryPolicy::AllowSliceSpacingOverride);
        passed &= expectTrue(
            metadataOverrideVolume->geometry().dimensions()
                == radmarky::core::ImageGeometry::Dimensions{{4, 3, 12}},
            "explicit override permits spacing metadata disagreement import");

        double previousProgress = 0.0;
        std::size_t progressUpdates = 0;
        bool monotonicProgress = true;
        radmarky::io::DicomReadTimings timings;
        const auto volume = radmarky::io::DicomReader::read(
            records,
            [&](const double progress) {
                monotonicProgress &= progress >= previousProgress;
                previousProgress = progress;
                ++progressUpdates;
            },
            {},
            &timings);
        passed &= expectTrue(monotonicProgress, "monotonic DICOM progress");
        passed &= expectTrue(progressUpdates == 12, "per-slice DICOM progress");
        passed &= expectNear(previousProgress, 1.0, "completed DICOM progress");
        passed &= expectTrue(timings.workerCount >= 1, "adaptive worker count");
        passed &= expectTrue(
            timings.pixelDecodeMilliseconds >= 0.0,
            "pixel decode timing captured");
        passed &= expectThrows(
            [&] {
                (void)radmarky::io::DicomReader::scan(
                    filePaths, [] { return true; });
            },
            "DICOM scan cancellation honored");
        passed &= expectTrue(
            volume->geometry().dimensions()
                == radmarky::core::ImageGeometry::Dimensions{{4, 3, 12}},
            "DICOM volume dimensions");
        const auto singleSliceVolume = radmarky::io::DicomReader::read(
            std::vector<radmarky::io::DicomFileRecord>{records.front()});
        passed &= expectTrue(
            singleSliceVolume->geometry().dimensions()
                == radmarky::core::ImageGeometry::Dimensions{{4, 3, 1}},
            "single-file DICOM fallback dimensions");
        passed &= expectNear(volume->geometry().spacing()[0], 0.7, "spacing x");
        passed &= expectNear(volume->geometry().spacing()[1], 0.8, "spacing y");
        passed &= expectNear(volume->geometry().spacing()[2], 2.5, "spacing z");
        passed &= expectNear(
            volume->geometry().direction()[0][2],
            gantryTiltX,
            "gantry tilt direction x");
        passed &= expectNear(
            volume->geometry().direction()[2][2],
            gantryTiltZ,
            "gantry tilt direction z");
        passed &= expectNear(volume->scalarRange().minimum, -100.0, "scalar min");
        passed &= expectNear(volume->scalarRange().maximum, 43.0, "scalar max");
        const auto& metadata = volume->dicomMetadata();
        passed &= expectTrue(!metadata.empty(), "DICOM metadata retained");
        const auto containsMetadata = [&metadata](
                                          const std::string_view key,
                                          const std::string_view value) {
            return std::any_of(
                metadata.begin(), metadata.end(), [&](const auto& entry) {
                    return entry.key.find(key) != std::string::npos
                        && entry.value == value;
                });
        };
        passed &= expectTrue(
            containsMetadata("(0008,0060) Modality", "CT"),
            "readable Modality metadata");
        passed &= expectTrue(
            containsMetadata("(0020,000E) Series Instance UID", seriesUid),
            "series UID metadata");
        passed &= expectTrue(
            containsMetadata("(0011,1010)", "PRIVATE-TEST-VALUE"),
            "unknown private metadata retained");

        const auto palettePath = writePaletteColorDicom(directory);
        const auto paletteVolume = radmarky::io::DicomReader::read(
            std::vector<std::filesystem::path>{palettePath});
        passed &= expectTrue(
            paletteVolume->hasDisplayRgb(),
            "palette-color DICOM retains RGB display data");
        const auto& rgb = paletteVolume->displayRgb();
        passed &= expectTrue(rgb.size() == 12, "palette RGB byte count");
        if(rgb.size() == 12)
        {
            passed &= expectTrue(
                rgb[3] == 255 && rgb[4] == 1 && rgb[5] == 1,
                "palette red entry decoded");
            passed &= expectTrue(
                rgb[6] == 2 && rgb[7] == 255 && rgb[8] == 2,
                "palette green entry decoded");
            passed &= expectTrue(
                rgb[9] == 3 && rgb[10] == 3 && rgb[11] == 255,
                "palette blue entry decoded");
        }

        std::filesystem::remove_all(directory);
        return passed ? 0 : 1;
    }
    catch(const std::exception& exception)
    {
        std::filesystem::remove_all(directory);
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
