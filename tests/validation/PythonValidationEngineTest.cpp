#include "validation/PythonValidationEngine.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QUuid>

#include <atomic>
#include <chrono>
#include <iostream>
#include <filesystem>
#include <thread>

namespace
{

bool expect(const bool condition, const char* const message)
{
    if(!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

QString writeScript(
    const QString& directory,
    const QString& name,
    const QByteArray& source)
{
    const QString path = QDir(directory).filePath(name);
    QFile file(path);
    if(!file.open(QIODevice::WriteOnly | QIODevice::Truncate)
       || file.write(source) != source.size())
    {
        return {};
    }
    return path;
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    const auto directoryPath = std::filesystem::temp_directory_path()
        / ("radmarky-python-"
           + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString());
    std::filesystem::create_directories(directoryPath);
    const QString directory = QString::fromStdWString(directoryPath.wstring());

    const QString pass = writeScript(
        directory, QStringLiteral("pass validator.py"),
        "def validate(annotation_path, context):\n"
        "    assert context['dimensions'] == [2, 3, 4]\n"
        "    return None\n");
    const QString reject = writeScript(
        directory, QStringLiteral("reject.py"),
        "def validate(annotation_path, context):\n"
        "    return 'label 1 is disconnected'\n");
    const QString rejectAtSlice = writeScript(
        directory, QStringLiteral("reject_at_slice.py"),
        "def validate(annotation_path, context):\n"
        "    return 'gap in annotation', 3\n");
    const QString invalidSlice = writeScript(
        directory, QStringLiteral("invalid_slice.py"),
        "def validate(annotation_path, context):\n"
        "    return 'gap in annotation', 5\n");
    const QString empty = writeScript(
        directory, QStringLiteral("empty.py"),
        "def validate(annotation_path, context):\n    return ''\n");
    const QString wrong = writeScript(
        directory, QStringLiteral("wrong.py"),
        "def validate(annotation_path, context):\n    return 7\n");
    const QString exception = writeScript(
        directory, QStringLiteral("exception.py"),
        "def validate(annotation_path, context):\n"
        "    raise RuntimeError('specific failure')\n");
    const QString syntax = writeScript(
        directory, QStringLiteral("syntax.py"),
        "def validate(:\n    pass\n");
    const QString timeout = writeScript(
        directory, QStringLiteral("timeout.py"),
        "def validate(annotation_path, context):\n"
        "    while True:\n        pass\n");
    const QString noFunction = writeScript(
        directory, QStringLiteral("missing.py"), "VALUE = 1\n");

    radmarky::validation::PythonValidationEngine engine(
        std::chrono::milliseconds(100));
    bool passed = true;
    const QString pythonExecutable =
        radmarky::validation::PythonValidationEngine::resolvePythonExecutable();
    const QString pythonVersion =
        radmarky::validation::PythonValidationEngine::runtimeVersion();
    if(pythonVersion.isEmpty())
    {
        std::cerr << "Resolved Python executable: "
                  << pythonExecutable.toStdString() << '\n';
    }
    passed &= expect(
        !pythonVersion.isEmpty(), "installed runtime version");
    const auto inspected = engine.inspectScript(pass);
    if(!inspected.passed())
    {
        std::cerr << "Inspection error: " << inspected.message.toStdString() << '\n';
    }
    passed &= expect(inspected.passed(), "inspect valid script");
    passed &= expect(!engine.inspectScript(syntax).passed(), "syntax error rejected");
    passed &= expect(
        !engine.inspectScript(noFunction).passed(), "missing callable rejected");

    QJsonObject context{{QStringLiteral("dimensions"), QJsonArray{2, 3, 4}}};
    const auto accepted = engine.validateScript(
        pass, QStringLiteral("C:/temporary annotation.nii.gz"), context);
    passed &= expect(accepted.passed(), "None passes");
    passed &= expect(
        engine.validateScript(pass, QStringLiteral("C:/again.nii.gz"), context)
            .passed(),
        "independent repeated call");
    const auto rejected = engine.validateScript(
        reject, QStringLiteral("C:/annotation.nii.gz"), context);
    passed &= expect(
        rejected.status == radmarky::validation::PythonValidationStatus::Rejected
            && rejected.message == QStringLiteral("label 1 is disconnected"),
        "error string rejects");
    const auto rejectedAtSlice = engine.validateScript(
        rejectAtSlice, QStringLiteral("C:/annotation.nii.gz"), context);
    passed &= expect(
        rejectedAtSlice.status
                == radmarky::validation::PythonValidationStatus::Rejected
            && rejectedAtSlice.sliceNumber == 3,
        "message and one-based slice reject with navigation metadata");
    passed &= expect(
        engine.validateScript(
                  invalidSlice, QStringLiteral("C:/annotation.nii.gz"), context)
                .status == radmarky::validation::PythonValidationStatus::Error,
        "slice beyond volume depth is a contract error");
    passed &= expect(
        engine.validateScript(empty, QStringLiteral("C:/a.nii.gz"), context).status
            == radmarky::validation::PythonValidationStatus::Error,
        "empty string contract error");
    passed &= expect(
        engine.validateScript(wrong, QStringLiteral("C:/a.nii.gz"), context).status
            == radmarky::validation::PythonValidationStatus::Error,
        "wrong return type contract error");
    const auto raised = engine.validateScript(
        exception, QStringLiteral("C:/a.nii.gz"), context);
    passed &= expect(
        raised.status == radmarky::validation::PythonValidationStatus::Error
            && raised.message.contains(QStringLiteral("specific failure")),
        "runtime exception text");
    passed &= expect(
        engine.validateScript(timeout, QStringLiteral("C:/a.nii.gz"), context).status
            == radmarky::validation::PythonValidationStatus::TimedOut,
        "enforced timeout");
    std::atomic_bool cancellation{false};
    std::thread canceller([&cancellation] {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        cancellation.store(true, std::memory_order_relaxed);
    });
    const auto cancelled = engine.validateScript(
        timeout, QStringLiteral("C:/a.nii.gz"), context, &cancellation);
    canceller.join();
    passed &= expect(
        cancelled.status
            == radmarky::validation::PythonValidationStatus::Cancelled,
        "enforced cancellation");
    radmarky::validation::PythonValidationEngine missingPython(
        std::chrono::milliseconds(100),
        QDir(directory).filePath(QStringLiteral("missing-python.exe")));
    passed &= expect(
        missingPython.inspectScript(pass).status
            == radmarky::validation::PythonValidationStatus::Error,
        "missing interpreter error");
    std::filesystem::remove_all(directoryPath);
    return passed ? 0 : 1;
}
