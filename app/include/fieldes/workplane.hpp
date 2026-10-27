/*
FielDes: field-driven design

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/
#pragma once

#include <QFrame>
#include <QString>
#include <QVector3D>

class QDoubleSpinBox;
class QLabel;
class QSpinBox;
class QToolButton;

namespace FielDes {

namespace Units { class LengthSpinBox; }

/*
 *  The work plane: a plane to put points in.  It is a tool of the viewport, not a model of the script -- what is put on it is
 *  written into the script as the points' own numbers, and the plane itself is not.  While its view is open the plane is drawn
 *  with a grid, the places of new models and the dragging of points follow it, and they snap to the grid (and to the points
 *  that are there)
 */
struct WorkPlane
{
    bool exists = false;            // a plane has been made (it is kept while its view is closed)
    bool open = false;              // the work plane view is open: the plane and its grid are drawn, its card is shown, places follow it
    QVector3D base{0, 0, 0};        // where it was made; its offset is along its normal from here
    float offset = 0;
    QVector3D u{1, 0, 0};           // its two axes, unit vectors; the normal is u x v
    QVector3D v{0, 1, 0};
    bool grid = true;               // the grid is drawn
    bool snap = true;               // places snap to the grid
    bool snapPoints = true;         // places snap to the points that are there
    float spacing = 0;              // the step of the grid in mm; 0: automatic, from how large the picture is
    int mode = 0;                   // what a click does: 0 nothing of its own (things are selected, moved, turned and scaled as usual: Q), 1 puts down the points of a line, 2 of a spline, 3 a point (E goes round 1-3)

    QVector3D normal() const { return QVector3D::crossProduct(u, v).normalized(); }
    QVector3D origin() const { return base + normal() * offset; }
};

/*
 *  The snapping mode (Tab in the viewport): a grid of the whole space, a lattice of lines a step apart whose nodes are the places that everything that is moved goes to --
 *  models dragged by their arrows or squares, the faces and points that are pulled, what a click puts down -- and a step of angle that every turn is made in.  It is a tool of
 *  the viewport like the work plane, and the work plane view switches it off while it is open (that plane has a grid of its own) and back on when it is left
 */
struct SnapGrid
{
    bool on = false;                // the mode is on
    bool show = true;               // the lattice is drawn
    float spacing = 0;              // the step of the lattice in mm; 0: automatic, from how large the picture is
    float angle = 5.0f;             // the step of a turn in degrees: every turn while the mode is on, and the turn of the Shift gizmo whether it is on or not
    int distance = 135;             // the viewing distance of the lattice, in per cent of the distance from the viewer to the place the view looks at: the lattice is seen out to there and fades out towards it
                                    // (a share of the way to what you look at, and not millimetres: the picture of it is the same at every zoom and every pan)
    bool snapPoints = true;         // what is put down or pulled also goes onto the points that are there
};

/*
 *  The card of the work plane view, in the viewport's top-right corner (styled like the section card): the grid and the snapping,
 *  the plane's offset along its normal, and what to do with the plane: face it, make it from the view, make a new one
 */
class WorkPlanePanel : public QFrame
{
    Q_OBJECT
public:
    explicit WorkPlanePanel(QWidget* parent = nullptr);

public slots:
    /*  The state of the plane, from the view; `step` is the grid's step now (when it is automatic, what it works out to)  */
    void setPlane(const WorkPlane& plane, float step);
    /*  Where the cursor is on the plane (empty: not on it)  */
    void setReadout(const QString& text);
    /*  The units were changed (Settings > Units): the boxes of the step and the offset show them in the unit chosen  */
    void refreshUnits();

signals:
    void optionsChanged(bool grid, bool snap, bool snapPoints, float spacing);
    void modeChanged(int mode);
    void offsetChanged(float offset);
    void faceRequested();
    void fromViewRequested();
    void newRequested();
    void closeRequested();

protected:
    bool eventFilter(QObject* obj, QEvent* e) override;
    void showEvent(QShowEvent* e) override;
    void place();
    void emitOptions();

    QToolButton* m_header;
    QToolButton* m_close;
    QWidget* m_body;
    QToolButton* m_grid;
    QToolButton* m_snap;
    QToolButton* m_snapPoints;
    QToolButton* m_auto;
    QToolButton* m_modes[3];            // (line, spline, point: what a click puts down -- none on: not drawing)
    QLabel* m_hint = nullptr;           // (what a click does now)
    Units::LengthSpinBox* m_step;
    Units::LengthSpinBox* m_offset;
    QLabel* m_info;
    QLabel* m_readout;
    bool m_collapsed = false;
    bool m_updating = false;
};

/*
 *  The card of the snapping mode, in the viewport's top-right corner like the work plane's: whether the lattice is drawn, its step (automatic or a number, halved and doubled with
 *  a click), the step of a turn, the viewing distance of the lattice, and whether the points that are there are snapped to as well
 */
class GridPanel : public QFrame
{
    Q_OBJECT
public:
    explicit GridPanel(QWidget* parent = nullptr);

public slots:
    /*  The state of the mode, from the view; `step` is the lattice's step now (when it is automatic, what it works out to)  */
    void setGrid(const SnapGrid& grid, float step, bool suspended);
    /*  The units were changed (Settings > Units): the box of the step shows them in the unit chosen  */
    void refreshUnits();

signals:
    void optionsChanged(bool show, float spacing, float angle, int distance, bool snapPoints);
    void closeRequested();

protected:
    bool eventFilter(QObject* obj, QEvent* e) override;
    void showEvent(QShowEvent* e) override;
    void place();
    void emitOptions();

    QToolButton* m_header;
    QToolButton* m_close;
    QWidget* m_body;
    QToolButton* m_show;
    QToolButton* m_snapPoints;
    QToolButton* m_auto;
    QToolButton* m_half;
    QToolButton* m_double;
    Units::LengthSpinBox* m_step;
    QDoubleSpinBox* m_angle;
    QSpinBox* m_distance;
    QLabel* m_hint;
    bool m_collapsed = false;
    bool m_updating = false;
};

}   // namespace FielDes
