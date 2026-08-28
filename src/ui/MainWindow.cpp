#include "ui/MainWindow.h"

#include "app/ApplicationInfo.h"
#include "core/Annotation.h"
#include "core/OrthogonalSliceGeometry.h"
#include "core/Volume.h"
#include "io/ArchiveExtractor.h"
#include "io/DicomReader.h"
#include "io/DicomSeries.h"
#include "io/NiftiReader.h"
#include "io/NiftiWriter.h"
#include "rendering/OrthogonalViewer.h"
#include "ui/AboutDialog.h"
#include "ui/DicomSeriesDialog.h"
#include "ui/DicomHeaderDialog.h"
#include "ui/UiTheme.h"
#include "ui/ValidationManagementDialog.h"
#include "ui/ViewerToolbox.h"
#include "validation/AnnotationValidationService.h"

#include <QAbstractSpinBox>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QComboBox>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDebug>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QFrame>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QImage>
#include <QImageWriter>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QProgressDialog>
#include <QProgressBar>
#include <QTextEdit>
#include <QPointer>
#include <QPixmap>
#include <QPushButton>
#include <QStackedWidget>
#include <QStatusBar>
#include <QString>
#include <QStyle>
#include <QTemporaryDir>
#include <QToolBar>
#include <QToolButton>
#include <QTimer>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <QtConcurrentRun>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <chrono>
#include <optional>
#include <stdexcept>
#include <utility>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace radmarky::ui
{
namespace
{

using ImportClock = std::chrono::steady_clock;

class AnnotationOperationProgressDialog final : public QDialog
{
public:
    AnnotationOperationProgressDialog(
        const bool saving,
        const int validationCount,
        QWidget* const parent)
        : QDialog(parent)
    {
        setObjectName(QStringLiteral("annotationOperationProgress"));
        setWindowTitle(
            saving
                ? (validationCount > 0
                       ? tr("Saving and Validating Annotation")
                       : tr("Saving Annotation"))
                : tr("Validating Annotation"));
        setWindowModality(Qt::ApplicationModal);
        setMinimumWidth(460);

        auto* const layout = new QVBoxLayout(this);
        layout->setContentsMargins(18, 16, 18, 14);
        layout->setSpacing(8);

        if(saving)
        {
            savingLabel_ = new QLabel(tr("Saving annotation…"), this);
            savingLabel_->setObjectName(QStringLiteral("annotationSavingLabel"));
            savingProgress_ = new QProgressBar(this);
            savingProgress_->setObjectName(
                QStringLiteral("annotationSavingProgressBar"));
            savingProgress_->setAccessibleName(tr("Annotation saving progress"));
            savingProgress_->setRange(0, 0);
            savingProgress_->setTextVisible(false);
            layout->addWidget(savingLabel_);
            layout->addWidget(savingProgress_);
        }

        if(!saving || validationCount > 0)
        {
            validationLabel_ = new QLabel(
                saving ? tr("Waiting to validate annotation…")
                       : tr("Preparing annotation validation…"),
                this);
            validationLabel_->setObjectName(
                QStringLiteral("annotationValidationLabel"));
            validationProgress_ = new QProgressBar(this);
            validationProgress_->setObjectName(
                QStringLiteral("annotationValidationProgressBar"));
            validationProgress_->setAccessibleName(
                tr("Annotation validation progress"));
            validationProgress_->setRange(0, std::max(1, validationCount));
            validationProgress_->setValue(0);
            validationProgress_->setFormat(tr("%v of %m validators completed"));
            layout->addWidget(validationLabel_);
            layout->addWidget(validationProgress_);
        }

        auto* const buttons = new QDialogButtonBox(
            QDialogButtonBox::Cancel, Qt::Horizontal, this);
        cancelButton_ = buttons->button(QDialogButtonBox::Cancel);
        layout->addSpacing(4);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::rejected, this, [this] {
            cancelButton_->setEnabled(false);
            if(validationLabel_ != nullptr)
            {
                validationLabel_->setText(tr("Cancelling validation…"));
            }
            else if(savingLabel_ != nullptr)
            {
                savingLabel_->setText(tr("Cancelling save…"));
            }
        });
    }

    [[nodiscard]] QPushButton* cancelButton() const noexcept
    {
        return cancelButton_;
    }

    void beginValidation(
        const int current,
        const int total,
        const QString& scriptName)
    {
        if(savingProgress_ != nullptr)
        {
            savingProgress_->setRange(0, 1);
            savingProgress_->setValue(1);
            savingProgress_->setTextVisible(true);
            savingProgress_->setFormat(tr("Save prepared"));
            savingLabel_->setText(tr("Annotation save prepared"));
        }
        if(validationProgress_ == nullptr)
        {
            return;
        }
        validationProgress_->setMaximum(std::max(1, total));
        validationProgress_->setValue(std::max(0, current - 1));
        validationLabel_->setText(
            tr("Running %1 (%2 of %3)…")
                .arg(scriptName)
                .arg(current)
                .arg(total));
    }

private:
    QLabel* savingLabel_ = nullptr;
    QProgressBar* savingProgress_ = nullptr;
    QLabel* validationLabel_ = nullptr;
    QProgressBar* validationProgress_ = nullptr;
    QPushButton* cancelButton_ = nullptr;
};

double importElapsedMilliseconds(const ImportClock::time_point start)
{
    return std::chrono::duration<double, std::milli>(ImportClock::now() - start)
        .count();
}

struct DicomScanOutcome
{
    std::vector<io::DicomFileRecord> records;
    double milliseconds = 0.0;
    QString error;
};

struct DicomReadOutcome
{
    std::shared_ptr<core::Volume> volume;
    io::DicomReadTimings timings;
    QString error;
};

struct ArchiveExtractionOutcome
{
    std::vector<std::filesystem::path> paths;
    double milliseconds = 0.0;
    QString error;
};

bool isNiftiFile(const QString& fileName)
{
    return fileName.endsWith(QStringLiteral(".nii"), Qt::CaseInsensitive)
        || fileName.endsWith(QStringLiteral(".nii.gz"), Qt::CaseInsensitive);
}

bool isArchiveFile(const QString& fileName)
{
    return fileName.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive)
        || fileName.endsWith(QStringLiteral(".tar.gz"), Qt::CaseInsensitive);
}

std::filesystem::path fileSystemPath(const QString& path)
{
#ifdef _WIN32
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::path(path.toStdString());
#endif
}

QString qtPath(const std::filesystem::path& path)
{
#ifdef _WIN32
    return QString::fromStdWString(path.wstring());
#else
    const auto utf8 = path.u8string();
    return QString::fromUtf8(
        reinterpret_cast<const char*>(utf8.data()),
        static_cast<qsizetype>(utf8.size()));
#endif
}

