#include "rendering/OrthogonalReslice.h"

#include <vtkImageData.h>
#include <vtkImageReslice.h>

#include <limits>
#include <stdexcept>

namespace radmarky::rendering
{
namespace
{

constexpr double identityDirection[9]{
    1.0, 0.0, 0.0,
    0.0, 1.0, 0.0,
    0.0, 0.0, 1.0,
};

} // namespace

void configureOrthogonalReslice(
    vtkImageReslice& reslice,
    vtkImageData* const imageData,
    const core::OrthogonalSliceGeometry& slice,
    const core::ImageGeometry::Vector& cursorPhysical,
    const double backgroundLevel)
{
    if(imageData == nullptr)
    {
        throw std::invalid_argument("Orthogonal reslice input cannot be null");
    }
    if(slice.width() > static_cast<std::size_t>(std::numeric_limits<int>::max())
       || slice.height()
           > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    {
        throw std::overflow_error("Slice dimensions exceed VTK extent limits");
    }

    const auto& horizontal = slice.horizontalDirectionLps();
    const auto& vertical = slice.verticalDirectionLps();
    const auto& normal = slice.normalDirectionLps();

    reslice.SetInputData(imageData);
    reslice.SetOutputDimensionality(2);
    reslice.SetInterpolationModeToLinear();
    reslice.SetBackgroundLevel(backgroundLevel);
    reslice.BorderOn();
    reslice.SetResliceAxesDirectionCosines(
        horizontal.data(), vertical.data(), normal.data());
    setOrthogonalResliceCursor(reslice, slice, cursorPhysical);

    // ResliceAxes already maps the screen-aligned output grid into patient LPS.
    // Inheriting the input direction would apply image orientation twice.
    reslice.SetOutputDirection(identityDirection);
    reslice.SetOutputOrigin(
        slice.horizontalMinimum(), slice.verticalMinimum(), 0.0);
    reslice.SetOutputSpacing(slice.outputSpacing(), slice.outputSpacing(), 1.0);
    reslice.SetOutputExtent(
        0,
        static_cast<int>(slice.width()) - 1,
        0,
        static_cast<int>(slice.height()) - 1,
        0,
        0);
    reslice.Update();
}

void setOrthogonalResliceCursor(
    vtkImageReslice& reslice,
    const core::OrthogonalSliceGeometry& slice,
    const core::ImageGeometry::Vector& cursorPhysical)
{
    const auto planeOrigin = slice.planeOriginForCursor(cursorPhysical);
    reslice.SetResliceAxesOrigin(planeOrigin.data());
    reslice.Modified();
}

} // namespace radmarky::rendering
