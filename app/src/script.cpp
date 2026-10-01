/*
FielDes: field-driven design
Derived from Studio, a simple GUI for the libfive CAD kernel
Copyright (C) 2017  Matt Keeter

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/
#include <algorithm>

#include <QAbstractItemView>
#include <QCompleter>
#include <QInputDialog>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSettings>
#include <QStringListModel>
#include <QTextBlock>
#include <QToolTip>

#include "fieldes/script.hpp"
#include "fieldes/formatter.hpp"
#include "fieldes/color.hpp"
#include "fieldes/theme.hpp"

namespace FielDes {

namespace {
bool isIdentChar(QChar c) { return c.isLetterOrNumber() || c == '_'; }

// Is column `col` of `line` inside a string literal or a comment?
// (Single-line approximation: good enough for completion / bracket work.)
bool inStringOrComment(const QString& line, int col)
{
    QChar quote = 0;
    for (int i=0; i < col && i < line.size(); ++i)
    {
        const QChar c = line[i];
        if (quote != QChar(0))
        {
            if (c == '\\') { ++i; continue; }
            if (c == quote) quote = 0;
        }
        else if (c == '"' || c == '\'') quote = c;
        else if (c == '#') return true;
    }
    return quote != QChar(0);
}

// Net bracket depth change of a line (outside strings and comments)
int bracketDelta(const QString& line)
{
    int depth = 0;
    QChar quote = 0;
    for (int i=0; i < line.size(); ++i)
    {
        const QChar c = line[i];
        if (quote != QChar(0))
        {
            if (c == '\\') { ++i; continue; }
            if (c == quote) quote = 0;
            continue;
        }
        if (c == '"' || c == '\'') quote = c;
        else if (c == '#') break;
        else if (c == '(' || c == '[' || c == '{') depth++;
        else if (c == ')' || c == ']' || c == '}') depth--;
    }
    return depth;
}

const QRegularExpression& sectionMarker()
{
    static const QRegularExpression r(R"(^\s*#\s*(%%|region\b))");
    return r;
}
const QRegularExpression& regionEnd()
{
    static const QRegularExpression r(R"(^\s*#\s*endregion\b)");
    return r;
}
}   // anonymous namespace

Script::Script(QWidget* parent)
    : QPlainTextEdit(parent)
{
    m_gutter = new ScriptGutter(this);
    connect(this, &QPlainTextEdit::blockCountChanged,
            this, [this](int){ updateGutterWidth(); });
    connect(this, &QPlainTextEdit::updateRequest,
            this, &Script::updateGutter);
    connect(this, &QPlainTextEdit::cursorPositionChanged,
            this, &Script::onCursorMoved);
    connect(document(), &QTextDocument::contentsChange,
            this, &Script::onContentsChange);
    updateGutterWidth();

    m_completionModel = new QStringListModel(this);
    m_completer = new QCompleter(m_completionModel, this);
    m_completer->setWidget(this);
    m_completer->setCompletionMode(QCompleter::PopupCompletion);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_completer->setModelSorting(QCompleter::CaseInsensitivelySortedModel);
    m_completer->setWrapAround(false);
    m_completer->setMaxVisibleItems(12);
    connect(m_completer, QOverload<const QString&>::of(&QCompleter::activated),
            this, &Script::insertCompletion);

    m_wordScanTimer.setSingleShot(true);
    m_wordScanTimer.setInterval(400);
    connect(document(), &QTextDocument::contentsChanged,
            &m_wordScanTimer, QOverload<>::of(&QTimer::start));
    connect(&m_wordScanTimer, &QTimer::timeout, this, &Script::rescanScriptWords);

    m_flashTimer.setSingleShot(true);
    m_flashTimer.setInterval(900);
    connect(&m_flashTimer, &QTimer::timeout,
            this, [this]{ setSelections(SEL_FLASH, {}); });

    viewport()->setMouseTracking(true);
    onCursorMoved();
}

void Script::bind(Formatter* f) {
    m_formatter = f;
}

////////////////////////////////////////////////////////////////////////////////
// Extra-selection layers

void Script::setSelections(SelectionLayer layer,
                           const QList<QTextEdit::ExtraSelection>& sels)
{
    m_layers[layer] = sels;
    refreshSelections();
}

void Script::refreshSelections()
{
    QList<QTextEdit::ExtraSelection> all;
    for (int i=0; i < SEL_COUNT; ++i)
    {
        all.append(m_layers[i]);
    }
    setExtraSelections(all);
}

void Script::setErrorLine(int line)
{
    m_errorLine = line;
    m_gutter->update();
}

////////////////////////////////////////////////////////////////////////////////
// Breakpoints

QList<int> Script::breakpoints() const
{
    QList<int> lines;
    for (const auto& c : m_breakpoints)
    {
        const int line = c.blockNumber() + 1;
        if (!lines.contains(line))
        {
            lines << line;
        }
    }
    std::sort(lines.begin(), lines.end());
    return lines;
}

bool Script::hasBreakpoint(int block) const
{
    for (const auto& c : m_breakpoints)
    {
        if (c.blockNumber() == block)
        {
            return true;
        }
    }
    return false;
}

void Script::toggleBreakpoint(int block)
{
    if (block < 0 || block >= blockCount())
    {
        return;
    }
    if (hasBreakpoint(block))
    {
        for (int i = m_breakpoints.size() - 1; i >= 0; --i)
        {
            if (m_breakpoints[i].blockNumber() == block)
            {
                m_breakpoints.removeAt(i);
            }
        }
    }
    else
    {
        QTextCursor c(document()->findBlockByNumber(block));
        c.setKeepPositionOnInsert(true);
        m_breakpoints << c;
    }
    m_gutter->update();
    emit breakpointsChanged();
}

void Script::clearBreakpoints()
{
    if (!m_breakpoints.isEmpty())
    {
        m_breakpoints.clear();
        m_gutter->update();
        emit breakpointsChanged();
    }
}

void Script::setPausedLine(int line)
{
    m_pausedLine = line;
    QList<QTextEdit::ExtraSelection> sels;
    if (line >= 0)
    {
        const QTextBlock b = document()->findBlockByNumber(line);
        if (b.isValid())
        {
            QTextEdit::ExtraSelection s;
            s.cursor = QTextCursor(b);
            s.format.setBackground(QColor(Color::yellow.red(), Color::yellow.green(),
                                          Color::yellow.blue(), 70));
            s.format.setProperty(QTextFormat::FullWidthSelection, true);
            sels << s;
        }
    }
    setSelections(SEL_PAUSE, sels);
    if (line >= 0)
    {
        goToLine(line, false);
    }
    m_gutter->update();
}

////////////////////////////////////////////////////////////////////////////////
// Gutter

// The gutter's own type: a size smaller than the script's
static QFont gutterFont(const QFont& scriptFont)
{
    QFont f = scriptFont;
    f.setPointSizeF(std::max(7.0, scriptFont.pointSizeF() * 0.8));
    return f;
}

// The column at the gutter's left edge where breakpoints are set
static int breakpointColumn(const QFontMetrics& fm)
{
    return fm.height() / 2 + 6;
}

int Script::gutterWidth() const
{
    const QFontMetrics fm(gutterFont(document()->defaultFont()));
    const int digits = std::max(2, int(QString::number(std::max(1, blockCount())).length()));
    return breakpointColumn(fm) + 4 + fm.horizontalAdvance(QLatin1Char('9')) * digits + fm.height() * 2 / 3;
}

void Script::updateGutterWidth()
{
    setViewportMargins(gutterWidth(), 0, 0, 0);
}

void Script::updateGutter(const QRect& rect, int dy)
{
    if (dy)
    {
        m_gutter->scroll(0, dy);
    }
    else
    {
        m_gutter->update(0, rect.y(), m_gutter->width(), rect.height());
    }
    if (rect.contains(viewport()->rect()))
    {
        updateGutterWidth();
    }
}

void Script::resizeEvent(QResizeEvent* e)
{
    QPlainTextEdit::resizeEvent(e);
    const QRect cr = contentsRect();
    m_gutter->setGeometry(QRect(cr.left(), cr.top(), gutterWidth(), cr.height()));
}

void Script::paintGutter(QPaintEvent* event)
{
    QPainter p(m_gutter);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(event->rect(), Theme::gutter);
    p.setPen(Theme::border);
    p.drawLine(m_gutter->width() - 1, event->rect().top(), m_gutter->width() - 1, event->rect().bottom());

    const QFont scriptFont = document()->defaultFont();
    const QFont font = gutterFont(scriptFont);
    p.setFont(font);
    const QFontMetrics fm(font);
    const QFontMetrics sfm(scriptFont);
    const int markerSize = fm.height() * 2 / 3;
    const int numberRight = m_gutter->width() - markerSize - 4;
    const int current = textCursor().blockNumber();

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());

    while (block.isValid() && top <= event->rect().bottom())
    {
        if (block.isVisible() && bottom >= event->rect().top())
        {
            {   // the left column: a breakpoint (a red disc), an error (a dot),
                // the line a run is stopped before (an arrow)
                const int col = breakpointColumn(fm);
                const QPoint c(col / 2, top + sfm.height() / 2);
                const int r = std::max(3, fm.height() * 3 / 10);
                p.setPen(Qt::NoPen);
                if (hasBreakpoint(blockNumber))
                {
                    p.setBrush(Color::red);
                    p.drawEllipse(c, r, r);
                }
                if (blockNumber == m_errorLine)
                {
                    p.setBrush(hasBreakpoint(blockNumber) ? Color::base3 : Color::red);
                    p.drawEllipse(c, std::max(2, r / 2), std::max(2, r / 2));
                }
                if (blockNumber == m_pausedLine)
                {
                    QPainterPath arrow;
                    arrow.moveTo(c.x() - r, c.y() - r);
                    arrow.lineTo(c.x() + r, c.y());
                    arrow.lineTo(c.x() - r, c.y() + r);
                    arrow.closeSubpath();
                    p.setPen(QPen(Color::base03, 1));
                    p.setBrush(Color::yellow);
                    p.drawPath(arrow);
                }
            }
            p.setPen(blockNumber == current ? Color::base00 : Color::base1);
            p.drawText(0, top, numberRight, sfm.height(), Qt::AlignRight | Qt::AlignVCenter,
                       QString::number(blockNumber + 1));

            int end;
            if (foldRange(blockNumber, &end))
            {
                const bool folded = isFolded(blockNumber);
                const double cx = numberRight + 4 + markerSize / 2.0;
                const double cy = top + sfm.height() / 2.0;
                const double s = markerSize * 0.22;
                QPainterPath tri;
                if (folded)
                {   // pointing right
                    tri.moveTo(cx - s * 0.6, cy - s);
                    tri.lineTo(cx + s * 0.9, cy);
                    tri.lineTo(cx - s * 0.6, cy + s);
                }
                else
                {   // pointing down
                    tri.moveTo(cx - s, cy - s * 0.6);
                    tri.lineTo(cx + s, cy - s * 0.6);
                    tri.lineTo(cx, cy + s * 0.9);
                }
                tri.closeSubpath();
                p.setPen(Qt::NoPen);
                p.setBrush(folded ? Color::blue : Color::base1);
                p.drawPath(tri);
            }
        }
        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++blockNumber;
    }
}

void Script::gutterPressed(QMouseEvent* event)
{
    QTextBlock block = firstVisibleBlock();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    while (block.isValid())
    {
        const int h = qRound(blockBoundingRect(block).height());
        if (block.isVisible() && event->pos().y() >= top && event->pos().y() < top + h)
        {
            const QFontMetrics fm(gutterFont(document()->defaultFont()));
            const int markerSize = fm.height() * 2 / 3;
            int end;
            if (event->pos().x() < breakpointColumn(fm))
            {
                toggleBreakpoint(block.blockNumber());
            }
            else if (event->pos().x() >= m_gutter->width() - markerSize - 6 &&
                foldRange(block.blockNumber(), &end))
            {
                setFolded(block.blockNumber(), !isFolded(block.blockNumber()));
            }
            else
            {   // Click on a line number selects the line
                QTextCursor c(block);
                c.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
                setTextCursor(c);
            }
            return;
        }
        top += h;
        block = block.next();
    }
}

////////////////////////////////////////////////////////////////////////////////
// Folding

int Script::indentOf(const QTextBlock& b) const
{
    const QString t = b.text();
    int col = 0;
    for (QChar c : t)
    {
        if (c == ' ') col++;
        else if (c == '\t') col += 4;
        else break;
    }
    return col;
}

bool Script::isBlankOrComment(const QTextBlock& b) const
{
    const QString t = b.text().trimmed();
    return t.isEmpty() || t.startsWith('#');
}

bool Script::foldRange(int blockNum, int* endOut) const
{
    QTextBlock b = document()->findBlockByNumber(blockNum);
    if (!b.isValid())
    {
        return false;
    }
    const QString text = b.text();
    const QString t = text.trimmed();
    if (t.isEmpty())
    {
        return false;
    }

    int end = -1;
    if (sectionMarker().match(text).hasMatch())
    {
        // "# %%" / "# region" sections run to the next marker (or the
        // matching "# endregion", inclusive) or the end of the file
        const bool region = text.contains("region");
        QTextBlock n = b.next();
        while (n.isValid())
        {
            if (region && regionEnd().match(n.text()).hasMatch())
            {
                end = n.blockNumber();
                break;
            }
            if (!region && sectionMarker().match(n.text()).hasMatch())
            {
                break;
            }
            if (!n.text().trimmed().isEmpty())
            {
                end = n.blockNumber();
            }
            n = n.next();
        }
    }
    else if (t.startsWith('#'))
    {
        // A run of three or more comment lines folds from its first line
        QTextBlock prev = b.previous();
        if (prev.isValid() && prev.text().trimmed().startsWith('#') &&
            !sectionMarker().match(prev.text()).hasMatch())
        {
            return false;
        }
        int count = 1;
        QTextBlock n = b.next();
        while (n.isValid() && n.text().trimmed().startsWith('#') &&
               !sectionMarker().match(n.text()).hasMatch())
        {
            end = n.blockNumber();
            count++;
            n = n.next();
        }
        if (count < 3)
        {
            end = -1;
        }
    }
    else
    {
        // Indented block: the following non-blank lines indented deeper
        const int ind = indentOf(b);
        QTextBlock n = b.next();
        while (n.isValid())
        {
            if (!n.text().trimmed().isEmpty())
            {
                if (indentOf(n) <= ind)
                {
                    break;
                }
                end = n.blockNumber();
            }
            n = n.next();
        }
        // Otherwise, a statement whose brackets span several lines
        if (end < 0)
        {
            int depth = bracketDelta(text);
            QTextBlock m = b.next();
            while (depth > 0 && m.isValid())
            {
                depth += bracketDelta(m.text());
                end = m.blockNumber();
                m = m.next();
            }
            if (depth > 0)
            {
                end = -1;
            }
        }
    }

    if (end > blockNum)
    {
        if (endOut) *endOut = end;
        return true;
    }
    return false;
}

bool Script::isFolded(int blockNum) const
{
    QTextBlock b = document()->findBlockByNumber(blockNum);
    return b.isValid() && b.isVisible() && b.next().isValid() && !b.next().isVisible();
}

void Script::setFolded(int blockNum, bool folded)
{
    int end;
    if (!foldRange(blockNum, &end))
    {
        return;
    }
    QTextBlock header = document()->findBlockByNumber(blockNum);
    QTextBlock last = document()->findBlockByNumber(end);

    // Don't leave the text cursor inside a hidden region
    if (folded)
    {
        const int pos = textCursor().position();
        if (pos > header.position() + header.length() - 1 &&
            pos <= last.position() + last.length() - 1)
        {
            QTextCursor c(header);
            c.movePosition(QTextCursor::EndOfBlock);
            setTextCursor(c);
        }
    }

    for (QTextBlock b = header.next(); b.isValid() && b.blockNumber() <= end; b = b.next())
    {
        b.setVisible(!folded);
    }
    document()->markContentsDirty(header.position(),
                                  last.position() + last.length() - header.position());
    viewport()->update();
    m_gutter->update();
    ensureCursorVisible();
}

void Script::toggleFoldAtCursor()
{
    // Fold the innermost region containing the cursor
    int n = textCursor().blockNumber();
    for (int b = n; b >= 0; --b)
    {
        int end;
        if (foldRange(b, &end) && end >= n)
        {
            setFolded(b, !isFolded(b));
            return;
        }
    }
}

void Script::foldAll()
{
    // Fold top-level regions only (outermost first, so nested headers
    // inside them simply become hidden)
    for (QTextBlock b = document()->begin(); b.isValid(); )
    {
        int end;
        if (b.isVisible() && foldRange(b.blockNumber(), &end))
        {
            setFolded(b.blockNumber(), true);
            b = document()->findBlockByNumber(end).next();
        }
        else
        {
            b = b.next();
        }
    }
}

void Script::unfoldAll()
{
    bool changed = false;
    for (QTextBlock b = document()->begin(); b.isValid(); b = b.next())
    {
        if (!b.isVisible())
        {
            b.setVisible(true);
            changed = true;
        }
    }
    if (changed)
    {
        document()->markContentsDirty(0, document()->characterCount());
        viewport()->update();
        m_gutter->update();
    }
}

void Script::validateFolds()
{
    // Any hidden run must be exactly the body of the visible fold header
    // right before it; anything else (e.g. a programmatic edit that
    // inserted lines into a folded region) is unhidden.
    bool changed = false;
    QTextBlock b = document()->begin();
    while (b.isValid())
    {
        if (b.isVisible())
        {
            b = b.next();
            continue;
        }
        QTextBlock first = b;
        QTextBlock last = b;
        while (b.isValid() && !b.isVisible())
        {
            last = b;
            b = b.next();
        }
        int end;
        QTextBlock header = first.previous();
        if (!header.isValid() || !foldRange(header.blockNumber(), &end) ||
            end != last.blockNumber())
        {
            for (QTextBlock u = first; u.isValid() && u.blockNumber() <= last.blockNumber();
                 u = u.next())
            {
                u.setVisible(true);
            }
            changed = true;
        }
    }
    if (changed)
    {
        document()->markContentsDirty(0, document()->characterCount());
        viewport()->update();
        m_gutter->update();
    }
}

void Script::ensureBlockVisible(const QTextBlock& target)
{
    for (int guard=0; guard < 64 && target.isValid() && !target.isVisible(); ++guard)
    {
        QTextBlock h = target;
        while (h.isValid() && !h.isVisible())
        {
            h = h.previous();
        }
        if (!h.isValid())
        {
            break;
        }
        setFolded(h.blockNumber(), false);
    }
}

void Script::onContentsChange(int, int, int)
{
    m_linkCache.clear();
    // Deferred: the layout must settle before re-checking fold shapes
    QTimer::singleShot(0, this, &Script::validateFolds);
}

////////////////////////////////////////////////////////////////////////////////
// Painting: folded-region ellipses and extra cursors

void Script::paintEvent(QPaintEvent* e)
{
    QPlainTextEdit::paintEvent(e);

    QPainter p(viewport());
    const QFontMetrics fm(document()->defaultFont());
    QTextBlock block = firstVisibleBlock();
    while (block.isValid())
    {
        const QRectF r = blockBoundingGeometry(block).translated(contentOffset());
        if (r.top() > viewport()->height())
        {
            break;
        }
        if (block.isVisible() && isFolded(block.blockNumber()))
        {
            const int x = int(r.left()) + fm.horizontalAdvance(block.text()) +
                          document()->documentMargin() + 6;
            QRect box(x, int(r.top()) + 2, fm.horizontalAdvance(" ... "), fm.height() - 4);
            p.setPen(Color::base1);
            p.setBrush(Color::base2);
            p.drawRoundedRect(box, 3, 3);
            p.setPen(Color::base00);
            p.drawText(box, Qt::AlignCenter, "...");
        }
        block = block.next();
    }

    for (const auto& c : m_extraCursors)
    {
        const QRect r = cursorRect(c);
        p.fillRect(QRect(r.left(), r.top(), 2, r.height()), Color::base00);
    }
}

////////////////////////////////////////////////////////////////////////////////
// Cursor tracking: current line and brackets

void Script::onCursorMoved()
{
    QTextEdit::ExtraSelection line;
    line.format.setBackground(QColor(0, 0, 0, 12));
    line.format.setProperty(QTextFormat::FullWidthSelection, true);
    line.cursor = textCursor();
    line.cursor.clearSelection();
    m_layers[SEL_CURRENT_LINE] = {line};

    highlightBrackets();
    refreshSelections();
    m_gutter->update();

    ensureBlockVisible(textCursor().block());
}

void Script::highlightBrackets()
{
    m_layers[SEL_BRACKET].clear();

    const QTextCursor cur = textCursor();
    const QString text = document()->toPlainText();
    const int pos = cur.position();

    static const QString opens = "([{";
    static const QString closes = ")]}";

    auto matchFrom = [&](int i) -> int {
        const QChar c = text[i];
        int k = opens.indexOf(c);
        if (k >= 0)
        {
            int depth = 0;
            for (int j=i; j < text.size(); ++j)
            {
                if (text[j] == c) depth++;
                else if (text[j] == closes[k] && --depth == 0) return j;
            }
            return -1;
        }
        k = closes.indexOf(c);
        if (k >= 0)
        {
            int depth = 0;
            for (int j=i; j >= 0; --j)
            {
                if (text[j] == c) depth++;
                else if (text[j] == opens[k] && --depth == 0) return j;
            }
        }
        return -1;
    };

    int at = -1;
    if (pos < text.size() && (opens + closes).contains(text[pos]))
    {
        at = pos;
    }
    else if (pos > 0 && (opens + closes).contains(text[pos - 1]))
    {
        at = pos - 1;
    }
    if (at < 0)
    {
        return;
    }

    const int other = matchFrom(at);
    QTextCharFormat fmt;
    if (other >= 0)
    {
        fmt.setBackground(QColor(Color::blue.red(), Color::blue.green(), Color::blue.blue(), 60));
    }
    else
    {
        fmt.setBackground(QColor(Color::red.red(), Color::red.green(), Color::red.blue(), 60));
    }
    for (int i : {at, other})
    {
        if (i < 0) continue;
        QTextEdit::ExtraSelection s;
        s.format = fmt;
        s.cursor = QTextCursor(document());
        s.cursor.setPosition(i);
        s.cursor.setPosition(i + 1, QTextCursor::KeepAnchor);
        m_layers[SEL_BRACKET].append(s);
    }
}

////////////////////////////////////////////////////////////////////////////////
// Navigation helpers

void Script::goToLine(int line, bool flash)
{
    QTextBlock b = document()->findBlockByNumber(std::max(0, line));
    if (!b.isValid())
    {
        return;
    }
    ensureBlockVisible(b);
    QTextCursor c(b);
    setTextCursor(c);

    // Centre the line in the view
    centerCursor();

    if (flash)
    {
        QTextEdit::ExtraSelection s;
        s.format.setBackground(QColor(Color::yellow.red(), Color::yellow.green(),
                                      Color::yellow.blue(), 90));
        s.format.setProperty(QTextFormat::FullWidthSelection, true);
        s.cursor = c;
        setSelections(SEL_FLASH, {s});
        m_flashTimer.start();
    }
}

void Script::selectRange(int line0, int col0, int line1, int col1)
{
    QTextBlock b0 = document()->findBlockByNumber(line0);
    QTextBlock b1 = document()->findBlockByNumber(line1);
    if (!b0.isValid() || !b1.isValid())
    {
        return;
    }
    ensureBlockVisible(b0);
    ensureBlockVisible(b1);
    QTextCursor c(document());
    c.setPosition(b0.position() + std::min(col0, b0.length() - 1));
    c.setPosition(b1.position() + std::min(col1, b1.length() - 1), QTextCursor::KeepAnchor);
    setTextCursor(c);
    centerCursor();
}

void Script::promptGoToLine()
{
    bool ok = false;
    const int line = QInputDialog::getInt(
            this, "Go to line", QString("Line (1 - %1):").arg(blockCount()),
            textCursor().blockNumber() + 1, 1, blockCount(), 1, &ok);
    if (ok)
    {
        goToLine(line - 1);
        setFocus();
    }
}

////////////////////////////////////////////////////////////////////////////////
// Line-editing commands

void Script::toggleComment()
{
    QTextCursor c = textCursor();
    QTextBlock first = document()->findBlock(c.selectionStart());
    QTextBlock last = document()->findBlock(c.selectionEnd());
    if (c.hasSelection() && last.position() == c.selectionEnd() && last != first)
    {
        last = last.previous();   // selection ending at column 0
    }

    // Comment out unless every non-blank line is already commented
    bool allCommented = true;
    int minIndent = INT_MAX;
    for (QTextBlock b = first; b.isValid(); b = b.next())
    {
        const QString t = b.text();
        if (!t.trimmed().isEmpty())
        {
            allCommented &= t.trimmed().startsWith('#');
            minIndent = std::min(minIndent, indentOf(b));
        }
        if (b == last) break;
    }
    if (minIndent == INT_MAX) minIndent = 0;

    QTextCursor e(document());
    e.beginEditBlock();
    for (QTextBlock b = first; b.isValid(); b = b.next())
    {
        const QString t = b.text();
        if (!t.trimmed().isEmpty())
        {
            if (allCommented)
            {
                const int i = t.indexOf('#');
                const int n = (i + 1 < t.size() && t[i + 1] == ' ') ? 2 : 1;
                e.setPosition(b.position() + i);
                e.setPosition(b.position() + i + n, QTextCursor::KeepAnchor);
                e.removeSelectedText();
            }
            else
            {
                e.setPosition(b.position() + minIndent);
                e.insertText("# ");
            }
        }
        if (b == last) break;
    }
    e.endEditBlock();
}

void Script::duplicateLines()
{
    QTextCursor c = textCursor();
    QTextBlock first = document()->findBlock(c.selectionStart());
    QTextBlock last = document()->findBlock(c.selectionEnd());

    const int start = first.position();
    const int end = last.position() + last.length() - 1;
    QTextCursor sel(document());
    sel.setPosition(start);
    sel.setPosition(end, QTextCursor::KeepAnchor);
    const QString text = sel.selectedText().replace(QChar::ParagraphSeparator, '\n');

    QTextCursor e(document());
    e.beginEditBlock();
    e.setPosition(end);
    e.insertText("\n" + text);
    e.endEditBlock();

    // Put the cursor on the copy, same relative position
    const int shift = text.length() + 1;
    QTextCursor n(document());
    n.setPosition(c.anchor() + shift);
    n.setPosition(c.position() + shift, QTextCursor::KeepAnchor);
    setTextCursor(n);
}

void Script::deleteLines()
{
    QTextCursor c = textCursor();
    QTextBlock first = document()->findBlock(c.selectionStart());
    QTextBlock last = document()->findBlock(c.selectionEnd());
    QTextCursor e(document());
    e.beginEditBlock();
    int start = first.position();
    int end = last.position() + last.length();   // includes the newline
    if (end > document()->characterCount() - 1)
    {
        end = document()->characterCount() - 1;
        start = std::max(0, start - 1);          // eat the previous newline
    }
    e.setPosition(start);
    e.setPosition(end, QTextCursor::KeepAnchor);
    e.removeSelectedText();
    e.endEditBlock();
}

void Script::moveLines(int dir)
{
    QTextCursor c = textCursor();
    QTextBlock first = document()->findBlock(c.selectionStart());
    QTextBlock last = document()->findBlock(c.selectionEnd());
    if (c.hasSelection() && last != first && last.position() == c.selectionEnd())
    {
        last = last.previous();
    }
    QTextBlock other = (dir < 0) ? first.previous() : last.next();
    if (!other.isValid())
    {
        return;
    }

    QTextCursor sel(document());
    sel.setPosition(first.position());
    sel.setPosition(last.position() + last.length() - 1, QTextCursor::KeepAnchor);
    const QString moving = sel.selectedText().replace(QChar::ParagraphSeparator, '\n');
    const QString otherText = other.text();

    const int anchorOff = c.anchor() - first.position();
    const int posOff = c.position() - first.position();

    QTextCursor e(document());
    e.beginEditBlock();
    if (dir < 0)
    {
        e.setPosition(other.position());
        e.setPosition(last.position() + last.length() - 1, QTextCursor::KeepAnchor);
        e.insertText(moving + "\n" + otherText);
    }
    else
    {
        e.setPosition(first.position());
        e.setPosition(other.position() + other.length() - 1, QTextCursor::KeepAnchor);
        e.insertText(otherText + "\n" + moving);
    }
    e.endEditBlock();

    const int newFirst = (dir < 0) ? other.position()
                                   : first.position() + otherText.length() + 1;
    QTextCursor n(document());
    n.setPosition(newFirst + anchorOff);
    n.setPosition(newFirst + posOff, QTextCursor::KeepAnchor);
    setTextCursor(n);
}

void Script::setFontSize(int size)
{
    QFont f = document()->defaultFont();
    f.setPointSize(size);
    document()->setDefaultFont(f);
    setTabStopDistance(QFontMetrics(f).horizontalAdvance("  "));
    updateGutterWidth();
    m_gutter->update();
}

void Script::zoomFont(int delta)
{
    const int size = std::max(6, std::min(40, document()->defaultFont().pointSize() + delta));
    setFontSize(size);
    QSettings("FielDes", "FielDes").setValue("editor-font-size", size);
    emit(fontSizeChanged(size));
}

////////////////////////////////////////////////////////////////////////////////
// Completion

void Script::setCompletionWords(const QStringList& words)
{
    m_baseWords = words;
    m_baseWords.removeDuplicates();
}

void Script::setMemberWords(const QString& owner, const QStringList& words)
{
    m_memberWords[owner] = words;
}

void Script::setCallTips(const QMap<QString, QString>& tips)
{
    m_callTips = tips;
}

void Script::setLibraryDefinitions(const QMap<QString, QString>& defs)
{
    m_libraryDefs = defs;
}

void Script::copyVocabularyFrom(const Script& other)
{
    m_baseWords = other.m_baseWords;
    m_memberWords = other.m_memberWords;
    m_callTips = other.m_callTips;
    m_libraryDefs = other.m_libraryDefs;
}

void Script::rescanScriptWords()
{
    static const QRegularExpression ident(R"(\b[A-Za-z_][A-Za-z0-9_]{2,}\b)");
    QSet<QString> words;
    auto it = ident.globalMatch(document()->toPlainText());
    while (it.hasNext())
    {
        words.insert(it.next().captured(0));
    }
    m_scriptWords = QStringList(words.begin(), words.end());
}

QString Script::textUnderCursorForCompletion(QString* owner) const
{
    const QTextCursor c = textCursor();
    const QString line = c.block().text();
    const int col = c.positionInBlock();
    int start = col;
    while (start > 0 && isIdentChar(line[start - 1]))
    {
        --start;
    }
    if (owner)
    {
        owner->clear();
        if (start > 0 && line[start - 1] == '.')
        {
            int oe = start - 1, os = oe;
            while (os > 0 && isIdentChar(line[os - 1]))
            {
                --os;
            }
            *owner = line.mid(os, oe - os);
            if (owner->isEmpty())
            {
                *owner = "*";
            }
        }
    }
    return line.mid(start, col - start);
}

void Script::insertCompletion(const QString& completion)
{
    if (m_completer->widget() != this)
    {
        return;
    }
    QTextCursor tc = textCursor();
    const int n = m_completer->completionPrefix().length();
    tc.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor, n);
    tc.beginEditBlock();
    tc.insertText(completion);

    // A function gets its brackets, with the cursor inside them (after them when it takes nothing);
    // typing ")" steps over the closing one.  Not when a "(" follows already, nor for a variable
    // that merely has a function's name
    static const QSet<QString> builtinCalls = {
        "range", "len", "print", "int", "float", "str", "list", "dict", "tuple", "set", "sum", "sorted",
        "enumerate", "zip", "round", "open", "isinstance", "reversed", "map", "filter", "any", "all",
        "abs", "min", "max", "bool", "type"};
    const auto tip = m_callTips.constFind(completion);
    const bool known = tip != m_callTips.constEnd() || builtinCalls.contains(completion);
    const bool onlyAVariable = !m_baseWords.contains(completion) && m_scriptWords.contains(completion);
    const QString line = tc.block().text();
    const int col = tc.positionInBlock();
    const bool opens = col < line.size() && line[col] == '(';
    bool call = false;
    if (known && !onlyAVariable && !opens && !inStringOrComment(line, col))
    {
        tc.insertText("()");
        call = true;
        // "name()" or "name(self)" in the tip: nothing to type inside
        bool noArguments = false;
        if (tip != m_callTips.constEnd())
        {
            const QString t = tip.value();
            const int open = t.indexOf('(');
            if (open >= 0)
            {
                const QString rest = t.mid(open + 1).trimmed();
                noArguments = rest.startsWith(')') || rest.startsWith("self)");
            }
        }
        if (!noArguments) tc.movePosition(QTextCursor::Left);
    }
    tc.endEditBlock();
    setTextCursor(tc);
    if (call && tip != m_callTips.constEnd()) showCallTip(completion);
}

void Script::triggerCompletion()
{
    QString owner;
    const QString prefix = textUnderCursorForCompletion(&owner);

    QStringList words;
    if (!owner.isEmpty())
    {
        words = m_memberWords.value(owner, m_memberWords.value("*"));
    }
    else
    {
        words = m_baseWords + m_scriptWords;
    }
    words.removeDuplicates();
    words.sort(Qt::CaseInsensitive);
    m_completionModel->setStringList(words);

    m_completer->setCompletionPrefix(prefix);
    if (m_completer->completionCount() == 0 ||
        (m_completer->completionCount() == 1 && m_completer->currentCompletion() == prefix))
    {
        m_completer->popup()->hide();
        return;
    }
    m_completer->popup()->setCurrentIndex(m_completer->completionModel()->index(0, 0));
    m_completer->popup()->setFont(document()->defaultFont());

    QRect cr = cursorRect();
    cr.translate(viewportMargins().left(), 0);
    cr.setWidth(m_completer->popup()->sizeHintForColumn(0) +
                m_completer->popup()->verticalScrollBar()->sizeHint().width() + 8);
    m_completer->complete(cr);
}

void Script::showCallTip(const QString& name)
{
    const auto tip = m_callTips.find(name);
    if (tip == m_callTips.end())
    {
        return;
    }
    QRect cr = cursorRect();
    cr.translate(viewportMargins().left(), 0);
    QToolTip::showText(mapToGlobal(cr.bottomLeft() + QPoint(0, 4)),
                       "<tt>" + tip.value().toHtmlEscaped() + "</tt>", this);
}

////////////////////////////////////////////////////////////////////////////////
// Go to definition

QString Script::wordAt(const QPoint& viewportPos, QTextCursor* range) const
{
    QTextCursor c = cursorForPosition(viewportPos);
    const QString line = c.block().text();
    int col = c.positionInBlock();

    // cursorForPosition snaps to the nearest gap; make sure the point
    // actually lies over text on this line
    const QRect r = cursorRect(c);
    if (std::abs(r.center().y() - viewportPos.y()) > r.height())
    {
        return QString();
    }

    int start = col, end = col;
    while (start > 0 && isIdentChar(line[start - 1])) --start;
    while (end < line.size() && isIdentChar(line[end])) ++end;
    if (start == end || line[start].isDigit())
    {
        return QString();
    }
    if (range)
    {
        *range = QTextCursor(c.block());
        range->setPosition(c.block().position() + start);
        range->setPosition(c.block().position() + end, QTextCursor::KeepAnchor);
    }
    return line.mid(start, end - start);
}

int Script::findDefinitionLine(const QString& name, int fromLine, bool onlyDefs) const
{
    const QString n = QRegularExpression::escape(name);
    const QRegularExpression defRe("^\\s*(?:def|class)\\s+" + n + "\\b");
    const QRegularExpression forRe("^\\s*for\\s+[^:]*\\b" + n + "\\b[^:]*\\bin\\b");
    const QRegularExpression importRe("^\\s*(?:from\\s+\\S+\\s+)?import\\s+.*\\b" + n + "\\b");
    const QRegularExpression withRe("^\\s*with\\s+.*\\bas\\s+" + n + "\\b");
    const QRegularExpression wordRe("\\b" + n + "\\b");
    const QRegularExpression targetsRe(R"(^\s*[\w\s,()\[\]]+$)");
    const QRegularExpression callLikeRe(R"(\w\s*\()");

    auto isDefinition = [&](const QString& line) {
        if (onlyDefs) return defRe.match(line).hasMatch();
        if (defRe.match(line).hasMatch() || forRe.match(line).hasMatch() ||
            importRe.match(line).hasMatch() || withRe.match(line).hasMatch())
        {
            return true;
        }
        // Plain / tuple assignment: the first bare '=' splits targets
        for (int i=0; i < line.size(); ++i)
        {
            if (line[i] == '#') return false;
            if (line[i] == '=' &&
                (i + 1 >= line.size() || line[i + 1] != '=') &&
                (i == 0 || !QString("=<>!+-*/%&|^:@").contains(line[i - 1])))
            {
                const QString lhs = line.left(i);
                return targetsRe.match(lhs).hasMatch() &&
                       !callLikeRe.match(lhs).hasMatch() &&
                       wordRe.match(lhs).hasMatch();
            }
        }
        return false;
    };

    int before = -1, after = -1;
    for (QTextBlock b = document()->begin(); b.isValid(); b = b.next())
    {
        if (isDefinition(b.text()))
        {
            if (b.blockNumber() <= fromLine) before = b.blockNumber();
            else if (after < 0) after = b.blockNumber();
        }
    }
    return before >= 0 ? before : after;
}

