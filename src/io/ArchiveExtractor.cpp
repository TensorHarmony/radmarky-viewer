#include "io/ArchiveExtractor.h"

#include <archive.h>
#include <archive_entry.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>

namespace radmarky::io
{
namespace
{

constexpr std::size_t maximumEntryCount = 100000;
constexpr std::uint64_t maximumTotalBytes = 64ULL * 1024ULL * 1024ULL * 1024ULL;
constexpr std::size_t archiveReadBlockSize = 1024 * 1024;

struct ArchiveDeleter
{
    void operator()(archive* const handle) const noexcept
    {
        if(handle != nullptr)
        {
            archive_read_free(handle);
        }
    }
};

using ArchiveHandle = std::unique_ptr<archive, ArchiveDeleter>;

std::runtime_error archiveError(
    archive* const handle, const std::string& context)
{
    const char* const detail = archive_error_string(handle);
    return std::runtime_error(
        context + (detail == nullptr ? std::string{} : ": " + std::string(detail)));
}

bool isSafeRelativePath(const std::filesystem::path& path)
{
    if(path.empty() || path.is_absolute() || path.has_root_name()
       || path.has_root_directory())
    {
        return false;
    }
    for(const auto& component : path)
    {
        if(component == "..")
        {
            return false;
        }
    }
    return true;
}

std::filesystem::path entryPath(archive_entry& entry)
{
    const char* name = archive_entry_pathname_utf8(&entry);
    if(name == nullptr)
    {
        name = archive_entry_pathname(&entry);
    }
    if(name == nullptr)
    {
        return {};
    }
    std::string normalized(name);
    for(char& character : normalized)
    {
        if(character == '\\')
        {
            character = '/';
        }
    }
    const std::u8string utf8(
        reinterpret_cast<const char8_t*>(normalized.data()), normalized.size());
    return std::filesystem::path(utf8).lexically_normal();
}

} // namespace

std::vector<std::filesystem::path> ArchiveExtractor::extract(
    const std::filesystem::path& archivePath,
    const std::filesystem::path& destinationDirectory,
    const ProgressCallback& progress,
    const CancelCheck& cancelled)
{
    if(!std::filesystem::is_regular_file(archivePath))
    {
        throw std::runtime_error("Archive does not exist: " + archivePath.string());
    }
    if(cancelled && cancelled())
    {
        throw std::runtime_error("Archive extraction cancelled");
    }
    const auto archiveBytes = std::filesystem::file_size(archivePath);
    std::filesystem::create_directories(destinationDirectory);

    ArchiveHandle handle(archive_read_new());
    if(!handle)
    {
        throw std::runtime_error("Unable to allocate archive reader");
    }
    archive_read_support_filter_gzip(handle.get());
    archive_read_support_format_tar(handle.get());
    archive_read_support_format_zip(handle.get());

#ifdef _WIN32
    if(archive_read_open_filename_w(
           handle.get(), archivePath.c_str(), archiveReadBlockSize)
       != ARCHIVE_OK)
#else
    if(archive_read_open_filename(
           handle.get(), archivePath.c_str(), archiveReadBlockSize)
       != ARCHIVE_OK)
#endif
    {
        throw archiveError(handle.get(), "Unable to open archive");
    }

    std::vector<std::filesystem::path> files;
    std::size_t entryCount = 0;
    std::uint64_t totalBytes = 0;
    archive_entry* rawEntry = nullptr;
    int status = ARCHIVE_OK;
    int lastReportedPercent = -1;
    const auto reportProgress = [&]() {
        if(!progress || archiveBytes == 0)
        {
            return;
        }
        const auto compressedPosition = archive_position_compressed(handle.get());
        if(compressedPosition < 0)
        {
            return;
        }
        const double fraction = std::clamp(
            static_cast<double>(compressedPosition)
                / static_cast<double>(archiveBytes),
            0.0,
            1.0);
        const int percent = static_cast<int>(fraction * 100.0);
        if(percent != lastReportedPercent)
        {
            lastReportedPercent = percent;
            progress(fraction);
        }
    };
    while((status = archive_read_next_header(handle.get(), &rawEntry)) == ARCHIVE_OK)
    {
        if(cancelled && cancelled())
        {
            throw std::runtime_error("Archive extraction cancelled");
        }
        reportProgress();
        if(entryCount >= maximumEntryCount)
        {
            throw std::runtime_error("Archive contains too many entries");
        }
        ++entryCount;
        if(rawEntry == nullptr)
        {
            throw std::runtime_error("Archive returned an invalid entry");
        }
        const auto relativePath = entryPath(*rawEntry);
        if(!isSafeRelativePath(relativePath))
        {
            throw std::runtime_error(
                "Archive contains an unsafe path: " + relativePath.string());
        }
        if(archive_entry_symlink(rawEntry) != nullptr
           || archive_entry_hardlink(rawEntry) != nullptr)
        {
            throw std::runtime_error("Archive links are not permitted");
        }

        const auto outputPath = destinationDirectory / relativePath;
        const auto fileType = archive_entry_filetype(rawEntry);
        if(fileType == AE_IFDIR)
        {
            std::filesystem::create_directories(outputPath);
            continue;
        }
        if(fileType != AE_IFREG)
        {
            throw std::runtime_error("Archive contains an unsupported entry type");
        }

        std::filesystem::create_directories(outputPath.parent_path());
        std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
        if(!output)
        {
            throw std::runtime_error(
                "Unable to create extracted file: " + outputPath.string());
        }

        const void* block = nullptr;
        std::size_t blockSize = 0;
        la_int64_t offset = 0;
        std::uint64_t entryBytes = 0;
        while((status = archive_read_data_block(
                   handle.get(), &block, &blockSize, &offset)) == ARCHIVE_OK)
        {
            if(cancelled && cancelled())
            {
                throw std::runtime_error("Archive extraction cancelled");
            }
            if(offset < 0 || static_cast<std::uint64_t>(offset) != entryBytes)
            {
                throw std::runtime_error(
                    "Sparse archive entries are not permitted");
            }
            if(blockSize > maximumTotalBytes - totalBytes)
            {
                throw std::runtime_error("Archive expands beyond the safety limit");
            }
            output.write(
                static_cast<const char*>(block),
                static_cast<std::streamsize>(blockSize));
            if(!output)
            {
                throw std::runtime_error(
                    "Unable to write extracted file: " + outputPath.string());
            }
            entryBytes += blockSize;
            totalBytes += blockSize;
            reportProgress();
        }
        if(status != ARCHIVE_EOF)
        {
            throw archiveError(handle.get(), "Unable to extract archive entry");
        }
        files.push_back(outputPath);
    }
    if(status != ARCHIVE_EOF)
    {
        throw archiveError(handle.get(), "Unable to read archive");
    }
    if(files.empty())
    {
        throw std::runtime_error("Archive contains no files");
    }
    if(progress && lastReportedPercent < 100)
    {
        progress(1.0);
    }
    return files;
}

} // namespace radmarky::io
