/*
FielDes: field-driven design
Derived from Studio, a simple GUI for the libfive CAD kernel

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/
#pragma once

#include <functional>

#include <QElapsedTimer>
#include <QFrame>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QList>
#include <QPair>
#include <QPointer>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QVector3D>

class QTreeWidget;
class QTreeWidgetItem;
class QToolButton;
class QLineEdit;
class QLabel;
class QMenu;

namespace FielDes {

/*  One text replacement in the script: [line0:col0, line1:col1) -> text,
 *  0-based lines and character columns  */
struct TextEdit {
    int line0, col0, line1, col1;
    QString text;
};

/*
 *  The model tree: a collapsible panel floating over the 3D view that
 *  lists what the script builds -- imports (with their parts), shape
 *  variables, displayed expressions and render settings -- as described by
 *  fieldes/app_support.py after every evaluation.
 *
 *  It holds no state of its own: every action (show / hide, reimport, pick
 *  another part, region of interest) is turned into an edit of the script
 *  text, which then re-evaluates, exactly like dragging a var() handle.
 */
class ScenePanel : public QFrame
{
    Q_OBJECT
public:
    ScenePanel(QWidget* parent=nullptr);

    /*  The current script text (edits are computed against it)  */
    void setScriptSource(std::function<QString()> f) { m_source = f; }

    /*  The statement that makes a shape's surfaces draggable (expose(...)), which only the
     *  interpreter can write: called with a variable's name, gives the text, or an empty
     *  text and the reason  */
    void setExposeSource(std::function<QString(const QString&, QString*)> f) { m_expose = f; }

    /*  A question to the interpreter (fieldes.app_support.<function>(arg) -> text; the reason in *error when it
     *  fails): the viewport menus' calls come from it  */
    void setSupport(std::function<QString(const QString&, const QString&, QString*)> f) { m_support = f; }

    /*  Whether the script has a model a menu operation can work on, and another one besides the model displayed
     *  by the (0-based) line `line0` (for the operations that combine two)  */
    bool hasModel() const;
    bool hasOtherModel(int line0) const;
    /*  Whether the model defined by the statement that starts on this (1-based) line is shown (its eye is on); true when the scene has no model there  */
    bool shownAtLine(int line1) const;
    /*  Whether what is selected (or the model on `line0`) can make this entry of a menu (kind "operation" or "primitive"): the library is asked
     *  to write the call for that selection, and an entry it refuses -- a part selected with supports and loads for a set of conditions,
     *  conditions without a part for a simulation ... -- is greyed out in the menu, without a word: it is not offered for a selection that
     *  cannot make it  */
    bool entryAllowed(const QString& kind, const QString& name, int line0) const;
    /*  Whether the (0-based) line belongs to a model (a variable or a displayed expression that ran), and the middle of its
     *  bounds -- where a menu opened on it from outside the viewport takes its place from  */
    bool modelAtLine(int line0, QVector3D* centre = nullptr) const;
    /*  What kind of model the line is of ("solid", "field", ...: see fieldes.kinds), or "" when it is no model's  */
    QString typeAtLine(int line0) const;
    /*  Whether a (0-based) line is one of a model's own: where it is displayed, its statement, its expose and handles lines  */
    bool ownsLine(const QJsonObject& item, int line0) const;

    /*  The selection, in the order it was made (the first is the one a subtraction subtracts from): the keys of the
     *  selected rows (for scripted tests)  */
    QStringList selectionKeys() const { return m_selectOrder; }

    /*  For the guided tour and the tests: select the model a variable holds (once it is in the tree); the rectangle of a row (the
     *  first whose text starts with `prefix`) in the coordinates of `in`; the tree's viewport (where a mouse is sent to); a
     *  render setting's field (fn: "set_bounds" part 0..5 | "set_resolution" 0 | "set_quality" 0); whether the settings are open  */
    bool selectModel(const QString& var) { return selectVar(var); }
    QRect rowRect(const QString& prefix, QWidget* in);
    QWidget* treeViewport() const;
    int treeScroll() const;                      // (the vertical scroll of the tree: for tests)
    QLineEdit* settingEditor(const QString& fn, int index) const;
    void showSettings(bool open);
    bool settingsOpen() const;
    bool dragging() const { return m_dragging; }
    /*  For the tests: whether a row is a shadow (the reference of a statement to a model another statement owns), and the row of a
     *  model under a statement, shadow or not (null if there is none)  */
    bool isShadow(QTreeWidgetItem* row) const;
    QTreeWidgetItem* rowUnder(const QString& parentVar, const QString& modelVar) const;

    /*  Where in a row a dragged model is dropped: above it, on it, below it, or in the empty space of the tree  */
    enum DropAt { DropAbove, DropOn, DropBelow, DropViewport };

    /*  The rows as text, one per line, two spaces of indentation for each level of nesting: `name  [type]` (for scripted tests)  */
    QString dumpRows() const;

    /*  What the rows of the models say, one line each: the lines of the script they are on, whether they are shown, locked, cached, what
     *  their gizmo's mode is, and what their buttons show (for scripted tests), and how the tree stands (a prediction, a scene)  */
    QString dumpState() const;

