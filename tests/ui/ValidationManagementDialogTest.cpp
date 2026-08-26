#include "ui/CodeEditor.h"
#include "ui/PythonSyntaxHighlighter.h"
#include "ui/UiTheme.h"
#include "ui/ValidationManagementDialog.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QDebug>
#include <QSplitter>
#include <QSyntaxHighlighter>
#include <QTableWidget>
#include <QAbstractTextDocumentLayout>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextLayout>
#include <QUuid>

#include <iostream>
#include <filesystem>

namespace
{

bool expect(const bool condition, const char* const message)
{
    if(!condition)
    {
        qCritical().noquote() << "FAILED:" << message;
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

[[nodiscard]] QColor colorAt(QPlainTextEdit& editor, const int position)
{
    if(position < 0)
    {
        return {};
    }
    const QTextBlock block = editor.document()->findBlock(position);
    if(!block.isValid() || block.layout() == nullptr)
    {
        return {};
    }
    if(QAbstractTextDocumentLayout* const documentLayout =
           editor.document()->documentLayout())
    {
        documentLayout->blockBoundingRect(block);
    }
    const int local = position - block.position();
    for(const QTextLayout::FormatRange& range : block.layout()->formats())
    {
        if(local >= range.start && local < range.start + range.length)
        {
            return range.format.foreground().color();
        }
    }
    QTextCursor cursor(editor.document());
    cursor.setPosition(position + 1);
    return cursor.charFormat().foreground().color();
}

[[nodiscard]] bool testPythonHighlighting()
{
    QPlainTextEdit editor;
    editor.resize(480, 240);
    radmarky::ui::PythonSyntaxHighlighter highlighter(editor.document());
    highlighter.setTheme(radmarky::ui::UiTheme::Dark);
    editor.setPlainText(
        QStringLiteral(
            "def validate():\n"
            "    # note\n"
            "    return \"ok\"\n"
            "    @check\n"
            "    value = 12\n"));
    const auto colors =
        radmarky::ui::codeHighlightColors(radmarky::ui::UiTheme::Dark);
    const QString source = editor.toPlainText();
    bool passed = expect(
        highlighter.theme() == radmarky::ui::UiTheme::Dark, "dark editor theme");
    passed &= expect(
        colorAt(editor, source.indexOf(QLatin1String("def"))) == colors.keyword,
        "keyword color");
    passed &= expect(
        colorAt(editor, source.indexOf(QLatin1String("validate")))
            == colors.definition,
        "definition color");
    passed &= expect(
        colorAt(editor, source.indexOf(QLatin1String("# note"))) == colors.comment,
        "comment color");
    passed &= expect(
        colorAt(editor, source.indexOf(QLatin1String("return"))) == colors.keyword,
        "return keyword color");
    passed &= expect(
        colorAt(editor, source.indexOf(QLatin1String("\"ok\""))) == colors.string,
        "string color");
    passed &= expect(
        colorAt(editor, source.indexOf(QLatin1String("@check")))
            == colors.decorator,
        "decorator color");
    passed &= expect(
        colorAt(editor, source.indexOf(QLatin1String("12"))) == colors.number,
        "number color");
    highlighter.setTheme(radmarky::ui::UiTheme::Light);
    const auto light =
        radmarky::ui::codeHighlightColors(radmarky::ui::UiTheme::Light);
    passed &= expect(
        colorAt(editor, source.indexOf(QLatin1String("def"))) == light.keyword,
        "light theme keyword color");
    editor.setPlainText(QStringLiteral("class Sample:\n    pass\n"));
    highlighter.setTheme(radmarky::ui::UiTheme::Dark);
    passed &= expect(
        colorAt(editor, editor.toPlainText().indexOf(QLatin1String("Sample")))
            == colors.typeName,
        "class name color");
    return passed;
}

[[nodiscard]] bool testCodeEditorChrome()
{
    radmarky::ui::CodeEditor editor;
    editor.resize(480, 240);
    editor.setTheme(radmarky::ui::UiTheme::Dark);
    const int singleDigitWidth = editor.lineNumberAreaWidth();
    QString lines;
    for(int index = 0; index < 100; ++index)
    {
        lines += QStringLiteral("value = %1\n").arg(index);
    }
    editor.setPlainText(lines);
    bool passed = expect(
        editor.lineNumberArea() != nullptr
            && editor.lineNumberArea()->objectName()
                == QStringLiteral("codeEditorLineNumberArea"),
        "line number gutter");
    passed &= expect(
        editor.lineNumberAreaWidth() > singleDigitWidth,
        "gutter grows with line count");
    passed &= expect(
        !editor.extraSelections().isEmpty()
            && editor.extraSelections().front().format.background().color()
                == radmarky::ui::codeHighlightColors(radmarky::ui::UiTheme::Dark)
                       .currentLine,
        "current line highlight");
    editor.setTheme(radmarky::ui::UiTheme::Light);
    passed &= expect(
        editor.theme() == radmarky::ui::UiTheme::Light
            && editor.extraSelections().front().format.background().color()
                == radmarky::ui::codeHighlightColors(radmarky::ui::UiTheme::Light)
                       .currentLine,
        "light current line highlight");

    qApp->setStyleSheet(
        radmarky::ui::applicationStyleSheet(radmarky::ui::UiTheme::Dark));
    radmarky::ui::CodeEditor themed;
    themed.resize(480, 240);
    themed.setTheme(radmarky::ui::UiTheme::Dark);
    themed.setPlainText(QStringLiteral("def validate():\n    return None\n"));
    const auto darkColors =
        radmarky::ui::codeHighlightColors(radmarky::ui::UiTheme::Dark);
    passed &= expect(
        colorAt(themed, themed.toPlainText().indexOf(QLatin1String("def")))
            == darkColors.keyword,
        "application stylesheet keeps keyword color");
    passed &= expect(
        colorAt(themed, themed.toPlainText().indexOf(QLatin1String("validate")))
            == darkColors.definition,
        "application stylesheet keeps definition color");
    qApp->setStyleSheet({});
    return passed;
}

} // namespace

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    const auto directoryPath = std::filesystem::temp_directory_path()
        / ("radmarky-dialog-"
           + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString());
    std::filesystem::create_directories(directoryPath);
    const QString directory = QString::fromStdWString(directoryPath.wstring());
    const QString path = QDir(directory).filePath(QStringLiteral("validator.py"));
    QFile file(path);
    if(!file.open(QIODevice::WriteOnly)
       || file.write("def validate(annotation_path, context):\n    return None\n") < 0)
    {
        qCritical().noquote() << "FAILED: write test script" << file.errorString();
        return 1;
    }
    file.close();

    bool validateCalled = false;
    radmarky::ui::ValidationManagementDialog dialog(
        {},
        true,
        [](const QString&) {
            return radmarky::validation::PythonValidationResult{
                radmarky::validation::PythonValidationStatus::Passed, {}};
        },
        [&validateCalled](const auto&) { validateCalled = true; });
    auto* const table = dialog.findChild<QTableWidget*>(
        QStringLiteral("validationScriptsTable"));
    auto* const validate = dialog.findChild<QPushButton*>(
        QStringLiteral("validateNowButton"));
    auto* const code = dialog.findChild<QPlainTextEdit*>(
        QStringLiteral("validationScriptCodeEditor"));
    auto* const splitter = dialog.findChild<QSplitter*>(
        QStringLiteral("validationContentSplitter"));
    bool passed = expect(dialog.isModal(), "dialog modality")
        && expect(table != nullptr, "scripts table")
        && expect(table != nullptr && table->columnCount() == 3,
                  "combined validator column")
        && expect(splitter != nullptr
                      && splitter->orientation() == Qt::Horizontal,
                  "horizontal list and editor layout")
        && expect(validate != nullptr && validate->isEnabled(), "validate enabled")
        && expect(code != nullptr && code->isReadOnly(), "read-only code viewer")
        && expect(
               code != nullptr
                   && !code->findChildren<QSyntaxHighlighter*>().isEmpty(),
               "python syntax highlighter")
        && expect(
               code != nullptr
                   && code->findChild<QWidget*>(
                          QStringLiteral("codeEditorLineNumberArea"))
                       != nullptr,
               "line number area");
    passed &= testPythonHighlighting();
    passed &= testCodeEditorChrome();
    QString error;
    passed &= expect(dialog.addScriptPath(path, &error), "add valid script");
    passed &= expect(dialog.scripts().size() == 1 && table->rowCount() == 1,
                     "script shown");
    passed &= expect(
        table->item(0, 1) != nullptr
            && table->item(0, 1)->text() == QStringLiteral("validator")
            && table->item(0, 1)->data(Qt::UserRole + 1).toString()
                == QDir::toNativeSeparators(path),
        "name and path share validator cell");
    passed &= expect(
        code->toPlainText().contains(QStringLiteral("def validate")),
        "selected script source shown");
    passed &= expect(!dialog.addScriptPath(path, &error), "duplicate rejected");
    passed &= expect(!error.isEmpty(), "duplicate error text");
    if(table->item(0, 0) != nullptr)
    {
        table->item(0, 0)->setCheckState(Qt::Unchecked);
        passed &= expect(!dialog.scripts()[0].enabled, "checkbox deselects script");
    }
    validate->click();
    passed &= expect(validateCalled, "validate callback");
    dialog.setCanValidate(false);
    passed &= expect(!validate->isEnabled(), "validate disabled without annotation");
    std::filesystem::remove_all(directoryPath);
    return passed ? 0 : 1;
}
