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
#include <array>
#include <functional>
#include <set>
#include <cassert>

#include <iostream>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QSizePolicy>
#include <QRegularExpression>
#include <QScrollBar>
#include <QtConcurrent/QtConcurrentRun>
#include <QPainterPath>
#include <QPolygonF>
#include <QTextBlock>
#include <QToolButton>

#include "libfive/step/step_progress.hpp"
#include "libfive/run_progress.hpp"

#include "fieldes/i18n.hpp"
#include "fieldes/editor.hpp"
#include "fieldes/popout.hpp"
#include "fieldes/script.hpp"
#include "fieldes/findbar.hpp"
#include "fieldes/color.hpp"
#include "fieldes/theme.hpp"

#include "fieldes/python/formatter.hpp"
#include "fieldes/python/language.hpp"
#include "fieldes/python/syntax.hpp"

namespace FielDes {

namespace {

enum class Glyph { Pause, Play, Retry, Terminate };

// The icons of the buttons under the editor, drawn here: the same size and weight whatever symbols the fonts of the computer have
QIcon glyphIcon(Glyph g)
{
    QIcon icon;
    for (const bool disabled : {false, true})
    {
        const qreal dpr = 2.0;
        QPixmap pm(int(16 * dpr), int(16 * dpr));
        pm.setDevicePixelRatio(dpr);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        QColor c = g == Glyph::Terminate ? Color::red : Theme::text;
        if (disabled) c.setAlpha(80);
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        switch (g)
        {
            case Glyph::Pause:
                p.drawRoundedRect(QRectF(4, 3, 3, 10), 0.8, 0.8);
                p.drawRoundedRect(QRectF(9, 3, 3, 10), 0.8, 0.8);
                break;
            case Glyph::Play:
                p.drawPolygon(QPolygonF({QPointF(4.5, 2.5), QPointF(13, 8), QPointF(4.5, 13.5)}));
                break;
            case Glyph::Terminate:
                p.drawEllipse(QPointF(8, 8), 5.2, 5.2);
                break;
            case Glyph::Retry:
            {
                // The circular arrow: an arc of 280 degrees going clockwise, ending in an arrow head at the top
                const QPointF centre(8, 8);
                const qreal r = 4.6;
                const QRectF box(centre.x() - r, centre.y() - r, 2 * r, 2 * r);
                QPainterPath arc;
                arc.arcMoveTo(box, 20);
                arc.arcTo(box, 20, -280);
                p.setPen(QPen(c, 1.7, Qt::SolidLine, Qt::RoundCap));
                p.setBrush(Qt::NoBrush);
                p.drawPath(arc);
                const qreal a = qDegreesToRadians(100.0);
                const QPointF end(centre.x() + r * std::cos(a), centre.y() - r * std::sin(a));
                const QPointF dir(std::sin(a), std::cos(a));        // (the way the arc is going there)
                const QPointF side(-dir.y(), dir.x());
                p.setPen(Qt::NoPen);
                p.setBrush(c);
                p.drawPolygon(QPolygonF({end + dir * 3.2, end + side * 2.5 - dir * 0.4, end - side * 2.5 - dir * 0.4}));
                break;
            }
        }
        p.end();
        icon.addPixmap(pm, disabled ? QIcon::Disabled : QIcon::Normal);
    }
    return icon;
}

// The play mark on the first tab (the tab that is rendered), in the accent colour
QIcon playTabIcon()
{
    const qreal dpr = 2.0;
    QPixmap pm(int(16 * dpr), int(16 * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(Theme::accent);
    p.drawPolygon(QPolygonF({QPointF(4, 2.5), QPointF(13, 8), QPointF(4, 13.5)}));
    p.end();
    return QIcon(pm);
}

// The progress row and the row of a breakpoint: cream in the light set, the colour of the bars in the dark one
QString rowStyle()
{
    return QString("background-color: %1;").arg(Theme::dark() ? Theme::surface.name() : Color::base3.name());
}

QString runLabelStyle()
{
    return QString("color: %1;").arg(Theme::dark() ? Theme::muted.name() : Color::base01.name());
}

// The line the console is typed in: Enter runs it, Shift+Enter is a new line, Up on the first line and Down on the last go through what was run before
class ConsoleInput : public QPlainTextEdit
{
public:
    std::function<void()> run;
    std::function<void(int)> history;
    std::function<void()> leave;            // (Escape: back to the script -- the window's own Escape, which cancels a render, is not for this line)

protected:
    bool event(QEvent* e) override
    {
        if (e->type() == QEvent::ShortcutOverride && static_cast<QKeyEvent*>(e)->key() == Qt::Key_Escape && leave)
        {
            e->accept();
            return true;
        }
        return QPlainTextEdit::event(e);
    }

    void keyPressEvent(QKeyEvent* event) override
    {
        if (event->key() == Qt::Key_Escape && leave)
        {
            leave();
            return;
        }
        const bool plain = !(event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier));
        if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && plain)
        {
            if (run) run();
            return;
        }
        if (plain && event->key() == Qt::Key_Up && textCursor().blockNumber() == 0 && history)
        {
            history(-1);
            return;
        }
        if (plain && event->key() == Qt::Key_Down && textCursor().blockNumber() == document()->blockCount() - 1 && history)
        {
            history(1);
            return;
        }
        QPlainTextEdit::keyPressEvent(event);
    }
};

}   // namespace

// The terminal at the bottom of the editor: a header (the progress of the run, Fix All when the warnings have a fix, the buttons of the run), the output -- what the run printed,
// its error, its warnings, and what was typed in the line below and answered -- and that line, which is where Python is typed.  The pane is always there, and the splitter above it
// is how large it is
QWidget* Editor::buildTerminal()
{
    m_terminal = new QWidget;
    m_terminal->setObjectName("Terminal");
    auto v = new QVBoxLayout(m_terminal);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    auto header = new QWidget;
    header->setObjectName("TerminalHeader");
    auto h = new QHBoxLayout(header);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(2);
    m_fixButton = new QPushButton(T("Fix All"));
    m_fixButton->setFocusPolicy(Qt::NoFocus);
    m_fixButton->hide();
    h->addWidget(m_runRow, 100);
    h->addStretch(1);
    h->addWidget(m_fixButton, 0, Qt::AlignVCenter);
    h->addWidget(m_controls, 0, Qt::AlignVCenter);
    auto popoutRoom = new QWidget;                   // (the corner of the console is for the button that pulls it out into a window: the buttons stay clear of it)
    popoutRoom->setFixedWidth(26);
    h->addWidget(popoutRoom);
    v->addWidget(header);

    err->setMinimumHeight(0);
    err->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    err->setMaximumBlockCount(6000);
    // (an output that is at its end stays at its end when the pane is made lower or higher; one that was scrolled up stays where it was)
    connect(err->verticalScrollBar(), &QScrollBar::rangeChanged, this, [this](int, int max) {
        if (m_outputAtEnd) err->verticalScrollBar()->setValue(max);
    });
    connect(err->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        m_outputAtEnd = value >= err->verticalScrollBar()->maximum() - 2;
    });
    v->addWidget(err, 1);

    const QFont font = err_doc->defaultFont();
    auto row = new QWidget;
    row->setObjectName("ConsoleRow");
    auto r = new QHBoxLayout(row);
    r->setContentsMargins(6, 0, 4, 0);
    r->setSpacing(4);
    auto prompt = new QLabel(">>>");
    prompt->setObjectName("ConsolePrompt");
    prompt->setFont(font);
    prompt->setContentsMargins(0, 2, 0, 0);
    auto input = new ConsoleInput;
    m_consoleInput = input;
    m_consoleInput->setObjectName("ConsoleInput");
    m_consoleInput->setFrameShape(QFrame::NoFrame);
    m_consoleInput->setFont(font);              // (the hint in the empty line is drawn in the widget's font, what is typed in the document's: both the font of the output)
    m_consoleInput->document()->setDefaultFont(font);
    m_consoleInput->setTabChangesFocus(false);
    // (the hint is short, so that it fits the line when the editor is narrow; the whole of it is the tooltip.  The line does not wrap: what is typed goes on to the side, and the line grows by the lines of it)
    m_consoleInput->setPlaceholderText(T("Python (Enter runs, Shift+Enter new line, Up/Down history)"));
    m_consoleInput->setToolTip(T("Python: Enter runs, Shift+Enter is a new line, Up and Down go through what was run"));
    m_consoleInput->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_consoleInput->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    input->run = [this] {
        const QString code = m_consoleInput->toPlainText();
        if (code.trimmed().isEmpty()) return;
        m_consoleInput->clear();
        runConsole(code);
    };
    input->history = [this](int step) { consoleHistory(step); };
    input->leave = [this] { script->setFocus(Qt::OtherFocusReason); };
    connect(m_consoleInput, &QPlainTextEdit::textChanged, this, &Editor::fitConsoleInput);
    fitConsoleInput();
    r->addWidget(prompt, 0, Qt::AlignTop);
    r->addWidget(m_consoleInput, 1);
    v->addWidget(row);
    PopOut::attach(m_terminal, T("Console and output"), "terminal");
    return m_terminal;
}

// The line Python is typed in is as high as what is typed in it (one line, up to six)
void Editor::fitConsoleInput()
{
    if (!m_consoleInput) return;
    const int lines = std::max(1, std::min(6, m_consoleInput->document()->blockCount()));
    // (the lines, the margin of the document above and below them and the padding of the style sheet: with less the line shows a scroll bar for one line of text)
    m_consoleInput->setFixedHeight(lines * QFontMetrics(m_consoleInput->font()).lineSpacing() + int(2 * m_consoleInput->document()->documentMargin()) + 6);
}

// The text of the terminal is made again from its parts: the result of the run (its color), the warnings of the run (they may have bold and code in them) and what the console
// did.  The first two are replaced by every run, the last stays
void Editor::rebuildTerminal()
{
    err->clear();
    QTextCursor cursor(err->document());
    bool first = true;
    auto paragraph = [&] {
        if (!first) cursor.insertBlock(QTextBlockFormat(), QTextCharFormat());
        first = false;
    };
    if (!m_resultText.isEmpty())
    {
        paragraph();
        QTextCharFormat fmt;
        fmt.setForeground(m_resultKind == 1 ? Theme::muted : m_resultKind == 2 ? Theme::text : m_resultColor);
        cursor.insertText(m_resultText, fmt);
    }
    for (const QString& w : m_warningTexts)
    {
        paragraph();
        if (Qt::mightBeRichText(w))
        {
            cursor.insertHtml(w);
        }
        else
        {
            QTextCharFormat fmt;
            fmt.setForeground(Theme::text);
            cursor.insertText(w, fmt);
        }
    }
    for (const auto& e : m_consoleEntries)
    {
        paragraph();
        QTextCharFormat fmt;
        fmt.setForeground(consoleColor(e.second));
        cursor.insertText(e.first, fmt);
    }
    // (what was just done in the console is at the end: that is where the eye is; a run that has said nothing of the kind starts at the top)
    err->verticalScrollBar()->setValue(m_consoleEntries.isEmpty() ? 0 : err->verticalScrollBar()->maximum());
}