    /*  For the tests: the lines that show and hide the models are moved by `delta` in the rows, the script untouched -- rows that are of
     *  another text than the script's, which is what an edit must not be made from (lineIs)  */
    void debugShiftLines(int delta, bool blocks = false);

    /*  The script has an error (the last line of its traceback, and the 0-based line, -1 when it is not known): the card has a red
     *  frame and header and the note under the rows says what, until it runs well again.  An empty text takes it away  */
    void setError(const QString& text, int line0);

public slots:
    /*  The provisional gizmo was pressed: the selected model is made ready to be dragged now, not after the pause that
     *  lets a rectangle finish selecting  */
    void prepareNow();
    /*  The I key: only the selected model is shown, all others hidden; pressed again, what was shown before is
     *  shown again.  Both are script edits (one undoable step), like the eyes of the tree  */
    void toggleIsolation();

    /*  A viewport menu entry: writes `name = <call>` and a line showing it, selects the new model.  kind is
     *  "primitive" (placed at `point`, `scale` mm across about a hundred pixels there, at the end of the script)
     *  or "operation" (on the model displayed by the (0-based) line `line0`, or, for a negative line, the
     *  selected model; written under its definition)  */
    void createFromMenu(QString kind, QString name, QVector3D point, double scale, int line0, int generation = -1);
    /*  Which run of the script the model tree shows (it counts up with every run): a menu remembers it, so that an entry
     *  chosen after the script changed is known not to mean the line the menu was opened on  */
    int generation() const { return m_generation; }

    void setScene(const QString& json);

    /*  A script has just been opened (its text is in the editor): if the tree of an earlier run of this very text was kept, it is
     *  shown at once, correct, before the script has run -- and the run replaces it  */
    void showCached();

    /*  Another script has replaced the one the tree shows: nothing of the old one stays (its rows, its selection, what
     *  was waiting for its run)  */
    void clearScene();

    /*  The keys that work on the selected models.  Every toggle follows one rule: if the models are not all in the
     *  "on" state, they are all put there first (visible, locked, cached); only when they all are, they are
     *  all turned off -- so a mixed selection is never flipped model by model.
     *
     *  E: when the gizmo is shown, round click -> never -> always (models that are in different modes all go to click).
     *  R: lock / unlock.  V: show / hide.  C: the render cache on / off.  Pulling a model's surfaces is always possible
     *  (selecting a model makes it so, when it has not the numbers yet), and the gizmo has priority where it is shown  */
    void toggleSelectedEdit();
    void toggleSelectedLock();
    void toggleSelectedVisible();
    void toggleSelectedCache();

    /*  D: the selected models are deleted from the script (their definitions and the lines that show, hide, edit and
     *  lock them), as one undoable step; it asks once if the rest of the script still uses their names  */
    void deleteSelected();

    /*  Ctrl+D: a copy of each selected model under it; Ctrl+C: the selected models (their statements) are kept, and
     *  Ctrl+V puts copies of them under the selected model, or at the end of the script -- with names that are free, and
     *  the copies referring to each other's copies.  Written like every other edit of the tree: one undoable step  */
    void duplicateSelected();
    void copySelected();
    void pasteModels();
    /*  The viewport's Delete entry on the model displayed by (0-based) line `line0`: that model, or all the selected
     *  ones when it is one of several selected  */
    void deleteByLine(int line0);

    /*  Nothing is selected any more (a click on empty space in the viewport)  */
    void clearSelection();

    /*  Selects the item displayed by the given (0-based) statement line,
     *  e.g. after a click on a shape in the viewport */
    void selectByLine(int line0);

    /*  Ctrl+click on a shape in the viewport: the model displayed by that line joins the selection, or leaves it
     *  when it is in it  */
    void toggleByLine(int line0);

    /*  A selection rectangle drawn in the viewport: the models displayed by these lines are the selection
     *  (with `add`, they join the selection that there is)  */
    void selectLines(QList<int> lines0, bool add);

    void setCollapsed(bool c);

    /*  A surface picked in the viewport: writes `name = select_surface(shape, seed=(x, y, z), ...)` and a
     *  line showing it under the definition of the shape displayed by the (0-based) line `line0`
     *  (the radius is mm, 0: no limit)  */
    void addSurfaceSelection(int line0, QVector3D seed, QString mode, double angle, double radius);

    /*  The model displayed by the (0-based) line `line0` is given a resolution of its own: the line `x = custom_resolution(x, number)` is
     *  written under its definition (the number: the scene's resolution)  */
    void addCustomResolution(int line0);

    /*  What the render cache did for the shapes displayed by (0-based) lines: "kept" (meshed and kept),
     *  "read" (the mesh on screen came from the cache), "no" (cannot be kept), "on" (not meshed yet),
     *  with the words for the tooltip, as `state|text`  */
    void setCacheStates(const QHash<int, QString>& states);

signals:
    /*  Scroll the editor to a (0-based) line and flash it  */
    void goToLine(int line0);
    /*  Select the placeholder (...) at a (0-based) place of the script, ready to be written over: the note under the tree and the rows of
     *  the placeholders point at the code editor, where they are filled in  */
    void placeholderRequested(int line0, int col0);