void Script::revealName(int line0, const QString& name)
{
    goToLine(line0);
    QTextBlock b = document()->findBlockByNumber(line0);
    if (!b.isValid()) return;
    QTextCursor c(b);
    const QRegularExpression wordRe("\\b" + QRegularExpression::escape(name) + "\\b");
    const auto m = wordRe.match(b.text());
    if (m.hasMatch())
    {
        c.setPosition(b.position() + m.capturedStart());
        c.setPosition(b.position() + m.capturedEnd(), QTextCursor::KeepAnchor);
    }
    else
    {
        c.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    }
    setTextCursor(c);
}

QString Script::ownerBefore(const QString& line, int wordStart)
{
    // "shape.max": the identifier in front of the dot ("*" for "(a + b).max")
    if (wordStart <= 0 || line[wordStart - 1] != '.') return QString();
    int end = wordStart - 1, start = end;
    while (start > 0 && isIdentChar(line[start - 1])) --start;
    return start < end ? line.mid(start, end - start) : QString("*");
}

bool Script::resolvable(const QString& name, const QString& owner)
{
    if (!m_resolver) return false;
    const QString key = owner + '.' + name;
    auto it = m_linkCache.find(key);
    if (it == m_linkCache.end())
    {
        it = m_linkCache.insert(key, !m_resolver(name, owner).isEmpty());
    }
    return it.value();
}