// The first time the editor is shown the terminal gets the height it had when the program was last closed (seven lines of output and the line Python is typed in, at first)
void Editor::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (m_terminalSized) return;
    QTimer::singleShot(0, this, [this] {
        if (m_terminalSized || !m_split || m_split->height() < 200) return;
        const int fallback = 8 * QFontMetrics(err_doc->defaultFont()).lineSpacing() + 40;
        const int saved = QSettings().value("terminal-height", fallback).toInt();
        const int h = std::max(70, std::min(saved, m_split->height() / 2));
        m_split->setSizes({m_split->height() - h, h});
        m_terminalSized = true;
    });
}

QColor Editor::consoleColor(int kind) const
{
    return kind == 0 ? Theme::muted : kind == 1 ? Theme::text : kind == 2 ? Theme::accent : Color::red;
}

void Editor::appendConsole(const QString& text, int kind)
{
    m_consoleEntries << qMakePair(text, kind);
    if (m_consoleEntries.size() > 3000) m_consoleEntries.removeFirst();
    rebuildTerminal();
}

void Editor::consoleHistory(int step)
{
    if (m_consoleHistory.isEmpty()) return;
    const int to = std::max(0, std::min(int(m_consoleHistory.size()), m_consoleAt + step));
    m_consoleAt = to;
    m_consoleInput->setPlainText(to < m_consoleHistory.size() ? m_consoleHistory[to] : QString());
    m_consoleInput->moveCursor(QTextCursor::End);
}

void Editor::focusConsole()
{
    if (m_consoleInput) m_consoleInput->setFocus(Qt::OtherFocusReason);
}

void Editor::runConsole(const QString& code)
{
    if (code.trimmed().isEmpty() || m_consoleBusy || !m_consoleInput) return;
    if (m_consoleHistory.isEmpty() || m_consoleHistory.last() != code) m_consoleHistory << code;
    m_consoleAt = int(m_consoleHistory.size());
    QString shown = code;
    shown.replace("\n", "\n... ");
    appendConsole(">>> " + shown, 0);
    m_consoleBusy = true;
    m_consoleInput->setReadOnly(true);
    // (on a thread of its own: a long computation does not stop the window; the interpreter takes turns with the script that may be running)
    const QString request = QString::fromUtf8(QJsonDocument(QJsonObject{{"code", code}}).toJson(QJsonDocument::Compact));
    auto watcher = new QFutureWatcher<QPair<QString, QString>>(this);
    connect(watcher, &QFutureWatcher<QPair<QString, QString>>::finished, this, [this, watcher] {
        const auto answer = watcher->result();
        watcher->deleteLater();
        m_consoleBusy = false;
        m_consoleInput->setReadOnly(false);
        if (!answer.second.isEmpty())
        {
            appendConsole(answer.second, 3);
            return;
        }
        const QJsonObject r = QJsonDocument::fromJson(answer.first.toUtf8()).object();
        const QString output = r["output"].toString(), value = r["value"].toString(), error = r["error"].toString();
        if (!output.isEmpty()) appendConsole(output.endsWith('\n') ? output.left(output.size() - 1) : output, 1);
        if (!value.isEmpty()) appendConsole(value, 2);
        if (!error.isEmpty()) appendConsole(error, 3);
    });
    watcher->setFuture(QtConcurrent::run([this, request] {
        QString error;
        const QString out = callSupport("terminal_exec", request, &error);
        return qMakePair(out, error);
    }));
}

void Editor::applyTheme()
{
    // The editor is paper-white (or the dark set's paper), the output a shade darker; both with the text colour of the chrome
    // (the other tabs, which are not rendered, have a slightly darker page; the first tab is the bold one with the accent line)
    setStyleSheet(QString(
        "QPlainTextEdit { background-color: %1; color: %2; selection-background-color: %8; border: none; }"
        "QPlainTextEdit#Output { background-color: %3; border: none; padding: 2px 4px; }"
        "QPlainTextEdit#Aux { background-color: %9; }"
        "QTabBar { background: %3; }"
        "QTabBar::tab { background: %3; color: %5; padding: 4px 12px 3px 10px; border: none;"
        "               border-right: 1px solid %4; border-bottom: 2px solid transparent; }"
        "QTabBar::tab:selected { background: %1; color: %2; border-bottom: 2px solid %6; }"
        "QTabBar::tab:first { color: %2; font-weight: bold; }"
        "QTabBar::tab:first:selected { border-bottom: 2px solid %7; }"
        "QWidget#AuxRow { background: %3; border-top: 1px solid %4; }"
        "QWidget#AuxRow QLabel { color: %5; }"
        "QWidget#Terminal { background: %3; }"
        "QWidget#TerminalHeader { background: %3; }"
        "QWidget#Controls { background: %3; }"
        "QWidget#Controls QToolButton { border: none; border-radius: 3px; padding: 2px; }"
        "QWidget#Controls QToolButton:hover { background: " + Theme::hoverShade() + "; }"
        "QWidget#ConsoleRow { background: %1; border-top: 1px solid %4; }"
        "QPlainTextEdit#ConsoleInput { background-color: %1; border: none; padding: 2px 4px; }"
        "QLabel#ConsolePrompt { color: %5; background: transparent; }"
        "QSplitter::handle:vertical { background: %3; border-top: 1px solid %4; }"
        "QSplitter::handle:vertical:hover { background: " + Theme::hoverShade() + "; }")
        .arg(Theme::paper.name(), Theme::text.name(), Theme::chrome.name(), Theme::border.name(),
             Theme::muted.name(), Theme::tabLine().name(), Theme::accent.name(), Theme::selection().name(), Theme::aux().name()));

    // What was drawn with the colours when it was made (the icons, the rows of the run) is made again; the first call, from the constructor, finds none of them
    if (m_tabs) m_tabs->setTabIcon(0, playTabIcon());
    if (m_retryButton)
    {
        m_iconPause = glyphIcon(Glyph::Pause);
        m_iconPlay = glyphIcon(Glyph::Play);
        m_retryButton->setIcon(glyphIcon(Glyph::Retry));
        m_terminateButton->setIcon(glyphIcon(Glyph::Terminate));
        updateControls();
    }
    if (m_runLabel) m_runLabel->setStyleSheet(runLabelStyle());
    if (m_runRow) m_runRow->setStyleSheet(rowStyle());
    if (m_pauseRow) m_pauseRow->setStyleSheet(rowStyle());
    script->update();
    rebuildTerminal();                      // (what the terminal shows is drawn again in the colours of the theme)
    script->updateBlockShading();           // (the shade of every second object is made of the colours of the theme)
    for (auto& a : m_aux) a->script->updateBlockShading();
}

