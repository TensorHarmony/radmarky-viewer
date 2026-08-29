#pragma once

#include "app/UserSettings.h"

#include <QList>
#include <QMainWindow>
#include <QStringList>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <utility>
#include <vector>

class QDragEnterEvent;
class QDragLeaveEvent;
class QDropEvent;
class QCloseEvent;
class QDockWidget;
class QFrame;
class QLabel;
class QProgressDialog;
class QStackedWidget;
class QTreeWidget;
class QAction;
class QTemporaryDir;
class QToolButton;

namespace radmarky::core
{
class Annotation;
class Volume;
}

namespace radmarky::rendering
{
class OrthogonalViewer;
}

namespace radmarky::validation
{
class AnnotationValidationService;
struct AnnotationValidationResult;
}

namespace radmarky::ui
{

class ViewerToolbox;

class MainWindow final : public QMainWindow
{
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    struct RecentAnnotationsState
    {
        RecentAnnotationsState() : activeLabel(1) {}
        RecentAnnotationsState(
            QStringList files,
            std::vector<app::RecentAnnotationSetting> annotationSettings,
            const int label)
            : sourceFiles(std::move(files)),
              annotations(std::move(annotationSettings)),
              activeLabel(label)
        {
        }

        QStringList sourceFiles;
        std::vector<app::RecentAnnotationSetting> annotations;
        int activeLabel;
    };

    void openImages();
    void openAnnotations();
    void showDicomHeader();
    void showValidationManagement();
    void validateOpenAnnotation();
    void showShortcuts();
    void showAbout();
    void createNewAnnotation();
    void saveSelectedAnnotation();
    void saveSelectedAnnotationAs();
    void closeImages();
    void loadInputs(
        const QStringList& fileNames,
        RecentAnnotationsState recentAnnotations = {});
    void loadAnnotations(const QStringList& fileNames);
    void loadNifti(
        const QString& fileName,
        RecentAnnotationsState recentAnnotations = {});
    void startDicomImport(
        const std::vector<std::filesystem::path>& filePaths,
        const QString& sourceName,
        const QStringList& sourceFiles,
        const std::shared_ptr<std::atomic_bool>& cancellation,
        RecentAnnotationsState recentAnnotations = {},
        std::shared_ptr<QTemporaryDir> extractionDirectory = {});
    void startArchiveDicomImport(
        const std::filesystem::path& archivePath,
        const QString& sourceName,
        const QStringList& sourceFiles,
        const std::shared_ptr<std::atomic_bool>& cancellation,
        RecentAnnotationsState recentAnnotations,
        std::shared_ptr<QTemporaryDir> extractionDirectory);
    void displayVolume(
        std::shared_ptr<core::Volume> volume,
        const QString& sourceName,
        const QString& sourceKind,
        const QStringList& sourceFiles,
        RecentAnnotationsState recentAnnotations = {});
    void restoreRecentAnnotations(RecentAnnotationsState recentAnnotations);
    void rememberRecentImage(
        const QString& sourceName,
        const QString& sourceKind,
        const QStringList& sourceFiles);
    void refreshRecentImages();
    void openRecentImage(int index);
    void showRecentImageInFolder(const QString& filePath);
    void beginImportProgress(const QString& label);
    void enableImportCancellation(
        const std::shared_ptr<std::atomic_bool>& cancellation);
    void updateImportProgress(int value, const QString& label = {});
    void finishImportProgress();
    void queueDroppedInputs(const QStringList& inputs);
    void removeAnnotation(int index);
    void setAnnotationOpacity(int index, double opacity);
    void setAnnotationVisibility(int index, bool visible);
    void setAnnotationsTemporarilyHidden(bool hidden);
    void setAnnotationSelection(const QList<int>& indices);
    void applyTheme(bool dark);
    void setDropActive(bool active);
    void showCursorStatus(
        double physicalX,
        double physicalY,
        double physicalZ,
        double indexX,
        double indexY,
        double indexZ);
    void setImageDependentActionsEnabled(bool enabled);
    void updateViewerShortcutActions();
    void updateActiveLabelActions();
    void activateAnnotationDigit(int digit);
    [[nodiscard]] bool ensureEditableAnnotationForShortcut();
    [[nodiscard]] bool canUseAnnotationDigitShortcuts() const;
    [[nodiscard]] std::shared_ptr<core::Annotation> selectedEditableAnnotation()
        const;
    [[nodiscard]] validation::AnnotationValidationResult runValidation(
        const std::shared_ptr<core::Annotation>& annotation,
        const QString& destinationPath,
        const std::vector<app::ValidationScriptSetting>& scripts,
        bool finalizeSave);
    void validateSelectedAnnotation(
        const std::vector<app::ValidationScriptSetting>& scripts);
    [[nodiscard]] bool saveAnnotation(
        const std::shared_ptr<core::Annotation>& annotation,
        bool chooseDestination);
    [[nodiscard]] bool saveAnnotationTo(
        const std::shared_ptr<core::Annotation>& annotation,
        const QString& destinationPath,
        bool updateSourcePath);
    [[nodiscard]] bool resolveUnsavedAnnotations(
        const std::vector<std::shared_ptr<core::Annotation>>& candidates,
        const QString& operation);
    void saveWindowLayout();
    void restoreWindowLayout();
    void rememberImageDockLayout();
    void applyImageDockLayout();
    void applyEmptyPageDockVisibility();

