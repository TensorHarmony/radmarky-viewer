#pragma once

#include "ui/UiTheme.h"

#include <QPlainTextEdit>

class QPaintEvent;
class QResizeEvent;

namespace radmarky::ui
{

class PythonSyntaxHighlighter;

class CodeEditor final : public QPlainTextEdit
{
public:
    explicit CodeEditor(QWidget* parent = nullptr);

    void setTheme(UiTheme theme);
    [[nodiscard]] UiTheme theme() const noexcept;
    [[nodiscard]] int lineNumberAreaWidth() const;
    [[nodiscard]] QWidget* lineNumberArea() const noexcept;
    void lineNumberAreaPaintEvent(QPaintEvent* event);

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void updateLineNumberAreaWidth();
    void updateLineNumberArea(const QRect& rect, int dy);
    void highlightCurrentLine();
    void applyTheme();

    QWidget* lineNumberArea_ = nullptr;
    PythonSyntaxHighlighter* highlighter_ = nullptr;
    UiTheme theme_ = UiTheme::Light;
};

} // namespace radmarky::ui
