/*
FielDes: field-driven design

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/
#pragma once

#include <QFrame>
#include <QList>
#include <QMatrix4x4>
#include <QString>
#include <QVector3D>

class QDoubleSpinBox;
class QLabel;
class QSpinBox;
class QToolButton;

namespace FielDes {

class Shape;

/*
 *  One image of the symmetry: where a point goes.  Every image is a turn, a mirror image or both -- a mirror image across a plane, then a turn about an axis through the
 *  centre -- and it is kept that way (`mirror`, `axis` + `degrees`), because that is how the script writes it: symmetry_image(shape, centre, mirror=..., turn=...)
 */
struct SymmetryElement
{
    QMatrix4x4 map;                  // takes a point to its image
    bool improper = false;           // a mirror image: it turns the way round
    QVector3D mirror;                // the normal of the plane the first part mirrors across (zero: none)
    QVector3D axis{0, 0, 1};         // the axis of the turn that follows
    float degrees = 0;               // the turn (0: none)
    QVector3D linear(const QVector3D& v) const { return map.mapVector(v); }
};

/*
 *  The symmetry the viewport works with, in 3D: planes of mirror symmetry through one centre.  Whatever is made while it is on is made at its images too, and the images
 *  follow it (the script keeps them as symmetry_image(...) of the original).  It is a tool of the viewport, as the work plane is: it is not a model of the script.
 *
 *  The planes are of three arrangements, round an axis `c`: `Axis` -- `count` planes that all contain the axis, equally spaced; `AxisAndAcross` -- the same, and the
 *  plane across the axis; `Cube` -- the nine planes of a cube whose axes are `a`, `b`, `c` (three across them, six diagonal).  `a` is a line of the first plane, `b` is c x a.
 *  The fourth, `Turn`, has no planes: `count` copies (1 to 24) equally spaced round the axis, each the one before it turned by 360 / count degrees, and nothing mirrored
 */
struct Symmetry
{
    enum Kind { Axis = 0, AxisAndAcross = 1, Cube = 2, Turn = 3 };
    bool on = false;
    int kind = Axis;
    int count = 1;                          // the planes about the axis (Axis, AxisAndAcross): 1 to 8; the copies round it (Turn): 1 to 24
    QVector3D centre{0, 0, 0};
    QVector3D a{0, 1, 0};                   // a line in the first plane, across the axis
    QVector3D c{0, 0, 1};                   // the axis

    QVector3D b() const { return QVector3D::crossProduct(c, a); }
    /*  The same frame with `a` made across `c` and unit length, and `c` unit  */
    void normalise();
    /*  The mirror planes: the unit normals of the planes through the centre  */
    QList<QVector3D> planeNormals() const;
    /*  Every image but the identity, in a fixed order (the turns, the mirror images across the planes about the axis, then what the plane across the axis adds)  */
    QList<SymmetryElement> elements() const;
    /*  The point `p` is on a mirror plane (within `tol`)  */
    bool onPlane(const QVector3D& p, float tol) const;
    /*  The element that a matrix of the turns and mirror images (about the origin) is: its mirror, its turn; the translation is the centre's  */
    SymmetryElement element(const QMatrix4x4& linear) const;
    /*  The same symmetry as `o` (the planes and the centre; the arrangement's name may differ)  */
    bool operator==(const Symmetry& o) const;
    bool operator!=(const Symmetry& o) const { return !(*this == o); }
    /*  A frame with `c` as its axis, and the line `a` as near as can be to `hint` (any direction when `hint` is along `c`)  */
    static void frameFrom(const QVector3D& axis, const QVector3D& hint, QVector3D& a, QVector3D& c);
    /*  The angle of `a` about `c` from a reference direction in the plane across `c` (the world's x, or y when c is along x): what the card shows and sets  */
    float angleOfFirst() const;
    void setAngleOfFirst(float degrees);
};

/*
 *  The card of the symmetry, in the viewport's top-right corner (as the work plane's): how many planes and how they are arranged, which way the axis goes (the world's axes,
 *  the work plane's normal, the view), the turn of the first plane and the centre
 */
class SymmetryPanel : public QFrame
{
    Q_OBJECT
public:
    explicit SymmetryPanel(QWidget* parent = nullptr);

public slots:
    /*  The symmetry that is selected: `exists` (the card is there while one is), whether it is in use, and its name  */
    void setSymmetry(const Symmetry& s, int images, bool exists, bool inUse, const QString& name);
    /*  Where the card sits: top-right, under the other cards  */
    void reposition() { place(); }

signals:
    void kindChanged(int kind);
    void countChanged(int count);
    void axisRequested(int which);          // (0 x, 1 y, 2 z, 3 the work plane's normal, 4 the view's up, 5 the selected shape's plane)
    void angleChanged(float degrees);
    void centreChanged(QVector3D centre);
    void centreRequested(int which);        // (0 the origin, 1 the middle of what is selected, 2 the work plane's origin)
    void useChanged(bool on);               // (what is made is made at the images of the symmetry)
    void closeRequested();

protected:
    bool eventFilter(QObject* obj, QEvent* e) override;
    void showEvent(QShowEvent* e) override;
    void place();

    QToolButton* m_header;
    QToolButton* m_close;
    QWidget* m_body;
    QToolButton* m_kind[4];
    QToolButton* m_count[8];
    QSpinBox* m_copies = nullptr;           // (Turn: how many copies, up to 24)
    QLabel* m_countLabel = nullptr;
    QLabel* m_angleLabel = nullptr;
    QDoubleSpinBox* m_angle;
    QDoubleSpinBox* m_centre[3];
    QLabel* m_info;
    QToolButton* m_use;
    bool m_collapsed = false;
    bool m_updating = false;
};

}   // namespace FielDes