    rendering::OrthogonalViewer* viewer_ = nullptr;
    ViewerToolbox* toolbox_ = nullptr;
    QDockWidget* identityDock_ = nullptr;
    QDockWidget* contrastDock_ = nullptr;
    QDockWidget* annotationsDock_ = nullptr;
    QDockWidget* annotationLabelsDock_ = nullptr;
    QDockWidget* cursorInspectorDock_ = nullptr;
    QStackedWidget* centralStack_ = nullptr;
    QLabel* cursorStatusLabel_ = nullptr;
    QLabel* volumeDimensionsLabel_ = nullptr;
    QTreeWidget* recentImages_ = nullptr;
    QProgressDialog* importProgress_ = nullptr;
    QAction* closeImagesAction_ = nullptr;
    QAction* openAnnotationsAction_ = nullptr;
    QAction* dicomHeaderAction_ = nullptr;
    QAction* validationManagementAction_ = nullptr;
    QAction* validateAnnotationAction_ = nullptr;
    QAction* invertAction_ = nullptr;
    QAction* keepOnTopAction_ = nullptr;
    QAction* measureAction_ = nullptr;
    QAction* newAnnotationAction_ = nullptr;
    QAction* saveAnnotationAction_ = nullptr;
    QAction* saveAnnotationAsAction_ = nullptr;
    QAction* brushAction_ = nullptr;
    QAction* eraseAction_ = nullptr;
    QAction* scopedEraseAction_ = nullptr;
    QAction* undoEditAction_ = nullptr;
    QAction* redoEditAction_ = nullptr;
    QAction* annotationVisibilityAction_ = nullptr;
    QList<QAction*> activeLabelActions_;
    QList<QAction*> viewerShortcutActions_;
    QList<QAction*> imageDependentActions_;
    std::vector<std::weak_ptr<core::Annotation>> annotationsVisibleBeforeHide_;
    QToolButton* layoutButton_ = nullptr;
    std::shared_ptr<core::Volume> primaryVolume_;
    std::vector<std::shared_ptr<core::Annotation>> annotations_;
    app::UserSettings settings_;
    std::unique_ptr<validation::AnnotationValidationService> validationService_;
    QStringList primarySourceFiles_;
    QString primarySourceName_;
    QString primarySourceKind_;
    std::shared_ptr<std::atomic_bool> importCancellation_;
    std::uint64_t importGeneration_ = 0;
    std::uint64_t newAnnotationSequence_ = 0;
    bool darkTheme_ = false;
};

} // namespace radmarky::ui
