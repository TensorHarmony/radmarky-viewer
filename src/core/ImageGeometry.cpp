#include "core/ImageGeometry.h"

#include <cmath>
#include <stdexcept>

namespace radmarky::core
{
namespace
{

ImageGeometry::Direction invert(const ImageGeometry::Direction& matrix)
{
    const double determinant =
        matrix[0][0] * (matrix[1][1] * matrix[2][2] - matrix[1][2] * matrix[2][1])
        - matrix[0][1]
            * (matrix[1][0] * matrix[2][2] - matrix[1][2] * matrix[2][0])
        + matrix[0][2]
            * (matrix[1][0] * matrix[2][1] - matrix[1][1] * matrix[2][0]);

    if(std::abs(determinant) < 1.0e-12)
    {
        throw std::invalid_argument("Image direction matrix is singular");
    }

    const double scale = 1.0 / determinant;
    return {{
        {{
            scale * (matrix[1][1] * matrix[2][2] - matrix[1][2] * matrix[2][1]),
            scale * (matrix[0][2] * matrix[2][1] - matrix[0][1] * matrix[2][2]),
            scale * (matrix[0][1] * matrix[1][2] - matrix[0][2] * matrix[1][1]),
        }},
        {{
            scale * (matrix[1][2] * matrix[2][0] - matrix[1][0] * matrix[2][2]),
            scale * (matrix[0][0] * matrix[2][2] - matrix[0][2] * matrix[2][0]),
            scale * (matrix[0][2] * matrix[1][0] - matrix[0][0] * matrix[1][2]),
        }},
        {{
            scale * (matrix[1][0] * matrix[2][1] - matrix[1][1] * matrix[2][0]),
            scale * (matrix[0][1] * matrix[2][0] - matrix[0][0] * matrix[2][1]),
            scale * (matrix[0][0] * matrix[1][1] - matrix[0][1] * matrix[1][0]),
        }},
    }};
}

} // namespace

ImageGeometry::ImageGeometry(const itk::ImageBase<3>& image)
{
    const auto size = image.GetLargestPossibleRegion().GetSize();
    const auto spacing = image.GetSpacing();
    const auto origin = image.GetOrigin();
    const auto direction = image.GetDirection();

    for(std::size_t row = 0; row < 3; ++row)
    {
        dimensions_[row] = size[row];
        spacing_[row] = spacing[row];
        origin_[row] = origin[row];

        if(dimensions_[row] == 0)
        {
            throw std::invalid_argument("Image dimensions must be non-zero");
        }
        if(!std::isfinite(spacing_[row]) || spacing_[row] <= 0.0)
        {
            throw std::invalid_argument("Image spacing must be finite and positive");
        }
        if(!std::isfinite(origin_[row]))
        {
            throw std::invalid_argument("Image origin must be finite");
        }

        for(std::size_t column = 0; column < 3; ++column)
        {
            direction_[row][column] = direction[row][column];
            if(!std::isfinite(direction_[row][column]))
            {
                throw std::invalid_argument("Image direction must be finite");
            }
        }
    }

    inverseDirection_ = invert(direction_);
}

const ImageGeometry::Dimensions& ImageGeometry::dimensions() const noexcept
{
    return dimensions_;
}

const ImageGeometry::Vector& ImageGeometry::spacing() const noexcept
{
    return spacing_;
}

const ImageGeometry::Vector& ImageGeometry::origin() const noexcept
{
    return origin_;
}

const ImageGeometry::Direction& ImageGeometry::direction() const noexcept
{
    return direction_;
}

ImageGeometry::Vector ImageGeometry::indexToPhysical(
    const Vector& continuousIndex) const
{
    Vector point = origin_;
    for(std::size_t row = 0; row < 3; ++row)
    {
        for(std::size_t column = 0; column < 3; ++column)
        {
            point[row] += direction_[row][column] * spacing_[column]
                * continuousIndex[column];
        }
    }
    return point;
}

ImageGeometry::Vector ImageGeometry::physicalToContinuousIndex(
    const Vector& point) const
{
    Vector offset{};
    for(std::size_t axis = 0; axis < 3; ++axis)
    {
        offset[axis] = point[axis] - origin_[axis];
    }

    Vector index{};
    for(std::size_t row = 0; row < 3; ++row)
    {
        for(std::size_t column = 0; column < 3; ++column)
        {
            index[row] += inverseDirection_[row][column] * offset[column];
        }
        index[row] /= spacing_[row];
    }
    return index;
}

double ImageGeometry::physicalStepForOneVoxel(
    const Vector& physicalDirection) const
{
    auto displaced = origin_;
    for(std::size_t axis = 0; axis < 3; ++axis)
    {
        displaced[axis] += physicalDirection[axis];
    }
    const auto displacedIndex = physicalToContinuousIndex(displaced);
    double indexDistanceSquared = 0.0;
    for(const double component : displacedIndex)
    {
        indexDistanceSquared += component * component;
    }
    if(!std::isfinite(indexDistanceSquared) || indexDistanceSquared <= 1.0e-12)
    {
        throw std::invalid_argument(
            "Physical direction cannot define a one-voxel step");
    }
    return 1.0 / std::sqrt(indexDistanceSquared);
}

} // namespace radmarky::core