bool Script::goToDefinition(const QString& name, int fromLine, const QString& owner)
{
    if (name.isEmpty())
    {
        return false;
    }
    // In this file first: "x.name" can only be a method here, anything else may be a variable too
    const int line = findDefinitionLine(name, fromLine, !owner.isEmpty());
    static const QRegularExpression importLine(R"(^\s*(?:from\s+\S+\s+)?import\s)");
    const bool onImport = line >= 0 &&
        importLine.match(document()->findBlockByNumber(line).text()).hasMatch();
    if (line >= 0 && !onImport && line != fromLine)
    {
        revealName(line, name);
        return true;
    }
    // Another file: the editor knows which file this is and follows what it imports
    if (m_resolver)
    {
        const auto parts = m_resolver(name, owner).split('\t');
        if (parts.size() == 2)
        {
            emit(libraryDefinitionRequested(name, parts[0], parts[1].toInt()));
            return true;
        }
    }
    if (line >= 0 && line != fromLine)
    {
        revealName(line, name);     // (the import line itself, when the file behind it can't be read)
        return true;
    }
    const auto lib = m_libraryDefs.find(name);
    if (lib != m_libraryDefs.end())
    {
        const auto parts = lib.value().split('\t');
        if (parts.size() == 2)
        {
            emit(libraryDefinitionRequested(name, parts[0], parts[1].toInt()));
            return true;
        }
    }
    return false;
}

