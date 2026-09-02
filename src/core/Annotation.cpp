#include "core/Annotation.h"

#include "core/OrthogonalSliceGeometry.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace radmarky::core
{
namespace
{

constexpr double geometryTolerance = 1.0e-4;
constexpr double directionTolerance = 1.0e-5;

bool sliceContainsLabel(
    const Volume::ImageType& image,
    const ImageGeometry::Dimensions& dimensions,
    const std::size_t normalAxis,
    const std::size_t slice,
    const std::uint16_t label)
{
    const auto* const values = image.GetBufferPointer();
    const float expected = static_cast<float>(label);
    const std::size_t rowStride = dimensions[0];
    const std::size_t planeStride = dimensions[0] * dimensions[1];
    if(normalAxis == 2)
    {
        const auto* const first = values + slice * planeStride;
        return std::find(first, first + planeStride, expected)
            != first + planeStride;
    }
    for(std::size_t z = 0; z < dimensions[2]; ++z)
    {
        const std::size_t planeOffset = z * planeStride;
        if(normalAxis == 1)
        {
            const auto* const first = values + planeOffset + slice * rowStride;
            if(std::find(first, first + rowStride, expected)
               != first + rowStride)
            {
                return true;
            }
            continue;
        }
        for(std::size_t y = 0; y < dimensions[1]; ++y)
        {
            if(values[planeOffset + y * rowStride + slice] == expected)
            {
                return true;
            }
        }
    }
    return false;
}

bool requiresPatientAxialLabelGrid(const ImageGeometry& geometry)
{
    if(std::ranges::any_of(
           geometry.dimensions(), [](const std::size_t dimension) {
               return dimension == 1;
           }))
    {
        return false;
    }

    const auto& direction = geometry.direction();
    for(std::size_t column = 0; column < 3; ++column)
    {
        if(std::abs(direction[0][column]) <= directionTolerance
           && std::abs(direction[1][column]) <= directionTolerance
           && std::abs(std::abs(direction[2][column]) - 1.0)
               <= directionTolerance)
        {
            bool inPlaneAxesStayAxial = true;
            for(std::size_t other = 0; other < 3; ++other)
            {
                if(other != column
                   && std::abs(direction[2][other]) > directionTolerance)
                {
                    inPlaneAxesStayAxial = false;
                }
            }
            if(inPlaneAxesStayAxial)
            {
                return false;
            }
        }
    }
    return true;
}

Volume::ImageType::Pointer labelMapGridImage(
    const ImageGeometry& primaryGeometry)
{
    using Image = Volume::ImageType;
    auto image = Image::New();
    Image::SizeType size;
    Image::SpacingType spacing;
    Image::PointType origin;
    Image::DirectionType direction;

    if(!requiresPatientAxialLabelGrid(primaryGeometry))
    {
        for(std::size_t axis = 0; axis < 3; ++axis)
        {
            size[axis] = primaryGeometry.dimensions()[axis];
            spacing[axis] = primaryGeometry.spacing()[axis];
            origin[axis] = primaryGeometry.origin()[axis];
            for(std::size_t column = 0; column < 3; ++column)
            {
                direction[axis][column] =
                    primaryGeometry.direction()[axis][column];
            }
        }
    }
    else
    {
        const auto axial = OrthogonalSliceGeometry::fromImageGeometry(
            primaryGeometry, SliceOrientation::Axial);
        const double sliceIntervals = std::round(
            (axial.normalMaximum() - axial.normalMinimum())
            / axial.sliceStep());
        if(!std::isfinite(sliceIntervals) || sliceIntervals < 0.0
           || sliceIntervals
               >= static_cast<double>(
                   std::numeric_limits<Image::SizeType::SizeValueType>::max()))
        {
            throw std::overflow_error(
                "Patient-axial label grid dimensions are invalid");
        }
        size[0] = axial.width();
        size[1] = axial.height();
        size[2] = static_cast<Image::SizeType::SizeValueType>(sliceIntervals) + 1;
        spacing[0] = axial.outputSpacing();
        spacing[1] = axial.outputSpacing();
        spacing[2] = axial.sliceStep();

        auto normalOrigin = axial.referenceLps();
        for(std::size_t axis = 0; axis < 3; ++axis)
        {
            normalOrigin[axis] += axial.normalDirectionLps()[axis]
                * axial.normalMinimum();
        }
        const auto physicalOrigin = axial.pointOnCursorPlane(
            axial.horizontalMinimum(), axial.verticalMinimum(), normalOrigin);
        for(std::size_t row = 0; row < 3; ++row)
        {
            origin[row] = physicalOrigin[row];
            direction[row][0] = axial.horizontalDirectionLps()[row];
            direction[row][1] = axial.verticalDirectionLps()[row];
            direction[row][2] = axial.normalDirectionLps()[row];
        }
    }

    Image::RegionType region;
    region.SetSize(size);
    image->SetRegions(region);
    image->SetSpacing(spacing);
    image->SetOrigin(origin);
    image->SetDirection(direction);
    return image;
}

Volume::ImageType::Pointer resampleLabelMap(
    Volume::ImageType& source,
    const ImageGeometry& primaryGeometry)
{
    using Image = Volume::ImageType;
    auto output = labelMapGridImage(primaryGeometry);
    output->Allocate();
    output->FillBuffer(0.0F);

    const ImageGeometry sourceGeometry(source);
    const ImageGeometry outputGeometry(*output);
    const auto& sourceDimensions = sourceGeometry.dimensions();
    const auto& outputDimensions = outputGeometry.dimensions();
    const auto* const sourceValues = source.GetBufferPointer();
    auto* const outputValues = output->GetBufferPointer();
    const ImageGeometry::Vector outputZero{{0.0, 0.0, 0.0}};
    const auto sourceBase = sourceGeometry.physicalToContinuousIndex(
        outputGeometry.indexToPhysical(outputZero));
    std::array<ImageGeometry::Vector, 3> sourceSteps{};
    for(std::size_t outputAxis = 0; outputAxis < 3; ++outputAxis)
    {
        auto unit = outputZero;
        unit[outputAxis] = 1.0;
        const auto sourceAtUnit = sourceGeometry.physicalToContinuousIndex(
            outputGeometry.indexToPhysical(unit));
        for(std::size_t sourceAxis = 0; sourceAxis < 3; ++sourceAxis)
        {
            sourceSteps[outputAxis][sourceAxis] =
                sourceAtUnit[sourceAxis] - sourceBase[sourceAxis];
        }
    }
    const std::size_t workerCount = std::max<std::size_t>(
        1,
        std::min<std::size_t>(
            outputDimensions[2], std::thread::hardware_concurrency()));
    std::vector<std::thread> workers;
    workers.reserve(workerCount);
    for(std::size_t worker = 0; worker < workerCount; ++worker)
    {
        const std::size_t firstZ = outputDimensions[2] * worker / workerCount;
        const std::size_t lastZ =
            outputDimensions[2] * (worker + 1) / workerCount;
        workers.emplace_back([&, firstZ, lastZ] {
            for(std::size_t z = firstZ; z < lastZ; ++z)
            {
                for(std::size_t y = 0; y < outputDimensions[1]; ++y)
                {
                    for(std::size_t x = 0; x < outputDimensions[0]; ++x)
                    {
                        std::array<long, 3> sourceIndex{};
                        bool inside = true;
                        for(std::size_t axis = 0; axis < 3; ++axis)
                        {
                            const double sourceContinuous = sourceBase[axis]
                                + static_cast<double>(x) * sourceSteps[0][axis]
                                + static_cast<double>(y) * sourceSteps[1][axis]
                                + static_cast<double>(z) * sourceSteps[2][axis];
                            sourceIndex[axis] = std::lround(sourceContinuous);
                            inside = inside && sourceIndex[axis] >= 0
                                && sourceIndex[axis]
                                    < static_cast<long>(sourceDimensions[axis]);
                        }
                        if(!inside)
                        {
                            continue;
                        }
                        const std::size_t sourceOffset =
                            static_cast<std::size_t>(sourceIndex[0])
                            + sourceDimensions[0]
                                * (static_cast<std::size_t>(sourceIndex[1])
                                   + sourceDimensions[1]
                                       * static_cast<std::size_t>(sourceIndex[2]));
                        const std::size_t outputOffset = x + outputDimensions[0]
                            * (y + outputDimensions[1] * z);
                        outputValues[outputOffset] = sourceValues[sourceOffset];
                    }
                }
            }
        });
    }
    for(auto& worker : workers)
    {
        worker.join();
    }
    return output;
}

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
    auto image = labelMapGridImage(primaryVolume.geometry());
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

std::optional<ImageGeometry::Vector>
Annotation::nearestAxialSlicePointContainingLabel(
    const std::uint16_t label,
    const ImageGeometry::Vector& currentPhysical) const
{
    if(kind_ != AnnotationKind::LabelMap || label == 0
       || !labelVoxelCounts_.contains(label))
    {
        return std::nullopt;
    }

    const auto& geometry = volume_->geometry();
    const auto& direction = geometry.direction();
    std::size_t normalAxis = 0;
    double normalAlignment = 0.0;
    for(std::size_t axis = 0; axis < 3; ++axis)
    {
        const double alignment = std::abs(direction[2][axis]);
        if(alignment > normalAlignment)
        {
            normalAlignment = alignment;
            normalAxis = axis;
        }
    }
    if(normalAlignment < 1.0 - directionTolerance)
    {
        return std::nullopt;
    }

    auto currentIndex = geometry.physicalToContinuousIndex(currentPhysical);
    if(!std::isfinite(currentIndex[normalAxis]))
    {
        return std::nullopt;
    }
    const auto& dimensions = geometry.dimensions();
    const auto lastSlice = static_cast<long long>(dimensions[normalAxis] - 1);
    const auto currentSlice = std::clamp(
        static_cast<long long>(std::llround(currentIndex[normalAxis])),
        0LL,
        lastSlice);
    std::optional<std::size_t> nearestSlice;
    for(long long distance = 0; distance <= lastSlice; ++distance)
    {
        const long long lower = currentSlice - distance;
        if(lower >= 0
           && sliceContainsLabel(
               volume_->image(),
               dimensions,
               normalAxis,
               static_cast<std::size_t>(lower),
               label))
        {
            nearestSlice = static_cast<std::size_t>(lower);
            break;
        }
        const long long upper = currentSlice + distance;
        if(distance > 0 && upper <= lastSlice
           && sliceContainsLabel(
               volume_->image(),
               dimensions,
               normalAxis,
               static_cast<std::size_t>(upper),
               label))
        {
            nearestSlice = static_cast<std::size_t>(upper);
            break;
        }
    }
    if(!nearestSlice)
    {
        return std::nullopt;
    }
    currentIndex[normalAxis] = static_cast<double>(*nearestSlice);
    return geometry.indexToPhysical(currentIndex);
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
    if(kind_ == AnnotationKind::LabelMap)
    {
        try
        {
            verifyGeometry(primaryGeometry);
            return;
        }
        catch(const std::invalid_argument&)
        {
            // A voxel-indexed label map with primary dimensions may have an
            // absent or unreliable physical header. Apply the primary header
            // before converting an oblique acquisition to the editing grid.
        }
        if(volume_->geometry().dimensions() != primaryGeometry.dimensions())
        {
            throw std::invalid_argument(
                "Annotation geometry does not match the anatomical image: dimensions");
        }
        volume_->setGeometry(primaryGeometry);
        if(requiresPatientAxialLabelGrid(primaryGeometry))
        {
            volume_ = std::make_shared<Volume>(
                resampleLabelMap(volume_->image(), primaryGeometry));
            validateLabelValuesAndBuildLedger();
        }
    }
    else if(volume_->geometry().dimensions() != primaryGeometry.dimensions())
    {
        throw std::invalid_argument(
            "Annotation geometry does not match the anatomical image: dimensions");
    }
    verifyGeometry(primaryGeometry);
}

void Annotation::verifyGeometry(const ImageGeometry& primaryGeometry) const
{
    const auto& annotationGeometry = volume_->geometry();
    const auto expectedImage = kind_ == AnnotationKind::LabelMap
        ? labelMapGridImage(primaryGeometry) : nullptr;
    const ImageGeometry expectedGeometry = expectedImage
        ? ImageGeometry(*expectedImage) : primaryGeometry;
    if(annotationGeometry.dimensions() != expectedGeometry.dimensions())
    {
        throw std::invalid_argument(
            "Annotation geometry does not match the anatomical image: dimensions");
    }
    for(std::size_t axis = 0; axis < 3; ++axis)
    {
        if(!nearlyEqual(
               annotationGeometry.spacing()[axis],
               expectedGeometry.spacing()[axis],
               geometryTolerance))
        {
            geometryMismatch("spacing", axis);
        }
        if(!nearlyEqual(
               annotationGeometry.origin()[axis],
               expectedGeometry.origin()[axis],
               geometryTolerance))
        {
            geometryMismatch("origin", axis);
        }
        for(std::size_t column = 0; column < 3; ++column)
        {
            if(!nearlyEqual(
                   annotationGeometry.direction()[axis][column],
                   expectedGeometry.direction()[axis][column],
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
