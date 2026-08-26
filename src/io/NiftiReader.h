#pragma once

#include <filesystem>
#include <functional>
#include <memory>

namespace radmarky::core
{
class Volume;
}

namespace radmarky::io
{

class NiftiReader
{
public:
    enum class ComponentKind
    {
        Integer,
        FloatingPoint,
    };

    [[nodiscard]] static ComponentKind componentKind(
        const std::filesystem::path& filePath);
    [[nodiscard]] static std::shared_ptr<core::Volume> read(
        const std::filesystem::path& filePath,
        const std::function<void(double)>& progress = {});
};

} // namespace radmarky::io
