#include "core/OrthogonalSliceGeometry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace radmarky::core
{
namespace
{

double dot(const ImageGeometry::Vector& left, const ImageGeometry::Vector& right)
{
    return left[0] * right[0] + left[1] * right[1] + left[2] * right[2];
}

ImageGeometry::Vector subtract(
    const ImageGeometry::Vector& left,
    const ImageGeometry::Vector& right)
{
    return {{left[0] - right[0], left[1] - right[1], left[2] - right[2]}};
}

ImageGeometry::Vector addScaled(
    ImageGeometry::Vector result,
    const ImageGeometry::Vector& direction,
    const double scale)
{
    for(std::size_t axis = 0; axis < 3; ++axis)
    {
        result[axis] += direction[axis] * scale;
    }
    return result;
}

std::array<std::size_t, 3> closestImageAxesByLpsAxis(
    const ImageGeometry& geometry)
{
    constexpr std::array<std::array<std::size_t, 3>, 6> permutations{{
        {{0, 1, 2}},
        {{0, 2, 1}},
        {{1, 0, 2}},
        {{1, 2, 0}},
        {{2, 0, 1}},
        {{2, 1, 0}},
    }};

    const auto& direction = geometry.direction();
    auto best = permutations.front();
    double bestAlignment = -1.0;
    for(const auto& candidate : permutations)
    {
        double alignment = 0.0;
        for(std::size_t lpsAxis = 0; lpsAxis < 3; ++lpsAxis)
        {
            alignment += std::abs(direction[lpsAxis][candidate[lpsAxis]]);
        }
        if(alignment > bestAlignment)
        {
            best = candidate;
            bestAlignment = alignment;
        }
    }
    return best;
}

std::size_t dominantAxis(const ImageGeometry::Vector& direction)
{
    std::size_t result = 0;
    for(std::size_t axis = 1; axis < 3; ++axis)
    {
        if(std::abs(direction[axis]) > std::abs(direction[result]))
        {
            result = axis;
        }
    }
    return result;
}

ImageGeometry::Vector nativeDirectionClosestTo(
    const ImageGeometry& geometry,
    const std::array<std::size_t, 3>& imageAxisByLpsAxis,
    const ImageGeometry::Vector& target)
{
    const auto lpsAxis = dominantAxis(target);

    ImageGeometry::Vector result{};
    const auto& direction = geometry.direction();
    const auto imageAxis = imageAxisByLpsAxis[lpsAxis];
    double lengthSquared = 0.0;
    for(std::size_t axis = 0; axis < 3; ++axis)
    {
        result[axis] = direction[axis][imageAxis];
        lengthSquared += result[axis] * result[axis];
    }
    const double length = std::sqrt(lengthSquared);
    if(!std::isfinite(length) || length <= 1.0e-12)
    {
        throw std::invalid_argument("Image direction cannot define native slices");
    }
    for(auto& value : result)
    {
        value /= length;
    }
    if(dot(result, target) < 0.0)
    {
        for(auto& value : result)
        {
            value = -value;
        }
    }
    return result;
}

std::size_t sampleCountForExtent(const double extent, const double spacing)
{
    double intervals = extent / spacing;
    const double nearestInteger = std::round(intervals);
    const double tolerance =
        1.0e-9 * std::max(1.0, std::abs(nearestInteger));
    if(std::abs(intervals - nearestInteger) <= tolerance)
    {
        intervals = nearestInteger;
    }
    return static_cast<std::size_t>(std::ceil(intervals)) + 1;
}

} // namespace

OrthogonalSliceGeometry OrthogonalSliceGeometry::fromImageGeometry(
    const ImageGeometry& geometry,
    const SliceOrientation orientation)
{
    OrthogonalSliceGeometry result;
    result.orientation_ = orientation;

    switch(orientation)
    {
    case SliceOrientation::Axial:
        // Radiological axial: right at screen left, anterior at the top.
        result.horizontalDirectionLps_ = {{1.0, 0.0, 0.0}};
        result.verticalDirectionLps_ = {{0.0, -1.0, 0.0}};
        result.normalDirectionLps_ = {{0.0, 0.0, -1.0}};
        break;
    case SliceOrientation::Sagittal:
        // Anterior at screen left, superior at the top.
        result.horizontalDirectionLps_ = {{0.0, 1.0, 0.0}};
        result.verticalDirectionLps_ = {{0.0, 0.0, 1.0}};
        result.normalDirectionLps_ = {{1.0, 0.0, 0.0}};
        break;
    case SliceOrientation::Coronal:
        // Patient right at screen left, superior at the top.
        result.horizontalDirectionLps_ = {{1.0, 0.0, 0.0}};
        result.verticalDirectionLps_ = {{0.0, 0.0, 1.0}};
        result.normalDirectionLps_ = {{0.0, -1.0, 0.0}};
        break;
    }

    const auto& dimensions = geometry.dimensions();
    const auto imageAxisByLpsAxis = closestImageAxesByLpsAxis(geometry);
    const bool useNativeDisplayAxes = std::any_of(
        dimensions.begin(), dimensions.end(), [](const std::size_t size) {
            return size == 1;
        });
    std::size_t horizontalImageAxis = 3;
    std::size_t verticalImageAxis = 3;
    if(useNativeDisplayAxes)
    {
        horizontalImageAxis = imageAxisByLpsAxis[dominantAxis(
            result.horizontalDirectionLps_)];
        verticalImageAxis = imageAxisByLpsAxis[dominantAxis(
            result.verticalDirectionLps_)];

        // A singleton dimension is an acquisition plane rather than a sampled
        // 3-D volume. Keep all three 2-D panes on the closest native voxel axes
        // to avoid a slanted patient-space intersection.
        result.horizontalDirectionLps_ = nativeDirectionClosestTo(
            geometry, imageAxisByLpsAxis, result.horizontalDirectionLps_);
        result.verticalDirectionLps_ = nativeDirectionClosestTo(
            geometry, imageAxisByLpsAxis, result.verticalDirectionLps_);
        result.normalDirectionLps_ = nativeDirectionClosestTo(
            geometry, imageAxisByLpsAxis, result.normalDirectionLps_);
    }

    const ImageGeometry::Vector centerIndex{{
        (static_cast<double>(dimensions[0]) - 1.0) / 2.0,
        (static_cast<double>(dimensions[1]) - 1.0) / 2.0,
        (static_cast<double>(dimensions[2]) - 1.0) / 2.0,
    }};
    result.referenceLps_ = geometry.indexToPhysical(centerIndex);
    result.outputSpacing_ =
        *std::min_element(geometry.spacing().begin(), geometry.spacing().end());
    auto oneMillimeterAlongNormal = result.referenceLps_;
    for(std::size_t axis = 0; axis < 3; ++axis)
    {
        oneMillimeterAlongNormal[axis] += result.normalDirectionLps_[axis];
    }
    const auto referenceIndex =
        geometry.physicalToContinuousIndex(result.referenceLps_);
    const auto offsetIndex =
        geometry.physicalToContinuousIndex(oneMillimeterAlongNormal);
    double indexDistanceSquared = 0.0;
    for(std::size_t axis = 0; axis < 3; ++axis)
    {
        const double difference = offsetIndex[axis] - referenceIndex[axis];
        indexDistanceSquared += difference * difference;
    }
    if(!std::isfinite(indexDistanceSquared) || indexDistanceSquared <= 1.0e-12)
    {
        throw std::invalid_argument("Image geometry cannot define a slice step");
    }
    result.sliceStep_ = 1.0 / std::sqrt(indexDistanceSquared);

    double horizontalMaximum = -std::numeric_limits<double>::infinity();
    double verticalMaximum = -std::numeric_limits<double>::infinity();
    result.horizontalMinimum_ = std::numeric_limits<double>::infinity();
    result.verticalMinimum_ = std::numeric_limits<double>::infinity();
    result.normalMinimum_ = std::numeric_limits<double>::infinity();
    result.normalMaximum_ = -std::numeric_limits<double>::infinity();

    for(std::size_t corner = 0; corner < 8; ++corner)
    {
        ImageGeometry::Vector index{};
        for(std::size_t axis = 0; axis < 3; ++axis)
        {
            index[axis] = (corner & (std::size_t{1} << axis)) == 0
                ? 0.0
                : static_cast<double>(dimensions[axis] - 1);
        }

        const auto point = geometry.indexToPhysical(index);
        const double horizontal = result.horizontalCoordinate(point);
        const double vertical = result.verticalCoordinate(point);
        const double normal = result.normalCoordinate(point);
        result.horizontalMinimum_ = std::min(result.horizontalMinimum_, horizontal);
        horizontalMaximum = std::max(horizontalMaximum, horizontal);
        result.verticalMinimum_ = std::min(result.verticalMinimum_, vertical);
        verticalMaximum = std::max(verticalMaximum, vertical);
        result.normalMinimum_ = std::min(result.normalMinimum_, normal);
        result.normalMaximum_ = std::max(result.normalMaximum_, normal);
    }

    const auto expandSingletonExtent = [&](const std::size_t imageAxis,
                                           double& minimum,
                                           double& maximum) {
        if(imageAxis >= 3 || dimensions[imageAxis] != 1)
        {
            return;
        }

        double directionLengthSquared = 0.0;
        for(std::size_t lpsAxis = 0; lpsAxis < 3; ++lpsAxis)
        {
            const double component = geometry.direction()[lpsAxis][imageAxis];
            directionLengthSquared += component * component;
        }
        const double halfVoxelThickness = 0.5 * geometry.spacing()[imageAxis]
            * std::sqrt(directionLengthSquared);
        minimum -= halfVoxelThickness;
        maximum += halfVoxelThickness;
    };
    // VTK image actors do not reliably render an output with a one-pixel
    // dimension. Give an in-plane singleton axis its physical voxel thickness;
    // the reslicer border mode then samples that voxel throughout its support.
    expandSingletonExtent(
        horizontalImageAxis, result.horizontalMinimum_, horizontalMaximum);
    expandSingletonExtent(
        verticalImageAxis, result.verticalMinimum_, verticalMaximum);

    result.width_ = sampleCountForExtent(
        horizontalMaximum - result.horizontalMinimum_, result.outputSpacing_);
    result.height_ = sampleCountForExtent(
        verticalMaximum - result.verticalMinimum_, result.outputSpacing_);
    return result;
}

SliceOrientation OrthogonalSliceGeometry::orientation() const noexcept
{
    return orientation_;
}

const OrthogonalSliceGeometry::Vector&
OrthogonalSliceGeometry::referenceLps() const noexcept
{
    return referenceLps_;
}

const OrthogonalSliceGeometry::Vector&
OrthogonalSliceGeometry::horizontalDirectionLps() const noexcept
{
    return horizontalDirectionLps_;
}

const OrthogonalSliceGeometry::Vector&
OrthogonalSliceGeometry::verticalDirectionLps() const noexcept
{
    return verticalDirectionLps_;
}

const OrthogonalSliceGeometry::Vector&
OrthogonalSliceGeometry::normalDirectionLps() const noexcept
{
    return normalDirectionLps_;
}

double OrthogonalSliceGeometry::horizontalMinimum() const noexcept
{
    return horizontalMinimum_;
}

double OrthogonalSliceGeometry::verticalMinimum() const noexcept
{
    return verticalMinimum_;
}

double OrthogonalSliceGeometry::normalMinimum() const noexcept
{
    return normalMinimum_;
}

double OrthogonalSliceGeometry::normalMaximum() const noexcept
{
    return normalMaximum_;
}

double OrthogonalSliceGeometry::outputSpacing() const noexcept
{
    return outputSpacing_;
}

double OrthogonalSliceGeometry::sliceStep() const noexcept
{
    return sliceStep_;
}

std::size_t OrthogonalSliceGeometry::width() const noexcept
{
    return width_;
}

std::size_t OrthogonalSliceGeometry::height() const noexcept
{
    return height_;
}

double OrthogonalSliceGeometry::horizontalCoordinate(const Vector& pointLps) const
{
    return dot(subtract(pointLps, referenceLps_), horizontalDirectionLps_);
}

double OrthogonalSliceGeometry::verticalCoordinate(const Vector& pointLps) const
{
    return dot(subtract(pointLps, referenceLps_), verticalDirectionLps_);
}

double OrthogonalSliceGeometry::normalCoordinate(const Vector& pointLps) const
{
    return dot(subtract(pointLps, referenceLps_), normalDirectionLps_);
}

OrthogonalSliceGeometry::Vector OrthogonalSliceGeometry::planeOriginForCursor(
    const Vector& cursorLps) const
{
    return addScaled(
        referenceLps_, normalDirectionLps_, normalCoordinate(cursorLps));
}

OrthogonalSliceGeometry::Vector OrthogonalSliceGeometry::pointOnCursorPlane(
    const double horizontal,
    const double vertical,
    const Vector& cursorLps) const
{
    auto point = planeOriginForCursor(cursorLps);
    point = addScaled(point, horizontalDirectionLps_, horizontal);
    return addScaled(point, verticalDirectionLps_, vertical);
}

} // namespace radmarky::core
