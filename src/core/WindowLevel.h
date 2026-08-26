#pragma once

namespace radmarky::core
{

enum class WindowLevelPreset
{
    FullRange,
    CtSoftTissue,
    CtLung,
    CtBone,
    CtBrain,
};

// Radiology / DICOM / VTK convention: window is the displayed intensity width
// and level is its center. The displayed range is [level - window/2,
// level + window/2]. These values affect visualization only.
class WindowLevel
{
public:
    static constexpr double minimumWindow = 1.0e-3;

    WindowLevel() = default;

    [[nodiscard]] static WindowLevel fromIntensityRange(
        double minimum,
        double maximum);
    [[nodiscard]] static WindowLevel fromPreset(
        WindowLevelPreset preset,
        double intensityMinimum,
        double intensityMaximum);

    [[nodiscard]] double window() const noexcept;
    [[nodiscard]] double level() const noexcept;
    [[nodiscard]] double intensityMinimum() const noexcept;
    [[nodiscard]] double intensityMaximum() const noexcept;
    [[nodiscard]] double displayMinimum() const noexcept;
    [[nodiscard]] double displayMaximum() const noexcept;

    void set(double window, double level);
    void reset();
    [[nodiscard]] WindowLevel dragged(
        double normalizedDx,
        double normalizedDy) const;

private:
    WindowLevel(
        double window,
        double level,
        double intensityMinimum,
        double intensityMaximum);

    static void validate(double window, double level);

    double window_ = 1.0;
    double level_ = 0.0;
    double intensityMinimum_ = 0.0;
    double intensityMaximum_ = 1.0;
};

} // namespace radmarky::core