void setKeepWindowOnTop(QWidget* const window, const bool keepOnTop)
{
#ifdef _WIN32
    // QWidget::setWindowFlag() calls setParent() and recreates the HWND.
    // That hides the frame, drops the Explorer taskbar button, and rebuilds
    // VTK surfaces. Change Z-order in place instead.
    const HWND hwnd = reinterpret_cast<HWND>(window->winId());
    SetWindowPos(
        hwnd,
        keepOnTop ? HWND_TOPMOST : HWND_NOTOPMOST,
        0,
        0,
        0,
        0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
#else
    const bool wasVisible = window->isVisible();
    window->setWindowFlag(Qt::WindowStaysOnTopHint, keepOnTop);
    if(wasVisible)
    {
        window->show();
        window->raise();
        window->activateWindow();
    }
#endif
}

bool isTextEntryWidget(QWidget* widget)
{
    while(widget != nullptr)
    {
        if(auto* const lineEdit = qobject_cast<QLineEdit*>(widget))
        {
            return !lineEdit->isReadOnly();
        }
        if(qobject_cast<QAbstractSpinBox*>(widget) != nullptr
           || qobject_cast<QPlainTextEdit*>(widget) != nullptr
           || qobject_cast<QTextEdit*>(widget) != nullptr)
        {
            return true;
        }
        if(auto* const combo = qobject_cast<QComboBox*>(widget))
        {
            return combo->isEditable();
        }
        widget = widget->parentWidget();
    }
    return false;
}

QString companionJsonPath(const QString& niftiPath)
{
    QString path = niftiPath;
    if(path.endsWith(QStringLiteral(".nii.gz"), Qt::CaseInsensitive))
    {
        path.chop(7);
    }
    else if(path.endsWith(QStringLiteral(".nii"), Qt::CaseInsensitive))
    {
        path.chop(4);
    }
    else
    {
        return {};
    }
    path += QStringLiteral(".json");
    const QFileInfo info(path);
    return info.isFile() ? info.absoluteFilePath() : QString{};
}

QString validationFailureText(
    const validation::AnnotationValidationResult& result)
{
    QStringList failures;
    for(const auto& script : result.scripts)
    {
        if(script.passed())
        {
            continue;
        }
        const QString location = script.sliceNumber
            ? QObject::tr(" (slice %1)").arg(*script.sliceNumber)
            : QString{};
        failures.push_back(
            QStringLiteral("%1%2:\n%3")
                .arg(script.script.name, location, script.message));
    }
    return failures.join(QStringLiteral("\n\n"));
}

std::optional<int> firstValidationIssueSlice(
    const validation::AnnotationValidationResult& result)
{
    const auto issue = std::find_if(
        result.scripts.begin(), result.scripts.end(), [](const auto& script) {
            return !script.passed() && script.sliceNumber.has_value();
        });
    return issue == result.scripts.end() ? std::nullopt : issue->sliceNumber;
}

QStringList droppedLocalInputs(const QMimeData* const mimeData)
{
    if(mimeData == nullptr || !mimeData->hasUrls())
    {
        return {};
    }
    QStringList inputs;
    for(const auto& url : mimeData->urls())
    {
        if(url.isLocalFile())
        {
            const QString path = url.toLocalFile();
            const QFileInfo information(path);
            if(information.isFile() || information.isDir())
            {
                inputs.push_back(path);
            }
        }
    }
    return inputs;
}

QString dicomSourceName(const QStringList& fileNames)
{
    if(fileNames.size() == 1)
    {
        return QFileInfo(fileNames.front()).fileName();
    }
    if(std::any_of(fileNames.begin(), fileNames.end(), [](const QString& path) {
           return QFileInfo(path).isDir();
       }))
    {
        return QObject::tr("DICOM collection (%1 inputs)").arg(fileNames.size());
    }
    return QObject::tr("DICOM series (%1 files)").arg(fileNames.size());
}

QString parentPrefixedFileName(const QString& fileName)
{
    const QFileInfo fileInfo(fileName);
    const QString parentName = fileInfo.absoluteDir().dirName();
    return parentName.isEmpty()
        ? fileInfo.fileName()
        : parentName + QLatin1Char('-') + fileInfo.fileName();
}

class NiftiDropChoiceDialog final : public QDialog
{
public:
    enum class Choice
    {
        Cancel,
        AddAnnotations,
        OpenImage,
    };

    NiftiDropChoiceDialog(
        const QStringList& fileNames,
        const bool canAddAnnotations,
        QWidget* parent)
        : QDialog(parent)
    {
        setObjectName(QStringLiteral("niftiDropChoiceDialog"));
        setWindowTitle(tr("Load NIfTI"));
        setModal(true);
        setWindowFlag(Qt::WindowContextHelpButtonHint, false);
        setMinimumWidth(420);

        auto* const layout = new QVBoxLayout(this);
        layout->setContentsMargins(22, 20, 22, 18);
        layout->setSpacing(10);
        auto* const title = new QLabel(tr("Load dropped NIfTI"), this);
        title->setObjectName(QStringLiteral("niftiDropDialogTitle"));
        auto* const prompt = new QLabel(
            fileNames.size() == 1
                ? tr("How would you like to load %1?")
                      .arg(QFileInfo(fileNames.front()).fileName())
                : tr("How would you like to load these %1 NIfTI files?")
                      .arg(fileNames.size()),
            this);
        prompt->setObjectName(QStringLiteral("niftiDropDialogPrompt"));
        prompt->setWordWrap(true);
        auto* const detail = new QLabel(
            canAddAnnotations
                ? tr("Adding preserves the anatomical image and all existing annotation layers.")
                : tr("Open an anatomical image before adding annotations."),
            this);
        detail->setObjectName(QStringLiteral("niftiDropDialogDetail"));
        detail->setWordWrap(true);
        layout->addWidget(title);
        layout->addWidget(prompt);
        layout->addWidget(detail);
        layout->addSpacing(6);

        auto* const buttons = new QHBoxLayout();
        buttons->setSpacing(8);
        auto* const cancel = new QPushButton(tr("Cancel"), this);
        cancel->setObjectName(QStringLiteral("niftiDropCancelButton"));
        auto* const image = new QPushButton(tr("Open as new image"), this);
        image->setObjectName(QStringLiteral("niftiDropImageButton"));
        auto* const annotation = new QPushButton(tr("Add as annotations"), this);
        annotation->setObjectName(QStringLiteral("niftiDropAnnotationButton"));
        annotation->setEnabled(canAddAnnotations);
        buttons->addWidget(cancel);
        buttons->addStretch(1);
        buttons->addWidget(image);
        buttons->addWidget(annotation);
        layout->addLayout(buttons);

        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        connect(image, &QPushButton::clicked, this, [this] {
            choice_ = Choice::OpenImage;
            accept();
        });
        connect(annotation, &QPushButton::clicked, this, [this] {
            choice_ = Choice::AddAnnotations;
            accept();
        });
        (canAddAnnotations ? annotation : image)->setDefault(true);
    }

    [[nodiscard]] Choice choice() const noexcept
    {
        return choice_;
    }

private:
    Choice choice_ = Choice::Cancel;
};

constexpr int mainWindowStateVersion = 1;

QString focusedViewName(
    const std::optional<core::SliceOrientation>& orientation)
{
    if(!orientation)
    {
        return {};
    }
    switch(*orientation)
    {
    case core::SliceOrientation::Axial:
        return QStringLiteral("axial");
    case core::SliceOrientation::Sagittal:
        return QStringLiteral("sagittal");
    case core::SliceOrientation::Coronal:
        return QStringLiteral("coronal");
    }
    return {};
}

std::optional<core::SliceOrientation> focusedViewFromName(const QString& name)
{
    if(name == QStringLiteral("axial"))
    {
        return core::SliceOrientation::Axial;
    }
    if(name == QStringLiteral("sagittal"))
    {
        return core::SliceOrientation::Sagittal;
    }
    if(name == QStringLiteral("coronal"))
    {
        return core::SliceOrientation::Coronal;
    }
    return std::nullopt;
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    const auto applicationName = radmarky::app::applicationName();

    setObjectName("mainWindow");
    setWindowTitle(QString::fromUtf8(
        applicationName.data(), static_cast<qsizetype>(applicationName.size())));
    resize(1100, 760);
    setMinimumSize(640, 480);
    setAcceptDrops(true);
    setDockNestingEnabled(true);
    qApp->installEventFilter(this);
    validationService_ =
        std::make_unique<validation::AnnotationValidationService>();

    auto* fileMenu = menuBar()->addMenu(tr("&File"));
    auto* openAction = fileMenu->addAction(tr("&Open Images..."));
    openAction->setObjectName("openImagesAction");
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, [this] { openImages(); });

    openAnnotationsAction_ = fileMenu->addAction(tr("Open &Annotations…"));
    openAnnotationsAction_->setObjectName(
        QStringLiteral("openAnnotationsAction"));
    openAnnotationsAction_->setEnabled(false);
    connect(
        openAnnotationsAction_,
        &QAction::triggered,
        this,
        &MainWindow::openAnnotations);

    closeImagesAction_ = fileMenu->addAction(tr("&Close Images"));
    closeImagesAction_->setObjectName(QStringLiteral("closeImagesAction"));
    closeImagesAction_->setIcon(
        svgIcon(QStringLiteral(":/icons/close-images.svg")));
    closeImagesAction_->setToolTip(tr("Close the open images"));
    closeImagesAction_->setShortcut(QKeySequence::Close);
    closeImagesAction_->setEnabled(false);
    connect(
        closeImagesAction_, &QAction::triggered, this, &MainWindow::closeImages);

    fileMenu->addSeparator();
    auto* exitAction = fileMenu->addAction(tr("E&xit"));
    exitAction->setShortcut(QKeySequence::Quit);
    connect(exitAction, &QAction::triggered, qApp, &QApplication::quit);

    centralStack_ = new QStackedWidget(this);
    centralStack_->setObjectName(QStringLiteral("centralStack"));

    auto* const emptyPage = new QWidget(centralStack_);
    emptyPage->setObjectName(QStringLiteral("emptyViewerPage"));
    auto* const emptyLayout = new QVBoxLayout(emptyPage);
    emptyLayout->setContentsMargins(32, 32, 32, 32);
    auto* const recentTitle = new QLabel(tr("Recent images"), emptyPage);
    recentTitle->setObjectName(QStringLiteral("recentImagesTitle"));
    auto* const recentHint = new QLabel(
        tr("Double-click a recent image below, drop medical images anywhere, "
           "or use File → Open Images…"),
        emptyPage);
    recentHint->setObjectName(QStringLiteral("recentImagesHint"));
    recentHint->setWordWrap(true);
    recentImages_ = new QTreeWidget(emptyPage);
    recentImages_->setObjectName(QStringLiteral("recentImagesList"));
    recentImages_->setColumnCount(3);
    recentImages_->setHeaderLabels(
        {tr("Image"), tr("File path"), QString{}});
    recentImages_->setRootIsDecorated(false);
    recentImages_->setItemsExpandable(false);
    recentImages_->setIndentation(0);
    recentImages_->setSelectionBehavior(QAbstractItemView::SelectRows);
    recentImages_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    recentImages_->setIconSize(QSize(120, 80));
    recentImages_->setUniformRowHeights(true);
    recentImages_->header()->setStretchLastSection(false);
    recentImages_->header()->setSectionResizeMode(0, QHeaderView::Fixed);
    recentImages_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    recentImages_->header()->setSectionResizeMode(2, QHeaderView::Fixed);
    recentImages_->setColumnWidth(0, 480);
    recentImages_->setColumnWidth(2, 76);
    emptyLayout->addWidget(recentTitle);
    emptyLayout->addWidget(recentHint);
    emptyLayout->addWidget(recentImages_, 1);
    connect(
        recentImages_,
        &QTreeWidget::itemDoubleClicked,
        this,
        [this](QTreeWidgetItem* item, int) {
            openRecentImage(item->data(0, Qt::UserRole).toInt());
        });

    viewer_ = new rendering::OrthogonalViewer(centralStack_);
    connect(
        viewer_,
        &rendering::OrthogonalViewer::cursorChanged,
        this,
        &MainWindow::showCursorStatus);
    centralStack_->addWidget(emptyPage);
    centralStack_->addWidget(viewer_);
    centralStack_->setCurrentWidget(emptyPage);
    setCentralWidget(centralStack_);

    cursorStatusLabel_ = new QLabel(this);
    cursorStatusLabel_->setObjectName(QStringLiteral("cursorStatusLabel"));
    statusBar()->addWidget(cursorStatusLabel_);
    cursorStatusLabel_->setVisible(false);

    volumeDimensionsLabel_ = new QLabel(this);
    volumeDimensionsLabel_->setObjectName(QStringLiteral("volumeDimensionsLabel"));
    volumeDimensionsLabel_->setToolTip(
        tr("Volume dimensions: slices x height x width"));
    statusBar()->addWidget(volumeDimensionsLabel_);
    volumeDimensionsLabel_->setVisible(false);

    auto* const viewMenu = menuBar()->addMenu(tr("&View"));
    auto* const toolbar = addToolBar(tr("Viewer Toolbar"));
    toolbar->setObjectName(QStringLiteral("viewerToolbar"));
    toolbar->setMovable(true);
    toolbar->setFloatable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    toolbar->addAction(openAction);
    openAction->setIcon(svgIcon(QStringLiteral(":/icons/open.svg")));
    openAction->setToolTip(tr("Open NIfTI, DICOM, ZIP, or TAR.GZ images"));
    dicomHeaderAction_ = toolbar->addAction(
        svgIcon(QStringLiteral(":/icons/document.svg")), tr("Image Metadata"));
    dicomHeaderAction_->setObjectName(QStringLiteral("dicomHeaderAction"));
    dicomHeaderAction_->setToolTip(tr("Show image metadata"));
    dicomHeaderAction_->setEnabled(false);
    connect(
        dicomHeaderAction_, &QAction::triggered,
        this, &MainWindow::showDicomHeader);
    toolbar->addAction(closeImagesAction_);
    validationManagementAction_ = new QAction(
        svgIcon(QStringLiteral(":/icons/validation.svg")),
        tr("Validation Management"),
        this);
    validationManagementAction_->setObjectName(
        QStringLiteral("validationManagementAction"));
    validationManagementAction_->setToolTip(
        tr("Manage Python annotation validation scripts"));
    connect(
        validationManagementAction_, &QAction::triggered,
        this, &MainWindow::showValidationManagement);
    validateAnnotationAction_ = new QAction(
        svgIcon(QStringLiteral(":/icons/check.svg")),
        tr("Validate"),
        this);
    validateAnnotationAction_->setObjectName(
        QStringLiteral("validateAnnotationAction"));
    validateAnnotationAction_->setToolTip(
        tr("Run enabled validation scripts on the open annotation"));
    validateAnnotationAction_->setEnabled(false);
    connect(
        validateAnnotationAction_, &QAction::triggered,
        this, &MainWindow::validateOpenAnnotation);
    toolbar->addSeparator();

    auto* const modeGroup = new QActionGroup(this);
    modeGroup->setExclusive(true);
    auto* const crosshairAction = toolbar->addAction(
        svgIcon(QStringLiteral(":/icons/cursor.svg")), tr("Cursor"));
    auto* const zoomAction = toolbar->addAction(
        svgIcon(QStringLiteral(":/icons/zoom.svg")), tr("Zoom"));
    auto* const panAction = toolbar->addAction(
        svgIcon(QStringLiteral(":/icons/pan.svg")), tr("Pan"));
    auto* const contrastAction = toolbar->addAction(
        svgIcon(QStringLiteral(":/icons/contrast.svg")), tr("Contrast"));
    measureAction_ = toolbar->addAction(
        svgIcon(QStringLiteral(":/icons/measure.svg")), tr("Measure"));
    for(auto* const action : {
            crosshairAction, zoomAction, panAction, contrastAction,
            measureAction_})
    {
        action->setCheckable(true);
        modeGroup->addAction(action);
    }
    crosshairAction->setChecked(true);
    crosshairAction->setToolTip(tr("Cursor: position the synchronized crosshair"));
    zoomAction->setToolTip(tr("Zoom: drag vertically in a view"));
    panAction->setToolTip(tr("Pan: drag a view"));
    contrastAction->setToolTip(tr(
        "Contrast: adjust window and level; hold Shift to adjust label opacity"));
    measureAction_->setObjectName(QStringLiteral("measureAction"));
    measureAction_->setToolTip(
        tr("Measure: drag a ruler across the image (millimetres)"));
    connect(
        crosshairAction,
        &QAction::triggered,
        viewer_,
        &rendering::OrthogonalViewer::setCrosshairTool);
    connect(
        zoomAction,
        &QAction::triggered,
        viewer_,
        &rendering::OrthogonalViewer::setZoomTool);
    connect(
        panAction,
        &QAction::triggered,
        viewer_,
        &rendering::OrthogonalViewer::setPanTool);
    connect(
        contrastAction,
        &QAction::triggered,
        viewer_,
        &rendering::OrthogonalViewer::setContrastTool);
    connect(
        measureAction_, &QAction::triggered,
        viewer_, &rendering::OrthogonalViewer::setMeasureTool);

    invertAction_ = new QAction(
        svgIcon(QStringLiteral(":/icons/invert.svg")),
        tr("Invert image"),
        this);
    invertAction_->setObjectName(QStringLiteral("invertImageAction"));
    invertAction_->setCheckable(true);
    invertAction_->setEnabled(false);
    invertAction_->setToolTip(tr("Invert the anatomical image grayscale"));
    connect(
        invertAction_, &QAction::toggled,
        viewer_, &rendering::OrthogonalViewer::setInverted);
    toolbar->addAction(invertAction_);

    brushAction_ = new QAction(
        svgIcon(QStringLiteral(":/icons/brush.svg")), tr("Brush"), this);
    eraseAction_ = new QAction(
        svgIcon(QStringLiteral(":/icons/erase.svg")), tr("Erase"), this);
    scopedEraseAction_ = new QAction(
        svgIcon(QStringLiteral(":/icons/scoped-erase.svg")),
        tr("Scoped Erase"),
        this);
    scopedEraseAction_->setObjectName(QStringLiteral("scopedEraseAction"));
    for(auto* const action : {brushAction_, eraseAction_, scopedEraseAction_})
    {
        action->setCheckable(true);
        action->setEnabled(false);
        modeGroup->addAction(action);
    }
    brushAction_->setToolTip(
        tr("Brush: paint the selected label map in the axial view. "
           "Press 1–9 to choose a label, [ and ] to change brush size"));
    eraseAction_->setToolTip(
        tr("Erase: clear the selected label map in the axial view. "
           "Press 0 to erase"));
    scopedEraseAction_->setToolTip(
        tr("Scoped Erase: click a labeled voxel in the axial view to clear "
           "its 8-connected component on that slice"));
    connect(
        brushAction_, &QAction::triggered,
        viewer_, &rendering::OrthogonalViewer::setBrushTool);
    connect(
        eraseAction_, &QAction::triggered,
        viewer_, &rendering::OrthogonalViewer::setEraseTool);
    connect(
        scopedEraseAction_, &QAction::triggered,
        viewer_, &rendering::OrthogonalViewer::setScopedEraseTool);
    toolbar->addSeparator();
    toolbar->addAction(brushAction_);
    toolbar->addAction(eraseAction_);
    toolbar->addAction(scopedEraseAction_);

    undoEditAction_ = new QAction(
        svgIcon(QStringLiteral(":/icons/undo.svg")),
        tr("Undo annotation edit"), this);
    redoEditAction_ = new QAction(
        svgIcon(QStringLiteral(":/icons/redo.svg")),
        tr("Redo annotation edit"), this);
    undoEditAction_->setShortcut(QKeySequence::Undo);
    redoEditAction_->setShortcut(QKeySequence::Redo);
    undoEditAction_->setEnabled(false);
    redoEditAction_->setEnabled(false);
    auto* const editMenu = menuBar()->addMenu(tr("&Edit"));
    editMenu->addAction(undoEditAction_);
    editMenu->addAction(redoEditAction_);

    auto* const annotationMenu = menuBar()->addMenu(tr("&Annotation"));
    newAnnotationAction_ = annotationMenu->addAction(tr("&New Annotation"));
    newAnnotationAction_->setObjectName(QStringLiteral("newAnnotationAction"));
    newAnnotationAction_->setIcon(
        svgIcon(QStringLiteral(":/icons/new-annotation.svg")));
    newAnnotationAction_->setEnabled(false);
    connect(
        newAnnotationAction_, &QAction::triggered,
        this, &MainWindow::createNewAnnotation);

    annotationMenu->addSeparator();
    saveAnnotationAction_ = annotationMenu->addAction(tr("&Save"));
    saveAnnotationAction_->setObjectName(QStringLiteral("saveAnnotationAction"));
    saveAnnotationAction_->setShortcut(QKeySequence::Save);
    saveAnnotationAction_->setEnabled(false);
    connect(
        saveAnnotationAction_, &QAction::triggered,
        this, &MainWindow::saveSelectedAnnotation);

    saveAnnotationAsAction_ =
        annotationMenu->addAction(tr("Save Annotation &As…"));
    saveAnnotationAsAction_->setObjectName(
        QStringLiteral("saveAnnotationAsAction"));
    saveAnnotationAsAction_->setShortcut(
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));
    saveAnnotationAsAction_->setEnabled(false);
    connect(
        saveAnnotationAsAction_, &QAction::triggered,
        this, &MainWindow::saveSelectedAnnotationAs);

    annotationMenu->addAction(validateAnnotationAction_);

    annotationMenu->addSeparator();
    auto* const activeLabelMenu = annotationMenu->addMenu(tr("Active &Label"));
    activeLabelMenu->setObjectName(QStringLiteral("activeLabelMenu"));
    activeLabelActions_.reserve(10);
    for(int label = 1; label <= 9; ++label)
    {
        auto* const action = activeLabelMenu->addAction(
            tr("Label %1").arg(label));
        action->setObjectName(
            QStringLiteral("activeLabel%1Action").arg(label));
        action->setShortcut(
            QKeySequence(static_cast<Qt::Key>(Qt::Key_0 + label)));
        action->setEnabled(false);
        connect(action, &QAction::triggered, this, [this, label] {
            activateAnnotationDigit(label);
        });
        activeLabelActions_.push_back(action);
    }
    auto* const eraseDigitAction = new QAction(tr("Erase"), this);
    eraseDigitAction->setObjectName(QStringLiteral("eraseDigitShortcutAction"));
    eraseDigitAction->setShortcut(QKeySequence(Qt::Key_0));
    eraseDigitAction->setEnabled(false);
    addAction(eraseDigitAction);
    connect(eraseDigitAction, &QAction::triggered, this, [this] {
        activateAnnotationDigit(0);
    });
    activeLabelActions_.push_back(eraseDigitAction);

    annotationMenu->addSeparator();
    auto* const decreaseBrushAction =
        annotationMenu->addAction(tr("Decrease Brush Size"));
    decreaseBrushAction->setObjectName(
        QStringLiteral("decreaseBrushSizeAction"));
    decreaseBrushAction->setShortcut(QKeySequence(Qt::Key_BracketLeft));
    decreaseBrushAction->setEnabled(false);
    connect(decreaseBrushAction, &QAction::triggered, this, [this] {
        toolbox_->adjustBrushRadius(-1);
    });
    auto* const increaseBrushAction =
        annotationMenu->addAction(tr("Increase Brush Size"));
    increaseBrushAction->setObjectName(
        QStringLiteral("increaseBrushSizeAction"));
    increaseBrushAction->setShortcut(QKeySequence(Qt::Key_BracketRight));
    increaseBrushAction->setEnabled(false);
    connect(increaseBrushAction, &QAction::triggered, this, [this] {
        toolbox_->adjustBrushRadius(1);
    });
    activeLabelActions_.push_back(decreaseBrushAction);
    activeLabelActions_.push_back(increaseBrushAction);
    connect(
        qApp,
        &QApplication::focusChanged,
        this,
        [this](QWidget*, QWidget*) { updateActiveLabelActions(); });

    connect(
        undoEditAction_, &QAction::triggered,
        viewer_, &rendering::OrthogonalViewer::undoAnnotationEdit);
    connect(
        redoEditAction_, &QAction::triggered,
        viewer_, &rendering::OrthogonalViewer::redoAnnotationEdit);
    toolbar->addSeparator();
    auto* const zoomInAction = toolbar->addAction(
        svgIcon(QStringLiteral(":/icons/zoom-in.svg")), tr("Zoom in"));
    auto* const zoomOutAction = toolbar->addAction(
        svgIcon(QStringLiteral(":/icons/zoom-out.svg")), tr("Zoom out"));
    auto* const centerAction = toolbar->addAction(
        svgIcon(QStringLiteral(":/icons/center-view.svg")), tr("Center all views"));
    auto* const recordGifAction = toolbar->addAction(
        svgIcon(QStringLiteral(":/icons/camcorder.svg")),
        tr("Record slice animation"));
    recordGifAction->setObjectName(QStringLiteral("recordGifAction"));
    zoomInAction->setToolTip(tr("Zoom all views in"));
    zoomOutAction->setToolTip(tr("Zoom all views out"));
    centerAction->setToolTip(tr("Center and fit all slice views"));
    recordGifAction->setToolTip(
        tr("Export an MP4 video or looping GIF across nearby slices"));
    connect(
        zoomInAction,
        &QAction::triggered,
        viewer_,
        &rendering::OrthogonalViewer::zoomAllIn);
    connect(
        zoomOutAction,
        &QAction::triggered,
        viewer_,
        &rendering::OrthogonalViewer::zoomAllOut);
    connect(
        centerAction,
        &QAction::triggered,
        viewer_,
        &rendering::OrthogonalViewer::resetAllViews);
    connect(
        recordGifAction,
        &QAction::triggered,
        viewer_,
        &rendering::OrthogonalViewer::exportSliceAnimation);

    toolbar->addSeparator();
    toolbar->addAction(validationManagementAction_);
    toolbar->addAction(validateAnnotationAction_);
    toolbar->addSeparator();
    layoutButton_ = new QToolButton(toolbar);
    layoutButton_->setObjectName(QStringLiteral("layoutButton"));
    layoutButton_->setIcon(svgIcon(QStringLiteral(":/icons/layout.svg")));
    layoutButton_->setToolTip(tr("Choose the slice-view layout"));
    layoutButton_->setPopupMode(QToolButton::InstantPopup);
    auto* const layoutMenu = new QMenu(layoutButton_);
    auto* const allViewsAction = layoutMenu->addAction(tr("Show all views"));
    layoutMenu->addSeparator();
    auto* const axialViewAction = layoutMenu->addAction(tr("Focus axial view"));
    auto* const sagittalViewAction = layoutMenu->addAction(tr("Focus sagittal view"));
    auto* const coronalViewAction = layoutMenu->addAction(tr("Focus coronal view"));
    layoutButton_->setMenu(layoutMenu);
    toolbar->addWidget(layoutButton_);
    viewMenu->addAction(allViewsAction);
    viewMenu->addSeparator();
    viewMenu->addAction(axialViewAction);
    viewMenu->addAction(sagittalViewAction);
    viewMenu->addAction(coronalViewAction);
    viewMenu->addSeparator();
    viewMenu->addAction(crosshairAction);
    viewMenu->addAction(zoomAction);
    viewMenu->addAction(panAction);
    viewMenu->addAction(contrastAction);
    viewMenu->addAction(measureAction_);
    viewMenu->addAction(invertAction_);
    viewMenu->addAction(brushAction_);
    viewMenu->addAction(eraseAction_);
    viewMenu->addAction(scopedEraseAction_);
    viewMenu->addSeparator();
    viewMenu->addAction(zoomInAction);
    viewMenu->addAction(zoomOutAction);
    viewMenu->addAction(centerAction);
    viewMenu->addAction(recordGifAction);
    connect(
        allViewsAction,
        &QAction::triggered,
        viewer_,
        &rendering::OrthogonalViewer::showAllViews);
    connect(
        axialViewAction,
        &QAction::triggered,
        viewer_,
        &rendering::OrthogonalViewer::focusAxialView);
    connect(
        sagittalViewAction,
        &QAction::triggered,
        viewer_,
        &rendering::OrthogonalViewer::focusSagittalView);
    connect(
        coronalViewAction,
        &QAction::triggered,
        viewer_,
        &rendering::OrthogonalViewer::focusCoronalView);

    auto* const themeAction = toolbar->addAction(
        svgIcon(QStringLiteral(":/icons/theme.svg")), tr("Dark color theme"));
    themeAction->setCheckable(true);
    themeAction->setToolTip(tr("Toggle the light and dark color themes"));
    viewMenu->addSeparator();
    viewMenu->addAction(themeAction);
    connect(themeAction, &QAction::toggled, this, &MainWindow::applyTheme);

    keepOnTopAction_ = toolbar->addAction(
        svgIcon(QStringLiteral(":/icons/pin-up.svg")),
        tr("Keep window on top"));
    keepOnTopAction_->setObjectName(QStringLiteral("keepOnTopAction"));
    keepOnTopAction_->setCheckable(true);
    keepOnTopAction_->setToolTip(
        tr("Keep RadMarky Viewer above other applications"));
    connect(
        keepOnTopAction_,
        &QAction::toggled,
        this,
        [this](const bool keepOnTop) {
            setKeepWindowOnTop(this, keepOnTop);
            settings_.setKeepWindowOnTop(keepOnTop);
            keepOnTopAction_->setIcon(svgIcon(
                keepOnTop ? QStringLiteral(":/icons/pin-down.svg")
                          : QStringLiteral(":/icons/pin-up.svg")));
            keepOnTopAction_->setToolTip(
                keepOnTop
                    ? tr("Keep RadMarky Viewer above other applications (on)")
                    : tr("Keep RadMarky Viewer above other applications"));
        });
    for(auto* const button : toolbar->findChildren<QToolButton*>())
    {
        button->setAutoRaise(false);
    }

    toolbox_ = new ViewerToolbox(this);
    toolbox_->hide();
    const auto makePanelDock = [this](
                                   const QString& title,
                                   const QString& objectName,
                                   QWidget* const panel,
                                   const bool fillDock = false) {
        auto* const dock = new QDockWidget(title, this);
        dock->setObjectName(objectName);
        dock->setProperty("viewerPanelDock", true);
        dock->setAllowedAreas(
            Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
        dock->setFeatures(
            QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable
            | QDockWidget::DockWidgetFloatable);
        dock->setMinimumWidth(210);
        if(auto* const group = qobject_cast<QGroupBox*>(panel))
        {
            group->setTitle({});
        }

        // QDockWidget expands its direct child to the full dock height.  Keep
        // each panel at its natural height instead, so extra dock space stays
        // below the controls rather than being distributed between them.
        auto* const dockContents = new QWidget(dock);
        dockContents->setObjectName(QStringLiteral("viewerDockContents"));
        auto* const dockLayout = new QVBoxLayout(dockContents);
        dockLayout->setContentsMargins(0, 0, 0, 0);
        dockLayout->setSpacing(0);
        if(fillDock)
        {
            dockLayout->addWidget(panel);
        }
        else
        {
            dockLayout->addWidget(panel, 0, Qt::AlignTop);
            dockLayout->addStretch();
        }
        dock->setWidget(dockContents);
        // Moving a page out of QStackedWidget leaves it explicitly hidden.
        // Reveal it after the new dock owns it so its existing contents render.
        panel->show();
        return dock;
    };
    identityDock_ = makePanelDock(
        tr("RadMarky Viewer"), QStringLiteral("viewerIdentityDock"),
        toolbox_->identityPanel(), true);
    identityDock_->setAllowedAreas(Qt::LeftDockWidgetArea);
    identityDock_->setFeatures(QDockWidget::NoDockWidgetFeatures);
    auto* const identityTitleBar = new QWidget(identityDock_);
    identityTitleBar->setFixedHeight(0);
    identityDock_->setTitleBarWidget(identityTitleBar);
    identityDock_->toggleViewAction()->setVisible(false);
    contrastDock_ = makePanelDock(
        tr("Contrast"), QStringLiteral("viewerContrastDock"),
        toolbox_->contrastPanel(), true);
    contrastDock_->setMaximumHeight(180);
    annotationsDock_ = makePanelDock(
        tr("Annotations"), QStringLiteral("viewerAnnotationsDock"),
        toolbox_->annotationsPanel());
    annotationLabelsDock_ = makePanelDock(
        tr("Annotation Labels"),
        QStringLiteral("viewerAnnotationLabelsDock"),
        toolbox_->annotationLabelsPanel());
    cursorInspectorDock_ = makePanelDock(
        tr("Cursor Inspector"), QStringLiteral("viewerCursorInspectorDock"),
        toolbox_->cursorInspectorPanel(), true);
    addDockWidget(Qt::LeftDockWidgetArea, identityDock_);
    splitDockWidget(identityDock_, cursorInspectorDock_, Qt::Vertical);
    splitDockWidget(cursorInspectorDock_, contrastDock_, Qt::Vertical);
    addDockWidget(Qt::RightDockWidgetArea, annotationsDock_);
    splitDockWidget(annotationsDock_, annotationLabelsDock_, Qt::Vertical);
    resizeDocks(
        {cursorInspectorDock_, contrastDock_},
        {430, 180},
        Qt::Vertical);
    resizeDocks(
        {annotationsDock_, annotationLabelsDock_},
        {160, 450},
        Qt::Vertical);
    resizeDocks(
        {identityDock_, contrastDock_, cursorInspectorDock_,
         annotationsDock_, annotationLabelsDock_},
        {235, 235, 235, 235, 235},
        Qt::Horizontal);
    contrastDock_->hide();
    annotationsDock_->hide();
    cursorInspectorDock_->hide();
    annotationLabelsDock_->hide();
    viewMenu->addSeparator();
    viewMenu->addAction(contrastDock_->toggleViewAction());
    viewMenu->addAction(annotationsDock_->toggleViewAction());
    viewMenu->addAction(cursorInspectorDock_->toggleViewAction());
    viewMenu->addAction(annotationLabelsDock_->toggleViewAction());

    auto* const helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->setObjectName(QStringLiteral("helpMenu"));
    auto* const aboutAction = helpMenu->addAction(
        tr("&About %1").arg(QString::fromUtf8(
            applicationName.data(),
            static_cast<qsizetype>(applicationName.size()))));
    aboutAction->setObjectName(QStringLiteral("aboutAction"));
    aboutAction->setMenuRole(QAction::AboutRole);
    connect(aboutAction, &QAction::triggered, this, &MainWindow::showAbout);

    imageDependentActions_ = {
        closeImagesAction_,
        openAnnotationsAction_,
        newAnnotationAction_,
        validationManagementAction_,
        crosshairAction,
        zoomAction,
        panAction,
        contrastAction,
        measureAction_,
        invertAction_,
        zoomInAction,
        zoomOutAction,
        centerAction,
        recordGifAction,
        allViewsAction,
        axialViewAction,
        sagittalViewAction,
        coronalViewAction,
        contrastDock_->toggleViewAction(),
        annotationsDock_->toggleViewAction(),
        cursorInspectorDock_->toggleViewAction(),
        annotationLabelsDock_->toggleViewAction(),
    };
    setImageDependentActionsEnabled(false);

    connect(
        toolbox_,
        &ViewerToolbox::windowLevelEdited,
        viewer_,
        &rendering::OrthogonalViewer::setWindowLevel);
    connect(
        toolbox_,
        &ViewerToolbox::automaticWindowLevelRequested,
        viewer_,
        &rendering::OrthogonalViewer::applyAutomaticWindowLevel);
    connect(
        toolbox_,
        &ViewerToolbox::windowLevelPresetSelected,
        viewer_,
        &rendering::OrthogonalViewer::applyWindowLevelPreset);
    connect(
        toolbox_,
        &ViewerToolbox::windowLevelDefaultRequested,
        this,
        [this](const double window, const double level) {
            settings_.setDefaultWindowLevel(window, level);
            statusBar()->showMessage(
                tr("Default window/level saved for future images"), 4000);
        });
    connect(
        toolbox_,
        &ViewerToolbox::windowLevelPresetSaveRequested,
        this,
        [this](const QString& name, const double window, const double level) {
            settings_.addWindowLevelPreset(name, window, level);
            toolbox_->setNamedWindowLevelPresets(settings_.windowLevelPresets());
            statusBar()->showMessage(tr("Saved preset “%1”").arg(name), 4000);
        });
    connect(
        viewer_,
        &rendering::OrthogonalViewer::cursorInspectionChanged,
        toolbox_,
        &ViewerToolbox::setCursorInspection);
    connect(
        toolbox_,
        &ViewerToolbox::axialSliceEdited,
        viewer_,
        &rendering::OrthogonalViewer::goToAxialSlice);
    connect(
        toolbox_,
        &ViewerToolbox::samplingRadiusChanged,
        viewer_,
        &rendering::OrthogonalViewer::setSamplingRadius);
    connect(
        toolbox_,
        &ViewerToolbox::annotationCreationRequested,
        this,
        &MainWindow::createNewAnnotation);
    connect(
        toolbox_,
        &ViewerToolbox::annotationLoadRequested,
        this,
        &MainWindow::openAnnotations);
    connect(
        toolbox_,
        &ViewerToolbox::annotationRemovalRequested,
        this,
        &MainWindow::removeAnnotation);
    connect(
        toolbox_,
        &ViewerToolbox::annotationOpacityChanged,
        this,
        &MainWindow::setAnnotationOpacity);
    connect(
        toolbox_,
        &ViewerToolbox::annotationVisibilityChanged,
        this,
        &MainWindow::setAnnotationVisibility);
    connect(
        toolbox_,
        &ViewerToolbox::annotationSelectionChanged,
        this,
        &MainWindow::setAnnotationSelection);
    connect(
        toolbox_,
        &ViewerToolbox::activeLabelChanged,
        viewer_,
        &rendering::OrthogonalViewer::setActiveLabel);
    connect(
        toolbox_,
        &ViewerToolbox::paintOverChanged,
        viewer_,
        &rendering::OrthogonalViewer::setPaintOver);
    connect(
        toolbox_,
        &ViewerToolbox::brushRadiusChanged,
        viewer_,
        &rendering::OrthogonalViewer::setBrushRadius);
    connect(
        toolbox_,
        &ViewerToolbox::brushShapeChanged,
        viewer_,
        &rendering::OrthogonalViewer::setBrushShape);
    connect(
        toolbox_,
        &ViewerToolbox::overallLabelOpacityChanged,
        viewer_,
        &rendering::OrthogonalViewer::setOverallLabelOpacity);
    connect(
        viewer_,
        &rendering::OrthogonalViewer::overallLabelOpacityChanged,
        toolbox_,
        &ViewerToolbox::setOverallLabelOpacity);
    connect(
        viewer_,
        &rendering::OrthogonalViewer::annotationEditingStateChanged,
        this,
        [this, crosshairAction](
            const bool editable, const bool canUndo, const bool canRedo) {
            brushAction_->setEnabled(editable);
            eraseAction_->setEnabled(editable);
            scopedEraseAction_->setEnabled(editable);
            saveAnnotationAction_->setEnabled(editable);
            saveAnnotationAsAction_->setEnabled(editable);
            validateAnnotationAction_->setEnabled(editable);
            undoEditAction_->setEnabled(editable && canUndo);
            redoEditAction_->setEnabled(editable && canRedo);
            toolbox_->setAnnotationEditingState(editable, canUndo, canRedo);
            updateActiveLabelActions();
            if(!editable && (brushAction_->isChecked()
                             || eraseAction_->isChecked()
                             || scopedEraseAction_->isChecked()))
            {
                crosshairAction->trigger();
            }
        });
    connect(
        viewer_,
        &rendering::OrthogonalViewer::annotationLabelsChanged,
        toolbox_,
        &ViewerToolbox::setAnnotationLabels);
    connect(
        viewer_,
        &rendering::OrthogonalViewer::windowLevelChanged,
        toolbox_,
        &ViewerToolbox::setWindowLevel);

    toolbox_->setNamedWindowLevelPresets(settings_.windowLevelPresets());
    const auto removedRecents = settings_.removeMissingRecentImages();
    if(removedRecents > 0)
    {
        qInfo() << "[SETTINGS] Removed" << removedRecents
                << "recent image entries with missing source files";
    }
    refreshRecentImages();
    statusBar()->showMessage(
        tr("Drop medical images anywhere or choose File → Open Images…"));
    themeAction->setChecked(settings_.darkTheme());
    applyTheme(settings_.darkTheme());
    restoreWindowLayout();
    keepOnTopAction_->setChecked(settings_.keepWindowOnTop());
}

