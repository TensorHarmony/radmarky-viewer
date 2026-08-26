#include "ui/CodeEditor.h"

#include "ui/PythonSyntaxHighlighter.h"

#include <QAbstractTextDocumentLayout>
#include <QFontDatabase>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextEdit>

namespace radmarky::ui
{
namespace
{

class LineNumberArea final : public QWidget
{
public:
    explicit LineNumberArea(CodeEditor* const editor)
        : QWidget(editor)
        , editor_(editor)
    {
    }

    [[nodiscard]] QSize sizeHint() const override
    {
        return {editor_->lineNumberAreaWidth(), 0};
    }

protected:
    void paintEvent(QPaintEvent* const event) override
    {
        editor_->lineNumberAreaPaintEvent(event);
    }

private:
    CodeEditor* editor_ = nullptr;
};

} // namespace

CodeEditor::CodeEditor(QWidget* const parent)
    : QPlainTextEdit(parent)
{
    setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setTabStopDistance(fontMetrics().horizontalAdvance(QLatin1Char(' ')) * 4);
    setAttribute(Qt::WA_StyledBackground, true);
    highlighter_ = new PythonSyntaxHighlighter(document());
    lineNumberArea_ = new LineNumberArea(this);
    lineNumberArea_->setObjectName(QStringLiteral("codeEditorLineNumberArea"));
    connect(this, &QPlainTextEdit::blockCountChanged, this, [this](int) {
        updateLineNumberAreaWidth();
    });
    connect(
        this, &QPlainTextEdit::updateRequest, this,
        [this](const QRect& rect, const int dy) {
            updateLineNumberArea(rect, dy);
        });
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this] {
        highlightCurrentLine();
    });
    setTheme(inferredApplicationTheme());
}

void CodeEditor::setTheme(const UiTheme theme)
{
    theme_ = theme;
    if(highlighter_ != nullptr)
    {
        highlighter_->setTheme(theme);
    }
    applyTheme();
}

UiTheme CodeEditor::theme() const noexcept
{
    return theme_;
}

QWidget* CodeEditor::lineNumberArea() const noexcept
{
    return lineNumberArea_;
}

int CodeEditor::lineNumberAreaWidth() const
{
    int digits = 1;
    int lines = qMax(1, blockCount());
    while(lines >= 10)
    {
        lines /= 10;
        ++digits;
    }
    return 18 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
}

void CodeEditor::lineNumberAreaPaintEvent(QPaintEvent* const event)
{
    const CodeHighlightColors colors = codeHighlightColors(theme_);
    QPainter painter(lineNumberArea_);
    painter.fillRect(event->rect(), colors.gutterBackground);
    painter.setFont(font());

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    int top = qRound(
        blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());
    const int current = textCursor().blockNumber();

    while(block.isValid() && top <= event->rect().bottom())
    {
        if(block.isVisible() && bottom >= event->rect().top())
        {
            painter.setPen(
                blockNumber == current ? colors.gutterActiveForeground
                                       : colors.gutterForeground);
            painter.drawText(
                0,
                top,
                lineNumberArea_->width() - 8,
                fontMetrics().height(),
                Qt::AlignRight | Qt::AlignVCenter,
                QString::number(blockNumber + 1));
        }
        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++blockNumber;
    }
}

void CodeEditor::resizeEvent(QResizeEvent* const event)
{
    QPlainTextEdit::resizeEvent(event);
    const QRect contents = contentsRect();
    lineNumberArea_->setGeometry(
        QRect(
            contents.left(),
            contents.top(),
            lineNumberAreaWidth(),
            contents.height()));
}

void CodeEditor::updateLineNumberAreaWidth()
{
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void CodeEditor::updateLineNumberArea(const QRect& rect, const int dy)
{
    if(dy != 0)
    {
        lineNumberArea_->scroll(0, dy);
    }
    else
    {
        lineNumberArea_->update(0, rect.y(), lineNumberArea_->width(), rect.height());
    }
    if(rect.contains(viewport()->rect()))
    {
        updateLineNumberAreaWidth();
    }
}

void CodeEditor::highlightCurrentLine()
{
    const CodeHighlightColors colors = codeHighlightColors(theme_);
    QTextEdit::ExtraSelection selection;
    selection.format.setBackground(colors.currentLine);
    selection.format.setProperty(QTextFormat::FullWidthSelection, true);
    selection.cursor = textCursor();
    selection.cursor.clearSelection();
    setExtraSelections({selection});
    if(lineNumberArea_ != nullptr)
    {
        lineNumberArea_->update();
    }
}

void CodeEditor::applyTheme()
{
    const CodeHighlightColors colors = codeHighlightColors(theme_);
    QPalette palette = this->palette();
    palette.setColor(QPalette::Base, colors.background);
    palette.setColor(QPalette::Text, colors.foreground);
    palette.setColor(QPalette::WindowText, colors.foreground);
    palette.setColor(QPalette::Highlight, colors.selection);
    palette.setColor(QPalette::HighlightedText, colors.foreground);
    setPalette(palette);
    QTextCharFormat defaultFormat;
    defaultFormat.setForeground(colors.foreground);
    document()->setDefaultFont(font());
    QTextCursor cursor(document());
    cursor.select(QTextCursor::Document);
    cursor.mergeBlockCharFormat(defaultFormat);
    if(highlighter_ != nullptr)
    {
        highlighter_->rehighlight();
    }
    updateLineNumberAreaWidth();
    highlightCurrentLine();
    if(lineNumberArea_ != nullptr)
    {
        lineNumberArea_->update();
    }
}

} // namespace radmarky::ui
