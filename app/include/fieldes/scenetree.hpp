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

#include <QFrame>
#include <QHash>
#include <QJsonObject>
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

public slots:
    void setScene(const QString& json);

    /*  How the selected model is edited by dragging: the next of gizmo / handles / lock, or one of them  */
    void cycleSelectedMode();
    void setSelectedMode(const QString& mode);

    /*  Selects the item displayed by the given (0-based) statement line,
     *  e.g. after a click on a shape in the viewport */
    void selectByLine(int line0);

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
    /*  How a shape or part is edited by dragging in the view: the gizmo (handles()),
     *  Studio's own handles on its surfaces (its var()s, or expose()'s) or none
     *  (a lock).  The button cycles them; setMode writes one into the script  */
    void cycleMode(const QJsonObject& target);
    QJsonObject selectedTarget() const;
    void setMode(const QJsonObject& target, const QString& mode);
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
    bool m_collapsed=false;
    QString spanText(const QJsonArray& span) const;
};

}   // namespace FielDes

Q_DECLARE_METATYPE(QList<FielDes::TextEdit>)