    /*  A right-click in the tree: the viewport's menu for the model displayed by the (0-based) line `line0` (-1: the menu of
     *  empty space), to be opened at `globalPos`  */
    void menuRequested(int line0, QPoint globalPos);

    /*  Apply these edits to the script as one undoable step  */
    void editScript(QList<TextEdit> edits, QString description);

    /*  The same, for an edit made while something is typed (a name, a number): the edits of one go on after another are one
     *  undoable step  */
    void editScriptLive(QList<TextEdit> edits, QString description);

    /*  Run the script again (nothing in its text changed)  */
    void rerunRequested();

    /*  Highlight the shapes displayed by these (0-based) lines  */
    void highlightLines(QList<int> lines0);
    /*  An edit of the script was followed by the tree before it was run: every (0-based) line of the old text is moved[line] now, or -1
     *  when it is gone.  The viewport's shapes know the lines they were made on only from a run: they are told  */
    void sourceLinesMoved(QVector<int> moved);

    /*  Frame the camera on a box; if the box is empty, on the shapes
     *  displayed by the given lines  */
    void focusRequested(QVector3D min, QVector3D max, QList<int> lines0);
    /*  The selected model has no gizmo yet (it gets its numbers when the script has run): where it will be, so that the view can
     *  draw it at once (see View::setProvisionalGizmo); `on` false when there is none to show  */
    void provisionalGizmo(bool on, QVector3D pivot, QList<int> lines0);

    /*  The fields among the selected models (they are not drawn: the field viewer shows them), by their keys -- a variable's name,
     *  or "line:N" for a field that is only displayed -- in the order selected; empty when the selection holds none.  Emitted when
     *  it changes  */
    void fieldsSelected(QStringList keys);

protected:
    bool eventFilter(QObject* obj, QEvent* e) override;
    void rebuild();
    /*  The field that is being typed in -- one of the render settings, or the name of a row -- found without asking the keyboard
     *  focus of the application, which a window that is not the active one does not have: the editor of the row that is current, else
     *  the widget that has the focus of the window  */
    QWidget* typingWidget() const;
    void onItemClicked(QTreeWidgetItem* item, int column);
    void onItemDoubleClicked(QTreeWidgetItem* item, int column);
    void onContextMenu(const QPoint& pos);

    /*  Drag and drop: models are dragged by their rows (the selected ones).  Dropped on an operation they become inputs of it
     *  (a menu asks which input they replace, or adds one to an operation that takes any number of them); dropped between
     *  two rows they are moved in the script, with the models they are made of when those come later; dropped in the empty
     *  space, to the end.  All of it is edits of the script text, in one undoable step  */
    QList<QJsonObject> draggedModels() const;
    bool canDrop(QTreeWidgetItem* over, int at, bool copy, QString* why = nullptr) const;
    /*  What a drop ON an operation does (never a question): the text that is written and where -- `replace` is the span of what it
     *  takes the place of, `after` the span of the argument it is put after -- or, when it cannot be done, why not  */
    // drop: the model a replacement takes out of the call (its `# shadow:` mark goes with it)
    //  more: the other places the same drop writes into (conditions of several kinds go to several inputs of one call)
    struct OnPlan
    {
        struct Piece { QJsonArray replace, after; QString text; };
        bool ok = false; bool own = false; QJsonArray replace, after; QString text, what, why, drop;
        QList<Piece> more;
    };
    /*  A dropped model of the same kind as one a call is given takes its place in the call (a force on a force, a part on a part, a
     *  field on a field, whether the one it replaces is the call's own or a shadow of another's).  Never a question  */
    OnPlan planReplace(const QList<QJsonObject>& models, const QJsonObject& parent, const QString& replaced) const;
    /*  A model dropped on a placeholder (`...`, the `which`-th of the statement's) takes its place  */
    OnPlan planFill(const QList<QJsonObject>& models, const QJsonObject& stmt, int which) const;
    bool sameKind(const QJsonObject& a, const QJsonObject& b) const;
    OnPlan planDropOn(const QList<QJsonObject>& models, const QJsonObject& target) const;
    bool takesInput(const QJsonObject& model) const;
    /*  Whether the model's call is written with numbers (a FIELD dropped on it can take the place of one: anywhere a number
     *  goes, a field goes)  */
    bool takesNumber(const QJsonObject& model) const;
    /*  Whether the model's call is written with a position as a tuple of numbers (a POINT dropped on it can take its place)  */
    bool takesPoint(const QJsonObject& model) const;
    bool dependsOn(const QJsonObject& model, const QJsonObject& on) const;
    /*  What a drop into a call writes about references (the # shadow: a, b comment on the last line of its statement): see the definition  */
    void markShadows(QStringList& lines, const QList<QJsonObject>& models, const QJsonObject& target, int last, bool copy,
                     bool reference) const;
    /*  A drag begins at a row: false if it cannot (nothing that can be dragged is selected); the label it carries  */
    bool beginDrag(QTreeWidgetItem* row, QString* text, QIcon* icon, bool ctrl, bool wasSelected);
    /*  The left button went down on the tree / came up: a model selected by a press is not made ready to be dragged (lines
     *  written into the script) while the button is down -- the drag that may follow must not wait for a run of the script  */
    void onTreeHeld(bool down);
    void onDrop(QTreeWidgetItem* over, int at, const QPoint& global, bool copy);
    void finishDrop(const QString& overKey, int at, const QPoint& global, bool copy);
    void dropOnModel(const QList<QJsonObject>& models, const QJsonObject& target, bool copy, const OnPlan* given = nullptr);

