#pragma once

#include "app/UserSettings.h"
#include "validation/PythonValidationEngine.h"

#include <QDialog>

#include <functional>
#include <vector>

class QLabel;
class QPushButton;
class QTableWidget;

namespace radmarky::ui
{
class CodeEditor;

class ValidationManagementDialog final : public QDialog
{
public:
    using Inspector = std::function<validation::PythonValidationResult(const QString&)>;
    using ValidateCallback =
        std::function<void(const std::vector<app::ValidationScriptSetting>&)>;

    ValidationManagementDialog(
        std::vector<app::ValidationScriptSetting> scripts,
        bool canValidate,
        Inspector inspector,
        ValidateCallback validate,
        QWidget* parent = nullptr);

    [[nodiscard]] const std::vector<app::ValidationScriptSetting>&
    scripts() const noexcept;
    [[nodiscard]] bool addScriptPath(
        const QString& path,
        QString* errorMessage = nullptr);
    void setCanValidate(bool canValidate);

private:
    void rebuildTable();
    void updateEmptyState();
    void loadSelectedScript();
    void chooseAndAddScript();
    void removeSelectedScript();

    std::vector<app::ValidationScriptSetting> scripts_;
    Inspector inspector_;
    ValidateCallback validate_;
    QTableWidget* table_ = nullptr;
    QLabel* emptyState_ = nullptr;
    CodeEditor* codeEditor_ = nullptr;
    QPushButton* removeButton_ = nullptr;
    QPushButton* validateButton_ = nullptr;
    bool rebuilding_ = false;
};

} // namespace radmarky::ui