void Script::goToDefinitionAtCursor()
{
    QTextCursor c = textCursor();
    const QString line = c.block().text();
    int start = c.positionInBlock(), end = start;
    while (start > 0 && isIdentChar(line[start - 1])) --start;
    while (end < line.size() && isIdentChar(line[end])) ++end;
    goToDefinition(line.mid(start, end - start), c.blockNumber(), ownerBefore(line, start));
}

////////////////////////////////////////////////////////////////////////////////
// Multi-cursor editing

void Script::selectNextOccurrence()
{
    QTextCursor c = textCursor();
    if (!c.hasSelection())
    {
        c.select(QTextCursor::WordUnderCursor);
        setTextCursor(c);
        return;
    }
    const QString needle = c.selectedText();

    // Search after the furthest cursor, wrapping around once
    int from = c.selectionEnd();
    for (const auto& x : m_extraCursors)
    {
        from = std::max(from, x.selectionEnd());
    }
    QTextCursor found = document()->find(needle, from, QTextDocument::FindCaseSensitively);
    if (found.isNull())
    {
        found = document()->find(needle, 0, QTextDocument::FindCaseSensitively);
    }
    if (found.isNull())
    {
        return;
    }
    auto same = [&](const QTextCursor& a) {
        return a.selectionStart() == found.selectionStart() &&
               a.selectionEnd() == found.selectionEnd();
    };
    if (same(c) || std::any_of(m_extraCursors.begin(), m_extraCursors.end(), same))
    {
        return;   // every occurrence already selected
    }
    m_extraCursors.append(c);
    setTextCursor(found);
    ensureBlockVisible(found.block());

    QList<QTextEdit::ExtraSelection> sels;
    for (const auto& x : m_extraCursors)
    {
        QTextEdit::ExtraSelection s;
        s.cursor = x;
        s.format.setBackground(palette().highlight().color().lighter(150));
        sels.append(s);
    }
    setSelections(SEL_MULTI, sels);
    viewport()->update();
}