Editor::Editor(Language::Type language)
    : QWidget(nullptr), script(new Script), script_doc(script->document()),
      m_findBar(new FindBar(script)),
      err(new QPlainTextEdit), err_doc(err->document()),
      layout(new QVBoxLayout)
{
    error_format.setUnderlineColor(Color::red);
    error_format.setUnderlineStyle(QTextCharFormat::SingleUnderline);

    script->setLineWrapMode(QPlainTextEdit::NoWrap);
    err->setReadOnly(true);

    {   // Inconsolata, the size remembered across runs; the output below the script a few points smaller
        QFont font("Inconsolata",
                   QSettings().value("editor-font-size", 11).toInt());
        QFontMetrics fm(font);
        script->setTabStopDistance(fm.horizontalAdvance("  "));
        script_doc->setDefaultFont(font);
        QFont out("Inconsolata", std::max(8, font.pointSize() - 3));
        err_doc->setDefaultFont(out);
    }

    // The editor is paper-white, the output a shade darker; both with the text colour of the chrome
    err->setObjectName("Output");
    err->setFrameShape(QFrame::NoFrame);
    script->setFrameShape(QFrame::NoFrame);
    // (the other tabs, which are not rendered, have a slightly darker page; the first tab is the bold
    // one with the accent line)
    applyTheme();

    // Emit the script whenever text changes
    connect(script, &QPlainTextEdit::textChanged,
            &m_textChangedDebounce, QOverload<>::of(&QTimer::start));
    // ... and read the model tree from it, at once: it does not wait for a run
    m_outlineTimer.setSingleShot(true);
    m_outlineTimer.setInterval(80);
    connect(script, &QPlainTextEdit::textChanged, &m_outlineTimer, QOverload<>::of(&QTimer::start));
    connect(&m_outlineTimer, &QTimer::timeout, this, [this] { refreshOutline(false); });

    // Emit modificationChanged to keep window in sync
    connect(script_doc, &QTextDocument::modificationChanged,
            this, &Editor::modificationChanged);

    // Emit undo / redo signals to keep window's menu in sync
    connect(script_doc, &QTextDocument::undoAvailable,
            this, [this](bool a){ if (scriptWidget() == script) emit undoAvailable(a); });
    connect(script_doc, &QTextDocument::redoAvailable,
            this, [this](bool a){ if (scriptWidget() == script) emit redoAvailable(a); });

    {   // The tabs.  The first holds the script, which is run and rendered; go to definition opens
        // the other files in further tabs, which are only there for editing
        m_tabs = new QTabBar;
        m_tabs->setTabsClosable(true);
        m_tabs->setMovable(false);
        m_tabs->setExpanding(false);
        m_tabs->setDrawBase(false);
        m_tabs->setElideMode(Qt::ElideMiddle);
        m_tabs->setFocusPolicy(Qt::NoFocus);
        m_tabs->addTab(T("Untitled"));
        m_tabs->setTabButton(0, QTabBar::RightSide, nullptr);    // (the script cannot be closed)
        m_tabs->setTabIcon(0, playTabIcon());       // (a play mark: this is the tab that is rendered)
        m_tabs->hide();         // (shown once a second file is open)

        auto page = new QWidget;
        auto pageLayout = new QVBoxLayout(page);
        pageLayout->setContentsMargins(0, 0, 0, 0);
        pageLayout->setSpacing(2);
        pageLayout->addWidget(script);
        pageLayout->addWidget(m_findBar);
        m_pages = new QStackedWidget;
        m_pages->addWidget(page);

        m_auxRow = new QWidget;
        m_auxRow->setObjectName("AuxRow");
        auto row = new QHBoxLayout(m_auxRow);
        row->setContentsMargins(8, 3, 8, 3);
        m_auxLabel = new QLabel;
        QFont smallFont = m_auxLabel->font();
        smallFont.setPointSizeF(8.0);
        m_auxLabel->setFont(smallFont);
        m_auxLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        m_auxLabel->setMinimumWidth(40);
        row->addWidget(m_auxLabel, 1);
        m_auxRow->hide();

        connect(m_tabs, &QTabBar::currentChanged, this, &Editor::onTabChanged);
        connect(m_tabs, &QTabBar::tabCloseRequested, this, [this](int i){ closeTab(i); });
    }
    // (the script with its tabs is the upper part of a splitter, the terminal the lower one: see below)
    auto top = new QWidget;
    auto topLayout = new QVBoxLayout(top);
    topLayout->setContentsMargins(0, 0, 0, 0);
    topLayout->setSpacing(2);
    topLayout->addWidget(m_tabs);
    topLayout->addWidget(m_pages, 1);
    topLayout->addWidget(m_auxRow);

    // Go to definition looks in the files the script imports; the text size is shared by the tabs
    script->setDefinitionResolver([this](const QString& name, const QString& owner) {
        return resolveDefinition(m_filePath, script->toPlainText(), name, owner);
    });
    script->setSupport([this](const QString& function, const QString& argument) { return callSupport(function, argument, nullptr); });
    connect(script, &Script::fontSizeChanged, this, [this](int size) {
        for (auto& a : m_aux) a->script->setFontSize(size);
    });
    updateTabs();

    {   // The buttons at the right of the bottom row: pause / continue, run again, terminate (the red dot)
        m_controls = new QWidget;
        m_controls->setObjectName("Controls");
        auto cl = new QHBoxLayout(m_controls);
        cl->setContentsMargins(4, 1, 6, 1);
        cl->setSpacing(1);
        auto make = [&](const QString& name) {
            auto b = new QToolButton;
            b->setObjectName(name);
            b->setAutoRaise(true);
            b->setFocusPolicy(Qt::NoFocus);
            b->setIconSize(QSize(16, 16));
            cl->addWidget(b);
            return b;
        };
        m_pauseButton = make("pauseButton");
        m_retryButton = make("retryButton");
        m_terminateButton = make("terminateButton");
        m_iconPause = glyphIcon(Glyph::Pause);
        m_iconPlay = glyphIcon(Glyph::Play);
        m_retryButton->setIcon(glyphIcon(Glyph::Retry));
        m_retryButton->setToolTip(T("Run the script again from the beginning (Ctrl+Shift+F5)"));
        m_terminateButton->setIcon(glyphIcon(Glyph::Terminate));
        m_terminateButton->setToolTip(T("Terminate the script (Shift+F5)"));
        connect(m_pauseButton, &QToolButton::clicked, this, &Editor::togglePause);
        connect(m_retryButton, &QToolButton::clicked, this, &Editor::retryRun);
        connect(m_terminateButton, &QToolButton::clicked, this, &Editor::terminateRun);
        updateControls();
    }

    {   // The script's progress (in place of the result line while it runs)
        m_runRow = new QWidget;
        auto row = new QHBoxLayout(m_runRow);
        row->setContentsMargins(6, 3, 6, 3);
        row->setSpacing(8);
        m_runBar = new QProgressBar;
        m_runBar->setTextVisible(false);
        m_runBar->setFixedSize(140, 5);
        m_runBar->setRange(0, 1000);
        m_runLabel = new QLabel;
        QFont smallFont = m_runLabel->font();
        smallFont.setPointSizeF(8.0);
        m_runLabel->setFont(smallFont);
        m_runLabel->setStyleSheet(runLabelStyle());
        // (its text is elided to the room it has: a long statement must not widen the editor)
        m_runLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        m_runLabel->setMinimumWidth(40);
        row->addWidget(m_runBar);
        row->addWidget(m_runLabel, 1);
        m_runRow->setStyleSheet(rowStyle());
        m_runRow->hide();

        // Under the script the terminal, in a splitter: how high it is, is the user's to say (and is remembered)
        m_split = new QSplitter(Qt::Vertical);
        m_split->setChildrenCollapsible(false);
        m_split->setHandleWidth(5);
        m_split->addWidget(top);
        m_split->addWidget(buildTerminal());
        m_split->setStretchFactor(0, 1);
        m_split->setStretchFactor(1, 0);
        connect(m_split, &QSplitter::splitterMoved, this, [this] {
            if (!m_terminalSized || qEnvironmentVariableIsSet("FIELDES_AUTOMATION")) return;        // (a scripted test never changes the user's settings)
            QSettings().setValue("terminal-height", m_split->sizes().value(1));
        });
        layout->addWidget(m_split, 1);
    }

    {   // Where a run stopped at a breakpoint waits to be continued
        m_pauseRow = new QWidget;
        auto row = new QHBoxLayout(m_pauseRow);
        row->setContentsMargins(6, 3, 6, 3);
        row->setSpacing(8);
        auto button = new QPushButton(T("Continue  %1").arg(QChar(0x25B6)));
        m_continueButton = button;
        button->setToolTip(T("Continue to the next breakpoint (F8)"));
        button->setFocusPolicy(Qt::NoFocus);
        m_pauseLabel = new QLabel;
        m_pauseLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        row->addWidget(button);
        row->addWidget(m_pauseLabel, 1);
        m_pauseRow->setStyleSheet(rowStyle());
        m_pauseRow->hide();
        layout->addWidget(m_pauseRow);
        connect(button, &QPushButton::clicked, this, &Editor::continueRun);
    }

    // A breakpoint set or removed runs the script again, up to the first one
    connect(script, &Script::breakpointsChanged,
            &m_textChangedDebounce, QOverload<>::of(&QTimer::start));
    // ... and is written down for the file, which finds it again when it is opened
    connect(script, &Script::breakpointsChanged, this, &Editor::saveBreakpoints);
    connect(script, &Script::foldToggled, this, &Editor::foldToggled);
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, &Editor::saveBreakpoints);
    // (a script that is paused when the program ends is let go: its thread waits on a flag that is about to be destroyed)
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, [] { libfive::run_progress::requestPause(false); });

    m_terminateRepeat.setInterval(1000);
    connect(&m_terminateRepeat, &QTimer::timeout, this, [this] {
        if (!m_terminating || !m_scriptRunning || !m_language || m_terminateSends >= 6)
        {
            m_terminateRepeat.stop();
            return;
        }
        ++m_terminateSends;
        m_language->terminate();
    });

    connect(script, &Script::libraryDefinitionRequested,
            this, &Editor::showSource);

    // A right-click on a line of the rendered script is a right-click on the model the line defines (the window opens the
    // viewport's menu for it); the other tabs (files that are only read) keep the text menu
    script->setObjectMenu(true);
    connect(script, &Script::objectMenuRequested, this, &Editor::objectMenuRequested);
    connect(script, &Script::userCursorMoved, this, &Editor::userCursorMoved);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    setLayout(layout);

    spinner.setInterval(150);
    connect(&spinner, &QTimer::timeout, this, &Editor::onSpinner);

    m_dragGuard.setInterval(4000);
    m_dragGuard.setSingleShot(true);
    connect(&m_dragGuard, &QTimer::timeout, this, [this] { if (m_dragActive) onDragEnd(); });
    m_textChangedDebounce.setInterval(250);
    m_textChangedDebounce.setSingleShot(true);
    connect(&m_textChangedDebounce, &QTimer::timeout,
            this, &Editor::onTextChangedDebounce);

    m_interpreterBusyDebounce.setInterval(100);
    m_interpreterBusyDebounce.setSingleShot(true);
    connect(&m_interpreterBusyDebounce, &QTimer::timeout,
            &spinner, QOverload<>::of(&QTimer::start));

    setLanguage(language == Language::LANGUAGE_NONE
            ? defaultLanguage()
            : language);
    loadDefaultScript();
}

void Editor::loadDefaultScript() {
    setScript(m_language->defaultScript());
    setModified(false);
}

void Editor::onInterpreterPartialResult(Result r)
{
    // What the run has shown so far, before a statement that takes long: drawn now, as anything is -- the viewport follows an edit (a part
    // hidden or shown) while an analysis runs.  The run's own result replaces it when the run is over (what is the same is not drawn again)
    if (m_discardResults || m_terminating)
    {
        for (auto s : r.shapes) s->deleteLater();
        return;
    }
    emit(partialShapes(r.shapes, r.keepFromLine));
    emit(settingsChanged(r.settings, first_change));
}

void Editor::onInterpreterBusy() {
    m_scriptRunning = true;
    m_interpreterBusyDebounce.start();
}

namespace {

// The kernel and the library say what they are doing in English ("optimising, iteration 11 of 100", "Reading part.step"): the bar shows it in the
// program's language.  A text that is not one of these is shown as it is
QString progressText(const QString& english)
{
    static const QHash<QString, QString> fixed = {
        {"setting up the solver", T("setting up the solver")},
        {"meshing the part", T("meshing the part")},
        {"resolving supports and loads", T("resolving supports and loads")},
        {"assembling the stiffness matrix", T("assembling the stiffness matrix")},
        {"preparing the solver", T("preparing the solver")},
        {"assembling the matrices", T("assembling the matrices")},
        {"finding the modes", T("finding the modes")},
        {"meshing the fluid", T("meshing the fluid")},
        {"resolving the boundary conditions", T("resolving the boundary conditions")},
        {"the start: the Stokes flow", T("the start: the Stokes flow")},
        {"assembling the conduction matrix", T("assembling the conduction matrix")},
        {"preparing the grid", T("preparing the grid")},
        {"preparing the FEA grid", T("preparing the FEA grid")},
        {"laying out the cells", T("laying out the cells")},
        {"meshing", T("meshing")},
        {"solving", T("solving")},
        {"solving the flow", T("solving the flow")},
        {"modal analysis", T("modal analysis")},
        {"thermal analysis", T("thermal analysis")},
        {"optimising", T("optimising")},
        {"optimising the body in the flow", T("optimising the body in the flow")},
        {"the flow in time", T("the flow in time")},
    };
    const auto it = fixed.constFind(english);
    if (it != fixed.constEnd()) return it.value();

    struct Rule { QRegularExpression pattern; QString text; };
    static const QList<Rule> rules = {
        {QRegularExpression("^solving, residual (\\S+) of (\\S+)$"), T("solving, residual %1 of %2")},
        {QRegularExpression("^solving: iteration (\\d+)$"), T("solving: iteration %1")},
        {QRegularExpression("^modal analysis, iteration (\\d+)$"), T("modal analysis, iteration %1")},
        {QRegularExpression("^optimising, iteration (\\d+) of (~?\\d+) \\((\\d+) taken back\\)$"), T("optimising, iteration %1 of %2 (%3 taken back)")},
        {QRegularExpression("^optimising, iteration (\\d+) of (~?\\d+)$"), T("optimising, iteration %1 of %2")},
        {QRegularExpression("^(Newton|Picard) iteration (\\d+), residual (\\S+)$"), T("%1 iteration %2, residual %3")},
        {QRegularExpression("^step (\\d+) of (\\d+), t = (\\S+) s$"), T("step %1 of %2, t = %3 s")},
        {QRegularExpression("^Reading (.+): (\\d+) of (\\d+) faces$"), T("Reading %1: %2 of %3 faces")},
        {QRegularExpression("^Reading (.+)$"), T("Reading %1")},
    };
    for (const Rule& r : rules)
    {
        const auto m = r.pattern.match(english);
        if (!m.hasMatch()) continue;
        QString out = r.text;
        for (int k = 1; k <= m.lastCapturedIndex(); ++k) out = out.arg(m.captured(k));
        return out;
    }

    // "Importing part.step: 3 of 12 solids rebuilt (+2 copies)"
    static const QRegularExpression importing("^Importing (.+): (\\d+) of (\\d+) solids rebuilt(?: \\(\\+(\\d+) cop(?:y|ies)\\))?$");
    const auto m = importing.match(english);
    if (m.hasMatch())
    {
        QString out = T("Importing %1: %2 of %3 solids rebuilt").arg(m.captured(1), m.captured(2), m.captured(3));
        if (!m.captured(4).isEmpty()) out += m.captured(4) == "1" ? T(" (+%1 copy)").arg(m.captured(4)) : T(" (+%1 copies)").arg(m.captured(4));
        return out;
    }
    return english;
}

}   // namespace

