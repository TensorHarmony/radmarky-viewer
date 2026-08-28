#pragma once

#include "io/DicomSeries.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <cstddef>
#include <vector>

namespace radmarky::core
{
class Volume;
}

namespace radmarky::io
{

struct DicomReadTimings
{
    double geometrySetupMilliseconds = 0.0;
    double pixelDecodeMilliseconds = 0.0;
    double volumeFinalizeMilliseconds = 0.0;
    double metadataMilliseconds = 0.0;
    std::size_t workerCount = 1;
};

enum class DicomReadGeometryPolicy
{
    Strict,
    AllowSliceSpacingOverride,
};

class DicomReader
{
public:
    using CancelCheck = std::function<bool()>;

    [[nodiscard]] static std::vector<DicomFileRecord> scan(
        const std::vector<std::filesystem::path>& filePaths,
        const CancelCheck& cancelled = {});

    [[nodiscard]] static std::shared_ptr<core::Volume> read(
        const std::vector<std::filesystem::path>& filePaths,
        const std::function<void(double)>& progress = {},
        const CancelCheck& cancelled = {},
        DicomReadTimings* timings = nullptr,
        DicomReadGeometryPolicy geometryPolicy = DicomReadGeometryPolicy::Strict);

    [[nodiscard]] static std::shared_ptr<core::Volume> read(
        const std::vector<DicomFileRecord>& records,
        const std::function<void(double)>& progress = {},
        const CancelCheck& cancelled = {},
        DicomReadTimings* timings = nullptr,
        DicomReadGeometryPolicy geometryPolicy = DicomReadGeometryPolicy::Strict);
};

} // namespace radmarky::io
