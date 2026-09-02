#pragma once

#include "core/ImageGeometry.h"
#include "core/OrthogonalSliceGeometry.h"

#include <QWidget>

#include <cstddef>
#include <memory>
#include <optional>

class vtkImageData;
class QImage;
class QPointF;

namespace radmarky::rendering
{

class VtkViewport final : public QWidget
{
    Q_OBJECT

public:
    enum class InteractionMode
    {
        Crosshair,
        Zoom,
        Pan,
        Contrast,
        Measure,
        Brush,
        Erase,
        ScopedErase,
    };

    explicit VtkViewport(
        core::SliceOrientation orientation,
        QWidget* parent = nullptr);
    ~VtkViewport() override;

    VtkViewport(const VtkViewport&) = delete;
    VtkViewport& operator=(const VtkViewport&) = delete;

    void setInput(
        vtkImageData* imageData,
        const core::ImageGeometry& geometry,
        const core::ImageGeometry::Vector& cursorPhysical,
        core::SliceAlignment alignment = core::SliceAlignment::Patient);
    void setSliceAlignment(core::SliceAlignment alignment);
    void addAnnotation(
        vtkImageData* imageData,
        bool labelMap,
        double scalarMinimum,
        double scalarMaximum,
        double opacity);
    void removeAnnotation(std::size_t index);
    void setAnnotationOpacity(std::size_t index, double opacity);
    void setAnnotationVisibility(std::size_t index, bool visible);
    void setAnnotationHiddenIndicatorVisible(bool visible);
    void annotationDataModified(std::size_t index);
    void setAnnotationComparison(vtkImageData* imageData, double opacity);
    void clearAnnotationComparison();
    void setAnnotationComparisonOpacity(double opacity);
    void setAnnotationComparisonVisibility(bool visible);
    void clearInput();
    void setCursor(const core::ImageGeometry::Vector& cursorPhysical);
    void setSliceCounter(int currentSlice, int totalSlices);
    void setWindowLevel(double window, double level);
    void setInverted(bool inverted);
    void setSamplingSideLength(int sideLength);
    void setBrushRadius(int radius);
    void setBrushShape(bool circular);
    void setBrushLabel(int label);
    void setEraseTargetLabel(int label);
    void setInteractionMode(InteractionMode mode);
    void zoomIn();
    void zoomOut();
    void panBy(double horizontalDirection, double verticalDirection);
    void resetView();
    QImage captureSliceImage(bool showCrosshair, bool respectZoom);
    QImage captureImageWithoutCrosshair();

signals:
    void physicalPointSelected(double x, double y, double z);
    void physicalPointHovered(double x, double y, double z);
    void pointerExited();
    void sliceStepRequested(int steps);
    void windowLevelDragStarted();
    void windowLevelDragged(double normalizedDx, double normalizedDy);
    void labelOpacityDragStarted();
    void labelOpacityDragged(double normalizedDy);
    void editStrokeStarted(double x, double y, double z);
    void editStrokeContinued(double x, double y, double z);
    void editStrokeFinished();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    bool mapWidgetToSlice(
        const QPointF& widgetPosition,
        double& horizontal,
        double& vertical,
        bool clampToImage) const;
    bool mapWidgetToZoomThumbnail(
        const QPointF& widgetPosition,
        double& horizontal,
        double& vertical,
        bool clampToThumbnail) const;
    void selectAt(const QPointF& widgetPosition);
    void hoverAt(const QPointF& widgetPosition);
    void editAt(const QPointF& widgetPosition, bool firstPoint);
    void measureAt(const QPointF& widgetPosition, bool firstPoint);
    void clearMeasurement();
    void updateMeasurementOverlay();
    void updateCrosshair();
    void updateSamplingBoundary(
        const core::ImageGeometry::Vector& inspectedPhysical);
    void updateBrushOutline(
        const core::ImageGeometry::Vector& inspectedPhysical);
    void updateBrushOutlineAppearance();
    void updateAnnotationHiddenIndicatorPosition();
    void updateZoomThumbnail();
    void hideSamplingBoundary();
    void zoomBy(double factor);
    void applyInteractionCursor();
    void setPanGrabCursor(bool grabbing);
    [[nodiscard]] std::optional<double> displayedLuminanceAt(
        const QPointF& widgetPosition) const;
    void updateAdaptiveCursorTone(const QPointF& widgetPosition);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace radmarky::rendering
