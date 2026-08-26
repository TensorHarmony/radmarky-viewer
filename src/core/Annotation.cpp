#include "core/Annotation.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace radmarky::core
{
namespace
{

constexpr double geometryTolerance = 1.0e-4;
constexpr double directionTolerance = 1.0e-5;

bool nearlyEqual(const double left, const double right, const double tolerance)
{
    const double scale = std::max({1.0, std::abs(left), std::abs(right)});
    return std::abs(left - right) <= tolerance * scale;
}

[[noreturn]] void geometryMismatch(
    const char* const field,
    const std::size_t first,
    const std::size_t second = 3)
{
    std::ostringstream message;
    message << "Annotation geometry does not match the anatomical image: "
            << field << '[' << first;
    if(second < 3)
    {
        message << "," << second;
    }
    message << ']';
    throw std::invalid_argument(message.str());
}

} // namespace

Annotation::Annotation(
    std::string name,
    std::filesystem::path sourcePath,
    std::shared_ptr<Volume> volume,
    const AnnotationKind kind)
    : name_(std::move(name))
    , sourcePath_(std::move(sourcePath))
    , volume_(std::move(volume))
    , kind_(kind)
{
    if(name_.empty())
    {
        throw std::invalid_argument("Annotation name cannot be empty");
    }
    if(!volume_)
    {
        throw std::invalid_argument("Annotation volume cannot be null");
    }
    if(kind_ == AnnotationKind::LabelMap)
    {
        validateLabelValuesAndBuildLedger();
    }
}

std::shared_ptr<Annotation> Annotation::createBlankLabelMap(
    std::string name, const Volume& primaryVolume)
{
    auto image = Volume::ImageType::New();
    image->SetRegions(primaryVolume.image().GetLargestPossibleRegion());
    image->CopyInformation(&primaryVolume.image());
    image->Allocate();
    image->FillBuffer(0.0F);
    auto annotation = std::make_shared<Annotation>(
        std::move(name), std::filesystem::path{},
        std::make_shared<Volume>(std::move(image)), AnnotationKind::LabelMap);
    annotation->markModified();
    return annotation;
}

const std::string& Annotation::name() const noexcept
{
    return name_;
}

void Annotation::setName(std::string name)
{
    if(name.empty())
    {
        throw std::invalid_argument("Annotation name cannot be empty");
    }
    name_ = std::move(name);
}

const std::filesystem::path& Annotation::sourcePath() const noexcept
{
    return sourcePath_;
}

void Annotation::setSourcePath(std::filesystem::path sourcePath)
{
    sourcePath_ = std::move(sourcePath);
}

bool Annotation::isModified() const noexcept
{
    return modified_;
}

void Annotation::markSaved() noexcept
{
    modified_ = false;
}

const Volume& Annotation::volume() const noexcept
{
    return *volume_;
}

AnnotationKind Annotation::kind() const noexcept
{
    return kind_;
}

std::vector<std::uint16_t> Annotation::labelValues() const
{
    std::vector<std::uint16_t> labels;
    labels.reserve(labelVoxelCounts_.size());
    for(const auto& [label, count] : labelVoxelCounts_)
    {
        if(label != 0 && count > 0)
        {
            labels.push_back(label);
        }
    }
    std::sort(labels.begin(), labels.end());
    return labels;
}

double Annotation::opacity() const noexcept
{
    return opacity_;
}

Volume& Annotation::editableVolume() noexcept
{
    return *volume_;
}

void Annotation::setOpacity(const double opacity)
{
    if(!std::isfinite(opacity) || opacity < 0.0 || opacity > 1.0)
    {
        throw std::invalid_argument("Annotation opacity must be between 0 and 1");
    }
    opacity_ = opacity;
}

bool Annotation::isVisible() const noexcept
{
    return visible_;
}

void Annotation::setVisible(const bool visible) noexcept
{
    visible_ = visible;
}

void Annotation::conformGeometry(const ImageGeometry& primaryGeometry)
{
    if(volume_->geometry().dimensions() != primaryGeometry.dimensions())
    {
        throw std::invalid_argument(
            "Annotation geometry does not match the anatomical image: dimensions");
    }
    if(kind_ == AnnotationKind::LabelMap)
    {
        volume_->setGeometry(primaryGeometry);
    }
    verifyGeometry(primaryGeometry);
}

void Annotation::verifyGeometry(const ImageGeometry& primaryGeometry) const
{
    const auto& annotationGeometry = volume_->geometry();
    if(annotationGeometry.dimensions() != primaryGeometry.dimensions())
    {
        throw std::invalid_argument(
            "Annotation geometry does not match the anatomical image: dimensions");
    }
    for(std::size_t axis = 0; axis < 3; ++axis)
    {
        if(!nearlyEqual(
               annotationGeometry.spacing()[axis],
               primaryGeometry.spacing()[axis],
               geometryTolerance))
        {
            geometryMismatch("spacing", axis);
        }
        if(!nearlyEqual(
               annotationGeometry.origin()[axis],
               primaryGeometry.origin()[axis],
               geometryTolerance))
        {
            geometryMismatch("origin", axis);
        }
        for(std::size_t column = 0; column < 3; ++column)
        {
            if(!nearlyEqual(
                   annotationGeometry.direction()[axis][column],
                   primaryGeometry.direction()[axis][column],
                   directionTolerance))
            {
                geometryMismatch("direction", axis, column);
            }
        }
    }
}

void Annotation::validateLabelValuesAndBuildLedger()
{
    labelVoxelCounts_.clear();
    const auto& image = volume_->image();
    const auto pixelCount = image.GetLargestPossibleRegion().GetNumberOfPixels();
    const auto* const values = image.GetBufferPointer();
    for(itk::SizeValueType index = 0; index < pixelCount; ++index)
    {
        const double value = values[index];
        if(!std::isfinite(value) || value < 0.0 || value > 65535.0
           || std::abs(value - std::round(value)) > 1.0e-4)
        {
            throw std::invalid_argument(
                "Label-map voxels must be finite integers from 0 to 65535");
        }
        const auto label = static_cast<std::uint16_t>(std::llround(value));
        if(label != 0)
        {
            ++labelVoxelCounts_[label];
        }
    }
}

void Annotation::updateLabelLedger(
    const std::uint16_t before, const std::uint16_t after)
{
    if(before == after)
    {
        return;
    }
    if(before != 0)
    {
        const auto found = labelVoxelCounts_.find(before);
        if(found == labelVoxelCounts_.end() || found->second == 0)
        {
            throw std::logic_error("Annotation label ledger is inconsistent");
        }
        if(--found->second == 0)
        {
            labelVoxelCounts_.erase(found);
        }
    }
    if(after != 0)
    {
        ++labelVoxelCounts_[after];
    }
}

void Annotation::markModified() noexcept
{
    modified_ = true;
}

} // namespace radmarky::core