void Editor::onSpinner()
{
    // The script's progress, small, where the result goes.  The script runs
    // statement by statement ("2/7" and the statement's line: the runner
    // says which); the bar is how far the operation running in it has got,
    // as that operation counts it -- a STEP import's solids and their
    // stages, a cached import's tree files, an FEA grid's points, a solve's
    // residual, an optimisation's iterations, a top-level loop's items,
    // progress() calls.  Each operation has its own bar (0 -> 100 %); a
    // statement running none of them shows an empty one.
    if (!m_runRow->isVisible())
    {
        m_runClock.start();
        m_runShown = 0.0;
        m_runLogged = -1;
        m_runOperation = {-2, 0};
        m_runRow->show();
    }
    if (m_terminating)
    {
        // The termination is asked for and the script is still on its way out: a solver gives up at its next look at the flag, a
        // statement at its next line.  A step that cannot be interrupted finishes first
        const QString text = T("Terminating the script...");
        m_runLabel->setText(m_runLabel->fontMetrics().elidedText(text, Qt::ElideRight, std::max(60, m_runLabel->width())));
        m_runLabel->setToolTip(T("The script stops at the next point where it can. A step that cannot be interrupted (an import, a lattice layout, a meshing) finishes first."));
        return;
    }
    const double e = m_runClock.elapsed() / 1000.0;
    const auto r = libfive::run_progress::current();
    const auto p = libfive::step::importProgress();

    const std::pair<int, int> op{r.step, r.operation};
    if (op != m_runOperation)
    {
        m_runOperation = op;
        m_runShown = 0.0;
        m_runImportStage = -1;
    }
    double f = r.fraction;
    QString what = progressText(QString::fromStdString(r.detail));
    if (p.fraction >= 0)
    {
        if (r.fraction < 0)
        {
            // An import of its own: each of its stages (reading the file,
            // resolving its faces, rebuilding its solids) has its own bar
            if (p.stage != m_runImportStage)
            {
                m_runImportStage = p.stage;
                m_runShown = 0.0;
            }
            f = p.fraction;
        }
        else
        {
            // inside an operation (a loop's item): its rebuilding counts
            // within the share that operation gave it
            f = std::max(f, r.spanLo + (r.spanHi - r.spanLo) * (p.stage == 2 ? p.fraction : 0.0));
        }
        const QString t = progressText(QString::fromStdString(p.text));
        what = what.isEmpty() ? t : what + QString(" %1 ").arg(QChar(0x00b7)) + t;
    }
    if (f >= 0) m_runShown = std::max(m_runShown, std::min(1.0, f));

    // "2/7 . <the statement> . <what the operation does>": when it doesn't
    // fit, the statement is shortened (the counts at the end matter more)
    const QString dot = QString(" %1 ").arg(QChar(0x00b7));
    // (a pause that is asked for goes first in the text, where the eliding of a long statement does not reach it: the script goes on
    // to its next checkpoint, then waits there -- "Pausing..." until it has)
    QString state;
    if (m_pauseWanted) state = (libfive::run_progress::pausedNow() ? T("Paused") : T("Pausing...")) + dot;
    QString text, shown;
    if (r.step < 0)
    {
        text = shown = state + T("Starting the script");
    }
    else
    {
        const QString head = state + QString("%1/%2").arg(r.step + 1).arg(r.steps) + dot;
        const QString tail = what.isEmpty() ? QString() : dot + what;
        const QString label = QString::fromStdString(r.label);
        text = head + label + tail;
        const auto& fm = m_runLabel->fontMetrics();
        const int room = std::max(60, m_runLabel->width()) - fm.horizontalAdvance(head + tail);
        // (no room for the statement beside what the operation does: the statement goes, and what the operation does is kept to its
        // end -- where it is, "iteration 5 of 100" -- by eliding it in the middle)
        shown = room > fm.horizontalAdvance("mmmmmm")
            ? head + fm.elidedText(label, Qt::ElideRight, room) + tail
            : what.isEmpty()
                ? fm.elidedText(text, Qt::ElideRight, std::max(60, m_runLabel->width()))
                : head + fm.elidedText(what, Qt::ElideMiddle, std::max(40, m_runLabel->width() - fm.horizontalAdvance(head)));
    }
    m_runBar->setRange(0, 1000);
    m_runBar->setValue(int(1000 * m_runShown));
    if (text != m_runText)
    {
        m_runText = text;
        m_runLabel->setToolTip(text);
    }
    m_runLabel->setText(shown);
    if (qEnvironmentVariableIsSet("FIELDES_TIMING"))
    {
        std::cerr << "[bar-script] " << std::fixed << QDateTime::currentMSecsSinceEpoch() / 1000.0 << " "
                  << e << " s: step " << r.step << "/" << r.steps << " op " << r.operation
                  << "." << (p.fraction >= 0 ? p.stage : m_runImportStage) << " "
                  << int(100 * m_runShown) << " %, " << text.toStdString() << "\n";
    }
}

void Editor::refreshOutline(bool runDone)
{
    if (!m_language) return;
    QJsonObject request;
    request["source"] = script_doc->toPlainText();
    request["run_done"] = runDone;
    QString error;
    const QString json = callSupport("outline_json", QString::fromUtf8(QJsonDocument(request).toJson(QJsonDocument::Compact)), &error);
    if (json.isEmpty())
    {
        m_outlineTimer.start(400);          // (Python is not ready yet, or is busy with something that cannot wait: again in a moment)
        return;
    }
    emit(sceneChanged(json));
}

