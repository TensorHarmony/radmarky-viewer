#pragma once

#include "core/ImageGeometry.h"

#include <cstddef>

namespace radmarky::core
{

enum class SliceOrientation
{
    Axial,
    Sagittal,
    Coronal,
};

// Patient alignment produces the fixed RPS/AIR/RIP reformats used for general
// review. Native alignment keeps each view on voxel planes from the reference
// image, which is required for exact label-map review and editing.
enum class SliceAlignment
{
    Patient,
    Native,
};

class OrthogonalSliceGeometry
{
public:
    using Vector = ImageGeometry::Vector;

    [[nodiscard]] static OrthogonalSliceGeometry fromImageGeometry(
        const ImageGeometry& geometry,
        SliceOrientation orientation,
        SliceAlignment alignment = SliceAlignment::Patient);

    [[nodiscard]] SliceOrientation orientation() const noexcept;
    [[nodiscard]] const Vector& referenceLps() const noexcept;
    [[nodiscard]] const Vector& horizontalDirectionLps() const noexcept;
    [[nodiscard]] const Vector& verticalDirectionLps() const noexcept;
    [[nodiscard]] const Vector& normalDirectionLps() const noexcept;
    [[nodiscard]] double horizontalMinimum() const noexcept;
    [[nodiscard]] double verticalMinimum() const noexcept;
    [[nodiscard]] double normalMinimum() const noexcept;
    [[nodiscard]] double normalMaximum() const noexcept;
    [[nodiscard]] double outputSpacing() const noexcept;
    [[nodiscard]] double sliceStep() const noexcept;
    [[nodiscard]] std::size_t width() const noexcept;
    [[nodiscard]] std::size_t height() const noexcept;

    [[nodiscard]] double horizontalCoordinate(const Vector& pointLps) const;
    [[nodiscard]] double verticalCoordinate(const Vector& pointLps) const;
    [[nodiscard]] double normalCoordinate(const Vector& pointLps) const;
    [[nodiscard]] Vector planeOriginForCursor(const Vector& cursorLps) const;
    [[nodiscard]] Vector pointOnCursorPlane(
        double horizontal,
        double vertical,
        const Vector& cursorLps) const;

private:
    SliceOrientation orientation_ = SliceOrientation::Axial;
    Vector referenceLps_{};
    Vector horizontalDirectionLps_{{1.0, 0.0, 0.0}};
    Vector verticalDirectionLps_{{0.0, -1.0, 0.0}};
    Vector normalDirectionLps_{{0.0, 0.0, -1.0}};
    double horizontalMinimum_ = 0.0;
    double verticalMinimum_ = 0.0;
    double normalMinimum_ = 0.0;
    double normalMaximum_ = 0.0;
    double outputSpacing_ = 1.0;
    double sliceStep_ = 1.0;
    std::size_t width_ = 1;
    std::size_t height_ = 1;
};

} // namespace radmarky::core
