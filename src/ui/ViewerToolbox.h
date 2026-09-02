#pragma once

#include "app/UserSettings.h"
#include "core/AnnotationEditor.h"

#include <QString>
#include <QStringList>
#include <QWidget>
#include <QList>

#include <map>
#include <vector>

class QComboBox;
class QDoubleSpinBox;
class QFrame;
class QIntValidator;
class QLabel;
class QLineEdit;
class QScrollArea;
class QStackedWidget;
class QTableWidget;
class QSpinBox;
class QSlider;
class QToolButton;

namespace radmarky::ui
{

class ViewerToolbox final : public QWidget
{
    Q_OBJECT

public:
    explicit ViewerToolbox(QWidget* parent = nullptr);

    void setCursorInspection(
        const QString& x,
        const QString& y,
        const QString& z,
        const QString& previousSpacing,
        const QString& nextSpacing,
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
    void setWindowLevel(
        double window,
        double level,
        double intensityMinimum,
        double intensityMaximum);
    void setNamedWindowLevelPresets(
        const std::vector<app::WindowLevelSetting>& presets);
    void addAnnotation(
        const QString& name, const QString& kind, double opacity, bool visible);
    void setAnnotationName(int index, const QString& name);
    void removeAnnotation(int index);
    void clearAnnotations();
    void selectAnnotation(int index);
    void setAnnotationOpacity(int index, double opacity);
    void setAnnotationVisibility(int index, bool visible);
    void setOverallLabelOpacity(double opacity);
    void setAnnotationLabels(const QList<int>& labels);
    void setActiveLabel(int label);
    [[nodiscard]] int activeLabel() const;
    void setBrushRadii(const std::map<int, int>& radii);
    void setPaintOverSelections(const std::map<int, int>& selections);
    [[nodiscard]] bool setPaintOverSelection(int selection);
    void adjustBrushRadius(int delta);
    void setAnnotationEditingState(
        bool editable, bool canUndo, bool canRedo);
    void clearVolume();
    [[nodiscard]] QList<int> selectedAnnotationIndices() const;
    [[nodiscard]] QWidget* identityPanel() const noexcept;
    [[nodiscard]] QWidget* contrastPanel() const noexcept;
    [[nodiscard]] QWidget* annotationsPanel() const noexcept;
    [[nodiscard]] QWidget* annotationLabelsPanel() const noexcept;
    [[nodiscard]] QWidget* cursorInspectorPanel() const noexcept;

signals:
    void windowLevelEdited(double window, double level);
    void automaticWindowLevelRequested();
    void windowLevelPresetSelected(int preset);
    void windowLevelDefaultRequested(double window, double level);
    void windowLevelPresetSaveRequested(
        const QString& name, double window, double level);
    void samplingRadiusChanged(int samplingRadius);
    void axialSliceEdited(int sliceNumber);
    void annotationCreationRequested();
    void annotationLoadRequested();
    void annotationRemovalRequested(int index);
    void annotationOpacityChanged(int index, double opacity);
    void annotationVisibilityChanged(int index, bool visible);
    void annotationSelectionChanged(const QList<int>& indices);
    void activeLabelChanged(int label);
    void paintOverChanged(int selection);
    void paintOverPreferenceChanged(int label, int selection);
    void brushRadiusChanged(int radius);
    void brushRadiusPreferenceChanged(int label, int radius);
    void brushShapeChanged(core::BrushShape shape);
    void overallLabelOpacityChanged(double opacity);

private:
    void updateAnnotationEmptyState();
    void rebuildPaintOverOptions();
    void applyPaintOverForActiveLabel();

    QLineEdit* cursorX_ = nullptr;
    QLineEdit* cursorY_ = nullptr;
    QLineEdit* cursorZ_ = nullptr;
    QLineEdit* cursorPreviousSpacing_ = nullptr;
    QLineEdit* cursorNextSpacing_ = nullptr;
    QIntValidator* cursorSliceValidator_ = nullptr;
    QLabel* intensityValue_ = nullptr;
    QLabel* maximumIntensityValue_ = nullptr;
    QLabel* meanIntensityValue_ = nullptr;
    QLabel* medianIntensityValue_ = nullptr;
    QLabel* minimumIntensityValue_ = nullptr;
    QFrame* cursorIntensityTable_ = nullptr;
    QTableWidget* annotationTable_ = nullptr;
    QStackedWidget* annotationStack_ = nullptr;
    QWidget* annotationEmptyState_ = nullptr;
    QLineEdit* labelValue_ = nullptr;
    QLineEdit* labelName_ = nullptr;
    QComboBox* windowLevelPreset_ = nullptr;
    QDoubleSpinBox* minimumSpin_ = nullptr;
    QDoubleSpinBox* maximumSpin_ = nullptr;
    QDoubleSpinBox* windowSpin_ = nullptr;
    QDoubleSpinBox* levelSpin_ = nullptr;
    QWidget* identityPage_ = nullptr;
    QWidget* contrastGroup_ = nullptr;
    QWidget* annotationsGroup_ = nullptr;
    QWidget* annotationLabelsGroup_ = nullptr;
    QComboBox* activeLabelCombo_ = nullptr;
    QComboBox* paintOverCombo_ = nullptr;
    QFrame* activeLabelColorSwatch_ = nullptr;
    QSpinBox* brushRadiusSpin_ = nullptr;
    QSlider* brushRadiusSlider_ = nullptr;
    QToolButton* squareBrushButton_ = nullptr;
    QToolButton* circleBrushButton_ = nullptr;
    QSpinBox* labelOpacitySpin_ = nullptr;
    QSlider* labelOpacitySlider_ = nullptr;
    QWidget* cursorInspectorGroup_ = nullptr;
    QStackedWidget* contentStack_ = nullptr;
    QScrollArea* controlsScroll_ = nullptr;
    QWidget* controlsPage_ = nullptr;
    std::map<int, int> brushRadii_;
    std::map<int, int> paintOverSelections_;
    QList<int> annotationLabels_;
    std::vector<app::WindowLevelSetting> namedPresets_;
};

} // namespace radmarky::ui