void Script::setMultiCursors(const QList<QTextCursor>& cursors)
{
    if (cursors.isEmpty())
    {
        clearExtraCursors();
        return;
    }
    m_extraCursors = cursors.mid(0, cursors.size() - 1);
    setTextCursor(cursors.last());
    QList<QTextEdit::ExtraSelection> sels;
    for (const auto& x : m_extraCursors)
    {
        QTextEdit::ExtraSelection s;
        s.cursor = x;
        s.format.setBackground(palette().highlight().color().lighter(150));
        sels.append(s);
    }
    setSelections(SEL_MULTI, sels);
    viewport()->update();
}

void Script::clearExtraCursors()
{
    if (!m_extraCursors.isEmpty())
    {
        m_extraCursors.clear();
        setSelections(SEL_MULTI, {});
        viewport()->update();
    }
}

bool Script::handleMultiCursorKey(QKeyEvent* e)
{
    if (m_extraCursors.isEmpty())
    {
        return false;
    }
    const auto mods = e->modifiers() & ~(Qt::KeypadModifier | Qt::ShiftModifier);
    if (e->key() == Qt::Key_Escape)
    {
        clearExtraCursors();
        return true;
    }

    QList<QTextCursor> all = m_extraCursors;
    all.append(textCursor());

    auto apply = [&](auto fn) {
        QTextCursor main = textCursor();
        main.beginEditBlock();
        for (auto& c : all)
        {
            fn(c);
        }
        main.endEditBlock();
        setTextCursor(all.last());
        m_extraCursors = all.mid(0, all.size() - 1);
        QList<QTextEdit::ExtraSelection> sels;
        setSelections(SEL_MULTI, sels);
        viewport()->update();
    };

    if (e->key() == Qt::Key_Backspace && mods == 0)
    {
        apply([](QTextCursor& c){
            if (c.hasSelection()) c.removeSelectedText(); else c.deletePreviousChar(); });
        return true;
    }
    if (e->key() == Qt::Key_Delete && mods == 0)
    {
        apply([](QTextCursor& c){
            if (c.hasSelection()) c.removeSelectedText(); else c.deleteChar(); });
        return true;
    }
    const QString text = e->text();
    if (!text.isEmpty() && mods == 0 && text[0].isPrint())
    {
        apply([&](QTextCursor& c){ c.insertText(text); });
        return true;
    }
    if (e->key() == Qt::Key_Left || e->key() == Qt::Key_Right ||
        e->key() == Qt::Key_Home || e->key() == Qt::Key_End)
    {
        QTextCursor::MoveOperation op =
            e->key() == Qt::Key_Left ? QTextCursor::Left :
            e->key() == Qt::Key_Right ? QTextCursor::Right :
            e->key() == Qt::Key_Home ? QTextCursor::StartOfLine : QTextCursor::EndOfLine;
        const auto mode = (e->modifiers() & Qt::ShiftModifier)
            ? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor;
        for (auto& c : all)
        {
            if (!c.hasSelection() || mode == QTextCursor::KeepAnchor ||
                op == QTextCursor::StartOfLine || op == QTextCursor::EndOfLine)
            {
                c.movePosition(op, mode);
            }
            else
            {   // Collapse the selection towards the arrow
                c.setPosition(op == QTextCursor::Left ? c.selectionStart()
                                                      : c.selectionEnd());
            }
        }
        setTextCursor(all.last());
        m_extraCursors = all.mid(0, all.size() - 1);
        setSelections(SEL_MULTI, {});
        viewport()->update();
        return true;
    }
    // Anything else (Enter, Up/Down, shortcuts...) ends multi-cursor mode
    clearExtraCursors();
    return false;
}

