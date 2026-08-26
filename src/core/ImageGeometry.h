#pragma once

#include <itkImageBase.h>

#include <array>
#include <cstddef>

namespace radmarky::core
{

class ImageGeometry
{
public:
    using Dimensions = std::array<std::size_t, 3>;
    using Vector = std::array<double, 3>;
    using Direction = std::array<std::array<double, 3>, 3>;

    explicit ImageGeometry(const itk::ImageBase<3>& image);

    [[nodiscard]] const Dimensions& dimensions() const noexcept;
    [[nodiscard]] const Vector& spacing() const noexcept;
    [[nodiscard]] const Vector& origin() const noexcept;
    [[nodiscard]] const Direction& direction() const noexcept;

    [[nodiscard]] Vector indexToPhysical(const Vector& continuousIndex) const;
    [[nodiscard]] Vector physicalToContinuousIndex(const Vector& point) const;
    [[nodiscard]] double physicalStepForOneVoxel(
        const Vector& physicalDirection) const;

private:
    Dimensions dimensions_{};
    Vector spacing_{};
    Vector origin_{};
    Direction direction_{};
    Direction inverseDirection_{};
};

} // namespace radmarky::core