    /*  The tree is the structure of the calls: a model that stands under an operation is one of the models it is given.  Every
     *  nesting, renesting and denesting is therefore an edit of the arguments of a call -- worked out here, before anything
     *  is written, as a `Rewire`: the models taken out of calls and put into them.  A call that cannot do without a model that is
     *  taken out gets a placeholder, `...`, where it was: nothing is deleted, and the script stops before the statement until
     *  something is written there  */
    struct Rewire
    {
        struct Put { QString name, relative, side; };                // side: "before" or "after" `relative`, or "end"
        // remove: models taken out of the call (the other arguments, or the rest of a list, stay); hole: models that become `...`
        struct Change { QJsonObject item; QStringList remove, hole; QList<Put> insert; };
        QMap<int, Change> edits;                                     // the statements whose call is rewritten, by their line
    };
    QJsonObject itemByKey(const QString& key) const;
    /*  Shadows deleted: the references (statement, model's name) are taken out of their calls, as when a model is denested  */
    void deleteReferences(const QList<QPair<QJsonObject, QString>>& refs);
    QJsonObject itemAtLine(int line) const;
    /*  The statement a dragged model is dragged out of: the one its row stands under, or, for a shadow, the one the shadow is
     *  under (empty: a model that stands under none)  */
    QJsonObject sourceStatement(const QJsonObject& model) const;
    /*  `model` is taken out of the call of `from`: out of its arguments (or the list it is in) when the call keeps what it needs,
     *  else a placeholder takes its place  */
    void wireRemove(Rewire& w, const QJsonObject& from, const QJsonObject& model) const;
    void wireInsert(Rewire& w, const QJsonObject& parent, const QString& name, const QString& relative, const QString& side) const;
    /*  Writes a rewire into the lines (the interpreter rewrites the calls; the lines stay where they are, a line that is no more
     *  is marked, and the lines of what goes are marked) -- `targets` gets the variables of the rewritten statements, `newLength`
     *  the number of lines of each.  False, with the reason, when a call cannot be rewritten  */
    bool wireStage(const Rewire& w, QStringList& lines, QStringList* targets, QHash<QString, int>* newLength, QString* why) const;
    void wireDropGone(QStringList& lines, QVector<int>& origin) const;
    /*  A shadow cannot be above its original.  The tree gives a model its row under the FIRST statement that uses it (the original);
     *  every later use is a shadow.  Whatever an edit does -- a copy or a moved shadow dropped above the original, a statement that holds
     *  a shadow moved above it, the original moved below -- a shadow that would then come first is found in the tree the edit is
     *  predicted to give (ownershipFlips), and ONE message lists them all (confirmFlips): the row would move to the first use and the
     *  old place would show a reference.  That is the easy fix, and it is what is done unless the user cancels (or has asked for it
     *  always, with "do not show again")  */
    struct Flip { QString model, from, to; bool added = false; };
    QList<Flip> ownershipFlips(const QJsonObject& predicted) const;
    bool confirmFlips(const QList<Flip>& flips);
    /*  Whether a copy / moved shadow of one of `models` dropped in `target` would be above the original use of it (no question asked)  */
    bool isAboveOriginal(const QList<QJsonObject>& models, const QJsonObject& target) const;
    QList<QPair<QString, QString>> m_copyAdds;          // (model, statement) the references this drop adds: copies and moved shadows
    /*  Models dropped between the models an operation is given: they become inputs at that place (or move there, when they
     *  are inputs already), and leave the call they were taken from, unless `copy`  */
    void insertAmong(const QList<QJsonObject>& models, const QJsonObject& parent, const QString& relative, bool after, bool copy);
    QList<QJsonObject> m_dropModels;
    QHash<QString, int> m_dragSourceLine;                // (model key -> the line of the statement it is dragged out of)
    bool m_dragShadow = false;                           // (the drag began on a shadow row)
    bool m_dropShadow = false;
    /*  The definition of a variable as a statement on `line` (1-based) sees it  */
    QJsonObject itemBefore(const QString& var, int line) const;
    /*  The line ranges (0-based, from..to) a model's statements are on: its definition (with the comment lines right above
     *  it) and the lines that show, hide, edit and lock it  */
    QList<QPair<int, int>> groupRanges(const QJsonObject& it, bool comments = true) const;
    int groupStart(const QJsonObject& it) const;
    QString groupText(const QJsonObject& it) const;
    QString copyName(const QString& name, const QSet<QString>& taken, bool part) const;
    QList<QJsonObject> copyableModels() const;
    /*  The line after a model's definition and the lines of its own that follow it right away  */
    int groupEnd(const QJsonObject& it) const;
    /*  Moves the statements of `models` (and, of what they need, what is defined after `at`) in front of the line `at`
     *  (0-based); with `onlyLater`, only models that are below that line move.  False, with the reason, when something
     *  would then be used before it is defined  */
    bool moveGroups(QStringList& lines, QVector<int>* origin, const QList<QJsonObject>& models, int at, bool onlyLater,
                    QString* why, QList<QJsonObject>* pulled = nullptr) const;
    /*  A model that statements hold only references to (`# shadow: name` in theirs) keeps its own row at the top level. When a
     *  statement that holds such a reference ends up above the model's definition -- the statement is dragged above it, or the
     *  model below the statement -- the reference is the first use: the model's definition goes directly above that statement and
     *  becomes its own, and the separate row is gone (a shadow is only ever an argument, it never stands by itself in the tree).
     *  A message asks first, once for all of them (hideable)  */
    struct Dissolve { QJsonObject model, user; };
    bool confirmDissolve(const QList<Dissolve>& list);

