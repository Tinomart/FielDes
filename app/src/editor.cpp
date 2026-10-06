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
#include <array>
#include <set>
#include <cassert>

#include <iostream>
#include <QDateTime>
#include <QDir>
#include <QHBoxLayout>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QSizePolicy>
#include <QTextBlock>

#include "libfive/step/step_progress.hpp"
#include "libfive/run_progress.hpp"

#include "fieldes/editor.hpp"
#include "fieldes/script.hpp"
#include "fieldes/findbar.hpp"
#include "fieldes/color.hpp"
#include "fieldes/theme.hpp"

#include "fieldes/python/formatter.hpp"
#include "fieldes/python/language.hpp"
#include "fieldes/python/syntax.hpp"

namespace FielDes {

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
                   QSettings("FielDes", "FielDes").value("editor-font-size", 11).toInt());
        QFontMetrics fm(font);
        script->setTabStopDistance(fm.horizontalAdvance("  "));
        script_doc->setDefaultFont(font);
        QFont out("Inconsolata", std::max(8, font.pointSize() - 3));
        err_doc->setDefaultFont(out);
        err->setFixedHeight(QFontMetrics(out).height() + 8);
    }

    // The editor is paper-white, the output a shade darker; both with the text colour of the chrome
    err->setObjectName("Output");
    err->setFrameShape(QFrame::NoFrame);
    script->setFrameShape(QFrame::NoFrame);
    // (the other tabs, which are not rendered, have a slightly darker page; the first tab is the bold
    // one with the accent line)
    setStyleSheet(QString(
        "QPlainTextEdit { background-color: %1; color: %2; selection-background-color: #cfe3f3; border: none; }"
        "QPlainTextEdit#Output { background-color: %3; border-top: 1px solid %4; padding: 2px 4px; }"
        "QPlainTextEdit#Aux { background-color: #f0ede3; }"
        "QTabBar { background: %3; }"
        "QTabBar::tab { background: %3; color: %5; padding: 4px 12px 3px 10px; border: none;"
        "               border-right: 1px solid %4; border-bottom: 2px solid transparent; }"
        "QTabBar::tab:selected { background: %1; color: %2; border-bottom: 2px solid %6; }"
        "QTabBar::tab:first { color: %2; font-weight: bold; }"
        "QTabBar::tab:first:selected { border-bottom: 2px solid %7; }"
        "QWidget#AuxRow { background: %3; border-top: 1px solid %4; }"
        "QWidget#AuxRow QLabel { color: %5; }")
        .arg(Theme::paper.name(), Theme::text.name(), Theme::chrome.name(), Theme::border.name(),
             Theme::muted.name(), "#9b968a", Theme::accent.name()));

    // Emit the script whenever text changes
    connect(script, &QPlainTextEdit::textChanged,
            &m_textChangedDebounce, QOverload<>::of(&QTimer::start));

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
        m_tabs->addTab("Untitled");
        m_tabs->setTabButton(0, QTabBar::RightSide, nullptr);    // (the script cannot be closed)
        {   // A play mark: this is the tab that is rendered
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
            m_tabs->setTabIcon(0, QIcon(pm));
        }
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
    layout->addWidget(m_tabs);
    layout->addWidget(m_pages, 1);
    layout->addWidget(m_auxRow);
    layout->addWidget(err);

    // Go to definition looks in the files the script imports; the text size is shared by the tabs
    script->setDefinitionResolver([this](const QString& name, const QString& owner) {
        return resolveDefinition(m_filePath, script->toPlainText(), name, owner);
    });
    connect(script, &Script::fontSizeChanged, this, [this](int size) {
        for (auto& a : m_aux) a->script->setFontSize(size);
    });
    updateTabs();

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
        m_runLabel->setStyleSheet(QString("color: %1;").arg(Color::base01.name()));
        // (its text is elided to the room it has: a long statement must not widen the editor)
        m_runLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        m_runLabel->setMinimumWidth(40);
        row->addWidget(m_runBar);
        row->addWidget(m_runLabel, 1);
        m_runRow->setStyleSheet(QString("background-color: %1;").arg(Color::base3.name()));
        m_runRow->hide();
        layout->addWidget(m_runRow);
    }

    {   // Where a run stopped at a breakpoint waits to be continued
        m_pauseRow = new QWidget;
        auto row = new QHBoxLayout(m_pauseRow);
        row->setContentsMargins(6, 3, 6, 3);
        row->setSpacing(8);
        auto button = new QPushButton(QString("Continue  %1").arg(QChar(0x25B6)));
        button->setToolTip("Continue to the next breakpoint (F8)");
        button->setFocusPolicy(Qt::NoFocus);
        m_pauseLabel = new QLabel;
        m_pauseLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        row->addWidget(button);
        row->addWidget(m_pauseLabel, 1);
        m_pauseRow->setStyleSheet(QString("background-color: %1;").arg(Color::base3.name()));
        m_pauseRow->hide();
        layout->addWidget(m_pauseRow);
        connect(button, &QPushButton::clicked, this, &Editor::continueRun);
    }

    // A breakpoint set or removed runs the script again, up to the first one
    connect(script, &Script::breakpointsChanged,
            &m_textChangedDebounce, QOverload<>::of(&QTimer::start));

    connect(script, &Script::libraryDefinitionRequested,
            this, &Editor::showSource);

    // A right-click on a line of the rendered script is a right-click on the model the line defines (the window opens the
    // viewport's menu for it); the other tabs (files that are only read) keep the text menu
    script->setObjectMenu(true);
    connect(script, &Script::objectMenuRequested, this, &Editor::objectMenuRequested);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    setLayout(layout);

    spinner.setInterval(150);
    connect(&spinner, &QTimer::timeout, this, &Editor::onSpinner);

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

