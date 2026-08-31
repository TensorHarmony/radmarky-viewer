#pragma once

#include "app/UserSettings.h"
#include "validation/PythonValidationEngine.h"

#include <QString>

#include <atomic>
#include <functional>
#include <optional>
#include <vector>

namespace radmarky::core
{
class Annotation;
}

namespace radmarky::validation
{

struct AnnotationValidationContext
{
    QString intendedDestinationPath;
    QString anatomicalImageSourcePath;
    QString companionJsonPath;
};

struct ScriptValidationResult
{
    app::ValidationScriptSetting script;
    PythonValidationStatus status = PythonValidationStatus::Error;
    QString message;
    std::optional<int> sliceNumber;

    [[nodiscard]] bool passed() const noexcept
    {
        return status == PythonValidationStatus::Passed;
    }
};

struct AnnotationValidationResult
{
    std::vector<ScriptValidationResult> scripts;
    QString fatalError;
    bool cancelled = false;
    bool saved = false;

    [[nodiscard]] bool accepted() const noexcept;
};

class AnnotationValidationService
{
public:
    using ProgressCallback =
        std::function<void(int current, int total, const QString& scriptName)>;

    explicit AnnotationValidationService(
        std::chrono::milliseconds timeout = std::chrono::minutes(5));

    [[nodiscard]] PythonValidationResult inspectScript(
        const QString& scriptPath,
        const std::atomic_bool* cancellation = nullptr);

    [[nodiscard]] AnnotationValidationResult validateOnly(
        const core::Annotation& annotation,
        const std::vector<app::ValidationScriptSetting>& scripts,
        const AnnotationValidationContext& context,
        const std::atomic_bool* cancellation = nullptr,
        const ProgressCallback& progress = {});

    [[nodiscard]] AnnotationValidationResult validateAndSave(
        const core::Annotation& annotation,
        const QString& destinationPath,
        const std::vector<app::ValidationScriptSetting>& scripts,
        const AnnotationValidationContext& context,
        const std::atomic_bool* cancellation = nullptr,
        const ProgressCallback& progress = {});

private:
    [[nodiscard]] AnnotationValidationResult run(
        const core::Annotation& annotation,
        const QString& destinationPath,
        const std::vector<app::ValidationScriptSetting>& scripts,
        const AnnotationValidationContext& context,
        bool finalizeSave,
        const std::atomic_bool* cancellation,
        const ProgressCallback& progress);

    PythonValidationEngine engine_;
};

} // namespace radmarky::validation
