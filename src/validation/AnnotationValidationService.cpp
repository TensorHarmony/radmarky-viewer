#include "validation/AnnotationValidationService.h"

#include "core/Annotation.h"
#include "core/ImageGeometry.h"
#include "io/NiftiWriter.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QSaveFile>
#include <QUuid>

#include <algorithm>
#include <filesystem>
#include <stdexcept>

namespace radmarky::validation
{
namespace
{

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
    return QString::fromStdString(path.string());
#endif
}

class TemporaryDirectory
{
public:
    TemporaryDirectory()
    {
        path_ = std::filesystem::temp_directory_path()
            / ("radmarky-validation-"
               + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString());
        std::error_code error;
        if(!std::filesystem::create_directory(path_, error))
        {
            throw std::runtime_error(
                "Unable to create temporary validation storage: "
                + error.message());
        }
    }

    ~TemporaryDirectory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    [[nodiscard]] QString filePath(const QString& name) const
    {
        return qtPath(path_ / fileSystemPath(name));
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

private:
    std::filesystem::path path_;
};

QJsonArray jsonVector(const core::ImageGeometry::Vector& values)
{
    QJsonArray result;
    for(const double value : values)
    {
        result.push_back(value);
    }
    return result;
}

QJsonObject validationContext(
    const core::Annotation& annotation,
    const AnnotationValidationContext& supplied)
{
    const auto& geometry = annotation.volume().geometry();
    QJsonArray dimensions;
    for(const std::size_t value : geometry.dimensions())
    {
        dimensions.push_back(static_cast<qint64>(value));
    }
    QJsonArray direction;
    for(const auto& row : geometry.direction())
    {
        direction.push_back(jsonVector(row));
    }
    const auto optionalPath = [](const QString& path) -> QJsonValue {
        return path.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(path);
    };
    QJsonObject result;
    result.insert(
        QStringLiteral("intended_destination_path"),
        optionalPath(supplied.intendedDestinationPath));
    result.insert(
        QStringLiteral("anatomical_image_source_path"),
        optionalPath(supplied.anatomicalImageSourcePath));
    result.insert(
        QStringLiteral("companion_json_path"),
        optionalPath(supplied.companionJsonPath));
    result.insert(
        QStringLiteral("active_annotation_name"),
        QString::fromUtf8(annotation.name()));
    result.insert(QStringLiteral("dimensions"), dimensions);
    result.insert(QStringLiteral("spacing"), jsonVector(geometry.spacing()));
    result.insert(QStringLiteral("origin"), jsonVector(geometry.origin()));
    result.insert(QStringLiteral("direction"), direction);
    return result;
}

QString candidateSuffix(const QString& destinationPath)
{
    return destinationPath.endsWith(QStringLiteral(".nii"), Qt::CaseInsensitive)
        ? QStringLiteral(".nii")
        : QStringLiteral(".nii.gz");
}

bool copyAtomically(
    const QString& sourcePath,
    const QString& destinationPath,
    QString& error)
{
    QFile source(sourcePath);
    if(!source.open(QIODevice::ReadOnly))
    {
        error = QStringLiteral("Unable to read validated save candidate: %1")
                    .arg(source.errorString());
        return false;
    }
    QSaveFile destination(destinationPath);
    destination.setDirectWriteFallback(false);
    if(!destination.open(QIODevice::WriteOnly))
    {
        error = QStringLiteral("Unable to create annotation output: %1")
                    .arg(destination.errorString());
        return false;
    }
    QByteArray buffer;
    buffer.resize(1024 * 1024);
    while(true)
    {
        const qint64 count = source.read(buffer.data(), buffer.size());
        if(count < 0)
        {
            destination.cancelWriting();
            error = QStringLiteral("Unable to read validated save candidate: %1")
                        .arg(source.errorString());
            return false;
        }
        if(count == 0)
        {
            break;
        }
        if(destination.write(buffer.constData(), count) != count)
        {
            destination.cancelWriting();
            error = QStringLiteral("Unable to write annotation output: %1")
                        .arg(destination.errorString());
            return false;
        }
    }
    if(!destination.commit())
    {
        error = QStringLiteral("Unable to finalize annotation output: %1")
                    .arg(destination.errorString());
        return false;
    }
    return true;
}

} // namespace

bool AnnotationValidationResult::accepted() const noexcept
{
    return !cancelled && fatalError.isEmpty()
        && std::all_of(
            scripts.begin(), scripts.end(),
            [](const ScriptValidationResult& result) { return result.passed(); });
}

AnnotationValidationService::AnnotationValidationService(
    const std::chrono::milliseconds timeout)
    : engine_(timeout)
{
}

PythonValidationResult AnnotationValidationService::inspectScript(
    const QString& scriptPath,
    const std::atomic_bool* const cancellation)
{
    return engine_.inspectScript(scriptPath, cancellation);
}

AnnotationValidationResult AnnotationValidationService::validateOnly(
    const core::Annotation& annotation,
    const std::vector<app::ValidationScriptSetting>& scripts,
    const AnnotationValidationContext& context,
    const std::atomic_bool* const cancellation,
    const ProgressCallback& progress)
{
    return run(
        annotation, {}, scripts, context, false, cancellation, progress);
}

AnnotationValidationResult AnnotationValidationService::validateAndSave(
    const core::Annotation& annotation,
    const QString& destinationPath,
    const std::vector<app::ValidationScriptSetting>& scripts,
    const AnnotationValidationContext& context,
    const std::atomic_bool* const cancellation,
    const ProgressCallback& progress)
{
    return run(
        annotation, destinationPath, scripts, context, true, cancellation,
        progress);
}

AnnotationValidationResult AnnotationValidationService::run(
    const core::Annotation& annotation,
    const QString& destinationPath,
    const std::vector<app::ValidationScriptSetting>& scripts,
    const AnnotationValidationContext& suppliedContext,
    const bool finalizeSave,
    const std::atomic_bool* const cancellation,
    const ProgressCallback& progress)
{
    AnnotationValidationResult result;
    if(annotation.kind() != core::AnnotationKind::LabelMap)
    {
        result.fatalError =
            QStringLiteral("Only label-map annotations can be validated");
        return result;
    }
    if(finalizeSave && destinationPath.isEmpty())
    {
        result.fatalError = QStringLiteral("No save destination was supplied");
        return result;
    }

    std::unique_ptr<TemporaryDirectory> directory;
    try
    {
        directory = std::make_unique<TemporaryDirectory>();
    }
    catch(const std::exception& exception)
    {
        result.fatalError = QString::fromUtf8(exception.what());
        return result;
    }
    const QString candidatePath = directory->filePath(
        QStringLiteral("private-candidate") + candidateSuffix(destinationPath));
    try
    {
        io::NiftiWriter::writeLabelMap(
            annotation, fileSystemPath(candidatePath));
    }
    catch(const std::exception& exception)
    {
        result.fatalError = QString::fromUtf8(exception.what());
        return result;
    }

    std::vector<app::ValidationScriptSetting> enabled;
    std::copy_if(
        scripts.begin(), scripts.end(), std::back_inserter(enabled),
        [](const app::ValidationScriptSetting& script) { return script.enabled; });
    QJsonObject pythonContext = validationContext(annotation, suppliedContext);
    const int total = static_cast<int>(enabled.size());
    for(int index = 0; index < total; ++index)
    {
        if(cancellation != nullptr
           && cancellation->load(std::memory_order_relaxed))
        {
            result.cancelled = true;
            break;
        }
        const auto& script = enabled[static_cast<std::size_t>(index)];
        if(progress)
        {
            progress(index + 1, total, script.name);
        }
        const QString validationCopy = directory->filePath(
            QStringLiteral("validator-%1").arg(index + 1)
            + candidateSuffix(destinationPath));
        if(!QFile::copy(candidatePath, validationCopy))
        {
            result.scripts.push_back({
                script,
                PythonValidationStatus::Error,
                QStringLiteral("Unable to create a disposable annotation snapshot")});
            continue;
        }
        static_cast<void>(QFile::setPermissions(
            validationCopy,
            QFileDevice::ReadOwner | QFileDevice::ReadUser
                | QFileDevice::ReadGroup | QFileDevice::ReadOther));
        const auto python = engine_.validateScript(
            script.path, validationCopy, pythonContext, cancellation);
        static_cast<void>(QFile::setPermissions(
            validationCopy, QFileDevice::ReadOwner | QFileDevice::WriteOwner));
        static_cast<void>(QFile::remove(validationCopy));
        result.scripts.push_back(
            {script, python.status, python.message, python.sliceNumber});
        if(python.status == PythonValidationStatus::Cancelled)
        {
            result.cancelled = true;
            break;
        }
    }

    if(finalizeSave && result.accepted())
    {
        if(!copyAtomically(candidatePath, destinationPath, result.fatalError))
        {
            return result;
        }
        result.saved = true;
    }
    return result;
}

} // namespace radmarky::validation
