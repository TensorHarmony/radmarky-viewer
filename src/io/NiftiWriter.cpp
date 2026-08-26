#include "io/NiftiWriter.h"

#include "core/Annotation.h"

#include <itkImageFileWriter.h>
#include <itkNiftiImageIO.h>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

namespace radmarky::io
{
namespace
{

std::string pathForItk(const std::filesystem::path& path)
{
    const auto utf8 = path.u8string();
    return std::string(utf8.begin(), utf8.end());
}

bool isNiftiPath(const std::filesystem::path& path)
{
    std::string name = path.filename().string();
    for(char& character : name)
    {
        character = static_cast<char>(std::tolower(
            static_cast<unsigned char>(character)));
    }
    return name.ends_with(".nii") || name.ends_with(".nii.gz");
}

template<typename TLabel>
void writeConvertedLabelMap(
    const core::Annotation& annotation,
    const std::filesystem::path& filePath)
{
    using LabelImage = itk::Image<TLabel, 3>;
    const auto& source = annotation.volume().image();
    auto output = LabelImage::New();
    output->SetRegions(source.GetLargestPossibleRegion());
    output->CopyInformation(&source);
    output->Allocate();

    const auto pixelCount = source.GetLargestPossibleRegion().GetNumberOfPixels();
    const float* const sourceValues = source.GetBufferPointer();
    TLabel* const outputValues = output->GetBufferPointer();
    for(itk::SizeValueType offset = 0; offset < pixelCount; ++offset)
    {
        outputValues[offset] = static_cast<TLabel>(
            std::llround(sourceValues[offset]));
    }

    using Writer = itk::ImageFileWriter<LabelImage>;
    auto writer = Writer::New();
    writer->SetImageIO(itk::NiftiImageIO::New());
    writer->SetFileName(pathForItk(filePath));
    writer->SetInput(output);
    writer->UseCompressionOn();
    writer->SetCompressionLevel(100);
    writer->Update();
}

} // namespace

void NiftiWriter::writeLabelMap(
    const core::Annotation& annotation,
    const std::filesystem::path& filePath)
{
    if(annotation.kind() != core::AnnotationKind::LabelMap)
    {
        throw std::invalid_argument("Only label-map annotations can be saved");
    }
    if(filePath.empty() || !isNiftiPath(filePath))
    {
        throw std::invalid_argument(
            "Annotation output must use a .nii or .nii.gz extension");
    }
    const auto parent = filePath.parent_path();
    if(!parent.empty() && !std::filesystem::is_directory(parent))
    {
        throw std::runtime_error(
            "Annotation output directory does not exist: " + parent.string());
    }

    const auto& source = annotation.volume().image();
    const auto pixelCount = source.GetLargestPossibleRegion().GetNumberOfPixels();
    const float* const sourceValues = source.GetBufferPointer();
    double maximumLabel = 0.0;
    for(itk::SizeValueType offset = 0; offset < pixelCount; ++offset)
    {
        const double value = sourceValues[offset];
        if(!std::isfinite(value) || value < 0.0 || value > 65535.0
           || std::abs(value - std::round(value)) > 1.0e-4)
        {
            throw std::invalid_argument(
                "Label-map voxels must be finite integers from 0 to 65535");
        }
        maximumLabel = std::max(maximumLabel, value);
    }

    try
    {
        if(maximumLabel <= std::numeric_limits<std::uint8_t>::max())
        {
            writeConvertedLabelMap<std::uint8_t>(annotation, filePath);
        }
        else
        {
            writeConvertedLabelMap<std::uint16_t>(annotation, filePath);
        }
    }
    catch(const itk::ExceptionObject& exception)
    {
        throw std::runtime_error(
            "Unable to write NIfTI annotation '" + filePath.string()
            + "': " + exception.GetDescription());
    }
}

} // namespace radmarky::io
