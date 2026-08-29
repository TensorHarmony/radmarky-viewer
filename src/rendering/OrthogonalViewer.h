#pragma once

#include "core/OrthogonalSliceGeometry.h"

#include <QString>
#include <QStringList>
#include <QList>
#include <QWidget>

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

class QImage;

namespace radmarky::core
{
class Annotation;
class Volume;
enum class BrushShape;
}

namespace radmarky::rendering
{

class OrthogonalViewer final : public QWidget
{
    Q_OBJECT

public:
    explicit OrthogonalViewer(QWidget* parent = nullptr);
    ~OrthogonalViewer() override;

    OrthogonalViewer(const OrthogonalViewer&) = delete;
    OrthogonalViewer& operator=(const OrthogonalViewer&) = delete;

    void setVolume(const std::shared_ptr<const core::Volume>& volume);
    void addAnnotation(const std::shared_ptr<core::Annotation>& annotation);
    void removeAnnotation(std::size_t index);
    void setAnnotationOpacity(std::size_t index, double opacity);
    void setAnnotationVisibility(std::size_t index, bool visible);
    void setOverallLabelOpacity(double opacity);
    void setAnnotationSelection(
        const std::vector<std::size_t>& selectedIndices);
    void clearVolume();
    [[nodiscard]] QImage captureMiddleSliceThumbnail();
    void setCrosshairTool();
    void setZoomTool();
    void setPanTool();
    void setContrastTool();
    void setMeasureTool();
    void setBrushTool();
    void setEraseTool();
    void setScopedEraseTool();
    void setActiveLabel(int label);
    void setBrushRadius(int radius);
    void setBrushShape(core::BrushShape shape);
    void setPaintOver(int selection);
    void undoAnnotationEdit();
    void redoAnnotationEdit();
    void zoomAllIn();
    void zoomAllOut();
    void panActiveView(double horizontalDirection, double verticalDirection);
    void resetActiveView();
    void resetAllViews();
    void showAllViews();
    void focusAxialView();
    void focusSagittalView();
    void focusCoronalView();
    void exportSliceAnimation();
    // Navigates to a one-based index along the volume's third dimension, which
    // is the slice numbering used by Python validators.
    void goToAxialSlice(int sliceNumber);
    void setWindowLevel(double window, double level);
    void applyAutomaticWindowLevel();
    void applyWindowLevelPreset(int preset);
    void resetWindowLevel();
    void setInverted(bool inverted);
    void setSamplingRadius(int samplingRadius);

    struct LayoutState
    {
        QList<int> viewSplitterSizes;
        QList<int> rightViewSplitterSizes;
        std::optional<core::SliceOrientation> focusedView;
    };
    [[nodiscard]] LayoutState layoutState() const;
    void setLayoutState(const LayoutState& state);

signals:
    void cursorChanged(
        double physicalX,
        double physicalY,
        double physicalZ,
        double indexX,
        double indexY,
        double indexZ);
    void cursorInspectionChanged(
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
        int totalAxialSlices);
    void windowLevelChanged(
        double window,
        double level,
        double intensityMinimum,
        double intensityMaximum);
    void annotationEditingStateChanged(
        bool editable, bool canUndo, bool canRedo);
    void annotationLabelsChanged(const QList<int>& labels);
    void overallLabelOpacityChanged(double opacity);

private:
    void selectPhysicalPoint(double x, double y, double z);
    void inspectPhysicalPoint(
        double x,
        double y,
        double z,
        core::SliceOrientation orientation);
    void restoreCrosshairInspection();
    void beginWindowLevelDrag();
    void updateWindowLevelDrag(double normalizedDx, double normalizedDy);
    void beginLabelOpacityDrag();
    void updateLabelOpacityDrag(double normalizedDy);
    void stepSlice(core::SliceOrientation orientation, int steps);
    void selectNormalPosition(core::SliceOrientation orientation, int position);
    void publishCursor();
    void publishWindowLevel();
    void publishAnnotationLabels();
    void toggleFocusedView(core::SliceOrientation orientation);
    void saveScreenshot(core::SliceOrientation orientation);
    void beginEditStroke(double x, double y, double z);
    void continueEditStroke(double x, double y, double z);
    void finishEditStroke();
    void refreshEditedAnnotation();
    void publishAnnotationEditingState();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace radmarky::rendering
