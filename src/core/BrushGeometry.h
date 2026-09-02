#pragma once

#include "core/ImageGeometry.h"
#include "core/OrthogonalSliceGeometry.h"

#include <array>
#include <optional>
#include <vector>

namespace radmarky::core
{

enum class BrushShape
{
    Square,
    Circle,
};

class BrushFootprint
{
public:
    BrushFootprint(int size, BrushShape shape);

    [[nodiscard]] int size() const noexcept;
    [[nodiscard]] BrushShape shape() const noexcept;
    [[nodiscard]] int firstOffset() const noexcept;
    [[nodiscard]] int lastOffset() const noexcept;
    [[nodiscard]] double centerOffset() const noexcept;
    [[nodiscard]] double radius() const noexcept;
    [[nodiscard]] bool contains(int dx, int dy) const noexcept;

private:
    int size_ = 1;
    BrushShape shape_ = BrushShape::Square;
    int firstOffset_ = 0;
    int lastOffset_ = 0;
};

using BrushOutlinePoint = std::array<double, 2>;
using BrushGridIndex = std::array<long, 2>;

// Locates a physical point on the fixed display grid used by the reslicer.
[[nodiscard]] std::optional<BrushGridIndex> brushGridIndex(
    const OrthogonalSliceGeometry& slice,
    const ImageGeometry::Vector& physicalPoint);

// Returns a physical point at an offset from a display-grid brush center while
// preserving the plane normal of planePoint.
[[nodiscard]] ImageGeometry::Vector brushPointOnSliceGrid(
    const OrthogonalSliceGeometry& slice,
    const BrushGridIndex& center,
    const ImageGeometry::Vector& planePoint,
    double horizontalOffset,
    double verticalOffset);

// Builds the screen-aligned outline of the same display-grid footprint used by
// AnnotationEditor. Even sizes retain their half-voxel visual anchor.
[[nodiscard]] std::vector<BrushOutlinePoint> brushOutlinePoints(
    const BrushFootprint& footprint,
    const OrthogonalSliceGeometry& slice,
    const ImageGeometry::Vector& centerPhysical);

} // namespace radmarky::core
