#include "app/ApplicationInfo.h"
#include "rendering/RenderProfiling.h"
#include "ui/MainWindow.h"
#include "ui/UiTheme.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QMutex>
#include <QMutexLocker>
#include <QSurfaceFormat>
#include <QString>
#include <QVTKOpenGLNativeWidget.h>

#include <cstdint>

namespace
{

QFile profileLog;
QMutex profileLogMutex;
std::uint64_t profileMessageCount = 0;

const char* messageTypeName(const QtMsgType type)
{
    switch(type)
    {
    case QtDebugMsg:
        return "debug";
    case QtInfoMsg:
        return "info";
    case QtWarningMsg:
        return "warning";
    case QtCriticalMsg:
        return "critical";
    case QtFatalMsg:
        return "fatal";
    }
    return "unknown";
}

void writeProfileMessage(
    const QtMsgType type,
    const QMessageLogContext& context,
    const QString& message)
{
    const QMutexLocker lock(&profileLogMutex);
    if(!profileLog.isOpen())
    {
        return;
    }
    QByteArray line = QDateTime::currentDateTimeUtc()
                          .toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss.zzzZ"))
                          .toUtf8();
    line += ' ';
    line += messageTypeName(type);
    if(context.category != nullptr && context.category[0] != '\0')
    {
        line += " [";
        line += context.category;
        line += ']';
    }
    line += ' ';
    line += message.toUtf8();
    line += '\n';
    static_cast<void>(profileLog.write(line));
    ++profileMessageCount;
    if(profileMessageCount == 1 || profileMessageCount % 64 == 0
       || type == QtCriticalMsg || type == QtFatalMsg)
    {
        profileLog.flush();
    }
}

} // namespace

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

    QCommandLineParser commandLine;
    commandLine.setApplicationDescription(
        QStringLiteral("RadMarky medical image viewer"));
    commandLine.addHelpOption();
    commandLine.addVersionOption();
    const QCommandLineOption renderProfileOption(
        QStringLiteral("profile-rendering"),
        QStringLiteral(
            "Write detailed slice-render timings and VTK pipeline counts to <file>."),
        QStringLiteral("file"));
    commandLine.addOption(renderProfileOption);
    commandLine.process(application);

    if(commandLine.isSet(renderProfileOption))
    {
        const QString requestedPath = commandLine.value(renderProfileOption);
        if(requestedPath.isEmpty())
        {
            qCritical().noquote()
                << "--profile-rendering requires an output file path";
            return 2;
        }
        profileLog.setFileName(QFileInfo(requestedPath).absoluteFilePath());
        if(!profileLog.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            qCritical().noquote()
                << QStringLiteral("Unable to open render profile %1: %2")
                       .arg(profileLog.fileName(), profileLog.errorString());
            return 2;
        }
        application.setProperty(
            radmarky::rendering::renderProfilingProperty, true);
        qInstallMessageHandler(writeProfileMessage);
        qInfo().noquote()
            << QStringLiteral("[PROFILE] enabled output=%1")
                   .arg(profileLog.fileName());
    }

    radmarky::ui::MainWindow window;
    window.show();

    const int result = application.exec();
    if(profileLog.isOpen())
    {
        qInstallMessageHandler(nullptr);
        profileLog.flush();
        profileLog.close();
    }
    return result;
}
