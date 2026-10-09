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
#pragma once

#include <QPlainTextEdit>
#include <QMap>
#include <QSet>
#include <QHash>
#include <QTimer>

#include <functional>

class QCompleter;
class QStringListModel;

namespace FielDes {
class Formatter;
class ScriptGutter;

/*
 *  The script editor: a QPlainTextEdit with the usual IDE conveniences --
 *  a line-number / fold gutter, block folding, current-line and bracket
 *  highlighting, autocompletion with call tips, go-to-definition,
 *  multi-cursor editing and a handful of line-editing commands.
 */
class Script : public QPlainTextEdit
{
    Q_OBJECT
public:
    Script(QWidget* parent=nullptr);
    void bind(Formatter* f);

    /*  Named layers of extra selections, composed into one list  */
    enum SelectionLayer {
        SEL_CURRENT_LINE=0, SEL_FIND, SEL_BRACKET, SEL_ERROR,
        SEL_FLASH, SEL_MULTI, SEL_LINK, SEL_PAUSE, SEL_COUNT
    };
    void setSelections(SelectionLayer layer,
                       const QList<QTextEdit::ExtraSelection>& sels);
    const QList<QTextEdit::ExtraSelection>& selections(SelectionLayer layer) const
    { return m_layers[layer]; }

    /*  Marks a (0-based) line as holding an error, shown in the gutter;
     *  -1 clears it. */
    void setErrorLine(int line);

    /*  Breakpoints: a run stops before the statement holding one.  They
     *  follow their line as the text is edited; clicking the left edge of
     *  the gutter (or F9) toggles one.  breakpoints() lists the 1-based
     *  lines. */
    QList<int> breakpoints() const;
    bool hasBreakpoint(int block) const;
    void toggleBreakpoint(int block);
    void toggleBreakpointAtCursor() { toggleBreakpoint(textCursor().blockNumber()); }
    void clearBreakpoints();
    /*  Makes exactly these (1-based) lines the breakpoints (those a file had when it was closed); one change signal  */
    void setBreakpoints(const QList<int>& lines);

    /*  Marks the (0-based) line a run is stopped before; -1 clears it.  The text cursor stays where it is, and the view
     *  follows the stop only while the script is not the widget being typed in  */
    void setPausedLine(int line);

    /*  Completion vocabulary: base words (keywords, library functions),
     *  attribute names offered after "studio." and after any other ".",
     *  and one-line call tips shown after typing "name(" */
    void setCompletionWords(const QStringList& words);
    void setMemberWords(const QString& owner, const QStringList& words);
    void setCallTips(const QMap<QString, QString>& tips);

    /*  Library definitions (name -> "file\tline", 1-based line) used by
     *  go-to-definition when a name isn't defined in the script itself */
    void setLibraryDefinitions(const QMap<QString, QString>& defs);
    const QMap<QString, QString>& libraryDefinitions() const { return m_libraryDefs; }

    /*  How a name is looked up in other files: (name, owner) -> "file<TAB>line", or an empty string.
     *  The owner is the identifier before a "." (module or object), possibly empty.  The editor sets
     *  it, since it knows which file this text belongs to and what that file imports  */
    using Resolver = std::function<QString(const QString& name, const QString& owner)>;
    void setDefinitionResolver(Resolver r) { m_resolver = std::move(r); m_linkCache.clear(); }

    /*  What the Python library does for the editor (fieldes.app_support): (function, argument as JSON) -> its answer.  The completion writes
     *  a call as the library lays it out (`complete_call`), a hover over an argument's name shows its documentation (`argument_doc`), and a
     *  call is split over lines or joined (`reformat_call`)  */
    using Support = std::function<QString(const QString& function, const QString& argument)>;
    void setSupport(Support s) { m_support = std::move(s); }

    /*  The call that holds the cursor with its arguments one to a line (join: on one line); false when there is none, or nothing changes  */
    bool reformatCall(bool join);
    /*  Selects `length` characters at a (0-based) place and gives the editor the keyboard: where a placeholder is, for the model tree to point at  */
    void selectRange(int line0, int col0, int length);

    /*  Takes over another script's completion words, call tips and library definitions  */
    void copyVocabularyFrom(const Script& other);

    /*  The text size, in points (changed by Ctrl+plus / Ctrl+minus; the editor's tabs share it)  */
    void setFontSize(int size);

    /*  Moves to a (0-based) line and selects `name` on it (the whole line when it isn't there)  */
    void revealName(int line0, const QString& name);

    /*  Moves the cursor to the start of a (0-based) line, unfolding it if
     *  needed, centres it and briefly flashes it. */
    void goToLine(int line, bool flash=true);
    /*  Scrolls a (0-based) line into view, without moving the text cursor  */
    void scrollToLine(int line);
    /*  The same, but only when the line is not in view (and then to the middle)  */
    void revealLine(int line);

    /*  Selects the given (0-based line / column) range */
    void selectRange(int line0, int col0, int line1, int col1);

    /*  Folding  */
    bool foldRange(int block, int* end) const;   // is `block` a fold header?
    bool isFolded(int block) const;
    void setFolded(int block, bool folded);
    void toggleFoldAtCursor();
    void foldAll();
    void unfoldAll();

    /*  Gutter geometry and painting (called by ScriptGutter)  */
    int gutterWidth() const;
    void paintGutter(QPaintEvent* event);
    void gutterPressed(QMouseEvent* event);

    /*  Returns the identifier under a viewport position (and its range)  */
    QString wordAt(const QPoint& viewportPos, QTextCursor* range=nullptr) const;