MainWindow::~MainWindow() = default;

void MainWindow::closeEvent(QCloseEvent* const event)
{
    if(resolveUnsavedAnnotations(annotations_, tr("closing RadMarky Viewer")))
    {
        if(primaryVolume_)
        {
            rememberRecentImage(
                primarySourceName_, primarySourceKind_, primarySourceFiles_);
        }
        settings_.setKeepWindowOnTop(keepOnTopAction_->isChecked());
        saveWindowLayout();
        event->accept();
        return;
    }
    event->ignore();
}

void MainWindow::applyTheme(const bool dark)
{
    darkTheme_ = dark;
    settings_.setDarkTheme(dark);
    qApp->setStyleSheet(applicationStyleSheet(
        darkTheme_ ? UiTheme::Dark : UiTheme::Light));
}

void MainWindow::applyEmptyPageDockVisibility()
{
    contrastDock_->hide();
    annotationsDock_->hide();
    cursorInspectorDock_->hide();
    annotationLabelsDock_->hide();
    identityDock_->show();
}

void MainWindow::rememberImageDockLayout()
{
    if(!primaryVolume_)
    {
        return;
    }
    auto layout = settings_.windowLayout().value_or(app::WindowLayoutSetting{});
    layout.windowState = saveState(mainWindowStateVersion);
    settings_.setWindowLayout(std::move(layout));
}