////////////////////////////////////////////////////////////////////////////////
// Events

void Script::keyPressEvent(QKeyEvent* e)
{
    // Keys that the completion popup handles itself
    if (m_completer->popup()->isVisible())
    {
        switch (e->key())
        {
            case Qt::Key_Enter:
            case Qt::Key_Return:
            case Qt::Key_Escape:
            case Qt::Key_Tab:
            case Qt::Key_Backtab:
                e->ignore();
                return;
            default:
                break;
        }
    }

    if (handleMultiCursorKey(e))
    {
        return;
    }

    const bool ctrl = e->modifiers() & Qt::ControlModifier;
    if (ctrl && e->key() == Qt::Key_Space)
    {
        triggerCompletion();
        return;
    }

    // Auto-close brackets (and step over an existing closing bracket)
    const QString typed = e->text();
    if (typed.size() == 1 && !ctrl && !(e->modifiers() & Qt::AltModifier))
    {
        const QChar ch = typed[0];
        QTextCursor c = textCursor();
        const QString line = c.block().text();
        const int col = c.positionInBlock();
        const QChar next = col < line.size() ? line[col] : QChar(' ');
        static const QString opens = "([{", closes = ")]}";
        if (closes.contains(ch) && next == ch && !c.hasSelection())
        {
            c.movePosition(QTextCursor::Right);
            setTextCursor(c);
            return;
        }
        const int k = opens.indexOf(ch);
        if (k >= 0 && !inStringOrComment(line, col) &&
            (next.isSpace() || closes.contains(next) || next == ',' || next == ':'))
        {
            c.beginEditBlock();
            if (c.hasSelection())
            {   // Wrap the selection
                const QString s = c.selectedText();
                c.insertText(QString(ch) + s + closes[k]);
            }
            else
            {
                c.insertText(QString(ch) + closes[k]);
                c.movePosition(QTextCursor::Left);
            }
            c.endEditBlock();
            setTextCursor(c);
            if (ch == '(')
            {
                QTextCursor t = textCursor();
                t.movePosition(QTextCursor::Left);
                const QString l = t.block().text();
                int end = t.positionInBlock(), start = end;
                while (start > 0 && isIdentChar(l[start - 1])) --start;
                showCallTip(l.mid(start, end - start));
            }
            return;
        }
    }
    // Backspace between an empty bracket pair deletes both
    if (e->key() == Qt::Key_Backspace && e->modifiers() == Qt::NoModifier)
    {
        QTextCursor c = textCursor();
        const QString line = c.block().text();
        const int col = c.positionInBlock();
        if (!c.hasSelection() && col > 0 && col < line.size())
        {
            const QString pair = line.mid(col - 1, 2);
            if (pair == "()" || pair == "[]" || pair == "{}")
            {
                c.beginEditBlock();
                c.deleteChar();
                c.deletePreviousChar();
                c.endEditBlock();
                return;
            }
        }
    }

    if (m_formatter)
    {
        m_formatter->keyPressEvent(this, e);
    }
    else
    {
        e->ignore();
    }
    if (!e->isAccepted())
    {
        QPlainTextEdit::keyPressEvent(e);
    }

    // Completion popup: shown while typing an identifier (2+ chars) or
    // right after "owner."; kept up to date on Backspace while open.
    QString owner;
    const QString prefix = textUnderCursorForCompletion(&owner);
    const QTextCursor tc = textCursor();
    const bool inStr = inStringOrComment(tc.block().text(), tc.positionInBlock());
    const bool identKey = !typed.isEmpty() &&
        (isIdentChar(typed[0]) || typed[0] == '.');
    const bool wanted = !owner.isEmpty() || prefix.length() >= 2;

    if (typed == "(" && !inStr)
    {
        // Call tip for "name(" when the bracket wasn't auto-closed
        const QString l = tc.block().text();
        int end = tc.positionInBlock() - 1, start = end;
        while (start > 0 && isIdentChar(l[start - 1])) --start;
        if (end > start) showCallTip(l.mid(start, end - start));
    }

    if (identKey && !ctrl && !inStr && wanted)
    {
        triggerCompletion();
    }
    else if (e->key() == Qt::Key_Backspace && m_completer->popup()->isVisible() &&
             wanted && !inStr)
    {
        triggerCompletion();
    }
    else if (m_completer->popup()->isVisible())
    {
        m_completer->popup()->hide();
    }
}

