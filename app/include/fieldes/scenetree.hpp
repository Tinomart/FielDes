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

#include "fieldes/symmetry.hpp"

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

    /*  The statement that makes a shape's surfaces draggable (handles(...)), which only the
     *  interpreter can write: called with a variable's name, gives the text, or an empty
     *  text and the reason  */
    void setHandlesSource(std::function<QString(const QString&, QString*)> f) { m_handlesSource = f; }

    /*  A question to the interpreter (fieldes.app_support.<function>(arg) -> text; the reason in *error when it
     *  fails): the viewport menus' calls come from it  */
    void setSupport(std::function<QString(const QString&, const QString&, QString*)> f) { m_support = f; }

    /*  The editor's key for the file that is open (empty for a script with no file of its own): what is collapsed in the tree is kept
     *  for it  */
    void setFileKeySource(std::function<QString()> f) { m_fileKey = f; }

    /*  Whether the script has a model a menu operation can work on, and another one besides the model displayed
     *  by the (0-based) line `line0` (for the operations that combine two)  */
    bool hasModel() const;
    bool hasOtherModel(int line0) const;
    /*  Whether the model defined by the statement that starts on this (1-based) line is shown (its eye is on); true when the scene has no model there  */
    bool shownAtLine(int line1) const;
    /*  The variable of the model on the (0-based) line (empty: it has none -- an expression that is only shown), and what can be exported of it
     *  ("mesh", "fea", "results", "summary": see fieldes.stdlib.exporting, and the Export menu)  */
    QString varAtLine(int line0) const;
    QStringList exportsAtLine(int line0) const;
    /*  An Export entry of the menus: the model on the line is exported as `kind` ("mesh", "fea", "results", "field") in the file format `ext`  */
    void requestExport(int line0, const QString& kind, const QString& ext);

    /*  Whether what is selected (or the model on `line0`) can make this entry of a menu (kind "operation" or "primitive"): the library is asked
     *  to write the call for that selection, and an entry it refuses -- a part selected with supports and loads for a set of conditions,
     *  conditions without a part for a simulation ... -- is greyed out in the menu, without a word: it is not offered for a selection that
     *  cannot make it  */
    bool entryAllowed(const QString& kind, const QString& name, int line0) const;

    /*  The model that is selected, for the Primitives menu of the menu bar ("Add selection"): {name, type, line, end_line} of the first one, whose statement is on those lines
     *  (from 1); empty when nothing that is a model of the script is selected  */
    QJsonObject selectedModelFacts() const;
    /*  Whether what a menu on this line (-1: empty space) would work on is points and nothing else: the first menu of a point offers the
     *  field of the distance from it  */
    bool pointsOnly(int line0) const;
    /*  Whether the (0-based) line belongs to a model (a variable or a displayed expression that ran), and the middle of its
     *  bounds -- where a menu opened on it from outside the viewport takes its place from  */
    bool modelAtLine(int line0, QVector3D* centre = nullptr) const;
    /*  What kind of model the line is of ("solid", "field", ...: see fieldes.kinds), or "" when it is no model's  */
    QString typeAtLine(int line0) const;
    /*  Whether a (0-based) line is one of a model's own: where it is displayed, its statement, its handles and gizmo lines  */
    bool ownsLine(const QJsonObject& item, int line0) const;

    /*  The selection, in the order it was made (the first is the one a subtraction subtracts from): the keys of the
     *  selected rows (for scripted tests)  */
    QStringList selectionKeys() const { return m_selectOrder; }

    /*  For the guided tour and the tests: select the model a variable holds (once it is in the tree); the rectangle of a row (the
     *  first whose text starts with `prefix`) in the coordinates of `in`; the tree's viewport (where a mouse is sent to); whether the settings are open  */
    bool selectModel(const QString& var) { return selectVar(var); }
    QRect rowRect(const QString& prefix, QWidget* in);
    QWidget* treeViewport() const;
    int treeScroll() const;                      // (the vertical scroll of the tree: for tests)
    void showSettings(bool open);
    bool settingsOpen() const;
    /*  The render region a drag has grown to (the viewport grew it as the model was dragged out of it: View::regionGrown): written into the script, to be run with the
     *  numbers of the drag -- the region is never behind what is on the screen  */
    void growRegionTo(const QVector3D& lo, const QVector3D& hi);
    /*  For the guided tour: close or open the first row whose text starts with `prefix` (as the arrow in front of it does), and whether it
     *  is open; whether a section is closed; every section closed or opened; where the arrow in front of the first section is, and the
     *  property arrow at the end of a row (in the coordinates of `in`); whether the name of a section is being typed  */
    void setRowOpen(const QString& prefix, bool open);
    bool rowIsOpen(const QString& prefix) const;
    bool sectionClosed() const;
    void setSectionsOpen(bool open);
    QPoint sectionArrowPoint(QWidget* in);
    QPoint propertyArrowPoint(const QString& prefix, QWidget* in);
    /*  The property tab (it sticks out of the right edge of the panel, level with the hovered row of a model that has properties): the widget, when it is
     *  shown, and where it is for the first row whose text starts with `prefix` (in the coordinates of `in`), shown or not  */
    QWidget* propertyTab() const { return m_propTab; }
    QRect propertyTabRect(const QString& prefix, QWidget* in);
    /*  For the guided tour: every row that has rows under it is closed, except the models named here (and what holds a placeholder) -- for every
     *  tree that is built until showAll(); and where a row is, without opening or scrolling to it (empty when it is not in view)  */
    void showOnly(const QStringList& open);
    void showAll();
    QRect visibleRowRect(const QString& prefix, QWidget* in);
    /*  Where the first placeholder row, and the first section row, are (in the coordinates of `in`; empty when there is none in view)  */
    QRect placeholderRect(QWidget* in);
    QRect sectionRect(QWidget* in);
    /*  For the guided tour: where the circle with a plus shows that starts a section in the gap above the first row whose text starts with `prefix` (in `in`'s coordinates; null when
     *  that row is not shown)  */
    QPoint sectionGapPoint(const QString& prefix, QWidget* in);
    QPoint endGapPoint(QWidget* in);            // (the same, for the gap after the last row)
    bool isNaming() const;
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
    /*  The work plane, while its view is open: what the menu makes at a place is made in its axes (a profile's points are in the plane, not horizontal); `step` is
        the grid the places snap to, 0 when they do not  */
    void setWorkPlane(bool active, QVector3D origin, QVector3D u, QVector3D v, float step);
    /*  A line, a spline or a profile put down on the work plane (`name` is polyline, spline or profile): written into the script with its points, each a model of its own
        with its gizmo, as one step; `smooth` makes the points of a profile spline points  */
    /*  What was put down on the work plane: `points`, and -- `also` -- the strokes it has in the symmetry of the plane, each a shape of its own  */
    void createPlaced(QString name, QList<QVector3D> points, bool smooth);
    /*  The symmetry of the viewport (see symmetry.hpp): while it is on, what a menu or the work plane makes is made with its images (the library writes them)  */
    void setSymmetry(FielDes::Symmetry symmetry, int images);
    /*  A symmetry model made of the planes of `symmetry` (see symmetry_planes), written into the script and selected: it is what is used from then on  */
    void createSymmetry(FielDes::Symmetry symmetry);
    /*  The arguments of the call of the symmetry that is selected given new values (`planes`, `kind`): the card's buttons  */
    void setSymmetryArguments(QStringList names, QStringList values);
    /*  The name of the symmetry model that is selected, the one that is used (empty: none)  */
    QString activeSymmetryName() const { return m_activeSymVar; }
    void updateActiveSymmetry();
    /*  Puts the symmetry in use back as it was (none when `var` is empty): the guided tour does, when it undoes what a step showed -- selecting a symmetry makes it the one in use, and it
        stays when nothing is selected  */
    void setActiveSymmetry(const QString& var);
    void showNotice(const QString& text) { notify(text); }
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

    /*  A fold of the code editor was closed or opened at this (0-based) line: the row of the statement or the section that starts
     *  there follows it  */
    void editorFolded(int line0, bool folded);

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

    /*  Ctrl+E: the selected 2D shape is pulled into a solid -- the Extruding > extrude entry of the menus, written under it (`extrude(shape, 0, size)`, the size a round number of the
     *  shape's own size) -- or a word of why not (the selection is not something a 2D shape was meant)  */
    void extrudeSelected();

    /*  The INSERTION POINT: what is added from the tree (a new shape, a condition, a simulation) is written at the point, not always at the end of the script.  It is a gap between
     *  two rows of the top level -- the line the gap is drawn on in the tree, above the row it stands for --, set by a click on that gap (the text cursor of the editor goes to
     *  the line) or by the text cursor of the editor, which the user moves (this is called with its line and column: inside a statement the point is after it, on its first
     *  column before it, between statements above the next one).  Known by the row below it, so that it stays where it is however the script is edited; written code goes above
     *  that row, so the next thing added lands under it.  Not set (a script just opened): the end of the script, as it always was  */
    void setEditorCursor(int line0, int col0);

    /*  Shift+D: a copy of each selected model under it; Ctrl+C: the selected models (their statements) are kept, and
     *  Ctrl+V puts copies of them under the selected model, or at the end of the script -- with names that are free, and
     *  the copies referring to each other's copies.  Written like every other edit of the tree: one undoable step  */
    void duplicateSelected();
    /*  Ctrl+D: a LINKED copy of each selected model -- `copy = instance(model)`, a block of its own that is the model (it follows every change of it) and can only be
     *  moved and turned, written under the model.  The model is held by reference only, always (`# shadow: model`): it keeps its own row whether it is locked or not.
     *  One undoable step  */
    void duplicateLinkedSelected();
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

    /*  A click on the icon of a field in the viewport (a field is not a shape: it has no line that displays it): the field -- by its key, a
     *  variable's name or "line:N" -- is the selection, which opens its viewer; with `toggle` (Ctrl) it joins the selection or leaves it  */
    void selectFieldKey(const QString& key, bool toggle);

    /*  A selection rectangle that holds the icons of fields: those fields are the selection (with `add`, they join it)  */
    void selectFieldKeys(const QStringList& keys, bool add);

    /*
     *  Editing the points of a profile or a curve from the viewport: `op` is "insert" (a point on its outline, where the mouse pressed: `at`) or
     *  "toggle" (the point displayed by the line: a corner becomes a spline point and the other way).  `line0` is the 0-based line that displays
     *  the model.  Both are edits of the script; pointEditDone says which line displays the new point (-1 when none)
     */
    void editPoints(const QString& op, int line0, const QVector3D& press, const QVector3D& at, int index = -1);

    /*  A selection rectangle drawn in the viewport: the models displayed by these lines are the selection
     *  (with `add`, they join the selection that there is)  */
    void selectLines(QList<int> lines0, bool add);

    void setCollapsed(bool c);

    /*  A surface picked in the viewport: writes `name = select_surface(shape, seed=(x, y, z), ...)` and a
     *  line showing it under the definition of the shape displayed by the (0-based) line `line0`
     *  (the radius is mm, 0: no limit)  */
    void addSurfaceSelection(int line0, QVector3D seed, QString mode, double angle, double radius);

    /*  A corner or an edge of the model displayed by the (0-based) line `line0`, near a place that was picked on its surface (a right click): writes `name = corner_at(shape, (x, y, z))`
     *  (kind "corner": a point), `edge_at` (kind "edge": a line) or `edge_point_at` (kind "edge_point": a point of the edge), and a line showing it, under the definition of the shape  */
    void addFeature(int line0, QVector3D click, QString kind);

    /*  What the library finds near a place on the model displayed by `line0`, for the menu of a right click: {"corner": [x, y, z] or null, "edge": {"point", "straight"} or null}
        (the neighbourhood is `reach` mm); empty when the model is no variable or the search cannot be done  */
    QJsonObject featuresNear(int line0, QVector3D point, double reach) const;

    /*  The model displayed by the (0-based) line `line0` is given a resolution of its own: the line `x = custom_resolution(x, number)` is
     *  written under its definition (the number: the scene's resolution)  */
    void addCustomResolution(int line0);

    /*  What the render cache did for the shapes displayed by (0-based) lines: "kept" (meshed and kept),
     *  "read" (the mesh on screen came from the cache), "no" (cannot be kept), "on" (not meshed yet),
     *  with the words for the tooltip, as `state|text`  */
    void setCacheStates(const QHash<int, QString>& states);