void Editor::onInterpreterDone(Result r)
{
    m_scriptRunning = false;
    if (qEnvironmentVariableIsSet("FIELDES_TIMING"))
        std::cerr << "[run] done: okay=" << r.okay << " replaced=" << r.replaced << " discard=" << m_discardResults
                  << " terminating=" << m_terminating << " shapes=" << r.shapes.size() << std::endl;
    // (the model tree is read from the text again once what is below has been set: what the run found out is laid over its rows)
    QTimer::singleShot(0, this, [this] { refreshOutline(true); emit(runFinished()); });
    // (a run that ended in an exception after the red dot was pressed was terminated by it; one that got to its end anyway shows its result)
    const bool terminated = m_terminating && !r.okay;
    const bool again = m_retryAfter;            // (Retry on a running script: the run that was in flight is over, the next one begins)
    m_terminating = false;
    m_retryAfter = false;
    m_terminateRepeat.stop();
    m_pauseWanted = false;
    libfive::run_progress::requestPause(false);
    m_runText.clear();
    updateControls();
    if (again) QTimer::singleShot(0, this, [this] { m_textChangedDebounce.start(); });
    if (m_runRow->isVisible() && qEnvironmentVariableIsSet("FIELDES_TIMING"))
        std::cerr << "[bar-script] " << std::fixed << QDateTime::currentMSecsSinceEpoch() / 1000.0
                  << " done after " << m_runClock.elapsed() / 1000.0 << " s" << std::endl;
    m_runRow->hide();
    m_interpreterBusyDebounce.stop();
    spinner.stop();

    // Error highlights live in their own selection layer
    QList<QTextEdit::ExtraSelection> selections;
    script->setErrorLine(-1);

    hidePause();
    if (m_discardResults)
    {
        // The result of a run of the script that has been replaced since: none of it is shown
        for (auto s : r.shapes) s->deleteLater();
        script->setSelections(Script::SEL_ERROR, selections);
        return;
    }
    if (r.replaced)
    {
        // An edit of the script stopped this run (the exception that ended it is the stop, not an error of the script) and the run of the
        // new text is on its way: nothing of this one is shown, and what the last finished run made stays until the next one is done
        for (auto s : r.shapes) s->deleteLater();
        script->setSelections(Script::SEL_ERROR, selections);
        return;
    }
    if (terminated)
    {
        // The exception that ended the run is the termination itself, not an error of the script: nothing of the run is shown (the
        // viewport keeps what the last run that finished made), and the result line says so (unless a new run is on its way)
        for (auto s : r.shapes) s->deleteLater();
        script->setSelections(Script::SEL_ERROR, selections);
        clearErrorNow();
        if (!again) setResult(Theme::muted, T("The script was terminated before it finished."));
        return;
    }
    if (r.okay) {
        if (r.pausedLine > 0) {
            // Stopped at a breakpoint, or before a statement that has a placeholder: what ran so far is shown as usual
            const bool hole = r.holeCount > 0;
            QString text = hole ? T("Line %1 is waiting for what goes in place of its placeholder (...): it, and what is made from it, did not run.").arg(r.pausedLine)
                                : T("Stopped before line %1 (a breakpoint)").arg(r.pausedLine);
            if (hole && !r.holeText.isEmpty()) {
                text += "\n" + r.holeText;
            }
            if (r.result != "None") {
                text += "\n" + r.result;
            }
            setResult(Theme::muted, text);
            showPause(r.pausedLine, hole);
        } else {
            setResult(Theme::text, r.result == "None" ? QString() : r.result);
        }

        // Store the textual position of variables, for later editing -- when this run was of the text that is in the editor now.  If the
        // text has been edited since the run began (a click of the tree, a key, the writes of a drag), the places that were followed
        // through those edits are the right ones, and the run's places are of another text (see varSpans)
        if (r.script == script_doc->toPlainText()) setVarSpans(r.vars);

        // (a number edited in the call of a shape that was dragged is the number it has: the list of numbers under the call follows it)
        if (!r.resync.isEmpty() && r.script == script_doc->toPlainText())
        {
            QList<TextEdit> follow;
            for (const auto& e : r.resync) follow << TextEdit{e.line0, e.col0, e.line1, e.col1, e.text};
            QTimer::singleShot(0, this, [this, follow] { applyEdits(follow, "Follow the number you changed"); });
        }

        m_errorLine0 = -1;
        emit(scriptErrorChanged(QString(), -1));       // (before the scene: the model tree's note is made from it)
        emit(shapes(r.shapes));
        emit(fieldSources(r.fields));
    } else {
        setResult(Color::red, r.error.error);
        {
            // (the last line of a traceback says what went wrong)
            QString last;
            for (const QString& l : r.error.error.split('\n'))
            {
                if (!l.trimmed().isEmpty()) last = l.trimmed();
            }
            if (last.size() > 220) last = last.left(217) + "...";
            emit(scriptErrorChanged(last.isEmpty() ? T("The script has an error") : last,
                                    r.error.range.isNull() ? -1 : r.error.range.top()));
            m_errorLine0 = r.error.range.isNull() ? -1 : r.error.range.top();
            m_errorLineText = m_errorLine0 >= 0 ? script_doc->findBlockByNumber(m_errorLine0).text() : QString();
        }

        // What ran before the error is drawn (its settings with it): the statements that were done made their shapes before the one that
        // failed.  (A script that did not parse ran nothing: the picture stays as it was)
        if (!r.shapes.isEmpty())
        {
            emit(shapes(r.shapes));
        }

        // Add new selections for errors in the script doc.  A null range
        // means "no location known"; a zero-width range marks a whole line
        // (e.g. a Python traceback, which only reports line numbers).
        if (!r.error.range.isNull())
        {
            QTextCursor c(script_doc);
            if (r.error.range.width() == 0)
            {
                const QTextBlock b = script_doc->findBlockByNumber(r.error.range.top());
                c = QTextCursor(b);
                // Skip leading indentation, then underline to end of line
                const QString t = b.text();
                int i = 0;
                while (i < t.size() && t[i].isSpace()) ++i;
                c.setPosition(b.position() + i);
                c.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
            }
            else
            {
                c.movePosition(QTextCursor::Down, QTextCursor::MoveAnchor, r.error.range.top());
                c.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, r.error.range.left());
                c.movePosition(QTextCursor::Down, QTextCursor::KeepAnchor, r.error.range.height());
                c.movePosition(QTextCursor::StartOfLine, QTextCursor::KeepAnchor);
                c.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, r.error.range.right());
            }

            QTextEdit::ExtraSelection s;
            s.cursor = c;
            s.format = error_format;
            selections.append(s);
            script->setErrorLine(r.error.range.top());
        }
    }

    // Set selections, which may include new errors if !r.okay
    script->setSelections(Script::SEL_ERROR, selections);

    // The warnings of the run are lines of the terminal and its Fix All button is in the header of the terminal; the ones of the run before are replaced
    m_warningTexts.clear();
    m_fixButton->disconnect(this);
    m_fixButton->hide();
    if (!r.warnings.isEmpty()) {
        QStringList fixes;

        for (auto& f : r.warnings) {
            m_warningTexts << f.first;
            if (!f.second.isEmpty()) {
                fixes.push_back(f.second);
            }
        }

        if (fixes.size()) {
            connect(m_fixButton, &QPushButton::pressed, this, [=](){
                // The import of the library goes to the top of the script; every other fix (the render settings) goes under
                // that line -- above it `view` is not there yet, and a settings line written before the model it is about
                // (`view.set_bounds(*roi(part))`) is an error -- and in the order they are listed
                static const QRegularExpression libraryImport(R"(^\s*(from\s+fieldes[\w.]*\s+import\b|import\s+fieldes\b))");
                QTextCursor c(script_doc);
                c.beginEditBlock();
                int at = -1;
                for (auto& f : fixes)
                {
                    if (libraryImport.match(f).hasMatch())
                    {
                        c.setPosition(0);
                        c.insertText(f);
                        at = c.position();
                        continue;
                    }
                    if (at < 0)
                    {
                        at = 0;
                        for (QTextBlock b = script_doc->begin(); b.isValid(); b = b.next())
                        {
                            if (!libraryImport.match(b.text()).hasMatch())
                            {
                                continue;
                            }
                            if (b.next().isValid())
                            {
                                at = b.next().position();
                            }
                            else
                            {
                                c.setPosition(b.position() + b.length() - 1);      // (the import is the last line)
                                c.insertText("\n");
                                at = c.position();
                            }
                            break;
                        }
                    }
                    c.setPosition(at);
                    c.insertText(f);
                    at = c.position();
                }
                c.endEditBlock();});
            m_fixButton->show();
        }
    }
    rebuildTerminal();

    // Announce the settings
    if (r.okay || !r.shapes.isEmpty()) {
        emit(settingsChanged(r.settings, first_change));
        first_change = false;
    }
}

void Editor::undo()
{
    // The lines that make a selected model ready to be dragged (its gizmo, its handles) are written by the program when the model is selected -- a model that was just
    // made, say.  They are not something the user did: undoing only them changes nothing that can be seen, and they are written again at once, because the model is still
    // selected -- so an undo went round in a circle and the model never went.  An undo that finds them on top takes the edit before them too: the last thing the user did
    QTextDocument* doc = scriptWidget()->document();
    const bool bookkeeping = m_readyDoc == doc && m_readyRevision >= 0 && doc->toPlainText() == m_readyText;
    const int steps = bookkeeping ? m_readyCount + 1 : 1;
    if (qEnvironmentVariableIsSet("FIELDES_TIMING"))
        std::cerr << "[undo] revision " << doc->revision() << " (the program's own edit was left at " << m_readyRevision << "), same text " << bookkeeping
                  << ", steps " << steps << std::endl;
    for (int i = 0; i < steps && doc->isUndoAvailable(); ++i) doc->undo();
    // (the redo that follows takes them back together)
    m_redoDoc = doc;
    m_redoRevision = doc->revision();
    m_redoSteps = steps;
}

void Editor::redo()
{
    // An undo that took the program's own edits with the user's brings them back together: one redo, as one undo took them
    QTextDocument* doc = scriptWidget()->document();
    const int steps = (m_redoDoc == doc && m_redoRevision >= 0 && doc->revision() == m_redoRevision) ? m_redoSteps : 1;
    for (int i = 0; i < steps && doc->isRedoAvailable(); ++i) doc->redo();
    m_redoRevision = -1;
    if (steps > 1)
    {
        // (and what is on top now is the program's own again: the next undo takes it with the user's edit below it)
        m_readyDoc = doc;
        m_readyRevision = doc->revision();
        m_readyText = doc->toPlainText();
        m_readyCount = steps - 1;
    }
}

////////////////////////////////////////////////////////////////////////////////

void Editor::setResult(QColor color, QString result)
{
    // (what is kept is which colour of the theme it is, so that a change of theme draws it in the colours of the new one)
    m_resultKind = color == Theme::muted ? 1 : color == Theme::text ? 2 : 0;
    m_resultColor = color;
    m_resultText = result;
    rebuildTerminal();
}

namespace {

// The file a script's breakpoints are kept for: its absolute path (a bundled example, or a script with no file, keeps none)
QString breakpointPath(const QString& path)
{
    if (path.isEmpty() || path.startsWith(":/")) return QString();
    QString p = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
#ifdef Q_OS_WIN
    p = p.toLower();
#endif
    return p;
}

QString breakpointKey(const QString& path)
{
    return "breakpoints/" + QString::fromLatin1(QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Md5).toHex());
}

// Only so many files are remembered: the ones not touched for longest go
void pruneBreakpoints(QSettings& settings)
{
    const int keep = 300;
    settings.beginGroup("breakpoints");
    const QStringList keys = settings.childKeys();
    if (keys.size() > keep)
    {
        QList<QPair<qint64, QString>> ages;
        for (const QString& k : keys)
        {
            const QJsonObject o = QJsonDocument::fromJson(settings.value(k).toString().toUtf8()).object();
            ages << qMakePair(qint64(o.value("saved").toDouble()), k);
        }
        std::sort(ages.begin(), ages.end());
        for (int i = 0; i < keys.size() - keep; ++i) settings.remove(ages[i].second);
    }
    settings.endGroup();
}

}   // namespace

void Editor::setScript(const QString& s, bool reload, const QString& path)
{
    first_change = !reload;
    saveBreakpoints();      // (of the file that is left: its lines, and the text on them)
    if (!reload)
    {
        closeAuxiliaryTabs();      // (the files opened from the old script)
        // Another script: what the old one made (its viewport shapes, its model tree, its output, its error) goes now,
        // and what a run of it that is still going delivers is thrown away.  Left until the new script has run, a file
        // that is slow, or does not run, would look as if opening it had done nothing
        m_discardResults = true;
        callSupport("forget_literals", QString(), nullptr);       // (what the last script's runs saw in the calls says nothing about this one)
        varSpans.clear();
        setResult(Theme::text, QString());
        m_errorLine0 = -1;
        emit(scriptErrorChanged(QString(), -1));
        script->setSelections(Script::SEL_ERROR, QList<QTextEdit::ExtraSelection>());
        script->setErrorLine(-1);
        emit(shapes(QList<Shape*>()));
        emit(documentReplaced());
    }
    m_breakpointsFrozen = true;
    script->clearBreakpoints();     // (their lines mean nothing in new text)
    script->setPlainText(s);
    m_breakpointsFrozen = false;
    // ... the ones the file had when it was last open come back, on the lines that still hold the text they were set on
    m_breakpointPath = breakpointPath(path.isEmpty() && reload ? m_filePath : path);
    restoreBreakpoints();
    if (!reload) emit(scriptLoaded());
}

void Editor::saveBreakpoints()
{
    if (m_breakpointsFrozen || m_breakpointPath.isEmpty() || !script) return;
    QSettings settings;
    const QString key = breakpointKey(m_breakpointPath);
    const QList<int> lines = script->breakpoints();
    if (lines.isEmpty())
    {
        if (settings.contains(key)) settings.remove(key);
        m_breakpointsSaved.clear();
        return;
    }
    QJsonArray points;
    for (const int line : lines)
        points.append(QJsonArray{line, script_doc->findBlockByNumber(line - 1).text().trimmed()});
    QJsonObject o;
    o["path"] = m_breakpointPath;
    o["points"] = points;
    const QByteArray same = QJsonDocument(o).toJson(QJsonDocument::Compact);
    if (same == m_breakpointsSaved) return;
    m_breakpointsSaved = same;
    o["saved"] = double(QDateTime::currentSecsSinceEpoch());
    const bool isNew = !settings.contains(key);
    settings.setValue(key, QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)));
    if (isNew) pruneBreakpoints(settings);
}

