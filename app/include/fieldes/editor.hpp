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

#include <memory>
#include <vector>

#include <QPlainTextEdit>
#include <QStackedWidget>
#include <QTabBar>
#include <QVBoxLayout>
#include <QTimer>
#include <QProgressBar>
#include <QLabel>
#include <QElapsedTimer>

#include "fieldes/documentation.hpp"
#include "fieldes/formatter.hpp"
#include "fieldes/result.hpp"
#include "fieldes/language.hpp"
#include "fieldes/scenetree.hpp"

namespace FielDes {

class Script;
class FindBar;

/*
 *  An Editor contains the script text editor window and an error pane
 */
class Editor : public QWidget
{
    Q_OBJECT
public:
    Editor(Language::Type language);
    void setScript(const QString& s, bool reload=false);
    QString getScript() const;
    void setModified(bool m);

    /*  Sets the script state to the default for the current language */
    void loadDefaultScript();

    /*  Loads an interpreter / syntax / formatter for the given language */
    void setLanguage(Language::Type type);

    /*  A question to the interpreter about the last run (Interpreter::callSupport)  */
    QString callSupport(const QString& function, const QString& arg, QString* error);

    /*  Returns the language currently loaded */
    Language::Type getLanguage() const;

    /*  Guesses the language type based on file extension, then calls
     *  setLanguage to load the appropriate interpreter */
    void guessLanguage(QString extension);

    /*  Returns an extension for the given language */
    QString getExtension() const;

    /*  Checks whether the given language is supported */
    static bool supportsLanguage(Language::Type type);

    /*  Returns a default language based on preprocessor macros */
    static Language::Type defaultLanguage();

    /*  The text widget and find bar of the tab being edited (for the menu actions).  The first tab
     *  holds the script that is run and rendered; the others hold files opened for editing only (see
     *  openFile)  */
    Script* scriptWidget() const;
    FindBar* findBar() const;

    /*  The file of the script (empty when it has none): tells the first tab's name, and where
     *  its imports are looked for  */
    void setFilePath(const QString& path);

    /*  Whether the tab being edited is one of the others, and its Save (true when it was one)  */
    bool currentIsAuxiliary() const { return m_tabs && m_tabs->currentIndex() > 0; }
    bool saveCurrentAuxiliary();

    /*  The other tabs' files with changes not saved yet (their names), and saving them all  */
    QStringList modifiedAuxiliary() const;
    bool saveAllAuxiliary();

    /*  Whether the script is being run now  */
    bool scriptRunning() const { return m_scriptRunning; }

public slots:
    /*  Opens a file in a tab of its own (or shows the one that has it) at a (0-based) line, and selects
     *  `name` there.  Only the first tab is run; this one is just for editing  */
    void openFile(const QString& file, int line0, const QString& name=QString());

    /*  The first tab (the rendered script), and moving between tabs  */
    void showScriptTab();
    void nextTab();
    void previousTab();
    bool closeCurrentTab();

    void onInterpreterDone(Result result);
    void onInterpreterBusy();

    void undo();
    void redo();

    /*
     *  When the text-change debouce timer expires, grab the script and
     *  emit it with scriptChanged() so that the interpreter starts working.
     */
    void onTextChangedDebounce();

    /*
     *  Modifies the textual values of variables
     */
    void setVarValues(QMap<libfive::Tree::Id, float> vs);

    /*
     *  Applies a set of text replacements as one undoable step (used by
     *  the model tree, whose every action is a script edit)
     */
    void applyEdits(QList<TextEdit> edits, QString description);

    /*  The same, for an edit that goes on as something is typed (a name, a number): the edits of one go on after another are
     *  one undoable step -- an edit joins the one before it as long as nothing else changed the script since  */
    void applyEditsLive(QList<TextEdit> edits, QString description);

    /*  Scrolls to (and flashes) a 0-based line  */
    void goToLine(int line0);

    /*  Continues a script stopped at a breakpoint (does nothing otherwise)  */
    void continueRun();

    /*  Whether the script is stopped at a breakpoint, and the (1-based) line
     *  it is stopped before  */
    bool paused() const { return m_pausedLine > 0; }
    int pausedLine() const { return m_pausedLine; }

    /*
     *  While a drag is taking place, the text field is frozen to user editing
     *  and drag_cursor is engaged in an edit session (so the entire drag
     *  operation can be undone at once).
     */
    void onDragStart();
    void onDragEnd();

signals:
    void languageChanged();
    void scriptChanged(QString s);
    void modificationChanged(bool m);
    void undoAvailable(bool a);
    void redoAvailable(bool a);

    /*
     *  Emitted when the interpreter delivers us a list of shapes
     */
    void shapes(QList<Shape*> shapes);

    /*  The field models of the script (they are not shapes: the section viewer shows the one that is selected)  */
    void fieldSources(QList<FieldEntry> fields);

    /*
     *  Shows the documentation pane attached to the language
     *  (forwarded to m_language)
     */
    void onShowDocs();

