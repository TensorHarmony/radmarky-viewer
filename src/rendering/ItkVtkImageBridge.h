#pragma once

#include <vtkSmartPointer.h>

#include <cstdint>

class vtkImageData;

namespace radmarky::core
{
class Volume;
}

namespace radmarky::rendering
{

class ItkVtkImageBridge
{
public:
    enum class ComparisonClass : std::uint8_t
    {
        Background = 0,
        FirstOnly = 1,
        SameValue = 2,
        SecondOnly = 3,
        DifferentValues = 4,
    };

    [[nodiscard]] static vtkSmartPointer<vtkImageData> shareWithVtk(
        const core::Volume& volume);
    [[nodiscard]] static vtkSmartPointer<vtkImageData> copyComparisonToVtk(
        const core::Volume& first,
        const core::Volume& second);
    [[nodiscard]] static ComparisonClass compareValues(
        float first,
        float second) noexcept;
};

} // namespace radmarky::rendering