void Editor::restoreBreakpoints()
{
    m_breakpointsSaved.clear();
    if (m_breakpointPath.isEmpty()) return;
    QSettings settings;
    const QString text = settings.value(breakpointKey(m_breakpointPath)).toString();
    if (text.isEmpty()) return;
    const QJsonArray points = QJsonDocument::fromJson(text.toUtf8()).object().value("points").toArray();
    const int n = script_doc->blockCount();
    auto textAt = [&](int line) { return script_doc->findBlockByNumber(line - 1).text().trimmed(); };
    QList<int> lines;
    for (const QJsonValue& v : points)
    {
        const QJsonArray p = v.toArray();
        if (p.size() < 2) continue;
        const int want = p[0].toInt();
        const QString t = p[1].toString();
        // The line it was on if that still holds the text; otherwise the nearest line (within 30) that does -- the file was changed
        // since -- otherwise it is gone
        int found = 0;
        if (want >= 1 && want <= n && textAt(want) == t) found = want;
        else if (!t.isEmpty())
        {
            for (int d = 1; d <= 30 && !found; ++d)
            {
                if (want - d >= 1 && want - d <= n && textAt(want - d) == t) found = want - d;
                else if (want + d >= 1 && want + d <= n && textAt(want + d) == t) found = want + d;
            }
        }
        if (found && !lines.contains(found)) lines << found;
    }
    if (lines.isEmpty()) return;
    m_breakpointsFrozen = true;
    script->setBreakpoints(lines);      // (this also runs the script again, up to the first of them)
    m_breakpointsFrozen = false;
    saveBreakpoints();                  // (on the lines they have now; and the file is marked as used lately)
}

void Editor::onInterpreterPartialScene(QString json)
{
    if (m_scriptRunning && !m_discardResults)
    {
        // An error is shown from the run before: when this run has got past the line it was on (the statements up to `done_line`
        // are done, none of them failed), it is not an error any more -- and the rest of the run, which may take a while, is no
        // reason to go on showing it
        if (m_errorLine0 >= 0)
        {
            const auto doc = QJsonDocument::fromJson(json.toUtf8());
            if (doc.isObject() && !doc.object()["errored"].toBool() && doc.object()["done_line"].toInt() > m_errorLine0)
                clearErrorNow();
        }
        m_outlineTimer.start();         // (a statement is done: what it found out is laid over the tree's rows)
    }
}

void Editor::clearErrorNow()
{
    m_errorLine0 = -1;
    m_errorLineText.clear();
    script->setSelections(Script::SEL_ERROR, QList<QTextEdit::ExtraSelection>());
    script->setErrorLine(-1);
    emit(scriptErrorChanged(QString(), -1));
}

QString Editor::getScript() const
{
    return script->toPlainText();
}

void Editor::setModified(bool m)
{
    script_doc->setModified(m);
    script_doc->modificationChanged(m);
}

void Editor::onTextChangedDebounce()
{
    if (m_dragActive)
    {
        m_textChangedDuringDrag = true;             // (a drag is writing numbers: the run waits for its end, see onDragEnd)
        return;
    }
    auto txt = script_doc->toPlainText();
    m_pauseWanted = false;          // (a run that is asked for takes the place of the one that waits: it is let go, to end)
    updateControls();
    saveBreakpoints();      // (the lines they are on have moved with the text)
    // The line the error was on is not in the script any more (it was rewritten): the error is gone from the screen at once, not
    // when the run of the new text ends.  If the new text fails again, the error comes back as an error does (after a moment)
    if (m_errorLine0 >= 0 && !m_errorLineText.trimmed().isEmpty() && !txt.contains(m_errorLineText))
        clearErrorNow();
    m_language->setBreakpoints(script->breakpoints());
    emit(scriptChanged(txt));
}

void Editor::showPause(int line, bool hole)
{
    m_pausedLine = line;
    m_holePause = hole;
    m_pauseLabel->setText(hole ? T("Line %1 waits: write what goes in place of the ... to run it").arg(line)
                               : T("Stopped before line %1").arg(line));
    m_continueButton->setVisible(!hole);
    m_pauseRow->show();
    script->setPausedLine(line - 1);
    updateControls();
}

void Editor::hidePause()
{
    m_pausedLine = -1;
    m_holePause = false;
    m_pauseRow->hide();
    script->setPausedLine(-1);
    updateControls();
}

void Editor::updateControls()
{
    if (!m_pauseButton) return;
    const bool atBreakpoint = m_pausedLine > 0 && !m_holePause;      // (a placeholder is not continued: it is filled in)
    // (the first button offers to continue when the script waits: at a breakpoint, or at a pause that was asked for)
    m_pauseButton->setIcon(atBreakpoint || m_pauseWanted ? m_iconPlay : m_iconPause);
    m_pauseButton->setEnabled(atBreakpoint || (m_scriptRunning && !m_terminating));
    m_pauseButton->setToolTip(atBreakpoint ? T("Continue to the next breakpoint (F8)")
                              : m_pauseWanted ? T("Continue the script (F6)")
                                              : T("Pause the script (F6)"));
    m_terminateButton->setEnabled(m_scriptRunning && !m_terminating);
}

void Editor::togglePause()
{
    if (m_pausedLine > 0)
    {
        continueRun();      // (does nothing at a placeholder)
        return;
    }
    if (!m_scriptRunning || m_terminating)
    {
        return;
    }
    m_pauseWanted = !m_pauseWanted;
    libfive::run_progress::requestPause(m_pauseWanted);
    updateControls();
    if (m_runRow->isVisible()) onSpinner();
}

void Editor::retryRun()
{
    if (m_scriptRunning)
    {
        // The run in flight is ended first; the next one begins when it is over (onInterpreterDone)
        m_retryAfter = true;
        terminateRun();
        return;
    }
    m_textChangedDebounce.start();
}

void Editor::terminateRun()
{
    if (!m_scriptRunning || m_terminating || !m_language)
    {
        return;
    }
    m_terminating = true;
    m_pauseWanted = false;
    m_terminateSends = 1;
    m_language->terminate();
    m_terminateRepeat.start();
    updateControls();
    if (m_runRow->isVisible()) onSpinner();
}

void Editor::continueRun()
{
    if (m_pausedLine < 0 || m_holePause || !m_language)
    {
        return;
    }
    hidePause();
    m_language->resume();
}

void Editor::onDragStart()
{
    // (a script that is disabled loses the keyboard: it is given back after the drag only if it had it)
    m_scriptHadFocus = script->hasFocus();
    script->setEnabled(false);
    drag_should_join = false;
    m_dragActive = true;
    m_textChangedDuringDrag = false;
    if (m_scriptRunning) m_discardResults = true;       // (what a run that began before the drag delivers would put its older numbers back over the dragged ones)
    m_dragGuard.start();
}

void Editor::onDragEnd()
{
    script->setEnabled(true);
    if (m_scriptHadFocus) script->setFocus();
    m_scriptHadFocus = false;
    m_dragGuard.stop();
    m_dragActive = false;
    if (m_textChangedDuringDrag)
    {
        m_textChangedDuringDrag = false;
        // (the one run of what the drag wrote: at once -- the 250 ms that wait for typing to pause are not for a gesture that has ended)
        QTimer::singleShot(0, this, [this] { if (!m_dragActive) onTextChangedDebounce(); });
    }
}

void Editor::setVarValues(QMap<libfive::Tree::Id, float> vs)
{
    if (m_dragActive) m_dragGuard.start();          // (the drag goes on: it is not left to the guard)
    // Temporarily enable the script so that we can edit the variable value
    script->setEnabled(true);
    QTextCursor drag_cursor(script_doc);
    if (drag_should_join)
    {
        drag_cursor.joinPreviousEditBlock();
    }
    else
    {
        drag_cursor.beginEditBlock();

        // There's a Qt bug where if you have a multiline selection,
        // editing the text within that selection causes it to be
        // rendered strangely until the pane is resized.  To work around this,
        // we clear selections when beginning a drag.
        auto cursor = script->textCursor();
        if (cursor.hasSelection())
        {
            cursor.clearSelection();
            script->setTextCursor(cursor);
        }
    }
    drag_should_join = true;

    // Where a number is NOW is where the cursor of its place is: every edit of the text since the run that said where it was -- the
    // writes of this drag, a click of the tree that put a line above, a key -- has moved it along (see varSpans).  So there is no
    // arithmetic of columns here, and no order to walk them in.  What stands at the place is checked to be what is expected before it
    // is written over: a number that is not there (its text was edited by hand, the script was replaced since the run) is not
    // written, anywhere -- the places of the others are not affected -- and the script is run again, which says where the numbers are
    bool lost = false;
    for (auto v = vs.begin(); v != vs.end(); ++v)
    {
        auto it = varSpans.find(v.key());
        if (it == varSpans.end()) continue;                 // (a number the editor has no place for: the text it is in has changed since the run)
        VarSpan& span = it.value();
        const int a = span.start.position(), b = span.end.position();
        QTextCursor look(script_doc);
        look.setPosition(a);
        look.setPosition(b, QTextCursor::KeepAnchor);
        if (b <= a || look.selectedText() != span.text || script_doc->characterAt(b) != QLatin1Char(')'))
        {
            varSpans.erase(it);
            lost = true;
            continue;
        }

        drag_cursor.setPosition(a);
        drag_cursor.setPosition(b, QTextCursor::KeepAnchor);
        drag_cursor.removeSelectedText();

        QString str;
        str.setNum(v.value());
        drag_cursor.insertText(str);

        // (the edit moved the cursors of the numbers to the right of this one by itself; this one's own are put back round what is
        // written: an insertion at a cursor's place is put before the cursor)
        span.start.setPosition(a);
        span.end.setPosition(a + str.size());
        span.text = str;
    }

    // Disable the script again (because this is only called when we're
    // doing a drag operation in the 3D viewport, and the script should
    // be locked).
    drag_cursor.endEditBlock();
    script->setEnabled(false);
    if (lost) m_textChangedDebounce.start();
}

void Editor::setVarSpans(const QMap<libfive::Tree::Id, QRect>& rects)
{
    varSpans.clear();
    // (Python's columns are bytes of UTF-8, the document's are characters: a line with an accent before a number is told apart)
    QHash<int, QPair<bool, QByteArray>> lines;
    auto charColumn = [&](int line, const QString& text, int byteColumn) {
        auto found = lines.find(line);
        if (found == lines.end())
        {
            bool ascii = true;
            for (const QChar c : text) ascii = ascii && c.unicode() < 128;
            found = lines.insert(line, qMakePair(ascii, ascii ? QByteArray() : text.toUtf8()));
        }
        return found->first ? byteColumn : QString::fromUtf8(found->second.left(byteColumn)).size();
    };
    for (auto it = rects.begin(); it != rects.end(); ++it)
    {
        const QRect& r = it.value();
        if (r.height() != 0) continue;                      // (a call over several lines: its number is not written to)
        const QTextBlock block = script_doc->findBlockByNumber(r.top());
        if (!block.isValid()) continue;
        const QString text = block.text();
        // (the rectangle goes from the first character of the number to the `)` that ends the call, which is not part of the number)
        const int first = charColumn(r.top(), text, r.left());
        const int last = charColumn(r.top(), text, r.left() + r.width() - 1);
        if (first < 0 || last <= first || last >= text.size() || text[last] != QLatin1Char(')')) continue;
        VarSpan span{QTextCursor(script_doc), QTextCursor(script_doc), text.mid(first, last - first)};
        span.start.setPosition(block.position() + first);
        span.end.setPosition(block.position() + last);
        varSpans.insert(it.key(), span);
    }
}