    /*  Text on one line of the script that moved: every column from `col` on, on the (1-based) `line`, is `delta` further  */
    struct ColShift { int line, col, delta; };
    /*  The edit that turns the script's lines `before` into `after` (one replacement of what differs), as one undoable step;
     *  and, at once, the tree as it will be when the script has run (see predictScene).  `origin` says, for each line of
     *  `after`, the line of `before` it is (-1: a new one); `shifts` the columns that moved; `target` the variable of the
     *  statement whose call was rewritten  */
    void applyLines(const QStringList& before, const QStringList& after, const QString& what, const QVector<int>& origin,
                    const QList<ColShift>& shifts, const QStringList& targets,
                    const QHash<QString, int>& newLength = QHash<QString, int>());
    /*  The tree as it will be once the script that was just edited has run, worked out from the edit alone, so that it is there
     *  at once: the lines of every item are where the edit put them, the models are nested by the new order of the statements and
     *  by what the rewritten statement uses; with a rename, the name is changed everywhere.  The scene of the run replaces it
     *  (the rows that depend on what only the run knows -- the inputs of the rewritten statement -- are marked `stale`)  */
    void predictScene(int oldCount, const QStringList& after, const QVector<int>& origin, const QList<ColShift>& shifts,
                      const QStringList& targets, const QHash<QString, int>& newLength = QHash<QString, int>(),
                      const QString& renameFrom = QString(), const QString& renameTo = QString());
    /*  The scene predictScene would show, without showing it (and without changing anything of the panel)  */
    QJsonObject predictedScene(int oldCount, const QStringList& after, const QVector<int>& origin, const QList<ColShift>& shifts,
                               const QStringList& targets, const QHash<QString, int>& newLength = QHash<QString, int>(),
                               const QString& renameFrom = QString(), const QString& renameTo = QString()) const;
    void installPrediction(const QJsonObject& scene);
    /*  Renaming a variable (a double click on its name): the interpreter says where the name is used (not in text, not an
     *  attribute, not a keyword, not another variable of the same name inside a function), all of them are replaced.  The script
     *  is rewritten as the name is typed (`live`: a name that is not one yet -- empty, a digit first, one that is taken -- waits
     *  for the next key without a word); false when the name cannot be, which is said unless it is live  */
    bool applyRename(const QString& oldName, const QString& newText, bool live = false);
    /*  What a name that is typed in a row is: the name it had when the typing began, and the one the script has now (the last
     *  that could be) -- so that a name that does not end up as one, or Escape, can give the first back  */
    struct Renaming { QString original, current; bool active = false; } m_renaming;
    void liveRename(QTreeWidgetItem* row, const QString& typed);
    void endRenaming(bool keep);
    /*  Escape gave the old name back; the editor's text, which is still the typed one, is then offered to the tree as if Enter had
     *  been pressed: that commit is not to put the typed name back  */
    QElapsedTimer m_revertClock;
    /*  A run answers a key that was typed some keys ago: its scene calls the variable what it was called then.  While a name is
     *  typed such a scene is not shown (it would take the name, and the editor, away under the hand): a scene that does not have
     *  the name the script has now is skipped, and the run that answers the last key brings the one that is shown  */
    bool staleWhileRenaming(const QJsonObject& scene) const;

