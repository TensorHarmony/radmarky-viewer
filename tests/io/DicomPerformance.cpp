#include "io/DicomReader.h"

#include "core/Volume.h"
#include "io/DicomSeries.h"

#include <itkGDCMImageIO.h>
#include <itkImage.h>
#include <itkImageSeriesWriter.h>
#include <itkMetaDataObject.h>
#include <itkNumericSeriesFileNames.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <vector>

namespace
{

using Clock = std::chrono::steady_clock;

double millisecondsSince(const Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

std::vector<std::filesystem::path> discoverFiles(
    const std::filesystem::path& directory)
{
    if(!std::filesystem::is_directory(directory))
    {
        throw std::runtime_error("Benchmark input must be a directory");
    }
    std::vector<std::filesystem::path> paths;
    for(const auto& entry : std::filesystem::recursive_directory_iterator(directory))
    {
        if(entry.is_regular_file())
        {
            paths.push_back(entry.path());
        }
    }
    std::sort(paths.begin(), paths.end());
    if(paths.empty())
    {
        throw std::runtime_error("Benchmark directory contains no files");
    }
    return paths;
}

class TemporaryStudy
{
public:
    TemporaryStudy()
        : path_(
              std::filesystem::temp_directory_path()
              / ("radmarky-dicom-performance-"
                 + std::to_string(Clock::now().time_since_epoch().count())))
    {
        std::filesystem::create_directories(path_);
    }

    ~TemporaryStudy()
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return path_;
    }

private:
    std::filesystem::path path_;
};

void writeSyntheticStudy(const std::filesystem::path& directory)
{
    using Image = itk::Image<short, 3>;
    using Slice = itk::Image<short, 2>;
    auto image = Image::New();
    Image::RegionType region;
    region.SetSize({{256, 256, 128}});
    image->SetRegions(region);
    Image::SpacingType spacing;
    spacing[0] = 0.7;
    spacing[1] = 0.7;
    spacing[2] = 1.0;
    image->SetSpacing(spacing);
    Image::PointType origin;
    origin[0] = -90.0;
    origin[1] = -90.0;
    origin[2] = -64.0;
    image->SetOrigin(origin);
    image->Allocate();
    auto* const buffer = image->GetBufferPointer();
    const auto pixelCount = region.GetNumberOfPixels();
    for(std::size_t index = 0; index < pixelCount; ++index)
    {
        buffer[index] = static_cast<short>(index % 4096) - 1024;
    }

    auto names = itk::NumericSeriesFileNames::New();
    names->SetSeriesFormat((directory / "%04d.dcm").string());
    names->SetStartIndex(0);
    names->SetEndIndex(127);
    names->SetIncrementIndex(1);
    auto dicomIo = itk::GDCMImageIO::New();
    dicomIo->KeepOriginalUIDOn();
    auto& dictionary = dicomIo->GetMetaDataDictionary();
    itk::EncapsulateMetaData<std::string>(dictionary, "0008|0060", "CT");
    itk::EncapsulateMetaData<std::string>(
        dictionary, "0020|000d", "1.2.826.0.1.3680043.2.1125.7000");
    itk::EncapsulateMetaData<std::string>(
        dictionary, "0020|000e", "1.2.826.0.1.3680043.2.1125.7001");
    using Writer = itk::ImageSeriesWriter<Image, Slice>;
    auto writer = Writer::New();
    writer->SetInput(image);
    writer->SetImageIO(dicomIo);
    writer->SetFileNames(names->GetFileNames());
    writer->Update();
}

} // namespace

int main(const int argc, const char* const argv[])
{
    if(argc > 2)
    {
        std::cerr
            << "Usage: radmarky_dicom_performance [dicom-directory]\n"
               "With no directory, a 256x256x128 synthetic study is generated.\n";
        return 2;
    }

    try
    {
        std::optional<TemporaryStudy> temporaryStudy;
        std::filesystem::path inputDirectory;
        if(argc == 2)
        {
            inputDirectory = std::filesystem::path(argv[1]);
        }
        else
        {
            temporaryStudy.emplace();
            writeSyntheticStudy(temporaryStudy->path());
            inputDirectory = temporaryStudy->path();
        }
        const auto paths = discoverFiles(inputDirectory);
        const auto scanStart = Clock::now();
        const auto records = radmarky::io::DicomReader::scan(paths);
        const double scanMilliseconds = millisecondsSince(scanStart);
        const auto series = radmarky::io::analyzeDicomSeries(records);
        if(!series.proposedSeriesUid || series.defaultSelection.empty())
        {
            throw std::runtime_error(
                "Benchmark input has no unique largest DICOM series");
        }

        std::vector<radmarky::io::DicomFileRecord> selected;
        selected.reserve(series.defaultSelection.size());
        for(const auto index : series.defaultSelection)
        {
            selected.push_back(records[index]);
        }

        radmarky::io::DicomReadTimings timings;
        const auto readStart = Clock::now();
        const auto volume = radmarky::io::DicomReader::read(
            selected, {}, {}, &timings);
        const double readMilliseconds = millisecondsSince(readStart);
        const auto voxelCount =
            volume->image().GetLargestPossibleRegion().GetNumberOfPixels();
        const double decodedMegabytes =
            static_cast<double>(voxelCount * sizeof(float)) / (1024.0 * 1024.0);

        std::cout << std::fixed << std::setprecision(2)
                  << "files_discovered=" << paths.size() << '\n'
                  << "series_slices=" << selected.size() << '\n'
                  << "decoded_mib=" << decodedMegabytes << '\n'
                  << "scan_ms=" << scanMilliseconds << '\n'
                  << "geometry_setup_ms=" << timings.geometrySetupMilliseconds << '\n'
                  << "pixel_decode_ms=" << timings.pixelDecodeMilliseconds << '\n'
                  << "volume_finalize_ms=" << timings.volumeFinalizeMilliseconds << '\n'
                  << "metadata_ms=" << timings.metadataMilliseconds << '\n'
                  << "read_total_ms=" << readMilliseconds << '\n'
                  << "workers=" << timings.workerCount << '\n'
                  << "decode_mib_per_second="
                  << (timings.pixelDecodeMilliseconds > 0.0
                          ? decodedMegabytes * 1000.0
                              / timings.pixelDecodeMilliseconds
                          : 0.0)
                  << '\n';
        return 0;
    }
    catch(const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
