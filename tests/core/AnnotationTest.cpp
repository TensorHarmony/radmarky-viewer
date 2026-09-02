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

Image::Pointer makeObliqueImage(const float fill = 0.0F)
{
    auto image = Image::New();
    Image::RegionType region;
    region.SetSize({{8, 6, 5}});
    image->SetRegions(region);
    Image::SpacingType spacing;
    spacing[0] = 0.8;
    spacing[1] = 0.9;
    spacing[2] = 2.5;
    image->SetSpacing(spacing);
    constexpr double sine = 0.17364817766693;
    constexpr double cosine = 0.98480775301221;
    Image::DirectionType direction;
    direction.SetIdentity();
    direction[1][1] = cosine;
    direction[1][2] = -sine;
    direction[2][1] = sine;
    direction[2][2] = cosine;
    image->SetDirection(direction);
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

    auto sliceLabels = makeImage();
    sliceLabels->SetPixel(Image::IndexType{{0, 0, 1}}, 4.0F);
    sliceLabels->SetPixel(Image::IndexType{{3, 4, 4}}, 4.0F);
    Annotation sliceAnnotation(
        "slice-labels.nii.gz",
        {},
        std::make_shared<Volume>(sliceLabels),
        AnnotationKind::LabelMap);
    const auto nearestSlice =
        sliceAnnotation.nearestAxialSlicePointContainingLabel(
            4,
            sliceAnnotation.volume().geometry().indexToPhysical(
                {{2.0, 2.0, 3.0}}));
    const auto nearestIndex = nearestSlice
        ? sliceAnnotation.volume().geometry().physicalToContinuousIndex(
              *nearestSlice)
        : radmarky::core::ImageGeometry::Vector{};
    passed &= nearestSlice && std::abs(nearestIndex[2] - 4.0) < 1.0e-8;
    passed &= !sliceAnnotation.nearestAxialSlicePointContainingLabel(
        8,
        sliceAnnotation.volume().geometry().indexToPhysical(
            {{2.0, 2.0, 3.0}}));

    auto permutedLabels = makeImage();
    Image::DirectionType permutedDirection;
    permutedDirection.Fill(0.0);
    permutedDirection[0][1] = 1.0;
    permutedDirection[1][2] = 1.0;
    permutedDirection[2][0] = 1.0;
    permutedLabels->SetDirection(permutedDirection);
    permutedLabels->SetPixel(Image::IndexType{{3, 2, 2}}, 5.0F);
    Annotation permutedSliceAnnotation(
        "permuted-slice-labels.nii.gz",
        {},
        std::make_shared<Volume>(permutedLabels),
        AnnotationKind::LabelMap);
    const auto permutedNearest =
        permutedSliceAnnotation.nearestAxialSlicePointContainingLabel(
            5,
            permutedSliceAnnotation.volume().geometry().indexToPhysical(
                {{1.0, 2.0, 2.0}}));
    const auto permutedNearestIndex = permutedNearest
        ? permutedSliceAnnotation.volume().geometry().physicalToContinuousIndex(
              *permutedNearest)
        : radmarky::core::ImageGeometry::Vector{};
    passed &= permutedNearest
        && std::abs(permutedNearestIndex[0] - 3.0) < 1.0e-8;

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

    auto obliquePrimary = std::make_shared<Volume>(makeObliqueImage());
    const auto obliqueBlank =
        Annotation::createBlankLabelMap("oblique-blank", *obliquePrimary);
    obliqueBlank->verifyGeometry(obliquePrimary->geometry());
    passed &= obliqueBlank->volume().geometry().dimensions()
        != obliquePrimary->geometry().dimensions();
    passed &= std::abs(
                  obliqueBlank->volume().geometry().direction()[2][2] + 1.0)
        < 1.0e-12;

    Annotation importedOblique(
        "oblique-labels.nii.gz",
        {},
        std::make_shared<Volume>(makeObliqueImage(3.0F)),
        AnnotationKind::LabelMap);
    passed &= expectThrows(
        [&] { importedOblique.verifyGeometry(obliquePrimary->geometry()); },
        "native oblique label grid before conforming");
    importedOblique.conformGeometry(obliquePrimary->geometry());
    importedOblique.verifyGeometry(obliquePrimary->geometry());
    const auto conformedDimensions =
        importedOblique.volume().geometry().dimensions();
    importedOblique.conformGeometry(obliquePrimary->geometry());
    passed &= importedOblique.volume().geometry().dimensions()
        == conformedDimensions;
    const auto obliqueCenter =
        obliquePrimary->geometry().indexToPhysical({{4.0, 3.0, 2.0}});
    const auto conformedSample =
        importedOblique.volume().sampleNearestPhysical(obliqueCenter);
    passed &= conformedSample && conformedSample->value == 3.0F;

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