    /*  The render settings as rows with fields in them: a number is typed in the tree and the call is rewritten (or added), as it
     *  is typed (`live`) and when the field is left  */
    void commitSetting(QLineEdit* edited, bool live = false);
    void insertSettingLine(const QString& call, const QString& fn = QString(), bool live = false);
    QHash<QString, QList<QPointer<QLineEdit>>> m_settingEditors;
    void commitCustomResolution(QLineEdit* edited, bool live, const QString& var);
    /*  Where the call that the last live edit of a setting wrote stands in the script, so that the next key rewrites the same place
     *  without waiting for a run to say where it is now (the scene is a run behind)  */
    struct LiveSetting { int line0 = -1, col0 = 0; QString call; };
    QHash<QString, LiveSetting> m_liveSettings;         // (by the function: set_bounds, set_resolution, set_quality)
    bool m_liveSettingStale = false;    // (written since the last scene: the scene's spans are not to be trusted)
    QList<QJsonObject> m_dragModels;
    bool m_dragging = false;            // (a model is being dragged by the mouse)
    bool m_treeHeld = false;            // (the left button is down on the tree)
    bool m_dropEdited = false;          // (the drag that is over edited the script)
    bool m_predicted = false;           // (the tree shown is a prediction: the script has not run yet)
    /*  An edit made from the rows needs rows that fit the script's text.  When the text has changed since the scene was made (a run
     *  is on its way), the edit waits for the scene that fits (deferEdit, deferItems: the items are found again by their keys),
     *  one at a time (runDeferred); a script with an error has no such scene to wait for, and the edit is refused with the reason  */
    bool sceneIsStale() const;
    bool deferEdit(std::function<void()> again, const QString& what);
    bool deferItems(const QList<QJsonObject>& items, const QString& what, std::function<void(const QList<QJsonObject>&)> again);
    void runDeferred();
    QList<std::function<void()>> m_deferred;
    QElapsedTimer m_sceneClock;         // (since the scene shown was made)
    QElapsedTimer m_predictClock;
    QString m_predictedMd5;             // (the text the prediction is of: its lines fit that text, and only it)
    bool m_textUnreadable = false;      // (the last text the tree was asked to read does not parse: the rows are of an earlier one)
    bool m_rebuildPending = false;      // (the rows are built from the scene a moment from now: until then they are the old ones)
    /*  The line of the old text that each line of the new text is (-1: a new line), found from the edits alone; empty when the edits
     *  cannot be followed with certainty (a line that no edit touched is not the same text afterwards, two lines became one ...)  */
    QVector<int> originAfter(const QStringList& before, const QList<TextEdit>& edits, const QStringList& after) const;
    /*  The edits of a button of the tree, written to the script AND followed by the tree at once: every row has the line that the
     *  edit gave it, and `patch` says what the edit did to the rows it is about (it gets the scene, and for each line of the old
     *  text the 0-based line it is in the new one).  The rows are made again a moment later (never inside the click that made the
     *  edit), and the scene that the run brings replaces it.  False when the edit could not be followed: it is written all the same,
     *  and the tree waits for the run, as it always did  */
    bool editFollowed(const QList<TextEdit>& edits, const QString& what,
                      const std::function<void(QJsonObject&, const QVector<int>&)>& patch);
    /*  The tree of the last run of a text is kept (by the text's md5 and by the program and library that made it): the next time the
     *  text is opened, the tree is there at once, before the script has run  */
    QString sceneCachePath(const QString& md5) const;
    void saveSceneCache(const QString& json, const QString& md5);
    QString m_savedSceneKey;            // (what was saved last, or shown from the cache: not written again)
    void expandTo(QTreeWidgetItem* row);

    /*  Actions (all become script edits)  */
    void toggleVisible(const QJsonObject& it);
    /*  Shows (or hides) all these models in one edit of the script (the eye of a "boundary conditions" row)  */
    void setVisibleAll(const QList<QJsonObject>& models, bool show);
    void reimport(const QJsonObject& it);
    void importMenu(const QJsonObject& it, const QPoint& global);
    /*  The edit raising the import call's rev= (false if it cannot be found)  */
    bool reimportEdit(const QJsonObject& it, QList<TextEdit>& edits);
    void showPart(const QJsonObject& imp, int part);
    void switchPart(const QJsonObject& imp, int part);
    void showOtherPart(const QJsonObject& imp, int part);
    /*  The item whose handles a part row edits: the part's variable's item
     *  (or the import statement's own variable); empty if the part is not
     *  in the script as a variable  */
    QJsonObject handlesTarget(const QJsonObject& imp, int part) const;
    /*  How a shape or part is edited by dragging in the view: the gizmo (handles()) or
     *  Studio's own handles on its surfaces (its var()s, or expose()'s).  The button toggles
     *  between them; setMode writes one into the script.  Whether it can be dragged at all is a
     *  switch of its own, the lock (`x = lock(x)`)  */
    void toggleEditMode(const QJsonObject& target);
    void toggleLock(const QJsonObject& target);
    QJsonObject selectedTarget() const;
    void setMode(const QJsonObject& target, const QString& mode, bool quiet = false);
    /*  The models a key (E, R, G, H) works on: the selected ones that can be edited by dragging  */
    QList<QJsonObject> editTargets() const;
    /*  One script edit for all of them (a gizmo mode "click", "never" or "always"; a lock, or unlock; the render cache on or off).
     *  `quiet`: no word under the tree when a model cannot be made draggable  */
    void applyModes(const QList<QJsonObject>& targets, const QString& mode, bool quiet = false);
    void applyLock(const QList<QJsonObject>& targets, bool lock);
    void applyCache(const QList<QJsonObject>& targets, bool on);
    /*  Which imports the edits of applyModes need (written once, not once for each model)  */
    struct ModeImports { bool handles = false, expose = false; };
    /*  The edits that give one model a mode; false (and the reason) when it cannot have it  */
    bool modeEdits(const QJsonObject& target, const QString& mode, QList<TextEdit>& edits, ModeImports& imports,
                   QString* why);
    /*  A displayed expression such as sphere(3) is given a name (`sphere_1 = sphere(3)`, and a line showing it),
     *  and `mode` -- "click", "never", "always", "lock" or "cache" -- is applied to the new variable once the script has
     *  run again; false if it cannot be named  */
    bool nameThen(const QJsonObject& target, const QString& mode);
    /*  A line of text under the tree (what could not be done)  */
    void notify(const QString& text);
    /*  The render cache button: a `name = render_cache(name)` line under the shape's definition (and under
     *  its handles and expose lines) when it is turned on, deleted when it is turned off  */
    void toggleCache(const QJsonObject& target);
    void setCacheButton(QTreeWidgetItem* row, const QJsonObject& target);
    /*  A part back to what the file says: its handles() and expose() lines are deleted and the
     *  import is read again (reimport)  */
    void reimportPart(const QJsonObject& imp, int part);
    /*  Deletes a shape's handles() line: back to where it was defined  */
    void removeHandles(const QJsonObject& target);
    /*  The delete button: the statements of a row's item (and the lines that show, hide and edit it) go
     *  from the script; asks first if the rest of the script still uses its name  */
    void deleteRow(QTreeWidgetItem* row);
    void deleteItem(const QJsonObject& it);
    /*  The same for several models at once: one edit, one question  */
    void deleteItems(const QList<QJsonObject>& items);
    /*  The edit deleting whole lines a..b (0-based), newline included  */
    TextEdit deleteLines(int a, int b) const;
    /*  Back to the file: deletes the import's cache and the handles() lines
     *  of its parts (after asking, if `ask`), then runs the script again  */
    void resetImport(const QJsonObject& imp, bool ask);
    QStringList visibleRoiExprs(int skipImportLine, int* endLine) const;
    /*  The edits making `it` the region of interest; false if it has no
     *  known extent  */
    bool roiEdits(const QJsonObject& it, QList<TextEdit>& edits) const;
    /*  The render region grows to hold the models that are new in the scene (those whose keys `before` does not have), when any of
     *  them reaches out of it: view.set_bounds is rewritten (or added) with whole numbers and a little room  */
    void growRegion(const QSet<QString>& before);
    QSet<QString> m_unfitted;       // (models that reach out of the region but could not be fitted yet: the region of the script is not known until its run has set it)
    bool visibilityEdit(const QJsonObject& it, bool show, QList<TextEdit>& edits) const;
    QJsonObject itemForVar(const QString& var) const;
    void focusOn(const QJsonObject& it);
    void select(QTreeWidgetItem* row, bool focus);

