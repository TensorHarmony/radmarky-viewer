#pragma once

#include "core/ImageGeometry.h"
#include "core/OrthogonalSliceGeometry.h"

class vtkImageData;
class vtkImageReslice;

namespace radmarky::rendering
{

void configureOrthogonalReslice(
    vtkImageReslice& reslice,
    vtkImageData* imageData,
    const core::OrthogonalSliceGeometry& slice,
    const core::ImageGeometry::Vector& cursorPhysical,
    double backgroundLevel = 0.0);

void setOrthogonalResliceCursor(
    vtkImageReslice& reslice,
    const core::OrthogonalSliceGeometry& slice,
    const core::ImageGeometry::Vector& cursorPhysical);

} // namespace radmarky::rendering
