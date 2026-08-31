#pragma once

#include <QJsonObject>
#include <QString>

#include <atomic>
#include <chrono>
#include <optional>

namespace radmarky::validation
{

enum class PythonValidationStatus
{
    Passed,
    Rejected,
    Error,
    Cancelled,
    TimedOut,
};

struct PythonValidationResult
{
    PythonValidationStatus status = PythonValidationStatus::Error;
    QString message;
    // One-based index along the NIfTI volume's third dimension. Validators may
    // provide this for rejected results so the viewer can navigate to the
    // affected axial slice.
    std::optional<int> sliceNumber;

    [[nodiscard]] bool passed() const noexcept
    {
        return status == PythonValidationStatus::Passed;
    }
};

// Runs trusted validators in a separate, installed CPython process. Keeping the
// interpreter out of process avoids bundling a Python runtime and isolates the
// viewer from validator crashes and global state.
class PythonValidationEngine
{
public:
    explicit PythonValidationEngine(
        std::chrono::milliseconds timeout = std::chrono::minutes(5),
        QString pythonExecutable = {});

    [[nodiscard]] PythonValidationResult inspectScript(
        const QString& scriptPath,
        const std::atomic_bool* cancellation = nullptr);
    [[nodiscard]] PythonValidationResult validateScript(
        const QString& scriptPath,
        const QString& annotationPath,
        const QJsonObject& context,
        const std::atomic_bool* cancellation = nullptr);
    [[nodiscard]] static QString resolvePythonExecutable();
    [[nodiscard]] static QString runtimeVersion(
        const QString& pythonExecutable = {});

private:
    [[nodiscard]] PythonValidationResult execute(
        const QString& scriptPath,
        const QString& annotationPath,
        const QJsonObject& context,
        bool inspectOnly,
        const std::atomic_bool* cancellation);

    std::chrono::milliseconds timeout_;
    QString pythonExecutable_;
};

} // namespace radmarky::validation