void Script::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton && (e->modifiers() & Qt::ControlModifier))
    {
        QTextCursor range;
        const QString w = wordAt(e->pos(), &range);
        if (!w.isEmpty() &&
            goToDefinition(w, range.blockNumber(),
                           ownerBefore(range.block().text(), range.selectionStart() - range.block().position())))
        {
            setSelections(SEL_LINK, {});
            e->accept();
            return;
        }
    }
    if (e->button() == Qt::LeftButton && (e->modifiers() & Qt::AltModifier))
    {
        // Alt+click adds a cursor
        m_extraCursors.append(textCursor());
        setTextCursor(cursorForPosition(e->pos()));
        viewport()->update();
        e->accept();
        return;
    }
    clearExtraCursors();
    QPlainTextEdit::mousePressEvent(e);
}

void Script::mouseMoveEvent(QMouseEvent* e)
{
    // Ctrl+hover shows go-to-definition links
    if (e->modifiers() & Qt::ControlModifier)
    {
        QTextCursor range;
        const QString w = wordAt(e->pos(), &range);
        const bool linkable = !w.isEmpty() &&
            (findDefinitionLine(w, range.blockNumber()) >= 0 ||
             resolvable(w, ownerBefore(range.block().text(),
                                       range.selectionStart() - range.block().position())) ||
             m_libraryDefs.contains(w));
        if (linkable)
        {
            QTextEdit::ExtraSelection s;
            s.cursor = range;
            s.format.setFontUnderline(true);
            s.format.setForeground(Color::blue);
            setSelections(SEL_LINK, {s});
            viewport()->setCursor(Qt::PointingHandCursor);
        }
        else if (!m_layers[SEL_LINK].isEmpty())
        {
            setSelections(SEL_LINK, {});
            viewport()->setCursor(Qt::IBeamCursor);
        }
    }
    else if (!m_layers[SEL_LINK].isEmpty())
    {
        setSelections(SEL_LINK, {});
        viewport()->setCursor(Qt::IBeamCursor);
    }
    QPlainTextEdit::mouseMoveEvent(e);
}