void MainWindow::saveWindowLayout()
{
    auto layout = settings_.windowLayout().value_or(app::WindowLayoutSetting{});
    layout.geometry = saveGeometry();
    if(primaryVolume_)
    {
        layout.windowState = saveState(mainWindowStateVersion);
    }
    const auto viewerLayout = viewer_->layoutState();
    layout.viewSplitterSizes = viewerLayout.viewSplitterSizes;
    layout.rightViewSplitterSizes = viewerLayout.rightViewSplitterSizes;
    layout.focusedView = focusedViewName(viewerLayout.focusedView);
    settings_.setWindowLayout(std::move(layout));
}

void MainWindow::restoreWindowLayout()
{
    const auto& layout = settings_.windowLayout();
    if(!layout)
    {
        return;
    }
    if(!layout->geometry.isEmpty())
    {
        restoreGeometry(layout->geometry);
    }
    if(!layout->windowState.isEmpty())
    {
        restoreState(layout->windowState, mainWindowStateVersion);
    }
    applyEmptyPageDockVisibility();
    rendering::OrthogonalViewer::LayoutState viewerLayout;
    viewerLayout.viewSplitterSizes = layout->viewSplitterSizes;
    viewerLayout.rightViewSplitterSizes = layout->rightViewSplitterSizes;
    viewerLayout.focusedView = focusedViewFromName(layout->focusedView);
    viewer_->setLayoutState(viewerLayout);
}

void MainWindow::applyImageDockLayout()
{
    identityDock_->hide();
    const auto& layout = settings_.windowLayout();
    if(layout && !layout->windowState.isEmpty()
       && restoreState(layout->windowState, mainWindowStateVersion))
    {
        identityDock_->hide();
        return;
    }
    contrastDock_->show();
    annotationsDock_->show();
    cursorInspectorDock_->show();
    annotationLabelsDock_->show();
}

void MainWindow::openImages()
{
    const QStringList fileNames = QFileDialog::getOpenFileNames(
        this,
        tr("Open Medical Images"),
        {},
        tr("Medical images and DICOM files (*);;"
           "NIfTI images (*.nii *.nii.gz);;"
           "DICOM and archives (*.dcm *.dicom *.zip *.tar.gz);;"
           "All files (*)"));
    if(fileNames.isEmpty())
    {
        return;
    }

    loadInputs(fileNames);
}

void MainWindow::openAnnotations()
{
    if(!primaryVolume_)
    {
        QMessageBox::information(
            this,
            tr("Open an Anatomical Image First"),
            tr("Open an anatomical image before adding annotations."));
        return;
    }
    const QStringList fileNames = QFileDialog::getOpenFileNames(
        this,
        tr("Open NIfTI Annotations"),
        {},
        tr("NIfTI annotations (*.nii *.nii.gz)"));
    if(!fileNames.isEmpty())
    {
        loadAnnotations(fileNames);
    }
}

void MainWindow::createNewAnnotation()
{
    if(!primaryVolume_)
    {
        return;
    }
    try
    {
        const QString name = tr("New annotation %1").arg(++newAnnotationSequence_);
        auto annotation = core::Annotation::createBlankLabelMap(
            name.toStdString(), *primaryVolume_);
        viewer_->addAnnotation(annotation);
        annotations_.push_back(annotation);
        toolbox_->addAnnotation(
            name, tr("Labels"), annotation->opacity(), annotation->isVisible());
        const int index = static_cast<int>(annotations_.size() - 1);
        toolbox_->selectAnnotation(index);
        setAnnotationSelection({index});
        annotationLabelsDock_->show();
        brushAction_->trigger();
        statusBar()->showMessage(
            tr("Created %1 — paint in the axial view").arg(name), 5000);
    }
    catch(const std::exception& exception)
    {
        QMessageBox::critical(
            this,
            tr("Unable to Create Annotation"),
            QString::fromUtf8(exception.what()));
    }
}

void MainWindow::saveSelectedAnnotation()
{
    const auto selected = toolbox_->selectedAnnotationIndices();
    if(selected.size() != 1)
    {
        QMessageBox::information(
            this,
            tr("Select One Annotation"),
            tr("Select one label-map annotation before saving."));
        return;
    }
    const int index = selected.front();
    if(index < 0 || static_cast<std::size_t>(index) >= annotations_.size()
       || annotations_[static_cast<std::size_t>(index)]->kind()
           != core::AnnotationKind::LabelMap)
    {
        QMessageBox::information(
            this,
            tr("Select a Label Map"),
            tr("Scalar-map annotations cannot be saved as label maps."));
        return;
    }

    const auto& annotation = annotations_[static_cast<std::size_t>(index)];
    static_cast<void>(saveAnnotation(annotation, false));
}

void MainWindow::saveSelectedAnnotationAs()
{
    const auto selected = toolbox_->selectedAnnotationIndices();
    if(selected.size() != 1)
    {
        QMessageBox::information(
            this,
            tr("Select One Annotation"),
            tr("Select one label-map annotation before saving."));
        return;
    }
    const int index = selected.front();
    if(index < 0 || static_cast<std::size_t>(index) >= annotations_.size()
       || annotations_[static_cast<std::size_t>(index)]->kind()
           != core::AnnotationKind::LabelMap)
    {
        QMessageBox::information(
            this,
            tr("Select a Label Map"),
            tr("Scalar-map annotations cannot be saved as label maps."));
        return;
    }

    static_cast<void>(saveAnnotation(
        annotations_[static_cast<std::size_t>(index)], true));
}

void MainWindow::closeImages()
{
    if(!primaryVolume_)
    {
        return;
    }
    if(!resolveUnsavedAnnotations(annotations_, tr("closing the current image")))
    {
        return;
    }
    rememberImageDockLayout();
    rememberRecentImage(
        primarySourceName_, primarySourceKind_, primarySourceFiles_);
    viewer_->clearVolume();
    invertAction_->setChecked(false);
    primaryVolume_.reset();
    primarySourceFiles_.clear();
    primarySourceName_.clear();
    primarySourceKind_.clear();
    annotations_.clear();
    toolbox_->clearVolume();
    updateActiveLabelActions();
    dicomHeaderAction_->setEnabled(false);
    setImageDependentActionsEnabled(false);
    centralStack_->setCurrentIndex(0);
    applyEmptyPageDockVisibility();
    setWindowTitle(QString::fromUtf8(app::applicationName()));
    cursorStatusLabel_->clear();
    cursorStatusLabel_->setVisible(false);
    volumeDimensionsLabel_->clear();
    volumeDimensionsLabel_->setVisible(false);
    statusBar()->showMessage(tr("Images closed"), 3000);
}

void MainWindow::showDicomHeader()
{
    if(!primaryVolume_ || primaryVolume_->dicomMetadata().empty())
    {
        return;
    }
    DicomHeaderDialog dialog(primaryVolume_->dicomMetadata(), this);
    dialog.exec();
}

void MainWindow::showValidationManagement()
{
    if(!primaryVolume_)
    {
        return;
    }
    ValidationManagementDialog dialog(
        settings_.validationScripts(),
        selectedEditableAnnotation() != nullptr,
        [this](const QString& path) {
            return validationService_->inspectScript(path);
        },
        [this](const std::vector<app::ValidationScriptSetting>& scripts) {
            validateSelectedAnnotation(scripts);
        },
        this);
    dialog.exec();
    settings_.setValidationScripts(dialog.scripts());
}

void MainWindow::validateOpenAnnotation()
{
    validateSelectedAnnotation(settings_.validationScripts());
}

void MainWindow::showAbout()
{
    AboutDialog dialog(this);
    dialog.exec();
}

