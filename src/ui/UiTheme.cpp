#include "ui/UiTheme.h"

#include "ui/PythonSyntaxHighlighter.h"

#include <QColor>
#include <QCursor>
#include <QDebug>
#include <QGuiApplication>
#include <QIcon>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QScreen>
#include <QSvgRenderer>

namespace
{
QPixmap tintedPixmap(const QPixmap& source, const QColor& color)
{
    QPixmap tinted = source;
    QPainter painter(&tinted);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(tinted.rect(), color);
    return tinted;
}

qreal cursorDevicePixelRatio()
{
    if(const QScreen* const screen = QGuiApplication::primaryScreen())
    {
        return screen->devicePixelRatio();
    }
    return 1.0;
}

void paintCrosshair(
    QPainter& painter,
    const QSize& pixelSize,
    const QColor& color,
    const qreal devicePixelRatio)
{
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(QPen(color, qMax(1.0, devicePixelRatio), Qt::SolidLine, Qt::FlatCap));
    const int centerX = pixelSize.width() / 2;
    const int centerY = pixelSize.height() / 2;
    const int arm = qRound(7.0 * devicePixelRatio);
    const int gap = qRound(2.0 * devicePixelRatio);
    painter.drawLine(centerX - arm, centerY, centerX - gap, centerY);
    painter.drawLine(centerX + gap, centerY, centerX + arm, centerY);
    painter.drawLine(centerX, centerY - arm, centerX, centerY - gap);
    painter.drawLine(centerX, centerY + gap, centerX, centerY + arm);
}
} // namespace

