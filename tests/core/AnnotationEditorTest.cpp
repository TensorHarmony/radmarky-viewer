#include "core/Annotation.h"
#include "core/AnnotationEditor.h"
#include "core/LabelPalette.h"

#include <itkImage.h>

#include <filesystem>
#include <iostream>
#include <memory>
#include <vector>

namespace
{
using Image = radmarky::core::Volume::ImageType;

Image::Pointer makeImage()
{
    auto image = Image::New();
    Image::SizeType size{{7, 7, 3}};
    Image::RegionType region;
    region.SetSize(size);
    image->SetRegions(region);
    image->Allocate();
    image->FillBuffer(0.0F);
    return image;
}

std::shared_ptr<radmarky::core::Annotation> makeAnnotation(
    const radmarky::core::AnnotationKind kind)
{
    return std::make_shared<radmarky::core::Annotation>(
        "labels.nii.gz", std::filesystem::path("labels.nii.gz"),
        std::make_shared<radmarky::core::Volume>(makeImage()), kind);
}

std::shared_ptr<radmarky::core::Annotation> makeComponentAnnotation()
{
    auto image = makeImage();
    for(const Image::IndexType index : {
            Image::IndexType{{1, 1, 0}},
            Image::IndexType{{1, 1, 1}},
            Image::IndexType{{2, 1, 1}},
            Image::IndexType{{2, 2, 1}},
            Image::IndexType{{3, 3, 1}},
            Image::IndexType{{6, 6, 1}}})
    {
        image->SetPixel(index, 4.0F);
    }
    image->SetPixel(Image::IndexType{{3, 2, 1}}, 9.0F);
    return std::make_shared<radmarky::core::Annotation>(
        "components.nii.gz", std::filesystem::path("components.nii.gz"),
        std::make_shared<radmarky::core::Volume>(image),
        radmarky::core::AnnotationKind::LabelMap);
}
}