signals:
    /*  The symmetry model that is selected (its lines, 0-based; none: no symmetry), whether it is another one than before (which is to use it), and its name  */
    void activeSymmetryChanged(QList<int> lines0, bool chosen, QString name);
    /*  Scroll the editor to a (0-based) line and flash it  */
    void goToLine(int line0);
    /*  A gap of the tree was clicked: the insertion point is there, and the editor's text cursor goes to the start of this (0-based) line -- or to the end of the script when the gap is the last one  */
    void insertionChosen(int line0, bool atEnd);
    /*  Select the placeholder (...) at a (0-based) place of the script, ready to be written over: the note under the tree and the rows of
     *  the placeholders point at the code editor, where they are filled in  */
    void placeholderRequested(int line0, int col0);
    /*  An argument row was clicked: its value in the code (from line0, col0 to endLine0, endCol0, 0-based) is selected, ready to be replaced  */
    void spanRequested(int line0, int col0, int endLine0, int endCol0);
    /*  The model of this variable is to be exported: as `kind` ("mesh", "fea", "results", "field"), in the format `ext` -- the window asks where  */
    void exportRequested(QString var, QString kind, QString ext);
    /*  A row was closed or opened in the tree: the code editor folds, or unfolds, the lines of its statement or section (the 0-based
     *  line they start on)  */
    void foldRequested(int line0, bool folded);

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

    /*  A point was put on an outline: the 0-based line that displays it once the edit is in the script (-1: nothing was done)  */
    void pointEditDone(int line0);

    /*  Frame the camera on a box; if the box is empty, on the shapes
     *  displayed by the given lines  */
    void focusRequested(QVector3D min, QVector3D max, QList<int> lines0);
    /*  The selected model has no gizmo yet (it gets its numbers when the script has run): where it will be, so that the view can
     *  draw it at once (see View::setProvisionalGizmo); `on` false when there is none to show  */
    void provisionalGizmo(bool on, QVector3D pivot, QList<int> lines0);
    /*  The modes of the gizmo buttons changed (gizmoModeOfLine): the view shows or hides the gizmos at once  */
    void gizmoModesChanged();

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
    /*  Shift+D and Ctrl+D: the models they act on (the selected ones; an import is its parts; an expression that has no name is given one first -- `mode` "copy" or "link" is
     *  done once the script has run), the plain copy of models with free names (a profile or a line with its own points), and the linked copy of a list of models: a model that
     *  cannot be linked is copied  */
    QList<QJsonObject> modelsToDuplicate(const char* mode);
    QList<TextEdit> plainCopyEdits(const QList<QJsonObject>& selected, QSet<QString>& taken, QStringList& chosen, QStringList& extra) const;
    void duplicateModels(const QList<QJsonObject>& items);
    void linkModels(const QList<QJsonObject>& items);
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

    void insertSettingLine(const QString& call, const QString& fn = QString(), bool live = false);
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
    /*  The item whose gizmo a part row edits: the part's variable's item
     *  (or the import statement's own variable); empty if the part is not
     *  in the script as a variable  */
    QJsonObject gizmoTarget(const QJsonObject& imp, int part) const;
    /*  How a shape or part is edited by dragging in the view: the gizmo (its placement arguments) or
     *  the handles on its surfaces (its var()s, or handles()'s).  The button toggles
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
    /*  Which imports the edits need (written once, not once for each model): `place` for a model that is placed by a statement  */
    struct ModeImports { bool gizmo = false, handles = false; };
    /*  The placement of a model -- position, rotation, scale, alpha (fieldes.stdlib.placement) -- written in the call that makes it, or in `x = place(x, ...)` under it when the
     *  call does not take them: the arguments in `set` (name, text) are written, the names in `remove` are taken away.  False when the text is not what the rows say  */
    bool placementEdits(const QJsonObject& target, const QList<QPair<QString, QString>>& set, const QStringList& remove, QList<TextEdit>& edits, ModeImports& imports,
                        const QString& before = QString());
