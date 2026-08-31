#include "ui/ViewerToolbox.h"

#include "app/ApplicationInfo.h"
#include "app/UserSettings.h"
#include "core/Volume.h"
#include "core/LabelPalette.h"
#include "core/WindowLevel.h"
#include "ui/UiTheme.h"

#include <QAbstractListModel>
#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QButtonGroup>
#include <QComboBox>
#include <QColor>
#include <QDoubleSpinBox>
#include <QEasingCurve>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QInputDialog>
#include <QIntValidator>
#include <QItemSelection>
#include <QItemSelectionModel>
#include <QMouseEvent>
#include <QPainter>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QPixmap>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSlider>
#include <QStackedWidget>
#include <QSpinBox>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>

namespace radmarky::ui
{
namespace
{

// The controls display two decimal places, so this is the smallest distinct
// range users can enter through the minimum/maximum representation.
constexpr double minimumEditableWindow = 0.01;
constexpr int compactRowHeight = 20;
constexpr int annotationVisibilityColumn = 0;
constexpr int annotationNameColumn = 1;
constexpr int annotationKindColumn = 2;
constexpr int annotationOpacityColumn = 3;
constexpr int annotationRemoveColumn = 4;

class LabelPaletteModel final : public QAbstractListModel
{
public:
    explicit LabelPaletteModel(QObject* parent)
        : QAbstractListModel(parent)
    {
    }

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override
    {
        return parent.isValid() ? 0 : 65536;
    }

    [[nodiscard]] QVariant data(
        const QModelIndex& index, const int role) const override
    {
        if(!index.isValid() || index.row() < 0 || index.row() >= rowCount())
        {
            return {};
        }
        const int label = index.row();
        if(role == Qt::DisplayRole)
        {
            return label == 0 ? tr("Clear (Eraser)") : tr("Label %1").arg(label);
        }
        if(role == Qt::UserRole)
        {
            return label;
        }
        if(role == Qt::DecorationRole)
        {
            if(label == 0)
            {
                return svgIcon(QStringLiteral(":/icons/erase.svg"));
            }
            const std::uint32_t packed = core::defaultLabelColor(
                static_cast<std::uint16_t>(label));
            return QColor(
                static_cast<int>((packed >> 16U) & 0xFFU),
                static_cast<int>((packed >> 8U) & 0xFFU),
                static_cast<int>(packed & 0xFFU));
        }
        return {};
    }
};

QColor labelColor(const int label)
{
    const std::uint32_t packed = core::defaultLabelColor(
        static_cast<std::uint16_t>(label));
    return {
        static_cast<int>((packed >> 16U) & 0xFFU),
        static_cast<int>((packed >> 8U) & 0xFFU),
        static_cast<int>(packed & 0xFFU)};
}

QIcon labelIcon(const int label)
{
    QPixmap swatch(12, 12);
    swatch.fill(labelColor(label));
    return QIcon(swatch);
}

void setCompactRowHeight(QWidget* const widget)
{
    widget->setFixedHeight(compactRowHeight);
}

class IntensityUnderCursorPanel final : public QFrame
{
public:
    explicit IntensityUnderCursorPanel(QWidget* parent)
        : QFrame(parent)
    {
        setObjectName(QStringLiteral("cursorIntensityTable"));
        setFrameShape(QFrame::NoFrame);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

        root_ = new QVBoxLayout(this);
        root_->setContentsMargins(0, 0, 0, 0);
        root_->setSpacing(0);

        auto* const layerHeader = new QLabel(tr("Layer"), this);
        auto* const intensityHeader = new QLabel(tr("Intensity"), this);
        root_->addWidget(makeRow(true, layerHeader, intensityHeader));

        auto* const layerValue = new QLabel(tr("Image"), this);
        intensityValue_ = new QLabel(QStringLiteral("—"), this);
        root_->addWidget(makeRow(false, layerValue, intensityValue_));

        filler_ = new QWidget(this);
        filler_->setObjectName(QStringLiteral("cursorIntensityFiller"));
        filler_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        root_->addWidget(filler_, 1);
        updateMinimumHeight();
    }

    [[nodiscard]] QLabel* intensityValue() const noexcept
    {
        return intensityValue_;
    }

    void setAnnotationLayers(
        const QStringList& names, const QStringList& values)
    {
        const int count = std::min(names.size(), values.size());
        if(annotationRows_.size() != count)
        {
            rebuildAnnotationRows(count);
        }
        for(int index = 0; index < count; ++index)
        {
            annotationRows_[index].name->setText(names[index]);
            annotationRows_[index].name->setToolTip(names[index]);
            annotationRows_[index].value->setText(values[index]);
        }
    }

    void setAnnotationName(const int index, const QString& name)
    {
        if(index < 0 || index >= annotationRows_.size() || name.isEmpty())
        {
            return;
        }
        annotationRows_[index].name->setText(name);
        annotationRows_[index].name->setToolTip(name);
    }

private:
    struct AnnotationRow
    {
        QWidget* widget = nullptr;
        QLabel* name = nullptr;
        QLabel* value = nullptr;
    };

    [[nodiscard]] QWidget* makeRow(
        const bool header, QLabel* const left, QLabel* const right)
    {
        auto* const row = new QWidget(this);
        row->setObjectName(
            header ? QStringLiteral("cursorIntensityHeader")
                   : QStringLiteral("cursorIntensityRow"));
        setCompactRowHeight(row);
        auto* const layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        for(auto* const label : {left, right})
        {
            label->setAlignment(Qt::AlignCenter);
            label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
            setCompactRowHeight(label);
        }
        if(header)
        {
            left->setObjectName(QStringLiteral("cursorIntensityHeaderLabel"));
            right->setObjectName(QStringLiteral("cursorIntensityHeaderLabel"));
        }
        auto* const divider = new QFrame(row);
        divider->setObjectName(QStringLiteral("cursorIntensityColumnDivider"));
        divider->setFixedWidth(1);
        divider->setFrameShape(QFrame::NoFrame);
        layout->addWidget(left, 1);
        layout->addWidget(divider);
        layout->addWidget(right, 1);
        return row;
    }

    void rebuildAnnotationRows(const int count)
    {
        for(auto& row : annotationRows_)
        {
            delete row.widget;
        }
        annotationRows_.clear();
        annotationRows_.reserve(count);
        const int fillerIndex = root_->indexOf(filler_);
        for(int index = 0; index < count; ++index)
        {
            auto* const name = new QLabel(this);
            auto* const value = new QLabel(this);
            auto* const row = makeRow(false, name, value);
            root_->insertWidget(fillerIndex + index, row);
            annotationRows_.push_back({row, name, value});
        }
        updateMinimumHeight();
    }

    void updateMinimumHeight()
    {
        setMinimumHeight(
            compactRowHeight * (2 + static_cast<int>(annotationRows_.size()))
            + 2);
    }

    QVBoxLayout* root_ = nullptr;
    QWidget* filler_ = nullptr;
    QLabel* intensityValue_ = nullptr;
    std::vector<AnnotationRow> annotationRows_;
};

class SamplingStatisticsPanel final : public QFrame
{
public:
    explicit SamplingStatisticsPanel(QWidget* parent)
        : QFrame(parent)
    {
        setObjectName(QStringLiteral("cursorStatisticsTable"));
        setFrameShape(QFrame::NoFrame);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        auto* const root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);
        root->addWidget(makeRow(
            true, new QLabel(tr("Statistic"), this),
            new QLabel(tr("Value"), this)));

        minimumValue_ = new QLabel(QStringLiteral("—"), this);
        root->addWidget(
            makeRow(false, new QLabel(tr("Min"), this), minimumValue_));
        maximumValue_ = new QLabel(QStringLiteral("—"), this);
        root->addWidget(
            makeRow(false, new QLabel(tr("Max"), this), maximumValue_));
        meanValue_ = new QLabel(QStringLiteral("—"), this);
        root->addWidget(makeRow(false, new QLabel(tr("Mean"), this), meanValue_));
        medianValue_ = new QLabel(QStringLiteral("—"), this);
        root->addWidget(
            makeRow(false, new QLabel(tr("Median"), this), medianValue_));
        setFixedHeight(compactRowHeight * 5 + 2);
    }

    [[nodiscard]] QLabel* maximumValue() const noexcept
    {
        return maximumValue_;
    }

    [[nodiscard]] QLabel* meanValue() const noexcept
    {
        return meanValue_;
    }

    [[nodiscard]] QLabel* medianValue() const noexcept
    {
        return medianValue_;
    }

