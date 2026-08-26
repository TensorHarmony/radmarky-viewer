#include "core/Annotation.h"

#include <itkImage.h>

#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace
{

using Image = radmarky::core::Volume::ImageType;

Image::Pointer makeImage(const float fill = 0.0F)
{
    auto image = Image::New();
    Image::SizeType size{{4, 5, 6}};
    Image::RegionType region;
    region.SetSize(size);
    image->SetRegions(region);
    Image::SpacingType spacing;
    spacing[0] = 0.7;
    spacing[1] = 1.2;
    spacing[2] = 2.5;
    image->SetSpacing(spacing);
    Image::PointType origin;
    origin[0] = 10.0;
    origin[1] = -20.0;
    origin[2] = 30.0;
    image->SetOrigin(origin);
    image->Allocate();
    image->FillBuffer(fill);
    return image;
}

bool expectThrows(const auto& operation, const std::string_view field)
{
    try
    {
        operation();
    }
    catch(const std::invalid_argument&)
    {
        return true;
    }
    std::cerr << field << ": expected invalid_argument\n";
    return false;
}

} // namespace

int main()
{
    using radmarky::core::Annotation;
    using radmarky::core::AnnotationKind;
    using radmarky::core::Volume;

    bool passed = true;
    auto primary = std::make_shared<Volume>(makeImage());
    auto labels = makeImage(1.0F);
    labels->SetPixel(Image::IndexType{{1, 2, 3}}, 7.0F);
    Annotation annotation(
        "vessel-labels.nii.gz",
        std::filesystem::path("vessel-labels.nii.gz"),
        std::make_shared<Volume>(labels),
        AnnotationKind::LabelMap);
    annotation.verifyGeometry(primary->geometry());
    passed &= annotation.name() == "vessel-labels.nii.gz";
    passed &= annotation.kind() == AnnotationKind::LabelMap;
    passed &= annotation.labelValues()
        == (std::vector<std::uint16_t>{1, 7});
    passed &= std::abs(annotation.opacity() - 0.5) < 1.0e-12;
    annotation.setOpacity(0.75);
    passed &= std::abs(annotation.opacity() - 0.75) < 1.0e-12;
    passed &= annotation.isVisible();
    annotation.setVisible(false);
    passed &= !annotation.isVisible();
    passed &= std::abs(annotation.opacity() - 0.75) < 1.0e-12;

    const auto blank = Annotation::createBlankLabelMap("New annotation", *primary);
    passed &= blank->kind() == AnnotationKind::LabelMap;
    passed &= blank->sourcePath().empty();
    passed &= blank->isModified();
    passed &= blank->labelValues().empty();
    passed &= blank->volume().geometry().dimensions()
        == primary->geometry().dimensions();
    passed &= blank->volume().image().GetPixel(Image::IndexType{{1, 2, 3}})
        == 0.0F;
    blank->setName("tumor.nii.gz");
    passed &= blank->name() == "tumor.nii.gz";
    passed &= expectThrows(
        [&] { blank->setName({}); }, "empty annotation name");

    auto tolerantImage = makeImage();
    auto tolerantOrigin = tolerantImage->GetOrigin();
    tolerantOrigin[0] += 5.0e-5;
    tolerantImage->SetOrigin(tolerantOrigin);
    Annotation tolerant(
        "tolerant.nii",
        {},
        std::make_shared<Volume>(tolerantImage),
        AnnotationKind::ScalarMap);
    tolerant.verifyGeometry(primary->geometry());

    auto wrongSpacingImage = makeImage();
    auto wrongSpacing = wrongSpacingImage->GetSpacing();
    wrongSpacing[1] = 1.4;
    wrongSpacingImage->SetSpacing(wrongSpacing);
    Annotation wrongSpacingAnnotation(
        "wrong.nii",
        {},
        std::make_shared<Volume>(wrongSpacingImage),
        AnnotationKind::ScalarMap);
    passed &= expectThrows(
        [&] { wrongSpacingAnnotation.conformGeometry(primary->geometry()); },
        "spacing mismatch");

    auto flippedLabelImage = makeImage();
    auto flippedOrigin = flippedLabelImage->GetOrigin();
    flippedOrigin[2] = -125.0;
    flippedLabelImage->SetOrigin(flippedOrigin);
    auto flippedDirection = flippedLabelImage->GetDirection();
    flippedDirection[2][2] = -1.0;
    flippedLabelImage->SetDirection(flippedDirection);
    const Image::IndexType retainedIndex{{1, 2, 3}};
    flippedLabelImage->SetPixel(retainedIndex, 9.0F);
    Annotation flippedLabel(
        "flipped-label.nii.gz",
        {},
        std::make_shared<Volume>(flippedLabelImage),
        AnnotationKind::LabelMap);
    passed &= expectThrows(
        [&] { flippedLabel.verifyGeometry(primary->geometry()); },
        "label direction mismatch before conforming");
    flippedLabel.conformGeometry(primary->geometry());
    flippedLabel.verifyGeometry(primary->geometry());
    passed &= flippedLabel.volume().image().GetPixel(retainedIndex) == 9.0F;

    passed &= expectThrows(
        [] {
            auto invalid = makeImage(-1.0F);
            Annotation bad(
                "negative.nii", {}, std::make_shared<Volume>(invalid),
                AnnotationKind::LabelMap);
        },
        "negative label");
    passed &= expectThrows(
        [] {
            auto invalid = makeImage(1.25F);
            Annotation bad(
                "fractional.nii", {}, std::make_shared<Volume>(invalid),
                AnnotationKind::LabelMap);
        },
        "fractional label");
    passed &= expectThrows(
        [&] { annotation.setOpacity(1.5); }, "invalid opacity");
    return passed ? 0 : 1;
}
