#pragma once

#include "core/ImageGeometry.h"
#include "core/WindowLevel.h"

namespace radmarky::core
{

class OrthogonalSliceGeometry;

class ViewerState
{
public:
    using Vector = ImageGeometry::Vector;

    explicit ViewerState(ImageGeometry geometry);

    [[nodiscard]] const ImageGeometry& geometry() const noexcept;
    [[nodiscard]] const Vector& cursorPhysical() const noexcept;
    [[nodiscard]] Vector cursorContinuousIndex() const;
    [[nodiscard]] const WindowLevel& windowLevel() const noexcept;
    [[nodiscard]] bool inverted() const noexcept;

    void setCursorPhysical(const Vector& point);
    void stepCursor(const OrthogonalSliceGeometry& slice, int steps);
    void setCursorNormalFraction(
        const OrthogonalSliceGeometry& slice,
        double fraction);
    [[nodiscard]] double cursorNormalFraction(
        const OrthogonalSliceGeometry& slice) const;
    void setIntensityRange(double minimum, double maximum);
    void setWindowLevel(double window, double level);
    void applyWindowLevelPreset(WindowLevelPreset preset);
    void resetWindowLevel();
    void setInverted(bool inverted) noexcept;

private:
    ImageGeometry geometry_;
    Vector cursorPhysical_{};
    WindowLevel windowLevel_{};
    bool inverted_ = false;
};

} // namespace radmarky::core