std::shared_ptr<core::Annotation> MainWindow::selectedEditableAnnotation() const
{
    if(toolbox_ == nullptr)
    {
        return {};
    }
    const auto selected = toolbox_->selectedAnnotationIndices();
    if(selected.size() != 1)
    {
        return {};
    }
    const int index = selected.front();
    if(index < 0 || static_cast<std::size_t>(index) >= annotations_.size())
    {
        return {};
    }
    const auto& annotation = annotations_[static_cast<std::size_t>(index)];
    return annotation->kind() == core::AnnotationKind::LabelMap
        ? annotation : std::shared_ptr<core::Annotation>{};
}

validation::AnnotationValidationResult MainWindow::runValidation(
    const std::shared_ptr<core::Annotation>& annotation,
    const QString& destinationPath,
    const std::vector<app::ValidationScriptSetting>& scripts,
    const bool finalizeSave)
{
    validation::AnnotationValidationContext context;
    context.intendedDestinationPath = finalizeSave ? destinationPath : QString{};
    if(!primarySourceFiles_.isEmpty())
    {
        context.anatomicalImageSourcePath =
            QFileInfo(primarySourceFiles_.front()).absoluteFilePath();
    }
    const QString annotationSource = annotation->sourcePath().empty()
        ? QString{} : qtPath(annotation->sourcePath());
    context.companionJsonPath = companionJsonPath(
        finalizeSave ? destinationPath : annotationSource);
    if(context.companionJsonPath.isEmpty() && finalizeSave)
    {
        context.companionJsonPath = companionJsonPath(annotationSource);
    }

    const int enabledCount = static_cast<int>(std::count_if(
        scripts.begin(), scripts.end(),
        [](const app::ValidationScriptSetting& script) { return script.enabled; }));
    AnnotationOperationProgressDialog progress(
        finalizeSave, enabledCount, this);

    std::atomic_bool cancellation{false};
    connect(progress.cancelButton(), &QPushButton::clicked, this, [&cancellation] {
        cancellation.store(true, std::memory_order_relaxed);
    });
    connect(&progress, &QDialog::rejected, this, [&cancellation] {
        cancellation.store(true, std::memory_order_relaxed);
    });
    QPointer<AnnotationOperationProgressDialog> progressPointer(&progress);
    auto progressCallback = [progressPointer](
                                const int current,
                                const int total,
                                const QString& scriptName) {
        QMetaObject::invokeMethod(
            qApp,
            [progressPointer, current, total, scriptName] {
                if(progressPointer == nullptr)
                {
                    return;
                }
                progressPointer->beginValidation(
                    current, total, scriptName);
            },
            Qt::QueuedConnection);
    };

    QFutureWatcher<validation::AnnotationValidationResult> watcher;
    QEventLoop loop;
    connect(
        &watcher, &QFutureWatcherBase::finished,
        &loop, &QEventLoop::quit);
    auto* const service = validationService_.get();
    watcher.setFuture(QtConcurrent::run(
        // Failures are returned in the result. A throw here can only be
        // allocation while building that result.
        // NOLINTNEXTLINE(bugprone-exception-escape)
        [service, annotation, destinationPath, scripts, context, finalizeSave,
         &cancellation, progressCallback]() {
            try
            {
                return finalizeSave
                    ? service->validateAndSave(
                          *annotation, destinationPath, scripts, context,
                          &cancellation, progressCallback)
                    : service->validateOnly(
                          *annotation, scripts, context,
                          &cancellation, progressCallback);
            }
            catch(const std::exception& exception)
            {
                validation::AnnotationValidationResult result;
                result.fatalError = QString::fromUtf8(exception.what());
                return result;
            }
            catch(...)
            {
                validation::AnnotationValidationResult result;
                result.fatalError =
                    QStringLiteral("An unexpected error occurred");
                return result;
            }
        }));
    progress.show();
    loop.exec();
    progress.accept();
    return watcher.result();
}

void MainWindow::validateSelectedAnnotation(
    const std::vector<app::ValidationScriptSetting>& scripts)
{
    const auto annotation = selectedEditableAnnotation();
    if(!annotation)
    {
        QMessageBox::information(
            this,
            tr("Select a Label Map"),
            tr("Select one editable label-map annotation before validating."));
        return;
    }
    try
    {
        const auto result = runValidation(annotation, {}, scripts, false);
        if(result.cancelled)
        {
            statusBar()->showMessage(tr("Annotation validation cancelled"), 4000);
        }
        else if(!result.fatalError.isEmpty())
        {
            QMessageBox::critical(
                this, tr("Unable to Validate Annotation"), result.fatalError);
        }
        else if(!result.accepted())
        {
            if(const auto slice = firstValidationIssueSlice(result))
            {
                viewer_->goToAxialSlice(*slice);
            }
            QMessageBox::warning(
                this,
                tr("Annotation Validation Failed"),
                validationFailureText(result));
        }
        else if(result.scripts.empty())
        {
            QMessageBox::information(
                this,
                tr("No Validators Enabled"),
                tr("No validation scripts are enabled. Open Validation Management to add or enable scripts."));
        }
        else
        {
            QMessageBox::information(
                this,
                tr("Annotation Accepted"),
                tr("The annotation passed %1 enabled validator(s). No file was saved.")
                    .arg(result.scripts.size()));
        }
    }
    catch(const std::exception& exception)
    {
        QMessageBox::critical(
            this,
            tr("Unable to Validate Annotation"),
            QString::fromUtf8(exception.what()));
    }
}

bool MainWindow::saveAnnotation(
    const std::shared_ptr<core::Annotation>& annotation,
    const bool chooseDestination)
{
    if(!annotation || annotation->kind() != core::AnnotationKind::LabelMap)
    {
        QMessageBox::critical(
            this,
            tr("Unable to Save Annotation"),
            tr("Only label-map annotations can be saved."));
        return false;
    }

    if(!chooseDestination && !annotation->sourcePath().empty())
    {
        return saveAnnotationTo(
            annotation, qtPath(annotation->sourcePath()), false);
    }

    QString suggestedPath;
    if(!annotation->sourcePath().empty())
    {
        suggestedPath = qtPath(annotation->sourcePath());
    }
    else
    {
        suggestedPath = QString::fromUtf8(
                            annotation->name().data(),
                            static_cast<qsizetype>(annotation->name().size()))
            + QStringLiteral(".nii.gz");
    }
    QString destinationPath = QFileDialog::getSaveFileName(
        this,
        tr("Save Annotation"),
        suggestedPath,
        tr("Compressed NIfTI (*.nii.gz);;NIfTI (*.nii)"));
    if(destinationPath.isEmpty())
    {
        return false;
    }
    if(!isNiftiFile(destinationPath))
    {
        destinationPath += QStringLiteral(".nii.gz");
    }
    return saveAnnotationTo(annotation, destinationPath, true);
}

bool MainWindow::saveAnnotationTo(
    const std::shared_ptr<core::Annotation>& annotation,
    const QString& destinationPath,
    const bool updateSourcePath)
{
    try
    {
        const auto result = runValidation(
            annotation, destinationPath, settings_.validationScripts(), true);
        if(result.cancelled)
        {
            statusBar()->showMessage(
                tr("Annotation save cancelled"), 5000);
            return false;
        }
        if(!result.fatalError.isEmpty())
        {
            QMessageBox::critical(
                this, tr("Unable to Save Annotation"), result.fatalError);
            return false;
        }
        if(!result.accepted() || !result.saved)
        {
            if(const auto slice = firstValidationIssueSlice(result))
            {
                viewer_->goToAxialSlice(*slice);
            }
            QMessageBox::warning(
                this,
                tr("Annotation Validation Failed"),
                tr("The annotation was not saved.\n\n%1")
                    .arg(validationFailureText(result)));
            return false;
        }
        if(updateSourcePath)
        {
            const bool firstSave = annotation->sourcePath().empty();
            annotation->setSourcePath(fileSystemPath(destinationPath));
            if(firstSave)
            {
                const QString displayName = QFileInfo(destinationPath).fileName();
                const QByteArray utf8Name = displayName.toUtf8();
                annotation->setName(
                    std::string(utf8Name.constData(), utf8Name.size()));
                const auto found = std::find(
                    annotations_.begin(), annotations_.end(), annotation);
                if(found != annotations_.end())
                {
                    toolbox_->setAnnotationName(
                        static_cast<int>(found - annotations_.begin()),
                        displayName);
                }
            }
        }
        annotation->markSaved();
        statusBar()->showMessage(
            tr("Saved annotation to %1").arg(
                QDir::toNativeSeparators(destinationPath)),
            6000);
        return true;
    }
    catch(const std::exception& exception)
    {
        QMessageBox::critical(
            this,
            tr("Unable to Save Annotation"),
            QString::fromUtf8(exception.what()));
        return false;
    }
}

