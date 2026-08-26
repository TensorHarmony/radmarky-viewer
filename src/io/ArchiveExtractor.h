#pragma once

#include <filesystem>
#include <functional>
#include <vector>

namespace radmarky::io
{

class ArchiveExtractor
{
public:
    using ProgressCallback = std::function<void(double)>;
    using CancelCheck = std::function<bool()>;

    [[nodiscard]] static std::vector<std::filesystem::path> extract(
        const std::filesystem::path& archivePath,
        const std::filesystem::path& destinationDirectory,
        const ProgressCallback& progress = {},
        const CancelCheck& cancelled = {});
};

} // namespace radmarky::io