namespace radmarky::ui
{

QString applicationStyleSheet(const UiTheme theme)
{
    const bool dark = theme == UiTheme::Dark;
    const QString window = dark ? QStringLiteral("#182129") : QStringLiteral("#f0f0f0");
    const QString sidebar = dark ? QStringLiteral("#26343e") : QStringLiteral("#f0f0f0");
    const QString panel = dark ? QStringLiteral("#202c35") : QStringLiteral("#f0f0f0");
    const QString field = dark ? QStringLiteral("#11191f") : QStringLiteral("#ffffff");
    const QString text = dark ? QStringLiteral("#e8f0f4") : QStringLiteral("#1c2b33");
    const QString muted = dark ? QStringLiteral("#a9bac4") : QStringLiteral("#526873");
    const QString border = dark ? QStringLiteral("#526874") : QStringLiteral("#91a5af");
    const QString button = dark ? QStringLiteral("#b9c9d0") : QStringLiteral("#f7fafb");
    const QString hover = dark ? QStringLiteral("#d7e3e8") : QStringLiteral("#ffffff");
    const QString selected = dark ? QStringLiteral("#78b1c9") : QStringLiteral("#c5e1ef");
    const QString viewportBorder = dark ? QStringLiteral("#466372") : QStringLiteral("#597682");
    const QString header = dark ? QStringLiteral("#243b47") : QStringLiteral("#e3e3e3");
    const QString inspector = dark ? QStringLiteral("#202c35") : QStringLiteral("#e3e3e3");
    const QString popup = dark ? QStringLiteral("#202428") : QStringLiteral("#dedede");
    const QString accent = QStringLiteral("#4c91b3");
    const QString controlText = QStringLiteral("#1c2b33");
    const QString recentTable =
        dark ? QStringLiteral("#17191b") : QStringLiteral("#d7d7d7");
    const QString recentTableHeader =
        dark ? QStringLiteral("#24272a") : QStringLiteral("#c6c6c6");

    return QStringLiteral(R"QSS(
/* Do not set `color` on QDialog or QPlainTextEdit. Qt then paints every
   character with that color and ignores QSyntaxHighlighter formats. */
QMainWindow#mainWindow { background: %1; color: %5; }
QMenuBar, QToolBar, QStatusBar { background: %2; color: %5; }
QMenuBar { border-bottom: 1px solid %7; }
QMenu { background: %16; color: %5; border: 1px solid %7; }
QMenu::item { color: %5; padding: 4px 24px 4px 24px; }
QMenu::item:selected { background: %10; color: %5; }
QMenu::item:disabled { color: %6; }
QMenu::item:selected:disabled { background: transparent; color: %6; }
QDialog, QProgressDialog, QMessageBox, QFileDialog, QInputDialog { background: %16; }
QDialog QLabel, QProgressDialog QLabel, QMessageBox QLabel { color: %5; }
QDialog QCheckBox, QDialog QRadioButton, QDialog QGroupBox { color: %5; }
QDialog QPushButton { min-height: 26px; border: 1px solid %7; border-radius: 4px; background: %8; color: %14; padding: 3px 10px; }
QDialog QPushButton:hover { background: %9; border-color: %13; }
QDialog QPushButton:disabled { color: %6; background: %11; border-color: %7; }
QDialog QLineEdit, QDialog QTextEdit, QDialog QTableView, QDialog QTableWidget, QDialog QListView, QDialog QTreeView, QDialog QComboBox, QDialog QSpinBox, QDialog QDoubleSpinBox { background: %4; alternate-background-color: %3; color: %5; border: 1px solid %7; selection-background-color: %10; selection-color: %5; }
QDialog QHeaderView::section { background: %11; color: %5; border: 0; border-right: 1px solid %7; border-bottom: 1px solid %7; padding: 4px; font-weight: 600; }
QDialog QDialogButtonBox { background: transparent; }
QComboBox QAbstractItemView { background: %16; color: %5; border: 1px solid %7; selection-background-color: %10; selection-color: %5; }
QToolTip { background: %16; color: %5; border: 1px solid %7; padding: 3px; }
QDialog#aboutDialog { background: %16; color: %5; border: 1px solid %7; }
QFrame#aboutIdentityCard { background: %3; border: 1px solid %7; border-radius: 8px; }
QLabel#aboutProductName { color: %5; font-size: 21px; font-weight: 700; }
QLabel#aboutVersion, QLabel#aboutReleaseDate { color: %6; font-size: 12px; padding: 0; margin: 0; }
QFrame#aboutDivider { color: %7; background: %7; max-height: 1px; border: 0; margin: 2px 24px; }
QLabel#aboutDescription { color: %5; font-size: 13px; padding: 4px 6px 0 6px; }
QLabel#aboutCreator, QLabel#aboutDisclaimer, QLabel#aboutLicense, QLabel#aboutCopyright { color: %6; font-size: 12px; padding: 0 6px; }
QLabel#aboutCreator a { color: %13; text-decoration: none; }
QLabel#aboutLinks { font-size: 12px; padding-top: 2px; }
QLabel#aboutLinks a { color: %13; text-decoration: none; }
QDialog#aboutDialog QPushButton { min-height: 28px; border: 1px solid %7; border-radius: 4px; background: %8; color: %14; padding: 3px 12px; }
QDialog#aboutDialog QPushButton:hover { background: %9; border-color: %13; }
QDialog#niftiDropChoiceDialog { background: %16; color: %5; border: 1px solid %7; }
QDialog#dicomHeaderDialog { background: %16; color: %5; border: 1px solid %7; }
QLabel#dicomHeaderTitle { color: %5; font-size: 18px; font-weight: 700; }
QLineEdit#dicomMetadataSearch { min-height: 28px; background: %4; border: 1px solid %7; border-radius: 4px; color: %5; selection-background-color: %10; padding: 2px 7px; }
QTableView#dicomMetadataTable { background: %4; alternate-background-color: %3; color: %5; border: 1px solid %7; border-radius: 3px; gridline-color: %7; selection-background-color: %10; selection-color: %5; }
QTableView#dicomMetadataTable QHeaderView::section { background: %11; color: %5; border: 0; border-right: 1px solid %7; border-bottom: 1px solid %7; padding: 5px; font-weight: 600; }
QLabel#dicomMetadataEmptyState { background: %4; color: %6; border: 1px solid %7; border-radius: 3px; font-size: 13px; }
QDialog#dicomHeaderDialog QPushButton { min-height: 28px; border: 1px solid %7; border-radius: 4px; background: %8; color: %14; padding: 3px 12px; }
QDialog#dicomHeaderDialog QPushButton:hover { background: %9; border-color: %13; }
QLabel#niftiDropDialogTitle { color: %5; font-size: 18px; font-weight: 700; }
QLabel#niftiDropDialogPrompt { color: %5; font-size: 13px; }
QLabel#niftiDropDialogDetail { color: %6; font-size: 12px; }
QDialog#niftiDropChoiceDialog QPushButton { min-height: 28px; border: 1px solid %7; border-radius: 4px; background: %8; color: %14; padding: 3px 10px; }
QDialog#niftiDropChoiceDialog QPushButton:hover { background: %9; border-color: %13; }
QPushButton#niftiDropAnnotationButton { background: %13; border-color: %13; color: #ffffff; font-weight: 600; }
QPushButton#niftiDropAnnotationButton:hover { background: %13; border-color: %5; }
QDialog#niftiDropChoiceDialog QPushButton:disabled { color: %6; background: %11; border-color: %7; }
QProgressDialog#imageImportProgress { background: %16; color: %5; }
QProgressDialog#imageImportProgress QLabel { color: %5; font-size: 13px; padding: 4px 2px; }
QProgressDialog#imageImportProgress QProgressBar { min-height: 20px; border: 1px solid %7; border-radius: 4px; background: %4; color: %5; text-align: center; }
QProgressDialog#imageImportProgress QProgressBar::chunk { background: %13; border-radius: 3px; }
QToolBar { border: 0; border-bottom: 1px solid %7; spacing: 3px; padding: 3px; }
QToolBar QToolButton { min-width: 28px; min-height: 28px; border: 1px solid %7; border-top-color: %9; border-left-color: %9; border-bottom-color: %12; border-right-color: %12; border-radius: 3px; background: %8; color: %14; padding: 2px; }
QToolBar QToolButton:hover { background: %9; border-color: %13; }
QToolBar QToolButton:pressed, QToolBar QToolButton:checked { background: %10; border: 1px solid %13; border-top-color: %12; border-left-color: %12; }
QToolBar QToolButton:disabled { background: %11; color: %6; border-color: %7; border-top-color: %7; border-left-color: %7; }
QDockWidget#viewerToolboxDock { color: %5; }
QDockWidget#viewerToolboxDock::title { background: %2; border-bottom: 1px solid %7; padding: 5px; font-weight: 600; }
QDockWidget[viewerPanelDock="true"] { background: %15; color: %5; }
QDockWidget[viewerPanelDock="true"]::title { background: %15; border-bottom: 1px solid %7; padding: 5px; font-weight: 600; }
QWidget#viewerDockContents { background: %15; }
QDockWidget[viewerPanelDock="true"] QGroupBox { background: %15; border: 0; margin: 0; padding: 0; }
QDockWidget[viewerPanelDock="true"] QLabel { color: %5; }
QDockWidget[viewerPanelDock="true"] QPushButton { border: 1px solid %7; border-radius: 4px; background: %8; color: %14; padding: 5px; }
QDockWidget[viewerPanelDock="true"] QPushButton:hover { background: %9; }
QDockWidget[viewerPanelDock="true"] QPushButton:disabled, QDockWidget[viewerPanelDock="true"] QComboBox:disabled, QDockWidget[viewerPanelDock="true"] QSpinBox:disabled, QDockWidget[viewerPanelDock="true"] QDoubleSpinBox:disabled, QDockWidget[viewerPanelDock="true"] QLineEdit:disabled { color: %6; background: %11; }
QGroupBox#contrastGroup QPushButton { padding: 0 4px; }
QGroupBox#contrastGroup QLabel, QGroupBox#contrastGroup QComboBox, QGroupBox#contrastGroup QDoubleSpinBox, QGroupBox#contrastGroup QPushButton, QGroupBox#cursorInspectorGroup QLabel, QGroupBox#cursorInspectorGroup QSlider { min-height: 20px; max-height: 20px; padding-top: 0; padding-bottom: 0; }
QGroupBox#contrastGroup QComboBox, QGroupBox#contrastGroup QDoubleSpinBox { padding-left: 4px; padding-right: 4px; }
QGroupBox#cursorInspectorGroup QLineEdit { min-height: 20px; max-height: 22px; padding: 0 4px; }
QDockWidget[viewerPanelDock="true"] QLineEdit, QDockWidget[viewerPanelDock="true"] QTableWidget, QDockWidget[viewerPanelDock="true"] QComboBox, QDockWidget[viewerPanelDock="true"] QSpinBox, QDockWidget[viewerPanelDock="true"] QDoubleSpinBox { background: %4; border: 1px solid %7; border-radius: 3px; color: %5; selection-background-color: %10; padding: 2px 4px; }
QFrame#activeLabelColorSwatch { background: #ff3030; border: 1px solid %12; }
QDockWidget[viewerPanelDock="true"] QHeaderView::section { background: %11; color: %5; border: 0; border-right: 1px solid %7; border-bottom: 1px solid %7; padding: 3px; font-weight: 600; }
QFrame#cursorIntensityTable, QFrame#cursorStatisticsTable { background: %4; border: 1px solid %7; border-radius: 0; padding: 0; margin: 0; }
QWidget#cursorIntensityHeader { background: %11; border: 0; border-bottom: 1px solid %7; }
QLabel#cursorIntensityHeaderLabel { background: transparent; font-weight: 600; }
QWidget#cursorIntensityRow, QWidget#cursorIntensityFiller { background: %4; }
QWidget#cursorIntensityRow { border-bottom: 1px solid %7; }
QWidget#emptyViewerPage { background: %1; }
QStackedWidget#centralStack[dragActive="true"] { border: 3px solid %13; }
QLabel#recentImagesTitle { color: %5; font-size: 22px; font-weight: 600; padding: 6px; }
QLabel#recentImagesHint { color: %6; font-size: 12px; padding: 0 6px 8px 6px; }
QTreeWidget#recentImagesList { background: %17; alternate-background-color: %17; color: %5; border: 1px solid %7; border-radius: 8px; padding: 0; }
QTreeWidget#recentImagesList QHeaderView::section { background: %18; color: %5; border: 0; border-right: 1px solid %7; border-bottom: 1px solid %7; padding: 5px 8px; font-weight: 600; }
QTreeWidget#recentImagesList::item { border: 0; border-bottom: 1px solid %7; padding: 6px; }
QTreeWidget#recentImagesList::item:hover { background: %9; color: %14; }
QTreeWidget#recentImagesList::item:selected { background: %10; color: %5; }
QWidget#recentImageIdentityCell { background: transparent; }
QLabel#recentImageThumbnail { background: transparent; }
QLabel#recentImageName { background: transparent; color: %5; font-size: 14px; font-weight: 700; padding: 0; }
QLabel#recentImageType { background: transparent; color: %6; font-size: 10px; padding: 0; }
QToolButton#recentImageFolderButton, QToolButton#removeRecentImageButton { border: 1px solid %7; border-radius: 4px; background: %8; color: %14; padding: 3px; }
QToolButton#recentImageFolderButton:hover, QToolButton#removeRecentImageButton:hover { background: %9; border-color: %13; }
QWidget#viewerToolbox { background: %2; color: %5; }
QWidget#toolboxIdentityPage { background: %2; color: %5; }
QScrollArea#toolboxControlsScrollArea { background: transparent; border: 0; }
QScrollArea#toolboxControlsScrollArea > QWidget > QWidget { background: transparent; }
QLabel#toolboxProductName { color: %5; font-size: 21px; font-weight: 700; }
QLabel#toolboxVersion, QLabel#toolboxReleaseDate { color: %6; font-size: 12px; padding: 0; margin: 0; }
QLabel#toolboxCopyright { color: %6; font-size: 11px; }
QWidget#viewerToolbox QGroupBox { background: %15; border: 1px solid %7; border-radius: 5px; margin-top: 12px; padding-top: 9px; font-weight: 600; }
QWidget#viewerToolbox QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; color: %5; }
QWidget#viewerToolbox QLabel { color: %5; }
QWidget#viewerToolbox QToolButton, QWidget#viewerToolbox QPushButton { border: 1px solid %7; border-radius: 4px; background: %8; color: %14; padding: 5px; }
QWidget#viewerToolbox QToolButton:hover, QWidget#viewerToolbox QPushButton:hover { background: %9; }
QWidget#viewerToolbox QToolButton:checked { background: %10; border-color: %13; }
QWidget#viewerToolbox QLineEdit, QWidget#viewerToolbox QTableWidget, QWidget#viewerToolbox QComboBox, QWidget#viewerToolbox QDoubleSpinBox { background: %4; border: 1px solid %7; border-radius: 3px; color: %5; selection-background-color: %10; padding: 2px 4px; }
QPushButton#removeAnnotationButton, QPushButton#annotationVisibilityButton { background: transparent; border: 0; border-radius: 3px; padding: 0; }
QPushButton#removeAnnotationButton:hover, QPushButton#annotationVisibilityButton:hover { background: %10; border: 1px solid %13; }
QFrame#cursorIntensityColumnDivider { background: %7; border: 0; }
QWidget#viewerToolbox QHeaderView::section { background: %11; color: %5; border: 0; border-right: 1px solid %7; border-bottom: 1px solid %7; padding: 3px; font-weight: 600; }
QWidget#viewerToolbox QLabel:disabled, QWidget#viewerToolbox QGroupBox:disabled { color: %6; }
QWidget#orthogonalViewer { background: #91a5af; }
QFrame#viewerFrame { background: #91a5af; border: 0; }
QFrame#slicePanel { border: 1px solid #91a5af; background: #000000; }
QWidget#slicePanelTools { background: #000000; border: 0; }
QWidget#slicePanelTools QToolButton { color: %14; border: 1px solid transparent; border-radius: 2px; background: %8; padding: 1px; }
QToolButton#expandViewButton { font-size: 11px; font-weight: 700; }
QWidget#slicePanelTools QToolButton:hover { background: %9; border-color: %7; }
QWidget#slicePanelTools QToolButton:checked { background: %10; border-color: %13; }
QWidget#slicePanelTools QToolButton:disabled { background: #000000; border-color: transparent; }
QSplitter::handle { background: %7; }
QWidget#orthogonalViewer QSplitter::handle { background: #91a5af; }
QSplitter::handle:hover { background: %13; }
QScrollBar:vertical { background: %3; width: 15px; margin: 15px 0; }
QScrollBar::handle:vertical { background: %7; min-height: 24px; border-radius: 3px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { background: %11; height: 15px; }
)QSS")
        .arg(window)
        .arg(sidebar)
        .arg(panel)
        .arg(field)
        .arg(text)
        .arg(muted)
        .arg(border)
        .arg(button)
        .arg(hover)
        .arg(selected)
        .arg(header)
        .arg(viewportBorder)
        .arg(accent)
        .arg(controlText)
        .arg(inspector)
        .arg(popup)
        .arg(recentTable)
        .arg(recentTableHeader)
        + QStringLiteral(
              "\nQPlainTextEdit#validationScriptCodeEditor {"
              " background: %1; padding: 0; border: 1px solid %2;"
              " selection-background-color: %3;"
              " }\n")
              .arg(codeHighlightColors(theme).background.name(QColor::HexRgb))
              .arg(border)
              .arg(codeHighlightColors(theme).selection.name(QColor::HexRgb));
}

QPixmap svgPixmap(const QString& resourcePath, const QSize& size)
{
    QSvgRenderer renderer(resourcePath);
    if(!renderer.isValid())
    {
        qWarning().noquote() << "[UI] Unable to load SVG icon:" << resourcePath;
        return {};
    }

    QPixmap pixmap(size);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    renderer.render(&painter);
    return pixmap;
}

QIcon svgIcon(const QString& resourcePath)
{
    return QIcon(svgPixmap(resourcePath, QSize(64, 64)));
}

QCursor svgCursor(
    const QString& resourcePath,
    const QSize& logicalSize,
    const QColor& tint)
{
    const qreal devicePixelRatio = cursorDevicePixelRatio();
    const QSize pixelSize(
        qMax(1, qRound(logicalSize.width() * devicePixelRatio)),
        qMax(1, qRound(logicalSize.height() * devicePixelRatio)));
    QPixmap pixmap = svgPixmap(resourcePath, pixelSize);
    if(pixmap.isNull())
    {
        return QCursor(Qt::OpenHandCursor);
    }
    if(tint.isValid())
    {
        pixmap = tintedPixmap(pixmap, tint);
    }

    pixmap.setDevicePixelRatio(devicePixelRatio);
    return QCursor(
        pixmap,
        logicalSize.width() / 2,
        logicalSize.height() / 2);
}

QCursor crosshairCursor(const QColor& tint, const QSize& logicalSize)
{
    const qreal devicePixelRatio = cursorDevicePixelRatio();
    const QSize pixelSize(
        qMax(1, qRound(logicalSize.width() * devicePixelRatio)),
        qMax(1, qRound(logicalSize.height() * devicePixelRatio)));
    QPixmap pixmap(pixelSize);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    paintCrosshair(
        painter,
        pixelSize,
        tint.isValid() ? tint : QColor(QStringLiteral("#274957")),
        devicePixelRatio);
    pixmap.setDevicePixelRatio(devicePixelRatio);
    return QCursor(
        pixmap,
        logicalSize.width() / 2,
        logicalSize.height() / 2);
}

} // namespace radmarky::ui
