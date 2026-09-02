#include "io/DicomReader.h"

#include "core/Volume.h"
#include "io/DicomGeometry.h"

#include <gdcmImageReader.h>
#include <gdcmReader.h>
#include <gdcmScanner.h>
#include <gdcmStringFilter.h>
#include <gdcmTag.h>
#include <itkGDCMImageIO.h>
#include <itkCommand.h>
#include <itkImageFileReader.h>
#include <itkImageSeriesReader.h>
#include <itkMacro.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <limits>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace radmarky::io
{
namespace
{

constexpr std::size_t maximumDicomWorkerCount = 8;

using Clock = std::chrono::steady_clock;

double elapsedMilliseconds(const Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

std::size_t dicomWorkerCount(const std::size_t taskCount)
{
    const auto hardware = std::thread::hardware_concurrency();
    const std::size_t hardwareTarget = hardware == 0
        ? 4
        : std::max<std::size_t>(2, (static_cast<std::size_t>(hardware) + 1) / 2);
    return std::min({taskCount, hardwareTarget, maximumDicomWorkerCount});
}

void throwIfCancelled(const DicomReader::CancelCheck& cancelled)
{
    if(cancelled && cancelled())
    {
        throw std::runtime_error("DICOM import cancelled");
    }
}

std::string pathForMedicalIo(const std::filesystem::path& path)
{
    const auto utf8 = path.u8string();
    return std::string(utf8.begin(), utf8.end());
}

std::filesystem::path normalizedInputPath(const std::filesystem::path& path)
{
    std::error_code error;
    const auto absolute = std::filesystem::absolute(path, error);
    return (error ? path : absolute).lexically_normal();
}

std::vector<std::filesystem::path> expandDicomInputPaths(
    const std::vector<std::filesystem::path>& inputPaths,
    const DicomReader::CancelCheck& cancelled)
{
    std::vector<std::filesystem::path> files;
    std::set<std::filesystem::path> seen;
    const auto append = [&files, &seen](const std::filesystem::path& path) {
        if(seen.insert(normalizedInputPath(path)).second)
        {
            files.push_back(path);
        }
    };

    for(const auto& inputPath : inputPaths)
    {
        throwIfCancelled(cancelled);
        std::error_code statusError;
        if(!std::filesystem::is_directory(inputPath, statusError) || statusError)
        {
            // Preserve the existing behavior for explicit file paths, including
            // paths that later prove to be missing or unreadable.
            append(inputPath);
            continue;
        }

        std::vector<std::filesystem::path> discovered;
        std::error_code iteratorError;
        std::filesystem::recursive_directory_iterator iterator(
            inputPath,
            std::filesystem::directory_options::skip_permission_denied,
            iteratorError);
        const std::filesystem::recursive_directory_iterator end;
        while(iterator != end)
        {
            throwIfCancelled(cancelled);
            std::error_code fileError;
            if(iterator->is_regular_file(fileError) && !fileError)
            {
                discovered.push_back(iterator->path());
            }
            iterator.increment(iteratorError);
            iteratorError.clear();
        }
        std::sort(discovered.begin(), discovered.end());
        for(const auto& path : discovered)
        {
            append(path);
        }
    }
    return files;
}

std::string trimmed(const char* const value)
{
    if(value == nullptr)
    {
        return {};
    }
    std::string result(value);
    while(!result.empty()
          && (result.back() == ' ' || result.back() == '\0'
              || result.back() == '\r' || result.back() == '\n'))
    {
        result.pop_back();
    }
    const auto first = result.find_first_not_of(" \t\r\n");
    return first == std::string::npos ? std::string{} : result.substr(first);
}

std::optional<long long> parseInstanceNumber(const char* const value)
{
    const auto text = trimmed(value);
    if(text.empty())
    {
        return std::nullopt;
    }
    long long number = 0;
    const auto [end, error] =
        std::from_chars(text.data(), text.data() + text.size(), number);
    if(error != std::errc{} || end != text.data() + text.size())
    {
        return std::nullopt;
    }
    return number;
}

template<std::size_t Size>
std::optional<std::array<double, Size>> parseDecimalValues(
    const char* const value)
{
    const auto text = trimmed(value);
    if(text.empty())
    {
        return std::nullopt;
    }

    std::array<double, Size> result{};
    std::size_t start = 0;
    for(std::size_t index = 0; index < Size; ++index)
    {
        const auto separator = text.find('\\', start);
        const auto end = separator == std::string::npos ? text.size() : separator;
        const auto firstValueCharacter = text.find_first_not_of(" \t", start);
        if(firstValueCharacter == std::string::npos || firstValueCharacter >= end)
        {
            return std::nullopt;
        }
        const auto lastValueCharacter = text.find_last_not_of(" \t", end - 1);
        const char* const beginPointer = text.data() + firstValueCharacter;
        const char* const endPointer = text.data() + lastValueCharacter + 1;
        const auto [parsedEnd, error] = std::from_chars(
            beginPointer,
            endPointer,
            result[index],
            std::chars_format::general);
        if(error != std::errc{} || parsedEnd != endPointer
           || !std::isfinite(result[index]))
        {
            return std::nullopt;
        }
        if(index + 1 < Size)
        {
            if(separator == std::string::npos)
            {
                return std::nullopt;
            }
            start = separator + 1;
        }
        else if(separator != std::string::npos)
        {
            return std::nullopt;
        }
    }
    return result;
}

std::optional<std::size_t> parseSize(const char* const value)
{
    const auto text = trimmed(value);
    if(text.empty())
    {
        return std::nullopt;
    }
    std::size_t result = 0;
    const auto [end, error] =
        std::from_chars(text.data(), text.data() + text.size(), result);
    if(error != std::errc{} || end != text.data() + text.size() || result == 0)
    {
        return std::nullopt;
    }
    return result;
}

std::string displayTagKey(const gdcm::Tag& tag, const std::string& label)
{
    std::ostringstream stream;
    stream << '(' << std::uppercase << std::hex << std::setfill('0')
           << std::setw(4) << tag.GetGroup() << ',' << std::setw(4)
           << tag.GetElement() << ')';
    if(!label.empty() && label != "Unknown")
    {
        stream << ' ' << label;
    }
    return stream.str();
}

std::string safeDisplayValue(std::string value)
{
    for(char& character : value)
    {
        const auto byte = static_cast<unsigned char>(character);
        if(character == '\0' || character == '\r' || character == '\n'
           || (byte < 0x20 && character != '\t'))
        {
            character = ' ';
        }
    }
    while(!value.empty() && value.back() == ' ')
    {
        value.pop_back();
    }
    return value;
}

bool isDisplayText(const char* const data, const std::size_t length)
{
    return std::all_of(data, data + length, [](const char character) {
        const auto byte = static_cast<unsigned char>(character);
        return byte == 0 || character == '\r' || character == '\n'
            || character == '\t' || byte >= 0x20;
    });
}

std::vector<core::Volume::DicomMetadataEntry> readDicomMetadata(
    const std::string& fileName)
{
    gdcm::Reader reader;
    reader.SetFileName(fileName.c_str());
    const gdcm::Tag pixelData(0x7fe0, 0x0010);
    if(!reader.ReadUpToTag(pixelData, std::set<gdcm::Tag>{pixelData}))
    {
        throw std::runtime_error("Unable to read DICOM header metadata");
    }
    gdcm::StringFilter stringFilter;
    stringFilter.SetFile(reader.GetFile());

    std::vector<core::Volume::DicomMetadataEntry> metadata;
    const auto appendDataSet = [&metadata, &stringFilter](const gdcm::DataSet& dataSet) {
        for(auto iterator = dataSet.Begin(); iterator != dataSet.End(); ++iterator)
        {
            const auto [label, rawValue] = stringFilter.ToStringPair(*iterator);
            std::string value = rawValue;
            if(value.empty() && iterator->GetVR() == gdcm::VR::SQ)
            {
                value = "[sequence]";
            }
            else if(value.empty())
            {
                const auto* const bytes = iterator->GetByteValue();
                if(bytes != nullptr && bytes->GetLength() > 0)
                {
                    const auto length = static_cast<std::size_t>(bytes->GetLength());
                    value = (gdcm::VR::IsASCII(iterator->GetVR())
                             || (length <= 4096
                                 && isDisplayText(bytes->GetPointer(), length)))
                        ? std::string(bytes->GetPointer(), length)
                        : "[binary data, " + std::to_string(length) + " bytes]";
                }
            }
            metadata.push_back({
                displayTagKey(iterator->GetTag(), label),
                safeDisplayValue(std::move(value))});
        }
    };
    appendDataSet(reader.GetFile().GetHeader());
    appendDataSet(reader.GetFile().GetDataSet());
    std::sort(
        metadata.begin(),
        metadata.end(),
        [](const auto& left, const auto& right) { return left.key < right.key; });
    return metadata;
}

struct DicomScanTags
{
    gdcm::Tag seriesUid{0x0020, 0x000e};
    gdcm::Tag seriesDescription{0x0008, 0x103e};
    gdcm::Tag acquisitionNumber{0x0020, 0x0012};
    gdcm::Tag instanceNumber{0x0020, 0x0013};
    gdcm::Tag imagePositionPatient{0x0020, 0x0032};
    gdcm::Tag imageOrientationPatient{0x0020, 0x0037};
    gdcm::Tag pixelSpacing{0x0028, 0x0030};
    gdcm::Tag rows{0x0028, 0x0010};
    gdcm::Tag columns{0x0028, 0x0011};
    gdcm::Tag spacingBetweenSlices{0x0018, 0x0088};
    gdcm::Tag frameOfReferenceUid{0x0020, 0x0052};
    gdcm::Tag sopInstanceUid{0x0008, 0x0018};
    gdcm::Tag numberOfFrames{0x0028, 0x0008};
};

void configureScanner(gdcm::Scanner& scanner, const DicomScanTags& tags)
{
    scanner.AddTag(tags.seriesUid);
    scanner.AddTag(tags.seriesDescription);
    scanner.AddTag(tags.acquisitionNumber);
    scanner.AddTag(tags.instanceNumber);
    scanner.AddTag(tags.imagePositionPatient);
    scanner.AddTag(tags.imageOrientationPatient);
    scanner.AddTag(tags.pixelSpacing);
    scanner.AddTag(tags.rows);
    scanner.AddTag(tags.columns);
    scanner.AddTag(tags.spacingBetweenSlices);
    scanner.AddTag(tags.frameOfReferenceUid);
    scanner.AddTag(tags.sopInstanceUid);
    scanner.AddTag(tags.numberOfFrames);
}

void populateRecord(
    const gdcm::Scanner& scanner,
    const DicomScanTags& tags,
    const std::string& name,
    DicomFileRecord& record)
{
    record.readable = scanner.IsKey(name.c_str());
    if(!record.readable)
    {
        record.issue = "Not a readable DICOM file";
        return;
    }

    record.seriesInstanceUid =
        trimmed(scanner.GetValue(name.c_str(), tags.seriesUid));
    record.seriesDescription =
        trimmed(scanner.GetValue(name.c_str(), tags.seriesDescription));
    record.acquisitionNumber = parseInstanceNumber(
        scanner.GetValue(name.c_str(), tags.acquisitionNumber));
    record.instanceNumber =
        parseInstanceNumber(scanner.GetValue(name.c_str(), tags.instanceNumber));
    record.imagePositionPatient = parseDecimalValues<3>(
        scanner.GetValue(name.c_str(), tags.imagePositionPatient));
    record.imageOrientationPatient = parseDecimalValues<6>(
        scanner.GetValue(name.c_str(), tags.imageOrientationPatient));
    record.pixelSpacing =
        parseDecimalValues<2>(scanner.GetValue(name.c_str(), tags.pixelSpacing));
    record.rows = parseSize(scanner.GetValue(name.c_str(), tags.rows));
    record.columns = parseSize(scanner.GetValue(name.c_str(), tags.columns));
    const auto spacingBetweenSlices = parseDecimalValues<1>(
        scanner.GetValue(name.c_str(), tags.spacingBetweenSlices));
    if(spacingBetweenSlices)
    {
        record.spacingBetweenSlices = (*spacingBetweenSlices)[0];
    }
    record.frameOfReferenceUid =
        trimmed(scanner.GetValue(name.c_str(), tags.frameOfReferenceUid));
    record.sopInstanceUid =
        trimmed(scanner.GetValue(name.c_str(), tags.sopInstanceUid));
    record.numberOfFrames =
        parseSize(scanner.GetValue(name.c_str(), tags.numberOfFrames));
    if(record.seriesInstanceUid.empty())
    {
        record.issue = "Missing Series Instance UID";
    }
}

struct OrderedDicomInput
{
    std::vector<std::string> names;
    std::vector<double> sliceGapsMillimetres;
};

OrderedDicomInput orderedDicomInput(
    const std::vector<DicomFileRecord>& records,
    const DicomReadGeometryPolicy geometryPolicy)
{
    const auto geometry = analyzeDicomGeometry(records);
    const bool overrideSliceSpacing = geometryPolicy
            == DicomReadGeometryPolicy::AllowSliceSpacingOverride
        && geometry.canOverrideSliceSpacing();
    if(!geometry.valid() && !overrideSliceSpacing)
    {
        throw std::runtime_error(
            "DICOM geometry validation failed: "
            + formatDicomGeometryDiagnostics(geometry));
    }

    OrderedDicomInput input;
    input.names.reserve(records.size());
    for(const auto index : geometry.orderedIndices)
    {
        input.names.push_back(pathForMedicalIo(records[index].filePath));
    }
    if(records.size() > 1 && geometry.completeSpatialGeometry)
    {
        input.sliceGapsMillimetres.reserve(records.size() - 1);
        for(std::size_t ordered = 1; ordered < geometry.orderedIndices.size();
            ++ordered)
        {
            const auto& previous = *records[geometry.orderedIndices[ordered - 1]]
                                        .imagePositionPatient;
            const auto& current = *records[geometry.orderedIndices[ordered]]
                                       .imagePositionPatient;
            double squaredDistance = 0.0;
            for(std::size_t axis = 0; axis < 3; ++axis)
            {
                const double difference = current[axis] - previous[axis];
                squaredDistance += difference * difference;
            }
            input.sliceGapsMillimetres.push_back(std::sqrt(squaredDistance));
        }
    }
    return input;
}

using VolumeImage = core::Volume::ImageType;
using SeriesReader = itk::ImageSeriesReader<VolumeImage>;

void observeProgress(
    SeriesReader& reader,
    const std::function<void(double)>& progress,
    const DicomReader::CancelCheck& cancelled)
{
    if(!progress && !cancelled)
    {
        reader.Update();
        return;
    }
    struct ObserverContext
    {
        const std::function<void(double)>* progress = nullptr;
        const DicomReader::CancelCheck* cancelled = nullptr;
    };
    // The observer is used only during the synchronous Update call below.
    ObserverContext context{&progress, &cancelled};
    auto command = itk::CStyleCommand::New();
    command->SetClientData(&context);
    command->SetCallback([](
                             itk::Object* caller,
                             const itk::EventObject&,
                             void* clientData) {
        auto* const process = dynamic_cast<itk::ProcessObject*>(caller);
        auto* const observer = static_cast<ObserverContext*>(clientData);
        if(process == nullptr || observer == nullptr)
        {
            return;
        }
        if(observer->cancelled != nullptr && *observer->cancelled
           && (*observer->cancelled)())
        {
            process->AbortGenerateDataOn();
        }
        if(observer->progress != nullptr && *observer->progress)
        {
            (*observer->progress)(process->GetProgress());
        }
    });
    reader.AddObserver(itk::ProgressEvent(), command);

    reader.Update();
}

VolumeImage::Pointer readSerialSeries(
    const std::vector<std::string>& names,
    const std::function<void(double)>& progress,
    const DicomReader::CancelCheck& cancelled)
{
    throwIfCancelled(cancelled);
    auto reader = SeriesReader::New();
    reader->SetImageIO(itk::GDCMImageIO::New());
    reader->SetFileNames(names);
    reader->SetForceOrthogonalDirection(false);
    observeProgress(*reader, progress, cancelled);
    throwIfCancelled(cancelled);
    VolumeImage::Pointer image = reader->GetOutput();
    image->DisconnectPipeline();
    return image;
}

bool supportsParallelSliceAssembly(
    const VolumeImage& seriesInformation,
    const VolumeImage& sliceInformation,
    const std::size_t sliceCount)
{
    const auto seriesSize =
        seriesInformation.GetLargestPossibleRegion().GetSize();
    const auto sliceSize = sliceInformation.GetLargestPossibleRegion().GetSize();
    return seriesSize[0] == sliceSize[0]
        && seriesSize[1] == sliceSize[1]
        && seriesSize[2] == sliceCount
        && sliceSize[2] == 1;
}

struct SliceRange
{
    bool foundFinite = false;
    core::Volume::ScalarRange value{};
};

void extendRange(SliceRange& range, const float* const values, const std::size_t count)
{
    for(std::size_t index = 0; index < count; ++index)
    {
        const double value = values[index];
        if(!std::isfinite(value))
        {
            continue;
        }
        if(!range.foundFinite)
        {
            range.value.minimum = value;
            range.value.maximum = value;
            range.foundFinite = true;
        }
        else
        {
            range.value.minimum = std::min(range.value.minimum, value);
            range.value.maximum = std::max(range.value.maximum, value);
        }
    }
}

struct SliceReadResult
{
    VolumeImage::Pointer image;
    std::optional<core::Volume::ScalarRange> scalarRange;
    double geometrySetupMilliseconds = 0.0;
    double pixelDecodeMilliseconds = 0.0;
    std::size_t workerCount = 1;
};

bool isPaletteColorDicom(const std::string& name)
{
    gdcm::Reader reader;
    reader.SetFileName(name.c_str());
    if(!reader.Read())
    {
        throw std::runtime_error(
            "Unable to inspect DICOM photometric interpretation");
    }
    gdcm::StringFilter filter;
    filter.SetFile(reader.GetFile());
    return trimmed(filter.ToString(gdcm::Tag(0x0028, 0x0004)).c_str())
        == "PALETTE COLOR";
}

std::optional<std::vector<std::uint8_t>> readPaletteColorDisplay(
    const std::vector<std::string>& names,
    const std::size_t expectedPixelCount,
    const DicomReader::CancelCheck& cancelled)
{
    if(names.empty() || !isPaletteColorDicom(names.front()))
    {
        return std::nullopt;
    }
    if(expectedPixelCount > std::numeric_limits<std::size_t>::max() / 3)
    {
        throw std::overflow_error("DICOM color image is too large");
    }

    std::vector<std::uint8_t> rgb;
    rgb.reserve(expectedPixelCount * 3);
    for(const auto& name : names)
    {
        throwIfCancelled(cancelled);
        gdcm::ImageReader reader;
        reader.SetFileName(name.c_str());
        if(!reader.Read())
        {
            throw std::runtime_error("Unable to decode palette-color DICOM");
        }
        if(reader.GetImage().GetPhotometricInterpretation()
           != gdcm::PhotometricInterpretation::PALETTE_COLOR)
        {
            throw std::runtime_error(
                "DICOM series mixes palette-color and non-palette images");
        }

        const auto& paletteImage = reader.GetImage();
        const auto inputByteCount =
            static_cast<std::size_t>(paletteImage.GetBufferLength());
        if(inputByteCount > std::numeric_limits<std::size_t>::max() / 3)
        {
            throw std::overflow_error("DICOM palette pixels are too large");
        }
        std::vector<char> paletteIndices(inputByteCount);
        if(inputByteCount == 0
           || !paletteImage.GetBuffer(paletteIndices.data()))
        {
            throw std::runtime_error(
                "Unable to retrieve palette-color DICOM pixels");
        }
        const auto byteCount = inputByteCount * 3;
        const auto start = rgb.size();
        if(start > std::numeric_limits<std::size_t>::max() - byteCount)
        {
            throw std::overflow_error("DICOM color image is too large");
        }
        rgb.resize(start + byteCount);
        if(!paletteImage.GetLUT().Decode8(
               reinterpret_cast<char*>(rgb.data() + start),
               byteCount,
               paletteIndices.data(),
               inputByteCount))
        {
            throw std::runtime_error(
                "Unable to apply DICOM color palette");
        }
    }
    if(rgb.size() != expectedPixelCount * 3)
    {
        throw std::runtime_error(
            "Decoded DICOM color data does not match the scalar volume");
    }
    return rgb;
}

SliceReadResult readParallelSlices(
    const std::vector<std::string>& names,
    const std::function<void(double)>& progress,
    const DicomReader::CancelCheck& cancelled)
{
    const auto setupStart = Clock::now();
    if(names.size() < 2)
    {
        const auto decodeStart = Clock::now();
        auto image = readSerialSeries(names, progress, cancelled);
        return {
            std::move(image),
            std::nullopt,
            0.0,
            elapsedMilliseconds(decodeStart),
            1};
    }

    throwIfCancelled(cancelled);
    // Let ImageSeriesReader calculate the authoritative 3-D geometry from the
    // first and last ordered slices without asking it to read pixel data.
    auto seriesInformationReader = SeriesReader::New();
    seriesInformationReader->SetImageIO(itk::GDCMImageIO::New());
    seriesInformationReader->SetFileNames(names);
    // ITK defaults this to true, which silently removes a consistent DICOM
    // gantry tilt. Preserve the actual first-to-last slice direction instead.
    seriesInformationReader->SetForceOrthogonalDirection(false);
    seriesInformationReader->UpdateOutputInformation();

    using SliceReader = itk::ImageFileReader<VolumeImage>;
    auto sliceProbe = SliceReader::New();
    sliceProbe->SetImageIO(itk::GDCMImageIO::New());
    sliceProbe->SetFileName(names.front());
    sliceProbe->UpdateOutputInformation();
    if(!supportsParallelSliceAssembly(
           *seriesInformationReader->GetOutput(),
           *sliceProbe->GetOutput(),
           names.size()))
    {
        // Multi-frame or otherwise non-slice inputs retain ITK's established
        // series behavior instead of being forced into a 2-D slice model.
        const double setupMilliseconds = elapsedMilliseconds(setupStart);
        const auto decodeStart = Clock::now();
        auto image = readSerialSeries(names, progress, cancelled);
        return {
            std::move(image),
            std::nullopt,
            setupMilliseconds,
            elapsedMilliseconds(decodeStart),
            1};
    }

    auto image = VolumeImage::New();
    image->CopyInformation(seriesInformationReader->GetOutput());
    image->SetRegions(
        seriesInformationReader->GetOutput()->GetLargestPossibleRegion());
    image->Allocate();

    const auto expectedSliceDimensions =
        sliceProbe->GetOutput()->GetLargestPossibleRegion().GetSize();
    const auto sliceSize =
        sliceProbe->GetOutput()->GetLargestPossibleRegion().GetNumberOfPixels();
    auto* const outputBuffer = image->GetBufferPointer();
    const double setupMilliseconds = elapsedMilliseconds(setupStart);
    const auto decodeStart = Clock::now();
    std::atomic<std::size_t> nextIndex{1};
    std::atomic<bool> stop{false};
    std::size_t completed = 0;
    int lastReportedPercent = -1;
    std::exception_ptr firstError;
    std::mutex errorMutex;
    std::mutex progressMutex;

    const auto reportCompletedSlice = [&]() {
        if(!progress)
        {
            return;
        }
        std::lock_guard lock(progressMutex);
        ++completed;
        const int percent = static_cast<int>(
            completed * 100 / names.size());
        if(percent != lastReportedPercent)
        {
            lastReportedPercent = percent;
            progress(
                static_cast<double>(completed)
                / static_cast<double>(names.size()));
        }
    };

    const auto decodeInto = [&](itk::ImageFileReader<VolumeImage>& reader,
                                const std::size_t index) {
        reader.SetFileName(names[index]);
        reader.UpdateOutputInformation();
        auto* const slice = reader.GetOutput();
        if(slice->GetLargestPossibleRegion().GetSize()
           != expectedSliceDimensions)
        {
            throw std::runtime_error(
                "DICOM slice dimensions changed while reading the series");
        }
        slice->GetPixelContainer()->SetImportPointer(
            outputBuffer + index * sliceSize,
            sliceSize,
            false);
        reader.Update();
    };

    // Reuse the probe for the first decode so it is not opened by another
    // ImageFileReader. All subsequent readers write directly into disjoint
    // regions of the final volume.
    sliceProbe->GetOutput()->GetPixelContainer()->SetImportPointer(
        outputBuffer, sliceSize, false);
    sliceProbe->SetNumberOfWorkUnits(1);
    sliceProbe->Update();
    SliceRange firstSliceRange;
    extendRange(firstSliceRange, outputBuffer, sliceSize);
    reportCompletedSlice();
    throwIfCancelled(cancelled);

    const auto activeWorkers = dicomWorkerCount(names.size() - 1);
    std::vector<SliceRange> workerRanges(activeWorkers);

    const auto workerTask = [&](const std::size_t workerIndex) {
        try
        {
            auto reader = SliceReader::New();
            reader->SetNumberOfWorkUnits(1);
            reader->SetImageIO(itk::GDCMImageIO::New());
            while(!stop.load(std::memory_order_relaxed))
            {
                throwIfCancelled(cancelled);
                const auto index =
                    nextIndex.fetch_add(1, std::memory_order_relaxed);
                if(index >= names.size())
                {
                    return;
                }

                decodeInto(*reader, index);
                extendRange(
                    workerRanges[workerIndex],
                    outputBuffer + index * sliceSize,
                    sliceSize);
                reportCompletedSlice();
            }
        }
        catch(...)
        {
            stop.store(true, std::memory_order_relaxed);
            std::lock_guard lock(errorMutex);
            if(!firstError)
            {
                firstError = std::current_exception();
            }
        }
    };

    std::vector<std::thread> workers;
    workers.reserve(activeWorkers);
    try
    {
        for(std::size_t worker = 0; worker < activeWorkers; ++worker)
        {
            workers.emplace_back(workerTask, worker);
        }
    }
    catch(...)
    {
        stop.store(true, std::memory_order_relaxed);
        for(auto& worker : workers)
        {
            worker.join();
        }
        throw;
    }
    for(auto& worker : workers)
    {
        worker.join();
    }
    if(firstError)
    {
        std::rethrow_exception(firstError);
    }
    throwIfCancelled(cancelled);

    SliceRange combinedRange = firstSliceRange;
    for(const auto& range : workerRanges)
    {
        if(!range.foundFinite)
        {
            continue;
        }
        if(!combinedRange.foundFinite)
        {
            combinedRange = range;
        }
        else
        {
            combinedRange.value.minimum = std::min(
                combinedRange.value.minimum, range.value.minimum);
            combinedRange.value.maximum = std::max(
                combinedRange.value.maximum, range.value.maximum);
        }
    }
    if(!combinedRange.foundFinite)
    {
        throw std::runtime_error("DICOM volume contains no finite voxels");
    }
    return {
        std::move(image),
        combinedRange.value,
        setupMilliseconds,
        elapsedMilliseconds(decodeStart),
        activeWorkers};
}

} // namespace

std::vector<DicomFileRecord> DicomReader::scan(
    const std::vector<std::filesystem::path>& filePaths,
    const CancelCheck& cancelled)
{
    if(filePaths.empty())
    {
        throw std::invalid_argument("At least one DICOM file is required");
    }

    throwIfCancelled(cancelled);
    const auto expandedFilePaths = expandDicomInputPaths(filePaths, cancelled);
    if(expandedFilePaths.empty())
    {
        throw std::invalid_argument(
            "No regular files were found in the selected DICOM folder(s)");
    }
    // Each worker batch is scanned in one GDCM call. Scanner instances are not
    // shared, while records retain their input order for the selection dialog.
    std::vector<DicomFileRecord> records(expandedFilePaths.size());
    const auto activeWorkers = dicomWorkerCount(expandedFilePaths.size());
    std::vector<std::thread> workers;
    workers.reserve(activeWorkers);
    try
    {
        for(std::size_t worker = 0; worker < activeWorkers; ++worker)
        {
            workers.emplace_back([&, worker]() {
                const std::size_t begin =
                    expandedFilePaths.size() * worker / activeWorkers;
                const std::size_t end =
                    expandedFilePaths.size() * (worker + 1) / activeWorkers;
                DicomScanTags tags;
                gdcm::Scanner scanner;
                configureScanner(scanner, tags);
                gdcm::Directory::FilenamesType names;
                std::vector<std::size_t> indices;
                names.reserve(end - begin);
                indices.reserve(end - begin);
                for(std::size_t index = begin; index < end; ++index)
                {
                    records[index].filePath = expandedFilePaths[index];
                    if(cancelled && cancelled())
                    {
                        return;
                    }
                    try
                    {
                        names.push_back(pathForMedicalIo(expandedFilePaths[index]));
                        indices.push_back(index);
                    }
                    catch(const std::exception& exception)
                    {
                        records[index].issue =
                            std::string("Unable to prepare DICOM path: ")
                            + exception.what();
                    }
                }
                try
                {
                    scanner.Scan(names);
                    for(std::size_t offset = 0; offset < names.size(); ++offset)
                    {
                        populateRecord(
                            scanner, tags, names[offset], records[indices[offset]]);
                    }
                }
                catch(const std::exception& exception)
                {
                    for(const auto index : indices)
                    {
                        records[index].issue =
                            std::string("Unable to read DICOM metadata: ")
                            + exception.what();
                    }
                }
            });
        }
    }
    catch(...)
    {
        for(auto& worker : workers)
        {
            worker.join();
        }
        throw;
    }
    for(auto& worker : workers)
    {
        worker.join();
    }
    throwIfCancelled(cancelled);
    return records;
}

std::shared_ptr<core::Volume> DicomReader::read(
    const std::vector<std::filesystem::path>& filePaths,
    const std::function<void(double)>& progress,
    const CancelCheck& cancelled,
    DicomReadTimings* const timings,
    const DicomReadGeometryPolicy geometryPolicy)
{
    return read(
        scan(filePaths, cancelled),
        progress,
        cancelled,
        timings,
        geometryPolicy);
}

std::shared_ptr<core::Volume> DicomReader::read(
    const std::vector<DicomFileRecord>& records,
    const std::function<void(double)>& progress,
    const CancelCheck& cancelled,
    DicomReadTimings* const timings,
    const DicomReadGeometryPolicy geometryPolicy)
{
    if(timings != nullptr)
    {
        *timings = {};
    }
    throwIfCancelled(cancelled);
    std::vector<std::size_t> allIndices(records.size());
    for(std::size_t index = 0; index < allIndices.size(); ++index)
    {
        allIndices[index] = index;
    }
    if(!isValidSingleSeriesSelection(records, allIndices))
    {
        throw std::runtime_error(
            "Selected DICOM files must be readable and belong to one series");
    }

    try
    {
        const auto orderedInput = orderedDicomInput(records, geometryPolicy);
        auto readResult =
            readParallelSlices(orderedInput.names, progress, cancelled);
        if(timings != nullptr)
        {
            timings->geometrySetupMilliseconds =
                readResult.geometrySetupMilliseconds;
            timings->pixelDecodeMilliseconds = readResult.pixelDecodeMilliseconds;
            timings->workerCount = readResult.workerCount;
        }
        throwIfCancelled(cancelled);
        const auto finalizeStart = Clock::now();
        auto volume = readResult.scalarRange
            ? std::make_shared<core::Volume>(
                  std::move(readResult.image), *readResult.scalarRange)
            : std::make_shared<core::Volume>(std::move(readResult.image));
        if(auto rgb = readPaletteColorDisplay(
               orderedInput.names,
               volume->image().GetLargestPossibleRegion().GetNumberOfPixels(),
               cancelled))
        {
            volume->setDisplayRgb(std::move(*rgb));
        }
        if(!orderedInput.sliceGapsMillimetres.empty())
        {
            volume->setDicomSliceGapsMillimetres(
                orderedInput.sliceGapsMillimetres);
        }
        if(timings != nullptr)
        {
            timings->volumeFinalizeMilliseconds =
                elapsedMilliseconds(finalizeStart);
        }
        throwIfCancelled(cancelled);
        const auto metadataStart = Clock::now();
        volume->setDicomMetadata(readDicomMetadata(orderedInput.names.front()));
        if(timings != nullptr)
        {
            timings->metadataMilliseconds = elapsedMilliseconds(metadataStart);
        }
        return volume;
    }
    catch(const itk::ExceptionObject& exception)
    {
        throw std::runtime_error(
            std::string("Unable to read DICOM series: ")
            + exception.GetDescription());
    }
}

} // namespace radmarky::io
