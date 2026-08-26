#include "validation/PythonValidationEngine.h"

#include <QByteArray>
#include <QDeadlineTimer>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QUuid>

#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <utility>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace radmarky::validation
{
namespace
{

constexpr int startupTimeoutMilliseconds = 10000;

std::filesystem::path fileSystemPath(const QString& path)
{
#ifdef Q_OS_WIN
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::path(path.toStdString());
#endif
}

QString qtPath(const std::filesystem::path& path)
{
#ifdef Q_OS_WIN
    return QString::fromStdWString(path.wstring());
#else
    return QString::fromStdString(path.string());
#endif
}

class ProtocolDirectory
{
public:
    ProtocolDirectory()
    {
        path_ = std::filesystem::temp_directory_path()
            / ("radmarky-python-protocol-"
               + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString());
        std::error_code error;
        valid_ = std::filesystem::create_directory(path_, error);
    }

    ~ProtocolDirectory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    [[nodiscard]] bool isValid() const noexcept
    {
        return valid_;
    }

    [[nodiscard]] QString filePath(const QString& name) const
    {
        return qtPath(path_ / fileSystemPath(name));
    }

    ProtocolDirectory(const ProtocolDirectory&) = delete;
    ProtocolDirectory& operator=(const ProtocolDirectory&) = delete;

private:
    std::filesystem::path path_;
    bool valid_ = false;
};

// Requests and results use private temporary files rather than stdout. A
// validator is free to print arbitrary output without corrupting the protocol.
constexpr auto runnerSource = R"PY(
import importlib.util
import json
import pathlib
import sys
import traceback

request_path = pathlib.Path(sys.argv[1])
ready_path = pathlib.Path(sys.argv[2])
result_path = pathlib.Path(sys.argv[3])

def emit(status, message="", slice_number=None):
    result_path.write_text(
        json.dumps(
            {
                "status": status,
                "message": message,
                "slice_number": slice_number,
            },
            ensure_ascii=False,
            separators=(",", ":"),
        ),
        encoding="utf-8",
    )

ready_path.touch()

try:
    request = json.loads(request_path.read_text(encoding="utf-8"))
    if sys.version_info < (3, 11):
        emit("error", "RadMarky validation requires Python 3.11 or newer")
        raise SystemExit(0)

    script_path = pathlib.Path(request["script_path"]).resolve()
    sys.path.insert(0, str(script_path.parent))
    spec = importlib.util.spec_from_file_location(
        "_radmarky_validator", str(script_path)
    )
    if spec is None or spec.loader is None:
        raise RuntimeError("Unable to load the validation script")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    function = getattr(module, "validate", None)
    if not callable(function):
        emit(
            "error",
            "Script must define callable validate(annotation_path, context)",
        )
    elif request["inspect_only"]:
        emit("passed")
    else:
        returned = function(request["annotation_path"], request["context"])
        if returned is None:
            emit("passed")
        elif isinstance(returned, str) or (
            isinstance(returned, (tuple, list)) and len(returned) == 2
        ):
            if isinstance(returned, str):
                message, slice_number = returned.strip(), None
            else:
                message, slice_number = returned
                if not isinstance(message, str):
                    emit("error", "validate() issue message must be a string")
                    raise SystemExit(0)
                message = message.strip()
                if (
                    not isinstance(slice_number, int)
                    or isinstance(slice_number, bool)
                    or slice_number < 1
                ):
                    emit(
                        "error",
                        "validate() issue slice must be a positive one-based integer",
                    )
                    raise SystemExit(0)
                dimensions = request.get("context", {}).get("dimensions", [])
                if len(dimensions) >= 3 and slice_number > dimensions[2]:
                    emit(
                        "error",
                        f"validate() issue slice {slice_number} exceeds the "
                        f"volume depth of {dimensions[2]}",
                    )
                    raise SystemExit(0)
            if message:
                emit("rejected", message, slice_number)
            else:
                emit(
                    "error",
                    "validate() returned an empty string; return None to pass",
                )
        else:
            emit(
                "error",
                "validate() must return None, a non-empty string, or "
                "(message, slice_number)",
            )
except SystemExit:
    raise
except BaseException:
    emit("error", traceback.format_exc().strip())
)PY";

QString usableExecutable(const QString& candidate)
{
    if(candidate.trimmed().isEmpty())
    {
        return {};
    }
    const QFileInfo direct(candidate);
    if(direct.isFile() && direct.isExecutable())
    {
        return direct.absoluteFilePath();
    }
    return QStandardPaths::findExecutable(candidate);
}

QStringList interpreterArguments(const QString& executable)
{
#ifdef Q_OS_WIN
    if(QFileInfo(executable).completeBaseName().compare(
           QStringLiteral("py"), Qt::CaseInsensitive) == 0)
    {
        return {QStringLiteral("-3")};
    }
#else
    static_cast<void>(executable);
#endif
    return {};
}

class ChildProcess
{
public:
    ~ChildProcess()
    {
        if(running())
        {
            terminate();
        }
#ifdef Q_OS_WIN
        if(handle_ != nullptr)
        {
            CloseHandle(handle_);
        }
#endif
    }

    bool start(
        const QString& executable,
        const QStringList& arguments,
        QString& errorMessage)
    {
#ifdef Q_OS_WIN
        const auto quote = [](const QString& argument) {
            QString quoted = QStringLiteral("\"");
            int backslashes = 0;
            for(const QChar character : argument)
            {
                if(character == QChar('\\'))
                {
                    ++backslashes;
                    continue;
                }
                if(character == QChar('\"'))
                {
                    quoted += QString(backslashes * 2 + 1, QChar('\\'));
                    quoted += character;
                    backslashes = 0;
                    continue;
                }
                quoted += QString(backslashes, QChar('\\'));
                backslashes = 0;
                quoted += character;
            }
            quoted += QString(backslashes * 2, QChar('\\'));
            quoted += QChar('\"');
            return quoted;
        };
        QStringList commandParts{quote(executable)};
        for(const auto& argument : arguments)
        {
            commandParts.push_back(quote(argument));
        }
        std::wstring commandLine = commandParts.join(QChar(' ')).toStdWString();
        commandLine.push_back(L'\0');
        const std::wstring applicationName = executable.toStdWString();
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION information{};
        const BOOL created = CreateProcessW(
            applicationName.c_str(), commandLine.data(), nullptr, nullptr, FALSE,
            CREATE_NO_WINDOW, nullptr, nullptr, &startup, &information);
        if(!created)
        {
            errorMessage = QStringLiteral("Windows error %1").arg(GetLastError());
            return false;
        }
        CloseHandle(information.hThread);
        handle_ = information.hProcess;
        return true;
#else
        process_.setProcessChannelMode(QProcess::ForwardedChannels);
        process_.start(executable, arguments, QIODevice::NotOpen);
        if(!process_.waitForStarted(5000))
        {
            errorMessage = process_.errorString();
            return false;
        }
        return true;
#endif
    }

    [[nodiscard]] bool running() const
    {
#ifdef Q_OS_WIN
        if(handle_ == nullptr)
        {
            return false;
        }
        DWORD code = 0;
        return GetExitCodeProcess(handle_, &code) && code == STILL_ACTIVE;
#else
        return process_.state() != QProcess::NotRunning;
#endif
    }

    void wait(const int milliseconds)
    {
#ifdef Q_OS_WIN
        if(handle_ != nullptr)
        {
            static_cast<void>(WaitForSingleObject(
                handle_, static_cast<DWORD>(std::max(0, milliseconds))));
        }
#else
        static_cast<void>(process_.waitForFinished(milliseconds));
#endif
    }

    void terminate()
    {
#ifdef Q_OS_WIN
        if(handle_ != nullptr && running())
        {
            static_cast<void>(TerminateProcess(handle_, 1));
            static_cast<void>(WaitForSingleObject(handle_, 2000));
        }
#else
        process_.kill();
        static_cast<void>(process_.waitForFinished(2000));
#endif
    }

    [[nodiscard]] int exitCode() const
    {
#ifdef Q_OS_WIN
        DWORD code = 0;
        return handle_ != nullptr && GetExitCodeProcess(handle_, &code)
            ? static_cast<int>(code) : -1;
#else
        return process_.exitCode();
#endif
    }

private:
#ifdef Q_OS_WIN
    HANDLE handle_ = nullptr;
#else
    QProcess process_;
#endif
};

bool writeJson(
    const QString& path,
    const QJsonObject& object,
    QString& errorMessage)
{
    QFile file(path);
    const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    if(!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        errorMessage = file.errorString();
        return false;
    }
    if(file.write(bytes) != bytes.size() || !file.flush())
    {
        errorMessage = file.errorString();
        return false;
    }
    return true;
}

PythonValidationResult parseResult(const QString& path)
{
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly))
    {
        return {
            PythonValidationStatus::Error,
            QStringLiteral("Python exited without returning a validation result")};
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if(error.error != QJsonParseError::NoError || !document.isObject())
    {
        return {
            PythonValidationStatus::Error,
            QStringLiteral("Python returned an invalid validation result: %1")
                .arg(error.errorString())};
    }
    const QJsonObject object = document.object();
    const QString status = object.value(QStringLiteral("status")).toString();
    const QString message = object.value(QStringLiteral("message")).toString();
    std::optional<int> sliceNumber;
    const QJsonValue sliceValue = object.value(QStringLiteral("slice_number"));
    if(sliceValue.isDouble())
    {
        const double numericSlice = sliceValue.toDouble();
        const int integerSlice = sliceValue.toInt();
        if(numericSlice != static_cast<double>(integerSlice) || integerSlice < 1)
        {
            return {
                PythonValidationStatus::Error,
                QStringLiteral("Python returned an invalid issue slice number")};
        }
        sliceNumber = integerSlice;
    }
    if(status == QStringLiteral("passed"))
    {
        return {PythonValidationStatus::Passed, {}};
    }
    if(status == QStringLiteral("rejected"))
    {
        return {PythonValidationStatus::Rejected, message, sliceNumber};
    }
    if(status == QStringLiteral("error"))
    {
        return {PythonValidationStatus::Error, message};
    }
    return {
        PythonValidationStatus::Error,
        QStringLiteral("Python returned an unknown validation status")};
}