bool MainWindow::resolveUnsavedAnnotations(
    const std::vector<std::shared_ptr<core::Annotation>>& candidates,
    const QString& operation)
{
    std::vector<std::shared_ptr<core::Annotation>> modified;
    std::copy_if(
        candidates.begin(), candidates.end(), std::back_inserter(modified),
        [](const auto& annotation) {
            return annotation != nullptr && annotation->isModified();
        });
    if(modified.empty())
    {
        return true;
    }

    QStringList names;
    names.reserve(static_cast<qsizetype>(modified.size()));
    for(const auto& annotation : modified)
    {
        names.push_back(QString::fromUtf8(
            annotation->name().data(),
            static_cast<qsizetype>(annotation->name().size())));
    }

    QMessageBox prompt(this);
    prompt.setIcon(QMessageBox::Warning);
    prompt.setWindowTitle(tr("Unsaved Annotations"));
    prompt.setText(
        modified.size() == 1
            ? tr("“%1” has unsaved changes.").arg(names.front())
            : tr("%1 annotations have unsaved changes.").arg(modified.size()));
    prompt.setInformativeText(
        tr("Do you want to save your changes before %1?").arg(operation));
    if(modified.size() > 1)
    {
        prompt.setDetailedText(names.join(QChar('\n')));
    }
    prompt.setStandardButtons(
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    prompt.setDefaultButton(QMessageBox::Save);
    prompt.setEscapeButton(QMessageBox::Cancel);
    if(modified.size() > 1)
    {
        prompt.button(QMessageBox::Save)->setText(tr("Save All"));
    }

    const auto choice = static_cast<QMessageBox::StandardButton>(prompt.exec());
    if(choice == QMessageBox::Cancel)
    {
        return false;
    }
    if(choice == QMessageBox::Discard)
    {
        return true;
    }
    if(choice != QMessageBox::Save)
    {
        return false;
    }
    for(const auto& annotation : modified)
    {
        if(!saveAnnotation(annotation, false))
        {
            return false;
        }
    }
    return true;
}

void MainWindow::setImageDependentActionsEnabled(const bool enabled)
{
    for(auto* const action : imageDependentActions_)
    {
        action->setEnabled(enabled);
    }
    if(layoutButton_ != nullptr)
    {
        layoutButton_->setEnabled(enabled);
    }
}

void MainWindow::updateActiveLabelActions()
{
    const bool enabled = canUseAnnotationDigitShortcuts()
        && !isTextEntryWidget(QApplication::focusWidget());
    for(auto* const action : activeLabelActions_)
    {
        action->setEnabled(enabled);
    }
}

bool MainWindow::canUseAnnotationDigitShortcuts() const
{
    if(brushAction_ != nullptr && brushAction_->isEnabled())
    {
        return true;
    }
    return annotations_.size() == 1
        && annotations_.front()->kind() == core::AnnotationKind::LabelMap;
}

bool MainWindow::ensureEditableAnnotationForShortcut()
{
    if(selectedEditableAnnotation() != nullptr)
    {
        return true;
    }
    if(annotations_.size() != 1
       || annotations_.front()->kind() != core::AnnotationKind::LabelMap)
    {
        return false;
    }
    toolbox_->selectAnnotation(0);
    setAnnotationSelection({0});
    if(annotationLabelsDock_ != nullptr)
    {
        annotationLabelsDock_->show();
    }
    return selectedEditableAnnotation() != nullptr;
}

void MainWindow::activateAnnotationDigit(const int digit)
{
    if(!ensureEditableAnnotationForShortcut())
    {
        return;
    }
    if(digit == 0)
    {
        eraseAction_->setChecked(true);
        viewer_->setEraseTool();
        return;
    }
    brushAction_->setChecked(true);
    viewer_->setBrushTool();
    toolbox_->setActiveLabel(digit);
}

void MainWindow::loadAnnotations(const QStringList& fileNames)
{
    if(fileNames.isEmpty())
    {
        return;
    }
    if(!primaryVolume_)
    {
        QMessageBox::warning(
            this,
            tr("Unable to Add Annotation"),
            tr("Open an anatomical image before adding annotations."));
        return;
    }
    if(!std::all_of(fileNames.begin(), fileNames.end(), isNiftiFile))
    {
        QMessageBox::warning(
            this,
            tr("Unable to Add Annotation"),
            tr("Annotations must be NIfTI files ending in .nii or .nii.gz."));
        return;
    }

    beginImportProgress(tr("Preparing annotation import…"));
    const std::size_t originalCount = annotations_.size();
    try
    {
        std::vector<std::shared_ptr<core::Annotation>> pending;
        pending.reserve(static_cast<std::size_t>(fileNames.size()));
        for(qsizetype index = 0; index < fileNames.size(); ++index)
        {
            const QString& fileName = fileNames[index];
            const double fileStart = static_cast<double>(index)
                / static_cast<double>(fileNames.size());
            const double fileShare = 1.0 / static_cast<double>(fileNames.size());
            updateImportProgress(
                5 + static_cast<int>(fileStart * 80.0),
                tr("Loading annotation %1 of %2…")
                    .arg(index + 1)
                    .arg(fileNames.size()));
            const auto path = fileSystemPath(fileName);
            const auto componentKind = io::NiftiReader::componentKind(path);
            const auto progress = [this, fileStart, fileShare, index, fileNames](
                                      const double fraction) {
                updateImportProgress(
                    5 + static_cast<int>(
                            (fileStart
                             + std::clamp(fraction, 0.0, 1.0) * fileShare)
                            * 80.0),
                    tr("Loading annotation %1 of %2…")
                        .arg(index + 1)
                        .arg(fileNames.size()));
            };
            auto volume = io::NiftiReader::read(path, progress);
            const QString displayName = parentPrefixedFileName(fileName);
            const QByteArray utf8Name = displayName.toUtf8();
            const auto kind = componentKind
                    == io::NiftiReader::ComponentKind::FloatingPoint
                ? core::AnnotationKind::ScalarMap
                : core::AnnotationKind::LabelMap;
            auto annotation = std::make_shared<core::Annotation>(
                std::string(utf8Name.constData(), utf8Name.size()),
                path,
                std::move(volume),
                kind);
            annotation->conformGeometry(primaryVolume_->geometry());
            pending.push_back(std::move(annotation));
        }

        updateImportProgress(90, tr("Preparing annotation overlays…"));
        for(const auto& annotation : pending)
        {
            viewer_->addAnnotation(annotation);
            annotations_.push_back(annotation);
        }
        for(const auto& annotation : pending)
        {
            toolbox_->addAnnotation(
                QString::fromUtf8(
                    annotation->name().data(),
                    static_cast<qsizetype>(annotation->name().size())),
                annotation->kind() == core::AnnotationKind::LabelMap
                    ? tr("Labels")
                    : tr("Scalar"),
                annotation->opacity(),
                annotation->isVisible());
        }
        for(std::size_t offset = pending.size(); offset > 0; --offset)
        {
            if(pending[offset - 1]->kind() == core::AnnotationKind::LabelMap)
            {
                const int selectedIndex = static_cast<int>(
                    originalCount + offset - 1);
                toolbox_->selectAnnotation(selectedIndex);
                setAnnotationSelection({selectedIndex});
                break;
            }
        }
        finishImportProgress();
        updateActiveLabelActions();
        statusBar()->showMessage(
            tr("Added %1 annotation layer(s)").arg(fileNames.size()), 5000);
    }
    catch(const std::exception& exception)
    {
        while(annotations_.size() > originalCount)
        {
            viewer_->removeAnnotation(annotations_.size() - 1);
            annotations_.pop_back();
        }
        finishImportProgress();
        QMessageBox::critical(
            this,
            tr("Unable to Add Annotation"),
            QString::fromUtf8(exception.what()));
        statusBar()->showMessage(tr("Annotation loading failed"), 5000);
    }
}

void MainWindow::loadInputs(
    const QStringList& fileNames,
    RecentAnnotationsState recentAnnotations)
{
    if(fileNames.isEmpty())
    {
        return;
    }
    beginImportProgress(tr("Preparing medical image import…"));
    try
    {
        const auto niftiCount = std::count_if(
            fileNames.begin(), fileNames.end(), isNiftiFile);
        const auto archiveCount = std::count_if(
            fileNames.begin(), fileNames.end(), isArchiveFile);
        if(niftiCount > 0)
        {
            if(fileNames.size() != 1 || niftiCount != 1)
            {
                throw std::runtime_error(
                    "Open one NIfTI image at a time; do not mix it with DICOM files");
            }
            loadNifti(fileNames.front(), std::move(recentAnnotations));
            finishImportProgress();
            return;
        }
        if(archiveCount > 0 && (fileNames.size() != 1 || archiveCount != 1))
        {
            throw std::runtime_error(
                "Open one ZIP or TAR.GZ archive at a time");
        }

        QString sourceName = dicomSourceName(fileNames);
        const auto cancellation = std::make_shared<std::atomic_bool>(false);
        enableImportCancellation(cancellation);
        std::shared_ptr<QTemporaryDir> extractionDirectory;
        if(archiveCount == 1)
        {
            extractionDirectory = std::make_shared<QTemporaryDir>();
            if(!extractionDirectory->isValid())
            {
                throw std::runtime_error(
                    "Unable to create a temporary archive directory");
            }
            updateImportProgress(8, tr("Extracting archive…"));
            startArchiveDicomImport(
                fileSystemPath(fileNames.front()),
                sourceName,
                fileNames,
                cancellation,
                std::move(recentAnnotations),
                std::move(extractionDirectory));
            return;
        }

        std::vector<std::filesystem::path> dicomPaths;
        dicomPaths.reserve(static_cast<std::size_t>(fileNames.size()));
        for(const auto& fileName : fileNames)
        {
            dicomPaths.push_back(fileSystemPath(fileName));
        }
        startDicomImport(
            dicomPaths,
            sourceName,
            fileNames,
            cancellation,
            std::move(recentAnnotations));
    }
    catch(const std::exception& exception)
    {
        finishImportProgress();
        QMessageBox::critical(
            this, tr("Unable to Open Image"), QString::fromUtf8(exception.what()));
        statusBar()->showMessage(tr("Image loading failed"), 5000);
    }
}

void MainWindow::startArchiveDicomImport(
    const std::filesystem::path& archivePath,
    const QString& sourceName,
    const QStringList& sourceFiles,
    const std::shared_ptr<std::atomic_bool>& cancellation,
    RecentAnnotationsState recentAnnotations,
    std::shared_ptr<QTemporaryDir> extractionDirectory)
{
    const auto importGeneration = importGeneration_;
    const QPointer<MainWindow> window(this);
    const auto destination = fileSystemPath(extractionDirectory->path());
    auto* const watcher = new QFutureWatcher<ArchiveExtractionOutcome>(this);
    connect(
        watcher,
        &QFutureWatcher<ArchiveExtractionOutcome>::finished,
        this,
        [this,
         watcher,
         sourceName,
         sourceFiles,
         importGeneration,
         cancellation,
         recentState = std::move(recentAnnotations),
         extractionDirectory = std::move(extractionDirectory)]() mutable {
            if(importGeneration != importGeneration_)
            {
                watcher->deleteLater();
                return;
            }
            try
            {
                auto outcome = watcher->result();
                if(!outcome.error.isEmpty())
                {
                    watcher->deleteLater();
                    finishImportProgress();
                    QMessageBox::critical(
                        this, tr("Unable to Open Image"), outcome.error);
                    statusBar()->showMessage(tr("Image loading failed"), 5000);
                    return;
                }
                qInfo().noquote()
                    << QStringLiteral("[PERF] DICOM archive extraction = %1 ms")
                           .arg(outcome.milliseconds, 0, 'f', 1);
                watcher->deleteLater();
                startDicomImport(
                    outcome.paths,
                    sourceName,
                    sourceFiles,
                    cancellation,
                    std::move(recentState),
                    std::move(extractionDirectory));
            }
            catch(const std::exception& exception)
            {
                watcher->deleteLater();
                finishImportProgress();
                QMessageBox::critical(
                    this,
                    tr("Unable to Open Image"),
                    QString::fromUtf8(exception.what()));
                statusBar()->showMessage(tr("Image loading failed"), 5000);
            }
        });

    watcher->setFuture(QtConcurrent::run(
        // NOLINTNEXTLINE(bugprone-exception-escape)
        [window, archivePath, destination, cancellation, importGeneration]() {
            const auto start = ImportClock::now();
            ArchiveExtractionOutcome outcome;
            outcome.milliseconds = importElapsedMilliseconds(start);
            try
            {
                const auto progress =
                    [window, importGeneration](const double fraction) {
                    if(window.isNull())
                    {
                        return;
                    }
                    QMetaObject::invokeMethod(
                        window.data(),
                        [window, importGeneration, fraction]() {
                            if(window.isNull()
                               || importGeneration != window->importGeneration_)
                            {
                                return;
                            }
                            window->updateImportProgress(
                                5 + static_cast<int>(
                                        std::clamp(fraction, 0.0, 1.0) * 14.0),
                                window->tr("Extracting archive…"));
                        },
                        Qt::QueuedConnection);
                };
                const auto cancelled = [cancellation] {
                    return cancellation->load(std::memory_order_relaxed);
                };
                outcome.paths = io::ArchiveExtractor::extract(
                    archivePath, destination, progress, cancelled);
                outcome.milliseconds = importElapsedMilliseconds(start);
            }
            catch(const std::exception& exception)
            {
                outcome.milliseconds = importElapsedMilliseconds(start);
                outcome.error = QString::fromUtf8(exception.what());
            }
            catch(...)
            {
                outcome.milliseconds = importElapsedMilliseconds(start);
                outcome.error = QStringLiteral("An unexpected error occurred");
            }
            return outcome;
        }));
}

void MainWindow::loadNifti(
    const QString& fileName,
    RecentAnnotationsState recentAnnotations)
{
    updateImportProgress(10, tr("Loading NIfTI volume…"));
    const auto progress = [this](const double fraction) {
        updateImportProgress(
            10 + static_cast<int>(std::clamp(fraction, 0.0, 1.0) * 78.0),
            tr("Loading NIfTI volume…"));
    };
    auto volume = io::NiftiReader::read(fileSystemPath(fileName), progress);
    updateImportProgress(90, tr("Preparing viewer…"));
    displayVolume(
        std::move(volume),
        parentPrefixedFileName(fileName),
        QStringLiteral("NIfTI"),
        {fileName},
        std::move(recentAnnotations));
}

void MainWindow::startDicomImport(
    const std::vector<std::filesystem::path>& filePaths,
    const QString& sourceName,
    const QStringList& sourceFiles,
    const std::shared_ptr<std::atomic_bool>& cancellation,
    RecentAnnotationsState recentAnnotations,
    std::shared_ptr<QTemporaryDir> extractionDirectory)
{
    updateImportProgress(20, tr("Reading DICOM metadata…"));
    const auto importGeneration = importGeneration_;
    const auto scanExtractionDirectory = extractionDirectory;
    auto* const scanWatcher =
        new QFutureWatcher<DicomScanOutcome>(this);
    connect(
        scanWatcher,
        &QFutureWatcher<DicomScanOutcome>::finished,
        this,
        [this,
         scanWatcher,
         sourceName,
         sourceFiles,
         importGeneration,
         cancellation,
         recentState = std::move(recentAnnotations),
         extractionDirectory = std::move(extractionDirectory)]() mutable {
            if(importGeneration != importGeneration_)
            {
                scanWatcher->deleteLater();
                return;
            }
            try
            {
                auto scanOutcome = scanWatcher->result();
                if(!scanOutcome.error.isEmpty())
                {
                    scanWatcher->deleteLater();
                    finishImportProgress();
                    QMessageBox::critical(
                        this, tr("Unable to Open Image"), scanOutcome.error);
                    statusBar()->showMessage(tr("Image loading failed"), 5000);
                    return;
                }
                qInfo().noquote()
                    << QStringLiteral(
                           "[PERF] DICOM metadata scan = %1 ms (%2 files)")
                           .arg(scanOutcome.milliseconds, 0, 'f', 1)
                           .arg(static_cast<qulonglong>(scanOutcome.records.size()));
                const auto& records = scanOutcome.records;
                auto analysis = io::analyzeDicomSeries(records);
                if(analysis.series.empty())
                {
                    throw std::runtime_error(
                        "No readable DICOM files with a Series Instance UID were found");
                }

                std::vector<io::DicomFileRecord> selectedRecords;
                auto geometryPolicy = io::DicomReadGeometryPolicy::Strict;
                if(analysis.canAutomaticallyImport())
                {
                    const auto& indices = analysis.series.front().recordIndices;
                    selectedRecords.reserve(indices.size());
                    for(const auto index : indices)
                    {
                        if(index >= records.size())
                        {
                            throw std::runtime_error(
                                "DICOM series contains an invalid file index");
                        }
                        selectedRecords.push_back(records[index]);
                    }
                }
                else
                {
                    importProgress_->hide();
                    DicomSeriesDialog dialog(records, std::move(analysis), this);
                    if(dialog.exec() != QDialog::Accepted)
                    {
                        scanWatcher->deleteLater();
                        finishImportProgress();
                        statusBar()->showMessage(
                            tr("DICOM import cancelled"), 5000);
                        return;
                    }
                    selectedRecords = dialog.selectedRecords();
                    geometryPolicy =
                        dialog.selectedSeriesRequiresSliceSpacingOverride()
                        ? io::DicomReadGeometryPolicy::AllowSliceSpacingOverride
                        : io::DicomReadGeometryPolicy::Strict;
                    importProgress_->show();
                }
                QStringList loadedSourceFiles = sourceFiles;
                if(std::any_of(
                       sourceFiles.begin(), sourceFiles.end(), [](const QString& path) {
                           return QFileInfo(path).isDir();
                       }))
                {
                    loadedSourceFiles.clear();
                    loadedSourceFiles.reserve(
                        static_cast<qsizetype>(selectedRecords.size()));
                    for(const auto& record : selectedRecords)
                    {
                        loadedSourceFiles.push_back(qtPath(record.filePath));
                    }
                }
                updateImportProgress(
                    35,
                    tr("Loading %1 DICOM slices…").arg(selectedRecords.size()));
                auto* const readWatcher =
                    new QFutureWatcher<DicomReadOutcome>(this);
                connect(
                    readWatcher,
                    &QFutureWatcher<DicomReadOutcome>::finished,
                    this,
                    [this,
                     readWatcher,
                     sourceName,
                     sourceFiles = std::move(loadedSourceFiles),
                     importGeneration,
                     recentState = std::move(recentState)]() mutable {
                        if(importGeneration != importGeneration_)
                        {
                            readWatcher->deleteLater();
                            return;
                        }
                        try
                        {
                            auto outcome = readWatcher->result();
                            if(!outcome.error.isEmpty())
                            {
                                readWatcher->deleteLater();
                                finishImportProgress();
                                QMessageBox::critical(
                                    this,
                                    tr("Unable to Open Image"),
                                    outcome.error);
                                statusBar()->showMessage(
                                    tr("Image loading failed"), 5000);
                                return;
                            }
                            const auto& timings = outcome.timings;
                            qInfo().noquote()
                                << QStringLiteral(
                                       "[PERF] DICOM geometry/setup = %1 ms; "
                                       "decode = %2 ms (%3 workers); finalize = %4 ms; "
                                       "metadata = %5 ms")
                                       .arg(
                                           timings.geometrySetupMilliseconds,
                                           0,
                                           'f',
                                           1)
                                       .arg(
                                           timings.pixelDecodeMilliseconds,
                                           0,
                                           'f',
                                           1)
                                       .arg(static_cast<qulonglong>(
                                           timings.workerCount))
                                       .arg(
                                           timings.volumeFinalizeMilliseconds,
                                           0,
                                           'f',
                                           1)
                                       .arg(
                                           timings.metadataMilliseconds,
                                           0,
                                           'f',
                                           1);
                            updateImportProgress(90, tr("Preparing viewer…"));
                            displayVolume(
                                std::move(outcome.volume),
                                sourceName,
                                QStringLiteral("DICOM"),
                                sourceFiles,
                                std::move(recentState));
                            readWatcher->deleteLater();
                            finishImportProgress();
                        }
                        catch(const std::exception& exception)
                        {
                            readWatcher->deleteLater();
                            finishImportProgress();
                            QMessageBox::critical(
                                this,
                                tr("Unable to Open Image"),
                                QString::fromUtf8(exception.what()));
                            statusBar()->showMessage(tr("Image loading failed"), 5000);
                        }
                    });
                const auto count = selectedRecords.size();
                const QPointer<MainWindow> window(this);
                readWatcher->setFuture(QtConcurrent::run(
                    // NOLINTNEXTLINE(bugprone-exception-escape)
                    [window,
                     selectedRecords = std::move(selectedRecords),
                     count,
                     importGeneration,
                     cancellation,
                     geometryPolicy,
                     extractionDirectory = std::move(extractionDirectory)]() {
                        // The archive directory must outlive the reader that uses its files.
                        static_cast<void>(extractionDirectory);
                        DicomReadOutcome outcome;
                        try
                        {
                            const auto progress =
                                [window, count, importGeneration](
                                    const double fraction) {
                                if(window.isNull())
                                {
                                    return;
                                }
                                QMetaObject::invokeMethod(
                                    window.data(),
                                    [window, count, fraction, importGeneration]() {
                                        if(window.isNull())
                                        {
                                            return;
                                        }
                                        if(importGeneration
                                           != window->importGeneration_)
                                        {
                                            return;
                                        }
                                        window->updateImportProgress(
                                            35 + static_cast<int>(
                                                     std::clamp(fraction, 0.0, 1.0)
                                                     * 53.0),
                                            window->tr("Loading %1 DICOM slices…")
                                                .arg(count));
                                    },
                                    Qt::QueuedConnection);
                            };
                            const auto cancelled = [cancellation] {
                                return cancellation->load(std::memory_order_relaxed);
                            };
                            outcome.volume = io::DicomReader::read(
                                selectedRecords,
                                progress,
                                cancelled,
                                &outcome.timings,
                                geometryPolicy);
                        }
                        catch(const std::exception& exception)
                        {
                            outcome.error = QString::fromUtf8(exception.what());
                        }
                        catch(...)
                        {
                            outcome.error =
                                QStringLiteral("An unexpected error occurred");
                        }
                        return outcome;
                    }));
                scanWatcher->deleteLater();
            }
            catch(const std::exception& exception)
            {
                scanWatcher->deleteLater();
                finishImportProgress();
                QMessageBox::critical(
                    this,
                    tr("Unable to Open Image"),
                    QString::fromUtf8(exception.what()));
                statusBar()->showMessage(tr("Image loading failed"), 5000);
            }
        });
    scanWatcher->setFuture(QtConcurrent::run(
        // NOLINTNEXTLINE(bugprone-exception-escape)
        [filePaths, scanExtractionDirectory, cancellation]() {
            static_cast<void>(scanExtractionDirectory);
            const auto start = ImportClock::now();
            DicomScanOutcome outcome;
            try
            {
                const auto cancelled = [cancellation] {
                    return cancellation->load(std::memory_order_relaxed);
                };
                outcome.records = io::DicomReader::scan(filePaths, cancelled);
                outcome.milliseconds = importElapsedMilliseconds(start);
            }
            catch(const std::exception& exception)
            {
                outcome.milliseconds = importElapsedMilliseconds(start);
                outcome.error = QString::fromUtf8(exception.what());
            }
            catch(...)
            {
                outcome.milliseconds = importElapsedMilliseconds(start);
                outcome.error = QStringLiteral("An unexpected error occurred");
            }
            return outcome;
        }));
}

void MainWindow::displayVolume(
    std::shared_ptr<core::Volume> volume,
    const QString& sourceName,
    const QString& sourceKind,
    const QStringList& sourceFiles,
    RecentAnnotationsState recentAnnotations)
{
    if(primaryVolume_
       && !resolveUnsavedAnnotations(
           annotations_, tr("opening another image")))
    {
        statusBar()->showMessage(
            tr("Kept the current image and its unsaved annotations"), 5000);
        return;
    }
    invertAction_->setChecked(false);
    if(primaryVolume_)
    {
        rememberRecentImage(
            primarySourceName_, primarySourceKind_, primarySourceFiles_);
        viewer_->clearVolume();
        annotations_.clear();
        toolbox_->clearAnnotations();
        updateActiveLabelActions();
    }
    QElapsedTimer viewerTimer;
    viewerTimer.start();
    viewer_->setVolume(volume);
    qInfo().noquote()
        << QStringLiteral("[PERF] %1 viewer handoff/initial render = %2 ms")
               .arg(sourceKind)
               .arg(viewerTimer.elapsed());
    if(const auto& defaultWindowLevel = settings_.defaultWindowLevel())
    {
        viewer_->setWindowLevel(
            defaultWindowLevel->window, defaultWindowLevel->level);
    }
    primaryVolume_ = std::move(volume);
    primarySourceFiles_ = sourceFiles;
    primarySourceName_ = sourceName;
    primarySourceKind_ = sourceKind;
    dicomHeaderAction_->setEnabled(!primaryVolume_->dicomMetadata().empty());
    centralStack_->setCurrentWidget(viewer_);
    applyImageDockLayout();
    setImageDependentActionsEnabled(true);

    const auto& geometry = primaryVolume_->geometry();
    const auto& dimensions = geometry.dimensions();
    const auto& spacing = geometry.spacing();
    const auto& origin = geometry.origin();
    const auto& direction = geometry.direction();
    const auto range = primaryVolume_->scalarRange();

    qInfo().noquote()
        << QStringLiteral("[IO] Loaded %1: %2").arg(sourceKind, sourceName);
    qInfo().noquote()
        << QStringLiteral("[GEOMETRY] dimensions = %1, %2, %3")
               .arg(static_cast<qulonglong>(dimensions[0]))
               .arg(static_cast<qulonglong>(dimensions[1]))
               .arg(static_cast<qulonglong>(dimensions[2]));
    qInfo().noquote()
        << QStringLiteral("[GEOMETRY] spacing = %1, %2, %3")
               .arg(spacing[0])
               .arg(spacing[1])
               .arg(spacing[2]);
    qInfo().noquote()
        << QStringLiteral("[GEOMETRY] origin = %1, %2, %3")
               .arg(origin[0])
               .arg(origin[1])
               .arg(origin[2]);
    for(std::size_t row = 0; row < 3; ++row)
    {
        qInfo().noquote()
            << QStringLiteral("[GEOMETRY] direction[%1] = %2, %3, %4")
                   .arg(static_cast<qulonglong>(row))
                   .arg(direction[row][0])
                   .arg(direction[row][1])
                   .arg(direction[row][2]);
    }
    qInfo().noquote()
        << QStringLiteral("[RENDER] intensity range = %1 .. %2")
               .arg(range.minimum)
               .arg(range.maximum);

    setWindowTitle(QStringLiteral("%1 — %2")
                       .arg(QString::fromUtf8(
                           app::applicationName().data(),
                           static_cast<qsizetype>(app::applicationName().size())))
                        .arg(sourceName));
    volumeDimensionsLabel_->setText(
        tr("%1 × %2 × %3 voxels")
            .arg(static_cast<qulonglong>(dimensions[2]))
            .arg(static_cast<qulonglong>(dimensions[1]))
            .arg(static_cast<qulonglong>(dimensions[0])));
    volumeDimensionsLabel_->setVisible(true);
    volumeDimensionsLabel_->setToolTip(
        tr("Volume dimensions: %1 × %2 × %3 voxels\n"
           "Voxel spacing: %4 × %5 × %6 mm")
            .arg(static_cast<qulonglong>(dimensions[0]))
            .arg(static_cast<qulonglong>(dimensions[1]))
            .arg(static_cast<qulonglong>(dimensions[2]))
            .arg(spacing[0], 0, 'g', 4)
            .arg(spacing[1], 0, 'g', 4)
            .arg(spacing[2], 0, 'g', 4));
    statusBar()->clearMessage();

    if(!recentAnnotations.annotations.empty())
    {
        QTimer::singleShot(
            0,
            this,
            [this, recentState = std::move(recentAnnotations)]() mutable {
                restoreRecentAnnotations(std::move(recentState));
            });
    }
}

void MainWindow::restoreRecentAnnotations(
    RecentAnnotationsState recentAnnotations)
{
    if(!primaryVolume_ || primarySourceFiles_ != recentAnnotations.sourceFiles)
    {
        return;
    }

    QStringList sourceFiles;
    std::vector<app::RecentAnnotationSetting> available;
    sourceFiles.reserve(static_cast<qsizetype>(recentAnnotations.annotations.size()));
    available.reserve(recentAnnotations.annotations.size());
    for(auto& annotation : recentAnnotations.annotations)
    {
        if(QFileInfo(annotation.sourceFile).isFile())
        {
            sourceFiles.push_back(annotation.sourceFile);
            available.push_back(std::move(annotation));
        }
        else
        {
            qWarning().noquote()
                << "[SETTINGS] Recent annotation no longer exists:"
                << annotation.sourceFile;
        }
    }
    if(sourceFiles.isEmpty())
    {
        statusBar()->showMessage(
            tr("The annotations saved with this recent image are no longer available"),
            5000);
        return;
    }

    const std::size_t originalCount = annotations_.size();
    loadAnnotations(sourceFiles);
    if(annotations_.size() != originalCount + available.size())
    {
        return;
    }
    for(std::size_t index = 0; index < available.size(); ++index)
    {
        setAnnotationOpacity(
            static_cast<int>(originalCount + index), available[index].opacity);
    }
    toolbox_->setActiveLabel(recentAnnotations.activeLabel);
    statusBar()->showMessage(
        tr("Restored %1 annotation layer(s)").arg(available.size()), 5000);
}

void MainWindow::rememberRecentImage(
    const QString& sourceName,
    const QString& sourceKind,
    const QStringList& sourceFiles)
{
    if(sourceFiles.isEmpty())
    {
        return;
    }
    const QByteArray key = QCryptographicHash::hash(
                               sourceFiles.join(QChar(0x001f)).toUtf8(),
                               QCryptographicHash::Sha256)
                               .toHex();
    QDir().mkpath(settings_.thumbnailDirectoryPath());
    const QString thumbnailPath =
        QDir(settings_.thumbnailDirectoryPath())
            .filePath(QString::fromLatin1(key) + QStringLiteral(".png"));
    const QImage thumbnail = viewer_->captureMiddleSliceThumbnail()
                                 .convertToFormat(QImage::Format_Grayscale8);
    QByteArray thumbnailPixels;
    if(thumbnail.isNull())
    {
        qWarning().noquote()
            << "[SETTINGS] Unable to create recent thumbnail for" << sourceName;
    }
    else
    {
        thumbnailPixels.resize(thumbnail.width() * thumbnail.height());
        for(int row = 0; row < thumbnail.height(); ++row)
        {
            std::copy_n(
                thumbnail.constScanLine(row),
                thumbnail.width(),
                thumbnailPixels.begin() + row * thumbnail.width());
        }

        QImageWriter writer(thumbnailPath, QByteArrayLiteral("png"));
        if(!writer.write(thumbnail))
        {
            qWarning().noquote()
                << "[SETTINGS] Unable to save recent thumbnail" << thumbnailPath
                << '-' << writer.errorString();
        }
    }
    std::vector<app::RecentAnnotationSetting> recentAnnotations;
    recentAnnotations.reserve(annotations_.size());
    for(const auto& annotation : annotations_)
    {
        if(annotation == nullptr || annotation->sourcePath().empty())
        {
            continue;
        }
        recentAnnotations.push_back(app::RecentAnnotationSetting{
            qtPath(annotation->sourcePath()), annotation->opacity()});
    }
    settings_.addRecentImage(app::RecentImageSetting{
        sourceName,
        sourceKind,
        sourceFiles,
        thumbnailPath,
        std::move(thumbnailPixels),
        thumbnail.width(),
        thumbnail.height(),
        std::move(recentAnnotations),
        toolbox_->activeLabel(),
    });
    refreshRecentImages();
}

void MainWindow::refreshRecentImages()
{
    recentImages_->clear();
    const auto& recents = settings_.recentImages();
    for(std::size_t index = 0; index < recents.size(); ++index)
    {
        const auto& recent = recents[index];
        QPixmap thumbnailPixmap;
        const qint64 expectedThumbnailBytes =
            static_cast<qint64>(recent.thumbnailWidth) * recent.thumbnailHeight;
        if(recent.thumbnailWidth > 0 && recent.thumbnailHeight > 0
           && expectedThumbnailBytes == recent.thumbnailPixels.size())
        {
            const QImage embeddedThumbnail(
                reinterpret_cast<const uchar*>(recent.thumbnailPixels.constData()),
                recent.thumbnailWidth,
                recent.thumbnailHeight,
                recent.thumbnailWidth,
                QImage::Format_Grayscale8);
            thumbnailPixmap = QPixmap::fromImage(embeddedThumbnail.copy());
        }
        if(thumbnailPixmap.isNull())
        {
            thumbnailPixmap.load(recent.thumbnailPath);
        }
        if(thumbnailPixmap.isNull())
        {
            thumbnailPixmap = svgIcon(QStringLiteral(":/icons/open.svg"))
                                  .pixmap(QSize(72, 72));
        }
        auto* const item = new QTreeWidgetItem(recentImages_);
        item->setData(0, Qt::UserRole, static_cast<int>(index));
        item->setSizeHint(0, QSize(280, 92));

        auto* const identityCell = new QWidget(recentImages_);
        identityCell->setObjectName(QStringLiteral("recentImageIdentityCell"));
        identityCell->setAttribute(Qt::WA_TransparentForMouseEvents);
        auto* const identityLayout = new QHBoxLayout(identityCell);
        identityLayout->setContentsMargins(6, 4, 6, 4);
        identityLayout->setSpacing(10);
        auto* const thumbnailLabel = new QLabel(identityCell);
        thumbnailLabel->setObjectName(QStringLiteral("recentImageThumbnail"));
        thumbnailLabel->setFixedSize(QSize(120, 80));
        thumbnailLabel->setAlignment(Qt::AlignCenter);
        thumbnailLabel->setPixmap(thumbnailPixmap.scaled(
            thumbnailLabel->size(),
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation));
        auto* const identityText = new QWidget(identityCell);
        auto* const identityTextLayout = new QVBoxLayout(identityText);
        identityTextLayout->setContentsMargins(0, 0, 0, 0);
        identityTextLayout->setSpacing(2);
        identityTextLayout->setAlignment(Qt::AlignVCenter);
        auto* const nameLabel = new QLabel(recent.name, identityText);
        nameLabel->setObjectName(QStringLiteral("recentImageName"));
        nameLabel->setWordWrap(true);
        auto* const typeLabel = new QLabel(recent.kind, identityText);
        typeLabel->setObjectName(QStringLiteral("recentImageType"));
        identityTextLayout->addWidget(nameLabel);
        identityTextLayout->addWidget(typeLabel);
        identityLayout->addWidget(thumbnailLabel);
        identityLayout->addWidget(identityText, 1);
        recentImages_->setItemWidget(item, 0, identityCell);

        QString pathText = QDir::toNativeSeparators(recent.sourceFiles.front());
        if(recent.sourceFiles.size() > 1)
        {
            pathText = tr("%1  (+%2 more files)")
                           .arg(pathText)
                           .arg(recent.sourceFiles.size() - 1);
        }
        const QString allPaths = recent.sourceFiles.join(QChar('\n'));
        item->setText(1, pathText);
        item->setToolTip(0, allPaths);
        item->setToolTip(1, allPaths);

        auto* const folderCell = new QWidget(recentImages_);
        auto* const folderLayout = new QHBoxLayout(folderCell);
        folderLayout->setContentsMargins(0, 0, 0, 0);
        folderLayout->setAlignment(Qt::AlignCenter);
        auto* const showInFolder = new QToolButton(folderCell);
        showInFolder->setObjectName(QStringLiteral("recentImageFolderButton"));
        showInFolder->setIcon(svgIcon(QStringLiteral(":/icons/open.svg")));
        showInFolder->setIconSize(QSize(16, 16));
        showInFolder->setFixedSize(QSize(28, 28));
        showInFolder->setToolTip(tr("Show in folder"));
        showInFolder->setAccessibleName(tr("Show %1 in folder").arg(recent.name));
        const QString filePath = recent.sourceFiles.front();
        connect(
            showInFolder,
            &QToolButton::clicked,
            this,
            [this, filePath] { showRecentImageInFolder(filePath); });
        auto* const removeRecent = new QToolButton(folderCell);
        removeRecent->setObjectName(QStringLiteral("removeRecentImageButton"));
        removeRecent->setIcon(svgIcon(QStringLiteral(":/icons/close.svg")));
        removeRecent->setIconSize(QSize(14, 14));
        removeRecent->setFixedSize(QSize(28, 28));
        removeRecent->setToolTip(tr("Remove from recent images"));
        removeRecent->setAccessibleName(
            tr("Remove %1 from recent images").arg(recent.name));
        connect(
            removeRecent,
            &QToolButton::clicked,
            this,
            [this, index] {
                if(settings_.removeRecentImage(index))
                {
                    refreshRecentImages();
                }
            });
        folderLayout->addWidget(showInFolder);
        folderLayout->addWidget(removeRecent);
        recentImages_->setItemWidget(item, 2, folderCell);
    }
}

void MainWindow::openRecentImage(const int index)
{
    const auto& recents = settings_.recentImages();
    if(index < 0 || static_cast<std::size_t>(index) >= recents.size())
    {
        return;
    }
    const auto recent = recents[static_cast<std::size_t>(index)];
    const QStringList files = recent.sourceFiles;
    const bool allExist = std::all_of(files.begin(), files.end(), [](const QString& file) {
        return QFileInfo(file).isFile();
    });
    if(!allExist)
    {
        settings_.removeMissingRecentImages();
        refreshRecentImages();
        QMessageBox::warning(
            this,
            tr("Recent Image Not Found"),
            tr("One or more source files for this recent image no longer exist."));
        return;
    }
    loadInputs(
        files,
        RecentAnnotationsState{
            files, recent.annotations, recent.activeLabel});
}

void MainWindow::showRecentImageInFolder(const QString& filePath)
{
    const QFileInfo file(filePath);
    if(!file.isFile())
    {
        settings_.removeMissingRecentImages();
        refreshRecentImages();
        QMessageBox::warning(
            this,
            tr("Recent Image Not Found"),
            tr("The source file for this recent image no longer exists."));
        return;
    }
    if(!QDesktopServices::openUrl(QUrl::fromLocalFile(file.absolutePath())))
    {
        QMessageBox::warning(
            this,
            tr("Unable to Open Folder"),
            tr("The folder could not be opened in the file manager."));
    }
}

void MainWindow::beginImportProgress(const QString& label)
{
    finishImportProgress();
    importProgress_ = new QProgressDialog(label, QString{}, 0, 100, this);
    importProgress_->setObjectName(QStringLiteral("imageImportProgress"));
    importProgress_->setWindowTitle(tr("Opening Medical Images"));
    importProgress_->setMinimumWidth(420);
    importProgress_->setWindowModality(Qt::ApplicationModal);
    importProgress_->setCancelButton(nullptr);
    importProgress_->setAutoClose(false);
    importProgress_->setAutoReset(false);
    importProgress_->setMinimumDuration(0);
    importProgress_->setValue(0);
    importProgress_->show();
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

void MainWindow::enableImportCancellation(
    const std::shared_ptr<std::atomic_bool>& cancellation)
{
    if(importProgress_ == nullptr)
    {
        return;
    }
    importCancellation_ = cancellation;
    auto* const cancelButton = new QPushButton(tr("Cancel"), importProgress_);
    importProgress_->setCancelButton(cancelButton);
    connect(
        importProgress_,
        &QProgressDialog::canceled,
        this,
        [this, cancellation] {
            if(importCancellation_ != cancellation)
            {
                return;
            }
            cancellation->store(true, std::memory_order_relaxed);
            finishImportProgress();
            statusBar()->showMessage(tr("DICOM import cancelled"), 5000);
        });
}

void MainWindow::updateImportProgress(const int value, const QString& label)
{
    if(importProgress_ == nullptr)
    {
        return;
    }
    if(!label.isEmpty())
    {
        importProgress_->setLabelText(label);
    }
    importProgress_->setValue(std::clamp(value, 0, 99));
}

void MainWindow::finishImportProgress()
{
    if(importCancellation_)
    {
        importCancellation_->store(true, std::memory_order_relaxed);
        importCancellation_.reset();
    }
    ++importGeneration_;
    if(importProgress_ == nullptr)
    {
        return;
    }
    const QPointer<QProgressDialog> progress(importProgress_);
    importProgress_ = nullptr;
    progress->setValue(100);
    if(!progress)
    {
        return;
    }
    progress->hide();
    progress->deleteLater();
}

void MainWindow::dragEnterEvent(QDragEnterEvent* const event)
{
    if(!droppedLocalInputs(event->mimeData()).isEmpty())
    {
        setDropActive(true);
        event->acceptProposedAction();
        return;
    }
    event->ignore();
}

void MainWindow::dragLeaveEvent(QDragLeaveEvent* const event)
{
    setDropActive(false);
    event->accept();
}

void MainWindow::dropEvent(QDropEvent* const event)
{
    setDropActive(false);
    const QStringList inputs = droppedLocalInputs(event->mimeData());
    if(inputs.isEmpty())
    {
        event->ignore();
        statusBar()->showMessage(
            tr("Drop a DICOM folder or local NIfTI, DICOM, ZIP, or TAR.GZ files"),
            5000);
        return;
    }
    event->acceptProposedAction();
    queueDroppedInputs(inputs);
}

bool MainWindow::eventFilter(QObject* const watched, QEvent* const event)
{
    auto* const widget = qobject_cast<QWidget*>(watched);
    if(widget == nullptr || (widget != this && !isAncestorOf(widget)))
    {
        return QMainWindow::eventFilter(watched, event);
    }
    switch(event->type())
    {
    case QEvent::DragEnter:
        dragEnterEvent(static_cast<QDragEnterEvent*>(event));
        return event->isAccepted();
    case QEvent::DragLeave:
        dragLeaveEvent(static_cast<QDragLeaveEvent*>(event));
        return true;
    case QEvent::Drop:
        dropEvent(static_cast<QDropEvent*>(event));
        return event->isAccepted();
    default:
        break;
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::queueDroppedInputs(const QStringList& inputs)
{
    // The MIME data belongs to the OS drag operation. Copy only the local paths
    // and return from the drop event before doing any archive or image I/O.
    QTimer::singleShot(0, this, [this, inputs] {
        const bool allNifti = std::all_of(
            inputs.begin(), inputs.end(), isNiftiFile);
        if(!allNifti)
        {
            loadInputs(inputs);
            return;
        }

        NiftiDropChoiceDialog choice(
            inputs, primaryVolume_ != nullptr, this);
        choice.exec();
        if(choice.choice() == NiftiDropChoiceDialog::Choice::AddAnnotations)
        {
            loadAnnotations(inputs);
        }
        else if(choice.choice() == NiftiDropChoiceDialog::Choice::OpenImage)
        {
            if(inputs.size() != 1)
            {
                QMessageBox::warning(
                    this,
                    tr("Open One Anatomical Image"),
                    tr("Choose one NIfTI file when opening a new anatomical image."));
                return;
            }
            loadInputs(inputs);
        }
    });
}

void MainWindow::removeAnnotation(const int index)
{
    if(index < 0 || static_cast<std::size_t>(index) >= annotations_.size())
    {
        return;
    }
    const auto& annotation = annotations_[static_cast<std::size_t>(index)];
    if(!resolveUnsavedAnnotations(
           std::vector<std::shared_ptr<core::Annotation>>{annotation},
           tr("removing this annotation")))
    {
        return;
    }
    const auto& utf8Name =
        annotation->name();
    const QString name = QString::fromUtf8(
        utf8Name.data(), static_cast<qsizetype>(utf8Name.size()));
    viewer_->removeAnnotation(static_cast<std::size_t>(index));
    annotations_.erase(annotations_.begin() + index);
    toolbox_->removeAnnotation(index);
    setAnnotationSelection(toolbox_->selectedAnnotationIndices());
    updateActiveLabelActions();
    statusBar()->showMessage(tr("Removed annotation %1").arg(name), 4000);
}

void MainWindow::setAnnotationOpacity(const int index, const double opacity)
{
    if(index < 0 || static_cast<std::size_t>(index) >= annotations_.size())
    {
        return;
    }
    try
    {
        auto& annotation = annotations_[static_cast<std::size_t>(index)];
        annotation->setOpacity(opacity);
        viewer_->setAnnotationOpacity(
            static_cast<std::size_t>(index), annotation->opacity());
        toolbox_->setAnnotationOpacity(index, annotation->opacity());
    }
    catch(const std::exception& exception)
    {
        qWarning().noquote()
            << "[RENDER] Annotation opacity update failed:" << exception.what();
    }
}

void MainWindow::setAnnotationVisibility(const int index, const bool visible)
{
    if(index < 0 || static_cast<std::size_t>(index) >= annotations_.size())
    {
        return;
    }
    try
    {
        auto& annotation = annotations_[static_cast<std::size_t>(index)];
        annotation->setVisible(visible);
        viewer_->setAnnotationVisibility(
            static_cast<std::size_t>(index), annotation->isVisible());
        toolbox_->setAnnotationVisibility(index, annotation->isVisible());
    }
    catch(const std::exception& exception)
    {
        qWarning().noquote()
            << "[RENDER] Annotation visibility update failed:" << exception.what();
    }
}

void MainWindow::setAnnotationSelection(const QList<int>& indices)
{
    std::vector<std::size_t> selected;
    selected.reserve(static_cast<std::size_t>(indices.size()));
    for(const int index : indices)
    {
        if(index >= 0 && static_cast<std::size_t>(index) < annotations_.size())
        {
            selected.push_back(static_cast<std::size_t>(index));
        }
    }
    try
    {
        viewer_->setAnnotationSelection(selected);
    }
    catch(const std::exception& exception)
    {
        qWarning().noquote()
            << "[RENDER] Annotation comparison failed:" << exception.what();
        statusBar()->showMessage(tr("Annotation comparison failed"), 5000);
    }
}

void MainWindow::setDropActive(const bool active)
{
    if(centralStack_ == nullptr
       || centralStack_->property("dragActive").toBool() == active)
    {
        return;
    }
    centralStack_->setProperty("dragActive", active);
    centralStack_->style()->unpolish(centralStack_);
    centralStack_->style()->polish(centralStack_);
    centralStack_->update();
}

void MainWindow::showCursorStatus(
    const double physicalX,
    const double physicalY,
    const double physicalZ,
    const double indexX,
    const double indexY,
    const double indexZ)
{
    cursorStatusLabel_->setText(
        tr("Cursor LPS: %1, %2, %3 mm  |  index: %4, %5, %6")
            .arg(physicalX, 0, 'f', 2)
            .arg(physicalY, 0, 'f', 2)
            .arg(physicalZ, 0, 'f', 2)
            .arg(indexX, 0, 'f', 1)
            .arg(indexY, 0, 'f', 1)
            .arg(indexZ, 0, 'f', 1));
    cursorStatusLabel_->setVisible(true);
    statusBar()->clearMessage();
}

} // namespace radmarky::ui