int Editor::testVarSteps(int line0)
{
    QList<QPair<int, libfive::Tree::Id>> onLine;           // (the column, the number)
    for (auto it = varSpans.begin(); it != varSpans.end(); ++it)
    {
        if (it.value().start.blockNumber() == line0) onLine << qMakePair(it.value().start.positionInBlock(), it.key());
    }
    std::sort(onLine.begin(), onLine.end(), [](const QPair<int, libfive::Tree::Id>& a, const QPair<int, libfive::Tree::Id>& b) {
        return a.first < b.first;
    });
    auto step = [&](const QList<int>& which, float base) {
        QMap<libfive::Tree::Id, float> m;
        for (int k : which)
        {
            if (k < onLine.size()) m[onLine[k].second] = base + 1.2345678f * float(k);
        }
        if (!m.isEmpty()) setVarValues(m);
    };
    step({0}, 123.456f);
    step({2}, -987.654f);
    step({1, 3}, 55.5f);
    step({0, 3}, 7.7777f);
    script->setEnabled(true);
    return int(onLine.size());
}

void Editor::applyEdits(QList<TextEdit> edits, QString description)
{
    applyEditsNoted(edits, description, false);
}

void Editor::applyEditsLive(QList<TextEdit> edits, QString description)
{
    applyEditsNoted(edits, description, true);
}

// The edits that the program makes by itself, after what the user did, to go with it -- the lines that make a selected model ready to be dragged (its gizmo, its handles) and the
// render region that follows a new model -- are not steps of their own for undo and redo: one undo takes them with the user's edit before them, one redo brings them back
void Editor::applyEditsNoted(QList<TextEdit> edits, const QString& description, bool live)
{
    const bool own = (description.startsWith("Make ") && description.contains(" draggable")) || description == "Grow the region";
    // (a live edit that joins the one before it is no step of its own at all)
    const bool joined = live && m_liveRevision >= 0 && m_liveRevision == script_doc->revision();
    const bool chain = m_readyDoc == script_doc && m_readyRevision >= 0 && script_doc->toPlainText() == m_readyText;
    applyEditsAs(edits, live);
    // What the guided tour writes -- the model of a step, a line it hides or takes out -- is not a step of the user's: an undo that took it would take the model of the step away (the user, 2026-10-27, of the
    // step "Snap one body onto another": "when I press ctrl+z then it deletes the cylinder"), and an undo that finds the program's own lines on top takes the edit before them too, which there is the tour's
    if (description == "Tour") script_doc->clearUndoRedoStacks();
    if (joined)
    {
        if (chain)
        {
            m_readyRevision = script_doc->revision();
            m_readyText = script_doc->toPlainText();
        }
        return;
    }
    if (own)
    {
        m_readyCount = chain ? m_readyCount + 1 : 1;
        m_readyDoc = script_doc;
        m_readyRevision = script_doc->revision();
        m_readyText = script_doc->toPlainText();
    }
    else
    {
        m_readyDoc.clear();
        m_readyRevision = -1;
        m_readyText.clear();
        m_readyCount = 0;
    }
    if (qEnvironmentVariableIsSet("FIELDES_TIMING"))
        std::cerr << "[edit] " << description.toStdString() << " (revision " << script_doc->revision() << ", " << m_readyCount << " of the program's own)" << std::endl;
}

void Editor::applyEditsAs(QList<TextEdit> edits, bool live)
{
    if (edits.isEmpty())
    {
        return;
    }
    showScriptTab();
    // Apply from the end of the document backwards so earlier positions
    // stay valid
    std::sort(edits.begin(), edits.end(), [](const TextEdit& a, const TextEdit& b) {
        return (a.line0 != b.line0) ? (a.line0 > b.line0) : (a.col0 > b.col0);
    });
    auto pos = [&](int line, int col) {
        QTextBlock b = script_doc->findBlockByNumber(line);
        if (!b.isValid())
        {
            return script_doc->characterCount() - 1;
        }
        return b.position() + std::min(col, b.length() - 1);
    };
    const bool wasEnabled = script->isEnabled();
    script->setEnabled(true);
    QTextCursor c(script_doc);
    // (a live edit joins the one before it when the script is as that one left it)
    if (live && m_liveRevision >= 0 && m_liveRevision == script_doc->revision()) c.joinPreviousEditBlock();
    else c.beginEditBlock();
    for (const auto& e : edits)
    {
        c.setPosition(pos(e.line0, e.col0));
        c.setPosition(pos(e.line1, e.col1), QTextCursor::KeepAnchor);
        c.insertText(e.text);
    }
    c.endEditBlock();
    m_liveRevision = live ? script_doc->revision() : -1;
    script->setEnabled(wasEnabled);
    // An edit made by the program (the tree, a menu) is one decision, not typing that goes on: the script runs at once, without
    // the quarter of a second that waits for the next key
    m_textChangedDebounce.stop();
    onTextChangedDebounce();
}

void Editor::goToLine(int line0)
{
    showScriptTab();
    script->goToLine(line0);
}

void Editor::placeCursor(int line0, bool atEnd)
{
    showScriptTab();
    if (atEnd)
    {
        script->moveCursor(QTextCursor::End);
        script->ensureCursorVisible();
    }
    else
        script->goToLine(line0);
    script->setFocus();
}

void Editor::goToPlaceholder(int line0, int col0)
{
    showScriptTab();
    script->selectRange(line0, col0, 3);
}

void Editor::setFold(int line0, bool folded)
{
    int end = 0;
    if (line0 < 0 || !script->foldRange(line0, &end)) return;       // (a line with nothing under it to fold)
    // (the lines the user is typing in are not hidden, whoever asks -- a row of the tree that closes, a tour step that shows only what it is about: the text cursor would be
    // taken out of them, to the line the call begins on, and what is typed next would go there, where no argument can be written)
    if (folded && writingIn(line0)) return;
    if (script->isFolded(line0) != folded) script->setFolded(line0, folded);
}

bool Editor::writingIn(int line0) const
{
    int end = 0;
    if (line0 < 0 || !script->hasFocus() || !script->foldRange(line0, &end)) return false;
    const int at = script->textCursor().blockNumber();
    return at > line0 && at <= end;
}

void Editor::goToSpan(int line0, int col0, int endLine0, int endCol0)
{
    showScriptTab();
    script->selectRange(line0, col0, endLine0, endCol0);
    script->setFocus();
}

void Editor::onSyntaxReady() {
    m_outlineTimer.start();
    script_doc->contentsChange(0, 0, script_doc->toPlainText().length());
}

Language::Type Editor::defaultLanguage() {
    return Language::LANGUAGE_PYTHON;
}

bool Editor::supportsLanguage(Language::Type t) {
    return t == Language::LANGUAGE_PYTHON;
}

void Editor::guessLanguage(QString ext) {
    if (ext == "py") {
        setLanguage(Language::LANGUAGE_PYTHON);
    }
}

QString Editor::getExtension() const {
    return m_language->extension();
}

QString Editor::callSupport(const QString& function, const QString& arg, QString* error)
{
    if (!m_language)
    {
        if (error) *error = "no language is loaded";
        return QString();
    }
    return m_language->callSupport(function, arg, error);
}

Language::Type Editor::getLanguage() const {
    return m_language->type();
}

void Editor::setLanguage(Language::Type t) {
    const auto prev_type = m_language ? m_language->type()
                                      : Language::LANGUAGE_NONE;
    bool changed = false;

    switch (t) {
        case Language::LANGUAGE_PYTHON:
            if (prev_type != Language::LANGUAGE_PYTHON) {
                m_language.reset(FielDes::Python::language(script));
                changed = true;
            }
            break;
        default: return;
    }

    if (changed) {
        emit languageChanged();

        connect(script, &QPlainTextEdit::cursorPositionChanged,
                m_language.data(), [&](){ m_language->onCursorMoved(script); });
        connect(m_language.data(), &Language::interpreterBusy,
                &m_interpreterBusyDebounce, QOverload<>::of(&QTimer::start));
        connect(m_language.data(), &Language::interpreterBusy,
                this, [&]() { m_scriptRunning = true; m_discardResults = false; hidePause(); updateControls(); });
        connect(m_language.data(), &Language::interpreterDone,
                this, &Editor::onInterpreterDone);
        connect(m_language.data(), &Language::interpreterPartialScene,
                this, &Editor::onInterpreterPartialScene);
        connect(m_language.data(), &Language::interpreterPartialResult,
                this, &Editor::onInterpreterPartialResult);
        connect(m_language.data(), &Language::syntaxReady,
                this, &Editor::onSyntaxReady);

        connect(this, &Editor::scriptChanged,
                m_language.data(), &Language::onScriptChanged);
        connect(this, &Editor::onShowDocs,
                m_language.data(), &Language::onShowDocs);
        connect(m_language.data(), &Language::languageReady,
                this, &Editor::onLanguageReady);
    }
}

void Editor::onLanguageReady(QStringList keywords, Documentation docs)
{
    m_keywords = keywords;

    QStringList words = keywords;
    QMap<QString, QString> tips;
    for (auto mod = docs.begin(); mod != docs.end(); ++mod)
    {
        if (mod.key().startsWith("__"))
        {
            continue;
        }
        for (auto f = mod.value().begin(); f != mod.value().end(); ++f)
        {
            words << f.key();
            // Docstrings start with "name(signature)"; use that line
            tips[f.key()] = f.value().section('\n', 0, 0);
        }
    }
    const auto extraTips = docs.value("__tips__");
    for (auto t = extraTips.begin(); t != extraTips.end(); ++t)
    {
        tips[t.key()] = t.value();
    }
    words << "view";
    script->setCompletionWords(words);
    script->setCallTips(tips);

    const auto members = docs.value("__members__");
    for (auto m = members.begin(); m != members.end(); ++m)
    {
        script->setMemberWords(m.key(), m.value().split(' ', Qt::SkipEmptyParts));
    }
    script->setLibraryDefinitions(docs.value("__defs__"));
}

void Editor::showSource(QString name, QString file, int line)
{
    openFile(file, line - 1, name);
}

////////////////////////////////////////////////////////////////////////////////
// Tabs

Script* Editor::tabScript(int index) const
{
    if (index <= 0) return script;
    return index - 1 < int(m_aux.size()) ? m_aux[index - 1]->script : script;
}

Script* Editor::scriptWidget() const
{
    return tabScript(m_tabs ? m_tabs->currentIndex() : 0);
}

FindBar* Editor::findBar() const
{
    const int i = m_tabs ? m_tabs->currentIndex() : 0;
    return (i > 0 && i - 1 < int(m_aux.size())) ? m_aux[i - 1]->find : m_findBar;
}

