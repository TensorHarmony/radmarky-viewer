#pragma once

#include "core/ImageGeometry.h"
#include "core/OrthogonalSliceGeometry.h"

#include <array>
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

// Projects the exact index-space footprint used by AnnotationEditor into the
// fixed display plane. This preserves the half-voxel anchor of even sizes for
// flipped, permuted, rotated, and anisotropic image geometries.
[[nodiscard]] std::vector<BrushOutlinePoint> brushOutlinePoints(
    const BrushFootprint& footprint,
    const ImageGeometry& geometry,
    const OrthogonalSliceGeometry& slice,
    const ImageGeometry::Vector& centerIndex);

} // namespace radmarky::core