    /*  The model (a variable, or an expression displayed on its own) shown by a line; its name or text for a call,
     *  the line after which a statement using it goes, the line it starts on.  False if there is none  */
    bool resolveModel(int line0, QJsonObject* model, QString* source, int* after) const;
    /*  The same for a model already found: false if there is nothing to name it by  */
    bool describeModel(const QJsonObject& target, QString* source, int* after) const;
    /*  The row of the model that a variable holds is selected (once the script has run and it is in the tree)  */
    bool selectVar(const QString& var);
    /*  A name for a new model: base_1, base_2, ... whichever the script does not use  */
    QString freeName(const QString& base) const;
    /*  The model a menu operation falls back on when none is named: the selected one, else the last variable  */
    QJsonObject defaultModel() const;

    /*  A model a menu operation works on: its item, the text that names it in a call, the line after which a
     *  statement using it goes  */
    struct Model { QJsonObject item; QString source; int after = 0; };
    /*  The selected models, in the order they were selected (a variable, or an expression displayed on its own)  */
    QList<Model> selectedModels() const;
    /*  What a menu opened on the shape displayed by `line0` (negative: in empty space) works on: when several models
     *  are selected and that one is among them, all of them (in the order they were selected), else just that one
     *  (in empty space: the selected ones, else the last model)  */
    QList<Model> operandsFor(int line0, bool fallback = true) const;
    /*  Whether the entry is a simulation of the menu (static_analysis ...): it works on what is selected and writes a placeholder for what is
     *  not -- the part, the conditions -- so it is never greyed out for lack of them, and never takes "the last model" for its part  */
    bool isSimulation(const QString& operation) const;
    /*  Whether an operation of the menu takes several models (union, difference, intersection, surface_from_bodies)  */
    bool combines(const QString& operation) const;
    /*  Whether it cannot do without a second model (surface_from_bodies can: one model alone is its whole surface)  */
    bool needsSecond(const QString& operation) const;
    /*  Whether a primitive of the menu -- a support, a load, a thermal or flow condition -- is made for a body and the regions it acts on
     *  when several models are selected (otherwise it is a box laid at the cursor)  */
    bool takesBodies(const QString& primitive) const;
    /*  The (0-based) lines that show the selected rows, for highlighting them in the viewport  */
    QList<int> selectedLines() const;
    QList<int> rowLines(QTreeWidgetItem* row) const;
    /*  The row of the model displayed by a line  */
    QTreeWidgetItem* rowForLine(int line0) const;
    void onSelectionChanged();
    /*  The model a row stands for (a variable, or an expression displayed on its own); empty for the other rows  */
    QJsonObject modelOfRow(QTreeWidgetItem* row) const;

