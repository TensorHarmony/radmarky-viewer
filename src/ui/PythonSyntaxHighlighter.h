#pragma once

#include "ui/UiTheme.h"

#include <QColor>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>

namespace radmarky::ui
{

struct CodeHighlightColors
{
    QColor background;
    QColor foreground;
    QColor gutterBackground;
    QColor gutterForeground;
    QColor gutterActiveForeground;
    QColor currentLine;
    QColor selection;
    QColor keyword;
    QColor builtin;
    QColor string;
    QColor comment;
    QColor number;
    QColor decorator;
    QColor definition;
    QColor typeName;
};

[[nodiscard]] CodeHighlightColors codeHighlightColors(UiTheme theme);
[[nodiscard]] UiTheme inferredApplicationTheme();

class PythonSyntaxHighlighter final : public QSyntaxHighlighter
{
public:
    explicit PythonSyntaxHighlighter(QTextDocument* parent = nullptr);

    void setTheme(UiTheme theme);
    [[nodiscard]] UiTheme theme() const noexcept;

protected:
    void highlightBlock(const QString& text) override;

private:
    void rebuildFormats();

    UiTheme theme_ = UiTheme::Light;
    QTextCharFormat keywordFormat_;
    QTextCharFormat builtinFormat_;
    QTextCharFormat stringFormat_;
    QTextCharFormat commentFormat_;
    QTextCharFormat numberFormat_;
    QTextCharFormat decoratorFormat_;
    QTextCharFormat definitionFormat_;
    QTextCharFormat typeNameFormat_;
};

} // namespace radmarky::ui