    [[nodiscard]] QLabel* minimumValue() const noexcept
    {
        return minimumValue_;
    }

private:
    [[nodiscard]] QWidget* makeRow(
        const bool header, QLabel* const left, QLabel* const right)
    {
        auto* const row = new QWidget(this);
        row->setObjectName(
            header ? QStringLiteral("cursorIntensityHeader")
                   : QStringLiteral("cursorIntensityRow"));
        setCompactRowHeight(row);
        auto* const layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        for(auto* const label : {left, right})
        {
            label->setAlignment(Qt::AlignCenter);
            label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
            setCompactRowHeight(label);
        }
        if(header)
        {
            left->setObjectName(QStringLiteral("cursorIntensityHeaderLabel"));
            right->setObjectName(QStringLiteral("cursorIntensityHeaderLabel"));
        }
        auto* const divider = new QFrame(row);
        divider->setObjectName(QStringLiteral("cursorIntensityColumnDivider"));
        divider->setFixedWidth(1);
        divider->setFrameShape(QFrame::NoFrame);
        layout->addWidget(left, 1);
        layout->addWidget(divider);
        layout->addWidget(right, 1);
        return row;
    }

    QLabel* maximumValue_ = nullptr;
    QLabel* meanValue_ = nullptr;
    QLabel* medianValue_ = nullptr;
    QLabel* minimumValue_ = nullptr;
};

// QDoubleSpinBox sizes itself from the numeric range. A ±1e12 range makes the
// field wider than the sidebar; keep a compact hint and let the layout stretch.
class CompactDoubleSpinBox final : public QDoubleSpinBox
{
public:
    explicit CompactDoubleSpinBox(QWidget* parent = nullptr)
        : QDoubleSpinBox(parent)
    {
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    }

    [[nodiscard]] QSize sizeHint() const override
    {
        return compactSize();
    }

    [[nodiscard]] QSize minimumSizeHint() const override
    {
        return compactSize();
    }

private:
    [[nodiscard]] QSize compactSize() const
    {
        const int width =
            fontMetrics().horizontalAdvance(QStringLiteral("-0000.00")) + 12;
        return {width, compactRowHeight};
    }
};

class AnnotationTable final : public QTableWidget
{
public:
    explicit AnnotationTable(QWidget* parent)
        : QTableWidget(0, 5, parent)
    {
    }

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        const auto index = indexAt(event->position().toPoint());
        const bool modifiedClick = event->button() == Qt::LeftButton
            && index.isValid()
            && event->modifiers().testAnyFlags(
                Qt::ShiftModifier | Qt::ControlModifier);
        if(!modifiedClick)
        {
            QTableWidget::mousePressEvent(event);
            if(event->button() == Qt::LeftButton && index.isValid())
            {
                anchorRow_ = index.row();
            }
            return;
        }

        const int clickedRow = index.row();
        const auto selected = selectionModel()->selectedRows();
        const bool clickedIsSelected = std::any_of(
            selected.begin(), selected.end(), [clickedRow](const QModelIndex& row) {
                return row.row() == clickedRow;
            });
        if(event->modifiers().testFlag(Qt::ShiftModifier))
        {
            int anchor = anchorRow_;
            if(anchor < 0 || anchor >= rowCount())
            {
                anchor = selected.empty() ? clickedRow : selected.front().row();
            }
            selectionModel()->clearSelection();
            selectWholeRow(anchor, QItemSelectionModel::Select);
            if(clickedRow != anchor)
            {
                selectWholeRow(clickedRow, QItemSelectionModel::Select);
            }
        }
        else if(clickedIsSelected)
        {
            selectWholeRow(clickedRow, QItemSelectionModel::Deselect);
        }
        else if(selected.size() < 2)
        {
            selectWholeRow(clickedRow, QItemSelectionModel::Select);
        }
        else
        {
            const int retained = std::any_of(
                                     selected.begin(),
                                     selected.end(),
                                     [this](const QModelIndex& row) {
                                         return row.row() == anchorRow_;
                                     })
                ? anchorRow_
                : selected.front().row();
            selectionModel()->clearSelection();
            selectWholeRow(retained, QItemSelectionModel::Select);
            selectWholeRow(clickedRow, QItemSelectionModel::Select);
        }
        setCurrentCell(clickedRow, 0, QItemSelectionModel::NoUpdate);
        event->accept();
    }

private:
    void selectWholeRow(
        const int row,
        const QItemSelectionModel::SelectionFlag command)
    {
        const QItemSelection selection(
            model()->index(row, 0), model()->index(row, columnCount() - 1));
        selectionModel()->select(
            selection, command | QItemSelectionModel::Rows);
    }

    int anchorRow_ = -1;
};

class OpacityBattery final : public QWidget
{
public:
    using ChangeCallback = std::function<void(double)>;

    explicit OpacityBattery(const double opacity, QWidget* parent = nullptr)
        : QWidget(parent)
        , opacity_(std::clamp(opacity, 0.0, 1.0))
    {
        setObjectName(QStringLiteral("annotationOpacityBattery"));
        setCursor(Qt::PointingHandCursor);
        updateToolTip();
    }

    void setChangeCallback(ChangeCallback onChanged)
    {
        onChanged_ = std::move(onChanged);
    }

    void setOpacity(const double opacity)
    {
        opacity_ = std::clamp(opacity, 0.0, 1.0);
        updateToolTip();
        update();
    }

