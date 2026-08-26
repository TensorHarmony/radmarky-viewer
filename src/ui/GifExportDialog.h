#pragma once

#include "core/OrthogonalSliceGeometry.h"

#include <QDialog>

#include <array>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;

namespace radmarky::ui
{

enum class AnimationFormat
{
    Mp4,
    Gif,
};

struct GifSliceRange
{
    core::SliceOrientation orientation = core::SliceOrientation::Axial;
    double sliceSpacingMm = 1.0;
    int availableBefore = 0;
    int availableAfter = 0;
};

class GifExportDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit GifExportDialog(
        const std::array<GifSliceRange, 3>& ranges,
        core::SliceOrientation initialOrientation,
        QWidget* parent = nullptr);

    [[nodiscard]] AnimationFormat format() const;
    [[nodiscard]] core::SliceOrientation orientation() const;
    [[nodiscard]] double rangeMm() const;
    [[nodiscard]] int slicesBefore() const;
    [[nodiscard]] int slicesAfter() const;
    [[nodiscard]] int frameDelayMilliseconds() const;
    [[nodiscard]] bool pingPong() const;
    [[nodiscard]] bool respectZoom() const;
    [[nodiscard]] bool showCrosshair() const;

private:
    [[nodiscard]] const GifSliceRange& selectedRange() const;
    void updateRangeSummary();

    std::array<GifSliceRange, 3> ranges_;
    QComboBox* format_ = nullptr;
    QComboBox* orientation_ = nullptr;
    QDoubleSpinBox* rangeMm_ = nullptr;
    QDoubleSpinBox* playbackSpeed_ = nullptr;
    QLabel* sliceCount_ = nullptr;
    QCheckBox* pingPong_ = nullptr;
    QCheckBox* respectZoom_ = nullptr;
    QCheckBox* showCrosshair_ = nullptr;
    QPushButton* saveButton_ = nullptr;
};

} // namespace radmarky::ui
