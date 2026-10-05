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
#include <QJsonObject>
#include <QList>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QVector3D>

class QTreeWidget;
class QTreeWidgetItem;
class QToolButton;
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

    /*  The selection, in the order it was made (the first is the one a subtraction subtracts from): the keys of the
     *  selected rows (for scripted tests)  */
    QStringList selectionKeys() const { return m_selectOrder; }

public slots:
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

    /*  While the script is still running: the models its finished statements have made so far (the rows are there as soon
     *  as their statement is done; the whole scene follows when the script is)  */
    void setPartialScene(const QString& json);

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
     *  (the thickness and the radius are mm, 0: automatic / no limit)  */
    void addSurfaceSelection(int line0, QVector3D seed, QString mode, double angle, double thickness,
                             double radius);

    /*  What the render cache did for the shapes displayed by (0-based) lines: "kept" (meshed and kept),
     *  "read" (the mesh on screen came from the cache), "no" (cannot be kept), "on" (not meshed yet),
     *  with the words for the tooltip, as `state|text`  */
    void setCacheStates(const QHash<int, QString>& states);

signals:
    /*  Scroll the editor to a (0-based) line and flash it  */
    void goToLine(int line0);

    /*  Apply these edits to the script as one undoable step  */
    void editScript(QList<TextEdit> edits, QString description);

    /*  Run the script again (nothing in its text changed)  */
    void rerunRequested();

    /*  Highlight the shapes displayed by these (0-based) lines  */
    void highlightLines(QList<int> lines0);

    /*  Frame the camera on a box; if the box is empty, on the shapes
     *  displayed by the given lines  */
    void focusRequested(QVector3D min, QVector3D max, QList<int> lines0);

protected:
    bool eventFilter(QObject* obj, QEvent* e) override;
    void rebuild();
    void onItemClicked(QTreeWidgetItem* item, int column);
    void onItemDoubleClicked(QTreeWidgetItem* item, int column);
    void onContextMenu(const QPoint& pos);

    /*  Actions (all become script edits)  */
    void toggleVisible(const QJsonObject& it);
    void reimport(const QJsonObject& it);
    /*  The edit raising the import call's rev= (false if it cannot be found)  */
    bool reimportEdit(const QJsonObject& it, QList<TextEdit>& edits);
    void showPart(const QJsonObject& imp, int part);
    void switchPart(const QJsonObject& imp, int part);
    void setAllParts(const QJsonObject& imp, bool show);
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
    void addModeActions(QMenu& menu, const QJsonObject& target);
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
    QList<Model> operandsFor(int line0) const;
    /*  Whether an operation of the menu combines models (union, difference, intersection)  */
    bool combines(const QString& operation) const;
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
     *  comes back when the selection is one model).  A locked model cannot be in it:
     *  stripLocked deselects the locked ones of a selection of several (with a warning, unless `warn` is false);
     *  prepareSelection gives each selected model that has no numbers to move it by a gizmo line, in the mode it is in, and
     *  the numbers to pull its surfaces by (expose) when it is small  */
    bool stripLocked(bool warn);
    void warnLockedMultiSelect();
    void prepareSelection();
    /*  The lines that make a model's surfaces draggable (an expose() statement), indented like its definition; empty (and
     *  the reason) when it cannot be done.  exposeSurfaces writes them (the model's menu, for a shape too big to get them
     *  by being selected)  */
    QString exposeBlock(const QJsonObject& target, QString* why) const;
    void exposeSurfaces(const QJsonObject& target);
    QSet<QString> m_exposeTried;              // (the models prepareSelection has tried to expose: once each)
    void updateMultiNote();
    bool m_stripping = false;
    bool m_silentStrip = false;               // (a model locked by a key leaves the selection without a warning)
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
    mutable QHash<QString, bool> m_combining;

    /*  A variable to select once the script that creates it has run (tries counts the runs waited)  */
    QString m_selectNew;
    int m_selectNewTries = 0;
    /*  Isolation: the keys of the models that were shown before, the one isolated, whether it is on  */
    bool m_isolated = false;
    QSet<QString> m_isolateRestore;
    QString m_isolatedKey, m_isolatedName;

    QList<int> linesOf(const QJsonObject& it) const;
    QString lineText(int line0) const;
    QString uniqueName(const QString& base) const;
    void addImportIfMissing(QList<TextEdit>& edits, const QString& name,
                            const QString& module = "cad_import") const;

    /*  A mode to apply once a displayed expression has been given a name and the script has run again  */
    struct Pending { QString var, mode; int tries = 0; };
    Pending m_pending;

    QTreeWidget* m_tree;
    QToolButton* m_header;
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
