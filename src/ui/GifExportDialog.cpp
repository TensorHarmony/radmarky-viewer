#include "ui/GifExportDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace radmarky::ui
{
namespace
{

QString orientationName(const core::SliceOrientation orientation)
{
    switch(orientation)
    {
    case core::SliceOrientation::Axial:
        return QObject::tr("Axial");
    case core::SliceOrientation::Sagittal:
        return QObject::tr("Sagittal");
    case core::SliceOrientation::Coronal:
        return QObject::tr("Coronal");
    }
    return {};
}

} // namespace

GifExportDialog::GifExportDialog(
    const std::array<GifSliceRange, 3>& ranges,
    const core::SliceOrientation initialOrientation,
    QWidget* const parent)
    : QDialog(parent)
    , ranges_(ranges)
{
    setObjectName(QStringLiteral("gifExportDialog"));
    setWindowTitle(tr("Record Slice Animation"));
    setModal(true);

    auto* const layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 18, 18, 14);
    layout->setSpacing(12);

    auto* const introduction = new QLabel(
        tr("Create an animation from the slices before and after the current "
           "position. Visible annotation overlays are included."),
        this);
    introduction->setWordWrap(true);
    layout->addWidget(introduction);

    auto* const form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(9);

    format_ = new QComboBox(this);
    format_->setObjectName(QStringLiteral("animationFormat"));
    format_->addItem(
        tr("MP4 video — high quality (recommended)"),
        static_cast<int>(AnimationFormat::Mp4));
    format_->addItem(
        tr("Animated GIF — 256 colors"),
        static_cast<int>(AnimationFormat::Gif));
    form->addRow(tr("Format:"), format_);

    orientation_ = new QComboBox(this);
    orientation_->setObjectName(QStringLiteral("gifOrientation"));
    int initialIndex = 0;
    for(std::size_t index = 0; index < ranges_.size(); ++index)
    {
        orientation_->addItem(orientationName(ranges_[index].orientation));
        if(ranges_[index].orientation == initialOrientation)
        {
            initialIndex = static_cast<int>(index);
        }
    }
    orientation_->setCurrentIndex(initialIndex);
    form->addRow(tr("Slice view:"), orientation_);

    rangeMm_ = new QDoubleSpinBox(this);
    rangeMm_->setObjectName(QStringLiteral("gifRangeMm"));
    rangeMm_->setDecimals(1);
    rangeMm_->setSingleStep(1.0);
    rangeMm_->setSuffix(tr(" mm"));
    rangeMm_->setPrefix(QString::fromUtf8("\xC2\xB1"));
    form->addRow(tr("Range:"), rangeMm_);

    playbackSpeed_ = new QDoubleSpinBox(this);
    playbackSpeed_->setObjectName(QStringLiteral("animationSpeedFps"));
    playbackSpeed_->setDecimals(1);
    playbackSpeed_->setRange(0.5, 30.0);
    playbackSpeed_->setSingleStep(0.5);
    playbackSpeed_->setValue(4.0);
    playbackSpeed_->setSuffix(tr(" slices/s"));
    playbackSpeed_->setToolTip(
        tr("Lower values make each slice remain visible longer."));
    form->addRow(tr("Playback speed:"), playbackSpeed_);

    sliceCount_ = new QLabel(this);
    sliceCount_->setObjectName(QStringLiteral("gifSliceCount"));
    form->addRow(tr("Frames:"), sliceCount_);
    layout->addLayout(form);

    pingPong_ = new QCheckBox(
        tr("Play back and forth for a smoother loop"), this);
    pingPong_->setObjectName(QStringLiteral("gifPingPong"));
    pingPong_->setChecked(true);
    layout->addWidget(pingPong_);

    respectZoom_ = new QCheckBox(tr("Respect current zoom and pan"), this);
    respectZoom_->setObjectName(QStringLiteral("gifRespectZoom"));
    respectZoom_->setChecked(true);
    layout->addWidget(respectZoom_);

    showCrosshair_ = new QCheckBox(tr("Show crosshair"), this);
    showCrosshair_->setObjectName(QStringLiteral("gifShowCrosshair"));
    showCrosshair_->setChecked(false);
    layout->addWidget(showCrosshair_);

    auto* const buttons = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    buttons->setObjectName(QStringLiteral("gifExportButtons"));
    saveButton_ = buttons->button(QDialogButtonBox::Save);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(
        format_, &QComboBox::currentIndexChanged,
        this, [this] { updateRangeSummary(); });
    connect(
        orientation_, &QComboBox::currentIndexChanged,
        this, [this] { updateRangeSummary(); });
    connect(
        rangeMm_, &QDoubleSpinBox::valueChanged,
        this, [this] { updateRangeSummary(); });
    connect(
        playbackSpeed_, &QDoubleSpinBox::valueChanged,
        this, [this] { updateRangeSummary(); });
    connect(
        pingPong_, &QCheckBox::toggled,
        this, [this] { updateRangeSummary(); });

    updateRangeSummary();
    setMinimumWidth(430);
    adjustSize();
}

