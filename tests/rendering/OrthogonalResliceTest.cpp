#include "core/OrthogonalSliceGeometry.h"
#include "core/Volume.h"
#include "rendering/ItkVtkImageBridge.h"
#include "rendering/OrthogonalReslice.h"

#include <vtkImageData.h>
#include <vtkImageReslice.h>
#include <vtkNew.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string_view>
#include <vector>

namespace
{

bool expectNear(
    const double actual,
    const double expected,
    const std::string_view field,
    const double tolerance = 1.0e-6)
{
    if(std::abs(actual - expected) <= tolerance)
    {
        return true;
    }
    std::cerr << field << ": expected " << expected << ", got " << actual << '\n';
    return false;
}

bool expectTrue(const bool condition, const std::string_view field)
{
    if(condition)
    {
        return true;
    }
    std::cerr << field << ": expected true\n";
    return false;
}

double valueAtIndex(const radmarky::core::ImageGeometry::Vector& index)
{
    return 1.0 + index[0] + 10.0 * index[1] + 100.0 * index[2];
}

} // namespace

int main()
{
    using Image = radmarky::core::Volume::ImageType;
    auto image = Image::New();

    Image::SizeType size{{7, 9, 11}};
    Image::RegionType region;
    region.SetSize(size);
    image->SetRegions(region);

    Image::SpacingType spacing;
    spacing.Fill(1.0);
    image->SetSpacing(spacing);

    Image::PointType origin;
    origin[0] = 53.0;
    origin[1] = -27.0;
    origin[2] = 104.0;
    image->SetOrigin(origin);

    Image::DirectionType direction;
    direction.SetIdentity();
    direction[0][0] = 0.0;
    direction[0][1] = -1.0;
    direction[1][0] = 1.0;
    direction[1][1] = 0.0;
    image->SetDirection(direction);
    image->Allocate();

    for(unsigned int z = 0; z < size[2]; ++z)
    {
        for(unsigned int y = 0; y < size[1]; ++y)
        {
            for(unsigned int x = 0; x < size[0]; ++x)
            {
                image->SetPixel(
                    {{x, y, z}},
                    static_cast<float>(1.0 + x + 10.0 * y + 100.0 * z));
            }
        }
    }

    const radmarky::core::Volume volume(image);
    const auto vtkImage = radmarky::rendering::ItkVtkImageBridge::shareWithVtk(volume);
    const auto& geometry = volume.geometry();
    const radmarky::core::ImageGeometry::Vector centerIndex{{3.0, 4.0, 5.0}};
    const auto centerPhysical = geometry.indexToPhysical(centerIndex);

    const std::array orientations{
        radmarky::core::SliceOrientation::Axial,
        radmarky::core::SliceOrientation::Sagittal,
        radmarky::core::SliceOrientation::Coronal,
    };

    bool passed = true;
    passed &= expectTrue(
        vtkImage->GetScalarPointer() == image->GetBufferPointer(),
        "VTK bridge shares the ITK voxel buffer");

    radmarky::core::Volume colorVolume(image);
    const auto pixelCount =
        image->GetLargestPossibleRegion().GetNumberOfPixels();
    std::vector<std::uint8_t> rgb(pixelCount * 3);
    rgb[0] = 17;
    rgb[1] = 83;
    rgb[2] = 241;
    colorVolume.setDisplayRgb(std::move(rgb));
    const auto colorVtk =
        radmarky::rendering::ItkVtkImageBridge::shareWithVtk(colorVolume);
    passed &= expectTrue(
        colorVtk->GetScalarType() == VTK_UNSIGNED_CHAR,
        "color display uses byte components");
    passed &= expectTrue(
        colorVtk->GetNumberOfScalarComponents() == 3,
        "color display retains RGB components");
    passed &= expectNear(
        colorVtk->GetScalarComponentAsDouble(0, 0, 0, 0),
        17.0,
        "color display red component");
    passed &= expectNear(
        colorVtk->GetScalarComponentAsDouble(0, 0, 0, 1),
        83.0,
        "color display green component");
    passed &= expectNear(
        colorVtk->GetScalarComponentAsDouble(0, 0, 0, 2),
        241.0,
        "color display blue component");

    auto firstComparisonImage = Image::New();
    firstComparisonImage->CopyInformation(image);
    firstComparisonImage->SetRegions(region);
    firstComparisonImage->Allocate();
    firstComparisonImage->FillBuffer(0.0F);
    auto secondComparisonImage = Image::New();
    secondComparisonImage->CopyInformation(image);
    secondComparisonImage->SetRegions(region);
    secondComparisonImage->Allocate();
    secondComparisonImage->FillBuffer(0.0F);
    const std::array<std::array<float, 2>, 5> comparisonValues{{
        {{0.0F, 0.0F}},
        {{1.0F, 0.0F}},
        {{2.0F, 2.0F}},
        {{0.0F, 3.0F}},
        {{4.0F, 5.0F}},
    }};
    for(std::size_t x = 0; x < comparisonValues.size(); ++x)
    {
        const Image::IndexType index{{
            static_cast<Image::IndexType::IndexValueType>(x), 0, 0}};
        firstComparisonImage->SetPixel(index, comparisonValues[x][0]);
        secondComparisonImage->SetPixel(index, comparisonValues[x][1]);
    }
    const radmarky::core::Volume firstComparison(firstComparisonImage);
    const radmarky::core::Volume secondComparison(secondComparisonImage);
    const auto comparison =
        radmarky::rendering::ItkVtkImageBridge::copyComparisonToVtk(
            firstComparison, secondComparison);
    passed &= expectTrue(
        comparison->GetScalarType() == VTK_UNSIGNED_CHAR,
        "comparison uses byte labels");
    for(std::size_t x = 0; x < comparisonValues.size(); ++x)
    {
        passed &= expectNear(
            comparison->GetScalarComponentAsDouble(
                static_cast<int>(x), 0, 0, 0),
            static_cast<double>(x),
            "annotation comparison class");
    }

    for(const auto orientation : orientations)
    {
        const auto slice =
            radmarky::core::OrthogonalSliceGeometry::fromImageGeometry(
                geometry, orientation);
        vtkNew<vtkImageReslice> reslice;
        radmarky::rendering::configureOrthogonalReslice(
            *reslice, vtkImage, slice, centerPhysical);

        auto* const output = reslice->GetOutput();
        double range[2]{};
        output->GetScalarRange(range);
        passed &= expectTrue(range[1] > range[0], "reslice contains voxel variation");

        const int outputX = static_cast<int>(std::llround(
            -slice.horizontalMinimum() / slice.outputSpacing()));
        const int outputY = static_cast<int>(std::llround(
            -slice.verticalMinimum() / slice.outputSpacing()));
        passed &= expectNear(
            output->GetScalarComponentAsDouble(outputX, outputY, 0, 0),
            valueAtIndex(centerIndex),
            "center reslice voxel");

        if(orientation == radmarky::core::SliceOrientation::Axial
           || orientation == radmarky::core::SliceOrientation::Coronal)
        {
            auto patientRight = centerPhysical;
            auto patientLeft = centerPhysical;
            patientRight[0] -= 1.0;
            patientLeft[0] += 1.0;
            const int rightScreenX = static_cast<int>(std::llround(
                (slice.horizontalCoordinate(patientRight)
                 - slice.horizontalMinimum())
                / slice.outputSpacing()));
            const int leftScreenX = static_cast<int>(std::llround(
                (slice.horizontalCoordinate(patientLeft)
                 - slice.horizontalMinimum())
                / slice.outputSpacing()));
            passed &= expectTrue(
                rightScreenX < leftScreenX,
                "radiological screen-left is patient-right");
            passed &= expectNear(
                output->GetScalarComponentAsDouble(rightScreenX, outputY, 0, 0),
                valueAtIndex(
                    geometry.physicalToContinuousIndex(patientRight)),
                "patient-right marker voxel");
            passed &= expectNear(
                output->GetScalarComponentAsDouble(leftScreenX, outputY, 0, 0),
                valueAtIndex(geometry.physicalToContinuousIndex(patientLeft)),
                "patient-left marker voxel");
        }

        const auto horizontalPoint =
            slice.pointOnCursorPlane(1.0, 0.0, centerPhysical);
        const auto verticalPoint =
            slice.pointOnCursorPlane(0.0, 1.0, centerPhysical);
        passed &= expectNear(
            output->GetScalarComponentAsDouble(outputX + 1, outputY, 0, 0),
            valueAtIndex(geometry.physicalToContinuousIndex(horizontalPoint)),
            "positive horizontal screen direction");
        passed &= expectNear(
            output->GetScalarComponentAsDouble(outputX, outputY + 1, 0, 0),
            valueAtIndex(geometry.physicalToContinuousIndex(verticalPoint)),
            "positive vertical screen direction");

        auto movedCursor = centerPhysical;
        const auto& normal = slice.normalDirectionLps();
        for(std::size_t axis = 0; axis < 3; ++axis)
        {
            movedCursor[axis] += normal[axis];
        }
        radmarky::rendering::setOrthogonalResliceCursor(
            *reslice, slice, movedCursor);
        reslice->Update();
        const auto movedIndex = geometry.physicalToContinuousIndex(
            slice.pointOnCursorPlane(0.0, 0.0, movedCursor));
        passed &= expectNear(
            reslice->GetOutput()->GetScalarComponentAsDouble(
                outputX, outputY, 0, 0),
            valueAtIndex(movedIndex),
            "moved cursor reslice voxel");
    }

    const auto axialSlice =
        radmarky::core::OrthogonalSliceGeometry::fromImageGeometry(
            geometry, radmarky::core::SliceOrientation::Axial);
    vtkNew<vtkImageReslice> nanBackgroundReslice;
    radmarky::rendering::configureOrthogonalReslice(
        *nanBackgroundReslice,
        vtkImage,
        axialSlice,
        centerPhysical,
        std::numeric_limits<double>::quiet_NaN());
    passed &= expectTrue(
        std::isnan(nanBackgroundReslice->GetBackgroundLevel()),
        "reslice preserves a NaN anatomical background");

    // A single-frame oblique DICOM is a 2-D acquisition plane. Its primary
    // view must show the full native image instead of the thin intersection
    // produced by patient-orthogonal reslicing.
    auto singleFrame = Image::New();
    Image::RegionType singleFrameRegion;
    singleFrameRegion.SetSize({{5, 4, 1}});
    singleFrame->SetRegions(singleFrameRegion);
    Image::SpacingType singleFrameSpacing;
    singleFrameSpacing[0] = 1.0;
    singleFrameSpacing[1] = 1.0;
    singleFrameSpacing[2] = 7.5;
    singleFrame->SetSpacing(singleFrameSpacing);
    constexpr double obliqueSine = 0.280292;
    const double obliqueCosine = std::sqrt(1.0 - obliqueSine * obliqueSine);
    Image::DirectionType singleFrameDirection;
    singleFrameDirection.SetIdentity();
    singleFrameDirection[1][1] = obliqueCosine;
    singleFrameDirection[1][2] = obliqueSine;
    singleFrameDirection[2][1] = -obliqueSine;
    singleFrameDirection[2][2] = obliqueCosine;
    singleFrame->SetDirection(singleFrameDirection);
    singleFrame->Allocate();
    for(unsigned int y = 0; y < 4; ++y)
    {
        for(unsigned int x = 0; x < 5; ++x)
        {
            singleFrame->SetPixel(
                {{x, y, 0}}, static_cast<float>(1 + x + 10 * y));
        }
    }

    const radmarky::core::Volume singleFrameVolume(singleFrame);
    const auto singleFrameVtk =
        radmarky::rendering::ItkVtkImageBridge::shareWithVtk(singleFrameVolume);
    const auto& singleFrameGeometry = singleFrameVolume.geometry();
    const auto singleFrameAxial =
        radmarky::core::OrthogonalSliceGeometry::fromImageGeometry(
            singleFrameGeometry, radmarky::core::SliceOrientation::Axial);
    passed &= expectNear(
        singleFrameAxial.verticalDirectionLps()[1],
        -obliqueCosine,
        "single-frame axial follows native vertical direction");
    passed &= expectNear(
        singleFrameAxial.verticalDirectionLps()[2],
        obliqueSine,
        "single-frame obliquity retained in native view");
    passed &= expectNear(
        singleFrameAxial.normalMinimum(),
        singleFrameAxial.normalMaximum(),
        "single-frame axial has one native plane");
    passed &= expectTrue(
        singleFrameAxial.width() == 5 && singleFrameAxial.height() == 4,
        "single-frame axial uses full native extent");

    vtkNew<vtkImageReslice> singleFrameReslice;
    radmarky::rendering::configureOrthogonalReslice(
        *singleFrameReslice,
        singleFrameVtk,
        singleFrameAxial,
        singleFrameGeometry.indexToPhysical({{2.0, 1.5, 0.0}}));
    double singleFrameRange[2]{};
    singleFrameReslice->GetOutput()->GetScalarRange(singleFrameRange);
    passed &= expectNear(singleFrameRange[0], 1.0, "single-frame full image minimum");
    passed &= expectNear(singleFrameRange[1], 35.0, "single-frame full image maximum");

    for(const auto secondaryOrientation : {
            radmarky::core::SliceOrientation::Sagittal,
            radmarky::core::SliceOrientation::Coronal})
    {
        const auto secondarySlice =
            radmarky::core::OrthogonalSliceGeometry::fromImageGeometry(
                singleFrameGeometry, secondaryOrientation);
        passed &= expectNear(
            secondarySlice.verticalDirectionLps()[1],
            obliqueSine,
            "single-frame secondary view follows native singleton axis");
        passed &= expectNear(
            secondarySlice.verticalDirectionLps()[2],
            obliqueCosine,
            "single-frame secondary view retains native obliquity");
        vtkNew<vtkImageReslice> secondaryReslice;
        radmarky::rendering::configureOrthogonalReslice(
            *secondaryReslice,
            singleFrameVtk,
            secondarySlice,
            singleFrameGeometry.indexToPhysical({{2.0, 1.5, 0.0}}));
        double secondaryRange[2]{};
        secondaryReslice->GetOutput()->GetScalarRange(secondaryRange);
        passed &= expectTrue(
            secondarySlice.width() > 1 && secondarySlice.height() > 1,
            "single-frame secondary view has non-degenerate extent");
        passed &= expectTrue(
            secondaryRange[1] > 0.0,
            "single-frame secondary view contains visible pixels");
    }

    return passed ? 0 : 1;
}
