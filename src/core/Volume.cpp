#include "core/Volume.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace radmarky::core
{

Volume::Volume(ImageType::Pointer image)
    : image_(std::move(image))
    , geometry_(geometryFor(image_))
    , scalarRange_(scalarRangeFor(image_))
{
}

Volume::Volume(ImageType::Pointer image, const ScalarRange scalarRange)
    : image_(std::move(image))
    , geometry_(geometryFor(image_))
    , scalarRange_(scalarRange)
{
    if(!std::isfinite(scalarRange_.minimum)
       || !std::isfinite(scalarRange_.maximum)
       || scalarRange_.minimum > scalarRange_.maximum)
    {
        throw std::invalid_argument("Volume scalar range must be finite and ordered");
    }
}

Volume::ImageType& Volume::image() noexcept
{
    return *image_;
}

const Volume::ImageType& Volume::image() const noexcept
{
    return *image_;
}

const ImageGeometry& Volume::geometry() const noexcept
{
    return geometry_;
}

void Volume::setGeometry(const ImageGeometry& geometry)
{
    if(geometry.dimensions() != geometry_.dimensions())
    {
        throw std::invalid_argument(
            "Replacement volume geometry must have matching dimensions");
    }

    ImageType::SpacingType spacing;
    ImageType::PointType origin;
    ImageType::DirectionType direction;
    for(std::size_t row = 0; row < 3; ++row)
    {
        spacing[row] = geometry.spacing()[row];
        origin[row] = geometry.origin()[row];
        for(std::size_t column = 0; column < 3; ++column)
        {
            direction[row][column] = geometry.direction()[row][column];
        }
    }

    image_->SetSpacing(spacing);
    image_->SetOrigin(origin);
    image_->SetDirection(direction);
    geometry_ = ImageGeometry(*image_);
}

Volume::ScalarRange Volume::scalarRange() const noexcept
{
    return scalarRange_;
}

Volume::ScalarRange Volume::robustScalarRange() const
{
    // A bounded, spatially distributed sample keeps auto contrast responsive
    // for large volumes while retaining every voxel for smaller images.
    constexpr itk::SizeValueType maximumSamples = 1'000'000;
    constexpr double lowerPercentile = 0.005;
    constexpr double upperPercentile = 0.995;

    const auto* const buffer = image_->GetBufferPointer();
    const auto pixelCount =
        image_->GetLargestPossibleRegion().GetNumberOfPixels();
    const itk::SizeValueType blockSize = std::max<itk::SizeValueType>(
        1,
        pixelCount / maximumSamples
            + static_cast<itk::SizeValueType>(pixelCount % maximumSamples != 0));

    std::vector<float> samples;
    samples.reserve(
        static_cast<std::size_t>(std::min(pixelCount, maximumSamples)));
    for(itk::SizeValueType blockBegin = 0; blockBegin < pixelCount;)
    {
        const auto blockEnd = std::min(pixelCount, blockBegin + blockSize);
        for(auto index = blockBegin; index < blockEnd; ++index)
        {
            if(std::isfinite(buffer[index]))
            {
                samples.push_back(buffer[index]);
                break;
            }
        }
        blockBegin = blockEnd;
    }

    if(samples.empty())
    {
        // The explicit-range constructor can represent an image whose voxel
        // buffer has no finite values, so retain its validated display range.
        return scalarRange_;
    }

    std::sort(samples.begin(), samples.end());
    const double lastIndex = static_cast<double>(samples.size() - 1);
    const auto lowerIndex = static_cast<std::size_t>(
        std::floor(lowerPercentile * lastIndex));
    const auto upperIndex = static_cast<std::size_t>(
        std::ceil(upperPercentile * lastIndex));
    return {
        static_cast<double>(samples[lowerIndex]),
        static_cast<double>(samples[upperIndex]),
    };
}

bool Volume::hasDisplayRgb() const noexcept
{
    return !displayRgb_.empty();
}

const std::vector<std::uint8_t>& Volume::displayRgb() const noexcept
{
    return displayRgb_;
}

void Volume::setDisplayRgb(std::vector<std::uint8_t> rgb)
{
    const auto pixelCount =
        image_->GetLargestPossibleRegion().GetNumberOfPixels();
    if(pixelCount > std::numeric_limits<std::size_t>::max() / 3
       || rgb.size() != static_cast<std::size_t>(pixelCount) * 3)
    {
        throw std::invalid_argument(
            "RGB display data must contain three bytes per volume voxel");
    }
    displayRgb_ = std::move(rgb);
}

const std::vector<Volume::DicomMetadataEntry>& Volume::dicomMetadata() const
    noexcept
{
    return dicomMetadata_;
}

void Volume::setDicomMetadata(std::vector<DicomMetadataEntry> metadata)
{
    dicomMetadata_ = std::move(metadata);
}

const std::vector<double>& Volume::dicomSliceGapsMillimetres() const noexcept
{
    return dicomSliceGapsMillimetres_;
}

void Volume::setDicomSliceGapsMillimetres(std::vector<double> gaps)
{
    const auto sliceCount = geometry_.dimensions()[2];
    if(!gaps.empty()
       && (sliceCount < 2 || gaps.size() != sliceCount - 1
           || std::any_of(gaps.begin(), gaps.end(), [](const double gap) {
                  return !std::isfinite(gap) || gap <= 0.0;
              })))
    {
        throw std::invalid_argument(
            "DICOM slice gaps must contain one positive finite value between "
            "each pair of volume slices");
    }
    dicomSliceGapsMillimetres_ = std::move(gaps);
}

std::optional<Volume::VoxelSample> Volume::sampleNearestPhysical(
    const ImageGeometry::Vector& point) const
{
    const auto continuousIndex = geometry_.physicalToContinuousIndex(point);
    ImageType::IndexType imageIndex;
    VoxelSample sample;
    const auto& dimensions = geometry_.dimensions();
    for(std::size_t axis = 0; axis < 3; ++axis)
    {
        if(!std::isfinite(continuousIndex[axis]))
        {
            return std::nullopt;
        }
        const double rounded = std::round(continuousIndex[axis]);
        if(rounded < 0.0 || rounded >= static_cast<double>(dimensions[axis]))
        {
            return std::nullopt;
        }
        sample.index[axis] = static_cast<std::size_t>(rounded);
        imageIndex[axis] = static_cast<ImageType::IndexType::IndexValueType>(
            sample.index[axis]);
    }
    sample.value = image_->GetPixel(imageIndex);
    return sample;
}

std::optional<Volume::VoxelSample> Volume::sampleMeanPhysical(
    const ImageGeometry::Vector& point,
    const OrthogonalSliceGeometry& slice,
    const int samplingRadius) const
{
    auto center = sampleNearestPhysical(point);
    if(!center)
    {
        return std::nullopt;
    }
    const auto statistics = sampleStatisticsPhysical(point, slice, samplingRadius);
    center->value = statistics
        ? static_cast<float>(statistics->mean)
        : std::numeric_limits<float>::quiet_NaN();
    return center;
}

std::optional<Volume::IntensityStatistics> Volume::sampleStatisticsPhysical(
    const ImageGeometry::Vector& point,
    const OrthogonalSliceGeometry& slice,
    const int samplingRadius) const
{
    const auto center = sampleNearestPhysical(point);
    if(!center)
    {
        return std::nullopt;
    }
    const int sideLength =
        static_cast<int>(samplingSideLength(samplingRadius));

    const int firstOffset = -(sideLength - 1) / 2;
    const auto& horizontal = slice.horizontalDirectionLps();
    const auto& vertical = slice.verticalDirectionLps();
    ImageGeometry::Vector centerIndex{};
    for(std::size_t axis = 0; axis < 3; ++axis)
    {
        centerIndex[axis] = static_cast<double>(center->index[axis]);
    }
    const auto centerPoint = geometry_.indexToPhysical(centerIndex);
    const double horizontalStep =
        geometry_.physicalStepForOneVoxel(horizontal);
    const double verticalStep =
        geometry_.physicalStepForOneVoxel(vertical);
    std::vector<double> values;
    values.reserve(static_cast<std::size_t>(sideLength * sideLength));
    for(int row = 0; row < sideLength; ++row)
    {
        for(int column = 0; column < sideLength; ++column)
        {
            auto samplePoint = centerPoint;
            const double horizontalDistance =
                static_cast<double>(firstOffset + column) * horizontalStep;
            const double verticalDistance =
                static_cast<double>(firstOffset + row) * verticalStep;
            for(std::size_t axis = 0; axis < 3; ++axis)
            {
                samplePoint[axis] += horizontal[axis] * horizontalDistance
                    + vertical[axis] * verticalDistance;
            }
            const auto sample = sampleNearestPhysical(samplePoint);
            if(sample && std::isfinite(sample->value))
            {
                values.push_back(sample->value);
            }
        }
    }

    if(values.empty())
    {
        const double notANumber = std::numeric_limits<double>::quiet_NaN();
        return IntensityStatistics{
            notANumber, notANumber, notANumber, notANumber};
    }

    std::sort(values.begin(), values.end());
    const auto middle = values.size() / 2;
    const double median = values.size() % 2 == 0
        ? (values[middle - 1] + values[middle]) / 2.0
        : values[middle];
    const double sum = std::accumulate(values.begin(), values.end(), 0.0);
    return IntensityStatistics{
        values.back(),
        sum / static_cast<double>(values.size()),
        median,
        values.front()};
}

std::size_t Volume::samplingSideLength(const int samplingRadius)
{
    switch(samplingRadius)
    {
    case 1:
        return 1;
    case 2:
        return 3;
    case 3:
        return 5;
    case 4:
        return 9;
    case 5:
        return 17;
    default:
        throw std::invalid_argument("Sampling radius must be between 1 and 5");
    }
}

std::size_t Volume::samplingPixelCount(const int samplingRadius)
{
    const auto sideLength = samplingSideLength(samplingRadius);
    return sideLength * sideLength;
}

ImageGeometry Volume::geometryFor(const ImageType::Pointer& image)
{
    if(!image)
    {
        throw std::invalid_argument("Volume image cannot be null");
    }
    if(image->GetLargestPossibleRegion().GetNumberOfPixels() == 0)
    {
        throw std::invalid_argument("Volume image cannot be empty");
    }
    return ImageGeometry(*image);
}

Volume::ScalarRange Volume::scalarRangeFor(const ImageType::Pointer& image)
{
    const auto* const buffer = image->GetBufferPointer();
    const auto pixelCount = image->GetLargestPossibleRegion().GetNumberOfPixels();
    if(buffer == nullptr)
    {
        throw std::invalid_argument("Volume image buffer cannot be null");
    }

    bool foundFinite = false;
    ScalarRange range;
    for(itk::SizeValueType index = 0; index < pixelCount; ++index)
    {
        const double value = buffer[index];
        if(!std::isfinite(value))
        {
            continue;
        }
        if(!foundFinite)
        {
            range.minimum = value;
            range.maximum = value;
            foundFinite = true;
            continue;
        }
        range.minimum = std::min(range.minimum, value);
        range.maximum = std::max(range.maximum, value);
    }
    if(!foundFinite)
    {
        throw std::invalid_argument("Volume image contains no finite voxels");
    }
    return range;
}

} // namespace radmarky::core