public:
    /*  The mode of the gizmo button of the model displayed on the (0-based) line: 0 click, 1 never, 2 always, -1 when the button was never pressed.  It is the state of the button --
     *  the script does not say it --, kept by the name of the model; the view asks for it when it makes a shape  */
    int gizmoModeOfLine(int line0) const;
    /*  S: the selected faces, edges and points -- made on two bodies -- are snapped: the body of the first is moved onto the other by its own `position=` and `rotation=` (as the gizmo
     *  moves it), and the places the faces and points on it were made at go with it  */
    void snapSelected();
    /*  The (0-based) line that displays the first selected model, or -1 when no model is selected: what File > Export model works on  */
    int selectedModelLine() const;

protected:
    /*  The body a feature (a face, an edge, a point) is on: what it is made of, followed through the features made of features; empty when it is on none  */
    QString bodyOfFeature(const QJsonObject& item, int depth = 0) const;
    /*  Arguments added to the call at `call` ([line, column, end line, end column] as the outline gives it), before its closing parenthesis, laid out as FielDes lays calls out  */
    void appendCallArguments(const QJsonArray& call, const QStringList& args, QList<TextEdit>& edits) const;
    /*  An argument taken out of its call: its comma goes with it, and the line it has to itself  */
    void removeCallArgument(const QJsonArray& span, QList<TextEdit>& edits) const;
    /*  Whether the numbers of a model's gizmo are written in the script: its position, rotation or scale is made of var()s  */
    static bool placementHasNumbers(const QJsonObject& target);
    /*  A click on a placement row that is not written (or the number of the gizmo of a model that has none): the model is made ready for its gizmo, and the argument is selected
     *  in the code once the script has run  */
    void writePlacement(const QJsonObject& target, const QString& argument);
    /*  A displayed expression such as sphere(3) is given a name (`sphere_1 = sphere(3)`, and a line showing it),
     *  and `mode` -- "click", "never", "always", "lock" or "cache" -- is applied to the new variable once the script has
     *  run again; false if it cannot be named  */
    bool nameThen(const QJsonObject& target, const QString& mode);
    /*  Several displayed expressions named in one edit; `mode` ("copy", "link") is then applied to all of them and to the named models `together` with them  */
    bool nameAllThen(const QList<QJsonObject>& displays, const QStringList& together, const QString& mode);
    /*  A line of text under the tree (what could not be done)  */
    void notify(const QString& text);
    /*  The render cache button: a `name = render_cache(name)` line under the shape's definition (and under
     *  its gizmo and handles lines) when it is turned on, deleted when it is turned off  */
    void toggleCache(const QJsonObject& target);
    void setCacheButton(QTreeWidgetItem* row, const QJsonObject& target);
    /*  A part back to what the file says: its placement arguments and handles() lines are deleted and the
     *  import is read again (reimport)  */
    void reimportPart(const QJsonObject& imp, int part);
    /*  Deletes a shape's placement arguments: back to where it was defined  */
    void removeGizmo(const QJsonObject& target);
    /*  The delete button: the statements of a row's item (and the lines that show, hide and edit it) go
     *  from the script; asks first if the rest of the script still uses its name  */
    void deleteRow(QTreeWidgetItem* row);
    void deleteItem(const QJsonObject& it);
    /*  The same for several models at once: one edit, one question  */
    void deleteItems(const QList<QJsonObject>& items);
    /*  The edit deleting whole lines a..b (0-based), newline included  */
    TextEdit deleteLines(int a, int b) const;
    /*  Back to the file: deletes the import's cache and the placement arguments and handles() lines
     *  of its parts (after asking, if `ask`), then runs the script again  */
    void resetImport(const QJsonObject& imp, bool ask);
    QStringList visibleRoiExprs(int skipImportLine, int* endLine) const;
    /*  The edits making `it` the region of interest; false if it has no
     *  known extent  */
    bool roiEdits(const QJsonObject& it, QList<TextEdit>& edits) const;
    /*  Auto scale (view.set_bounds(..., auto_scale=True), the default): after every run, when a model reaches out of the render region,
     *  the numbers of view.set_bounds are rewritten in the script -- live, as the model is dragged or typed out -- with whole numbers and
     *  a little room (the line is added when the script has none).  The region only grows.  Off (auto_scale=False): it is what is written  */
    void fitRegion();
    /*  The call that sets the region, written with the six numbers `text` (low x y z, high x y z)  */
    void writeRegion(const QStringList& text, const double nlo[3], const double nhi[3]);
    QString m_lastRegionEdit;
    bool m_regionFitted = false;       // (the first scene of a file has been seen: from then on the region may also shrink)
    QElapsedTimer m_regionEditClock;
    /*  The Auto scale row of the render settings: writes (or takes out) `auto_scale=False` in the view.set_bounds line  */
    void toggleAutoScale();
    bool visibilityEdit(const QJsonObject& it, bool show, QList<TextEdit>& edits) const;
    /*  The points a profile, a polyline or a spline is made of that nothing else uses -- the ones that go with it when it is deleted  */
    QList<QJsonObject> ownedPointsOf(const QJsonObject& owner) const;
    /*  The edits that show or hide the points of `owner` (see ownedPointsOf) as the owner is shown or hidden; a line that has an edit already is left alone, and the points that
        were changed are added to `changed` (when it is given)  */
    void addPointsVisibility(const QJsonObject& owner, bool show, QList<TextEdit>& edits, QList<QJsonObject>* changed) const;
    QJsonObject itemForVar(const QString& var) const;
    void focusOn(const QJsonObject& it);
    void select(QTreeWidgetItem* row, bool focus);

    /*  The model (a variable, or an expression displayed on its own) shown by a line; its name or text for a call,
     *  the line after which a statement using it goes, the line it starts on.  False if there is none  */
    bool resolveModel(int line0, QJsonObject* model, QString* source, int* after) const;
    /*  The same for a model already found: false if there is nothing to name it by  */
    bool describeModel(const QJsonObject& target, QString* source, int* after) const;
    /*  The row of the model that a variable holds is selected (once the script has run and it is in the tree)  */
    /*  Selects the row of a model: that row alone, or (`add`) as one more of the selected.  The selection is set with explicit flags: the
     *  tree's own rule for a row it is told to make current looks at the keys held down at that moment (Shift or Ctrl, still down from
     *  the key that made the copy, would add the row to the old selection or toggle it)  */
    bool selectVar(const QString& var, bool add = false);
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
    /*  Whether the library writes the call of this entry for the models `operandsFor(line0)` gives (entryAllowed is this, and also true for an operation in the menu of empty space
     *  with nothing selected: it is written with a placeholder where the model goes)  */
    bool fitsOperands(const QString& kind, const QString& name, int line0) const;
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
    bool fromSelection(const QString& primitive) const;
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
     *  the numbers to pull its surfaces by (handles) when it is small  */
    bool lockedInMulti() const;
    void noteLockedInMulti(bool warn);
    void warnLockedMultiSelect();
    void prepareSelection();
    void updateProvisional();
    void updateFieldView();
    QString m_fieldKeyShown;
    /*  The lines that make a model's surfaces draggable (a handles() statement), indented like its definition; empty (and
     *  the reason) when it cannot be done.  addHandles writes them (the model's menu, for a shape too big to get them
     *  by being selected)  */
    QString handlesBlock(const QJsonObject& target, QString* why, bool* later = nullptr) const;
    void addHandles(const QJsonObject& target);
    QSet<QString> m_handlesTried;              // (the models prepareSelection has tried to expose: once each)
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
    mutable QSet<QString> m_fromSelection;      // (the entries that are made of the models selected: profile, curve, extrude, path_extrude)
    mutable QSet<QString> m_simulations;

    /*  A variable to select once the script that creates it has run (tries counts the runs waited)  */
    QList<QPair<QString, QString>> m_copied;     // (the models of the last Ctrl+C: name, statements)
    QString m_copiedText;                        // (what the clipboard held then: a paste is of this only)
    QSet<QString> m_copiedPart;                  // (which of them were parts of an import)
    QString m_selectNew;
    QStringList m_selectNewMore;                 // (a duplicate of several models: the other copies, selected with the last one)
    bool m_workPlaneActive = false;
    QJsonArray m_placed;                       // (the points of what createPlaced is making: read by createFromMenu)
    QJsonObject m_symmetryRequest;             // (the symmetry as the library takes it: the centre, and every image as a mirror and a turn; empty while it is off)
    QString m_activeSymVar;                    // (the symmetry model that is selected: what is made is made with its images while it is in use)
    QJsonObject m_symdefRequest;               // (the planes of a symmetry that is being made: planes, kind, centre, axis, first)
    bool m_placedSmooth = false;
    QVector3D m_workPlaneOrigin, m_workPlaneU, m_workPlaneV;
    float m_workPlaneStep = 0;
    int m_selectNewTries = 0;
    QPair<QString, QString> m_selectArg;            // (a placement argument written by a click, to select in the code once the script has run: the model's name, the argument)
    int m_selectArgTries = 0;
    QHash<QString, QString> m_gizmoModes;           // (the state of the gizmo buttons, by the name of the model: click, never, always)
    QHash<int, int> m_modeByLine;                   // (the same by the line that displays the model, for the view)
    void patchGizmoModes();
    void announceGizmoModes();
    /*  Isolation: the keys of the models that were shown before, the one isolated, whether it is on  */
    bool m_isolated = false;
    QSet<QString> m_isolateRestore;
    QString m_isolatedKey, m_isolatedName;

    QList<int> linesOf(const QJsonObject& it) const;
    QString lineText(int line0) const;
    /*  An edit that comments out, rewrites or deletes a line that it found by the rows asks first whether the line IS what the rows say:
     *  the line that shows the model `var` (kind "display": `x`), that hides it ("hidden": `# hidden: x`), a statement that edits it
     *  ("lock", "gizmo", "handles", "render_cache", "custom_resolution": `x = lock(x)`), or its "definition".  Rows of another text than
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
    struct Pending { QString var, mode; int tries = 0; QStringList together; };
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
    std::function<QString()> m_fileKey;
    QTimer m_expandSaveTimer;             // (what is collapsed is written a moment after it was changed)
    bool m_foldsPending = false;          // (a file was opened: the folds its saved state asks for are set in the editor with its first tree)
    bool m_foldSyncing = false;           // (a fold is being passed between the tree and the editor: its echo is not taken as a new one)
    struct SectionRename { bool active = false; int line = 0, col = 0, length = 0; QString original, lead, current; };
    SectionRename m_sectionRename;        // (the name of a section being typed: where in its comment it is written)
    int m_startSectionRename = -1;        // (the 0-based line of a section just written: its name is typed in as soon as its row is there)
    int m_startSectionTries = 0;
    std::function<QString()> m_source;
    std::function<QString(const QString&, QString*)> m_handlesSource;
    std::function<QString(const QString&, const QString&, QString*)> m_support;
    bool m_collapsed=false;
    QString spanText(const QJsonArray& span) const;

    /*  What is collapsed: a property of the file.  It is kept by the editor's key for the file and brought back when the file is opened;
     *  and it is tied to the code editor's folds in both directions -- the lines of a statement or of a section fold when its row closes,
     *  and its row closes when they are folded.  A row that holds a placeholder is never closed (that is where the user has to write)  */
    void noteExpansion(QTreeWidgetItem* row, bool open);
    int foldLineOf(QTreeWidgetItem* row) const;
    bool holdsPlaceholder(QTreeWidgetItem* row) const;
    void refoldInside(QTreeWidgetItem* row);
    void loadExpandState();
    void saveExpandState();
    void applyFolds();
    QString sectionKey(int index) const;

    /*  Sections: `# SECTION name` comments, rows of their own.  A new one is written in the gap between two rows, and its name typed
     *  in the row; a double click renames one (the comment is rewritten as it is typed)  */
    void createSectionBefore(QTreeWidgetItem* below);
    void createSectionAtEnd();                  // (the gap after the last row: a section at the end of the script)
    /*  The line (0-based) of the script a gap above this row is on: the first line of the statements the row holds (what it is made of is defined above it), or of a section's heading  */
    int gapLine(QTreeWidgetItem* below) const;
    /*  What a gap is known by: the row below it -- a model by its key, a section by its name  */
    QString gapKey(QTreeWidgetItem* row) const;
    /*  The row the insertion point is above (`end`: it is at the end of the tree, no row), and whether it was set (it is drawn faint while it is only the default, the end)  */
    QTreeWidgetItem* insertionRow(bool* end, bool* active) const;
    /*  The (0-based) line what the tree adds is written before; -1: at the end of the script (not set, at the end, the row is gone, the rows are not of the text in the editor, or
     *  the line is inside a block of code that cannot take a statement)  */
    int insertionLine(const QStringList& lines) const;
    void placeInsertion(const QString& key, bool atEnd);
    /*  Lines were written above the row the point is above: a block of Python is known by the line it starts on, so its key moves down with them (a model's does not change)  */
    void shiftInsertKey(int lines);
    /*  A block of plain Python (a row of kind `python`) is dragged on its own: it is moved among the rows of the top level, its lines moved in the script (one undoable edit).
     *  Dropped above or below `over` (`at`: DropAbove / DropBelow); what is inside a block, or inside a model, is locked: nothing is dropped there  */
    void movePythonBlock(const QJsonObject& block, QTreeWidgetItem* over, int at);
    /*  The question when a move of Python would break the script (a name used above the statement that makes it, a function called above its def): `check` is what Python
     *  answered (move_check: the problems and the statements that have to go along), `along` their labels.  True: move them along; false: leave the script as it is  */
    bool confirmPythonMove(const QString& what, const QJsonObject& check, const QStringList& along);
    QJsonObject m_dragPython;                   // (the block being dragged; empty: the drag is of models)
    bool m_insertSet = false;
    QString m_insertKey;                        // (the key of the row the point is above; empty: the end)

    /*  The property arrow (the end of a row that is hovered, when the model has properties: the fields and numbers of a result) opens a
     *  drawer that lists them; one that is clicked is written as a new model of its own, under the statement: `stress_1 = result.von_mises`  */
    void openProperties(QTreeWidgetItem* row, const QPoint& global);
    void addProperty(const QJsonObject& item, const QString& property);
    QPointer<QFrame> m_drawer;
    QPointer<QWidget> m_propTab;                // (the tab that opens the drawer)
    QString m_propTabKey;                       // (the row it is for)
    QTimer m_propTabHide;                       // (it goes a moment after the mouse leaves the row and the tab)
    void showPropertyTab(QTreeWidgetItem* row);
    void placePropertyTab();
    void hidePropertyTab();
    bool m_showOnly = false;                    // (a step of the tour: only the rows it is about are open)
    QStringList m_showOnlyOpen;
    void applyShowOnly();
    void liveRenameSection(QTreeWidgetItem* row, const QString& typed);
    void endSectionRename(bool keep);
};

}   // namespace FielDes

Q_DECLARE_METATYPE(QList<FielDes::TextEdit>)
