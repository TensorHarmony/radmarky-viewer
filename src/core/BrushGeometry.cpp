#include "core/BrushGeometry.h"

#include <cmath>
#include <stdexcept>

namespace radmarky::core
{

BrushFootprint::BrushFootprint(const int size, const BrushShape shape)
    : size_(size), shape_(shape)
{
    if(size < 1 || size > 100)
    {
        throw std::invalid_argument("Brush size must be from 1 to 100 voxels");
    }
    firstOffset_ = -(size - 1) / 2;
    lastOffset_ = firstOffset_ + size - 1;
}

int BrushFootprint::size() const noexcept
{
    return size_;
}

BrushShape BrushFootprint::shape() const noexcept
{
    return shape_;
}

int BrushFootprint::firstOffset() const noexcept
{
    return firstOffset_;
}

int BrushFootprint::lastOffset() const noexcept
{
    return lastOffset_;
}

double BrushFootprint::centerOffset() const noexcept
{
    return 0.5 * static_cast<double>(firstOffset_ + lastOffset_);
}

double BrushFootprint::radius() const noexcept
{
    return 0.5 * static_cast<double>(size_);
}

bool BrushFootprint::contains(const int dx, const int dy) const noexcept
{
    if(dx < firstOffset_ || dx > lastOffset_
       || dy < firstOffset_ || dy > lastOffset_)
    {
        return false;
    }
    if(shape_ == BrushShape::Square)
    {
        return true;
    }
    const double offsetX = static_cast<double>(dx) - centerOffset();
    const double offsetY = static_cast<double>(dy) - centerOffset();
    return offsetX * offsetX + offsetY * offsetY
        <= radius() * radius() + 1.0e-9;
}

std::vector<BrushOutlinePoint> brushOutlinePoints(
    const BrushFootprint& footprint,
    const OrthogonalSliceGeometry& slice,
    const ImageGeometry::Vector& centerPhysical)
{
    const auto gridCenter = brushGridIndex(slice, centerPhysical);
    if(!gridCenter)
    {
        return {};
    }
    const auto project = [&](const double dx, const double dy) {
        const auto physical = brushPointOnSliceGrid(
            slice, *gridCenter, centerPhysical, dx, dy);
        return BrushOutlinePoint{{
            slice.horizontalCoordinate(physical),
            slice.verticalCoordinate(physical),
        }};
    };

    if(footprint.shape() == BrushShape::Square)
    {
        const double lower = static_cast<double>(footprint.firstOffset()) - 0.5;
        const double upper = static_cast<double>(footprint.lastOffset()) + 0.5;
        return {
            project(lower, lower),
            project(upper, lower),
            project(upper, upper),
            project(lower, upper),
        };
    }

    constexpr int segmentCount = 48;
    constexpr double pi = 3.14159265358979323846;
    std::vector<BrushOutlinePoint> points;
    points.reserve(segmentCount);
    const double center = footprint.centerOffset();
    const double radius = footprint.radius();
    for(int segment = 0; segment < segmentCount; ++segment)
    {
        const double angle = 2.0 * pi * static_cast<double>(segment)
            / static_cast<double>(segmentCount);
        points.push_back(project(
            center + radius * std::cos(angle),
            center + radius * std::sin(angle)));
    }
    return points;
}

std::optional<BrushGridIndex> brushGridIndex(
    const OrthogonalSliceGeometry& slice,
    const ImageGeometry::Vector& physicalPoint)
{
    const double spacing = slice.outputSpacing();
    const double horizontal =
        (slice.horizontalCoordinate(physicalPoint) - slice.horizontalMinimum())
        / spacing;
    const double vertical =
        (slice.verticalCoordinate(physicalPoint) - slice.verticalMinimum())
        / spacing;
    if(!std::isfinite(horizontal) || !std::isfinite(vertical))
    {
        return std::nullopt;
    }
    const BrushGridIndex result{{
        std::lround(horizontal),
        std::lround(vertical),
    }};
    if(result[0] < 0 || result[1] < 0
       || result[0] >= static_cast<long>(slice.width())
       || result[1] >= static_cast<long>(slice.height()))
    {
        return std::nullopt;
    }
    return result;
}

ImageGeometry::Vector brushPointOnSliceGrid(
    const OrthogonalSliceGeometry& slice,
    const BrushGridIndex& center,
    const ImageGeometry::Vector& planePoint,
    const double horizontalOffset,
    const double verticalOffset)
{
    const double spacing = slice.outputSpacing();
    const double horizontal = slice.horizontalMinimum()
        + (static_cast<double>(center[0]) + horizontalOffset) * spacing;
    const double vertical = slice.verticalMinimum()
        + (static_cast<double>(center[1]) + verticalOffset) * spacing;
    return slice.pointOnCursorPlane(horizontal, vertical, planePoint);
}

} // namespace radmarky::core