    /*
     *  Invoked when a script defines settings using special global functions.
     *  first is true if this is the first time this script has been evaluated,
     *  false otherwise; it is used to decide whether to zoom to shape bounds.
     */
    void settingsChanged(Settings s, bool first);

    /*
     *  The model-tree description of the last successful evaluation
     */
    void sceneChanged(QString json);

    /*
     *  The model-tree description of what the statements of a script that is still running have made so far
     */
    void partialSceneChanged(QString json);

    /*
     *  Another script (a file opened, a new one) has taken the place of the one that was shown: what the old one
     *  rendered and listed is to go at once, not when the new one has run
     */
    void documentReplaced();

    /*  A short message for the status bar  */
    void notice(QString text);

    /*  A right-click on a (0-based) line of the rendered script: the viewport's menu for the model it defines is wanted at
     *  `globalPos`  */
    void objectMenuRequested(int line0, QPoint globalPos);

protected slots:
    void onSpinner();

    /*  Fills the completion / call-tip / definition tables once the
     *  interpreter has started */
    void onLanguageReady(QStringList keywords, Documentation docs);

    /*  Go to definition found a name in another file: opens it in a tab  */
    void showSource(QString name, QString file, int line);

    /*  Marks the whole script as changed, which triggers re-highlighting */
    void onSyntaxReady();

    /*  What a run in progress has made so far, for the model tree (see Interpreter::partialScene)  */
    void onInterpreterPartialScene(QString json);

protected:
    void setResult(QColor color, QString result);

    /*  The "stopped at a breakpoint" row (with its Continue button)  */
    void showPause(int line);
    void hidePause();

    Script* script;             // owned by layout
    QTextDocument* script_doc;  // owned by script

    FindBar* m_findBar;         // owned by layout
    QStringList m_keywords;

    // The tabs: 0 is `script` (run, rendered), then one per file opened for editing
    struct Aux
    {
        QString path;
        QWidget* page=nullptr;
        Script* script=nullptr;
        FindBar* find=nullptr;
        std::unique_ptr<::FielDes::Formatter> formatter;
        bool crlf=false;
    };
    QTabBar* m_tabs=nullptr;
    QStackedWidget* m_pages=nullptr;
    QWidget* m_auxRow=nullptr;          // "editing only" note under an other tab
    QLabel* m_auxLabel=nullptr;
    std::vector<std::unique_ptr<Aux>> m_aux;
    QString m_filePath;

    Script* tabScript(int index) const;
    bool closeTab(int index);
    bool saveAux(Aux& a);
    void onTabChanged(int index);
    void updateTabs();
    void updateAuxTitle(const Aux& a);
    QString resolveDefinition(const QString& path, const QString& text,
                              const QString& name, const QString& owner);
    void closeAuxiliaryTabs();

    QPlainTextEdit* err;        // owned by layout
    QTextDocument* err_doc;     // owned by err;

    QVBoxLayout* layout;    // owned by the widget itself
    QVBoxLayout* m_warnings=nullptr;  // current warnings block, if any

    // The Language manages the embedded interpreter
    QScopedPointer<Language> m_language;

    // The documentation pane, constructed when the interpreter is ready
    // then shown / hidden as needed
    QScopedPointer<DocumentationPane> doc_pane;

    QTextCharFormat error_format;
    QTimer spinner;

    // Debounces text changes, to avoid emitting too many signals
    QTimer m_textChangedDebounce;

    // The document's revision just after the last live edit (applyEditsLive): the next one joins it only if it is still current
    int m_liveRevision = -1;
    void applyEditsAs(QList<TextEdit> edits, bool live);

    // Debounces the interpreter's "busy" signal to avoid UI jitter
    QTimer m_interpreterBusyDebounce;
    bool m_scriptRunning = false;

    // Another script has taken the place of the one that a run in flight is of: what that run delivers is not shown
    // (until the next run begins)
    bool m_discardResults = false;

    // The script's progress while it runs, small, in place of the result
    // line (see onSpinner)
    QWidget* m_runRow = nullptr;
    QProgressBar* m_runBar = nullptr;
    QLabel* m_runLabel = nullptr;
    QElapsedTimer m_runClock;
    double m_runShown = 0.0;
    int m_runLogged = -1;
    std::pair<int, int> m_runOperation{-2, 0};   // (step, operation) the bar shows
    int m_runImportStage = -1;                   // and the stage of a STEP import in it
    QString m_runText;

    // Shown instead of the progress while the script is stopped at a breakpoint
    QWidget* m_pauseRow = nullptr;
    QLabel* m_pauseLabel = nullptr;
    int m_pausedLine = -1;      // 1-based, or -1

    bool drag_should_join=false;
    bool first_change=false;
    /*  Whether the script had the keyboard when a drag in the viewport began (it gets it back after: a click on a
     *  shape in the viewport must not take it away from the viewport)  */
    bool m_scriptHadFocus=false;
    /*  Set while the program, not the user, changes the tab (showScriptTab): the editor does not take the keyboard  */
    bool m_quietTab=false;

    QMap<libfive::Tree::Id, QRect> vars;
};

}   // namespace FielDes