    /*  The multi-select state (two or more models selected) is a state of its own, not an edit mode of any model:
     *  the models are moved by one gizmo whatever way each of them is edited when selected alone, which is kept (and
     *  comes back when the selection is one model).  A locked model can be in it, but then the whole selection cannot be moved
     *  or edited (lockedInMulti; a popup says so when it comes about, unless `warn` is false, and a line under the tree);
     *  prepareSelection gives each selected model that has no numbers to move it by a gizmo line, in the mode it is in, and
     *  the numbers to pull its surfaces by (expose) when it is small  */
    bool lockedInMulti() const;
    void noteLockedInMulti(bool warn);
    void warnLockedMultiSelect();
    void prepareSelection();
    void updateProvisional();
    void updateFieldView();
    QString m_fieldKeyShown;
    /*  The lines that make a model's surfaces draggable (an expose() statement), indented like its definition; empty (and
     *  the reason) when it cannot be done.  exposeSurfaces writes them (the model's menu, for a shape too big to get them
     *  by being selected)  */
    QString exposeBlock(const QJsonObject& target, QString* why) const;
    void exposeSurfaces(const QJsonObject& target);
    QSet<QString> m_exposeTried;              // (the models prepareSelection has tried to expose: once each)
    void updateMultiNote();
    bool m_lockedInMulti = false;             // (the selection of several that is there has a locked model in it)
    bool m_multiNote = false;                 // (the note under the tree is the multi-select one)
    bool m_editPending = false;               // (an edit of prepareSelection has not been answered by a run yet)
    bool m_prepareAgain = false;
    int m_generation = 0;
    std::function<void()> m_afterRun;         // (a menu entry waiting for the run of prepareSelection's edit)
    QStringList m_reselect;                   // (keys to select once the run that named some expressions is done)
    int m_reselectTries = 0;
    QElapsedTimer m_editClock;
    QTimer m_prepareTimer;

    /*  The keys of the selected rows in the order they were selected; the modifiers of the last mouse click on the
     *  tree (Ctrl and Shift change the selection, they do not frame the camera)  */
    QStringList m_selectOrder;
    bool m_rebuilding = false;
    Qt::KeyboardModifiers m_clickMods;
    QStringList m_pressedSelection;     // (the keys of the rows selected when the mouse button last went down in the tree)
    mutable QHash<QString, bool> m_combining;
    mutable QHash<QString, bool> m_needsSecond;
    mutable QSet<QString> m_withBodies;
    mutable QSet<QString> m_simulations;

    /*  A variable to select once the script that creates it has run (tries counts the runs waited)  */
    QList<QPair<QString, QString>> m_copied;     // (the models of the last Ctrl+C: name, statements)
    QString m_copiedText;                        // (what the clipboard held then: a paste is of this only)
    QSet<QString> m_copiedPart;                  // (which of them were parts of an import)
    QString m_selectNew;
    int m_selectNewTries = 0;
    /*  Isolation: the keys of the models that were shown before, the one isolated, whether it is on  */
    bool m_isolated = false;
    QSet<QString> m_isolateRestore;
    QString m_isolatedKey, m_isolatedName;

    QList<int> linesOf(const QJsonObject& it) const;
    QString lineText(int line0) const;
    /*  An edit that comments out, rewrites or deletes a line that it found by the rows asks first whether the line IS what the rows say:
     *  the line that shows the model `var` (kind "display": `x`), that hides it ("hidden": `# hidden: x`), a statement that edits it
     *  ("lock", "handles", "expose", "render_cache", "custom_resolution": `x = lock(x)`), or its "definition".  Rows of another text than
     *  the script's (a scene that a run left behind, a prediction that was wrong) would have the edit land on a line that is something
     *  else -- a `lock(x)` line commented out as if it were a hidden model.  False says why in m_lineMismatch  */
    bool lineIs(int line0, const QString& var, const QString& kind) const;
    /*  The same for a displayed expression that has no name (its row has the text of the statement)  */
    bool lineIsExpression(int line0, const QJsonObject& it) const;
    mutable QString m_lineMismatch;
    QString m_refusedNote;              // (what a refused click said: said again when the run that brings the rows up to date is done)
    /*  Nothing is written: the notes say why, and the script is run again, which brings the rows up to date  */
    void refuseOutOfDate();
    QString uniqueName(const QString& base) const;
    void addImportIfMissing(QList<TextEdit>& edits, const QString& name,
                            const QString& module = "cad_import") const;

    /*  A mode to apply once a displayed expression has been given a name and the script has run again  */
    struct Pending { QString var, mode; int tries = 0; };
    Pending m_pending;

    QTreeWidget* m_tree;
    QToolButton* m_header;
    QString m_errorText;
    int m_errorLine = -1;
    QString headerText() const;
    QString errorNote() const;
    static void setErrorStyle(QWidget* w, bool on);
    void setWaitingStyle(QWidget* w, bool on);          // (what has to be done to run the script: amber, not red)
    QLabel* m_note;

    QJsonObject m_scene;
    QHash<int, QString> m_cacheStates;
    QString m_selectedKey;
    QHash<QString, bool> m_expandState;   // user's expand / collapse, by row key
    std::function<QString()> m_source;
    std::function<QString(const QString&, QString*)> m_expose;
    std::function<QString(const QString&, const QString&, QString*)> m_support;
    bool m_collapsed=false;
    QString spanText(const QJsonArray& span) const;
};

}   // namespace FielDes

Q_DECLARE_METATYPE(QList<FielDes::TextEdit>)