QString processExitMessage(const ChildProcess& process)
{
    return QStringLiteral("Python stopped without returning a validation result "
                          "(exit code %1)")
        .arg(process.exitCode());
}

} // namespace

PythonValidationEngine::PythonValidationEngine(
    const std::chrono::milliseconds timeout,
    QString pythonExecutable)
    : timeout_(timeout),
      pythonExecutable_(pythonExecutable.trimmed().isEmpty()
                            ? resolvePythonExecutable()
                            : usableExecutable(pythonExecutable))
{
    if(timeout_ <= std::chrono::milliseconds::zero())
    {
        throw std::invalid_argument("Python validation timeout must be positive");
    }
}

PythonValidationResult PythonValidationEngine::inspectScript(
    const QString& scriptPath,
    const std::atomic_bool* const cancellation)
{
    return execute(scriptPath, {}, {}, true, cancellation);
}

PythonValidationResult PythonValidationEngine::validateScript(
    const QString& scriptPath,
    const QString& annotationPath,
    const QJsonObject& context,
    const std::atomic_bool* const cancellation)
{
    return execute(scriptPath, annotationPath, context, false, cancellation);
}

QString PythonValidationEngine::resolvePythonExecutable()
{
    const QString environment = qEnvironmentVariable("RADMARKY_PYTHON_EXECUTABLE");
    if(const QString executable = usableExecutable(environment); !executable.isEmpty())
    {
        return executable;
    }
#ifdef RADMARKY_DEFAULT_PYTHON_EXECUTABLE
    if(const QString executable = usableExecutable(
           QString::fromUtf8(RADMARKY_DEFAULT_PYTHON_EXECUTABLE));
       !executable.isEmpty())
    {
        return executable;
    }
#endif
    const QStringList candidates = {
        QStringLiteral("python3"), QStringLiteral("python")
#ifdef Q_OS_WIN
        , QStringLiteral("py")
#endif
    };
    for(const auto& candidate : candidates)
    {
        if(const QString executable = usableExecutable(candidate); !executable.isEmpty())
        {
            return executable;
        }
    }
    return {};
}