int main()
{
    using radmarky::core::AnnotationKind;
    using radmarky::core::BrushShape;
    using radmarky::core::PaintOverMode;
    using radmarky::core::AnnotationEditor;

    bool passed = true;
    passed &= radmarky::core::defaultLabelColor(1) == 0xFF0000U;
    passed &= radmarky::core::defaultLabelColor(2) == 0x00FF00U;
    passed &= radmarky::core::defaultLabelColor(131) == 0xFF0000U;
    auto labels = makeAnnotation(AnnotationKind::LabelMap);
    AnnotationEditor editor;
    for(const auto brushShape : {BrushShape::Square, BrushShape::Circle})
    {
        for(int brushSize = 1; brushSize <= 6; ++brushSize)
        {
            auto footprint = makeAnnotation(AnnotationKind::LabelMap);
            AnnotationEditor footprintEditor;
            footprintEditor.setAnnotation(footprint);
            footprintEditor.setBrushRadius(brushSize);
            footprintEditor.setBrushShape(brushShape);
            footprintEditor.beginStroke(false);
            passed &= footprintEditor.stamp({{3.0, 3.0, 1.0}});
            passed &= footprintEditor.endStroke();

            const radmarky::core::BrushFootprint expectedFootprint(
                brushSize, brushShape);
            const auto& footprintImage = footprint->volume().image();
            for(long z = 0; z < 3; ++z)
            {
                for(long y = 0; y < 7; ++y)
                {
                    for(long x = 0; x < 7; ++x)
                    {
                        const bool expected = z == 1
                            && expectedFootprint.contains(
                                static_cast<int>(x - 3),
                                static_cast<int>(y - 3));
                        passed &=
                            (footprintImage.GetPixel(
                                 Image::IndexType{{x, y, z}})
                             != 0.0F)
                            == expected;
                    }
                }
            }
        }
    }
    {
        auto circleFootprint = makeAnnotation(AnnotationKind::LabelMap);
        AnnotationEditor circleEditor;
        circleEditor.setAnnotation(circleFootprint);
        circleEditor.setBrushRadius(5);
        circleEditor.setBrushShape(BrushShape::Circle);
        circleEditor.beginStroke(false);
        passed &= circleEditor.stamp({{3.0, 3.0, 1.0}});
        passed &= circleEditor.endStroke();
        const auto& circleImage = circleFootprint->volume().image();
        std::size_t circleCount = 0;
        const auto circlePixelCount =
            circleImage.GetLargestPossibleRegion().GetNumberOfPixels();
        for(itk::SizeValueType offset = 0;
            offset < circlePixelCount;
            ++offset)
        {
            circleCount +=
                circleImage.GetBufferPointer()[offset] != 0.0F ? 1U : 0U;
        }
        passed &= circleCount == 21;
        passed &= circleImage.GetPixel(Image::IndexType{{3, 3, 1}}) == 1.0F;
        passed &= circleImage.GetPixel(Image::IndexType{{5, 5, 1}}) == 0.0F;
        passed &= circleImage.GetPixel(Image::IndexType{{1, 1, 1}}) == 0.0F;
        passed &= circleImage.GetPixel(Image::IndexType{{5, 3, 1}}) == 1.0F;
    }
    editor.setAnnotation(labels);
    editor.setActiveLabel(12);
    editor.setBrushRadius(1);
    editor.beginStroke(false);
    passed &= editor.stamp({{3.0, 3.0, 1.0}});
    passed &= editor.stamp({{4.0, 3.0, 1.0}});
    passed &= editor.endStroke();
    passed &= editor.canUndo() && !editor.canRedo();

    const auto& image = labels->volume().image();
    passed &= image.GetPixel(Image::IndexType{{3, 3, 1}}) == 12.0F;
    passed &= image.GetPixel(Image::IndexType{{4, 3, 1}}) == 12.0F;
    passed &= image.GetPixel(Image::IndexType{{3, 2, 1}}) == 0.0F;
    passed &= image.GetPixel(Image::IndexType{{3, 3, 0}}) == 0.0F;
    std::size_t paintedVoxelCount = 0;
    const auto pixelCount = image.GetLargestPossibleRegion().GetNumberOfPixels();
    for(itk::SizeValueType offset = 0; offset < pixelCount; ++offset)
    {
        paintedVoxelCount += image.GetBufferPointer()[offset] == 12.0F ? 1U : 0U;
    }
    passed &= paintedVoxelCount == 2;
    passed &= editor.undo();
    passed &= image.GetPixel(Image::IndexType{{3, 3, 1}}) == 0.0F;
    passed &= editor.canRedo();
    passed &= editor.redo();
    passed &= image.GetPixel(Image::IndexType{{3, 3, 1}}) == 12.0F;

    auto continuousLabels = makeAnnotation(AnnotationKind::LabelMap);
    AnnotationEditor continuousEditor;
    continuousEditor.setAnnotation(continuousLabels);
    continuousEditor.setActiveLabel(5);
    continuousEditor.beginStroke(false);
    passed &= continuousEditor.stamp({{0.0, 1.0, 1.0}});
    passed &= continuousEditor.stamp({{6.0, 1.0, 1.0}});
    passed &= continuousEditor.endStroke();
    const auto& continuousImage = continuousLabels->volume().image();
    for(long x = 0; x < 7; ++x)
    {
        passed &= continuousImage.GetPixel(Image::IndexType{{x, 1, 1}}) == 5.0F;
    }
    passed &= continuousEditor.undo();
    for(long x = 0; x < 7; ++x)
    {
        passed &= continuousImage.GetPixel(Image::IndexType{{x, 1, 1}}) == 0.0F;
    }

    editor.beginStroke(true);
    passed &= editor.stamp({{3.0, 3.0, 1.0}});
    passed &= editor.endStroke();
    passed &= image.GetPixel(Image::IndexType{{3, 3, 1}}) == 0.0F;
    passed &= editor.undo();
    passed &= image.GetPixel(Image::IndexType{{3, 3, 1}}) == 12.0F;

    auto maskedLabels = makeAnnotation(AnnotationKind::LabelMap);
    AnnotationEditor maskedEditor;
    maskedEditor.setAnnotation(maskedLabels);
    passed &= maskedLabels->labelValues().empty();
    maskedEditor.setActiveLabel(7);
    maskedEditor.beginStroke(false);
    passed &= maskedEditor.stamp({{3.0, 3.0, 1.0}});
    passed &= maskedEditor.endStroke();
    const auto& maskedImage = maskedLabels->volume().image();
    passed &= maskedLabels->labelValues()
        == std::vector<std::uint16_t>{7};

    maskedEditor.setActiveLabel(9);
    maskedEditor.setPaintOver(PaintOverMode::OneLabel, 0);
    maskedEditor.beginStroke(false);
    passed &= !maskedEditor.stamp({{3.0, 3.0, 1.0}});
    passed &= !maskedEditor.endStroke();
    passed &= maskedImage.GetPixel(Image::IndexType{{3, 3, 1}}) == 7.0F;
    maskedEditor.beginStroke(false);
    passed &= maskedEditor.stamp({{2.0, 3.0, 1.0}});
    passed &= maskedEditor.endStroke();
    passed &= maskedImage.GetPixel(Image::IndexType{{2, 3, 1}}) == 9.0F;
    passed &= maskedLabels->labelValues()
        == (std::vector<std::uint16_t>{7, 9});

    maskedEditor.setActiveLabel(11);
    maskedEditor.setPaintOver(PaintOverMode::OneLabel, 9);
    maskedEditor.beginStroke(false);
    passed &= maskedEditor.stamp({{2.0, 3.0, 1.0}});
    passed &= !maskedEditor.stamp({{3.0, 3.0, 1.0}});
    passed &= maskedEditor.endStroke();
    passed &= maskedImage.GetPixel(Image::IndexType{{2, 3, 1}}) == 11.0F;
    passed &= maskedImage.GetPixel(Image::IndexType{{3, 3, 1}}) == 7.0F;
    passed &= maskedLabels->labelValues()
        == (std::vector<std::uint16_t>{7, 11});
    passed &= maskedEditor.undo();
    passed &= maskedLabels->labelValues()
        == (std::vector<std::uint16_t>{7, 9});
    passed &= maskedEditor.redo();
    passed &= maskedLabels->labelValues()
        == (std::vector<std::uint16_t>{7, 11});

    auto components = makeComponentAnnotation();
    AnnotationEditor componentEditor;
    componentEditor.setAnnotation(components);
    const auto& componentImage = components->volume().image();
    passed &= !componentEditor.eraseConnectedComponentOnSlice(
        {{0.0, 0.0, 0.0}});
    passed &= componentEditor.eraseConnectedComponentOnSlice(
        {{1.0, 1.0, 1.0}});
    for(const Image::IndexType index : {
            Image::IndexType{{1, 1, 1}},
            Image::IndexType{{2, 1, 1}},
            Image::IndexType{{2, 2, 1}},
            Image::IndexType{{3, 3, 1}}})
    {
        passed &= componentImage.GetPixel(index) == 0.0F;
    }
    passed &= componentImage.GetPixel(Image::IndexType{{1, 1, 0}}) == 4.0F;
    passed &= componentImage.GetPixel(Image::IndexType{{6, 6, 1}}) == 4.0F;
    passed &= componentImage.GetPixel(Image::IndexType{{3, 2, 1}}) == 9.0F;
    passed &= componentEditor.canUndo() && !componentEditor.canRedo();
    passed &= componentEditor.undo();
    passed &= componentImage.GetPixel(Image::IndexType{{1, 1, 1}}) == 4.0F;
    passed &= componentImage.GetPixel(Image::IndexType{{3, 3, 1}}) == 4.0F;
    passed &= componentEditor.redo();
    passed &= componentImage.GetPixel(Image::IndexType{{1, 1, 1}}) == 0.0F;
    passed &= components->labelValues()
        == (std::vector<std::uint16_t>{4, 9});

    bool rejectedScalar = false;
    try
    {
        editor.setAnnotation(makeAnnotation(AnnotationKind::ScalarMap));
    }
    catch(const std::invalid_argument&)
    {
        rejectedScalar = true;
    }
    passed &= rejectedScalar;

    bool rejectedRadius = false;
    try
    {
        editor.setBrushRadius(0);
    }
    catch(const std::invalid_argument&)
    {
        rejectedRadius = true;
    }
    passed &= rejectedRadius;

    if(!passed)
    {
        std::cerr << "Annotation editor checks failed\n";
    }
    return passed ? 0 : 1;
}
