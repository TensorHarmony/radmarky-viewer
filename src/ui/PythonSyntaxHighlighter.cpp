#include "ui/PythonSyntaxHighlighter.h"

#include <QApplication>
#include <QFont>
#include <QSet>

namespace radmarky::ui
{
namespace
{

enum BlockState
{
    StateNormal = 0,
    StateTripleSingle = 1,
    StateTripleDouble = 2,
};

[[nodiscard]] bool isIdentifierStart(const QChar c)
{
    return c.isLetter() || c == QLatin1Char('_');
}

[[nodiscard]] bool isIdentifierPart(const QChar c)
{
    return c.isLetterOrNumber() || c == QLatin1Char('_');
}

[[nodiscard]] const QSet<QString>& keywords()
{
    static const QSet<QString> value = {
        QStringLiteral("False"),
        QStringLiteral("None"),
        QStringLiteral("True"),
        QStringLiteral("and"),
        QStringLiteral("as"),
        QStringLiteral("assert"),
        QStringLiteral("async"),
        QStringLiteral("await"),
        QStringLiteral("break"),
        QStringLiteral("case"),
        QStringLiteral("class"),
        QStringLiteral("continue"),
        QStringLiteral("def"),
        QStringLiteral("del"),
        QStringLiteral("elif"),
        QStringLiteral("else"),
        QStringLiteral("except"),
        QStringLiteral("finally"),
        QStringLiteral("for"),
        QStringLiteral("from"),
        QStringLiteral("global"),
        QStringLiteral("if"),
        QStringLiteral("import"),
        QStringLiteral("in"),
        QStringLiteral("is"),
        QStringLiteral("lambda"),
        QStringLiteral("match"),
        QStringLiteral("nonlocal"),
        QStringLiteral("not"),
        QStringLiteral("or"),
        QStringLiteral("pass"),
        QStringLiteral("raise"),
        QStringLiteral("return"),
        QStringLiteral("try"),
        QStringLiteral("while"),
        QStringLiteral("with"),
        QStringLiteral("yield")};
    return value;
}

[[nodiscard]] const QSet<QString>& builtins()
{
    static const QSet<QString> value = {
        QStringLiteral("Exception"),
        QStringLiteral("OSError"),
        QStringLiteral("RuntimeError"),
        QStringLiteral("TypeError"),
        QStringLiteral("ValueError"),
        QStringLiteral("abs"),
        QStringLiteral("all"),
        QStringLiteral("any"),
        QStringLiteral("bool"),
        QStringLiteral("dict"),
        QStringLiteral("enumerate"),
        QStringLiteral("float"),
        QStringLiteral("getattr"),
        QStringLiteral("hasattr"),
        QStringLiteral("int"),
        QStringLiteral("isinstance"),
        QStringLiteral("len"),
        QStringLiteral("list"),
        QStringLiteral("max"),
        QStringLiteral("min"),
        QStringLiteral("open"),
        QStringLiteral("print"),
        QStringLiteral("range"),
        QStringLiteral("set"),
        QStringLiteral("sorted"),
        QStringLiteral("str"),
        QStringLiteral("sum"),
        QStringLiteral("tuple"),
        QStringLiteral("type"),
        QStringLiteral("zip")};
    return value;
}

[[nodiscard]] int stringPrefixLength(const QString& text, const int start)
{
    if(start >= text.size() || !isIdentifierStart(text.at(start)))
    {
        return 0;
    }
    int end = start;
    while(end < text.size() && isIdentifierPart(text.at(end)))
    {
        ++end;
    }
    const QString prefix = text.mid(start, end - start).toLower();
    static const QSet<QString> prefixes = {
        QStringLiteral("b"),
        QStringLiteral("br"),
        QStringLiteral("f"),
        QStringLiteral("fr"),
        QStringLiteral("r"),
        QStringLiteral("rb"),
        QStringLiteral("rf"),
        QStringLiteral("ru"),
        QStringLiteral("u"),
        QStringLiteral("ur")};
    if(!prefixes.contains(prefix) || end >= text.size())
    {
        return 0;
    }
    const QChar quote = text.at(end);
    if(quote != QLatin1Char('\'') && quote != QLatin1Char('"'))
    {
        return 0;
    }
    return end - start;
}

[[nodiscard]] QString stringDelimiter(const QString& text, const int quoteIndex)
{
    const QChar quote = text.at(quoteIndex);
    if(quoteIndex + 2 < text.size()
       && text.at(quoteIndex + 1) == quote
       && text.at(quoteIndex + 2) == quote)
    {
        return QString(3, quote);
    }
    return QString(1, quote);
}

[[nodiscard]] int closeString(
    const QString& text,
    const int contentStart,
    const QString& delimiter,
    const bool raw)
{
    int index = contentStart;
    while(index < text.size())
    {
        if(!raw
           && text.at(index) == QLatin1Char('\\')
           && index + 1 < text.size())
        {
            index += 2;
            continue;
        }
        if(text.mid(index, delimiter.size()) == delimiter)
        {
            return index + delimiter.size();
        }
        ++index;
    }
    return -1;
}

[[nodiscard]] int consumeNumber(const QString& text, const int start)
{
    int index = start;
    if(index + 1 < text.size() && text.at(index) == QLatin1Char('0'))
    {
        const QChar base = text.at(index + 1).toLower();
        if(base == QLatin1Char('x') || base == QLatin1Char('o')
           || base == QLatin1Char('b'))
        {
            index += 2;
            while(index < text.size()
                  && (text.at(index).isLetterOrNumber()
                      || text.at(index) == QLatin1Char('_')))
            {
                ++index;
            }
            return index;
        }
    }

    while(index < text.size()
          && (text.at(index).isDigit() || text.at(index) == QLatin1Char('_')))
    {
        ++index;
    }
    if(index < text.size() && text.at(index) == QLatin1Char('.'))
    {
        ++index;
        while(index < text.size()
              && (text.at(index).isDigit() || text.at(index) == QLatin1Char('_')))
        {
            ++index;
        }
    }
    if(index < text.size())
    {
        const QChar exponent = text.at(index).toLower();
        if(exponent == QLatin1Char('e'))
        {
            int exponentIndex = index + 1;
            if(exponentIndex < text.size()
               && (text.at(exponentIndex) == QLatin1Char('+')
                   || text.at(exponentIndex) == QLatin1Char('-')))
            {
                ++exponentIndex;
            }
            if(exponentIndex < text.size() && text.at(exponentIndex).isDigit())
            {
                index = exponentIndex;
                while(index < text.size()
                      && (text.at(index).isDigit()
                          || text.at(index) == QLatin1Char('_')))
                {
                    ++index;
                }
            }
        }
    }
    return index;
}

[[nodiscard]] QTextCharFormat makeFormat(
    const QColor& color,
    const bool bold = false,
    const bool italic = false)
{
    QTextCharFormat format;
    format.setForeground(color);
    if(bold)
    {
        format.setFontWeight(QFont::DemiBold);
    }
    if(italic)
    {
        format.setFontItalic(true);
    }
    return format;
}

} // namespace

CodeHighlightColors codeHighlightColors(const UiTheme theme)
{
    if(theme == UiTheme::Dark)
    {
        return {
            .background = QColor(QStringLiteral("#1e1e1e")),
            .foreground = QColor(QStringLiteral("#d4d4d4")),
            .gutterBackground = QColor(QStringLiteral("#1e1e1e")),
            .gutterForeground = QColor(QStringLiteral("#858585")),
            .gutterActiveForeground = QColor(QStringLiteral("#c6c6c6")),
            .currentLine = QColor(QStringLiteral("#3c3c3c")),
            .selection = QColor(QStringLiteral("#264f78")),
            .keyword = QColor(QStringLiteral("#569cd6")),
            .builtin = QColor(QStringLiteral("#4ec9b0")),
            .string = QColor(QStringLiteral("#ce9178")),
            .comment = QColor(QStringLiteral("#6a9955")),
            .number = QColor(QStringLiteral("#b5cea8")),
            .decorator = QColor(QStringLiteral("#dcdcaa")),
            .definition = QColor(QStringLiteral("#dcdcaa")),
            .typeName = QColor(QStringLiteral("#4ec9b0"))};
    }
    return {
        .background = QColor(QStringLiteral("#ffffff")),
        .foreground = QColor(QStringLiteral("#000000")),
        .gutterBackground = QColor(QStringLiteral("#ffffff")),
        .gutterForeground = QColor(QStringLiteral("#237893")),
        .gutterActiveForeground = QColor(QStringLiteral("#0b216f")),
        .currentLine = QColor(QStringLiteral("#e8e8e8")),
        .selection = QColor(QStringLiteral("#add6ff")),
        .keyword = QColor(QStringLiteral("#0000ff")),
        .builtin = QColor(QStringLiteral("#267f99")),
        .string = QColor(QStringLiteral("#a31515")),
        .comment = QColor(QStringLiteral("#008000")),
        .number = QColor(QStringLiteral("#098658")),
        .decorator = QColor(QStringLiteral("#795e26")),
        .definition = QColor(QStringLiteral("#795e26")),
        .typeName = QColor(QStringLiteral("#267f99"))};
}

UiTheme inferredApplicationTheme()
{
    if(qApp == nullptr)
    {
        return UiTheme::Light;
    }
    return qApp->styleSheet().contains(QLatin1String("#182129"))
        ? UiTheme::Dark
        : UiTheme::Light;
}

PythonSyntaxHighlighter::PythonSyntaxHighlighter(QTextDocument* const parent)
    : QSyntaxHighlighter(parent)
{
    setTheme(inferredApplicationTheme());
}

void PythonSyntaxHighlighter::setTheme(const UiTheme theme)
{
    const bool uninitialized = !keywordFormat_.foreground().color().isValid();
    if(!uninitialized && theme_ == theme)
    {
        return;
    }
    theme_ = theme;
    rebuildFormats();
    rehighlight();
}

UiTheme PythonSyntaxHighlighter::theme() const noexcept
{
    return theme_;
}

void PythonSyntaxHighlighter::rebuildFormats()
{
    const CodeHighlightColors colors = codeHighlightColors(theme_);
    keywordFormat_ = makeFormat(colors.keyword, true);
    builtinFormat_ = makeFormat(colors.builtin);
    stringFormat_ = makeFormat(colors.string);
    commentFormat_ = makeFormat(colors.comment, false, true);
    numberFormat_ = makeFormat(colors.number);
    decoratorFormat_ = makeFormat(colors.decorator);
    definitionFormat_ = makeFormat(colors.definition);
    typeNameFormat_ = makeFormat(colors.typeName);
}

void PythonSyntaxHighlighter::highlightBlock(const QString& text)
{
    int index = 0;
    int state = previousBlockState();
    if(state < 0)
    {
        state = StateNormal;
    }

    if(state == StateTripleSingle || state == StateTripleDouble)
    {
        const QString delimiter =
            state == StateTripleSingle ? QStringLiteral("'''")
                                       : QStringLiteral("\"\"\"");
        const int closed = closeString(text, 0, delimiter, false);
        if(closed < 0)
        {
            setFormat(0, text.size(), stringFormat_);
            setCurrentBlockState(state);
            return;
        }
        setFormat(0, closed, stringFormat_);
        index = closed;
    }

    bool pendingFunction = false;
    bool pendingClass = false;
    while(index < text.size())
    {
        const QChar current = text.at(index);
        if(current.isSpace())
        {
            ++index;
            continue;
        }
        if(current == QLatin1Char('#'))
        {
            setFormat(index, text.size() - index, commentFormat_);
            break;
        }
        if(current == QLatin1Char('@'))
        {
            int end = index + 1;
            while(end < text.size()
                  && (isIdentifierPart(text.at(end))
                      || text.at(end) == QLatin1Char('.')))
            {
                ++end;
            }
            setFormat(index, end - index, decoratorFormat_);
            pendingFunction = false;
            pendingClass = false;
            index = end;
            continue;
        }

        const int prefix = stringPrefixLength(text, index);
        const int quoteIndex = index + prefix;
        if(quoteIndex < text.size()
           && (text.at(quoteIndex) == QLatin1Char('\'')
               || text.at(quoteIndex) == QLatin1Char('"')))
        {
            const QString delimiter = stringDelimiter(text, quoteIndex);
            const bool raw = text.mid(index, prefix).contains(
                QLatin1Char('r'), Qt::CaseInsensitive);
            const int closed = closeString(
                text, quoteIndex + delimiter.size(), delimiter, raw);
            if(closed < 0)
            {
                setFormat(index, text.size() - index, stringFormat_);
                setCurrentBlockState(
                    delimiter.size() == 3
                        ? (delimiter.at(0) == QLatin1Char('\'')
                               ? StateTripleSingle
                               : StateTripleDouble)
                        : StateNormal);
                return;
            }
            setFormat(index, closed - index, stringFormat_);
            pendingFunction = false;
            pendingClass = false;
            index = closed;
            continue;
        }

        if(current.isDigit()
           || (current == QLatin1Char('.')
               && index + 1 < text.size()
               && text.at(index + 1).isDigit()))
        {
            const int end = consumeNumber(text, index);
            setFormat(index, end - index, numberFormat_);
            pendingFunction = false;
            pendingClass = false;
            index = end;
            continue;
        }

        if(isIdentifierStart(current))
        {
            int end = index + 1;
            while(end < text.size() && isIdentifierPart(text.at(end)))
            {
                ++end;
            }
            const QString word = text.mid(index, end - index);
            if(pendingFunction)
            {
                setFormat(index, end - index, definitionFormat_);
                pendingFunction = false;
            }
            else if(pendingClass)
            {
                setFormat(index, end - index, typeNameFormat_);
                pendingClass = false;
            }
            else if(keywords().contains(word))
            {
                setFormat(index, end - index, keywordFormat_);
                pendingFunction = word == QLatin1String("def");
                pendingClass = word == QLatin1String("class");
            }
            else if(builtins().contains(word))
            {
                setFormat(index, end - index, builtinFormat_);
            }
            index = end;
            continue;
        }

        pendingFunction = false;
        pendingClass = false;
        ++index;
    }

    setCurrentBlockState(StateNormal);
}

} // namespace radmarky::ui
