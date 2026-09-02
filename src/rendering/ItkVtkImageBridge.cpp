#include "rendering/ItkVtkImageBridge.h"

#include "core/Volume.h"

#include <vtkFloatArray.h>
#include <vtkImageData.h>
#include <vtkPointData.h>
#include <vtkUnsignedCharArray.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace radmarky::rendering
{
namespace
{

bool sameGrid(
    const core::ImageGeometry& first,
    const core::ImageGeometry& second)
{
    if(first.dimensions() != second.dimensions())
    {
        return false;
    }
    constexpr double tolerance = 1.0e-9;
    for(std::size_t axis = 0; axis < 3; ++axis)
    {
        if(std::abs(first.spacing()[axis] - second.spacing()[axis]) > tolerance
           || std::abs(first.origin()[axis] - second.origin()[axis]) > tolerance)
        {
            return false;
        }
        for(std::size_t column = 0; column < 3; ++column)
        {
            if(std::abs(
                   first.direction()[axis][column]
                   - second.direction()[axis][column]) > tolerance)
            {
                return false;
            }
        }
    }
    return true;
}

vtkSmartPointer<vtkImageData> describeImage(
    const core::Volume::ImageType& image)
{
    const auto& region = image.GetLargestPossibleRegion();
    const auto size = region.GetSize();
    const auto start = region.GetIndex();

    for(std::size_t axis = 0; axis < 3; ++axis)
    {
        if(size[axis] > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        {
            throw std::overflow_error("Volume dimensions exceed VTK extent limits");
        }
    }

    core::Volume::ImageType::PointType startPoint;
    image.TransformIndexToPhysicalPoint(start, startPoint);

    auto vtkImage = vtkSmartPointer<vtkImageData>::New();
    vtkImage->SetExtent(
        0,
        static_cast<int>(size[0]) - 1,
        0,
        static_cast<int>(size[1]) - 1,
        0,
        static_cast<int>(size[2]) - 1);
    vtkImage->SetSpacing(image.GetSpacing().GetDataPointer());
    vtkImage->SetOrigin(startPoint.GetDataPointer());

    std::array<double, 9> direction{};
    for(std::size_t row = 0; row < 3; ++row)
    {
        for(std::size_t column = 0; column < 3; ++column)
        {
            direction[row * 3 + column] = image.GetDirection()[row][column];
        }
    }
    vtkImage->SetDirectionMatrix(direction.data());
    return vtkImage;
}

vtkSmartPointer<vtkImageData> allocateLike(
    const core::Volume::ImageType& image,
    const int scalarType)
{
    auto vtkImage = describeImage(image);
    vtkImage->AllocateScalars(scalarType, 1);
    return vtkImage;
}

vtkSmartPointer<vtkImageData> describeLike(
    const core::Volume::ImageType& image)
{
    const auto& region = image.GetLargestPossibleRegion();
    const auto pixelCount = region.GetNumberOfPixels();
    if(pixelCount > static_cast<std::size_t>(std::numeric_limits<vtkIdType>::max()))
    {
        throw std::overflow_error("Volume contains too many voxels for VTK");
    }

    auto vtkImage = describeImage(image);
    // The owning Volume is retained alongside vtkImage by the viewer.
    auto scalars = vtkSmartPointer<vtkFloatArray>::New();
    scalars->SetNumberOfComponents(1);
    scalars->SetArray(
        const_cast<float*>(image.GetBufferPointer()),
        static_cast<vtkIdType>(pixelCount),
        1);
    vtkImage->GetPointData()->SetScalars(scalars);
    return vtkImage;
}

} // namespace

vtkSmartPointer<vtkImageData> ItkVtkImageBridge::shareWithVtk(
    const core::Volume& volume)
{
    if(volume.hasDisplayRgb())
    {
        const auto pixelCount =
            volume.image().GetLargestPossibleRegion().GetNumberOfPixels();
        if(pixelCount
           > static_cast<std::size_t>(
                 std::numeric_limits<vtkIdType>::max() / 3))
        {
            throw std::overflow_error(
                "Color volume contains too many components for VTK");
        }
        auto vtkImage = describeImage(volume.image());
        // The owning Volume is retained alongside vtkImage by the viewer.
        auto scalars = vtkSmartPointer<vtkUnsignedCharArray>::New();
        scalars->SetNumberOfComponents(3);
        scalars->SetArray(
            const_cast<unsigned char*>(volume.displayRgb().data()),
            static_cast<vtkIdType>(pixelCount * 3),
            1);
        vtkImage->GetPointData()->SetScalars(scalars);
        return vtkImage;
    }
    return describeLike(volume.image());
}

vtkSmartPointer<vtkImageData> ItkVtkImageBridge::copyComparisonToVtk(
    const core::Volume& first,
    const core::Volume& second)
{
    const auto& firstImage = first.image();
    auto vtkImage = allocateLike(firstImage, VTK_UNSIGNED_CHAR);
    const auto pixelCount =
        firstImage.GetLargestPossibleRegion().GetNumberOfPixels();
    const auto* const firstValues = firstImage.GetBufferPointer();
    const auto* const secondValues = second.image().GetBufferPointer();
    auto* const destination =
        static_cast<unsigned char*>(vtkImage->GetScalarPointer());
    if(sameGrid(first.geometry(), second.geometry()))
    {
        for(itk::SizeValueType index = 0; index < pixelCount; ++index)
        {
            destination[index] = static_cast<unsigned char>(
                compareValues(firstValues[index], secondValues[index]));
        }
        return vtkImage;
    }

    const auto& dimensions = first.geometry().dimensions();
    const auto& secondDimensions = second.geometry().dimensions();
    const core::ImageGeometry::Vector zero{{0.0, 0.0, 0.0}};
    const auto secondBase = second.geometry().physicalToContinuousIndex(
        first.geometry().indexToPhysical(zero));
    std::array<core::ImageGeometry::Vector, 3> secondSteps{};
    for(std::size_t firstAxis = 0; firstAxis < 3; ++firstAxis)
    {
        auto unit = zero;
        unit[firstAxis] = 1.0;
        const auto secondAtUnit = second.geometry().physicalToContinuousIndex(
            first.geometry().indexToPhysical(unit));
        for(std::size_t secondAxis = 0; secondAxis < 3; ++secondAxis)
        {
            secondSteps[firstAxis][secondAxis] =
                secondAtUnit[secondAxis] - secondBase[secondAxis];
        }
    }
    for(std::size_t z = 0; z < dimensions[2]; ++z)
    {
        for(std::size_t y = 0; y < dimensions[1]; ++y)
        {
            for(std::size_t x = 0; x < dimensions[0]; ++x)
            {
                const std::size_t offset =
                    x + dimensions[0] * (y + dimensions[1] * z);
                std::array<long, 3> secondIndex{};
                bool secondInside = true;
                for(std::size_t axis = 0; axis < 3; ++axis)
                {
                    const double continuous = secondBase[axis]
                        + static_cast<double>(x) * secondSteps[0][axis]
                        + static_cast<double>(y) * secondSteps[1][axis]
                        + static_cast<double>(z) * secondSteps[2][axis];
                    secondIndex[axis] = std::lround(continuous);
                    secondInside = secondInside && secondIndex[axis] >= 0
                        && secondIndex[axis]
                            < static_cast<long>(secondDimensions[axis]);
                }
                const std::size_t secondOffset = secondInside
                    ? static_cast<std::size_t>(secondIndex[0])
                        + secondDimensions[0]
                            * (static_cast<std::size_t>(secondIndex[1])
                               + secondDimensions[1]
                                   * static_cast<std::size_t>(secondIndex[2]))
                    : 0;
                destination[offset] = static_cast<unsigned char>(compareValues(
                    firstValues[offset],
                    secondInside ? secondValues[secondOffset] : 0.0F));
            }
        }
    }
    return vtkImage;
}

ItkVtkImageBridge::ComparisonClass ItkVtkImageBridge::compareValues(
    const float first,
    const float second) noexcept
{
    const bool firstPresent = std::isfinite(first) && first != 0.0F;
    const bool secondPresent = std::isfinite(second) && second != 0.0F;
    if(firstPresent && !secondPresent)
    {
        return ComparisonClass::FirstOnly;
    }
    if(!firstPresent && secondPresent)
    {
        return ComparisonClass::SecondOnly;
    }
    if(!firstPresent)
    {
        return ComparisonClass::Background;
    }

    const double scale = std::max(
        {1.0, std::abs(static_cast<double>(first)),
         std::abs(static_cast<double>(second))});
    return std::abs(static_cast<double>(first) - static_cast<double>(second))
            <= 1.0e-4 * scale
        ? ComparisonClass::SameValue
        : ComparisonClass::DifferentValues;
}

} // namespace radmarky::rendering
