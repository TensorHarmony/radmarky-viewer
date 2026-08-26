#include "core/ViewerState.h"

#include "core/OrthogonalSliceGeometry.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace radmarky::core
{

ViewerState::ViewerState(ImageGeometry geometry)
    : geometry_(geometry)
{
    const auto& dimensions = geometry_.dimensions();
    // Start on an actual voxel center. For an even-sized axis, the geometric
    // midpoint lies halfway between two voxels and an integer scrollbar step
    // can then appear to repeat the initially displayed slice number.
    const Vector centerIndex{{
        static_cast<double>(dimensions[0] / 2),
        static_cast<double>(dimensions[1] / 2),
        static_cast<double>(dimensions[2] / 2),
    }};
    cursorPhysical_ = geometry_.indexToPhysical(centerIndex);
}

const ImageGeometry& ViewerState::geometry() const noexcept
{
    return geometry_;
}

const ViewerState::Vector& ViewerState::cursorPhysical() const noexcept
{
    return cursorPhysical_;
}

ViewerState::Vector ViewerState::cursorContinuousIndex() const
{
    return geometry_.physicalToContinuousIndex(cursorPhysical_);
}

void ViewerState::setCursorPhysical(const Vector& point)
{
    for(const double coordinate : point)
    {
        if(!std::isfinite(coordinate))
        {
            throw std::invalid_argument("Cursor coordinates must be finite");
        }
    }

    auto index = geometry_.physicalToContinuousIndex(point);
    const auto& dimensions = geometry_.dimensions();
    for(std::size_t axis = 0; axis < 3; ++axis)
    {
        index[axis] = std::clamp(
            index[axis], 0.0, static_cast<double>(dimensions[axis] - 1));
    }
    cursorPhysical_ = geometry_.indexToPhysical(index);
}

void ViewerState::stepCursor(
    const OrthogonalSliceGeometry& slice,
    const int steps)
{
    if(steps == 0)
    {
        return;
    }
    auto point = cursorPhysical_;
    const auto& normal = slice.normalDirectionLps();
    const double distance = slice.sliceStep() * steps;
    for(std::size_t axis = 0; axis < 3; ++axis)
    {
        point[axis] += normal[axis] * distance;
    }
    setCursorPhysical(point);
}

void ViewerState::setCursorNormalFraction(
    const OrthogonalSliceGeometry& slice,
    const double fraction)
{
    if(!std::isfinite(fraction))
    {
        throw std::invalid_argument("Slice position fraction must be finite");
    }
    const double clampedFraction = std::clamp(fraction, 0.0, 1.0);
    const double targetNormal = slice.normalMinimum()
        + clampedFraction * (slice.normalMaximum() - slice.normalMinimum());
    auto point = cursorPhysical_;
    const double distance = targetNormal - slice.normalCoordinate(point);
    const auto& normal = slice.normalDirectionLps();
    for(std::size_t axis = 0; axis < 3; ++axis)
    {
        point[axis] += normal[axis] * distance;
    }
    setCursorPhysical(point);
}

double ViewerState::cursorNormalFraction(
    const OrthogonalSliceGeometry& slice) const
{
    const double range = slice.normalMaximum() - slice.normalMinimum();
    if(range <= 0.0)
    {
        return 0.0;
    }
    return std::clamp(
        (slice.normalCoordinate(cursorPhysical_) - slice.normalMinimum()) / range,
        0.0,
        1.0);
}

const WindowLevel& ViewerState::windowLevel() const noexcept
{
    return windowLevel_;
}

bool ViewerState::inverted() const noexcept
{
    return inverted_;
}

void ViewerState::setIntensityRange(const double minimum, const double maximum)
{
    windowLevel_ = WindowLevel::fromIntensityRange(minimum, maximum);
}

void ViewerState::setWindowLevel(const double window, const double level)
{
    windowLevel_.set(window, level);
}

void ViewerState::applyWindowLevelPreset(const WindowLevelPreset preset)
{
    windowLevel_ = WindowLevel::fromPreset(
        preset,
        windowLevel_.intensityMinimum(),
        windowLevel_.intensityMaximum());
}

void ViewerState::resetWindowLevel()
{
    windowLevel_.reset();
}

void ViewerState::setInverted(const bool inverted) noexcept
{
    inverted_ = inverted;
}

} // namespace radmarky::core