static bool isLibraryFile(const QString& path)
{
    return QDir::fromNativeSeparators(path).contains("/python/fieldes/", Qt::CaseInsensitive);
}

void Editor::setFilePath(const QString& path)
{
    m_filePath = (path.isEmpty() || path.startsWith(":/")) ? path : QFileInfo(path).absoluteFilePath();
    // A file that was loaded has had its breakpoints brought back already (setScript was told its path).  Another name for the script
    // that is here (Save as) takes the ones that are set along
    const QString key = breakpointPath(m_filePath);
    if (key != m_breakpointPath)
    {
        saveBreakpoints();                  // (for the name it had until now)
        m_breakpointPath = key;
        m_breakpointsSaved.clear();
        if (script->breakpoints().isEmpty()) restoreBreakpoints();
        else saveBreakpoints();
    }
    updateTabs();
}

void Editor::updateTabs()
{
    const QString name = m_filePath.isEmpty() ? T("Untitled") : QFileInfo(m_filePath).fileName();
    m_tabs->setTabText(0, name);
    m_tabs->setTabToolTip(0, T("Rendered: the viewport shows what this script makes.\n"
                             "The other tabs are files opened for editing only."));
    m_tabs->setVisible(!m_aux.empty());
}

void Editor::updateAuxTitle(const Aux& a)
{
    for (size_t i = 0; i < m_aux.size(); ++i)
    {
        if (m_aux[i].get() != &a) continue;
        const QString name = QFileInfo(a.path).fileName() +
            (a.script->document()->isModified() ? QString(" %1").arg(QChar(0x2022)) : QString());
        m_tabs->setTabText(int(i) + 1, name);
        m_tabs->setTabToolTip(int(i) + 1, QDir::toNativeSeparators(a.path) + "\n" +
            T("Editing only: this file is not rendered.") +
            (isLibraryFile(a.path) ? "\n" + T("Part of FielDes's library: changes apply after FielDes is restarted.")
                                   : "\n" + T("Saving it runs the script again.")));
    }
}

void Editor::onTabChanged(int index)
{
    if (index < 0) return;
    m_pages->setCurrentIndex(index);
    const bool other = index > 0 && index - 1 < int(m_aux.size());
    if (other)
    {
        const Aux& a = *m_aux[index - 1];
        const QString where = QDir::toNativeSeparators(a.path);
        m_auxLabel->setText(T("Editing only: this file is not rendered.") + " " +
            (isLibraryFile(a.path) ? T("Changes to the library apply after FielDes is restarted.")
                                   : T("Saving it runs the script again.")));
        m_auxLabel->setToolTip(where);
    }
    m_auxRow->setVisible(other);
    auto doc = scriptWidget()->document();
    emit undoAvailable(doc->isUndoAvailable());
    emit redoAvailable(doc->isRedoAvailable());
    // (the user's own click on a tab or key moves the keyboard into it; the program's switch, to show where a
    // selected model is defined, does not)
    if (!m_quietTab) scriptWidget()->setFocus();
}

QString Editor::resolveDefinition(const QString& path, const QString& text,
                                  const QString& name, const QString& owner)
{
    // (a script from the application's resources has no folder to look in)
    const QString file = path.startsWith(":/") ? QString() : QDir::toNativeSeparators(path);
    const QChar sep(0x1f);
    return callSupport("find_definition", name + sep + owner + sep + file + sep + text, nullptr);
}

void Editor::openFile(const QString& file, int line0, const QString& name)
{
    QPointer<Script> target;
    auto reveal = [&](Script* s) {
        target = s;
        // (after the page has been laid out, so that the line is centred)
        QTimer::singleShot(0, s, [=]() {
            if (name.isEmpty()) s->goToLine(line0); else s->revealName(line0, name);
            s->setFocus();
        });
    };

    // This script itself, or a file that has a tab already
    if (!m_filePath.isEmpty() && !m_filePath.startsWith(":/") && QFileInfo(file) == QFileInfo(m_filePath))
    {
        m_tabs->setCurrentIndex(0);
        reveal(script);
        return;
    }
    for (size_t i = 0; i < m_aux.size(); ++i)
    {
        if (QFileInfo(file) == QFileInfo(m_aux[i]->path))
        {
            m_tabs->setCurrentIndex(int(i) + 1);
            reveal(m_aux[i]->script);
            return;
        }
    }

    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
    {
        emit notice(T("Cannot open %1").arg(QDir::toNativeSeparators(file)));
        return;
    }
    const QByteArray data = f.readAll();

    auto aux = std::make_unique<Aux>();
    aux->path = QFileInfo(file).absoluteFilePath();
    aux->crlf = data.contains("\r\n");
    aux->page = new QWidget;
    auto pageLayout = new QVBoxLayout(aux->page);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(2);
    aux->script = new Script(aux->page);
    aux->script->setObjectName("Aux");
    aux->script->setFrameShape(QFrame::NoFrame);
    aux->script->setLineWrapMode(QPlainTextEdit::NoWrap);
    aux->script->document()->setDefaultFont(script_doc->defaultFont());
    aux->script->setTabStopDistance(script->tabStopDistance());
    aux->script->copyVocabularyFrom(*script);
    aux->formatter = std::make_unique<FielDes::Python::Formatter>();
    aux->script->bind(aux->formatter.get());
    auto syntax = new FielDes::Python::Syntax(aux->script->document());
    syntax->setKeywords(m_keywords);
    aux->find = new FindBar(aux->script, aux->page);
    pageLayout->addWidget(aux->script);
    pageLayout->addWidget(aux->find);

    QString text = QString::fromUtf8(data);
    if (aux->crlf) text.replace("\r\n", "\n");
    aux->script->setPlainText(text);
    aux->script->document()->setModified(false);

    Aux* a = aux.get();
    a->script->setDefinitionResolver([this, a](const QString& n, const QString& o) {
        return resolveDefinition(a->path, a->script->toPlainText(), n, o);
    });
    connect(a->script, &Script::libraryDefinitionRequested, this, &Editor::showSource);
    connect(a->script, &Script::fontSizeChanged, this, [this, a](int size) {
        script->setFontSize(size);
        for (auto& other : m_aux) if (other.get() != a) other->script->setFontSize(size);
    });
    connect(a->script->document(), &QTextDocument::modificationChanged, a->page,
            [this, a](bool) { updateAuxTitle(*a); });
    connect(a->script->document(), &QTextDocument::undoAvailable, a->page,
            [this, a](bool v) { if (scriptWidget() == a->script) emit undoAvailable(v); });
    connect(a->script->document(), &QTextDocument::redoAvailable, a->page,
            [this, a](bool v) { if (scriptWidget() == a->script) emit redoAvailable(v); });

    m_pages->addWidget(a->page);
    m_aux.push_back(std::move(aux));
    const int index = int(m_aux.size());
    m_tabs->addTab(QFileInfo(file).fileName());
    updateAuxTitle(*a);
    updateTabs();
    m_tabs->setCurrentIndex(index);
    reveal(a->script);
}

void Editor::showScriptTab()
{
    if (m_tabs->currentIndex() != 0)
    {
        m_quietTab = true;
        m_tabs->setCurrentIndex(0);
        m_quietTab = false;
    }
}

void Editor::nextTab()
{
    if (m_tabs->count() > 1) m_tabs->setCurrentIndex((m_tabs->currentIndex() + 1) % m_tabs->count());
}

void Editor::previousTab()
{
    if (m_tabs->count() > 1)
        m_tabs->setCurrentIndex((m_tabs->currentIndex() + m_tabs->count() - 1) % m_tabs->count());
}

bool Editor::closeCurrentTab()
{
    return m_tabs->currentIndex() > 0 && closeTab(m_tabs->currentIndex());
}

bool Editor::closeTab(int index)
{
    if (index <= 0 || index > int(m_aux.size())) return false;
    Aux& a = *m_aux[index - 1];
    if (a.script->document()->isModified())
    {
        QMessageBox m(this);
        m.setText(T("Save the changes to %1?").arg(QFileInfo(a.path).fileName()));
        m.setInformativeText(T("If you don't save, they will be lost."));
        m.addButton(QMessageBox::Discard);
        m.addButton(QMessageBox::Cancel);
        m.addButton(QMessageBox::Save);
        m.setIcon(QMessageBox::Warning);
        m.setWindowModality(Qt::WindowModal);
        const int r = m.exec();
        if (r == QMessageBox::Cancel) return false;
        if (r == QMessageBox::Save && !saveAux(a)) return false;
    }
    QWidget* page = a.page;
    m_tabs->removeTab(index);       // (shows the neighbour)
    m_pages->removeWidget(page);
    m_aux.erase(m_aux.begin() + (index - 1));
    page->deleteLater();
    updateTabs();
    // (the tab bar and the pages must show the same tab again)
    m_pages->setCurrentIndex(m_tabs->currentIndex());
    onTabChanged(m_tabs->currentIndex());
    return true;
}

void Editor::closeAuxiliaryTabs()
{
    while (!m_aux.empty())
    {
        QWidget* page = m_aux.back()->page;
        m_tabs->removeTab(int(m_aux.size()));
        m_pages->removeWidget(page);
        m_aux.pop_back();
        page->deleteLater();
    }
    if (m_tabs)
    {
        updateTabs();
        m_pages->setCurrentIndex(0);
        m_auxRow->hide();
    }
}

bool Editor::saveAux(Aux& a)
{
    QFile f(a.path);
    if (!f.open(QIODevice::WriteOnly))
    {
        QMessageBox::warning(this, "FielDes", T("Could not save\n%1").arg(QDir::toNativeSeparators(a.path)) +
                                              "\n\n" + f.errorString());
        return false;
    }
    QString text = a.script->toPlainText();
    if (a.crlf) text.replace("\n", "\r\n");
    f.write(text.toUtf8());
    f.close();
    a.script->document()->setModified(false);
    if (isLibraryFile(a.path))
    {
        emit notice(T("Saved %1. The library is read when FielDes starts.").arg(QFileInfo(a.path).fileName()));
    }
    else
    {
        emit notice(T("Saved %1").arg(QFileInfo(a.path).fileName()));
        m_textChangedDebounce.start();      // the script runs again, with the new file
    }
    return true;
}

bool Editor::saveCurrentAuxiliary()
{
    const int i = m_tabs->currentIndex();
    if (i <= 0 || i > int(m_aux.size())) return false;
    saveAux(*m_aux[i - 1]);
    return true;
}

QStringList Editor::modifiedAuxiliary() const
{
    QStringList names;
    for (const auto& a : m_aux)
    {
        if (a->script->document()->isModified()) names << QFileInfo(a->path).fileName();
    }
    return names;
}

bool Editor::saveAllAuxiliary()
{
    for (auto& a : m_aux)
    {
        if (a->script->document()->isModified() && !saveAux(*a)) return false;
    }
    return true;
}

}   // namespace FielDes
