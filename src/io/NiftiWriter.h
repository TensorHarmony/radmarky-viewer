#pragma once

#include <filesystem>

namespace radmarky::core
{
class Annotation;
}

namespace radmarky::io
{

class NiftiWriter
{
public:
    static void writeLabelMap(
        const core::Annotation& annotation,
        const std::filesystem::path& filePath);
};

} // namespace radmarky::io