QString PythonValidationEngine::runtimeVersion(const QString& pythonExecutable)
{
    const QString executable = pythonExecutable.trimmed().isEmpty()
        ? resolvePythonExecutable() : usableExecutable(pythonExecutable);
    if(executable.isEmpty())
    {
        return {};
    }
    ProtocolDirectory directory;
    if(!directory.isValid())
    {
        return {};
    }
    const QString runnerPath = directory.filePath(QStringLiteral("version.py"));
    const QString resultPath = directory.filePath(QStringLiteral("version.txt"));
    QFile runner(runnerPath);
    constexpr auto versionSource =
        "import pathlib,sys\n"
        "pathlib.Path(sys.argv[1]).write_text("
        "'.'.join(map(str,sys.version_info[:3])),encoding='utf-8')\n";
    const QByteArray versionBytes(versionSource);
    if(!runner.open(QIODevice::WriteOnly | QIODevice::Truncate)
       || runner.write(versionBytes) != versionBytes.size())
    {
        return {};
    }
    runner.close();
    ChildProcess process;
    QStringList arguments = interpreterArguments(executable);
    arguments.append({runnerPath, resultPath});
    QString startError;
    if(!process.start(executable, arguments, startError))
    {
        return {};
    }
    QDeadlineTimer deadline(5000);
    while(process.running() && !deadline.hasExpired())
    {
        process.wait(static_cast<int>(std::min<qint64>(10, deadline.remainingTime())));
    }
    if(process.running())
    {
        process.terminate();
        return {};
    }
    QFile result(resultPath);
    return result.open(QIODevice::ReadOnly)
        ? QString::fromUtf8(result.readAll()).trimmed() : QString{};
}

