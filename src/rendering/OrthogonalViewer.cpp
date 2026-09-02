#include "rendering/OrthogonalViewer.h"

#include "core/Annotation.h"
#include "core/OrthogonalSliceGeometry.h"
#include "core/ViewerState.h"
#include "core/AnnotationEditor.h"
#include "core/Volume.h"
#include "core/WindowLevel.h"
#include "io/AnimationWriter.h"
#include "io/AnimatedGifWriter.h"
#include "io/Mp4Writer.h"
#include "rendering/ItkVtkImageBridge.h"
#include "rendering/RenderProfiling.h"
#include "rendering/VtkViewport.h"
#include "ui/GifExportDialog.h"
#include "ui/UiTheme.h"

#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QMessageBox>
#include <QProgressDialog>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStringList>
#include <QSplitter>
#include <QStandardPaths>
#include <QToolButton>
#include <QVBoxLayout>

#include <vtkImageData.h>
#include <vtkSmartPointer.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <utility>
#include <vector>

namespace radmarky::rendering
{
namespace
{

constexpr int splitterHandleWidth = 4;
constexpr int viewerFrameMargin = splitterHandleWidth + 1;
constexpr int recentThumbnailMaximumWidth = 360;
constexpr int recentThumbnailMaximumHeight = 240;

int totalAxialSlices(const core::ImageGeometry& geometry)
{
    return static_cast<int>(std::min<std::size_t>(
        geometry.dimensions()[2],
        static_cast<std::size_t>(std::numeric_limits<int>::max())));
}

int currentAxialSlice(const core::ViewerState& state)
{
    const int totalSlices = totalAxialSlices(state.geometry());
    return static_cast<int>(std::clamp(
        std::llround(state.cursorContinuousIndex()[2]) + 1LL,
        1LL,
        static_cast<long long>(totalSlices)));
}

QImage renderAxialThumbnail(
    const core::Volume& volume,
    const core::ViewerState& state)
{
    const auto slice = core::OrthogonalSliceGeometry::fromImageGeometry(
        volume.geometry(), core::SliceOrientation::Axial);
    const double sourceWidth = static_cast<double>(slice.width());
    const double sourceHeight = static_cast<double>(slice.height());
    const double scale = std::min(
        static_cast<double>(recentThumbnailMaximumWidth) / sourceWidth,
        static_cast<double>(recentThumbnailMaximumHeight) / sourceHeight);
    const int width = std::max(1, static_cast<int>(std::round(sourceWidth * scale)));
    const int height =
        std::max(1, static_cast<int>(std::round(sourceHeight * scale)));

    QImage thumbnail(width, height, QImage::Format_Grayscale8);
    thumbnail.fill(0);
    const auto& windowLevel = state.windowLevel();
    const double displayMinimum = windowLevel.displayMinimum();
    const double displayWidth = windowLevel.window();
    const double horizontalSpan = static_cast<double>(slice.width() - 1)
        * slice.outputSpacing();
    const double verticalSpan = static_cast<double>(slice.height() - 1)
        * slice.outputSpacing();
    const auto& cursor = state.cursorPhysical();

    for(int row = 0; row < height; ++row)
    {
        auto* const pixels = thumbnail.scanLine(row);
        const double verticalFraction = height == 1
            ? 0.5
            : 1.0 - static_cast<double>(row) / static_cast<double>(height - 1);
        const double vertical =
            slice.verticalMinimum() + verticalFraction * verticalSpan;
        for(int column = 0; column < width; ++column)
        {
            const double horizontalFraction = width == 1
                ? 0.5
                : static_cast<double>(column) / static_cast<double>(width - 1);
            const double horizontal =
                slice.horizontalMinimum() + horizontalFraction * horizontalSpan;
            const auto sample = volume.sampleNearestPhysical(
                slice.pointOnCursorPlane(horizontal, vertical, cursor));
            if(!sample || !std::isfinite(sample->value))
            {
                pixels[column] = 0;
                continue;
            }
            double luminance =
                (static_cast<double>(sample->value) - displayMinimum) / displayWidth;
            luminance = std::clamp(luminance, 0.0, 1.0);
            if(state.inverted())
            {
                luminance = 1.0 - luminance;
            }
            pixels[column] = static_cast<uchar>(std::round(luminance * 255.0));
        }
    }
    return thumbnail;
}

struct SlicePanel
{
    core::SliceOrientation orientation = core::SliceOrientation::Axial;
    QWidget* panelWidget = nullptr;
    VtkViewport* viewport = nullptr;
    QScrollBar* sliceScrollBar = nullptr;
    QToolButton* centerButton = nullptr;
    QToolButton* expandButton = nullptr;
    QToolButton* screenshotButton = nullptr;
    std::optional<core::OrthogonalSliceGeometry> geometry;
};

QString orientationTitle(const core::SliceOrientation orientation)
{
    switch(orientation)
    {
    case core::SliceOrientation::Axial:
        return QStringLiteral("AXIAL");
    case core::SliceOrientation::Sagittal:
        return QStringLiteral("SAGITTAL");
    case core::SliceOrientation::Coronal:
        return QStringLiteral("CORONAL");
    }
    return {};
}

QString orientationLetter(const core::SliceOrientation orientation)
{
    switch(orientation)
    {
    case core::SliceOrientation::Axial:
        return QStringLiteral("A");
    case core::SliceOrientation::Sagittal:
        return QStringLiteral("S");
    case core::SliceOrientation::Coronal:
        return QStringLiteral("C");
    }
    return {};
}

void setFocusButtonAppearance(SlicePanel& panel, const bool singleView)
{
    auto* const button = panel.expandButton;
    if(singleView)
    {
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setText({});
        button->setIcon(ui::svgIcon(QStringLiteral(":/icons/expand-view.svg")));
        button->setToolTip(QObject::tr("Restore all views"));
    }
    else
    {
        button->setIcon(QIcon());
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setText(orientationLetter(panel.orientation));
        switch(panel.orientation)
        {
        case core::SliceOrientation::Axial:
            button->setToolTip(QObject::tr("Focus axial view"));
            break;
        case core::SliceOrientation::Sagittal:
            button->setToolTip(QObject::tr("Focus sagittal view"));
            break;
        case core::SliceOrientation::Coronal:
            button->setToolTip(QObject::tr("Focus coronal view"));
            break;
        }
    }
    button->setAccessibleName(button->toolTip());
}

QString formatSampledValue(const double value)
{
    if(!std::isfinite(value))
    {
        return QStringLiteral("—");
    }
    const bool integral = std::abs(value - std::round(value)) < 1.0e-4;
    return QString::number(value, integral ? 'f' : 'g', integral ? 0 : 6);
}

QString formatSliceGap(
    const std::vector<double>& gaps,
    const std::size_t sliceIndex,
    const bool previous)
{
    if((previous && (sliceIndex == 0 || sliceIndex > gaps.size()))
       || (!previous && sliceIndex >= gaps.size()))
    {
        return QStringLiteral("—");
    }
    const std::size_t gapIndex = previous ? sliceIndex - 1 : sliceIndex;
    return QStringLiteral("%1 mm").arg(gaps[gapIndex], 0, 'f', 3);
}

QString formatAnnotationInspection(
    const core::Annotation& annotation,
    const core::ImageGeometry::Vector& point)
{
    const auto sample = annotation.volume().sampleNearestPhysical(point);
    if(!sample)
    {
        return QStringLiteral("—");
    }
    if(annotation.kind() == core::AnnotationKind::LabelMap)
    {
        const auto label =
            static_cast<unsigned int>(std::llround(sample->value));
        return label == 0
            ? QObject::tr("Clear Label")
            : QObject::tr("Label %1").arg(label);
    }
    return formatSampledValue(sample->value);
}

QWidget* makePanel(SlicePanel& controls, QWidget* const parent)
{
    auto* const panel = new QFrame(parent);
    controls.panelWidget = panel;
    panel->setObjectName("slicePanel");
    panel->setFrameShape(QFrame::StyledPanel);
    panel->setMinimumSize(120, 100);
    auto* const outerLayout = new QVBoxLayout(panel);
    outerLayout->setContentsMargins(1, 1, 1, 1);
    outerLayout->setSpacing(0);

    auto* const content = new QWidget(panel);
    auto* const contentLayout = new QHBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(1);
    controls.viewport = new VtkViewport(controls.orientation, content);
    controls.viewport->setInteractionMode(
        VtkViewport::InteractionMode::Crosshair);
    contentLayout->addWidget(controls.viewport, 1);

    auto* const tools = new QWidget(content);
    tools->setObjectName(QStringLiteral("slicePanelTools"));
    tools->setFixedWidth(24);
    auto* const toolsLayout = new QVBoxLayout(tools);
    toolsLayout->setContentsMargins(1, 1, 1, 0);
    toolsLayout->setSpacing(1);
    const auto makeToolButton = [tools](
                                    const QString& name,
                                    const QString& tooltip,
                                    const QString& icon) {
        auto* const button = new QToolButton(tools);
        button->setObjectName(name);
        button->setAccessibleName(tooltip);
        button->setToolTip(tooltip);
        button->setIcon(ui::svgIcon(icon));
        button->setIconSize(QSize(16, 16));
        button->setFixedSize(22, 22);
        button->setAutoRaise(true);
        return button;
    };
    controls.centerButton = makeToolButton(
        QStringLiteral("centerViewButton"),
        QObject::tr("Center and fit this view"),
        QStringLiteral(":/icons/center-view.svg"));
    controls.expandButton = makeToolButton(
        QStringLiteral("expandViewButton"),
        QObject::tr("Focus this view"),
        QStringLiteral(":/icons/expand-view.svg"));
    controls.expandButton->setCheckable(true);
    setFocusButtonAppearance(controls, false);
    controls.screenshotButton = makeToolButton(
        QStringLiteral("screenshotButton"),
        QObject::tr("Save this slice view without the crosshair"),
        QStringLiteral(":/icons/camera.svg"));
    controls.expandButton->setEnabled(false);
    controls.centerButton->setEnabled(false);
    controls.screenshotButton->setEnabled(false);
    toolsLayout->addWidget(controls.expandButton, 0, Qt::AlignHCenter);
    toolsLayout->addWidget(controls.centerButton, 0, Qt::AlignHCenter);
    toolsLayout->addWidget(controls.screenshotButton, 0, Qt::AlignHCenter);

    controls.sliceScrollBar = new QScrollBar(Qt::Vertical, tools);
    controls.sliceScrollBar->setObjectName("sliceScrollBar");
    controls.sliceScrollBar->setToolTip(QObject::tr("Slice position"));
    controls.sliceScrollBar->setEnabled(false);
    toolsLayout->addWidget(controls.sliceScrollBar, 1, Qt::AlignHCenter);
    contentLayout->addWidget(tools);
    outerLayout->addWidget(content, 1);
    return panel;
}

} // namespace

struct OrthogonalViewer::Impl
{
    std::array<SlicePanel, 3> panels{{
        {core::SliceOrientation::Axial},
        {core::SliceOrientation::Sagittal},
        {core::SliceOrientation::Coronal},
    }};
    std::shared_ptr<const core::Volume> volume;
    vtkSmartPointer<vtkImageData> imageData;
    struct AnnotationData
    {
        std::shared_ptr<core::Annotation> annotation;
        vtkSmartPointer<vtkImageData> imageData;
    };
    std::vector<AnnotationData> annotations;
    std::vector<std::size_t> selectedAnnotationIndices;
    core::AnnotationEditor annotationEditor;
    std::optional<std::size_t> editableAnnotationIndex;
    vtkSmartPointer<vtkImageData> annotationComparison;
    std::optional<std::array<std::size_t, 2>> comparisonIndices;
    std::optional<core::ViewerState> state;
    core::SliceAlignment sliceAlignment = core::SliceAlignment::Patient;
    std::optional<core::WindowLevel> windowLevelDragOrigin;
    std::optional<double> labelOpacityDragOrigin;
    VtkViewport::InteractionMode interactionMode =
        VtkViewport::InteractionMode::Crosshair;
    QSplitter* rightViews = nullptr;
    QSplitter* viewSplitter = nullptr;
    std::optional<core::SliceOrientation> focusedView;
    QList<int> rightViewSizes;
    QList<int> viewSizes;
    int samplingRadius = 1;
    double overallLabelOpacity = 1.0;
    core::SliceOrientation inspectionOrientation =
        core::SliceOrientation::Axial;
    std::optional<core::ImageGeometry::Vector> inspectionPoint;
};

OrthogonalViewer::OrthogonalViewer(QWidget* parent)
    : QWidget(parent)
    , impl_(std::make_unique<Impl>())
{
    setObjectName("orthogonalViewer");
    auto* const rightViews = new QSplitter(Qt::Vertical, this);
    impl_->rightViews = rightViews;
    rightViews->setObjectName(QStringLiteral("rightViewSplitter"));
    rightViews->setChildrenCollapsible(false);
    rightViews->setHandleWidth(splitterHandleWidth);
    rightViews->addWidget(makePanel(impl_->panels[1], rightViews));
    rightViews->addWidget(makePanel(impl_->panels[2], rightViews));
    rightViews->setStretchFactor(0, 1);
    rightViews->setStretchFactor(1, 1);
    rightViews->setSizes({360, 360});

    auto* const viewSplitter = new QSplitter(Qt::Horizontal, this);
    impl_->viewSplitter = viewSplitter;
    viewSplitter->setObjectName(QStringLiteral("viewSplitter"));
    viewSplitter->setChildrenCollapsible(false);
    viewSplitter->setHandleWidth(splitterHandleWidth);
    viewSplitter->addWidget(makePanel(impl_->panels[0], viewSplitter));
    viewSplitter->addWidget(rightViews);
    viewSplitter->setStretchFactor(0, 2);
    viewSplitter->setStretchFactor(1, 1);
    viewSplitter->setSizes({640, 320});

    // Keep the perimeter entirely in Qt. The native VTK widgets live inside
    // this frame and cannot cover the frame's painted layout margin.
    auto* const viewerFrame = new QFrame(this);
    viewerFrame->setObjectName(QStringLiteral("viewerFrame"));
    viewerFrame->setAttribute(Qt::WA_StyledBackground, true);
    auto* const frameLayout = new QVBoxLayout(viewerFrame);
    // Each internal seam is one panel border + the splitter handle + the next
    // panel border. The Qt margin plus its panel border matches that width.
    frameLayout->setContentsMargins(
        viewerFrameMargin,
        viewerFrameMargin,
        viewerFrameMargin,
        viewerFrameMargin);
    frameLayout->setSpacing(0);
    frameLayout->addWidget(viewSplitter);

    auto* const layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(viewerFrame);

    for(auto& panel : impl_->panels)
    {
        connect(
            panel.centerButton,
            &QToolButton::clicked,
            panel.viewport,
            &VtkViewport::resetView);
        connect(
            panel.expandButton,
            &QToolButton::clicked,
            this,
            [this, orientation = panel.orientation] {
                toggleFocusedView(orientation);
            });
        connect(
            panel.screenshotButton,
            &QToolButton::clicked,
            this,
            [this, orientation = panel.orientation] {
                saveScreenshot(orientation);
            });
        connect(
            panel.viewport,
            &VtkViewport::physicalPointSelected,
            this,
            [this, orientation = panel.orientation](
                const double x, const double y, const double z) {
                impl_->inspectionOrientation = orientation;
                selectPhysicalPoint(x, y, z);
            });
        connect(
            panel.viewport,
            &VtkViewport::sliceStepRequested,
            this,
            [this, orientation = panel.orientation](const int steps) {
                stepSlice(orientation, steps);
            });
        connect(
            panel.viewport,
            &VtkViewport::physicalPointHovered,
            this,
            [this, orientation = panel.orientation](
                const double x, const double y, const double z) {
                inspectPhysicalPoint(x, y, z, orientation);
            });
        connect(
            panel.viewport,
            &VtkViewport::pointerExited,
            this,
            [this, orientation = panel.orientation] {
                impl_->inspectionOrientation = orientation;
                restoreCrosshairInspection();
            });
        connect(
            panel.viewport,
            &VtkViewport::windowLevelDragStarted,
            this,
            &OrthogonalViewer::beginWindowLevelDrag);
        connect(
            panel.viewport,
            &VtkViewport::windowLevelDragged,
            this,
            &OrthogonalViewer::updateWindowLevelDrag);
        connect(
            panel.viewport,
            &VtkViewport::labelOpacityDragStarted,
            this,
            &OrthogonalViewer::beginLabelOpacityDrag);
        connect(
            panel.viewport,
            &VtkViewport::labelOpacityDragged,
            this,
            &OrthogonalViewer::updateLabelOpacityDrag);
        connect(
            panel.viewport,
            &VtkViewport::editStrokeStarted,
            this,
            [this, orientation = panel.orientation](double x, double y, double z) {
                if(orientation == core::SliceOrientation::Axial)
                {
                    beginEditStroke(x, y, z);
                }
            });
        connect(
            panel.viewport,
            &VtkViewport::editStrokeContinued,
            this,
            [this, orientation = panel.orientation](double x, double y, double z) {
                if(orientation == core::SliceOrientation::Axial)
                {
                    continueEditStroke(x, y, z);
                }
            });
        connect(
            panel.viewport,
            &VtkViewport::editStrokeFinished,
            this,
            [this, orientation = panel.orientation] {
                if(orientation == core::SliceOrientation::Axial)
                {
                    finishEditStroke();
                }
            });
        connect(
            panel.sliceScrollBar,
            &QScrollBar::valueChanged,
            this,
            [this, orientation = panel.orientation](const int position) {
                selectNormalPosition(orientation, position);
            });
    }
}

void OrthogonalViewer::inspectPhysicalPoint(
    const double x,
    const double y,
    const double z,
    const core::SliceOrientation orientation)
{
    if(!impl_->volume)
    {
        return;
    }
    const core::ImageGeometry::Vector point{{x, y, z}};
    const int axialSliceCount = totalAxialSlices(impl_->volume->geometry());
    impl_->inspectionPoint = point;
    impl_->inspectionOrientation = orientation;
    const auto panel = std::find_if(
        impl_->panels.begin(),
        impl_->panels.end(),
        [orientation](const SlicePanel& candidate) {
            return candidate.orientation == orientation;
        });
    const auto sample = impl_->volume->sampleNearestPhysical(point);
    const auto statistics = panel != impl_->panels.end() && panel->geometry
        ? impl_->volume->sampleStatisticsPhysical(
              point, *panel->geometry, impl_->samplingRadius)
        : sample
            ? std::optional<core::Volume::IntensityStatistics>(
                  core::Volume::IntensityStatistics{
                      sample->value,
                      sample->value,
                      sample->value,
                      sample->value})
            : std::nullopt;
    QStringList annotationNames;
    QStringList annotationIntensities;
    annotationNames.reserve(static_cast<int>(impl_->annotations.size()));
    annotationIntensities.reserve(static_cast<int>(impl_->annotations.size()));
    for(const auto& layer : impl_->annotations)
    {
        annotationNames.push_back(
            QString::fromStdString(layer.annotation->name()));
        annotationIntensities.push_back(
            formatAnnotationInspection(*layer.annotation, point));
    }
    if(!sample)
    {
        emit cursorInspectionChanged(
            QStringLiteral("—"),
            QStringLiteral("—"),
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
            annotationNames,
            annotationIntensities,
            axialSliceCount);
        return;
    }

    const QString formattedValue = formatSampledValue(sample->value);
    const QString formattedMaximum = statistics
        ? formatSampledValue(statistics->maximum) : QStringLiteral("—");
    const QString formattedMean = statistics
        ? formatSampledValue(statistics->mean) : QStringLiteral("—");
    const QString formattedMedian = statistics
        ? formatSampledValue(statistics->median) : QStringLiteral("—");
    const QString formattedMinimum = statistics
        ? formatSampledValue(statistics->minimum) : QStringLiteral("—");
    QString labelValue = QStringLiteral("0");
    QString labelName = tr("Clear Label");
    if(impl_->comparisonIndices)
    {
        const auto firstIndex = (*impl_->comparisonIndices)[0];
        const auto secondIndex = (*impl_->comparisonIndices)[1];
        const auto first = impl_->annotations[firstIndex]
                               .annotation->volume().sampleNearestPhysical(point);
        const auto second = impl_->annotations[secondIndex]
                                .annotation->volume().sampleNearestPhysical(point);
        const auto comparison = ItkVtkImageBridge::compareValues(
            first ? first->value : 0.0F,
            second ? second->value : 0.0F);
        labelValue = QString::number(static_cast<unsigned int>(comparison));
        switch(comparison)
        {
        case ItkVtkImageBridge::ComparisonClass::FirstOnly:
            labelName = tr("First annotation only");
            break;
        case ItkVtkImageBridge::ComparisonClass::SameValue:
            labelName = tr("Same annotation value");
            break;
        case ItkVtkImageBridge::ComparisonClass::SecondOnly:
            labelName = tr("Second annotation only");
            break;
        case ItkVtkImageBridge::ComparisonClass::DifferentValues:
            labelName = tr("Different annotation values");
            break;
        case ItkVtkImageBridge::ComparisonClass::Background:
            labelName = tr("Clear Label");
            break;
        }
    }
    else
    {
        for(const auto& annotation : impl_->annotations | std::views::reverse)
        {
            if(annotation.annotation->kind() != core::AnnotationKind::LabelMap)
            {
                continue;
            }
            const auto labelSample =
                annotation.annotation->volume().sampleNearestPhysical(point);
            if(labelSample && labelSample->value > 0.0F)
            {
                const auto label = static_cast<unsigned int>(
                    std::llround(labelSample->value));
                labelValue = QString::number(label);
                labelName = tr("Label %1").arg(label);
                break;
            }
        }
    }
    // A patient-axial plane can cross several source k indices in an oblique
    // or tilted volume. Keep the editable slice field tied to the plane's
    // cursor position instead of letting lateral mouse motion change it.
    const std::size_t inspectedAxialSlice =
        orientation == core::SliceOrientation::Axial && impl_->state
        ? static_cast<std::size_t>(currentAxialSlice(*impl_->state))
        : sample->index[2] + 1;
    const auto& sliceGaps = impl_->volume->dicomSliceGapsMillimetres();
    const std::size_t inspectedSliceIndex = inspectedAxialSlice - 1;
    emit cursorInspectionChanged(
        QString::number(static_cast<qulonglong>(sample->index[0])),
        QString::number(static_cast<qulonglong>(sample->index[1])),
        QString::number(static_cast<qulonglong>(inspectedAxialSlice)),
        formatSliceGap(sliceGaps, inspectedSliceIndex, true),
        formatSliceGap(sliceGaps, inspectedSliceIndex, false),
        formattedValue,
        formattedMaximum,
        formattedMean,
        formattedMedian,
        formattedMinimum,
        labelValue,
        labelName,
        annotationNames,
        annotationIntensities,
        axialSliceCount);
}

void OrthogonalViewer::restoreCrosshairInspection()
{
    if(!impl_->state)
    {
        return;
    }
    const auto& point = impl_->state->cursorPhysical();
    inspectPhysicalPoint(
        point[0], point[1], point[2], impl_->inspectionOrientation);
}

OrthogonalViewer::LayoutState OrthogonalViewer::layoutState() const
{
    LayoutState state;
    state.focusedView = impl_->focusedView;
    if(impl_->focusedView)
    {
        state.viewSplitterSizes = impl_->viewSizes;
        state.rightViewSplitterSizes = impl_->rightViewSizes;
    }
    else
    {
        state.viewSplitterSizes = impl_->viewSplitter->sizes();
        state.rightViewSplitterSizes = impl_->rightViews->sizes();
    }
    return state;
}

void OrthogonalViewer::setLayoutState(const LayoutState& state)
{
    showAllViews();
    if(state.viewSplitterSizes.size() == 2)
    {
        impl_->viewSizes = state.viewSplitterSizes;
        impl_->viewSplitter->setSizes(state.viewSplitterSizes);
    }
    if(state.rightViewSplitterSizes.size() == 2)
    {
        impl_->rightViewSizes = state.rightViewSplitterSizes;
        impl_->rightViews->setSizes(state.rightViewSplitterSizes);
    }
    if(state.focusedView)
    {
        toggleFocusedView(*state.focusedView);
    }
}

void OrthogonalViewer::setSamplingRadius(const int samplingRadius)
{
    impl_->samplingRadius = std::clamp(samplingRadius, 1, 5);
    const int sideLength = static_cast<int>(
        core::Volume::samplingSideLength(impl_->samplingRadius));
    for(auto& panel : impl_->panels)
    {
        panel.viewport->setSamplingSideLength(sideLength);
    }
    if(impl_->inspectionPoint)
    {
        const auto point = *impl_->inspectionPoint;
        inspectPhysicalPoint(
            point[0], point[1], point[2], impl_->inspectionOrientation);
    }
}

OrthogonalViewer::~OrthogonalViewer() = default;

void OrthogonalViewer::clearVolume()
{
    impl_->windowLevelDragOrigin.reset();
    impl_->labelOpacityDragOrigin.reset();
    impl_->state.reset();
    impl_->imageData = nullptr;
    impl_->volume.reset();
    impl_->sliceAlignment = core::SliceAlignment::Patient;
    impl_->annotations.clear();
    impl_->selectedAnnotationIndices.clear();
    impl_->annotationEditor.clearAnnotation();
    impl_->editableAnnotationIndex.reset();
    publishAnnotationLabels();
    impl_->annotationComparison = nullptr;
    impl_->comparisonIndices.reset();
    setOverallLabelOpacity(1.0);
    impl_->inspectionPoint.reset();
    impl_->inspectionOrientation = core::SliceOrientation::Axial;
    for(auto& panel : impl_->panels)
    {
        panel.geometry.reset();
        panel.viewport->clearInput();
        panel.expandButton->setEnabled(false);
        panel.expandButton->setChecked(false);
        setFocusButtonAppearance(panel, false);
        panel.centerButton->setEnabled(false);
        panel.screenshotButton->setEnabled(false);
        panel.sliceScrollBar->setEnabled(false);
    }
    publishAnnotationEditingState();
}

void OrthogonalViewer::addAnnotation(
    const std::shared_ptr<core::Annotation>& annotation)
{
    if(!annotation || !impl_->volume)
    {
        throw std::invalid_argument(
            "Annotation overlays require an anatomical image");
    }
    annotation->verifyGeometry(impl_->volume->geometry());
    const bool labelMap = annotation->kind() == core::AnnotationKind::LabelMap;
    const auto previousAlignment = impl_->sliceAlignment;
    if(labelMap)
    {
        // A label map is defined on discrete reference-image planes. Display
        // and edit those planes directly, as ITK-SNAP does, so an oblique
        // patient reformat cannot mix neighboring slices into a false gap.
        applySliceAlignment(core::SliceAlignment::Native);
    }
    auto imageData = ItkVtkImageBridge::shareWithVtk(annotation->volume());
    const auto range = annotation->volume().scalarRange();
    std::size_t addedPanelCount = 0;
    try
    {
        for(auto& panel : impl_->panels)
        {
            panel.viewport->addAnnotation(
                imageData,
                labelMap,
                range.minimum,
                range.maximum,
                annotation->opacity() * impl_->overallLabelOpacity);
            panel.viewport->setAnnotationVisibility(
                impl_->annotations.size(), annotation->isVisible());
            ++addedPanelCount;
        }
    }
    catch(...)
    {
        for(std::size_t panel = 0; panel < addedPanelCount; ++panel)
        {
            impl_->panels[panel].viewport->removeAnnotation(
                impl_->annotations.size());
        }
        applySliceAlignment(previousAlignment);
        throw;
    }
    impl_->annotations.push_back({annotation, std::move(imageData)});
    restoreCrosshairInspection();
}

void OrthogonalViewer::removeAnnotation(const std::size_t index)
{
    if(index >= impl_->annotations.size())
    {
        throw std::out_of_range("Annotation index is out of range");
    }
    setAnnotationSelection({});
    for(auto& panel : impl_->panels)
    {
        panel.viewport->removeAnnotation(index);
    }
    impl_->annotations.erase(
        impl_->annotations.begin() + static_cast<std::ptrdiff_t>(index));
    impl_->labelOpacityDragOrigin.reset();
    if(impl_->annotations.empty())
    {
        setOverallLabelOpacity(1.0);
    }
    restoreCrosshairInspection();
    impl_->annotationEditor.clearAnnotation();
    impl_->editableAnnotationIndex.reset();
    const bool hasLabelMap = std::ranges::any_of(
        impl_->annotations, [](const Impl::AnnotationData& item) {
            return item.annotation->kind() == core::AnnotationKind::LabelMap;
        });
    if(!hasLabelMap)
    {
        applySliceAlignment(core::SliceAlignment::Patient);
    }
    publishAnnotationEditingState();
}

void OrthogonalViewer::setAnnotationOpacity(
    const std::size_t index,
    const double opacity)
{
    if(index >= impl_->annotations.size())
    {
        throw std::out_of_range("Annotation index is out of range");
    }
    for(auto& panel : impl_->panels)
    {
        panel.viewport->setAnnotationOpacity(
            index, opacity * impl_->overallLabelOpacity);
    }
    if(impl_->comparisonIndices
       && (index == (*impl_->comparisonIndices)[0]
           || index == (*impl_->comparisonIndices)[1]))
    {
        const double comparisonOpacity = std::max(
            impl_->annotations[(*impl_->comparisonIndices)[0]]
                .annotation->opacity(),
            impl_->annotations[(*impl_->comparisonIndices)[1]]
                .annotation->opacity())
            * impl_->overallLabelOpacity;
        for(auto& panel : impl_->panels)
        {
            panel.viewport->setAnnotationComparisonOpacity(comparisonOpacity);
        }
    }
}

void OrthogonalViewer::setAnnotationVisibility(
    const std::size_t index,
    const bool visible)
{
    if(index >= impl_->annotations.size())
    {
        throw std::out_of_range("Annotation index is out of range");
    }
    for(auto& panel : impl_->panels)
    {
        panel.viewport->setAnnotationVisibility(index, visible);
    }
    if(impl_->comparisonIndices
       && (index == (*impl_->comparisonIndices)[0]
           || index == (*impl_->comparisonIndices)[1]))
    {
        const bool comparisonVisible =
            impl_->annotations[(*impl_->comparisonIndices)[0]]
                .annotation->isVisible()
            && impl_->annotations[(*impl_->comparisonIndices)[1]]
                   .annotation->isVisible();
        for(auto& panel : impl_->panels)
        {
            panel.viewport->setAnnotationComparisonVisibility(comparisonVisible);
        }
    }
}

void OrthogonalViewer::setAnnotationHiddenIndicatorVisible(const bool visible)
{
    impl_->panels[0].viewport->setAnnotationHiddenIndicatorVisible(visible);
}

void OrthogonalViewer::setOverallLabelOpacity(const double opacity)
{
    impl_->overallLabelOpacity = std::clamp(opacity, 0.0, 1.0);
    for(std::size_t index = 0; index < impl_->annotations.size(); ++index)
    {
        const double displayedOpacity =
            impl_->annotations[index].annotation->opacity()
            * impl_->overallLabelOpacity;
        for(auto& panel : impl_->panels)
        {
            panel.viewport->setAnnotationOpacity(index, displayedOpacity);
        }
    }
    if(impl_->comparisonIndices)
    {
        const double comparisonOpacity = std::max(
            impl_->annotations[(*impl_->comparisonIndices)[0]]
                .annotation->opacity(),
            impl_->annotations[(*impl_->comparisonIndices)[1]]
                .annotation->opacity())
            * impl_->overallLabelOpacity;
        for(auto& panel : impl_->panels)
        {
            panel.viewport->setAnnotationComparisonOpacity(comparisonOpacity);
        }
    }
    emit overallLabelOpacityChanged(impl_->overallLabelOpacity);
}

void OrthogonalViewer::setAnnotationSelection(
    const std::vector<std::size_t>& selectedIndices)
{
    if(selectedIndices.size() != 2)
    {
        impl_->selectedAnnotationIndices.clear();
        if(selectedIndices.size() == 1
           && selectedIndices.front() < impl_->annotations.size())
        {
            impl_->selectedAnnotationIndices = selectedIndices;
        }
        for(auto& panel : impl_->panels)
        {
            panel.viewport->clearAnnotationComparison();
        }
        impl_->annotationComparison = nullptr;
        impl_->comparisonIndices.reset();
        impl_->annotationEditor.clearAnnotation();
        impl_->editableAnnotationIndex.reset();
        if(selectedIndices.size() == 1
           && selectedIndices[0] < impl_->annotations.size()
           && impl_->annotations[selectedIndices[0]].annotation->kind()
               == core::AnnotationKind::LabelMap)
        {
            impl_->editableAnnotationIndex = selectedIndices[0];
            impl_->annotationEditor.setAnnotation(
                impl_->annotations[selectedIndices[0]].annotation,
                impl_->sliceAlignment);
        }
        restoreCrosshairInspection();
        publishAnnotationLabels();
        publishAnnotationEditingState();
        return;
    }

    std::array<std::size_t, 2> indices{
        selectedIndices[0], selectedIndices[1]};
    std::sort(indices.begin(), indices.end());
    if(indices[0] == indices[1] || indices[1] >= impl_->annotations.size())
    {
        throw std::out_of_range("Annotation comparison selection is invalid");
    }
    impl_->selectedAnnotationIndices.assign(indices.begin(), indices.end());
    if(impl_->comparisonIndices && *impl_->comparisonIndices == indices)
    {
        return;
    }

    auto comparison = ItkVtkImageBridge::copyComparisonToVtk(
        impl_->annotations[indices[0]].annotation->volume(),
        impl_->annotations[indices[1]].annotation->volume());
    const double opacity = std::max(
        impl_->annotations[indices[0]].annotation->opacity(),
        impl_->annotations[indices[1]].annotation->opacity())
        * impl_->overallLabelOpacity;
    try
    {
        for(auto& panel : impl_->panels)
        {
            panel.viewport->setAnnotationComparison(comparison, opacity);
            panel.viewport->setAnnotationComparisonVisibility(
                impl_->annotations[indices[0]].annotation->isVisible()
                && impl_->annotations[indices[1]].annotation->isVisible());
        }
    }
    catch(...)
    {
        for(auto& panel : impl_->panels)
        {
            panel.viewport->clearAnnotationComparison();
        }
        impl_->annotationComparison = nullptr;
        impl_->comparisonIndices.reset();
        throw;
    }
    impl_->annotationComparison = comparison;
    impl_->comparisonIndices = indices;
    impl_->annotationEditor.clearAnnotation();
    impl_->editableAnnotationIndex.reset();
    restoreCrosshairInspection();
    publishAnnotationLabels();
    publishAnnotationEditingState();
}

QImage OrthogonalViewer::captureMiddleSliceThumbnail()
{
    if(!impl_->volume || !impl_->state)
    {
        return {};
    }
    return renderAxialThumbnail(*impl_->volume, *impl_->state);
}

void OrthogonalViewer::setCrosshairTool()
{
    impl_->interactionMode = VtkViewport::InteractionMode::Crosshair;
    for(auto& panel : impl_->panels)
    {
        panel.viewport->setInteractionMode(impl_->interactionMode);
    }
}

void OrthogonalViewer::setZoomTool()
{
    impl_->interactionMode = VtkViewport::InteractionMode::Zoom;
    for(auto& panel : impl_->panels)
    {
        panel.viewport->setInteractionMode(impl_->interactionMode);
    }
}

void OrthogonalViewer::setPanTool()
{
    impl_->interactionMode = VtkViewport::InteractionMode::Pan;
    for(auto& panel : impl_->panels)
    {
        panel.viewport->setInteractionMode(impl_->interactionMode);
    }
}

void OrthogonalViewer::setContrastTool()
{
    impl_->interactionMode = VtkViewport::InteractionMode::Contrast;
    for(auto& panel : impl_->panels)
    {
        panel.viewport->setInteractionMode(impl_->interactionMode);
    }
}

void OrthogonalViewer::setMeasureTool()
{
    impl_->interactionMode = VtkViewport::InteractionMode::Measure;
    for(auto& panel : impl_->panels)
    {
        panel.viewport->setInteractionMode(impl_->interactionMode);
    }
}

void OrthogonalViewer::setBrushTool()
{
    impl_->interactionMode = VtkViewport::InteractionMode::Brush;
    for(auto& panel : impl_->panels)
    {
        panel.viewport->setInteractionMode(
            panel.orientation == core::SliceOrientation::Axial
                ? VtkViewport::InteractionMode::Brush
                : VtkViewport::InteractionMode::Crosshair);
    }
}

void OrthogonalViewer::setEraseTool()
{
    impl_->interactionMode = VtkViewport::InteractionMode::Erase;
    for(auto& panel : impl_->panels)
    {
        panel.viewport->setInteractionMode(
            panel.orientation == core::SliceOrientation::Axial
                ? VtkViewport::InteractionMode::Erase
                : VtkViewport::InteractionMode::Crosshair);
    }
}

void OrthogonalViewer::setScopedEraseTool()
{
    impl_->interactionMode = VtkViewport::InteractionMode::ScopedErase;
    for(auto& panel : impl_->panels)
    {
        panel.viewport->setInteractionMode(
            panel.orientation == core::SliceOrientation::Axial
                ? VtkViewport::InteractionMode::ScopedErase
                : VtkViewport::InteractionMode::Crosshair);
    }
}

void OrthogonalViewer::setActiveLabel(const int label)
{
    if(label < 1 || label > 65535)
    {
        throw std::invalid_argument("Active label must be from 1 to 65535");
    }
    impl_->annotationEditor.setActiveLabel(static_cast<std::uint16_t>(label));
    for(auto& panel : impl_->panels)
    {
        panel.viewport->setBrushLabel(label);
    }
}

void OrthogonalViewer::setBrushRadius(const int radius)
{
    impl_->annotationEditor.setBrushRadius(radius);
    for(auto& panel : impl_->panels)
    {
        panel.viewport->setBrushRadius(radius);
    }
}

void OrthogonalViewer::setBrushShape(const core::BrushShape shape)
{
    impl_->annotationEditor.setBrushShape(shape);
    const bool circular = shape == core::BrushShape::Circle;
    for(auto& panel : impl_->panels)
    {
        panel.viewport->setBrushShape(circular);
    }
}

void OrthogonalViewer::setPaintOver(const int selection)
{
    if(selection == -1)
    {
        impl_->annotationEditor.setPaintOver(
            core::PaintOverMode::AllLabels);
    }
    else if(selection >= 0 && selection <= 65535)
    {
        impl_->annotationEditor.setPaintOver(
            core::PaintOverMode::OneLabel,
            static_cast<std::uint16_t>(selection));
    }
    else
    {
        throw std::invalid_argument("Paint-over selection is invalid");
    }
    const int eraseTarget = selection == 0 ? -1 : selection;
    for(auto& panel : impl_->panels)
    {
        panel.viewport->setEraseTargetLabel(eraseTarget);
    }
}

void OrthogonalViewer::beginEditStroke(
    const double x, const double y, const double z)
{
    if(!impl_->editableAnnotationIndex)
    {
        return;
    }
    if(impl_->interactionMode == VtkViewport::InteractionMode::ScopedErase)
    {
        if(impl_->annotationEditor.eraseConnectedComponentOnSlice({{x, y, z}}))
        {
            refreshEditedAnnotation();
            publishAnnotationLabels();
        }
        publishAnnotationEditingState();
        return;
    }
    impl_->annotationEditor.beginStroke(
        impl_->interactionMode == VtkViewport::InteractionMode::Erase);
    continueEditStroke(x, y, z);
}

void OrthogonalViewer::continueEditStroke(
    const double x, const double y, const double z)
{
    if(impl_->annotationEditor.stamp({{x, y, z}}))
    {
        refreshEditedAnnotation();
    }
}

void OrthogonalViewer::finishEditStroke()
{
    static_cast<void>(impl_->annotationEditor.endStroke());
    publishAnnotationLabels();
    publishAnnotationEditingState();
    restoreCrosshairInspection();
}

void OrthogonalViewer::undoAnnotationEdit()
{
    if(impl_->annotationEditor.undo())
    {
        refreshEditedAnnotation();
        publishAnnotationLabels();
        restoreCrosshairInspection();
    }
    publishAnnotationEditingState();
}

void OrthogonalViewer::redoAnnotationEdit()
{
    if(impl_->annotationEditor.redo())
    {
        refreshEditedAnnotation();
        publishAnnotationLabels();
        restoreCrosshairInspection();
    }
    publishAnnotationEditingState();
}

void OrthogonalViewer::refreshEditedAnnotation()
{
    if(!impl_->editableAnnotationIndex) return;
    const std::size_t index = *impl_->editableAnnotationIndex;
    auto& annotationData = impl_->annotations[index];
    annotationData.imageData->Modified();
    for(auto& panel : impl_->panels)
    {
        panel.viewport->annotationDataModified(index);
    }
}

void OrthogonalViewer::publishAnnotationEditingState()
{
    emit annotationEditingStateChanged(
        impl_->editableAnnotationIndex.has_value(),
        impl_->annotationEditor.canUndo(),
        impl_->annotationEditor.canRedo());
}

void OrthogonalViewer::publishAnnotationLabels()
{
    QList<int> labels;
    const auto& annotation = impl_->annotationEditor.annotation();
    if(annotation)
    {
        const auto values = annotation->labelValues();
        labels.reserve(static_cast<qsizetype>(values.size()));
        for(const std::uint16_t value : values)
        {
            labels.push_back(static_cast<int>(value));
        }
    }
    emit annotationLabelsChanged(labels);
}

void OrthogonalViewer::zoomAllIn()
{
    for(auto& panel : impl_->panels)
    {
        panel.viewport->zoomIn();
    }
}

void OrthogonalViewer::zoomAllOut()
{
    for(auto& panel : impl_->panels)
    {
        panel.viewport->zoomOut();
    }
}

void OrthogonalViewer::stepActiveViewSlice(const int steps)
{
    stepSlice(
        impl_->focusedView.value_or(impl_->inspectionOrientation), steps);
}

void OrthogonalViewer::panActiveView(
    const double horizontalDirection,
    const double verticalDirection)
{
    const auto orientation =
        impl_->focusedView.value_or(impl_->inspectionOrientation);
    const auto panel = std::find_if(
        impl_->panels.begin(),
        impl_->panels.end(),
        [orientation](const SlicePanel& candidate) {
            return candidate.orientation == orientation;
        });
    if(panel != impl_->panels.end())
    {
        panel->viewport->panBy(horizontalDirection, verticalDirection);
    }
}

void OrthogonalViewer::resetActiveView()
{
    const auto orientation =
        impl_->focusedView.value_or(impl_->inspectionOrientation);
    const auto panel = std::find_if(
        impl_->panels.begin(),
        impl_->panels.end(),
        [orientation](const SlicePanel& candidate) {
            return candidate.orientation == orientation;
        });
    if(panel != impl_->panels.end())
    {
        panel->viewport->resetView();
    }
}

void OrthogonalViewer::resetAllViews()
{
    for(auto& panel : impl_->panels)
    {
        panel.viewport->resetView();
    }
}

void OrthogonalViewer::showAllViews()
{
    impl_->focusedView.reset();
    impl_->rightViews->show();
    for(auto& panel : impl_->panels)
    {
        panel.panelWidget->show();
        panel.expandButton->setChecked(false);
        setFocusButtonAppearance(panel, false);
    }
    if(!impl_->rightViewSizes.isEmpty())
    {
        impl_->rightViews->setSizes(impl_->rightViewSizes);
    }
    if(!impl_->viewSizes.isEmpty())
    {
        impl_->viewSplitter->setSizes(impl_->viewSizes);
    }
}

void OrthogonalViewer::focusAxialView()
{
    toggleFocusedView(core::SliceOrientation::Axial);
}

void OrthogonalViewer::focusSagittalView()
{
    toggleFocusedView(core::SliceOrientation::Sagittal);
}

void OrthogonalViewer::focusCoronalView()
{
    toggleFocusedView(core::SliceOrientation::Coronal);
}

void OrthogonalViewer::goToAxialSlice(const int sliceNumber)
{
    if(!impl_->state || sliceNumber < 1)
    {
        return;
    }
    const auto& geometry = impl_->state->geometry();
    const auto& dimensions = geometry.dimensions();
    if(static_cast<std::size_t>(sliceNumber) > dimensions[2])
    {
        return;
    }

    try
    {
        auto index = impl_->state->cursorContinuousIndex();
        index[2] = static_cast<double>(sliceNumber - 1);
        impl_->inspectionOrientation = core::SliceOrientation::Axial;
        impl_->state->setCursorPhysical(geometry.indexToPhysical(index));
        publishCursor();
    }
    catch(const std::exception& exception)
    {
        qWarning().noquote()
            << "[RENDER] Axial slice navigation failed:" << exception.what();
    }
}

void OrthogonalViewer::toggleFocusedView(const core::SliceOrientation orientation)
{
    if(impl_->focusedView && *impl_->focusedView == orientation)
    {
        showAllViews();
        return;
    }

    if(!impl_->focusedView)
    {
        impl_->rightViewSizes = impl_->rightViews->sizes();
        impl_->viewSizes = impl_->viewSplitter->sizes();
    }
    impl_->focusedView = orientation;
    const bool axial = orientation == core::SliceOrientation::Axial;
    impl_->rightViews->setVisible(!axial);
    for(auto& panel : impl_->panels)
    {
        const bool selected = panel.orientation == orientation;
        panel.panelWidget->setVisible(selected);
        panel.expandButton->setChecked(selected);
        setFocusButtonAppearance(panel, true);
    }
}

void OrthogonalViewer::saveScreenshot(const core::SliceOrientation orientation)
{
    const auto panel = std::find_if(
        impl_->panels.begin(),
        impl_->panels.end(),
        [orientation](const SlicePanel& candidate) {
            return candidate.orientation == orientation;
        });
    if(panel == impl_->panels.end() || !impl_->volume)
    {
        return;
    }

    const QString suggestedName = QDir(
        QStandardPaths::writableLocation(QStandardPaths::PicturesLocation))
                                      .filePath(
                                          orientationTitle(orientation).toLower()
                                          + QStringLiteral("-slice.png"));
    const QString fileName = QFileDialog::getSaveFileName(
        this,
        tr("Save Slice Screenshot"),
        suggestedName,
        tr("PNG image (*.png);;JPEG image (*.jpg *.jpeg);;Bitmap image (*.bmp)"));
    if(fileName.isEmpty())
    {
        return;
    }

    const QImage image = panel->viewport->captureImageWithoutCrosshair();
    if(image.isNull() || !image.save(fileName))
    {
        QMessageBox::warning(
            this,
            tr("Unable to Save Screenshot"),
            tr("The slice screenshot could not be written to the selected file."));
    }
}

void OrthogonalViewer::goToNearestAxialSliceContainingLabel(const int label)
{
    const auto& annotation = impl_->annotationEditor.annotation();
    auto& axialPanel = impl_->panels[0];
    if(!impl_->state || !annotation || label < 1 || label > 65535
       || !axialPanel.geometry)
    {
        return;
    }
    const auto target = annotation->nearestAxialSlicePointContainingLabel(
        static_cast<std::uint16_t>(label),
        impl_->state->cursorPhysical(),
        impl_->sliceAlignment);
    if(!target)
    {
        return;
    }

    const auto& geometry = *axialPanel.geometry;
    const double normalRange =
        geometry.normalMaximum() - geometry.normalMinimum();
    if(normalRange <= 0.0)
    {
        return;
    }
    const double fraction = std::clamp(
        (geometry.normalCoordinate(*target) - geometry.normalMinimum())
            / normalRange,
        0.0,
        1.0);
    try
    {
        impl_->inspectionOrientation = core::SliceOrientation::Axial;
        impl_->state->setCursorNormalFraction(geometry, fraction);
        publishCursor();
    }
    catch(const std::exception& exception)
    {
        qWarning().noquote()
            << "[RENDER] Label slice navigation failed:" << exception.what();
    }
}

void OrthogonalViewer::exportSliceAnimation()
{
    if(!impl_->state || !impl_->volume)
    {
        return;
    }

    std::array<ui::GifSliceRange, 3> ranges{};
    for(std::size_t index = 0; index < impl_->panels.size(); ++index)
    {
        const auto& panel = impl_->panels[index];
        if(!panel.geometry)
        {
            return;
        }
        ranges[index] = {
            panel.orientation,
            panel.geometry->sliceStep(),
            panel.sliceScrollBar->value(),
            panel.sliceScrollBar->maximum() - panel.sliceScrollBar->value(),
        };
    }

    const core::SliceOrientation initialOrientation =
        impl_->focusedView.value_or(impl_->inspectionOrientation);
    ui::GifExportDialog dialog(ranges, initialOrientation, this);
    if(dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    const bool mp4 = dialog.format() == ui::AnimationFormat::Mp4;
    const QString extension = mp4
        ? QStringLiteral(".mp4")
        : QStringLiteral(".gif");
    QString baseName = orientationTitle(dialog.orientation()).toLower()
        + QStringLiteral("-slices");
    const core::Annotation* namedAnnotation = nullptr;
    if(!impl_->selectedAnnotationIndices.empty())
    {
        namedAnnotation = impl_->annotations[
            impl_->selectedAnnotationIndices.front()].annotation.get();
    }
    else if(!impl_->annotations.empty())
    {
        namedAnnotation = impl_->annotations.front().annotation.get();
    }
    if(namedAnnotation != nullptr)
    {
        baseName = QString::fromUtf8(
            namedAnnotation->name().data(),
            static_cast<qsizetype>(namedAnnotation->name().size()));
    }
    const QString suggestedName = QDir(
        QStandardPaths::writableLocation(QStandardPaths::PicturesLocation))
                                      .filePath(baseName + extension);
    QString fileName = QFileDialog::getSaveFileName(
        this,
        tr("Save Slice Animation"),
        suggestedName,
        mp4 ? tr("MP4 video (*.mp4)") : tr("Animated GIF (*.gif)"));
    if(fileName.isEmpty())
    {
        return;
    }
    if(QFileInfo(fileName).suffix().isEmpty())
    {
        fileName += extension;
    }

    auto panel = std::find_if(
        impl_->panels.begin(),
        impl_->panels.end(),
        [&dialog](const SlicePanel& candidate) {
            return candidate.orientation == dialog.orientation();
        });
    if(panel == impl_->panels.end() || !panel->geometry)
    {
        return;
    }

    const LayoutState originalLayout = layoutState();
    const bool temporarilyFocused = !panel->panelWidget->isVisible();
    if(temporarilyFocused)
    {
        toggleFocusedView(panel->orientation);
        QCoreApplication::processEvents();
    }

    std::vector<int> positions;
    const int currentPosition = panel->sliceScrollBar->value();
    const int firstPosition = currentPosition - dialog.slicesBefore();
    const int lastPosition = currentPosition + dialog.slicesAfter();
    positions.reserve(static_cast<std::size_t>(
        (lastPosition - firstPosition + 1) * (dialog.pingPong() ? 2 : 1)));
    for(int position = firstPosition; position <= lastPosition; ++position)
    {
        positions.push_back(position);
    }
    if(dialog.pingPong() && positions.size() > 2)
    {
        for(int position = lastPosition - 1; position > firstPosition; --position)
        {
            positions.push_back(position);
        }
    }

    QProgressDialog progress(
        tr("Recording slice animation…"),
        tr("Cancel"),
        0,
        static_cast<int>(positions.size()),
        this);
    progress.setWindowTitle(tr("Save Slice Animation"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setValue(0);

    const auto originalCursor = impl_->state->cursorPhysical();
    const auto originalInspectionOrientation = impl_->inspectionOrientation;
    std::unique_ptr<io::AnimationWriter> writer;
    QString exportError;
    bool canceled = false;
    const int frameDelayMilliseconds = dialog.frameDelayMilliseconds();
    const int maximumPosition = panel->sliceScrollBar->maximum();

    for(std::size_t frameIndex = 0; frameIndex < positions.size(); ++frameIndex)
    {
        QCoreApplication::processEvents();
        if(progress.wasCanceled())
        {
            canceled = true;
            break;
        }

        const double fraction = maximumPosition > 0
            ? static_cast<double>(positions[frameIndex]) / maximumPosition
            : 0.0;
        impl_->state->setCursorNormalFraction(*panel->geometry, fraction);
        impl_->inspectionOrientation = panel->orientation;
        publishCursor();

        const QImage frame = panel->viewport->captureSliceImage(
            dialog.showCrosshair(), dialog.respectZoom());
        if(frame.isNull())
        {
            exportError = tr("A slice frame could not be captured.");
            break;
        }
        if(!writer)
        {
            if(mp4)
            {
                writer = std::make_unique<io::Mp4Writer>(
                    fileName,
                    frame.width(),
                    frame.height(),
                    frameDelayMilliseconds);
            }
            else
            {
                writer = std::make_unique<io::AnimatedGifWriter>(
                    fileName, frame.width(), frame.height());
            }
            if(!writer->isOpen())
            {
                exportError = writer->errorString();
                break;
            }
        }
        if(!writer->writeFrame(frame, frameDelayMilliseconds))
        {
            exportError = writer->errorString();
            break;
        }
        progress.setValue(static_cast<int>(frameIndex + 1));
    }

    impl_->state->setCursorPhysical(originalCursor);
    impl_->inspectionOrientation = originalInspectionOrientation;
    publishCursor();
    if(temporarilyFocused)
    {
        setLayoutState(originalLayout);
    }

    if(!canceled && exportError.isEmpty()
       && (!writer || !writer->finish()))
    {
        exportError = writer
            ? writer->errorString()
            : tr("No animation frames were recorded.");
    }
    if(canceled)
    {
        return;
    }
    if(!exportError.isEmpty())
    {
        QMessageBox::warning(
            this,
            tr("Unable to Save Animation"),
            tr("The slice animation could not be written.\n\n%1")
                .arg(exportError));
        return;
    }

    QMessageBox::information(
        this,
        tr("Animation Saved"),
        tr("The slice animation was saved to:\n%1")
            .arg(QDir::toNativeSeparators(fileName)));
}

void OrthogonalViewer::setWindowLevel(const double window, const double level)
{
    if(!impl_->state)
    {
        return;
    }

    try
    {
        impl_->state->setWindowLevel(window, level);
        impl_->windowLevelDragOrigin.reset();
        publishWindowLevel();
    }
    catch(const std::exception& exception)
    {
        qWarning().noquote()
            << "[RENDER] Window/level update failed:" << exception.what();
    }
}

void OrthogonalViewer::applyAutomaticWindowLevel()
{
    if(!impl_->state || !impl_->volume)
    {
        return;
    }

    try
    {
        const auto range = impl_->volume->robustScalarRange();
        const auto automatic =
            core::WindowLevel::fromIntensityRange(range.minimum, range.maximum);
        impl_->state->setWindowLevel(automatic.window(), automatic.level());
        impl_->windowLevelDragOrigin.reset();
        qInfo().noquote()
            << QStringLiteral("[RENDER] window = %1  level = %2  (auto)")
                   .arg(automatic.window())
                   .arg(automatic.level());
        publishWindowLevel();
    }
    catch(const std::exception& exception)
    {
        qWarning().noquote()
            << "[RENDER] Automatic window/level failed:" << exception.what();
    }
}

void OrthogonalViewer::applyWindowLevelPreset(const int preset)
{
    if(!impl_->state)
    {
        return;
    }

    try
    {
        impl_->state->applyWindowLevelPreset(
            static_cast<core::WindowLevelPreset>(preset));
        impl_->windowLevelDragOrigin.reset();
        const auto& windowLevel = impl_->state->windowLevel();
        qInfo().noquote()
            << QStringLiteral("[RENDER] window = %1  level = %2  (preset)")
                   .arg(windowLevel.window())
                   .arg(windowLevel.level());
        publishWindowLevel();
    }
    catch(const std::exception& exception)
    {
        qWarning().noquote()
            << "[RENDER] Window/level preset failed:" << exception.what();
    }
}

void OrthogonalViewer::resetWindowLevel()
{
    if(!impl_->state)
    {
        return;
    }

    impl_->state->resetWindowLevel();
    impl_->windowLevelDragOrigin.reset();
    const auto& windowLevel = impl_->state->windowLevel();
    qInfo().noquote()
        << QStringLiteral("[RENDER] window = %1  level = %2  (reset)")
               .arg(windowLevel.window())
               .arg(windowLevel.level());
    publishWindowLevel();
}

void OrthogonalViewer::setInverted(const bool inverted)
{
    if(!impl_->state)
    {
        return;
    }
    impl_->state->setInverted(inverted);
    for(auto& panel : impl_->panels)
    {
        panel.viewport->setInverted(inverted);
    }
}

void OrthogonalViewer::setVolume(
    const std::shared_ptr<const core::Volume>& volume)
{
    if(!volume)
    {
        throw std::invalid_argument("Orthogonal viewer volume cannot be null");
    }

    auto imageData = ItkVtkImageBridge::shareWithVtk(*volume);
    core::ViewerState state(volume->geometry());
    const auto range = volume->scalarRange();
    state.setIntensityRange(range.minimum, range.maximum);
    const auto cursor = state.cursorPhysical();
    const auto windowLevel = state.windowLevel();
    constexpr auto alignment = core::SliceAlignment::Patient;
    std::array<core::OrthogonalSliceGeometry, 3> geometries{
        core::OrthogonalSliceGeometry::fromImageGeometry(
            volume->geometry(), core::SliceOrientation::Axial, alignment),
        core::OrthogonalSliceGeometry::fromImageGeometry(
            volume->geometry(), core::SliceOrientation::Sagittal, alignment),
        core::OrthogonalSliceGeometry::fromImageGeometry(
            volume->geometry(), core::SliceOrientation::Coronal, alignment),
    };

    for(auto& panel : impl_->panels)
    {
        panel.viewport->setWindowLevel(windowLevel.window(), windowLevel.level());
        panel.viewport->setInverted(state.inverted());
        panel.viewport->setInput(
            imageData, volume->geometry(), cursor, alignment);
        panel.expandButton->setEnabled(true);
        panel.centerButton->setEnabled(true);
        panel.screenshotButton->setEnabled(true);
    }

    impl_->imageData = imageData;
    impl_->volume = volume;
    impl_->state = state;
    impl_->sliceAlignment = alignment;
    impl_->windowLevelDragOrigin.reset();
    for(std::size_t index = 0; index < impl_->panels.size(); ++index)
    {
        impl_->panels[index].geometry = geometries[index];
        const auto& slice = *impl_->panels[index].geometry;
        const double sliceCount = std::round(
            (slice.normalMaximum() - slice.normalMinimum()) / slice.sliceStep());
        const int maximum = static_cast<int>(std::clamp(
            sliceCount,
            1.0,
            static_cast<double>(std::numeric_limits<int>::max())));
        const QSignalBlocker blocker(impl_->panels[index].sliceScrollBar);
        impl_->panels[index].sliceScrollBar->setRange(0, maximum);
        impl_->panels[index].sliceScrollBar->setSingleStep(1);
        impl_->panels[index].sliceScrollBar->setPageStep(10);
        impl_->panels[index].sliceScrollBar->setEnabled(true);
    }
    publishCursor();
    qInfo().noquote()
        << QStringLiteral(
               "[RENDER] window = %1  level = %2  (range %3 .. %4)")
               .arg(windowLevel.window())
               .arg(windowLevel.level())
               .arg(windowLevel.intensityMinimum())
               .arg(windowLevel.intensityMaximum());
    publishWindowLevel();
}

void OrthogonalViewer::applySliceAlignment(
    const core::SliceAlignment alignment)
{
    if(!impl_->volume || !impl_->state || impl_->sliceAlignment == alignment)
    {
        return;
    }

    const auto& geometry = impl_->volume->geometry();
    for(auto& panel : impl_->panels)
    {
        auto slice = core::OrthogonalSliceGeometry::fromImageGeometry(
            geometry, panel.orientation, alignment);
        panel.viewport->setSliceAlignment(alignment);
        panel.geometry = std::move(slice);

        const double intervals = std::round(
            (panel.geometry->normalMaximum()
             - panel.geometry->normalMinimum())
            / panel.geometry->sliceStep());
        const int maximum = static_cast<int>(std::clamp(
            intervals,
            1.0,
            static_cast<double>(std::numeric_limits<int>::max())));
        const QSignalBlocker blocker(panel.sliceScrollBar);
        panel.sliceScrollBar->setRange(0, maximum);
        panel.sliceScrollBar->setSingleStep(1);
        panel.sliceScrollBar->setPageStep(10);
    }
    impl_->sliceAlignment = alignment;
    publishCursor();
}

void OrthogonalViewer::selectPhysicalPoint(
    const double x,
    const double y,
    const double z)
{
    if(!impl_->state)
    {
        return;
    }

    try
    {
        impl_->state->setCursorPhysical({{x, y, z}});
        publishCursor();
    }
    catch(const std::exception& exception)
    {
        qWarning().noquote()
            << "[RENDER] Cursor update failed:" << exception.what();
    }
}

void OrthogonalViewer::stepSlice(
    const core::SliceOrientation orientation,
    const int steps)
{
    if(!impl_->state || steps == 0)
    {
        return;
    }
    const auto panel = std::find_if(
        impl_->panels.begin(),
        impl_->panels.end(),
        [orientation](const SlicePanel& candidate) {
            return candidate.orientation == orientation;
        });
    if(panel == impl_->panels.end() || !panel->geometry)
    {
        return;
    }

    try
    {
        impl_->inspectionOrientation = orientation;
        impl_->state->stepCursor(*panel->geometry, steps);
        publishCursor();
    }
    catch(const std::exception& exception)
    {
        qWarning().noquote()
            << "[RENDER] Slice step failed:" << exception.what();
    }
}

void OrthogonalViewer::selectNormalPosition(
    const core::SliceOrientation orientation,
    const int position)
{
    if(!impl_->state)
    {
        return;
    }
    const auto panel = std::find_if(
        impl_->panels.begin(),
        impl_->panels.end(),
        [orientation](const SlicePanel& candidate) {
            return candidate.orientation == orientation;
        });
    if(panel == impl_->panels.end() || !panel->geometry
       || panel->sliceScrollBar->maximum() <= 0)
    {
        return;
    }

    const double fraction = static_cast<double>(position)
        / panel->sliceScrollBar->maximum();
    try
    {
        impl_->inspectionOrientation = orientation;
        impl_->state->setCursorNormalFraction(*panel->geometry, fraction);
        publishCursor();
    }
    catch(const std::exception& exception)
    {
        qWarning().noquote()
            << "[RENDER] Slice position failed:" << exception.what();
    }
}

void OrthogonalViewer::publishCursor()
{
    if(!impl_->state)
    {
        return;
    }
    const bool profiling = renderProfilingEnabled();
    QElapsedTimer profileTimer;
    if(profiling)
    {
        profileTimer.start();
    }
    std::array<qint64, 3> panelMicroseconds{};
    const auto& physical = impl_->state->cursorPhysical();
    const auto index = impl_->state->cursorContinuousIndex();
    const int axialSliceCount = totalAxialSlices(impl_->state->geometry());
    const int axialSlice = currentAxialSlice(*impl_->state);
    for(std::size_t panelIndex = 0;
        panelIndex < impl_->panels.size(); ++panelIndex)
    {
        auto& panel = impl_->panels[panelIndex];
        const qint64 panelStart = profiling
            ? profileTimer.nsecsElapsed() / 1000 : 0;
        if(panel.geometry && panel.sliceScrollBar->maximum() > 0)
        {
            const double fraction =
                impl_->state->cursorNormalFraction(*panel.geometry);
            const int position = static_cast<int>(std::llround(
                std::clamp(fraction, 0.0, 1.0)
                * panel.sliceScrollBar->maximum()));
            const QSignalBlocker blocker(panel.sliceScrollBar);
            panel.sliceScrollBar->setValue(position);
        }
        if(panel.orientation == core::SliceOrientation::Axial)
        {
            panel.viewport->setSliceCounter(axialSlice, axialSliceCount);
        }
        panel.viewport->setCursor(physical);
        if(profiling)
        {
            panelMicroseconds[panelIndex] =
                profileTimer.nsecsElapsed() / 1000 - panelStart;
        }
    }

    if(profiling)
    {
        qInfo().noquote()
            << QStringLiteral(
                   "[PROFILE] cursor physical=%1,%2,%3 index=%4,%5,%6 "
                   "axial_us=%7 sagittal_us=%8 coronal_us=%9 panels_us=%10")
                   .arg(physical[0])
                   .arg(physical[1])
                   .arg(physical[2])
                   .arg(index[0])
                   .arg(index[1])
                   .arg(index[2])
                   .arg(panelMicroseconds[0])
                   .arg(panelMicroseconds[1])
                   .arg(panelMicroseconds[2])
                   .arg(profileTimer.nsecsElapsed() / 1000);
    }
    emit cursorChanged(
        physical[0], physical[1], physical[2], index[0], index[1], index[2]);
    inspectPhysicalPoint(
        physical[0], physical[1], physical[2], impl_->inspectionOrientation);
    if(profiling)
    {
        qInfo().noquote()
            << QStringLiteral("[PROFILE] cursor_complete total_us=%1")
                   .arg(profileTimer.nsecsElapsed() / 1000);
    }
}

void OrthogonalViewer::beginWindowLevelDrag()
{
    if(!impl_->state)
    {
        return;
    }
    impl_->windowLevelDragOrigin = impl_->state->windowLevel();
}

void OrthogonalViewer::updateWindowLevelDrag(
    const double normalizedDx,
    const double normalizedDy)
{
    if(!impl_->state || !impl_->windowLevelDragOrigin)
    {
        return;
    }

    try
    {
        const auto next = impl_->windowLevelDragOrigin->dragged(
            normalizedDx, normalizedDy);
        impl_->state->setWindowLevel(next.window(), next.level());
        publishWindowLevel();
    }
    catch(const std::exception& exception)
    {
        qWarning().noquote()
            << "[RENDER] Window/level drag failed:" << exception.what();
    }
}

void OrthogonalViewer::beginLabelOpacityDrag()
{
    if(impl_->annotations.empty())
    {
        impl_->labelOpacityDragOrigin.reset();
        return;
    }
    impl_->labelOpacityDragOrigin = impl_->overallLabelOpacity;
}

void OrthogonalViewer::updateLabelOpacityDrag(const double normalizedDy)
{
    if(!impl_->labelOpacityDragOrigin || impl_->annotations.empty())
    {
        return;
    }
    setOverallLabelOpacity(*impl_->labelOpacityDragOrigin + normalizedDy);
}

void OrthogonalViewer::publishWindowLevel()
{
    if(!impl_->state)
    {
        return;
    }
    const auto& windowLevel = impl_->state->windowLevel();
    for(auto& panel : impl_->panels)
    {
        panel.viewport->setWindowLevel(windowLevel.window(), windowLevel.level());
    }
    emit windowLevelChanged(
        windowLevel.window(),
        windowLevel.level(),
        windowLevel.intensityMinimum(),
        windowLevel.intensityMaximum());
}

} // namespace radmarky::rendering
