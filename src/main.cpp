#include "app/ApplicationInfo.h"
#include "ui/MainWindow.h"
#include "ui/UiTheme.h"

#include <QApplication>
#include <QCoreApplication>
#include <QIcon>
#include <QSurfaceFormat>
#include <QString>
#include <QVTKOpenGLNativeWidget.h>

int main(int argc, char* argv[])
{
    // VTK must define the default OpenGL format before QApplication creates a
    // context. This is required by QVTKOpenGLNativeWidget.
    QSurfaceFormat::setDefaultFormat(QVTKOpenGLNativeWidget::defaultFormat());
    // Keep file pickers and other dialogs inside Qt so the application theme
    // applies consistently instead of falling back to a light native window.
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);

    QApplication application(argc, argv);
    Q_INIT_RESOURCE(radmarky_navigation_icons);
    application.setWindowIcon(
        radmarky::ui::svgIcon(QStringLiteral(":/icons/app-icon.svg")));
    const auto applicationName = radmarky::app::applicationName();
    const auto applicationVersion = radmarky::app::applicationVersion();
    const auto copyrightHolder = radmarky::app::copyrightHolder();

    QString organizationName = QString::fromUtf8(
        copyrightHolder.data(), static_cast<qsizetype>(copyrightHolder.size()));
    // A trailing period is not a valid Windows directory-name suffix. Keeping it
    // here makes QStandardPaths report an `Inc.` component while Windows creates
    // `Inc`, which leaves persisted thumbnail paths unreadable by QIcon.
    while(organizationName.endsWith(QChar('.')))
    {
        organizationName.chop(1);
    }
    QCoreApplication::setOrganizationName(organizationName);
    QCoreApplication::setOrganizationDomain(QStringLiteral("tensorharmony.com"));
    QCoreApplication::setApplicationName(
        QString::fromUtf8(
            applicationName.data(), static_cast<qsizetype>(applicationName.size())));
    QCoreApplication::setApplicationVersion(
        QString::fromUtf8(
            applicationVersion.data(),
            static_cast<qsizetype>(applicationVersion.size())));

    radmarky::ui::MainWindow window;
    window.show();

    return application.exec();
}