PythonValidationResult PythonValidationEngine::execute(
    const QString& scriptPath,
    const QString& annotationPath,
    const QJsonObject& context,
    const bool inspectOnly,
    const std::atomic_bool* const cancellation)
{
    const QFileInfo scriptInfo(scriptPath);
    const bool resourceScript = scriptPath.startsWith(QStringLiteral(":/"));
    if((resourceScript && !QFile::exists(scriptPath))
       || (!resourceScript && !scriptInfo.isFile()))
    {
        return {PythonValidationStatus::Error,
                QStringLiteral("Python script does not exist: %1").arg(scriptPath)};
    }
    if(scriptInfo.suffix().compare(QStringLiteral("py"), Qt::CaseInsensitive) != 0)
    {
        return {PythonValidationStatus::Error,
                QStringLiteral("Validation scripts must use the .py extension")};
    }
    if(!inspectOnly && annotationPath.isEmpty())
    {
        return {PythonValidationStatus::Error,
                QStringLiteral("No annotation snapshot was supplied")};
    }
    if(cancellation != nullptr
       && cancellation->load(std::memory_order_relaxed))
    {
        return {PythonValidationStatus::Cancelled,
                QStringLiteral("Validation was cancelled")};
    }
    if(pythonExecutable_.isEmpty())
    {
        return {
            PythonValidationStatus::Error,
            QStringLiteral(
                "Python 3.11 or newer was not found. Install Python and add it "
                "to PATH, or set RADMARKY_PYTHON_EXECUTABLE.")};
    }

    ProtocolDirectory protocolDirectory;
    if(!protocolDirectory.isValid())
    {
        return {PythonValidationStatus::Error,
                QStringLiteral("Unable to create Python validation protocol storage")};
    }
    const QString requestPath =
        protocolDirectory.filePath(QStringLiteral("request.json"));
    const QString readyPath =
        protocolDirectory.filePath(QStringLiteral("ready"));
    const QString resultPath =
        protocolDirectory.filePath(QStringLiteral("result.json"));
    const QString runnerPath =
        protocolDirectory.filePath(QStringLiteral("runner.py"));
    QString executableScriptPath = scriptInfo.absoluteFilePath();
    if(resourceScript)
    {
        QFile source(scriptPath);
        executableScriptPath = protocolDirectory.filePath(scriptInfo.fileName());
        QFile destination(executableScriptPath);
        if(!source.open(QIODevice::ReadOnly)
           || !destination.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            return {
                PythonValidationStatus::Error,
                QStringLiteral("Unable to prepare the built-in validation script")};
        }
        const QByteArray sourceBytes = source.readAll();
        if(destination.write(sourceBytes) != sourceBytes.size()
           || !destination.flush())
        {
            return {
                PythonValidationStatus::Error,
                QStringLiteral("Unable to prepare the built-in validation script")};
        }
    }
    QJsonObject request;
    request.insert(QStringLiteral("script_path"), executableScriptPath);
    request.insert(QStringLiteral("annotation_path"), annotationPath);
    request.insert(QStringLiteral("context"), context);
    request.insert(QStringLiteral("inspect_only"), inspectOnly);
    QString requestError;
    if(!writeJson(requestPath, request, requestError))
    {
        return {PythonValidationStatus::Error,
                QStringLiteral("Unable to prepare the Python validation request: %1")
                    .arg(requestError)};
    }
    QFile runner(runnerPath);
    const QByteArray runnerBytes(runnerSource);
    if(!runner.open(QIODevice::WriteOnly | QIODevice::Truncate)
       || runner.write(runnerBytes) != runnerBytes.size() || !runner.flush())
    {
        return {
            PythonValidationStatus::Error,
            QStringLiteral("Unable to prepare the Python validation runner: %1")
                .arg(runner.errorString())};
    }
    runner.close();

    ChildProcess process;
    QStringList arguments = interpreterArguments(pythonExecutable_);
    arguments.append({
        QStringLiteral("-u"), runnerPath, requestPath, readyPath, resultPath});
    QString startError;
    if(!process.start(pythonExecutable_, arguments, startError))
    {
        return {
            PythonValidationStatus::Error,
            QStringLiteral("Unable to start Python interpreter %1: %2")
                .arg(QFileInfo(pythonExecutable_).fileName(), startError)};
    }

    QDeadlineTimer startupDeadline(startupTimeoutMilliseconds);
    while(!QFileInfo::exists(readyPath))
    {
        if(cancellation != nullptr
           && cancellation->load(std::memory_order_relaxed))
        {
            process.terminate();
            return {PythonValidationStatus::Cancelled,
                    QStringLiteral("Validation was cancelled")};
        }
        if(!process.running())
        {
            return {PythonValidationStatus::Error, processExitMessage(process)};
        }
        if(startupDeadline.hasExpired())
        {
            process.terminate();
            return {
                PythonValidationStatus::Error,
                QStringLiteral("Python did not start the validation runner")};
        }
        process.wait(
            static_cast<int>(std::min<qint64>(10, startupDeadline.remainingTime())));
    }

    QElapsedTimer executionTimer;
    executionTimer.start();
    while(process.running())
    {
        process.wait(10);
        if(cancellation != nullptr
           && cancellation->load(std::memory_order_relaxed))
        {
            process.terminate();
            return {PythonValidationStatus::Cancelled,
                    QStringLiteral("Validation was cancelled")};
        }
        if(executionTimer.elapsed() >= timeout_.count())
        {
            process.terminate();
            return {PythonValidationStatus::TimedOut,
                    QStringLiteral("Validation exceeded its time limit")};
        }
    }
    PythonValidationResult result = parseResult(resultPath);
    if(result.status == PythonValidationStatus::Error && result.message.startsWith(
           QStringLiteral("Python exited without")))
    {
        return {PythonValidationStatus::Error, processExitMessage(process)};
    }
    return result;
}

} // namespace radmarky::validation
