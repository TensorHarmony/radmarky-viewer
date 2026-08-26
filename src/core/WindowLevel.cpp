#include "core/WindowLevel.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace radmarky::core
{
namespace
{

struct PresetValues
{
    double window = 1.0;
    double level = 0.0;
};

PresetValues valuesFor(const WindowLevelPreset preset)
{
    switch(preset)
    {
    case WindowLevelPreset::CtSoftTissue:
        return {400.0, 40.0};
    case WindowLevelPreset::CtLung:
        return {1500.0, -600.0};
    case WindowLevelPreset::CtBone:
        return {2500.0, 480.0};
    case WindowLevelPreset::CtBrain:
        return {80.0, 40.0};
    case WindowLevelPreset::FullRange:
        break;
    }
    throw std::invalid_argument("Window/level preset does not have fixed values");
}

} // namespace

WindowLevel::WindowLevel(
    const double window,
    const double level,
    const double intensityMinimum,
    const double intensityMaximum)
    : window_(window)
    , level_(level)
    , intensityMinimum_(intensityMinimum)
    , intensityMaximum_(intensityMaximum)
{
    validate(window, level);
    if(!std::isfinite(intensityMinimum) || !std::isfinite(intensityMaximum))
    {
        throw std::invalid_argument("Intensity range values must be finite");
    }
    if(intensityMaximum < intensityMinimum)
    {
        throw std::invalid_argument(
            "Intensity range maximum must be at least the minimum");
    }
}

WindowLevel WindowLevel::fromIntensityRange(
    const double minimum,
    const double maximum)
{
    if(!std::isfinite(minimum) || !std::isfinite(maximum))
    {
        throw std::invalid_argument("Intensity range values must be finite");
    }
    if(maximum < minimum)
    {
        throw std::invalid_argument(
            "Intensity range maximum must be at least the minimum");
    }
    if(maximum == minimum)
    {
        return {1.0, minimum, minimum, maximum};
    }
    return {maximum - minimum, (minimum + maximum) / 2.0, minimum, maximum};
}

WindowLevel WindowLevel::fromPreset(
    const WindowLevelPreset preset,
    const double intensityMinimum,
    const double intensityMaximum)
{
    if(preset == WindowLevelPreset::FullRange)
    {
        return fromIntensityRange(intensityMinimum, intensityMaximum);
    }
    const auto values = valuesFor(preset);
    return {
        values.window,
        values.level,
        intensityMinimum,
        intensityMaximum,
    };
}

double WindowLevel::window() const noexcept
{
    return window_;
}

double WindowLevel::level() const noexcept
{
    return level_;
}

double WindowLevel::intensityMinimum() const noexcept
{
    return intensityMinimum_;
}

double WindowLevel::intensityMaximum() const noexcept
{
    return intensityMaximum_;
}

double WindowLevel::displayMinimum() const noexcept
{
    return level_ - window_ / 2.0;
}

double WindowLevel::displayMaximum() const noexcept
{
    return level_ + window_ / 2.0;
}

void WindowLevel::set(const double window, const double level)
{
    validate(window, level);
    window_ = window;
    level_ = level;
}

void WindowLevel::reset()
{
    *this = fromIntensityRange(intensityMinimum_, intensityMaximum_);
}

WindowLevel WindowLevel::dragged(
    const double normalizedDx,
    const double normalizedDy) const
{
    if(!std::isfinite(normalizedDx) || !std::isfinite(normalizedDy))
    {
        throw std::invalid_argument("Window/level drag deltas must be finite");
    }
    const double scale = std::max(std::abs(window_), 1.0);
    WindowLevel result = *this;
    result.set(
        std::max(window_ + normalizedDy * scale, minimumWindow),
        level_ + normalizedDx * scale);
    return result;
}

void WindowLevel::validate(const double window, const double level)
{
    if(!std::isfinite(window) || !std::isfinite(level))
    {
        throw std::invalid_argument("Window and level must be finite");
    }
    if(window < minimumWindow)
    {
        throw std::invalid_argument("Window must be greater than zero");
    }
}

} // namespace radmarky::core