    [[nodiscard]] QSize sizeHint() const override
    {
        return {72, 21};
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRect track = rect().adjusted(2, 3, -2, -3);
        const QColor empty = palette().color(QPalette::AlternateBase);
        const QColor fill(QStringLiteral("#4c91b3"));
        painter.setPen(Qt::NoPen);
        painter.setBrush(empty);
        painter.drawRoundedRect(track, 2.0, 2.0);

        QRect fillRect = track;
        fillRect.setWidth(static_cast<int>(std::lround(
            static_cast<double>(fillRect.width()) * opacity_)));
        if(fillRect.width() > 0)
        {
            painter.setBrush(fill);
            painter.drawRoundedRect(fillRect, 1.5, 1.5);
        }
        painter.setPen(palette().color(QPalette::Text));
        painter.drawText(
            track,
            Qt::AlignCenter,
            QString::number(static_cast<int>(std::lround(opacity_ * 100.0)))
                + '%');
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if(event->button() == Qt::LeftButton)
        {
            setOpacityAt(event->position().x());
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if(event->buttons().testFlag(Qt::LeftButton))
        {
            setOpacityAt(event->position().x());
            event->accept();
            return;
        }
        QWidget::mouseMoveEvent(event);
    }

private:
    void setOpacityAt(const double x)
    {
        const QRect track = rect().adjusted(2, 3, -2, -3);
        const double next = track.width() > 0
            ? std::clamp(
                  (x - static_cast<double>(track.left()))
                      / static_cast<double>(track.width()),
                  0.0,
                  1.0)
            : 0.0;
        if(std::abs(next - opacity_) < 1.0e-6)
        {
            return;
        }
        opacity_ = next;
        updateToolTip();
        update();
        if(onChanged_)
        {
            onChanged_(opacity_);
        }
    }

    void updateToolTip()
    {
        setToolTip(
            tr("Overlay opacity: %1% (click left for transparent, right for opaque)")
                .arg(static_cast<int>(std::lround(opacity_ * 100.0))));
    }

    double opacity_ = 0.5;
    ChangeCallback onChanged_;
};

QString fromUtf8(const std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

QLineEdit* makeReadOnlyField(QWidget* const parent)
{
    auto* const field = new QLineEdit(parent);
    field->setReadOnly(true);
    field->setAlignment(Qt::AlignCenter);
    field->setText(QStringLiteral("—"));
    field->setFixedHeight(compactRowHeight + 2);
    return field;
}

bool nearlyEqual(const double left, const double right)
{
    return std::abs(left - right)
        <= std::max(1.0e-3, 1.0e-4 * std::max(std::abs(left), std::abs(right)));
}

int matchingPresetIndex(
    const double window,
    const double level,
    const double intensityMinimum,
    const double intensityMaximum)
{
    using radmarky::core::WindowLevel;
    using radmarky::core::WindowLevelPreset;
    const auto fullRange =
        WindowLevel::fromIntensityRange(intensityMinimum, intensityMaximum);
    if(nearlyEqual(window, fullRange.window())
       && nearlyEqual(level, fullRange.level()))
    {
        return 1;
    }

    constexpr WindowLevelPreset presets[]{
        WindowLevelPreset::CtSoftTissue,
        WindowLevelPreset::CtLung,
        WindowLevelPreset::CtBone,
        WindowLevelPreset::CtBrain,
    };
    for(int index = 0; index < 4; ++index)
    {
        const auto preset = WindowLevel::fromPreset(
            presets[index], intensityMinimum, intensityMaximum);
        if(nearlyEqual(window, preset.window())
           && nearlyEqual(level, preset.level()))
        {
            return index + 2;
        }
    }
    return 0;
}

} // namespace

ViewerToolbox::ViewerToolbox(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("viewerToolbox");
    setMinimumWidth(210);

    auto* const root = new QVBoxLayout(this);
    root->setContentsMargins(10, 8, 3, 12);
    root->setSpacing(0);

    contentStack_ = new QStackedWidget(this);
    contentStack_->setObjectName(QStringLiteral("toolboxContentStack"));

    auto* const identityPage = new QWidget(contentStack_);
    identityPage_ = identityPage;
    identityPage->setObjectName(QStringLiteral("toolboxIdentityPage"));
    auto* const identityLayout = new QVBoxLayout(identityPage);
    identityLayout->setContentsMargins(10, 24, 10, 12);
    identityLayout->setSpacing(5);
    auto* const productIcon = new QLabel(identityPage);
    productIcon->setObjectName(QStringLiteral("toolboxProductIcon"));
    productIcon->setAccessibleName(tr("RadMarky application logo"));
    productIcon->setAlignment(Qt::AlignCenter);
    productIcon->setPixmap(svgPixmap(
        QStringLiteral(":/icons/app-icon.svg"), QSize(104, 104)));
    auto* const productName =
        new QLabel(fromUtf8(app::applicationName()), identityPage);
    productName->setObjectName(QStringLiteral("toolboxProductName"));
    productName->setAlignment(Qt::AlignCenter);
    auto* const version = new QLabel(
        tr("Version %1").arg(fromUtf8(app::applicationVersion())), identityPage);
    version->setObjectName(QStringLiteral("toolboxVersion"));
    version->setAlignment(Qt::AlignCenter);
    auto* const releaseDate =
        new QLabel(fromUtf8(app::applicationReleaseDate()), identityPage);
    releaseDate->setObjectName(QStringLiteral("toolboxReleaseDate"));
    releaseDate->setAlignment(Qt::AlignCenter);
    auto* const releaseMeta = new QWidget(identityPage);
    releaseMeta->setObjectName(QStringLiteral("toolboxReleaseMeta"));
    auto* const releaseMetaLayout = new QVBoxLayout(releaseMeta);
    releaseMetaLayout->setContentsMargins(0, 0, 0, 0);
    releaseMetaLayout->setSpacing(0);
    releaseMetaLayout->addWidget(version);
    releaseMetaLayout->addWidget(releaseDate);
    auto* const copyright = new QLabel(
        QStringLiteral("Copyright © 2026\n%1")
            .arg(fromUtf8(app::copyrightHolder())),
        identityPage);
    copyright->setObjectName(QStringLiteral("toolboxCopyright"));
    copyright->setAlignment(Qt::AlignCenter);
    copyright->setWordWrap(true);
    identityLayout->addStretch(2);
    identityLayout->addWidget(productIcon, 0, Qt::AlignHCenter);
    identityLayout->addSpacing(8);
    identityLayout->addWidget(productName);
    identityLayout->addWidget(releaseMeta);
    identityLayout->addStretch(5);
    identityLayout->addWidget(copyright);

    controlsScroll_ = new QScrollArea(contentStack_);
    controlsScroll_->setObjectName(QStringLiteral("toolboxControlsScrollArea"));
    controlsScroll_->setWidgetResizable(true);
    controlsScroll_->setFrameShape(QFrame::NoFrame);
    controlsScroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    controlsPage_ = new QWidget(controlsScroll_);
    controlsPage_->setObjectName(QStringLiteral("toolboxControlsPage"));
    auto* const controlsLayout = new QVBoxLayout(controlsPage_);
    controlsLayout->setContentsMargins(0, 0, 0, 0);
    controlsLayout->setSpacing(12);
    controlsScroll_->setWidget(controlsPage_);
    contentStack_->addWidget(identityPage);
    contentStack_->addWidget(controlsScroll_);
    contentStack_->setCurrentWidget(identityPage);
    root->addWidget(contentStack_);

    auto* const contrastGroup = new QGroupBox(tr("Contrast"), controlsPage_);
    contrastGroup->setObjectName(QStringLiteral("contrastGroup"));
    contrastGroup_ = contrastGroup;
    auto* const contrastLayout = new QVBoxLayout(contrastGroup);
    contrastLayout->setContentsMargins(8, 8, 8, 8);
    contrastLayout->setSpacing(4);

    windowLevelPreset_ = new QComboBox(contrastGroup);
    windowLevelPreset_->setObjectName(QStringLiteral("windowLevelPreset"));
    windowLevelPreset_->addItem(tr("Custom"));
    windowLevelPreset_->addItem(tr("Full range"));
    windowLevelPreset_->addItem(tr("CT Soft Tissue"));
    windowLevelPreset_->addItem(tr("CT Lung"));
    windowLevelPreset_->addItem(tr("CT Bone"));
    windowLevelPreset_->addItem(tr("CT Brain"));
    windowLevelPreset_->setCurrentIndex(1);
    setCompactRowHeight(windowLevelPreset_);
    contrastLayout->addWidget(windowLevelPreset_);

    const auto configureIntensitySpin = [](QDoubleSpinBox* const spin) {
        spin->setRange(-1.0e12, 1.0e12);
        spin->setDecimals(2);
        spin->setSingleStep(1.0);
        spin->setAccelerated(true);
        spin->setKeyboardTracking(false);
        spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
        spin->setAlignment(Qt::AlignRight);
        spin->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        setCompactRowHeight(spin);
    };
    minimumSpin_ = new CompactDoubleSpinBox(contrastGroup);
    minimumSpin_->setObjectName(QStringLiteral("minimumSpin"));
    minimumSpin_->setToolTip(tr("Minimum displayed intensity"));
    configureIntensitySpin(minimumSpin_);
    minimumSpin_->setValue(-0.5);
    maximumSpin_ = new CompactDoubleSpinBox(contrastGroup);
    maximumSpin_->setObjectName(QStringLiteral("maximumSpin"));
    maximumSpin_->setToolTip(tr("Maximum displayed intensity"));
    configureIntensitySpin(maximumSpin_);
    maximumSpin_->setValue(0.5);
    windowSpin_ = new CompactDoubleSpinBox(contrastGroup);
    windowSpin_->setObjectName(QStringLiteral("windowSpin"));
    configureIntensitySpin(windowSpin_);
    windowSpin_->setRange(core::WindowLevel::minimumWindow, 2.0e12);
    windowSpin_->setValue(1.0);
    levelSpin_ = new CompactDoubleSpinBox(contrastGroup);
    levelSpin_->setObjectName(QStringLiteral("levelSpin"));
    configureIntensitySpin(levelSpin_);

    auto* const values = new QGridLayout();
    values->setContentsMargins(0, 0, 0, 0);
    values->setHorizontalSpacing(6);
    values->setVerticalSpacing(4);
    const auto makeCompactLabel = [](const QString& text, QWidget* const parent) {
        auto* const label = new QLabel(text, parent);
        setCompactRowHeight(label);
        return label;
    };
    values->addWidget(makeCompactLabel(tr("Min"), contrastGroup), 0, 0);
    values->addWidget(minimumSpin_, 0, 1);
    values->addWidget(makeCompactLabel(tr("Max"), contrastGroup), 0, 2);
    values->addWidget(maximumSpin_, 0, 3);
    values->addWidget(makeCompactLabel(tr("Level"), contrastGroup), 1, 0);
    values->addWidget(levelSpin_, 1, 1);
    values->addWidget(makeCompactLabel(tr("Window"), contrastGroup), 1, 2);
    values->addWidget(windowSpin_, 1, 3);
    values->setColumnStretch(1, 1);
    values->setColumnStretch(3, 1);
    contrastLayout->addLayout(values);

    auto* const automaticContrast = new QPushButton(tr("Auto"), contrastGroup);
    automaticContrast->setObjectName(QStringLiteral("automaticContrastButton"));
    automaticContrast->setToolTip(tr(
        "Fit window/level to finite voxel values, excluding the lowest and "
        "highest 0.5% as outliers"));
    auto* const defaultContrast =
        new QPushButton(tr("Make Default"), contrastGroup);
    defaultContrast->setObjectName(QStringLiteral("defaultContrastButton"));
    defaultContrast->setToolTip(
        tr("Use the current window/level for subsequently opened images"));
    auto* const savePreset = new QPushButton(tr("Save"), contrastGroup);
    savePreset->setObjectName(QStringLiteral("saveContrastPresetButton"));
    savePreset->setToolTip(tr("Save the current window/level as a named preset"));
    auto* const actionRow = new QHBoxLayout();
    actionRow->setContentsMargins(0, 0, 0, 0);
    actionRow->setSpacing(4);
    for(auto* const button : {automaticContrast, defaultContrast, savePreset})
    {
        button->setAutoDefault(false);
        button->setDefault(false);
        button->setMinimumWidth(0);
        button->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
        setCompactRowHeight(button);
        actionRow->addWidget(button);
    }
    contrastLayout->addLayout(actionRow);
    contrastGroup->setEnabled(false);
    controlsLayout->addWidget(contrastGroup);

    connect(
        windowLevelPreset_,
        &QComboBox::currentIndexChanged,
        this,
        [this](const int index) {
            if(index <= 0)
            {
                return;
            }
            if(index <= 5)
            {
                emit windowLevelPresetSelected(index - 1);
                return;
            }
            const auto namedIndex = static_cast<std::size_t>(index - 6);
            if(namedIndex < namedPresets_.size())
            {
                const auto& preset = namedPresets_[namedIndex];
                emit windowLevelEdited(preset.window, preset.level);
            }
        });
    connect(
        minimumSpin_,
        &QDoubleSpinBox::valueChanged,
        this,
        [this](const double minimum) {
            double maximum = maximumSpin_->value();
            if(maximum - minimum < minimumEditableWindow)
            {
                maximum = minimum + minimumEditableWindow;
                const QSignalBlocker blocker(maximumSpin_);
                maximumSpin_->setValue(maximum);
            }
            emit windowLevelEdited(
                maximum - minimum, (minimum + maximum) / 2.0);
        });
    connect(
        maximumSpin_,
        &QDoubleSpinBox::valueChanged,
        this,
        [this](const double maximum) {
            double minimum = minimumSpin_->value();
            if(maximum - minimum < minimumEditableWindow)
            {
                minimum = maximum - minimumEditableWindow;
                const QSignalBlocker blocker(minimumSpin_);
                minimumSpin_->setValue(minimum);
            }
            emit windowLevelEdited(
                maximum - minimum, (minimum + maximum) / 2.0);
        });
    connect(
        windowSpin_,
        &QDoubleSpinBox::valueChanged,
        this,
        [this](const double) {
            emit windowLevelEdited(windowSpin_->value(), levelSpin_->value());
        });
    connect(
        levelSpin_,
        &QDoubleSpinBox::valueChanged,
        this,
        [this](const double) {
            emit windowLevelEdited(windowSpin_->value(), levelSpin_->value());
        });
    connect(
        automaticContrast,
        &QPushButton::clicked,
        this,
        &ViewerToolbox::automaticWindowLevelRequested);
    connect(defaultContrast, &QPushButton::clicked, this, [this] {
        emit windowLevelDefaultRequested(windowSpin_->value(), levelSpin_->value());
    });
    connect(savePreset, &QPushButton::clicked, this, [this] {
        bool accepted = false;
        const QString name = QInputDialog::getText(
                                 this,
                                 tr("Save Window/Level Preset"),
                                 tr("Preset name:"),
                                 QLineEdit::Normal,
                                 {},
                                 &accepted)
                                 .trimmed();
        if(accepted && !name.isEmpty())
        {
            emit windowLevelPresetSaveRequested(
                name, windowSpin_->value(), levelSpin_->value());
        }
    });

    auto* const annotationsGroup =
        new QGroupBox(tr("Annotations"), controlsPage_);
    annotationsGroup->setObjectName(QStringLiteral("annotationsGroup"));
    annotationsGroup_ = annotationsGroup;
    auto* const annotationsLayout = new QVBoxLayout(annotationsGroup);
    annotationsLayout->setContentsMargins(8, 16, 8, 8);
    annotationStack_ = new QStackedWidget(annotationsGroup);
    annotationStack_->setObjectName(QStringLiteral("annotationStack"));
    annotationStack_->setMinimumHeight(136);

    annotationEmptyState_ = new QWidget(annotationStack_);
    annotationEmptyState_->setObjectName(QStringLiteral("annotationEmptyState"));
    auto* const emptyLayout = new QVBoxLayout(annotationEmptyState_);
    emptyLayout->setContentsMargins(12, 10, 12, 10);
    emptyLayout->setSpacing(8);
    emptyLayout->addStretch();
    auto* const emptyLabel = new QLabel(
        tr("No annotations yet"), annotationEmptyState_);
    emptyLabel->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(emptyLabel);
    auto* const newAnnotationButton = new QPushButton(
        tr("New Annotation"), annotationEmptyState_);
    newAnnotationButton->setObjectName(
        QStringLiteral("newAnnotationEmptyStateButton"));
    newAnnotationButton->setAccessibleName(
        tr("Create a new annotation"));
    newAnnotationButton->setIcon(
        svgIcon(QStringLiteral(":/icons/new-annotation.svg")));
    emptyLayout->addWidget(newAnnotationButton, 0, Qt::AlignHCenter);
    auto* const loadAnnotationButton = new QPushButton(
        tr("Load Annotation"), annotationEmptyState_);
    loadAnnotationButton->setObjectName(
        QStringLiteral("loadAnnotationEmptyStateButton"));
    loadAnnotationButton->setAccessibleName(
        tr("Load an annotation"));
    loadAnnotationButton->setIcon(
        svgIcon(QStringLiteral(":/icons/open.svg")));
    emptyLayout->addWidget(loadAnnotationButton, 0, Qt::AlignHCenter);
    emptyLayout->addStretch();
    connect(
        newAnnotationButton, &QPushButton::clicked,
        this, &ViewerToolbox::annotationCreationRequested);
    connect(
        loadAnnotationButton, &QPushButton::clicked,
        this, &ViewerToolbox::annotationLoadRequested);

    annotationTable_ = new AnnotationTable(annotationStack_);
    annotationTable_->setObjectName(QStringLiteral("annotationTable"));
    annotationTable_->setHorizontalHeaderLabels(
        {QString{}, tr("Name"), tr("Kind"), tr("Opacity"), QString{}});
    annotationTable_->verticalHeader()->hide();
    annotationTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    annotationTable_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    annotationTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    annotationTable_->setFocusPolicy(Qt::NoFocus);
    annotationTable_->setToolTip(
        tr("Select two rows to compare (top row = first, lower row = second): "
           "red = first only, green = same value, blue = second only, "
           "yellow = different values."));
    annotationTable_->horizontalHeader()->setFixedHeight(
        annotationTable_->horizontalHeader()->fontMetrics().height() + 4);
    annotationTable_->horizontalHeader()->setSectionResizeMode(
        annotationVisibilityColumn, QHeaderView::ResizeToContents);
    annotationTable_->horizontalHeader()->setSectionResizeMode(
        annotationNameColumn, QHeaderView::Stretch);
    annotationTable_->horizontalHeader()->setSectionResizeMode(
        annotationKindColumn, QHeaderView::ResizeToContents);
    annotationTable_->horizontalHeader()->setSectionResizeMode(
        annotationOpacityColumn, QHeaderView::ResizeToContents);
    annotationTable_->horizontalHeader()->setSectionResizeMode(
        annotationRemoveColumn, QHeaderView::ResizeToContents);
    annotationTable_->setColumnWidth(annotationVisibilityColumn, 28);
    annotationTable_->setColumnWidth(annotationRemoveColumn, 28);
    connect(
        annotationTable_,
        &QTableWidget::itemSelectionChanged,
        this,
        [this] {
            emit annotationSelectionChanged(selectedAnnotationIndices());
        });
    annotationStack_->addWidget(annotationEmptyState_);
    annotationStack_->addWidget(annotationTable_);
    updateAnnotationEmptyState();
    annotationsLayout->addWidget(annotationStack_);
    annotationsGroup->setEnabled(false);
    controlsLayout->addWidget(annotationsGroup);

    auto* const labelsGroup =
        new QGroupBox(tr("Annotation Labels"), controlsPage_);
    labelsGroup->setObjectName(QStringLiteral("annotationLabelsGroup"));
    annotationLabelsGroup_ = labelsGroup;
    auto* const labelsLayout = new QVBoxLayout(labelsGroup);
    labelsLayout->setContentsMargins(8, 8, 8, 8);
    labelsLayout->setSpacing(6);

    auto* const radiusRow = new QWidget(labelsGroup);
    auto* const radiusLayout = new QHBoxLayout(radiusRow);
    radiusLayout->setContentsMargins(0, 0, 0, 0);
    radiusLayout->setSpacing(6);
    radiusLayout->addWidget(new QLabel(tr("Brush size:"), radiusRow));
    brushRadiusSpin_ = new QSpinBox(radiusRow);
    brushRadiusSpin_->setObjectName(QStringLiteral("brushRadiusSpin"));
    brushRadiusSpin_->setAccessibleName(tr("Brush size in voxels"));
    brushRadiusSpin_->setRange(1, 100);
    brushRadiusSpin_->setValue(1);
    brushRadiusSpin_->setSuffix(tr(" vox"));
    brushRadiusSpin_->setKeyboardTracking(false);
    brushRadiusSpin_->setToolTip(
        tr("Brush size in voxels. Press [ to shrink and ] to grow"));
    brushRadiusSlider_ = new QSlider(Qt::Horizontal, radiusRow);
    brushRadiusSlider_->setObjectName(QStringLiteral("brushRadiusSlider"));
    brushRadiusSlider_->setAccessibleName(tr("Brush size in voxels"));
    brushRadiusSlider_->setRange(1, 100);
    brushRadiusSlider_->setValue(1);
    brushRadiusSlider_->setToolTip(
        tr("Brush size in voxels. Press [ to shrink and ] to grow"));
    radiusLayout->addWidget(brushRadiusSpin_);
    radiusLayout->addWidget(brushRadiusSlider_, 1);
    labelsLayout->addWidget(radiusRow);

    auto* const shapeRow = new QWidget(labelsGroup);
    auto* const shapeLayout = new QHBoxLayout(shapeRow);
    shapeLayout->setContentsMargins(0, 0, 0, 0);
    shapeLayout->setSpacing(6);
    shapeLayout->addWidget(new QLabel(tr("Brush shape:"), shapeRow));
    squareBrushButton_ = new QToolButton(shapeRow);
    squareBrushButton_->setObjectName(QStringLiteral("squareBrushButton"));
    squareBrushButton_->setCheckable(true);
    squareBrushButton_->setChecked(true);
    squareBrushButton_->setIcon(svgIcon(QStringLiteral(":/icons/square.svg")));
    squareBrushButton_->setIconSize(QSize(16, 16));
    squareBrushButton_->setToolTip(tr("Square brush"));
    squareBrushButton_->setAccessibleName(tr("Square brush"));
    circleBrushButton_ = new QToolButton(shapeRow);
    circleBrushButton_->setObjectName(QStringLiteral("circleBrushButton"));
    circleBrushButton_->setCheckable(true);
    circleBrushButton_->setIcon(svgIcon(QStringLiteral(":/icons/circle.svg")));
    circleBrushButton_->setIconSize(QSize(16, 16));
    circleBrushButton_->setToolTip(tr("Circle brush"));
    circleBrushButton_->setAccessibleName(tr("Circle brush"));
    auto* const shapeGroup = new QButtonGroup(labelsGroup);
    shapeGroup->setExclusive(true);
    shapeGroup->addButton(squareBrushButton_);
    shapeGroup->addButton(circleBrushButton_);
    shapeLayout->addWidget(squareBrushButton_);
    shapeLayout->addWidget(circleBrushButton_);
    shapeLayout->addStretch();
    labelsLayout->addWidget(shapeRow);

    labelsLayout->addWidget(new QLabel(tr("Active label:"), labelsGroup));
    auto* const activeLabelRow = new QWidget(labelsGroup);
    auto* const activeLabelLayout = new QHBoxLayout(activeLabelRow);
    activeLabelLayout->setContentsMargins(0, 0, 0, 0);
    activeLabelLayout->setSpacing(6);
    activeLabelColorSwatch_ = new QFrame(activeLabelRow);
    activeLabelColorSwatch_->setObjectName(
        QStringLiteral("activeLabelColorSwatch"));
    activeLabelColorSwatch_->setFrameShape(QFrame::StyledPanel);
    activeLabelColorSwatch_->setFixedSize(16, 16);
    activeLabelCombo_ = new QComboBox(activeLabelRow);
    activeLabelCombo_->setObjectName(QStringLiteral("activeLabelCombo"));
    activeLabelCombo_->setAccessibleName(tr("Active annotation label"));
    activeLabelCombo_->setToolTip(
        tr("Press 1–9 to paint with that label, or 0 to erase"));
    activeLabelCombo_->setModel(new LabelPaletteModel(activeLabelCombo_));
    activeLabelCombo_->setCurrentIndex(1);
    activeLabelCombo_->setMaxVisibleItems(16);
    activeLabelLayout->addWidget(activeLabelColorSwatch_);
    activeLabelLayout->addWidget(activeLabelCombo_, 1);
    labelsLayout->addWidget(activeLabelRow);

    labelsLayout->addWidget(new QLabel(tr("Paint over:"), labelsGroup));
    paintOverCombo_ = new QComboBox(labelsGroup);
    paintOverCombo_->setObjectName(QStringLiteral("paintOverCombo"));
    paintOverCombo_->setAccessibleName(tr("Paint-over mask"));
    paintOverCombo_->addItem(tr("All labels"), -1);
    paintOverCombo_->addItem(tr("Clear Label"), 0);
    paintOverCombo_->setMaxVisibleItems(16);
    paintOverCombo_->setToolTip(
        tr("Choose which existing labels brush and eraser strokes may replace"));
    labelsLayout->addWidget(paintOverCombo_);

    labelsLayout->addWidget(new QLabel(tr("Overall label opacity:"), labelsGroup));
    auto* const opacityRow = new QWidget(labelsGroup);
    auto* const opacityLayout = new QHBoxLayout(opacityRow);
    opacityLayout->setContentsMargins(0, 0, 0, 0);
    opacityLayout->setSpacing(6);
    labelOpacitySpin_ = new QSpinBox(opacityRow);
    labelOpacitySpin_->setObjectName(QStringLiteral("labelOpacitySpin"));
    labelOpacitySpin_->setRange(0, 100);
    labelOpacitySpin_->setSuffix(QStringLiteral("%"));
    labelOpacitySpin_->setValue(100);
    labelOpacitySpin_->setKeyboardTracking(false);
    labelOpacitySlider_ = new QSlider(Qt::Horizontal, opacityRow);
    labelOpacitySlider_->setObjectName(QStringLiteral("labelOpacitySlider"));
    labelOpacitySlider_->setRange(0, 100);
    labelOpacitySlider_->setValue(100);
    opacityLayout->addWidget(labelOpacitySpin_);
    opacityLayout->addWidget(labelOpacitySlider_, 1);
    labelsLayout->addWidget(opacityRow);
    labelsGroup->setEnabled(false);
    controlsLayout->addWidget(labelsGroup);

    const auto updateActiveLabel = [this](const int index) {
        if(index < 0)
        {
            return;
        }
        const int label = activeLabelCombo_->itemData(index).toInt();
        if(label == 0)
        {
            activeLabelColorSwatch_->setStyleSheet(
                QStringLiteral("background-color: transparent;"));
        }
        else
        {
            const QColor color = labelColor(label);
            activeLabelColorSwatch_->setStyleSheet(
                QStringLiteral("background-color: %1;").arg(color.name()));
        }
        const auto radius = brushRadii_.find(label);
        const int selectedRadius = radius != brushRadii_.end() ? radius->second : 1;
        const QSignalBlocker spinBlocker(brushRadiusSpin_);
        const QSignalBlocker sliderBlocker(brushRadiusSlider_);
        brushRadiusSpin_->setValue(selectedRadius);
        brushRadiusSlider_->setValue(selectedRadius);
        emit brushRadiusChanged(selectedRadius);
        applyPaintOverForActiveLabel();
        emit activeLabelChanged(label);
    };
    connect(
        activeLabelCombo_, &QComboBox::currentIndexChanged,
        this, updateActiveLabel);
    updateActiveLabel(activeLabelCombo_->currentIndex());
    connect(
        paintOverCombo_, &QComboBox::currentIndexChanged,
        this,
        [this](const int index) {
            if(index >= 0)
            {
                const int selection = paintOverCombo_->itemData(index).toInt();
                const int label = activeLabel();
                paintOverSelections_[label] = selection;
                emit paintOverChanged(selection);
                emit paintOverPreferenceChanged(label, selection);
            }
        });
    const auto updateBrushRadius = [this](const int radius) {
        const QSignalBlocker spinBlocker(brushRadiusSpin_);
        const QSignalBlocker sliderBlocker(brushRadiusSlider_);
        brushRadiusSpin_->setValue(radius);
        brushRadiusSlider_->setValue(radius);
        const int label = activeLabel();
        brushRadii_[label] = radius;
        emit brushRadiusChanged(radius);
        emit brushRadiusPreferenceChanged(label, radius);
    };
    connect(
        brushRadiusSpin_, &QSpinBox::valueChanged,
        this, updateBrushRadius);
    connect(
        brushRadiusSlider_, &QSlider::valueChanged,
        this, updateBrushRadius);
    connect(
        squareBrushButton_, &QToolButton::toggled,
        this,
        [this](const bool checked) {
            if(checked)
            {
                emit brushShapeChanged(core::BrushShape::Square);
            }
        });
    connect(
        circleBrushButton_, &QToolButton::toggled,
        this,
        [this](const bool checked) {
            if(checked)
            {
                emit brushShapeChanged(core::BrushShape::Circle);
            }
        });
    const auto updateOpacity = [this](const int percent) {
        const QSignalBlocker spinBlocker(labelOpacitySpin_);
        const QSignalBlocker sliderBlocker(labelOpacitySlider_);
        labelOpacitySpin_->setValue(percent);
        labelOpacitySlider_->setValue(percent);
        emit overallLabelOpacityChanged(
            static_cast<double>(percent) / 100.0);
    };
    connect(labelOpacitySpin_, &QSpinBox::valueChanged, this, updateOpacity);
    connect(labelOpacitySlider_, &QSlider::valueChanged, this, updateOpacity);

    auto* const inspectorGroup =
        new QGroupBox(tr("Cursor Inspector"), controlsPage_);
    inspectorGroup->setObjectName(QStringLiteral("cursorInspectorGroup"));
    cursorInspectorGroup_ = inspectorGroup;
    inspectorGroup->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* const inspectorLayout = new QVBoxLayout(inspectorGroup);
    inspectorLayout->setContentsMargins(8, 8, 8, 10);
    inspectorLayout->setSpacing(6);

    inspectorLayout->addWidget(
        makeCompactLabel(tr("Cursor position (x,y,slice):"), inspectorGroup));
    auto* const cursorRow = new QWidget(inspectorGroup);
    auto* const cursorLayout = new QHBoxLayout(cursorRow);
    cursorLayout->setContentsMargins(0, 0, 0, 0);
    cursorLayout->setSpacing(6);
    cursorX_ = makeReadOnlyField(cursorRow);
    cursorY_ = makeReadOnlyField(cursorRow);
    cursorZ_ = new QLineEdit(cursorRow);
    cursorZ_->setObjectName(QStringLiteral("cursorAxialSliceField"));
    cursorZ_->setAccessibleName(tr("Axial slice number"));
    cursorZ_->setAlignment(Qt::AlignCenter);
    cursorSliceValidator_ = new QIntValidator(1, 1, cursorZ_);
    cursorZ_->setValidator(cursorSliceValidator_);
    setCompactRowHeight(cursorZ_);
    connect(cursorZ_, &QLineEdit::editingFinished, this, [this] {
        bool validNumber = false;
        const int sliceNumber = cursorZ_->text().toInt(&validNumber);
        QString candidate = cursorZ_->text();
        int cursorPosition = candidate.size();
        if(validNumber
           && cursorSliceValidator_->validate(candidate, cursorPosition)
               == QValidator::Acceptable)
        {
            cursorZ_->setModified(false);
            emit axialSliceEdited(sliceNumber);
        }
    });
    for(auto* const field : {cursorX_, cursorY_, cursorZ_})
    {
        cursorLayout->addWidget(field);
    }
    inspectorLayout->addWidget(cursorRow);

    inspectorLayout->addWidget(
        makeCompactLabel(tr("Intensity under cursor:"), inspectorGroup));
    auto* const intensityPanel = new IntensityUnderCursorPanel(inspectorGroup);
    cursorIntensityTable_ = intensityPanel;
    intensityValue_ = intensityPanel->intensityValue();
    inspectorLayout->addWidget(intensityPanel, 1);

    inspectorLayout->addWidget(
        makeCompactLabel(tr("Sample area size:"), inspectorGroup));
    auto* const samplingRow = new QWidget(inspectorGroup);
    auto* const samplingLayout = new QHBoxLayout(samplingRow);
    samplingLayout->setContentsMargins(0, 0, 0, 0);
    samplingLayout->setSpacing(6);
    auto* const samplingSlider = new QSlider(Qt::Horizontal, samplingRow);
    samplingSlider->setObjectName(QStringLiteral("cursorSamplingRadiusSlider"));
    samplingSlider->setAccessibleName(tr("Cursor sample area size"));
    samplingSlider->setRange(1, 5);
    samplingSlider->setValue(1);
    samplingSlider->setSingleStep(1);
    samplingSlider->setPageStep(1);
    samplingSlider->setTickInterval(1);
    samplingSlider->setTickPosition(QSlider::NoTicks);
    setCompactRowHeight(samplingSlider);
    auto* const samplingValue = new QLabel(samplingRow);
    samplingValue->setObjectName(QStringLiteral("cursorSamplingRadiusValue"));
    samplingValue->setAlignment(Qt::AlignCenter);
    samplingValue->setFixedWidth(
        samplingValue->fontMetrics().horizontalAdvance(QStringLiteral("17 × 17"))
        + 2);
    setCompactRowHeight(samplingValue);
    const auto updateSamplingText = [samplingSlider, samplingValue](const int radius) {
        const auto sideLength = core::Volume::samplingSideLength(radius);
        const auto pixelCount = core::Volume::samplingPixelCount(radius);
        const QString text = QStringLiteral("%1 × %1")
                                 .arg(static_cast<qulonglong>(sideLength));
        samplingValue->setText(text);
        samplingSlider->setToolTip(
            sideLength == 1
            ? QObject::tr("Inspect the pixel under the cursor")
            : QObject::tr(
                  "Calculate statistics from the %1 pixels surrounding the "
                  "cursor in the visible slice")
                  .arg(static_cast<qulonglong>(pixelCount)));
    };
    updateSamplingText(samplingSlider->value());
    connect(
        samplingSlider,
        &QSlider::valueChanged,
        this,
        [this, updateSamplingText](const int radius) {
            updateSamplingText(radius);
            emit samplingRadiusChanged(radius);
        });
    samplingLayout->addWidget(samplingSlider, 1);
    samplingLayout->addWidget(samplingValue);
    setCompactRowHeight(samplingRow);
    inspectorLayout->addWidget(samplingRow);

    auto* const statisticsSection = new QWidget(inspectorGroup);
    statisticsSection->setObjectName(QStringLiteral("cursorStatisticsSection"));
    statisticsSection->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* const statisticsLayout = new QVBoxLayout(statisticsSection);
    statisticsLayout->setContentsMargins(0, 0, 0, 0);
    statisticsLayout->setSpacing(6);
    auto* const statisticsLabel =
        makeCompactLabel(tr("Area statistics:"), statisticsSection);
    statisticsLayout->addWidget(statisticsLabel);
    auto* const statisticsPanel =
        new SamplingStatisticsPanel(statisticsSection);
    maximumIntensityValue_ = statisticsPanel->maximumValue();
    meanIntensityValue_ = statisticsPanel->meanValue();
    medianIntensityValue_ = statisticsPanel->medianValue();
    minimumIntensityValue_ = statisticsPanel->minimumValue();
    statisticsLayout->addWidget(statisticsPanel);
    inspectorLayout->addWidget(statisticsSection);

    auto* const statisticsOpacity =
        new QGraphicsOpacityEffect(statisticsSection);
    statisticsOpacity->setOpacity(0.0);
    statisticsSection->setGraphicsEffect(statisticsOpacity);
    statisticsSection->setMaximumHeight(0);
    statisticsSection->hide();

    auto* const statisticsAnimation =
        new QParallelAnimationGroup(statisticsSection);
    auto* const statisticsHeightAnimation = new QPropertyAnimation(
        statisticsSection, "maximumHeight", statisticsAnimation);
    statisticsHeightAnimation->setDuration(160);
    auto* const statisticsOpacityAnimation = new QPropertyAnimation(
        statisticsOpacity, "opacity", statisticsAnimation);
    statisticsOpacityAnimation->setDuration(130);
    connect(
        statisticsAnimation,
        &QParallelAnimationGroup::finished,
        statisticsSection,
        [statisticsSection, statisticsOpacity, samplingSlider] {
            const bool expanded = samplingSlider->value() > 1;
            statisticsSection->setVisible(expanded);
            statisticsSection->setMaximumHeight(expanded ? QWIDGETSIZE_MAX : 0);
            statisticsOpacity->setOpacity(expanded ? 1.0 : 0.0);
        });

    const auto animateStatistics = [
                                       statisticsSection,
                                       statisticsLayout,
                                       statisticsOpacity,
                                       statisticsAnimation,
                                       statisticsHeightAnimation,
                                       statisticsOpacityAnimation](
                                       const bool expanded) {
        if((expanded && statisticsSection->isVisible()
            && statisticsOpacity->opacity() >= 0.999)
           || (!expanded && !statisticsSection->isVisible()))
        {
            return;
        }
        statisticsAnimation->stop();
        const int startHeight = statisticsSection->isVisible()
            ? statisticsSection->height()
            : 0;
        if(expanded)
        {
            statisticsSection->show();
        }
        statisticsSection->setMaximumHeight(startHeight);
        statisticsHeightAnimation->setStartValue(startHeight);
        statisticsHeightAnimation->setEndValue(
            expanded ? statisticsLayout->sizeHint().height() : 0);
        statisticsHeightAnimation->setEasingCurve(
            expanded ? QEasingCurve::OutCubic : QEasingCurve::InCubic);
        statisticsOpacityAnimation->setStartValue(statisticsOpacity->opacity());
        statisticsOpacityAnimation->setEndValue(expanded ? 1.0 : 0.0);
        statisticsOpacityAnimation->setEasingCurve(
            expanded ? QEasingCurve::OutCubic : QEasingCurve::InCubic);
        statisticsAnimation->start();
    };
    connect(
        samplingSlider,
        &QSlider::valueChanged,
        statisticsSection,
        [animateStatistics, statisticsLabel](const int radius) {
            const auto sideLength = core::Volume::samplingSideLength(radius);
            statisticsLabel->setText(
                QObject::tr("Area statistics (%1 × %1 pixels):")
                    .arg(static_cast<qulonglong>(sideLength)));
            animateStatistics(radius > 1);
        });

    inspectorLayout->addWidget(
        makeCompactLabel(tr("Label under cursor:"), inspectorGroup));
    auto* const labelRow = new QWidget(inspectorGroup);
    auto* const labelLayout = new QHBoxLayout(labelRow);
    labelLayout->setContentsMargins(0, 0, 0, 0);
    labelLayout->setSpacing(6);
    labelValue_ = makeReadOnlyField(labelRow);
    labelValue_->setMaximumWidth(48);
    labelName_ = makeReadOnlyField(labelRow);
    labelLayout->addWidget(labelValue_);
    labelLayout->addWidget(labelName_, 1);
    inspectorLayout->addWidget(labelRow);
    inspectorGroup->setEnabled(false);
    controlsLayout->addWidget(inspectorGroup);
    controlsLayout->addStretch(1);
}

void ViewerToolbox::setCursorInspection(
    const QString& x,
    const QString& y,
    const QString& z,
    const QString& intensity,
    const QString& maximumIntensity,
    const QString& meanIntensity,
    const QString& medianIntensity,
    const QString& minimumIntensity,
    const QString& labelValue,
    const QString& labelName,
    const QStringList& annotationNames,
    const QStringList& annotationIntensities,
    const int totalAxialSlices)
{
    cursorX_->setText(x);
    cursorY_->setText(y);
    cursorSliceValidator_->setRange(1, std::max(1, totalAxialSlices));
    if(!cursorZ_->hasFocus() || !cursorZ_->isModified())
    {
        cursorZ_->setText(z);
    }
    intensityValue_->setText(intensity);
    maximumIntensityValue_->setText(maximumIntensity);
    meanIntensityValue_->setText(meanIntensity);
    medianIntensityValue_->setText(medianIntensity);
    minimumIntensityValue_->setText(minimumIntensity);
    labelValue_->setText(labelValue);
    labelName_->setText(labelName);
    if(cursorIntensityTable_ != nullptr)
    {
        static_cast<IntensityUnderCursorPanel*>(cursorIntensityTable_)
            ->setAnnotationLayers(annotationNames, annotationIntensities);
    }
}

void ViewerToolbox::setWindowLevel(
    const double window,
    const double level,
    const double intensityMinimum,
    const double intensityMaximum)
{
    contentStack_->setCurrentWidget(controlsScroll_);
    contrastGroup_->setEnabled(true);
    annotationsGroup_->setEnabled(true);
    annotationLabelsGroup_->setEnabled(annotationTable_->rowCount() > 0);
    cursorInspectorGroup_->setEnabled(true);
    const QSignalBlocker windowBlocker(windowSpin_);
    const QSignalBlocker levelBlocker(levelSpin_);
    const QSignalBlocker minimumBlocker(minimumSpin_);
    const QSignalBlocker maximumBlocker(maximumSpin_);
    const QSignalBlocker presetBlocker(windowLevelPreset_);
    windowSpin_->setValue(window);
    levelSpin_->setValue(level);
    minimumSpin_->setValue(level - window / 2.0);
    maximumSpin_->setValue(level + window / 2.0);
    int presetIndex = matchingPresetIndex(
        window, level, intensityMinimum, intensityMaximum);
    if(presetIndex == 0)
    {
        for(std::size_t index = 0; index < namedPresets_.size(); ++index)
        {
            if(nearlyEqual(window, namedPresets_[index].window)
               && nearlyEqual(level, namedPresets_[index].level))
            {
                presetIndex = static_cast<int>(index) + 6;
                break;
            }
        }
    }
    windowLevelPreset_->setCurrentIndex(presetIndex);
}

void ViewerToolbox::setNamedWindowLevelPresets(
    const std::vector<app::WindowLevelSetting>& presets)
{
    const QSignalBlocker blocker(windowLevelPreset_);
    while(windowLevelPreset_->count() > 6)
    {
        windowLevelPreset_->removeItem(6);
    }
    namedPresets_ = presets;
    for(const auto& preset : namedPresets_)
    {
        windowLevelPreset_->addItem(preset.name);
    }
}

void ViewerToolbox::addAnnotation(
    const QString& name,
    const QString& kind,
    const double opacity,
    const bool visible)
{
    const int row = annotationTable_->rowCount();
    annotationTable_->insertRow(row);

    auto* const visibilityButton = new QPushButton(annotationTable_);
    visibilityButton->setObjectName(QStringLiteral("annotationVisibilityButton"));
    visibilityButton->setCheckable(true);
    visibilityButton->setChecked(visible);
    visibilityButton->setIconSize(QSize(14, 14));
    visibilityButton->setFixedSize(20, 20);
    const auto updateVisibilityButton = [this, visibilityButton, name](
                                            const bool isVisible) {
        visibilityButton->setIcon(svgIcon(
            isVisible ? QStringLiteral(":/icons/eye-show.svg")
                      : QStringLiteral(":/icons/eye-hide.svg")));
        visibilityButton->setAccessibleName(
            isVisible ? tr("Hide annotation %1").arg(name)
                      : tr("Show annotation %1").arg(name));
        visibilityButton->setToolTip(
            isVisible ? tr("Hide %1").arg(name) : tr("Show %1").arg(name));
    };
    updateVisibilityButton(visible);
    auto* const visibilityCell = new QWidget(annotationTable_);
    auto* const visibilityLayout = new QHBoxLayout(visibilityCell);
    visibilityLayout->setContentsMargins(0, 0, 0, 0);
    visibilityLayout->setAlignment(Qt::AlignCenter);
    visibilityLayout->addWidget(visibilityButton);
    annotationTable_->setCellWidget(
        row, annotationVisibilityColumn, visibilityCell);
    connect(
        visibilityButton,
        &QPushButton::toggled,
        this,
        [this, visibilityButton, updateVisibilityButton](const bool isVisible) {
            updateVisibilityButton(isVisible);
            for(int index = 0; index < annotationTable_->rowCount(); ++index)
            {
                const auto* const cell = annotationTable_->cellWidget(
                    index, annotationVisibilityColumn);
                if(cell != nullptr
                   && cell->findChild<QPushButton*>() == visibilityButton)
                {
                    emit annotationVisibilityChanged(index, isVisible);
                    return;
                }
            }
        });

    auto* const nameItem = new QTableWidgetItem(name);
    nameItem->setToolTip(name);
    nameItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    annotationTable_->setItem(row, annotationNameColumn, nameItem);
    auto* const kindItem = new QTableWidgetItem(kind);
    kindItem->setTextAlignment(Qt::AlignCenter);
    annotationTable_->setItem(row, annotationKindColumn, kindItem);

    auto* const opacityBattery = new OpacityBattery(opacity, annotationTable_);
    annotationTable_->setCellWidget(row, annotationOpacityColumn, opacityBattery);
    opacityBattery->setChangeCallback(
        [this, opacityBattery](const double value) {
            for(int index = 0; index < annotationTable_->rowCount(); ++index)
            {
                if(annotationTable_->cellWidget(index, annotationOpacityColumn)
                   == opacityBattery)
                {
                    emit annotationOpacityChanged(index, value);
                    return;
                }
            }
        });

    auto* const removeButton =
        new QPushButton(annotationTable_);
    removeButton->setObjectName(QStringLiteral("removeAnnotationButton"));
    removeButton->setAccessibleName(tr("Remove annotation %1").arg(name));
    removeButton->setToolTip(tr("Remove %1").arg(name));
    removeButton->setIcon(svgIcon(QStringLiteral(":/icons/close.svg")));
    removeButton->setIconSize(QSize(11, 11));
    removeButton->setFixedSize(18, 18);
    auto* const removeCell = new QWidget(annotationTable_);
    auto* const removeLayout = new QHBoxLayout(removeCell);
    removeLayout->setContentsMargins(0, 0, 0, 0);
    removeLayout->setAlignment(Qt::AlignCenter);
    removeLayout->addWidget(removeButton);
    annotationTable_->setCellWidget(row, annotationRemoveColumn, removeCell);
    connect(removeButton, &QPushButton::clicked, this, [this, removeButton] {
        for(int index = 0; index < annotationTable_->rowCount(); ++index)
        {
            const auto* const cell = annotationTable_->cellWidget(
                index, annotationRemoveColumn);
            if(cell != nullptr && cell->findChild<QPushButton*>() == removeButton)
            {
                emit annotationRemovalRequested(index);
                return;
            }
        }
    });
    annotationTable_->resizeRowsToContents();
    updateAnnotationEmptyState();
}

void ViewerToolbox::setAnnotationName(const int index, const QString& name)
{
    if(index < 0 || index >= annotationTable_->rowCount() || name.isEmpty())
    {
        return;
    }
    if(auto* const nameItem = annotationTable_->item(index, annotationNameColumn))
    {
        nameItem->setText(name);
        nameItem->setToolTip(name);
    }
    if(cursorIntensityTable_ != nullptr)
    {
        static_cast<IntensityUnderCursorPanel*>(cursorIntensityTable_)
            ->setAnnotationName(index, name);
    }
    if(auto* const cell = annotationTable_->cellWidget(
           index, annotationVisibilityColumn))
    {
        if(auto* const visibilityButton = cell->findChild<QPushButton*>())
        {
            const bool visible = visibilityButton->isChecked();
            visibilityButton->setAccessibleName(
                visible ? tr("Hide annotation %1").arg(name)
                        : tr("Show annotation %1").arg(name));
            visibilityButton->setToolTip(
                visible ? tr("Hide %1").arg(name) : tr("Show %1").arg(name));
        }
    }
    if(auto* const cell = annotationTable_->cellWidget(index, annotationRemoveColumn))
    {
        if(auto* const removeButton = cell->findChild<QPushButton*>())
        {
            removeButton->setAccessibleName(tr("Remove annotation %1").arg(name));
            removeButton->setToolTip(tr("Remove %1").arg(name));
        }
    }
}

void ViewerToolbox::removeAnnotation(const int index)
{
    if(index >= 0 && index < annotationTable_->rowCount())
    {
        annotationTable_->removeRow(index);
        updateAnnotationEmptyState();
    }
}

void ViewerToolbox::clearAnnotations()
{
    annotationTable_->setRowCount(0);
    updateAnnotationEmptyState();
    if(cursorIntensityTable_ != nullptr)
    {
        static_cast<IntensityUnderCursorPanel*>(cursorIntensityTable_)
            ->setAnnotationLayers({}, {});
    }
}

void ViewerToolbox::updateAnnotationEmptyState()
{
    if(annotationStack_ == nullptr || annotationEmptyState_ == nullptr
       || annotationTable_ == nullptr)
    {
        return;
    }
    annotationStack_->setCurrentWidget(
        annotationTable_->rowCount() == 0
            ? annotationEmptyState_ : static_cast<QWidget*>(annotationTable_));
    if(annotationLabelsGroup_ != nullptr)
    {
        annotationLabelsGroup_->setEnabled(annotationTable_->rowCount() > 0);
    }
}

void ViewerToolbox::selectAnnotation(const int index)
{
    if(index < 0 || index >= annotationTable_->rowCount())
    {
        return;
    }
    annotationTable_->clearSelection();
    annotationTable_->selectRow(index);
    annotationTable_->setCurrentCell(index, 0);
}

void ViewerToolbox::setAnnotationOpacity(
    const int index, const double opacity)
{
    if(index < 0 || index >= annotationTable_->rowCount())
    {
        return;
    }
    auto* const battery = static_cast<OpacityBattery*>(
        annotationTable_->cellWidget(index, annotationOpacityColumn));
    if(battery != nullptr)
    {
        battery->setOpacity(opacity);
    }
}

void ViewerToolbox::setAnnotationVisibility(
    const int index,
    const bool visible)
{
    if(index < 0 || index >= annotationTable_->rowCount())
    {
        return;
    }
    auto* const cell = annotationTable_->cellWidget(
        index, annotationVisibilityColumn);
    auto* const button = cell != nullptr
        ? cell->findChild<QPushButton*>() : nullptr;
    if(button != nullptr)
    {
        const QSignalBlocker blocker(button);
        button->setChecked(visible);
        button->setIcon(svgIcon(
            visible ? QStringLiteral(":/icons/eye-show.svg")
                    : QStringLiteral(":/icons/eye-hide.svg")));
        const auto* const nameItem = annotationTable_->item(
            index, annotationNameColumn);
        const QString name = nameItem != nullptr ? nameItem->text() : QString{};
        button->setAccessibleName(
            visible ? tr("Hide annotation %1").arg(name)
                    : tr("Show annotation %1").arg(name));
        button->setToolTip(
            visible ? tr("Hide %1").arg(name) : tr("Show %1").arg(name));
    }
}

void ViewerToolbox::setOverallLabelOpacity(const double opacity)
{
    const int percent = static_cast<int>(std::lround(
        std::clamp(opacity, 0.0, 1.0) * 100.0));
    const QSignalBlocker spinBlocker(labelOpacitySpin_);
    const QSignalBlocker sliderBlocker(labelOpacitySlider_);
    labelOpacitySpin_->setValue(percent);
    labelOpacitySlider_->setValue(percent);
}

void ViewerToolbox::setAnnotationLabels(const QList<int>& labels)
{
    QList<int> sortedLabels = labels;
    std::sort(sortedLabels.begin(), sortedLabels.end());
    sortedLabels.erase(
        std::remove_if(
            sortedLabels.begin(), sortedLabels.end(), [](const int label) {
                return label < 1 || label > 65535;
            }),
        sortedLabels.end());
    sortedLabels.erase(
        std::unique(sortedLabels.begin(), sortedLabels.end()),
        sortedLabels.end());

    {
        const QSignalBlocker paintOverBlocker(paintOverCombo_);
        paintOverCombo_->clear();
        paintOverCombo_->addItem(tr("All labels"), -1);
        paintOverCombo_->addItem(tr("Clear Label"), 0);
        for(const int label : sortedLabels)
        {
            paintOverCombo_->addItem(
                labelIcon(label), tr("Label %1").arg(label), label);
        }
    }
    applyPaintOverForActiveLabel();
}

void ViewerToolbox::setActiveLabel(const int label)
{
    if(!activeLabelCombo_->isEnabled() || label < 0 || label > 65535)
    {
        return;
    }
    const int index = label;
    if(activeLabelCombo_->currentIndex() != index)
    {
        activeLabelCombo_->setCurrentIndex(index);
    }
}

int ViewerToolbox::activeLabel() const
{
    return activeLabelCombo_->currentData().toInt();
}

void ViewerToolbox::setBrushRadii(const std::map<int, int>& radii)
{
    brushRadii_.clear();
    for(const auto& [label, radius] : radii)
    {
        if(label >= 0 && label <= 65535 && radius >= 1 && radius <= 100)
        {
            brushRadii_[label] = radius;
        }
    }
    const auto selected = brushRadii_.find(activeLabel());
    const int radius = selected != brushRadii_.end() ? selected->second : 1;
    const QSignalBlocker spinBlocker(brushRadiusSpin_);
    const QSignalBlocker sliderBlocker(brushRadiusSlider_);
    brushRadiusSpin_->setValue(radius);
    brushRadiusSlider_->setValue(radius);
    emit brushRadiusChanged(radius);
}

void ViewerToolbox::setPaintOverSelections(
    const std::map<int, int>& selections)
{
    paintOverSelections_.clear();
    for(const auto& [label, selection] : selections)
    {
        if(label >= 0 && label <= 65535
           && selection >= -1 && selection <= 65535)
        {
            paintOverSelections_[label] = selection;
        }
    }
    applyPaintOverForActiveLabel();
}

void ViewerToolbox::applyPaintOverForActiveLabel()
{
    const auto selected = paintOverSelections_.find(activeLabel());
    const int desired =
        selected != paintOverSelections_.end() ? selected->second : -1;
    const int index = paintOverCombo_->findData(desired);
    {
        const QSignalBlocker blocker(paintOverCombo_);
        paintOverCombo_->setCurrentIndex(index >= 0 ? index : 0);
    }
    emit paintOverChanged(paintOverCombo_->currentData().toInt());
}

void ViewerToolbox::adjustBrushRadius(const int delta)
{
    if(brushRadiusSpin_ == nullptr || delta == 0)
    {
        return;
    }
    const int next = std::clamp(
        brushRadiusSpin_->value() + delta,
        brushRadiusSpin_->minimum(),
        brushRadiusSpin_->maximum());
    if(next != brushRadiusSpin_->value())
    {
        brushRadiusSpin_->setValue(next);
    }
}

void ViewerToolbox::setAnnotationEditingState(
    const bool editable, const bool, const bool)
{
    brushRadiusSpin_->setEnabled(editable);
    brushRadiusSlider_->setEnabled(editable);
    squareBrushButton_->setEnabled(editable);
    circleBrushButton_->setEnabled(editable);
    activeLabelCombo_->setEnabled(editable);
    paintOverCombo_->setEnabled(editable);
}

QList<int> ViewerToolbox::selectedAnnotationIndices() const
{
    QList<int> indices;
    const auto selectedRows = annotationTable_->selectionModel()->selectedRows();
    indices.reserve(selectedRows.size());
    for(const auto& row : selectedRows)
    {
        indices.push_back(row.row());
    }
    std::sort(indices.begin(), indices.end());
    if(indices.size() > 2)
    {
        indices.erase(indices.begin() + 2, indices.end());
    }
    return indices;
}

void ViewerToolbox::clearVolume()
{
    clearAnnotations();
    contrastGroup_->setEnabled(false);
    annotationsGroup_->setEnabled(false);
    annotationLabelsGroup_->setEnabled(false);
    cursorInspectorGroup_->setEnabled(false);
    contentStack_->setCurrentIndex(0);
    setCursorInspection(
        QStringLiteral("—"),
        QStringLiteral("—"),
        QStringLiteral("—"),
        QStringLiteral("—"),
        QStringLiteral("—"),
        QStringLiteral("—"),
        QStringLiteral("—"),
        QStringLiteral("—"),
        QStringLiteral("0"),
        tr("Clear Label"),
        {},
        {},
        0);
}

QWidget* ViewerToolbox::contrastPanel() const noexcept
{
    return contrastGroup_;
}

QWidget* ViewerToolbox::identityPanel() const noexcept
{
    return identityPage_;
}

QWidget* ViewerToolbox::annotationsPanel() const noexcept
{
    return annotationsGroup_;
}

QWidget* ViewerToolbox::annotationLabelsPanel() const noexcept
{
    return annotationLabelsGroup_;
}

QWidget* ViewerToolbox::cursorInspectorPanel() const noexcept
{
    return cursorInspectorGroup_;
}

} // namespace radmarky::ui
