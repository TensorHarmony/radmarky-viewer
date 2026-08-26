#include "core/BrushGeometry.h"
#include "core/ImageGeometry.h"
#include "core/OrthogonalSliceGeometry.h"
#include "core/PhysicalMeasurement.h"
#include "core/ViewerState.h"
#include "core/Volume.h"

#include <itkImage.h>

#include <array>
#include <cmath>
#include <iostream>
#include <string_view>

namespace
{

bool expectNear(
    const double actual,
    const double expected,
    const std::string_view field,
    const double tolerance = 1.0e-9)
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

bool expectVectorNear(
    const radmarky::core::ImageGeometry::Vector& actual,
    const radmarky::core::ImageGeometry::Vector& expected,
    const std::string_view field)
{
    bool passed = true;
    for(std::size_t axis = 0; axis < 3; ++axis)
    {
        passed &= expectNear(actual[axis], expected[axis], field);
    }
    return passed;
}

bool expectOutlinePointNear(
    const radmarky::core::BrushOutlinePoint& actual,
    const radmarky::core::BrushOutlinePoint& expected,
    const std::string_view field)
{
    return expectNear(actual[0], expected[0], field)
        && expectNear(actual[1], expected[1], field);
}

} // namespace

int main()
{
    using Image = radmarky::core::Volume::ImageType;
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

    Image::DirectionType direction;
    direction.SetIdentity();
    direction[0][0] = 0.0;
    direction[0][1] = -1.0;
    direction[1][0] = 1.0;
    direction[1][1] = 0.0;
    image->SetDirection(direction);
    image->Allocate();
    image->FillBuffer(-1024.0F);
    Image::IndexType sampleIndex{{2, 3, 4}};
    image->SetPixel(sampleIndex, 321.0F);

    const radmarky::core::ImageGeometry geometry(*image);
    bool passed = true;
    passed &= expectTrue(
        geometry.dimensions() == radmarky::core::ImageGeometry::Dimensions{{4, 5, 6}},
        "dimensions");
    passed &= expectNear(geometry.spacing()[0], 0.7, "spacing x");
    passed &= expectNear(geometry.spacing()[1], 1.2, "spacing y");
    passed &= expectNear(geometry.spacing()[2], 2.5, "spacing z");
    passed &= expectNear(geometry.origin()[0], 10.0, "origin x");
    passed &= expectNear(geometry.origin()[1], -20.0, "origin y");
    passed &= expectNear(geometry.origin()[2], 30.0, "origin z");
    passed &= expectNear(geometry.direction()[0][1], -1.0, "direction 0,1");
    passed &= expectNear(geometry.direction()[1][0], 1.0, "direction 1,0");

    const radmarky::core::ImageGeometry::Vector index{{2.25, 1.5, 4.0}};
    const auto point = geometry.indexToPhysical(index);
    passed &= expectNear(point[0], 8.2, "physical x");
    passed &= expectNear(point[1], -18.425, "physical y");
    passed &= expectNear(point[2], 40.0, "physical z");

    const auto roundTrip = geometry.physicalToContinuousIndex(point);
    for(std::size_t axis = 0; axis < 3; ++axis)
    {
        passed &= expectNear(roundTrip[axis], index[axis], "index round trip");
    }
    passed &= expectNear(
        radmarky::core::physicalDistanceMillimetres(
            {{1.0, 2.0, 3.0}}, {{4.0, 6.0, 3.0}}),
        5.0,
        "physical measurement 3-4-5");
    const auto measurementStart = geometry.indexToPhysical({{0.0, 0.0, 0.0}});
    const auto measurementEnd = geometry.indexToPhysical({{3.0, 4.0, 5.0}});
    passed &= expectNear(
        radmarky::core::physicalDistanceMillimetres(
            measurementStart, measurementEnd),
        std::sqrt(3.0 * 3.0 * 0.7 * 0.7
                  + 4.0 * 4.0 * 1.2 * 1.2
                  + 5.0 * 5.0 * 2.5 * 2.5),
        "physical measurement anisotropic oblique");

    const radmarky::core::Volume sampledVolume(image);
    passed &= expectNear(
        sampledVolume.scalarRange().minimum, -1024.0, "volume scalar min");
    passed &= expectNear(
        sampledVolume.scalarRange().maximum, 321.0, "volume scalar max");
    const auto voxelSample = sampledVolume.sampleNearestPhysical(
        geometry.indexToPhysical({{2.2, 2.8, 4.1}}));
    passed &= expectTrue(voxelSample.has_value(), "physical voxel sample exists");
    if(voxelSample)
    {
        passed &= expectNear(voxelSample->value, 321.0, "physical voxel sample value");
        passed &= expectTrue(
            voxelSample->index == std::array<std::size_t, 3>{{2, 3, 4}},
            "physical voxel sample index");
    }
    passed &= expectTrue(
        !sampledVolume
             .sampleNearestPhysical(geometry.indexToPhysical({{-1.0, 0.0, 0.0}}))
             .has_value(),
        "outside physical voxel sample");

    const auto axial = radmarky::core::OrthogonalSliceGeometry::fromImageGeometry(
        geometry, radmarky::core::SliceOrientation::Axial);
    const radmarky::core::ImageGeometry::Vector brushCenter{{2.0, 2.0, 3.0}};
    const auto projectBrushOffset =
        [&](const double dx, const double dy) {
            auto indexAtOffset = brushCenter;
            indexAtOffset[0] += dx;
            indexAtOffset[1] += dy;
            const auto physical = geometry.indexToPhysical(indexAtOffset);
            return radmarky::core::BrushOutlinePoint{{
                axial.horizontalCoordinate(physical),
                axial.verticalCoordinate(physical),
            }};
        };
    for(int brushSize = 1; brushSize <= 6; ++brushSize)
    {
        for(const auto shape : {
                radmarky::core::BrushShape::Square,
                radmarky::core::BrushShape::Circle})
        {
            const radmarky::core::BrushFootprint footprint(brushSize, shape);
            const auto outline = radmarky::core::brushOutlinePoints(
                footprint, geometry, axial, brushCenter);
            const std::size_t expectedPointCount =
                shape == radmarky::core::BrushShape::Square ? 4U : 48U;
            passed &= expectTrue(
                outline.size() == expectedPointCount,
                "brush outline point count");

            radmarky::core::BrushOutlinePoint outlineCenter{{0.0, 0.0}};
            for(const auto& outlinePoint : outline)
            {
                outlineCenter[0] += outlinePoint[0];
                outlineCenter[1] += outlinePoint[1];
            }
            outlineCenter[0] /= static_cast<double>(outline.size());
            outlineCenter[1] /= static_cast<double>(outline.size());
            passed &= expectOutlinePointNear(
                outlineCenter,
                projectBrushOffset(
                    footprint.centerOffset(), footprint.centerOffset()),
                "direction-aware brush center");

            const double centerOffset = footprint.centerOffset();
            if(shape == radmarky::core::BrushShape::Square)
            {
                const double lower =
                    static_cast<double>(footprint.firstOffset()) - 0.5;
                const double upper =
                    static_cast<double>(footprint.lastOffset()) + 0.5;
                const std::array<radmarky::core::BrushOutlinePoint, 4>
                    expectedCorners{{
                        projectBrushOffset(lower, lower),
                        projectBrushOffset(upper, lower),
                        projectBrushOffset(upper, upper),
                        projectBrushOffset(lower, upper),
                    }};
                for(std::size_t corner = 0; corner < expectedCorners.size(); ++corner)
                {
                    passed &= expectOutlinePointNear(
                        outline[corner],
                        expectedCorners[corner],
                        "direction-aware square corner");
                }
            }
            else
            {
                const double radius = footprint.radius();
                const std::array<radmarky::core::BrushOutlinePoint, 4>
                    expectedCardinalPoints{{
                        projectBrushOffset(centerOffset + radius, centerOffset),
                        projectBrushOffset(centerOffset, centerOffset + radius),
                        projectBrushOffset(centerOffset - radius, centerOffset),
                        projectBrushOffset(centerOffset, centerOffset - radius),
                    }};
                for(std::size_t cardinal = 0;
                    cardinal < expectedCardinalPoints.size();
                    ++cardinal)
                {
                    passed &= expectOutlinePointNear(
                        outline[cardinal * 12],
                        expectedCardinalPoints[cardinal],
                        "direction-aware circle cardinal point");
                }
            }
        }
    }
    // In patient LPS, RPS/AIR/RIP correspond to these screen horizontal,
    // vertical, and normal directions for axial, sagittal, and coronal views.
    passed &= expectVectorNear(
        axial.horizontalDirectionLps(), {{1.0, 0.0, 0.0}}, "axial R direction");
    passed &= expectVectorNear(
        axial.verticalDirectionLps(), {{0.0, -1.0, 0.0}}, "axial P direction");
    passed &= expectVectorNear(
        axial.normalDirectionLps(), {{0.0, 0.0, -1.0}}, "axial S direction");
    const auto center = axial.referenceLps();
    auto patientRight = center;
    auto patientLeft = center;
    patientRight[0] -= 10.0;
    patientLeft[0] += 10.0;
    passed &= expectTrue(
        axial.horizontalCoordinate(patientRight)
            < axial.horizontalCoordinate(patientLeft),
        "radiological right-to-left horizontal orientation");

    auto patientAnterior = center;
    auto patientPosterior = center;
    patientAnterior[1] -= 10.0;
    patientPosterior[1] += 10.0;
    passed &= expectTrue(
        axial.verticalCoordinate(patientAnterior)
            > axial.verticalCoordinate(patientPosterior),
        "anterior-at-top vertical orientation");
    passed &= expectTrue(axial.width() > 0 && axial.height() > 0, "axial extent");
    passed &= expectNear(axial.sliceStep(), 2.5, "axial physical slice step");
    passed &= expectTrue(
        axial.normalMinimum() < 0.0 && axial.normalMaximum() > 0.0,
        "axial normal range surrounds center");

    const auto sagittal =
        radmarky::core::OrthogonalSliceGeometry::fromImageGeometry(
            geometry, radmarky::core::SliceOrientation::Sagittal);
    passed &= expectVectorNear(
        sagittal.horizontalDirectionLps(),
        {{0.0, 1.0, 0.0}},
        "sagittal A direction");
    passed &= expectVectorNear(
        sagittal.verticalDirectionLps(),
        {{0.0, 0.0, 1.0}},
        "sagittal I direction");
    passed &= expectVectorNear(
        sagittal.normalDirectionLps(),
        {{1.0, 0.0, 0.0}},
        "sagittal R direction");
    passed &= expectTrue(
        sagittal.horizontalCoordinate(patientAnterior)
            < sagittal.horizontalCoordinate(patientPosterior),
        "sagittal anterior-at-left orientation");
    auto patientSuperior = center;
    auto patientInferior = center;
    patientSuperior[2] += 10.0;
    patientInferior[2] -= 10.0;
    passed &= expectTrue(
        sagittal.verticalCoordinate(patientSuperior)
            > sagittal.verticalCoordinate(patientInferior),
        "sagittal superior-at-top orientation");
    passed &= expectNear(sagittal.sliceStep(), 1.2, "sagittal physical slice step");

    const auto coronal =
        radmarky::core::OrthogonalSliceGeometry::fromImageGeometry(
            geometry, radmarky::core::SliceOrientation::Coronal);
    passed &= expectVectorNear(
        coronal.horizontalDirectionLps(),
        {{1.0, 0.0, 0.0}},
        "coronal R direction");
    passed &= expectVectorNear(
        coronal.verticalDirectionLps(),
        {{0.0, 0.0, 1.0}},
        "coronal I direction");
    passed &= expectVectorNear(
        coronal.normalDirectionLps(),
        {{0.0, -1.0, 0.0}},
        "coronal P direction");
    passed &= expectTrue(
        coronal.horizontalCoordinate(patientRight)
            < coronal.horizontalCoordinate(patientLeft),
        "coronal right-to-left horizontal orientation");
    passed &= expectTrue(
        coronal.verticalCoordinate(patientSuperior)
            > coronal.verticalCoordinate(patientInferior),
        "coronal superior-at-top orientation");
    passed &= expectNear(coronal.sliceStep(), 0.7, "coronal physical slice step");

    auto movedCursor = center;
    movedCursor[0] += 3.0;
    movedCursor[1] += 4.0;
    movedCursor[2] += 5.0;
    const auto axialOrigin = axial.planeOriginForCursor(movedCursor);
    passed &= expectNear(axialOrigin[0], center[0], "axial fixed in-plane x");
    passed &= expectNear(axialOrigin[1], center[1], "axial fixed in-plane y");
    passed &= expectNear(axialOrigin[2], movedCursor[2], "axial cursor plane z");

    radmarky::core::ViewerState state(geometry);
    const auto initialCursorIndex = state.cursorContinuousIndex();
    passed &= expectNear(initialCursorIndex[0], 2.0, "initial cursor x");
    passed &= expectNear(initialCursorIndex[1], 2.0, "initial cursor y");
    passed &= expectNear(initialCursorIndex[2], 3.0, "initial cursor z");

    const int maximumAxialPosition = static_cast<int>(size[2]) - 1;
    const int initialAxialPosition = static_cast<int>(std::llround(
        state.cursorNormalFraction(axial) * maximumAxialPosition));
    const int initialAxialSlice = static_cast<int>(
        std::llround(initialCursorIndex[2])) + 1;
    radmarky::core::ViewerState previousSliceState(geometry);
    previousSliceState.setCursorNormalFraction(
        axial,
        static_cast<double>(initialAxialPosition - 1) / maximumAxialPosition);
    radmarky::core::ViewerState nextSliceState(geometry);
    nextSliceState.setCursorNormalFraction(
        axial,
        static_cast<double>(initialAxialPosition + 1) / maximumAxialPosition);
    const int previousAxialSlice = static_cast<int>(std::llround(
        previousSliceState.cursorContinuousIndex()[2])) + 1;
    const int nextAxialSlice = static_cast<int>(std::llround(
        nextSliceState.cursorContinuousIndex()[2])) + 1;
    passed &= expectTrue(
        previousAxialSlice != initialAxialSlice,
        "previous scrollbar position has a distinct slice number");
    passed &= expectTrue(
        nextAxialSlice != initialAxialSlice,
        "next scrollbar position has a distinct slice number");

    const radmarky::core::ImageGeometry::Vector outsideIndex{{10.0, -4.0, 20.0}};
    state.setCursorPhysical(geometry.indexToPhysical(outsideIndex));
    const auto clampedIndex = state.cursorContinuousIndex();
    passed &= expectNear(clampedIndex[0], 3.0, "clamped cursor x");
    passed &= expectNear(clampedIndex[1], 0.0, "clamped cursor y");
    passed &= expectNear(clampedIndex[2], 5.0, "clamped cursor z");

    radmarky::core::ViewerState navigationState(geometry);
    const auto beforeStep = navigationState.cursorContinuousIndex();
    navigationState.stepCursor(sagittal, 1);
    const auto afterStep = navigationState.cursorContinuousIndex();
    double stepDistanceSquared = 0.0;
    for(std::size_t axis = 0; axis < 3; ++axis)
    {
        const double difference = afterStep[axis] - beforeStep[axis];
        stepDistanceSquared += difference * difference;
    }
    passed &= expectNear(
        std::sqrt(stepDistanceSquared), 1.0, "one wheel step in index space");

    navigationState.setCursorNormalFraction(axial, 0.0);
    passed &= expectNear(
        navigationState.cursorNormalFraction(axial),
        0.0,
        "slice scrollbar lower boundary");
    navigationState.setCursorNormalFraction(axial, 1.0);
    passed &= expectNear(
        navigationState.cursorNormalFraction(axial),
        1.0,
        "slice scrollbar upper boundary");

    navigationState.setIntensityRange(-1024.0, 321.0);
    passed &= expectNear(
        navigationState.windowLevel().window(), 1345.0, "viewer window from range");
    passed &= expectNear(
        navigationState.windowLevel().level(), -351.5, "viewer level from range");
    navigationState.setWindowLevel(400.0, 40.0);
    passed &= expectNear(navigationState.windowLevel().window(), 400.0, "set window");
    passed &= expectNear(navigationState.windowLevel().level(), 40.0, "set level");
    navigationState.resetWindowLevel();
    passed &= expectNear(
        navigationState.windowLevel().window(), 1345.0, "reset viewer window");
    passed &= expectTrue(!navigationState.inverted(), "viewer starts uninverted");
    navigationState.setInverted(true);
    passed &= expectTrue(navigationState.inverted(), "viewer inversion enabled");
    navigationState.setInverted(false);
    passed &= expectTrue(!navigationState.inverted(), "viewer inversion disabled");

    return passed ? 0 : 1;
}
