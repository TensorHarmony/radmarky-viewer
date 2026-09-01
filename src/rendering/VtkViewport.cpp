#include "rendering/VtkViewport.h"

#include "core/BrushGeometry.h"
#include "rendering/OrthogonalReslice.h"
#include "rendering/RenderProfiling.h"
#include "core/LabelPalette.h"
#include "core/PhysicalMeasurement.h"
#include "ui/UiTheme.h"

#include <QColor>
#include <QCursor>
#include <QDebug>
#include <QElapsedTimer>
#include <QEvent>
#include <QGuiApplication>
#include <QImage>
#include <QMouseEvent>
#include <QSize>
#include <QSizePolicy>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QVTKOpenGLNativeWidget.h>

#include <vtkActor.h>
#include <vtkCamera.h>
#include <vtkCellArray.h>
#include <vtkCoordinate.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkImageData.h>
#include <vtkImageProperty.h>
#include <vtkImageReslice.h>
#include <vtkImageSlice.h>
#include <vtkImageSliceMapper.h>
#include <vtkInteractorStyleUser.h>
#include <vtkLookupTable.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkPropCollection.h>
#include <vtkRenderer.h>
#include <vtkSmartPointer.h>
#include <vtkTextActor.h>
#include <vtkTextProperty.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace radmarky::rendering
{
namespace
{

enum class DragMode
{
    None,
    Cursor,
    Zoom,
    Pan,
    ZoomThumbnailPan,
    Contrast,
    LabelOpacity,
    Editing,
    ScopedErasing,
    Measuring,
};

const QColor kDarkCursorTint(QStringLiteral("#274957"));
const QColor kLightCursorTint(QStringLiteral("#e8f0f4"));

void setRulerShape(
    vtkPolyData& rulerData,
    const double startX,
    const double startY,
    const double endX,
    const double endY,
    const double depth,
    const double capHalfLength)
{
    auto points = vtkSmartPointer<vtkPoints>::New();
    auto lines = vtkSmartPointer<vtkCellArray>::New();
    const double deltaX = endX - startX;
    const double deltaY = endY - startY;
    const double length = std::hypot(deltaX, deltaY);
    const double perpendicularX = length > 1.0e-12 ? -deltaY / length : 0.0;
    const double perpendicularY = length > 1.0e-12 ? deltaX / length : 1.0;
    const auto addLine = [&](const double x1, const double y1,
                             const double x2, const double y2) {
        vtkIdType ids[2]{
            points->InsertNextPoint(x1, y1, depth),
            points->InsertNextPoint(x2, y2, depth),
        };
        lines->InsertNextCell(2, ids);
    };
    addLine(startX, startY, endX, endY);
    addLine(
        startX - perpendicularX * capHalfLength,
        startY - perpendicularY * capHalfLength,
        startX + perpendicularX * capHalfLength,
        startY + perpendicularY * capHalfLength);
    addLine(
        endX - perpendicularX * capHalfLength,
        endY - perpendicularY * capHalfLength,
        endX + perpendicularX * capHalfLength,
        endY + perpendicularY * capHalfLength);
    rulerData.SetPoints(points);
    rulerData.SetLines(lines);
    rulerData.Modified();
}

const char* orientationTitle(const core::SliceOrientation orientation)
{
    switch(orientation)
    {
    case core::SliceOrientation::Axial:
        return "AXIAL";
    case core::SliceOrientation::Sagittal:
        return "SAGITTAL";
    case core::SliceOrientation::Coronal:
        return "CORONAL";
    }
    return "";
}

void setCenteredDashedLine(
    vtkPolyData& lineData,
    const double startX,
    const double startY,
    const double endX,
    const double endY,
    const double centerX,
    const double centerY,
    const double depth,
    const double dashLength,
    const double gapLength)
{
    auto points = vtkSmartPointer<vtkPoints>::New();
    auto lines = vtkSmartPointer<vtkCellArray>::New();
    const double deltaX = endX - startX;
    const double deltaY = endY - startY;
    const double length = std::hypot(deltaX, deltaY);
    if(length > 0.0)
    {
        const double unitX = deltaX / length;
        const double unitY = deltaY / length;
        const double centerPosition = std::clamp(
            (centerX - startX) * unitX + (centerY - startY) * unitY,
            0.0,
            length);
        const double period = dashLength + gapLength;
        const double halfDash = dashLength / 2.0;
        const auto firstDash = static_cast<long long>(std::ceil(
            (-centerPosition - halfDash) / period));
        const auto lastDash = static_cast<long long>(std::floor(
            (length - centerPosition + halfDash) / period));
        for(long long dashIndex = firstDash; dashIndex <= lastDash; ++dashIndex)
        {
            const double position = centerPosition - halfDash
                + static_cast<double>(dashIndex) * period;
            const double dashStart = std::max(position, 0.0);
            const double dashEnd = std::min(position + dashLength, length);
            if(dashEnd <= dashStart)
            {
                continue;
            }
            vtkIdType pointIds[2]{
                points->InsertNextPoint(
                    startX + unitX * dashStart,
                    startY + unitY * dashStart,
                    depth),
                points->InsertNextPoint(
                    startX + unitX * dashEnd,
                    startY + unitY * dashEnd,
                    depth),
            };
            lines->InsertNextCell(2, pointIds);
        }
    }
    lineData.SetPoints(points);
    lineData.SetLines(lines);
    lineData.Modified();
}

void setRectangle(
    vtkPolyData& rectangleData,
    const double left,
    const double bottom,
    const double right,
    const double top,
    const double depth)
{
    auto points = vtkSmartPointer<vtkPoints>::New();
    vtkIdType pointIds[5]{
        points->InsertNextPoint(left, bottom, depth),
        points->InsertNextPoint(right, bottom, depth),
        points->InsertNextPoint(right, top, depth),
        points->InsertNextPoint(left, top, depth),
        0,
    };
    pointIds[4] = pointIds[0];
    auto lines = vtkSmartPointer<vtkCellArray>::New();
    lines->InsertNextCell(5, pointIds);
    rectangleData.SetPoints(points);
    rectangleData.SetLines(lines);
    rectangleData.Modified();
}

void setClosedOutline(
    vtkPolyData& outlineData,
    const std::vector<core::BrushOutlinePoint>& outline,
    const double depth)
{
    auto points = vtkSmartPointer<vtkPoints>::New();
    auto lines = vtkSmartPointer<vtkCellArray>::New();
    lines->InsertNextCell(static_cast<vtkIdType>(outline.size() + 1));
    for(const auto& point : outline)
    {
        const vtkIdType id =
            points->InsertNextPoint(point[0], point[1], depth);
        lines->InsertCellPoint(id);
    }
    lines->InsertCellPoint(0);
    outlineData.SetPoints(points);
    outlineData.SetLines(lines);
    outlineData.Modified();
}

vtkSmartPointer<vtkLookupTable> makeLabelLookupTable(const double)
{
    // Label 0 is transparent background and the default categorical palette
    // repeats for higher label values.
    // Editing may introduce any valid label after the pipeline is built.
    constexpr int maximumLabel = 65535;
    auto table = vtkSmartPointer<vtkLookupTable>::New();
    table->SetNumberOfTableValues(maximumLabel + 1);
    table->SetTableRange(0.0, static_cast<double>(maximumLabel));
    table->Build();
    table->SetTableValue(0, 0.0, 0.0, 0.0, 0.0);
    for(int label = 1; label <= maximumLabel; ++label)
    {
        const std::uint32_t color = core::defaultLabelColor(
            static_cast<std::uint16_t>(label));
        table->SetTableValue(
            label,
            static_cast<double>((color >> 16U) & 0xFFU) / 255.0,
            static_cast<double>((color >> 8U) & 0xFFU) / 255.0,
            static_cast<double>(color & 0xFFU) / 255.0,
            1.0);
    }
    return table;
}

vtkSmartPointer<vtkLookupTable> makeScalarLookupTable(
    double minimum,
    double maximum)
{
    if(maximum <= minimum)
    {
        minimum -= 0.5;
        maximum += 0.5;
    }
    auto table = vtkSmartPointer<vtkLookupTable>::New();
    table->SetNumberOfTableValues(256);
    table->SetTableRange(minimum, maximum);
    table->SetHueRange(2.0 / 3.0, 0.0);
    table->SetSaturationRange(1.0, 1.0);
    table->SetValueRange(1.0, 1.0);
    table->SetNanColor(0.0, 0.0, 0.0, 0.0);
    table->Build();
    return table;
}

} // namespace

struct AnnotationPipeline
{
    vtkSmartPointer<vtkImageData> imageData;
    vtkSmartPointer<vtkImageReslice> reslice;
    vtkSmartPointer<vtkImageSliceMapper> mapper;
    vtkSmartPointer<vtkImageSlice> imageSlice;
    vtkSmartPointer<vtkLookupTable> lookupTable;
    bool visible = true;
};

namespace
{

AnnotationPipeline makeAnnotationPipeline(
    vtkImageData* const imageData,
    const bool labelMap,
    const double scalarMinimum,
    const double scalarMaximum,
    const double opacity,
    const core::OrthogonalSliceGeometry& sliceGeometry,
    const core::ImageGeometry::Vector& cursorPhysical,
    const double depth)
{
    AnnotationPipeline pipeline;
    pipeline.imageData = imageData;
    pipeline.reslice = vtkSmartPointer<vtkImageReslice>::New();
    pipeline.mapper = vtkSmartPointer<vtkImageSliceMapper>::New();
    pipeline.imageSlice = vtkSmartPointer<vtkImageSlice>::New();
    pipeline.lookupTable = labelMap
        ? makeLabelLookupTable(scalarMaximum)
        : makeScalarLookupTable(scalarMinimum, scalarMaximum);

    configureOrthogonalReslice(
        *pipeline.reslice,
        pipeline.imageData,
        sliceGeometry,
        cursorPhysical,
        labelMap ? 0.0 : std::numeric_limits<double>::quiet_NaN());
    if(labelMap)
    {
        pipeline.reslice->SetInterpolationModeToNearestNeighbor();
    }
    pipeline.mapper->SetInputConnection(pipeline.reslice->GetOutputPort());
    pipeline.mapper->SetOrientationToZ();
    pipeline.mapper->SetSliceNumber(0);
    pipeline.imageSlice->SetMapper(pipeline.mapper);
    auto* const property = pipeline.imageSlice->GetProperty();
    property->SetLookupTable(pipeline.lookupTable);
    property->UseLookupTableScalarRangeOn();
    property->SetOpacity(std::clamp(opacity, 0.0, 1.0));
    property->SetInterpolationType(
        labelMap ? VTK_NEAREST_INTERPOLATION : VTK_LINEAR_INTERPOLATION);
    pipeline.imageSlice->SetPosition(0.0, 0.0, depth);
    return pipeline;
}

void releaseAnnotationPipeline(
    AnnotationPipeline& pipeline,
    vtkGenericOpenGLRenderWindow& renderWindow)
{
    pipeline.imageSlice->ReleaseGraphicsResources(&renderWindow);
    pipeline.reslice->SetInputData(nullptr);
    pipeline.mapper->SetInputConnection(nullptr);
    pipeline.imageSlice->SetMapper(nullptr);
    pipeline.imageData = nullptr;
}

} // namespace

struct VtkViewport::Impl
{
    explicit Impl(const core::SliceOrientation selectedOrientation)
        : orientation(selectedOrientation)
    {
    }

    core::SliceOrientation orientation;
    QVTKOpenGLNativeWidget* widget = nullptr;
    vtkSmartPointer<vtkGenericOpenGLRenderWindow> renderWindow;
    vtkSmartPointer<vtkRenderer> renderer;
    vtkSmartPointer<vtkRenderer> zoomThumbnailRenderer;
    vtkSmartPointer<vtkImageData> imageData;
    vtkSmartPointer<vtkImageReslice> reslice;
    vtkSmartPointer<vtkImageSliceMapper> mapper;
    vtkSmartPointer<vtkImageSlice> imageSlice;
    vtkSmartPointer<vtkImageSlice> zoomThumbnailImageSlice;
    vtkSmartPointer<vtkLookupTable> anatomicalLookupTable;
    vtkSmartPointer<vtkPolyData> zoomThumbnailBorder;
    vtkSmartPointer<vtkPolyDataMapper> zoomThumbnailBorderMapper;
    vtkSmartPointer<vtkActor> zoomThumbnailBorderActor;
    vtkSmartPointer<vtkPolyData> zoomThumbnailViewport;
    vtkSmartPointer<vtkPolyDataMapper> zoomThumbnailViewportMapper;
    vtkSmartPointer<vtkActor> zoomThumbnailViewportActor;
    vtkSmartPointer<vtkPolyData> horizontalCrosshair;
    vtkSmartPointer<vtkPolyData> verticalCrosshair;
    vtkSmartPointer<vtkPolyDataMapper> horizontalCrosshairMapper;
    vtkSmartPointer<vtkPolyDataMapper> verticalCrosshairMapper;
    vtkSmartPointer<vtkActor> horizontalCrosshairActor;
    vtkSmartPointer<vtkActor> verticalCrosshairActor;
    vtkSmartPointer<vtkPolyData> samplingBoundary;
    vtkSmartPointer<vtkPolyDataMapper> samplingBoundaryMapper;
    vtkSmartPointer<vtkActor> samplingBoundaryActor;
    vtkSmartPointer<vtkPolyData> brushOutline;
    vtkSmartPointer<vtkPolyDataMapper> brushOutlineMapper;
    vtkSmartPointer<vtkActor> brushOutlineActor;
    vtkSmartPointer<vtkPolyData> measurementRuler;
    vtkSmartPointer<vtkPolyDataMapper> measurementRulerMapper;
    vtkSmartPointer<vtkActor> measurementRulerActor;
    vtkSmartPointer<vtkTextActor> measurementLabelActor;
    vtkSmartPointer<vtkTextActor> orientationActor;
    vtkSmartPointer<vtkTextActor> sliceCounterActor;
    std::vector<AnnotationPipeline> annotations;
    std::optional<AnnotationPipeline> annotationComparison;
    std::optional<core::ImageGeometry> imageGeometry;
    std::optional<core::OrthogonalSliceGeometry> sliceGeometry;
    std::optional<core::ImageGeometry::Vector> inspectedPhysical;
    std::optional<core::ImageGeometry::Vector> measurementStartPhysical;
    std::optional<core::ImageGeometry::Vector> measurementEndPhysical;
    core::ImageGeometry::Vector cursorPhysical{};
    InteractionMode interactionMode = InteractionMode::Crosshair;
    DragMode dragMode = DragMode::None;
    QPointF lastMousePosition;
    QPointF zoomThumbnailDragOffset;
    double window = 1.0;
    double level = 0.0;
    int wheelAngleRemainder = 0;
    int samplingSideLength = 1;
    int brushRadius = 1;
    bool brushCircular = false;
    double fitParallelScale = 0.0;
    bool zoomThumbnailAttached = false;
    bool panGrabCursorActive = false;
    bool inverted = false;
    bool colorInput = false;
    bool lightCursor = true;
};

VtkViewport::VtkViewport(
    const core::SliceOrientation orientation,
    QWidget* parent)
    : QWidget(parent)
    , impl_(std::make_unique<Impl>(orientation))
{
    setObjectName("vtkViewport");
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto* const layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    impl_->widget = new QVTKOpenGLNativeWidget(this);
    impl_->widget->setObjectName("vtkOpenGLWidget");
    impl_->widget->setFocusPolicy(Qt::StrongFocus);
    impl_->widget->setMouseTracking(true);
    impl_->widget->installEventFilter(this);
    layout->addWidget(impl_->widget);

    impl_->renderWindow = vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New();
    impl_->renderer = vtkSmartPointer<vtkRenderer>::New();
    impl_->zoomThumbnailRenderer = vtkSmartPointer<vtkRenderer>::New();
    impl_->reslice = vtkSmartPointer<vtkImageReslice>::New();
    impl_->mapper = vtkSmartPointer<vtkImageSliceMapper>::New();
    impl_->imageSlice = vtkSmartPointer<vtkImageSlice>::New();
    impl_->zoomThumbnailImageSlice = vtkSmartPointer<vtkImageSlice>::New();
    impl_->anatomicalLookupTable = vtkSmartPointer<vtkLookupTable>::New();
    impl_->zoomThumbnailBorder = vtkSmartPointer<vtkPolyData>::New();
    impl_->zoomThumbnailBorderMapper =
        vtkSmartPointer<vtkPolyDataMapper>::New();
    impl_->zoomThumbnailBorderActor = vtkSmartPointer<vtkActor>::New();
    impl_->zoomThumbnailViewport = vtkSmartPointer<vtkPolyData>::New();
    impl_->zoomThumbnailViewportMapper =
        vtkSmartPointer<vtkPolyDataMapper>::New();
    impl_->zoomThumbnailViewportActor = vtkSmartPointer<vtkActor>::New();
    impl_->horizontalCrosshair = vtkSmartPointer<vtkPolyData>::New();
    impl_->verticalCrosshair = vtkSmartPointer<vtkPolyData>::New();
    impl_->horizontalCrosshairMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    impl_->verticalCrosshairMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    impl_->horizontalCrosshairActor = vtkSmartPointer<vtkActor>::New();
    impl_->verticalCrosshairActor = vtkSmartPointer<vtkActor>::New();
    impl_->samplingBoundary = vtkSmartPointer<vtkPolyData>::New();
    impl_->samplingBoundaryMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    impl_->samplingBoundaryActor = vtkSmartPointer<vtkActor>::New();
    impl_->brushOutline = vtkSmartPointer<vtkPolyData>::New();
    impl_->brushOutlineMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    impl_->brushOutlineActor = vtkSmartPointer<vtkActor>::New();
    impl_->measurementRuler = vtkSmartPointer<vtkPolyData>::New();
    impl_->measurementRulerMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    impl_->measurementRulerActor = vtkSmartPointer<vtkActor>::New();
    impl_->measurementLabelActor = vtkSmartPointer<vtkTextActor>::New();
    impl_->orientationActor = vtkSmartPointer<vtkTextActor>::New();
    impl_->sliceCounterActor = vtkSmartPointer<vtkTextActor>::New();

    impl_->mapper->SetInputConnection(impl_->reslice->GetOutputPort());
    impl_->mapper->SetOrientationToZ();
    impl_->mapper->SetSliceNumber(0);
    impl_->imageSlice->SetMapper(impl_->mapper);
    impl_->anatomicalLookupTable->SetNumberOfTableValues(256);
    impl_->anatomicalLookupTable->SetHueRange(0.0, 0.0);
    impl_->anatomicalLookupTable->SetSaturationRange(0.0, 0.0);
    impl_->anatomicalLookupTable->SetValueRange(0.0, 1.0);
    impl_->anatomicalLookupTable->SetAlphaRange(1.0, 1.0);
    impl_->anatomicalLookupTable->SetNanColor(0.0, 0.0, 0.0, 1.0);
    impl_->anatomicalLookupTable->SetRampToLinear();
    impl_->anatomicalLookupTable->Build();
    impl_->imageSlice->GetProperty()->SetLookupTable(
        impl_->anatomicalLookupTable);
    impl_->imageSlice->GetProperty()->UseLookupTableScalarRangeOff();
    impl_->imageSlice->SetVisibility(false);
    impl_->zoomThumbnailImageSlice->SetMapper(impl_->mapper);
    impl_->zoomThumbnailImageSlice->SetProperty(impl_->imageSlice->GetProperty());
    impl_->zoomThumbnailImageSlice->SetVisibility(false);

    impl_->zoomThumbnailBorderMapper->SetInputData(impl_->zoomThumbnailBorder);
    impl_->zoomThumbnailBorderActor->SetMapper(
        impl_->zoomThumbnailBorderMapper);
    impl_->zoomThumbnailViewportMapper->SetInputData(
        impl_->zoomThumbnailViewport);
    impl_->zoomThumbnailViewportActor->SetMapper(
        impl_->zoomThumbnailViewportMapper);
    for(auto* const actor : {
            impl_->zoomThumbnailBorderActor.GetPointer(),
            impl_->zoomThumbnailViewportActor.GetPointer()})
    {
        actor->GetProperty()->SetColor(1.0, 176.0 / 255.0, 0.0);
        actor->GetProperty()->SetLineWidth(1.5F);
    }

    impl_->horizontalCrosshairMapper->SetInputData(impl_->horizontalCrosshair);
    impl_->verticalCrosshairMapper->SetInputData(impl_->verticalCrosshair);
    impl_->horizontalCrosshairActor->SetMapper(impl_->horizontalCrosshairMapper);
    impl_->verticalCrosshairActor->SetMapper(impl_->verticalCrosshairMapper);
    for(auto* const actor : {
            impl_->horizontalCrosshairActor.GetPointer(),
            impl_->verticalCrosshairActor.GetPointer()})
    {
        actor->GetProperty()->SetColor(1.0, 176.0 / 255.0, 0.0);
        actor->GetProperty()->SetLineWidth(1.25F);
        actor->SetVisibility(false);
    }

    impl_->samplingBoundaryMapper->SetInputData(impl_->samplingBoundary);
    impl_->samplingBoundaryActor->SetMapper(impl_->samplingBoundaryMapper);
    impl_->samplingBoundaryActor->GetProperty()->SetColor(1.0, 0.72, 0.16);
    impl_->samplingBoundaryActor->GetProperty()->SetLineWidth(1.5F);
    impl_->samplingBoundaryActor->SetVisibility(false);

    impl_->brushOutlineMapper->SetInputData(impl_->brushOutline);
    impl_->brushOutlineActor->SetMapper(impl_->brushOutlineMapper);
    impl_->brushOutlineActor->GetProperty()->SetColor(1.0, 0.0, 0.0);
    impl_->brushOutlineActor->GetProperty()->SetLineWidth(2.0F);
    impl_->brushOutlineActor->SetVisibility(false);

    impl_->measurementRulerMapper->SetInputData(impl_->measurementRuler);
    impl_->measurementRulerActor->SetMapper(impl_->measurementRulerMapper);
    impl_->measurementRulerActor->GetProperty()->SetColor(1.0, 0.72, 0.16);
    impl_->measurementRulerActor->GetProperty()->SetLineWidth(2.0F);
    impl_->measurementRulerActor->SetVisibility(false);
    impl_->measurementLabelActor->GetPositionCoordinate()
        ->SetCoordinateSystemToWorld();
    auto* const measurementText =
        impl_->measurementLabelActor->GetTextProperty();
    measurementText->SetColor(1.0, 0.72, 0.16);
    measurementText->SetFontSize(14);
    measurementText->SetBold(true);
    measurementText->SetShadow(true);
    measurementText->SetBackgroundColor(0.0, 0.0, 0.0);
    measurementText->SetBackgroundOpacity(0.65);
    impl_->measurementLabelActor->SetVisibility(false);

    impl_->orientationActor->SetInput(orientationTitle(orientation));
    impl_->orientationActor->GetPositionCoordinate()
        ->SetCoordinateSystemToNormalizedViewport();
    impl_->orientationActor->GetPositionCoordinate()->SetValue(0.02, 0.98);
    auto* const orientationText = impl_->orientationActor->GetTextProperty();
    orientationText->SetColor(1.0, 176.0 / 255.0, 0.0);
    orientationText->SetFontSize(11);
    orientationText->SetBold(true);
    orientationText->SetJustificationToLeft();
    orientationText->SetVerticalJustificationToTop();
    orientationText->SetShadow(true);

    impl_->sliceCounterActor->GetPositionCoordinate()
        ->SetCoordinateSystemToNormalizedViewport();
    impl_->sliceCounterActor->GetPositionCoordinate()->SetValue(0.98, 0.02);
    auto* const sliceCounterText =
        impl_->sliceCounterActor->GetTextProperty();
    sliceCounterText->SetColor(1.0, 176.0 / 255.0, 0.0);
    sliceCounterText->SetFontSize(12);
    sliceCounterText->SetBold(true);
    sliceCounterText->SetJustificationToRight();
    sliceCounterText->SetVerticalJustificationToBottom();
    sliceCounterText->SetShadow(true);
    impl_->sliceCounterActor->SetVisibility(false);

    impl_->renderer->SetBackground(0.0, 0.0, 0.0);
    impl_->renderer->SetBackground2(0.0, 0.0, 0.0);
    impl_->renderer->GradientBackgroundOff();
    impl_->renderer->AddViewProp(impl_->imageSlice);
    impl_->renderer->AddActor(impl_->horizontalCrosshairActor);
    impl_->renderer->AddActor(impl_->verticalCrosshairActor);
    impl_->renderer->AddActor(impl_->samplingBoundaryActor);
    impl_->renderer->AddActor(impl_->brushOutlineActor);
    impl_->renderer->AddActor(impl_->measurementRulerActor);
    impl_->renderer->AddActor2D(impl_->measurementLabelActor);
    impl_->renderer->AddActor2D(impl_->orientationActor);
    impl_->renderer->AddActor2D(impl_->sliceCounterActor);

    impl_->zoomThumbnailRenderer->SetLayer(1);
    impl_->zoomThumbnailRenderer->PreserveColorBufferOff();
    impl_->zoomThumbnailRenderer->InteractiveOff();
    impl_->zoomThumbnailRenderer->SetBackground(0.0, 0.0, 0.0);
    impl_->zoomThumbnailRenderer->GradientBackgroundOff();
    impl_->zoomThumbnailRenderer->AddViewProp(impl_->zoomThumbnailImageSlice);
    impl_->zoomThumbnailRenderer->AddActor(impl_->zoomThumbnailBorderActor);
    impl_->zoomThumbnailRenderer->AddActor(impl_->zoomThumbnailViewportActor);

    impl_->renderer->SetLayer(0);
    impl_->renderWindow->SetNumberOfLayers(2);
    impl_->renderWindow->AddRenderer(impl_->renderer);
    impl_->renderWindow->SetWindowName("RadMarky Orthogonal Slice");
    impl_->renderWindow->SetStereoCapableWindow(0);
    impl_->renderWindow->StereoRenderOff();
    impl_->widget->setRenderWindow(impl_->renderWindow);
    if(auto* const interactor = impl_->widget->interactor())
    {
        // vtkInteractorStyle::OnChar maps '3' to anaglyph stereo, which turns
        // the focused slice purple. Slice views handle keys in Qt instead.
        interactor->SetInteractorStyle(vtkSmartPointer<vtkInteractorStyleUser>::New());
    }
}

VtkViewport::~VtkViewport()
{
    if(impl_->panGrabCursorActive)
    {
        QGuiApplication::restoreOverrideCursor();
        impl_->panGrabCursorActive = false;
    }
}

void VtkViewport::setInput(
    vtkImageData* const imageData,
    const core::ImageGeometry& geometry,
    const core::ImageGeometry::Vector& cursorPhysical)
{
    if(imageData == nullptr)
    {
        throw std::invalid_argument("VTK slice input cannot be null");
    }

    auto sliceGeometry =
        core::OrthogonalSliceGeometry::fromImageGeometry(geometry, impl_->orientation);
    impl_->imageData = imageData;
    impl_->colorInput = imageData->GetNumberOfScalarComponents() == 3
        && imageData->GetScalarType() == VTK_UNSIGNED_CHAR;
    impl_->imageGeometry = geometry;
    impl_->sliceGeometry = sliceGeometry;
    impl_->cursorPhysical = cursorPhysical;
    impl_->measurementStartPhysical.reset();
    impl_->measurementEndPhysical.reset();
    impl_->measurementRulerActor->SetVisibility(false);
    impl_->measurementLabelActor->SetVisibility(false);

    const auto& slice = *impl_->sliceGeometry;
    // A patient-aligned output rectangle can extend beyond an oblique or
    // sheared input volume. Keep those out-of-volume samples distinct from a
    // real intensity of zero so the anatomical NaN color renders them black.
    configureOrthogonalReslice(
        *impl_->reslice,
        impl_->imageData,
        slice,
        cursorPhysical,
        impl_->colorInput
            ? 0.0
            : std::numeric_limits<double>::quiet_NaN());

    if(impl_->colorInput)
    {
        // RGB display voxels have already been transformed from the DICOM
        // palette. A scalar lookup table would map them back to grayscale.
        impl_->imageSlice->GetProperty()->SetLookupTable(nullptr);
        impl_->imageSlice->GetProperty()->SetColorWindow(255.0);
        impl_->imageSlice->GetProperty()->SetColorLevel(127.5);
    }
    else
    {
        impl_->imageSlice->GetProperty()->SetLookupTable(
            impl_->anatomicalLookupTable);
        impl_->imageSlice->GetProperty()->UseLookupTableScalarRangeOff();
        impl_->imageSlice->GetProperty()->SetColorWindow(impl_->window);
        impl_->imageSlice->GetProperty()->SetColorLevel(impl_->level);
    }
    impl_->imageSlice->GetProperty()->SetInterpolationTypeToLinear();
    impl_->imageSlice->SetVisibility(true);
    impl_->horizontalCrosshairActor->SetVisibility(true);
    impl_->verticalCrosshairActor->SetVisibility(true);
    updateCrosshair();
    resetView();
}

void VtkViewport::addAnnotation(
    vtkImageData* const imageData,
    const bool labelMap,
    const double scalarMinimum,
    const double scalarMaximum,
    const double opacity)
{
    if(imageData == nullptr || !impl_->sliceGeometry)
    {
        throw std::invalid_argument(
            "Annotation overlays require an anatomical image");
    }

    auto pipeline = makeAnnotationPipeline(
        imageData,
        labelMap,
        scalarMinimum,
        scalarMaximum,
        opacity,
        *impl_->sliceGeometry,
        impl_->cursorPhysical,
        0.01 * static_cast<double>(impl_->annotations.size() + 1));
    pipeline.imageSlice->SetVisibility(!impl_->annotationComparison.has_value());
    impl_->renderer->AddViewProp(pipeline.imageSlice);
    impl_->annotations.push_back(std::move(pipeline));
    if(impl_->annotationComparison)
    {
        impl_->annotationComparison->imageSlice->SetPosition(
            0.0,
            0.0,
            0.01 * static_cast<double>(impl_->annotations.size() + 1));
    }
    impl_->renderer->ResetCameraClippingRange();
    impl_->renderWindow->Render();
}

void VtkViewport::removeAnnotation(const std::size_t index)
{
    if(index >= impl_->annotations.size())
    {
        throw std::out_of_range("Annotation overlay index is out of range");
    }
    auto& removed = impl_->annotations[index];
    impl_->renderer->RemoveViewProp(removed.imageSlice);
    releaseAnnotationPipeline(removed, *impl_->renderWindow);
    impl_->annotations.erase(
        impl_->annotations.begin() + static_cast<std::ptrdiff_t>(index));
    for(std::size_t layer = index; layer < impl_->annotations.size(); ++layer)
    {
        impl_->annotations[layer].imageSlice->SetPosition(
            0.0, 0.0, 0.01 * static_cast<double>(layer + 1));
    }
    if(impl_->annotationComparison)
    {
        impl_->annotationComparison->imageSlice->SetPosition(
            0.0,
            0.0,
            0.01 * static_cast<double>(impl_->annotations.size() + 1));
    }
    impl_->renderer->ResetCameraClippingRange();
    impl_->renderWindow->Render();
}

void VtkViewport::setAnnotationOpacity(
    const std::size_t index,
    const double opacity)
{
    if(index >= impl_->annotations.size())
    {
        throw std::out_of_range("Annotation overlay index is out of range");
    }
    impl_->annotations[index].imageSlice->GetProperty()->SetOpacity(
        std::clamp(opacity, 0.0, 1.0));
    impl_->renderWindow->Render();
}

void VtkViewport::setAnnotationVisibility(
    const std::size_t index,
    const bool visible)
{
    if(index >= impl_->annotations.size())
    {
        throw std::out_of_range("Annotation overlay index is out of range");
    }
    auto& annotation = impl_->annotations[index];
    annotation.visible = visible;
    annotation.imageSlice->SetVisibility(
        visible && !impl_->annotationComparison.has_value());
    impl_->renderWindow->Render();
}

void VtkViewport::annotationDataModified(const std::size_t index)
{
    if(index >= impl_->annotations.size())
    {
        throw std::out_of_range("Annotation index is out of range");
    }
    const bool profiling = renderProfilingEnabled();
    QElapsedTimer timer;
    if(profiling)
    {
        timer.start();
    }
    impl_->annotations[index].reslice->Modified();
    impl_->annotations[index].reslice->Update();
    const qint64 updateMicroseconds = profiling
        ? timer.nsecsElapsed() / 1000 : 0;
    impl_->renderWindow->Render();
    if(profiling)
    {
        const qint64 totalMicroseconds = timer.nsecsElapsed() / 1000;
        qInfo().noquote()
            << QStringLiteral(
                   "[PROFILE] annotation_update orientation=%1 index=%2 "
                   "overlays=%3 update_us=%4 render_us=%5 total_us=%6")
                   .arg(QString::fromLatin1(orientationTitle(impl_->orientation)))
                   .arg(static_cast<qulonglong>(index))
                   .arg(static_cast<qulonglong>(impl_->annotations.size()))
                   .arg(updateMicroseconds)
                   .arg(totalMicroseconds - updateMicroseconds)
                   .arg(totalMicroseconds);
    }
}

void VtkViewport::setAnnotationComparison(
    vtkImageData* const imageData,
    const double opacity)
{
    if(imageData == nullptr || !impl_->sliceGeometry)
    {
        throw std::invalid_argument(
            "Annotation comparison requires an anatomical image");
    }
    if(impl_->annotationComparison)
    {
        impl_->renderer->RemoveViewProp(
            impl_->annotationComparison->imageSlice);
        releaseAnnotationPipeline(
            *impl_->annotationComparison, *impl_->renderWindow);
    }

    impl_->annotationComparison = makeAnnotationPipeline(
        imageData,
        true,
        0.0,
        4.0,
        opacity,
        *impl_->sliceGeometry,
        impl_->cursorPhysical,
        0.01 * static_cast<double>(impl_->annotations.size() + 1));
    for(auto& annotation : impl_->annotations)
    {
        annotation.imageSlice->SetVisibility(false);
    }
    impl_->renderer->AddViewProp(impl_->annotationComparison->imageSlice);
    impl_->renderer->ResetCameraClippingRange();
    impl_->renderWindow->Render();
}

void VtkViewport::clearAnnotationComparison()
{
    if(!impl_->annotationComparison)
    {
        return;
    }
    impl_->renderer->RemoveViewProp(impl_->annotationComparison->imageSlice);
    releaseAnnotationPipeline(
        *impl_->annotationComparison, *impl_->renderWindow);
    impl_->annotationComparison.reset();
    for(auto& annotation : impl_->annotations)
    {
        annotation.imageSlice->SetVisibility(annotation.visible);
    }
    impl_->renderer->ResetCameraClippingRange();
    impl_->renderWindow->Render();
}

void VtkViewport::setAnnotationComparisonOpacity(const double opacity)
{
    if(!impl_->annotationComparison)
    {
        return;
    }
    impl_->annotationComparison->imageSlice->GetProperty()->SetOpacity(
        std::clamp(opacity, 0.0, 1.0));
    impl_->renderWindow->Render();
}

void VtkViewport::setAnnotationComparisonVisibility(const bool visible)
{
    if(!impl_->annotationComparison)
    {
        return;
    }
    impl_->annotationComparison->imageSlice->SetVisibility(visible);
    for(auto& annotation : impl_->annotations)
    {
        annotation.imageSlice->SetVisibility(!visible && annotation.visible);
    }
    impl_->renderWindow->Render();
}

void VtkViewport::clearInput()
{
    const bool profiling = renderProfilingEnabled();
    const auto previousAnnotationCount = impl_->annotations.size();
    const vtkIdType previousPropCount = profiling
        ? impl_->renderer->GetViewProps()->GetNumberOfItems() : 0;
    for(auto& annotation : impl_->annotations)
    {
        impl_->renderer->RemoveViewProp(annotation.imageSlice);
        releaseAnnotationPipeline(annotation, *impl_->renderWindow);
    }
    impl_->annotations.clear();
    if(impl_->annotationComparison)
    {
        impl_->renderer->RemoveViewProp(
            impl_->annotationComparison->imageSlice);
        releaseAnnotationPipeline(
            *impl_->annotationComparison, *impl_->renderWindow);
        impl_->annotationComparison.reset();
    }
    impl_->sliceGeometry.reset();
    impl_->imageGeometry.reset();
    impl_->inspectedPhysical.reset();
    impl_->imageData = nullptr;
    impl_->imageSlice->ReleaseGraphicsResources(impl_->renderWindow);
    impl_->reslice->SetInputData(nullptr);
    impl_->imageSlice->SetVisibility(false);
    impl_->zoomThumbnailImageSlice->SetVisibility(false);
    if(impl_->zoomThumbnailAttached)
    {
        impl_->renderWindow->RemoveRenderer(impl_->zoomThumbnailRenderer);
        impl_->zoomThumbnailAttached = false;
    }
    impl_->fitParallelScale = 0.0;
    impl_->horizontalCrosshairActor->SetVisibility(false);
    impl_->verticalCrosshairActor->SetVisibility(false);
    impl_->samplingBoundaryActor->SetVisibility(false);
    impl_->brushOutlineActor->SetVisibility(false);
    impl_->measurementStartPhysical.reset();
    impl_->measurementEndPhysical.reset();
    impl_->measurementRulerActor->SetVisibility(false);
    impl_->measurementLabelActor->SetVisibility(false);
    impl_->sliceCounterActor->SetVisibility(false);
    impl_->dragMode = DragMode::None;
    setPanGrabCursor(false);
    impl_->renderWindow->Render();
    if(profiling)
    {
        qInfo().noquote()
            << QStringLiteral(
                   "[PROFILE] viewport_clear orientation=%1 overlays=%2 "
                   "props_before=%3 props_after=%4")
                   .arg(QString::fromLatin1(orientationTitle(impl_->orientation)))
                   .arg(static_cast<qulonglong>(previousAnnotationCount))
                   .arg(previousPropCount)
                   .arg(impl_->renderer->GetViewProps()->GetNumberOfItems());
    }
}

void VtkViewport::setCursor(
    const core::ImageGeometry::Vector& cursorPhysical)
{
    if(!impl_->sliceGeometry)
    {
        return;
    }

    const bool profiling = renderProfilingEnabled();
    QElapsedTimer timer;
    if(profiling)
    {
        timer.start();
    }
    const double previousSlice =
        impl_->sliceGeometry->normalCoordinate(impl_->cursorPhysical);
    const double nextSlice =
        impl_->sliceGeometry->normalCoordinate(cursorPhysical);
    const bool sliceChanged = std::abs(previousSlice - nextSlice) > 1.0e-6;
    if(sliceChanged)
    {
        impl_->measurementStartPhysical.reset();
        impl_->measurementEndPhysical.reset();
        impl_->measurementRulerActor->SetVisibility(false);
        impl_->measurementLabelActor->SetVisibility(false);
    }
    impl_->cursorPhysical = cursorPhysical;
    if(sliceChanged)
    {
        setOrthogonalResliceCursor(
            *impl_->reslice, *impl_->sliceGeometry, cursorPhysical);
        for(auto& annotation : impl_->annotations)
        {
            setOrthogonalResliceCursor(
                *annotation.reslice, *impl_->sliceGeometry, cursorPhysical);
        }
        if(impl_->annotationComparison)
        {
            setOrthogonalResliceCursor(
                *impl_->annotationComparison->reslice,
                *impl_->sliceGeometry,
                cursorPhysical);
        }
    }
    const qint64 pipelineMicroseconds = profiling
        ? timer.nsecsElapsed() / 1000 : 0;
    updateCrosshair();
    if(sliceChanged)
    {
        impl_->renderer->ResetCameraClippingRange();
    }
    const qint64 overlayMicroseconds = profiling
        ? timer.nsecsElapsed() / 1000 - pipelineMicroseconds : 0;
    impl_->renderWindow->Render();
    if(profiling)
    {
        const qint64 totalMicroseconds = timer.nsecsElapsed() / 1000;
        qInfo().noquote()
            << QStringLiteral(
                   "[PROFILE] viewport_cursor orientation=%1 overlays=%2 "
                   "props=%3 slice_changed=%4 pipeline_us=%5 overlay_us=%6 "
                   "render_us=%7 total_us=%8")
                   .arg(QString::fromLatin1(orientationTitle(impl_->orientation)))
                   .arg(static_cast<qulonglong>(impl_->annotations.size()))
                   .arg(impl_->renderer->GetViewProps()->GetNumberOfItems())
                   .arg(sliceChanged ? 1 : 0)
                   .arg(pipelineMicroseconds)
                   .arg(overlayMicroseconds)
                   .arg(totalMicroseconds - pipelineMicroseconds
                        - overlayMicroseconds)
                   .arg(totalMicroseconds);
    }
}

void VtkViewport::setSliceCounter(
    const int currentSlice, const int totalSlices)
{
    if(impl_->orientation != core::SliceOrientation::Axial
       || !impl_->sliceGeometry)
    {
        impl_->sliceCounterActor->SetVisibility(false);
        return;
    }
    if(totalSlices < 1 || currentSlice < 1 || currentSlice > totalSlices)
    {
        throw std::invalid_argument("Slice counter values are invalid");
    }
    const std::string text = std::to_string(currentSlice) + " / "
        + std::to_string(totalSlices);
    impl_->sliceCounterActor->SetInput(text.c_str());
    impl_->sliceCounterActor->SetVisibility(true);
}

void VtkViewport::setWindowLevel(const double window, const double level)
{
    impl_->window = window;
    impl_->level = level;
    if(!impl_->colorInput)
    {
        impl_->imageSlice->GetProperty()->SetColorWindow(window);
        impl_->imageSlice->GetProperty()->SetColorLevel(level);
    }
    if(impl_->sliceGeometry)
    {
        impl_->renderWindow->Render();
    }
    updateAdaptiveCursorTone(impl_->lastMousePosition);
}

void VtkViewport::setInverted(const bool inverted)
{
    impl_->inverted = inverted;
    impl_->anatomicalLookupTable->SetValueRange(
        inverted ? 1.0 : 0.0,
        inverted ? 0.0 : 1.0);
    impl_->anatomicalLookupTable->Build();
    impl_->imageSlice->GetProperty()->Modified();
    if(impl_->sliceGeometry)
    {
        impl_->renderWindow->Render();
    }
    updateAdaptiveCursorTone(impl_->lastMousePosition);
}

void VtkViewport::setSamplingSideLength(const int sideLength)
{
    if(sideLength < 1)
    {
        throw std::invalid_argument("Sampling side length must be positive");
    }
    impl_->samplingSideLength = sideLength;
    if(sideLength == 1 || !impl_->inspectedPhysical)
    {
        impl_->samplingBoundaryActor->SetVisibility(false);
    }
    else
    {
        updateSamplingBoundary(*impl_->inspectedPhysical);
    }
    if(impl_->sliceGeometry)
    {
        impl_->renderWindow->Render();
    }
}

void VtkViewport::setBrushRadius(const int radius)
{
    if(radius < 1 || radius > 100)
    {
        throw std::invalid_argument("Brush size must be from 1 to 100 voxels");
    }
    impl_->brushRadius = radius;
    if(impl_->inspectedPhysical)
    {
        updateBrushOutline(*impl_->inspectedPhysical);
    }
    if(impl_->sliceGeometry)
    {
        impl_->renderWindow->Render();
    }
}

void VtkViewport::setBrushShape(const bool circular)
{
    impl_->brushCircular = circular;
    if(impl_->inspectedPhysical)
    {
        updateBrushOutline(*impl_->inspectedPhysical);
    }
    if(impl_->sliceGeometry)
    {
        impl_->renderWindow->Render();
    }
}

void VtkViewport::setBrushLabel(const int label)
{
    if(label < 1 || label > 65535)
    {
        throw std::invalid_argument("Brush label must be from 1 to 65535");
    }
    const std::uint32_t packed = core::defaultLabelColor(
        static_cast<std::uint16_t>(label));
    impl_->brushOutlineActor->GetProperty()->SetColor(
        static_cast<double>((packed >> 16U) & 0xFFU) / 255.0,
        static_cast<double>((packed >> 8U) & 0xFFU) / 255.0,
        static_cast<double>(packed & 0xFFU) / 255.0);
    if(impl_->brushOutlineActor->GetVisibility())
    {
        impl_->renderWindow->Render();
    }
}

void VtkViewport::setInteractionMode(const InteractionMode mode)
{
    impl_->interactionMode = mode;
    const bool editing = mode == InteractionMode::Brush
        || mode == InteractionMode::Erase
        || mode == InteractionMode::ScopedErase;
    const bool brushEditing = mode == InteractionMode::Brush
        || mode == InteractionMode::Erase;
    impl_->horizontalCrosshairActor->SetVisibility(
        impl_->sliceGeometry.has_value() && !editing);
    impl_->verticalCrosshairActor->SetVisibility(
        impl_->sliceGeometry.has_value() && !editing);
    if(brushEditing && impl_->inspectedPhysical)
    {
        updateBrushOutline(*impl_->inspectedPhysical);
    }
    else
    {
        impl_->brushOutlineActor->SetVisibility(false);
    }
    if(!impl_->panGrabCursorActive)
    {
        applyInteractionCursor();
    }
    if(impl_->sliceGeometry)
    {
        impl_->renderWindow->Render();
    }
}

void VtkViewport::applyInteractionCursor()
{
    const QColor tint =
        impl_->lightCursor ? kLightCursorTint : kDarkCursorTint;
    QCursor cursor(Qt::ArrowCursor);
    switch(impl_->interactionMode)
    {
    case InteractionMode::Crosshair:
        cursor = QCursor(Qt::CrossCursor);
        break;
    case InteractionMode::Zoom:
        cursor = ui::svgCursor(QStringLiteral(":/icons/zoom.svg"), QSize(32, 32), tint);
        break;
    case InteractionMode::Pan:
        cursor = ui::svgCursor(QStringLiteral(":/icons/pan.svg"), QSize(32, 32), tint);
        break;
    case InteractionMode::Contrast:
        cursor = ui::svgCursor(
            QStringLiteral(":/icons/contrast.svg"), QSize(32, 32), tint);
        break;
    case InteractionMode::Measure:
        cursor = ui::crosshairCursor(tint);
        break;
    case InteractionMode::Brush:
    case InteractionMode::Erase:
        cursor = QCursor(Qt::CrossCursor);
        break;
    case InteractionMode::ScopedErase:
        cursor = ui::svgCursor(
            QStringLiteral(":/icons/circular-target-sniper.svg"),
            QSize(32, 32),
            tint);
        break;
    }
    // QVTK resets to DefaultCursor on every render, so both must stay in sync.
    impl_->widget->setDefaultCursor(cursor);
    impl_->widget->setCursor(cursor);
}

void VtkViewport::setPanGrabCursor(const bool grabbing)
{
    const QColor tint =
        impl_->lightCursor ? kLightCursorTint : kDarkCursorTint;
    const QCursor grabCursor =
        ui::svgCursor(QStringLiteral(":/icons/pan.svg"), QSize(32, 32), tint);
    if(grabbing)
    {
        impl_->widget->setDefaultCursor(grabCursor);
        impl_->widget->setCursor(grabCursor);
        if(impl_->panGrabCursorActive)
        {
            QGuiApplication::changeOverrideCursor(grabCursor);
        }
        else
        {
            QGuiApplication::setOverrideCursor(grabCursor);
            impl_->panGrabCursorActive = true;
        }
        return;
    }

    if(impl_->panGrabCursorActive)
    {
        QGuiApplication::restoreOverrideCursor();
        impl_->panGrabCursorActive = false;
    }
    if(impl_->widget != nullptr)
    {
        applyInteractionCursor();
    }
}

std::optional<double> VtkViewport::displayedLuminanceAt(
    const QPointF& widgetPosition) const
{
    if(!impl_->imageData || !impl_->imageGeometry || !impl_->sliceGeometry
       || impl_->window <= 0.0)
    {
        return std::nullopt;
    }

    double horizontal = 0.0;
    double vertical = 0.0;
    if(!mapWidgetToSlice(widgetPosition, horizontal, vertical, true))
    {
        return std::nullopt;
    }

    const auto physical = impl_->sliceGeometry->pointOnCursorPlane(
        horizontal, vertical, impl_->cursorPhysical);
    const auto continuous =
        impl_->imageGeometry->physicalToContinuousIndex(physical);
    int extent[6]{};
    impl_->imageData->GetExtent(extent);
    const int ijk[3]{
        static_cast<int>(std::llround(continuous[0])),
        static_cast<int>(std::llround(continuous[1])),
        static_cast<int>(std::llround(continuous[2])),
    };
    if(ijk[0] < extent[0] || ijk[0] > extent[1] || ijk[1] < extent[2]
       || ijk[1] > extent[3] || ijk[2] < extent[4] || ijk[2] > extent[5])
    {
        return std::nullopt;
    }

    const double value = impl_->imageData->GetScalarComponentAsDouble(
        ijk[0], ijk[1], ijk[2], 0);
    if(!std::isfinite(value))
    {
        return std::nullopt;
    }

    const double displayMinimum = impl_->level - 0.5 * impl_->window;
    double luminance = (value - displayMinimum) / impl_->window;
    luminance = std::clamp(luminance, 0.0, 1.0);
    if(impl_->inverted)
    {
        luminance = 1.0 - luminance;
    }
    return luminance;
}

void VtkViewport::updateAdaptiveCursorTone(const QPointF& widgetPosition)
{
    const bool adaptive = impl_->panGrabCursorActive
        || impl_->interactionMode == InteractionMode::Pan
        || impl_->interactionMode == InteractionMode::Zoom
        || impl_->interactionMode == InteractionMode::Contrast
        || impl_->interactionMode == InteractionMode::Measure
        || impl_->interactionMode == InteractionMode::ScopedErase;
    if(!adaptive)
    {
        return;
    }

    const auto luminance = displayedLuminanceAt(widgetPosition);
    bool lightCursor = impl_->lightCursor;
    if(luminance)
    {
        if(*luminance < 0.42)
        {
            lightCursor = true;
        }
        else if(*luminance > 0.58)
        {
            lightCursor = false;
        }
    }
    if(lightCursor == impl_->lightCursor)
    {
        return;
    }

    impl_->lightCursor = lightCursor;
    if(impl_->panGrabCursorActive)
    {
        setPanGrabCursor(true);
    }
    else
    {
        applyInteractionCursor();
    }
}

void VtkViewport::zoomIn()
{
    zoomBy(1.25);
}

void VtkViewport::zoomOut()
{
    zoomBy(0.8);
}

void VtkViewport::panBy(
    const double horizontalDirection,
    const double verticalDirection)
{
    if(!impl_->sliceGeometry
       || !std::isfinite(horizontalDirection)
       || !std::isfinite(verticalDirection))
    {
        return;
    }
    auto* const camera = impl_->renderer->GetActiveCamera();
    const double step = camera->GetParallelScale() * 0.1;
    double position[3]{};
    double focalPoint[3]{};
    camera->GetPosition(position);
    camera->GetFocalPoint(focalPoint);
    const double horizontalTranslation = -horizontalDirection * step;
    const double verticalTranslation = -verticalDirection * step;
    camera->SetPosition(
        position[0] + horizontalTranslation,
        position[1] + verticalTranslation,
        position[2]);
    camera->SetFocalPoint(
        focalPoint[0] + horizontalTranslation,
        focalPoint[1] + verticalTranslation,
        focalPoint[2]);
    impl_->renderer->ResetCameraClippingRange();
    updateZoomThumbnail();
    impl_->renderWindow->Render();
}

void VtkViewport::resetView()
{
    if(!impl_->sliceGeometry)
    {
        return;
    }

    double bounds[6]{};
    impl_->imageSlice->GetBounds(bounds);
    const double centerX = (bounds[0] + bounds[1]) / 2.0;
    const double centerY = (bounds[2] + bounds[3]) / 2.0;
    auto* const camera = impl_->renderer->GetActiveCamera();
    camera->ParallelProjectionOn();
    camera->SetFocalPoint(centerX, centerY, 0.0);
    camera->SetPosition(centerX, centerY, 1.0);
    camera->SetViewUp(0.0, 1.0, 0.0);
    impl_->renderer->ResetCamera();
    impl_->fitParallelScale = camera->GetParallelScale();
    impl_->renderer->ResetCameraClippingRange();
    updateCrosshair();
    updateMeasurementOverlay();
    updateZoomThumbnail();
    impl_->renderWindow->Render();
}

QImage VtkViewport::captureImageWithoutCrosshair()
{
    return captureSliceImage(false, true);
}

QImage VtkViewport::captureSliceImage(
    const bool showCrosshair,
    const bool respectZoom)
{
    if(!impl_->sliceGeometry)
    {
        return {};
    }

    auto savedCamera = vtkSmartPointer<vtkCamera>::New();
    savedCamera->DeepCopy(impl_->renderer->GetActiveCamera());
    const double savedFitParallelScale = impl_->fitParallelScale;
    if(!respectZoom)
    {
        double bounds[6]{};
        impl_->imageSlice->GetBounds(bounds);
        const double centerX = (bounds[0] + bounds[1]) / 2.0;
        const double centerY = (bounds[2] + bounds[3]) / 2.0;
        auto* const camera = impl_->renderer->GetActiveCamera();
        camera->ParallelProjectionOn();
        camera->SetFocalPoint(centerX, centerY, 0.0);
        camera->SetPosition(centerX, centerY, 1.0);
        camera->SetViewUp(0.0, 1.0, 0.0);
        impl_->renderer->ResetCamera();
        impl_->renderer->ResetCameraClippingRange();
        updateCrosshair();
    }

    const int horizontalVisibility =
        impl_->horizontalCrosshairActor->GetVisibility();
    const int verticalVisibility =
        impl_->verticalCrosshairActor->GetVisibility();
    const int samplingVisibility =
        impl_->samplingBoundaryActor->GetVisibility();
    const int brushVisibility = impl_->brushOutlineActor->GetVisibility();
    const int rulerVisibility = impl_->measurementRulerActor->GetVisibility();
    const int labelVisibility = impl_->measurementLabelActor->GetVisibility();
    impl_->horizontalCrosshairActor->SetVisibility(
        showCrosshair ? horizontalVisibility : false);
    impl_->verticalCrosshairActor->SetVisibility(
        showCrosshair ? verticalVisibility : false);
    impl_->samplingBoundaryActor->SetVisibility(false);
    impl_->brushOutlineActor->SetVisibility(false);
    impl_->measurementRulerActor->SetVisibility(false);
    impl_->measurementLabelActor->SetVisibility(false);
    const bool hadZoomThumbnail = impl_->zoomThumbnailAttached;
    if(hadZoomThumbnail)
    {
        impl_->renderWindow->RemoveRenderer(impl_->zoomThumbnailRenderer);
        impl_->zoomThumbnailAttached = false;
    }
    impl_->renderWindow->Render();

    const QImage image = impl_->widget->grabFramebuffer();

    impl_->horizontalCrosshairActor->SetVisibility(horizontalVisibility);
    impl_->verticalCrosshairActor->SetVisibility(verticalVisibility);
    impl_->samplingBoundaryActor->SetVisibility(samplingVisibility);
    impl_->brushOutlineActor->SetVisibility(brushVisibility);
    impl_->measurementRulerActor->SetVisibility(rulerVisibility);
    impl_->measurementLabelActor->SetVisibility(labelVisibility);
    impl_->renderer->GetActiveCamera()->DeepCopy(savedCamera);
    impl_->fitParallelScale = savedFitParallelScale;
    updateCrosshair();
    updateMeasurementOverlay();
    updateZoomThumbnail();
    if(hadZoomThumbnail && !impl_->zoomThumbnailAttached)
    {
        impl_->renderWindow->AddRenderer(impl_->zoomThumbnailRenderer);
        impl_->zoomThumbnailAttached = true;
    }
    impl_->renderWindow->Render();
    return image;
}

bool VtkViewport::mapWidgetToSlice(
    const QPointF& widgetPosition,
    double& horizontal,
    double& vertical,
    const bool clampToImage) const
{
    if(!impl_->sliceGeometry || impl_->widget->width() <= 0
       || impl_->widget->height() <= 0)
    {
        return false;
    }

    const int* const renderSize = impl_->renderWindow->GetSize();
    const double scaleX =
        static_cast<double>(renderSize[0]) / impl_->widget->width();
    const double scaleY =
        static_cast<double>(renderSize[1]) / impl_->widget->height();
    const double displayX = widgetPosition.x() * scaleX;
    const double displayY =
        (impl_->widget->height() - 1.0 - widgetPosition.y()) * scaleY;

    double nearPoint[4]{};
    double farPoint[4]{};
    impl_->renderer->SetDisplayPoint(displayX, displayY, 0.0);
    impl_->renderer->DisplayToWorld();
    impl_->renderer->GetWorldPoint(nearPoint);
    impl_->renderer->SetDisplayPoint(displayX, displayY, 1.0);
    impl_->renderer->DisplayToWorld();
    impl_->renderer->GetWorldPoint(farPoint);
    if(nearPoint[3] == 0.0 || farPoint[3] == 0.0)
    {
        return false;
    }
    for(std::size_t axis = 0; axis < 3; ++axis)
    {
        nearPoint[axis] /= nearPoint[3];
        farPoint[axis] /= farPoint[3];
    }
    const double depth = farPoint[2] - nearPoint[2];
    if(std::abs(depth) < 1.0e-12)
    {
        return false;
    }
    const double t = -nearPoint[2] / depth;
    horizontal = nearPoint[0] + t * (farPoint[0] - nearPoint[0]);
    vertical = nearPoint[1] + t * (farPoint[1] - nearPoint[1]);

    if(clampToImage)
    {
        const auto& slice = *impl_->sliceGeometry;
        const double horizontalMaximum = slice.horizontalMinimum()
            + static_cast<double>(slice.width() - 1) * slice.outputSpacing();
        const double verticalMaximum = slice.verticalMinimum()
            + static_cast<double>(slice.height() - 1) * slice.outputSpacing();
        horizontal = std::clamp(
            horizontal, slice.horizontalMinimum(), horizontalMaximum);
        vertical =
            std::clamp(vertical, slice.verticalMinimum(), verticalMaximum);
    }
    return true;
}

bool VtkViewport::mapWidgetToZoomThumbnail(
    const QPointF& widgetPosition,
    double& horizontal,
    double& vertical,
    const bool clampToThumbnail) const
{
    if(!impl_->zoomThumbnailAttached || impl_->widget->width() <= 0
       || impl_->widget->height() <= 0)
    {
        return false;
    }

    const int* const renderSize = impl_->renderWindow->GetSize();
    if(renderSize[0] <= 0 || renderSize[1] <= 0)
    {
        return false;
    }
    double displayX = widgetPosition.x()
        * static_cast<double>(renderSize[0]) / impl_->widget->width();
    double displayY = (impl_->widget->height() - 1.0 - widgetPosition.y())
        * static_cast<double>(renderSize[1]) / impl_->widget->height();
    const double* const viewport = impl_->zoomThumbnailRenderer->GetViewport();
    const double left = viewport[0] * renderSize[0];
    const double bottom = viewport[1] * renderSize[1];
    const double right = viewport[2] * renderSize[0];
    const double top = viewport[3] * renderSize[1];
    const bool inside = displayX >= left && displayX <= right
        && displayY >= bottom && displayY <= top;
    if(!inside && !clampToThumbnail)
    {
        return false;
    }
    displayX = std::clamp(displayX, left, right);
    displayY = std::clamp(displayY, bottom, top);

    double nearPoint[4]{};
    double farPoint[4]{};
    impl_->zoomThumbnailRenderer->SetDisplayPoint(displayX, displayY, 0.0);
    impl_->zoomThumbnailRenderer->DisplayToWorld();
    impl_->zoomThumbnailRenderer->GetWorldPoint(nearPoint);
    impl_->zoomThumbnailRenderer->SetDisplayPoint(displayX, displayY, 1.0);
    impl_->zoomThumbnailRenderer->DisplayToWorld();
    impl_->zoomThumbnailRenderer->GetWorldPoint(farPoint);
    if(nearPoint[3] == 0.0 || farPoint[3] == 0.0)
    {
        return false;
    }
    for(std::size_t axis = 0; axis < 3; ++axis)
    {
        nearPoint[axis] /= nearPoint[3];
        farPoint[axis] /= farPoint[3];
    }
    const double depth = farPoint[2] - nearPoint[2];
    if(std::abs(depth) < 1.0e-12)
    {
        return false;
    }
    const double t = -nearPoint[2] / depth;
    horizontal = nearPoint[0] + t * (farPoint[0] - nearPoint[0]);
    vertical = nearPoint[1] + t * (farPoint[1] - nearPoint[1]);
    return true;
}

void VtkViewport::selectAt(const QPointF& widgetPosition)
{
    if(!impl_->sliceGeometry)
    {
        return;
    }
    double horizontal = 0.0;
    double vertical = 0.0;
    if(!mapWidgetToSlice(widgetPosition, horizontal, vertical, true))
    {
        return;
    }
    const auto point = impl_->sliceGeometry->pointOnCursorPlane(
        horizontal, vertical, impl_->cursorPhysical);
    emit physicalPointSelected(point[0], point[1], point[2]);
}

void VtkViewport::hoverAt(const QPointF& widgetPosition)
{
    if(!impl_->sliceGeometry)
    {
        return;
    }
    double horizontal = 0.0;
    double vertical = 0.0;
    if(!mapWidgetToSlice(widgetPosition, horizontal, vertical, false))
    {
        emit pointerExited();
        return;
    }
    const auto point = impl_->sliceGeometry->pointOnCursorPlane(
        horizontal, vertical, impl_->cursorPhysical);
    impl_->inspectedPhysical = point;
    updateSamplingBoundary(point);
    updateBrushOutline(point);
    impl_->renderWindow->Render();
    updateAdaptiveCursorTone(widgetPosition);
    emit physicalPointHovered(point[0], point[1], point[2]);
}

void VtkViewport::updateBrushOutline(
    const core::ImageGeometry::Vector& inspectedPhysical)
{
    const bool editing = impl_->interactionMode == InteractionMode::Brush
        || impl_->interactionMode == InteractionMode::Erase;
    if(!editing || !impl_->imageGeometry || !impl_->sliceGeometry)
    {
        impl_->brushOutlineActor->SetVisibility(false);
        return;
    }
    const auto continuous =
        impl_->imageGeometry->physicalToContinuousIndex(inspectedPhysical);
    core::ImageGeometry::Vector centerIndex{};
    const auto& dimensions = impl_->imageGeometry->dimensions();
    for(std::size_t axis = 0; axis < 3; ++axis)
    {
        centerIndex[axis] = std::round(continuous[axis]);
        if(!std::isfinite(centerIndex[axis]) || centerIndex[axis] < 0.0
           || centerIndex[axis] >= static_cast<double>(dimensions[axis]))
        {
            impl_->brushOutlineActor->SetVisibility(false);
            return;
        }
    }
    const auto& slice = *impl_->sliceGeometry;
    const core::BrushFootprint footprint(
        impl_->brushRadius,
        impl_->brushCircular ? core::BrushShape::Circle
                             : core::BrushShape::Square);
    const auto outline = core::brushOutlinePoints(
        footprint, *impl_->imageGeometry, slice, centerIndex);
    constexpr double overlayDepth = 0.3;
    setClosedOutline(*impl_->brushOutline, outline, overlayDepth);
    impl_->brushOutlineActor->SetVisibility(true);
}

void VtkViewport::editAt(
    const QPointF& widgetPosition, const bool firstPoint)
{
    double horizontal = 0.0;
    double vertical = 0.0;
    if(!mapWidgetToSlice(widgetPosition, horizontal, vertical, true))
    {
        return;
    }
    const auto point = impl_->sliceGeometry->pointOnCursorPlane(
        horizontal, vertical, impl_->cursorPhysical);
    if(firstPoint)
    {
        emit editStrokeStarted(point[0], point[1], point[2]);
    }
    else
    {
        emit editStrokeContinued(point[0], point[1], point[2]);
    }
}

void VtkViewport::measureAt(
    const QPointF& widgetPosition, const bool firstPoint)
{
    if(!impl_->sliceGeometry)
    {
        return;
    }
    double horizontal = 0.0;
    double vertical = 0.0;
    if(!mapWidgetToSlice(widgetPosition, horizontal, vertical, true))
    {
        return;
    }
    const auto point = impl_->sliceGeometry->pointOnCursorPlane(
        horizontal, vertical, impl_->cursorPhysical);
    if(firstPoint || !impl_->measurementStartPhysical)
    {
        impl_->measurementStartPhysical = point;
    }
    impl_->measurementEndPhysical = point;
    updateMeasurementOverlay();
    impl_->renderWindow->Render();
}

void VtkViewport::clearMeasurement()
{
    impl_->measurementStartPhysical.reset();
    impl_->measurementEndPhysical.reset();
    impl_->measurementRulerActor->SetVisibility(false);
    impl_->measurementLabelActor->SetVisibility(false);
    if(impl_->sliceGeometry)
    {
        impl_->renderWindow->Render();
    }
}

void VtkViewport::updateMeasurementOverlay()
{
    if(!impl_->sliceGeometry || !impl_->measurementStartPhysical
       || !impl_->measurementEndPhysical)
    {
        impl_->measurementRulerActor->SetVisibility(false);
        impl_->measurementLabelActor->SetVisibility(false);
        return;
    }
    const auto& slice = *impl_->sliceGeometry;
    const auto& start = *impl_->measurementStartPhysical;
    const auto& end = *impl_->measurementEndPhysical;
    const double startX = slice.horizontalCoordinate(start);
    const double startY = slice.verticalCoordinate(start);
    const double endX = slice.horizontalCoordinate(end);
    const double endY = slice.verticalCoordinate(end);
    const int viewportHeight = impl_->widget->height();
    const double cameraScale =
        impl_->renderer->GetActiveCamera()->GetParallelScale();
    const double worldUnitsPerPixel = viewportHeight > 0
            && std::isfinite(cameraScale) && cameraScale > 0.0
        ? (2.0 * cameraScale) / static_cast<double>(viewportHeight)
        : slice.outputSpacing();
    constexpr double overlayDepth = 0.3;
    setRulerShape(
        *impl_->measurementRuler,
        startX, startY, endX, endY,
        overlayDepth, 5.0 * worldUnitsPerPixel);

    std::ostringstream label;
    label.setf(std::ios::fixed);
    label.precision(1);
    label << core::physicalDistanceMillimetres(start, end) << " mm";
    impl_->measurementLabelActor->SetInput(label.str().c_str());
    impl_->measurementLabelActor->SetPosition(
        (startX + endX) / 2.0 + 7.0 * worldUnitsPerPixel,
        (startY + endY) / 2.0 + 7.0 * worldUnitsPerPixel);
    impl_->measurementRulerActor->SetVisibility(true);
    impl_->measurementLabelActor->SetVisibility(true);
}

void VtkViewport::updateSamplingBoundary(
    const core::ImageGeometry::Vector& inspectedPhysical)
{
    if(impl_->samplingSideLength == 1 || !impl_->imageGeometry
       || !impl_->sliceGeometry)
    {
        impl_->samplingBoundaryActor->SetVisibility(false);
        return;
    }

    const auto continuousIndex =
        impl_->imageGeometry->physicalToContinuousIndex(inspectedPhysical);
    core::ImageGeometry::Vector centerIndex{};
    const auto& dimensions = impl_->imageGeometry->dimensions();
    for(std::size_t axis = 0; axis < 3; ++axis)
    {
        if(!std::isfinite(continuousIndex[axis]))
        {
            impl_->samplingBoundaryActor->SetVisibility(false);
            return;
        }
        centerIndex[axis] = std::round(continuousIndex[axis]);
        if(centerIndex[axis] < 0.0
           || centerIndex[axis] >= static_cast<double>(dimensions[axis]))
        {
            impl_->samplingBoundaryActor->SetVisibility(false);
            return;
        }
    }

    const auto centerPhysical =
        impl_->imageGeometry->indexToPhysical(centerIndex);
    const auto& slice = *impl_->sliceGeometry;
    const double centerHorizontal = slice.horizontalCoordinate(centerPhysical);
    const double centerVertical = slice.verticalCoordinate(centerPhysical);
    const double horizontalStep = impl_->imageGeometry->physicalStepForOneVoxel(
        slice.horizontalDirectionLps());
    const double verticalStep = impl_->imageGeometry->physicalStepForOneVoxel(
        slice.verticalDirectionLps());
    const int firstOffset = -(impl_->samplingSideLength - 1) / 2;
    double left = centerHorizontal
        + (static_cast<double>(firstOffset) - 0.5) * horizontalStep;
    double right = centerHorizontal
        + (static_cast<double>(firstOffset + impl_->samplingSideLength) - 0.5)
            * horizontalStep;
    double bottom = centerVertical
        + (static_cast<double>(firstOffset) - 0.5) * verticalStep;
    double top = centerVertical
        + (static_cast<double>(firstOffset + impl_->samplingSideLength) - 0.5)
            * verticalStep;
    const double horizontalMaximum = slice.horizontalMinimum()
        + static_cast<double>(slice.width() - 1) * slice.outputSpacing();
    const double verticalMaximum = slice.verticalMinimum()
        + static_cast<double>(slice.height() - 1) * slice.outputSpacing();
    left = std::clamp(left, slice.horizontalMinimum(), horizontalMaximum);
    right = std::clamp(right, slice.horizontalMinimum(), horizontalMaximum);
    bottom = std::clamp(bottom, slice.verticalMinimum(), verticalMaximum);
    top = std::clamp(top, slice.verticalMinimum(), verticalMaximum);
    if(right <= left || top <= bottom)
    {
        impl_->samplingBoundaryActor->SetVisibility(false);
        return;
    }

    constexpr double overlayDepth = 0.2;
    setRectangle(
        *impl_->samplingBoundary,
        left,
        bottom,
        right,
        top,
        overlayDepth);
    impl_->samplingBoundaryActor->SetVisibility(true);
}

void VtkViewport::hideSamplingBoundary()
{
    impl_->inspectedPhysical.reset();
    impl_->samplingBoundaryActor->SetVisibility(false);
    impl_->brushOutlineActor->SetVisibility(false);
    if(impl_->sliceGeometry)
    {
        impl_->renderWindow->Render();
    }
}

void VtkViewport::updateCrosshair()
{
    if(!impl_->sliceGeometry)
    {
        return;
    }
    const auto& slice = *impl_->sliceGeometry;
    const double horizontal = slice.horizontalCoordinate(impl_->cursorPhysical);
    const double vertical = slice.verticalCoordinate(impl_->cursorPhysical);
    const double horizontalMaximum = slice.horizontalMinimum()
        + static_cast<double>(slice.width() - 1) * slice.outputSpacing();
    const double verticalMaximum = slice.verticalMinimum()
        + static_cast<double>(slice.height() - 1) * slice.outputSpacing();
    const int viewportHeight = impl_->widget->height();
    const double cameraScale =
        impl_->renderer->GetActiveCamera()->GetParallelScale();
    const double worldUnitsPerPixel = viewportHeight > 0
            && std::isfinite(cameraScale) && cameraScale > 0.0
        ? (2.0 * cameraScale) / static_cast<double>(viewportHeight)
        : slice.outputSpacing();
    constexpr double dashPixels = 8.0;
    constexpr double gapPixels = 6.0;
    const double dashLength = worldUnitsPerPixel * dashPixels;
    const double gapLength = worldUnitsPerPixel * gapPixels;
    constexpr double overlayDepth = 0.1;
    setCenteredDashedLine(
        *impl_->horizontalCrosshair,
        slice.horizontalMinimum(),
        vertical,
        horizontalMaximum,
        vertical,
        horizontal,
        vertical,
        overlayDepth,
        dashLength,
        gapLength);
    setCenteredDashedLine(
        *impl_->verticalCrosshair,
        horizontal,
        slice.verticalMinimum(),
        horizontal,
        verticalMaximum,
        horizontal,
        vertical,
        overlayDepth,
        dashLength,
        gapLength);
}

void VtkViewport::updateZoomThumbnail()
{
    const auto detachThumbnail = [this]() {
        if(impl_->zoomThumbnailAttached)
        {
            impl_->renderWindow->RemoveRenderer(impl_->zoomThumbnailRenderer);
            impl_->zoomThumbnailAttached = false;
        }
    };

    if(!impl_->sliceGeometry || impl_->fitParallelScale <= 0.0
       || impl_->widget->width() <= 0 || impl_->widget->height() <= 0)
    {
        detachThumbnail();
        return;
    }

    auto* const mainCamera = impl_->renderer->GetActiveCamera();
    if(mainCamera->GetParallelScale() >= impl_->fitParallelScale * 0.995)
    {
        detachThumbnail();
        return;
    }

    double bounds[6]{};
    impl_->imageSlice->GetBounds(bounds);
    const double imageWidth = bounds[1] - bounds[0];
    const double imageHeight = bounds[3] - bounds[2];
    if(imageWidth <= 0.0 || imageHeight <= 0.0)
    {
        detachThumbnail();
        return;
    }

    const double widgetWidth = static_cast<double>(impl_->widget->width());
    const double widgetHeight = static_cast<double>(impl_->widget->height());
    const double maximumThumbnailSide = std::min(
        160.0, 0.30 * std::min(widgetWidth, widgetHeight));
    double thumbnailWidth = maximumThumbnailSide;
    double thumbnailHeight = maximumThumbnailSide;
    if(imageWidth >= imageHeight)
    {
        thumbnailHeight *= imageHeight / imageWidth;
    }
    else
    {
        thumbnailWidth *= imageWidth / imageHeight;
    }
    constexpr double marginPixels = 5.0;
    impl_->zoomThumbnailRenderer->SetViewport(
        marginPixels / widgetWidth,
        marginPixels / widgetHeight,
        (marginPixels + thumbnailWidth) / widgetWidth,
        (marginPixels + thumbnailHeight) / widgetHeight);

    constexpr double borderDepth = 0.2;
    constexpr double viewportDepth = 0.3;
    setRectangle(
        *impl_->zoomThumbnailBorder,
        bounds[0],
        bounds[2],
        bounds[1],
        bounds[3],
        borderDepth);

    double focalPoint[3]{};
    mainCamera->GetFocalPoint(focalPoint);
    const double halfHeight = mainCamera->GetParallelScale();
    const double halfWidth = halfHeight * impl_->renderer->GetTiledAspectRatio();
    setRectangle(
        *impl_->zoomThumbnailViewport,
        focalPoint[0] - halfWidth,
        focalPoint[1] - halfHeight,
        focalPoint[0] + halfWidth,
        focalPoint[1] + halfHeight,
        viewportDepth);

    auto* const thumbnailCamera =
        impl_->zoomThumbnailRenderer->GetActiveCamera();
    const double centerX = (bounds[0] + bounds[1]) / 2.0;
    const double centerY = (bounds[2] + bounds[3]) / 2.0;
    thumbnailCamera->ParallelProjectionOn();
    thumbnailCamera->SetFocalPoint(centerX, centerY, 0.0);
    thumbnailCamera->SetPosition(centerX, centerY, 1.0);
    thumbnailCamera->SetViewUp(0.0, 1.0, 0.0);
    thumbnailCamera->SetParallelScale(imageHeight * 0.525);
    impl_->zoomThumbnailRenderer->ResetCameraClippingRange();
    impl_->zoomThumbnailImageSlice->SetVisibility(true);

    if(!impl_->zoomThumbnailAttached)
    {
        impl_->renderWindow->AddRenderer(impl_->zoomThumbnailRenderer);
        impl_->zoomThumbnailAttached = true;
    }
}

void VtkViewport::zoomBy(const double factor)
{
    if(!impl_->sliceGeometry || !std::isfinite(factor) || factor <= 0.0)
    {
        return;
    }
    auto* const camera = impl_->renderer->GetActiveCamera();
    const double currentScale = camera->GetParallelScale();
    const double maximumExtent = std::max(
        static_cast<double>(impl_->sliceGeometry->width()),
        static_cast<double>(impl_->sliceGeometry->height()))
        * impl_->sliceGeometry->outputSpacing();
    const double nextScale = std::clamp(
        currentScale / factor,
        impl_->sliceGeometry->outputSpacing() * 0.5,
        maximumExtent * 10.0);
    camera->SetParallelScale(nextScale);
    impl_->renderer->ResetCameraClippingRange();
    updateCrosshair();
    updateMeasurementOverlay();
    updateZoomThumbnail();
    impl_->renderWindow->Render();
}

bool VtkViewport::eventFilter(QObject* const watched, QEvent* const event)
{
    if(watched != impl_->widget)
    {
        return QWidget::eventFilter(watched, event);
    }
    if(event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease)
    {
        return true;
    }
    if(!impl_->sliceGeometry)
    {
        return QWidget::eventFilter(watched, event);
    }

    try
    {
        const auto panFromZoomThumbnail = [this](
                                                  const double horizontal,
                                                  const double vertical) {
            auto* const camera = impl_->renderer->GetActiveCamera();
            double position[3]{};
            double focalPoint[3]{};
            camera->GetPosition(position);
            camera->GetFocalPoint(focalPoint);
            const double nextFocalX = horizontal
                + impl_->zoomThumbnailDragOffset.x();
            const double nextFocalY = vertical
                + impl_->zoomThumbnailDragOffset.y();
            camera->SetPosition(
                nextFocalX,
                nextFocalY,
                position[2]);
            camera->SetFocalPoint(
                nextFocalX,
                nextFocalY,
                focalPoint[2]);
            impl_->renderer->ResetCameraClippingRange();
            updateZoomThumbnail();
            impl_->renderWindow->Render();
        };

        if(event->type() == QEvent::Resize)
        {
            updateCrosshair();
            updateMeasurementOverlay();
            updateZoomThumbnail();
            impl_->renderWindow->Render();
            return false;
        }
        if(event->type() == QEvent::MouseButtonPress)
        {
            const auto* const mouseEvent = static_cast<QMouseEvent*>(event);
            impl_->lastMousePosition = mouseEvent->position();
            double thumbnailHorizontal = 0.0;
            double thumbnailVertical = 0.0;
            if(mouseEvent->button() == Qt::LeftButton
               && mapWidgetToZoomThumbnail(
                   mouseEvent->position(),
                   thumbnailHorizontal,
                   thumbnailVertical,
                   false))
            {
                double focalPoint[3]{};
                auto* const camera = impl_->renderer->GetActiveCamera();
                camera->GetFocalPoint(focalPoint);
                const double halfHeight = camera->GetParallelScale();
                const double halfWidth =
                    halfHeight * impl_->renderer->GetTiledAspectRatio();
                const bool pressedInsideViewport =
                    thumbnailHorizontal >= focalPoint[0] - halfWidth
                    && thumbnailHorizontal <= focalPoint[0] + halfWidth
                    && thumbnailVertical >= focalPoint[1] - halfHeight
                    && thumbnailVertical <= focalPoint[1] + halfHeight;
                impl_->zoomThumbnailDragOffset = pressedInsideViewport
                    ? QPointF(
                        focalPoint[0] - thumbnailHorizontal,
                        focalPoint[1] - thumbnailVertical)
                    : QPointF{};
                impl_->dragMode = DragMode::ZoomThumbnailPan;
                setPanGrabCursor(true);
                panFromZoomThumbnail(
                    thumbnailHorizontal, thumbnailVertical);
                return true;
            }
            if(mouseEvent->button() == Qt::RightButton
               || (mouseEvent->button() == Qt::LeftButton
                   && mouseEvent->modifiers().testFlag(Qt::ControlModifier)))
            {
                impl_->dragMode = DragMode::Zoom;
            }
            else if(mouseEvent->button() == Qt::MiddleButton
                    || (mouseEvent->button() == Qt::LeftButton
                        && mouseEvent->modifiers().testFlag(Qt::AltModifier)))
            {
                impl_->dragMode = DragMode::Pan;
                setPanGrabCursor(true);
            }
            else if(mouseEvent->button() == Qt::LeftButton
                    && mouseEvent->modifiers().testFlag(Qt::ShiftModifier))
            {
                if(impl_->interactionMode == InteractionMode::Contrast)
                {
                    impl_->dragMode = DragMode::LabelOpacity;
                    emit labelOpacityDragStarted();
                }
                else
                {
                    impl_->dragMode = DragMode::Contrast;
                    emit windowLevelDragStarted();
                }
            }
            else if(mouseEvent->button() == Qt::LeftButton)
            {
                switch(impl_->interactionMode)
                {
                case InteractionMode::Crosshair:
                    impl_->dragMode = DragMode::Cursor;
                    selectAt(mouseEvent->position());
                    break;
                case InteractionMode::Zoom:
                    impl_->dragMode = DragMode::Zoom;
                    break;
                case InteractionMode::Pan:
                    impl_->dragMode = DragMode::Pan;
                    setPanGrabCursor(true);
                    break;
                case InteractionMode::Contrast:
                    impl_->dragMode = DragMode::Contrast;
                    emit windowLevelDragStarted();
                    break;
                case InteractionMode::Measure:
                    impl_->dragMode = DragMode::Measuring;
                    measureAt(mouseEvent->position(), true);
                    break;
                case InteractionMode::Brush:
                case InteractionMode::Erase:
                    impl_->dragMode = DragMode::Editing;
                    editAt(mouseEvent->position(), true);
                    break;
                case InteractionMode::ScopedErase:
                    impl_->dragMode = DragMode::ScopedErasing;
                    editAt(mouseEvent->position(), true);
                    break;
                }
            }
            return impl_->dragMode != DragMode::None;
        }
        if(event->type() == QEvent::MouseMove)
        {
            const auto* const mouseEvent = static_cast<QMouseEvent*>(event);
            if(impl_->dragMode == DragMode::ZoomThumbnailPan)
            {
                double thumbnailHorizontal = 0.0;
                double thumbnailVertical = 0.0;
                if(mapWidgetToZoomThumbnail(
                       mouseEvent->position(),
                       thumbnailHorizontal,
                       thumbnailVertical,
                       true))
                {
                    panFromZoomThumbnail(
                        thumbnailHorizontal, thumbnailVertical);
                }
                impl_->lastMousePosition = mouseEvent->position();
                return true;
            }
            hoverAt(mouseEvent->position());
            if(impl_->dragMode == DragMode::None)
            {
                return false;
            }
            if(impl_->dragMode == DragMode::Cursor)
            {
                selectAt(mouseEvent->position());
            }
            else if(impl_->dragMode == DragMode::Zoom)
            {
                const double delta =
                    impl_->lastMousePosition.y() - mouseEvent->position().y();
                zoomBy(std::pow(1.01, delta));
            }
            else if(impl_->dragMode == DragMode::Pan)
            {
                double previousHorizontal = 0.0;
                double previousVertical = 0.0;
                double currentHorizontal = 0.0;
                double currentVertical = 0.0;
                if(mapWidgetToSlice(
                       impl_->lastMousePosition,
                       previousHorizontal,
                       previousVertical,
                       false)
                   && mapWidgetToSlice(
                       mouseEvent->position(),
                       currentHorizontal,
                       currentVertical,
                       false))
                {
                    const double translation[3]{
                        previousHorizontal - currentHorizontal,
                        previousVertical - currentVertical,
                        0.0,
                    };
                    auto* const camera = impl_->renderer->GetActiveCamera();
                    double position[3]{};
                    double focalPoint[3]{};
                    camera->GetPosition(position);
                    camera->GetFocalPoint(focalPoint);
                    camera->SetPosition(
                        position[0] + translation[0],
                        position[1] + translation[1],
                        position[2]);
                    camera->SetFocalPoint(
                        focalPoint[0] + translation[0],
                        focalPoint[1] + translation[1],
                        focalPoint[2]);
                    impl_->renderer->ResetCameraClippingRange();
                    updateZoomThumbnail();
                    impl_->renderWindow->Render();
                }
            }
            else if(impl_->dragMode == DragMode::Contrast)
            {
                const double width = impl_->widget->width();
                const double height = impl_->widget->height();
                if(width > 0.0 && height > 0.0)
                {
                    const double normalizedDx =
                        (mouseEvent->position().x() - impl_->lastMousePosition.x())
                        / width;
                    const double normalizedDy =
                        (impl_->lastMousePosition.y() - mouseEvent->position().y())
                        / height;
                    emit windowLevelDragged(normalizedDx, normalizedDy);
                }
                return true;
            }
            else if(impl_->dragMode == DragMode::LabelOpacity)
            {
                const double height = impl_->widget->height();
                if(height > 0.0)
                {
                    const double normalizedDy =
                        (impl_->lastMousePosition.y() - mouseEvent->position().y())
                        / height;
                    emit labelOpacityDragged(normalizedDy);
                }
                return true;
            }
            else if(impl_->dragMode == DragMode::Editing)
            {
                editAt(mouseEvent->position(), false);
            }
            else if(impl_->dragMode == DragMode::Measuring)
            {
                measureAt(mouseEvent->position(), false);
            }
            impl_->lastMousePosition = mouseEvent->position();
            return true;
        }
        if(event->type() == QEvent::Leave)
        {
            hideSamplingBoundary();
            emit pointerExited();
            return false;
        }
        if(event->type() == QEvent::MouseButtonRelease)
        {
            const bool finishedEditing = impl_->dragMode == DragMode::Editing;
            const bool finishedPan = impl_->dragMode == DragMode::Pan
                || impl_->dragMode == DragMode::ZoomThumbnailPan;
            impl_->dragMode = DragMode::None;
            if(finishedPan)
            {
                setPanGrabCursor(false);
            }
            if(finishedEditing)
            {
                emit editStrokeFinished();
            }
            return true;
        }
        if(event->type() == QEvent::Wheel)
        {
            const auto* const wheelEvent = static_cast<QWheelEvent*>(event);
            impl_->wheelAngleRemainder += wheelEvent->angleDelta().y();
            const int steps = impl_->wheelAngleRemainder / 120;
            impl_->wheelAngleRemainder %= 120;
            if(steps != 0)
            {
                emit sliceStepRequested(steps);
            }
            return true;
        }
    }
    catch(const std::exception& exception)
    {
        impl_->dragMode = DragMode::None;
        setPanGrabCursor(false);
        qWarning().noquote()
            << "[RENDER] Slice interaction failed:" << exception.what();
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace radmarky::rendering