AnimationFormat GifExportDialog::format() const
{
    return static_cast<AnimationFormat>(format_->currentData().toInt());
}

core::SliceOrientation GifExportDialog::orientation() const
{
    return selectedRange().orientation;
}

double GifExportDialog::rangeMm() const
{
    return rangeMm_->value();
}

int GifExportDialog::slicesBefore() const
{
    const auto& range = selectedRange();
    const int requested = static_cast<int>(std::floor(
        rangeMm() / range.sliceSpacingMm + 1.0e-9));
    return std::min(requested, range.availableBefore);
}

int GifExportDialog::slicesAfter() const
{
    const auto& range = selectedRange();
    const int requested = static_cast<int>(std::floor(
        rangeMm() / range.sliceSpacingMm + 1.0e-9));
    return std::min(requested, range.availableAfter);
}

int GifExportDialog::frameDelayMilliseconds() const
{
    return std::clamp(
        static_cast<int>(std::lround(1000.0 / playbackSpeed_->value())),
        33,
        2000);
}

bool GifExportDialog::pingPong() const
{
    return pingPong_->isChecked();
}

bool GifExportDialog::respectZoom() const
{
    return respectZoom_->isChecked();
}

bool GifExportDialog::showCrosshair() const
{
    return showCrosshair_->isChecked();
}

const GifSliceRange& GifExportDialog::selectedRange() const
{
    const int index = std::clamp(
        orientation_->currentIndex(), 0, static_cast<int>(ranges_.size()) - 1);
    return ranges_[static_cast<std::size_t>(index)];
}

void GifExportDialog::updateRangeSummary()
{
    saveButton_->setText(
        format() == AnimationFormat::Mp4 ? tr("Save MP4…") : tr("Save GIF…"));
    const auto& range = selectedRange();
    const double maximumMm = std::max(
        range.availableBefore, range.availableAfter) * range.sliceSpacingMm;
    rangeMm_->setMaximum(maximumMm);
    if(rangeMm_->value() <= 0.0 && maximumMm > 0.0)
    {
        rangeMm_->setValue(std::min(20.0, maximumMm));
        return;
    }

    const int before = slicesBefore();
    const int after = slicesAfter();
    const int forwardFrames = before + 1 + after;
    const int playbackFrames = pingPong() && forwardFrames > 2
        ? forwardFrames * 2 - 2
        : forwardFrames;
    const double durationSeconds =
        playbackFrames * frameDelayMilliseconds() / 1000.0;
    sliceCount_->setText(tr("%1 slices (%2 before, current, %3 after)%4 · %5 s")
                             .arg(forwardFrames)
                             .arg(before)
                             .arg(after)
                             .arg(
                                 playbackFrames == forwardFrames
                                     ? QString{}
                                     : tr(" · %1 playback frames")
                                           .arg(playbackFrames))
                             .arg(durationSeconds, 0, 'f', 1));
}

} // namespace radmarky::ui