void Editor::onInterpreterBusy() {
    m_scriptRunning = true;
    m_interpreterBusyDebounce.start();
}

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
        err->hide();
        m_runRow->show();
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
    QString what = QString::fromStdString(r.detail);
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
        const QString t = QString::fromStdString(p.text);
        what = what.isEmpty() ? t : what + QString(" %1 ").arg(QChar(0x00b7)) + t;
    }
    if (f >= 0) m_runShown = std::max(m_runShown, std::min(1.0, f));

    // "2/7 . <the statement> . <what the operation does>": when it doesn't
    // fit, the statement is shortened (the counts at the end matter more)
    const QString dot = QString(" %1 ").arg(QChar(0x00b7));
    QString text, shown;
    if (r.step < 0)
    {
        text = shown = "Starting the script";
    }
    else
    {
        const QString head = QString("%1/%2").arg(r.step + 1).arg(r.steps) + dot;
        const QString tail = what.isEmpty() ? QString() : dot + what;
        const QString label = QString::fromStdString(r.label);
        text = head + label + tail;
        const auto& fm = m_runLabel->fontMetrics();
        const int room = std::max(60, m_runLabel->width()) - fm.horizontalAdvance(head + tail);
        shown = room > fm.horizontalAdvance("mmmmmm")
            ? head + fm.elidedText(label, Qt::ElideRight, room) + tail
            : fm.elidedText(text, Qt::ElideRight, std::max(60, m_runLabel->width()));
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

void Editor::onInterpreterDone(Result r)
{
    m_scriptRunning = false;
    if (m_runRow->isVisible() && qEnvironmentVariableIsSet("FIELDES_TIMING"))
        std::cerr << "[bar-script] " << std::fixed << QDateTime::currentMSecsSinceEpoch() / 1000.0
                  << " done after " << m_runClock.elapsed() / 1000.0 << " s" << std::endl;
    m_runRow->hide();
    err->show();
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
    if (r.okay) {
        if (r.pausedLine > 0) {
            // Stopped at a breakpoint: what ran so far is shown as usual
            QString text = QString("Stopped before line %1 (a breakpoint)").arg(r.pausedLine);
            if (r.result != "None") {
                text += "\n" + r.result;
            }
            setResult(Theme::muted, text);
            showPause(r.pausedLine);
        } else {
            setResult(Theme::text, r.result == "None" ? QString() : r.result);
        }

        // Store the textual position of variables, for later editing
        vars = r.vars;

        emit(shapes(r.shapes));
        emit(fieldSources(r.fields));
        if (!r.scene.isEmpty())
        {
            emit(sceneChanged(r.scene));
        }
    } else {
        setResult(Color::red, r.error.error);

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

    // Clear warnings from the UI.  (Only the warnings layout itself: other
    // child widgets, e.g. the find bar, own vertical layouts too, and the
    // previous "every QVBoxLayout but the main one" sweep deleted them.)
    if (m_warnings) {
        for (int i=0; i < m_warnings->count(); ++i) {
            auto item = m_warnings->itemAt(i)->widget();
            if (item) {
                item->deleteLater();
            }
        }
        layout->removeItem(m_warnings);
        m_warnings->deleteLater();
        m_warnings = nullptr;
    }
    // Insert new warnings into the UI
    if (!r.warnings.isEmpty()) {
        QStringList fixes;

        auto v = new QVBoxLayout();
        v->setContentsMargins(10, 10, 10, 10);
        for (auto& f : r.warnings) {
            v->addWidget(new QLabel(f.first, this));
            if (!f.second.isEmpty()) {
                fixes.push_back(f.second);
            }
        }

        if (fixes.size()) {
            auto button = new QPushButton("Fix All", this);
            connect(button, &QPushButton::pressed, this, [=](){
                QTextCursor c(script_doc);
                c.movePosition(QTextCursor::Start);
                for (auto& f : fixes)
                {
                    c.insertText(f);
                }});
            v->addWidget(button, 0, Qt::AlignHCenter);
        }
        layout->addLayout(v);
        m_warnings = v;
    }

    // Announce the settings
    if (r.okay) {
        emit(settingsChanged(r.settings, first_change));
        first_change = false;
    }
}

void Editor::undo()
{
    scriptWidget()->document()->undo();
}

void Editor::redo()
{
    scriptWidget()->document()->redo();
}

////////////////////////////////////////////////////////////////////////////////

void Editor::setResult(QColor color, QString result)
{
    QTextCharFormat fmt;
    fmt.setForeground(color);
    err->setCurrentCharFormat(fmt);
    err->setPlainText(result);
    int lines = err_doc->size().height() + 1;
    QFontMetrics fm(err_doc->defaultFont());
    err->setFixedHeight(std::min(this->height() / 4, lines * fm.lineSpacing() + 10));
}

void Editor::setScript(const QString& s, bool reload)
{
    first_change = !reload;
    if (!reload)
    {
        closeAuxiliaryTabs();      // (the files opened from the old script)
        // Another script: what the old one made (its viewport shapes, its model tree, its output, its error) goes now,
        // and what a run of it that is still going delivers is thrown away.  Left until the new script has run, a file
        // that is slow, or does not run, would look as if opening it had done nothing
        m_discardResults = true;
        vars.clear();
        setResult(Theme::text, QString());
        script->setSelections(Script::SEL_ERROR, QList<QTextEdit::ExtraSelection>());
        script->setErrorLine(-1);
        emit(shapes(QList<Shape*>()));
        emit(documentReplaced());
    }
    script->clearBreakpoints();     // (their lines mean nothing in new text)
    script->setPlainText(s);
}

void Editor::onInterpreterPartialScene(QString json)
{
    if (m_scriptRunning && !m_discardResults)
    {
        emit(partialSceneChanged(json));
    }
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
    auto txt = script_doc->toPlainText();
    m_language->setBreakpoints(script->breakpoints());
    emit(scriptChanged(txt));
}

void Editor::showPause(int line)
{
    m_pausedLine = line;
    m_pauseLabel->setText(QString("Stopped before line %1").arg(line));
    m_pauseRow->show();
    script->setPausedLine(line - 1);
}

void Editor::hidePause()
{
    m_pausedLine = -1;
    m_pauseRow->hide();
    script->setPausedLine(-1);
}

void Editor::continueRun()
{
    if (m_pausedLine < 0 || !m_language)
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
}

void Editor::onDragEnd()
{
    script->setEnabled(true);
    if (m_scriptHadFocus) script->setFocus();
    m_scriptHadFocus = false;
}

void Editor::setVarValues(QMap<libfive::Tree::Id, float> vs)
{
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

    // Build an ordered set so that we can walk through variables
    // in sorted line / column order, making offsets as textual positions
    // shift due to earlier variables in the same line
    auto comp = [&](libfive::Tree::Id a, libfive::Tree::Id b){
        auto& pa = vars[a];
        auto& pb = vars[b];
        return (pa.top() != pb.top()) ? (pa.top() < pb.top())
                                      : (pa.left() < pb.left());
    };
    std::set<libfive::Tree::Id, decltype(comp)> ordered(comp);
    for (auto v=vs.begin(); v != vs.end(); ++v)
    {
        ordered.insert(v.key());
    }

    int line = -1;
    int offset = 0;
    for (auto t : ordered)
    {
        auto v = vs.find(t);
        assert(v != vs.end());

        // Apply an offset to compensate for other variables that may have
        // changed already in this line
        auto pos = vars.find(v.key());
        assert(pos != vars.end());
        if (pos.value().top() == line)
        {
            pos.value().translate({offset, 0});
        }
        else
        {
            line = pos.value().top();
            offset = 0;
        }

        drag_cursor.movePosition(QTextCursor::Start);
        drag_cursor.movePosition(
                QTextCursor::Down, QTextCursor::MoveAnchor, pos.value().top());
        drag_cursor.movePosition(
                QTextCursor::Right, QTextCursor::MoveAnchor, pos.value().left());

        const auto length_before = pos.value().right() - pos.value().left();
        drag_cursor.movePosition(
                QTextCursor::Right, QTextCursor::KeepAnchor, length_before);
        drag_cursor.removeSelectedText();

        QString str;
        str.setNum(v.value());
        drag_cursor.insertText(str);
        auto length_after = str.length();

        pos.value().setRight(pos.value().left() + length_after);
        offset += length_after - length_before;
    }

    // Disable the script again (because this is only called when we're
    // doing a drag operation in the 3D viewport, and the script should
    // be locked).
    drag_cursor.endEditBlock();
    script->setEnabled(false);
}

void Editor::applyEdits(QList<TextEdit> edits, QString description)
{
    (void)description;
    applyEditsAs(edits, false);
}

void Editor::applyEditsLive(QList<TextEdit> edits, QString description)
{
    (void)description;
    applyEditsAs(edits, true);
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

void Editor::onSyntaxReady() {
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
                this, [&]() { m_scriptRunning = true; m_discardResults = false; hidePause(); });
        connect(m_language.data(), &Language::interpreterDone,
                this, &Editor::onInterpreterDone);
        connect(m_language.data(), &Language::interpreterPartialScene,
                this, &Editor::onInterpreterPartialScene);
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
    updateTabs();
}

void Editor::updateTabs()
{
    const QString name = m_filePath.isEmpty() ? QString("Untitled") : QFileInfo(m_filePath).fileName();
    m_tabs->setTabText(0, name);
    m_tabs->setTabToolTip(0, "Rendered: the viewport shows what this script makes.\n"
                             "The other tabs are files opened for editing only.");
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
            "Editing only: this file is not rendered." +
            (isLibraryFile(a.path) ? QString("\nPart of FielDes's library: changes apply after FielDes is restarted.")
                                   : QString("\nSaving it runs the script again.")));
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
        m_auxLabel->setText(QString("Editing only: this file is not rendered. ") +
            (isLibraryFile(a.path) ? "Changes to the library apply after FielDes is restarted."
                                   : "Saving it runs the script again."));
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
        emit notice("Cannot open " + QDir::toNativeSeparators(file));
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
        m.setText("Save the changes to " + QFileInfo(a.path).fileName() + "?");
        m.setInformativeText("If you don't save, they will be lost.");
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
        QMessageBox::warning(this, "FielDes", "Could not save\n" + QDir::toNativeSeparators(a.path) +
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
        emit notice("Saved " + QFileInfo(a.path).fileName() + ". The library is read when FielDes starts.");
    }
    else
    {
        emit notice("Saved " + QFileInfo(a.path).fileName());
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