    /*  Replaces the multi-cursor set: the last cursor becomes the main one,
     *  and typing then edits every selection at once. */
    void setMultiCursors(const QList<QTextCursor>& cursors);

    /*  Unfolds whatever hides the given block  */
    void revealBlock(const QTextBlock& b) { ensureBlockVisible(b); }

public slots:
    void toggleComment();
    void duplicateLines();
    void deleteLines();
    void moveLinesUp()   { moveLines(-1); }
    void moveLinesDown() { moveLines(+1); }
    void selectNextOccurrence();
    void clearExtraCursors();
    void triggerCompletion();
    /*  Whether the word being typed (`prefix`) is the first of a comment: after `#` or `# `  */
    bool atCommentHead(const QString& prefix) const;
    void goToDefinitionAtCursor();
    void zoomInFont()  { zoomFont(+1); }
    void zoomOutFont() { zoomFont(-1); }
    void promptGoToLine();

signals:
    /*  The text size was changed with the keys  */
    void fontSizeChanged(int size);

    /*  A name defined in a library file (not the script) was requested */
    void libraryDefinitionRequested(QString name, QString file, int line);

    /*  A breakpoint was set or removed (the script should run again)  */
    void breakpointsChanged();

    /*  A right-click on a (0-based) line, to be answered with the menu of the viewport for what the line defines (see
     *  setObjectMenu)  */
    void objectMenuRequested(int line0, QPoint globalPos);

public:
    /*  Whether a right-click on a line opens the menu of the viewport for the model that line defines (the script that is
     *  rendered has it; a right-click on selected text still opens the text menu: cut, copy, paste)  */
    void setObjectMenu(bool on) { m_objectMenu = on; }

protected:
    void contextMenuEvent(QContextMenuEvent* e) override;
    bool m_objectMenu = false;
    void keyPressEvent(QKeyEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void focusOutEvent(QFocusEvent* e) override;
    bool event(QEvent* e) override;

    void moveLines(int dir);
    void zoomFont(int delta);
    void updateGutterWidth();
    void updateGutter(const QRect& rect, int dy);
    void onCursorMoved();
    void onContentsChange(int pos, int removed, int added);
    void validateFolds();
    void ensureBlockVisible(const QTextBlock& b);
    void refreshSelections();
    void highlightBrackets();
    void rescanScriptWords();
    void insertCompletion(const QString& completion);
    QString textUnderCursorForCompletion(QString* owner) const;
    void showCallTip(const QString& name);
    bool goToDefinition(const QString& name, int fromLine, const QString& owner=QString());
    int findDefinitionLine(const QString& name, int fromLine, bool onlyDefs=false) const;
    bool resolvable(const QString& name, const QString& owner);
    static QString ownerBefore(const QString& line, int wordStart);
    bool handleMultiCursorKey(QKeyEvent* e);
    int indentOf(const QTextBlock& b) const;
    bool isBlankOrComment(const QTextBlock& b) const;

    Formatter* m_formatter=nullptr;
    ScriptGutter* m_gutter=nullptr;

    QList<QTextEdit::ExtraSelection> m_layers[SEL_COUNT];
    int m_errorLine=-1;
    int m_pausedLine=-1;

    // A breakpoint is bound to the code on its line, not to the line's number: a cursor at the start of the line, which the
    // document moves along with the text, kept right by trackBreakpoints() where a cursor alone is wrong (see there)
    struct Breakpoint
    {
        QTextCursor cursor;         // (always at the start of a line; stays before text typed there)
        QString text;               // what the line said when last seen, trimmed: the code the breakpoint is bound to
        int pos = 0;                // where the line started and how long its text was, before the change being handled
        int length = 0;
        bool dormant = false;       // its line was deleted: kept a while, for an undo or the line pasted somewhere else
        int age = 0;                // (changes since)
    };
    QList<Breakpoint> m_breakpoints;

    // The placeholders (`...`) of the call that was just completed: the first is selected, Tab goes to the next
    QList<QTextCursor> m_holes;
    int m_holeFirst = -1, m_holeLast = -1;          // (the lines the completed call is on: the placeholders are given up when the cursor leaves them)
    Support m_support;
    bool nextHole();
    Breakpoint makeBreakpoint(const QTextBlock& block) const;
    void trackBreakpoints(int position, int removed, int added);

    QCompleter* m_completer=nullptr;
    QStringListModel* m_completionModel=nullptr;
    QStringList m_baseWords;
    QStringList m_scriptWords;
    QMap<QString, QStringList> m_memberWords;
    QMap<QString, QString> m_callTips;
    QMap<QString, QString> m_libraryDefs;
    Resolver m_resolver;
    QHash<QString, bool> m_linkCache;       // (owner.name) -> whether another file defines it
    QTimer m_wordScanTimer;

    QList<QTextCursor> m_extraCursors;
    QTimer m_flashTimer;
};

/*  The gutter widget, which just forwards painting / clicks to Script  */
class ScriptGutter : public QWidget
{
public:
    ScriptGutter(Script* s) : QWidget(s), m_script(s) { setMouseTracking(true); }
    QSize sizeHint() const override { return QSize(m_script->gutterWidth(), 0); }
protected:
    void paintEvent(QPaintEvent* e) override { m_script->paintGutter(e); }
    void mousePressEvent(QMouseEvent* e) override { m_script->gutterPressed(e); }
    Script* m_script;
};

}   // namespace FielDes