void Script::focusOutEvent(QFocusEvent* e)
{
    if (!m_layers[SEL_LINK].isEmpty())
    {
        setSelections(SEL_LINK, {});
    }
    QPlainTextEdit::focusOutEvent(e);
}

bool Script::event(QEvent* e)
{
    // Keep Escape for ourselves (not the window's "cancel render") while
    // it has a local meaning: closing completions or extra cursors
    if (e->type() == QEvent::ShortcutOverride)
    {
        auto k = static_cast<QKeyEvent*>(e);
        if (k->key() == Qt::Key_Escape &&
            (!m_extraCursors.isEmpty() || m_completer->popup()->isVisible()))
        {
            e->accept();
            return true;
        }
    }
    // Hovering a documented library name shows its signature
    if (e->type() == QEvent::ToolTip)
    {
        auto he = static_cast<QHelpEvent*>(e);
        const QPoint vp = viewport()->mapFrom(this, he->pos());
        const QString w = wordAt(vp);
        const auto tip = m_callTips.find(w);
        if (tip != m_callTips.end())
        {
            QToolTip::showText(he->globalPos(),
                               "<tt>" + tip.value().toHtmlEscaped() + "</tt>", this);
        }
        else
        {
            QToolTip::hideText();
        }
        return true;
    }
    if (e->type() == QEvent::KeyRelease &&
        static_cast<QKeyEvent*>(e)->key() == Qt::Key_Control &&
        !m_layers[SEL_LINK].isEmpty())
    {
        setSelections(SEL_LINK, {});
        viewport()->setCursor(Qt::IBeamCursor);
    }
    return QPlainTextEdit::event(e);
}

}   // namespace FielDes
