#include "io/ArchiveExtractor.h"

#include <archive.h>
#include <archive_entry.h>

#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{

struct ArchiveWriter
{
    archive* handle = archive_write_new();

    ~ArchiveWriter()
    {
        if(handle != nullptr)
        {
            archive_write_close(handle);
            archive_write_free(handle);
        }
    }
};

void writeArchive(
    const std::filesystem::path& archivePath,
    const bool zip,
    const std::vector<std::pair<std::string, std::string>>& entries)
{
    ArchiveWriter writer;
    if(writer.handle == nullptr)
    {
        throw std::runtime_error("Unable to allocate archive writer");
    }
    if(zip)
    {
        archive_write_set_format_zip(writer.handle);
    }
    else
    {
        archive_write_add_filter_gzip(writer.handle);
        archive_write_set_format_pax_restricted(writer.handle);
    }
#ifdef _WIN32
    if(archive_write_open_filename_w(writer.handle, archivePath.c_str()) != ARCHIVE_OK)
#else
    if(archive_write_open_filename(writer.handle, archivePath.c_str()) != ARCHIVE_OK)
#endif
    {
        throw std::runtime_error("Unable to open archive test output");
    }

    for(const auto& [name, contents] : entries)
    {
        archive_entry* const entry = archive_entry_new();
        if(entry == nullptr)
        {
            throw std::runtime_error("Unable to allocate archive entry");
        }
        archive_entry_set_pathname(entry, name.c_str());
        archive_entry_set_filetype(entry, AE_IFREG);
        archive_entry_set_perm(entry, 0644);
        archive_entry_set_size(entry, static_cast<la_int64_t>(contents.size()));
        const int headerStatus = archive_write_header(writer.handle, entry);
        if(headerStatus != ARCHIVE_OK)
        {
            archive_entry_free(entry);
            throw std::runtime_error("Unable to write archive entry header");
        }
        if(archive_write_data(writer.handle, contents.data(), contents.size()) < 0)
        {
            archive_entry_free(entry);
            throw std::runtime_error("Unable to write archive entry data");
        }
        archive_entry_free(entry);
    }
}

std::string readText(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return std::string(
        std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
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

bool expectThrows(const auto& action, const std::string_view field)
{
    try
    {
        action();
        std::cerr << field << ": expected exception\n";
        return false;
    }
    catch(const std::exception&)
    {
        return true;
    }
}

} // namespace

int main()
{
    const auto unique = std::chrono::high_resolution_clock::now()
                            .time_since_epoch()
                            .count();
    const auto root = std::filesystem::temp_directory_path()
        / ("radmarky-archive-test-" + std::to_string(unique));
    std::filesystem::create_directories(root);

    bool passed = true;
    try
    {
        const auto zipPath = root / "dicom.zip";
        writeArchive(
            zipPath,
            true,
            {{"series/one.dcm", "first"}, {"series/two.dcm", "second"}});
        const auto zipOutput = root / "zip-output";
        const auto zipFiles =
            radmarky::io::ArchiveExtractor::extract(zipPath, zipOutput);
        passed &= expectTrue(zipFiles.size() == 2, "ZIP file count");
        passed &= expectTrue(
            readText(zipOutput / "series/one.dcm") == "first",
            "nested ZIP contents");
        passed &= expectThrows(
            [&] {
                (void)radmarky::io::ArchiveExtractor::extract(
                    zipPath,
                    root / "cancelled-output",
                    {},
                    [] { return true; });
            },
            "archive cancellation honored");

        const auto tarPath = root / "dicom.tar.gz";
        writeArchive(
            tarPath,
            false,
            {{"one.dcm", "alpha"}, {"two.dcm", "beta"}});
        const auto tarOutput = root / "tar-output";
        const auto tarFiles =
            radmarky::io::ArchiveExtractor::extract(tarPath, tarOutput);
        passed &= expectTrue(tarFiles.size() == 2, "TAR.GZ file count");
        passed &= expectTrue(
            readText(tarOutput / "two.dcm") == "beta",
            "TAR.GZ contents");

        const auto unsafePath = root / "unsafe.zip";
        writeArchive(unsafePath, true, {{"../escape.dcm", "unsafe"}});
        passed &= expectThrows(
            [&] {
                (void)radmarky::io::ArchiveExtractor::extract(
                    unsafePath, root / "unsafe-output");
            },
            "archive traversal rejected");
        passed &= expectTrue(
            !std::filesystem::exists(root / "escape.dcm"),
            "archive traversal writes nothing outside destination");

        const auto corruptPath = root / "corrupt.zip";
        {
            std::ofstream corrupt(corruptPath, std::ios::binary);
            corrupt << "This is not an archive";
        }
        passed &= expectThrows(
            [&] {
                (void)radmarky::io::ArchiveExtractor::extract(
                    corruptPath, root / "corrupt-output");
            },
            "corrupt archive rejected");
    }
    catch(const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        passed = false;
    }
    std::filesystem::remove_all(root);
    return passed ? 0 : 1;
}
