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

#include <QMatrix4x4>
#include <QObject>
#include <QPropertyAnimation>

namespace FielDes {
class Camera : public QObject
{
    Q_OBJECT
public:
    Camera(QSize size);

    /*
     *  Triggers an animation of the perspective member variable
     */
    void toOrthographic();
    void toPerspective();

    /*
     *  Triggers an animation of the axis member variable
     */
    void toTurnZ();
    void toTurnY();

    /*
     *  Animates the camera so that it looks at the model from the given
     *  world-space direction (e.g. {0, -1, 0} = front view)
     */
    void lookFrom(QVector3D dir);

    /*
     *  Sets the mouse rotation sensitivity of the viewport 
     *  in degrees per windown size. (Angle rotated for 
     *  distance dragged across the screen, relative to the 
     *  size of the window)
     */
    void setRotationSensitivity(float sensitivity);

    /*
     *  Triggers an animation to zoom to the given setting
     */
    void zoomTo(const QVector3D& min, const QVector3D& max);

    /*
     *  Returns a matrix with perspective (if present), Z-flattening,
     *  and compensation for the window's aspect ratio.
     */
    QMatrix4x4 proj() const;

    /*
     *  Returns a matrix with rotation, scale, and center applied
     */
    QMatrix4x4 view() const;

    /*
     *  Complete transform matrix
     */
    QMatrix4x4 M() const;

    /*
     *  The rotation part of view() (no scale or translation)
     */
    QMatrix4x4 rotation() const;

    /*
     *  World-space unit vector from the scene towards the viewer
     */
    QVector3D towardViewer() const;

    /*
     *  Returns aspect ratio
     */
    float getAspect() const { return size.width() / float(size.height()); }

    /*  All QPoint coordinates are in window pixels  */
    void rotateIncremental(QPoint delta);
    void panIncremental(QPoint delta);
    void zoomIncremental(float ds, QPoint c);

    float getScale() const { return scale; }

    /*  Window size  */
    QSize size;

signals:
    void changed();
    void animDone();

protected:
    void animateAxis(QQuaternion end);

    float scale=0.5;
    QVector3D center={0,0,0};
    float pitch=128;
    float yaw=-59;
    float rotationSensitivity=360;

    float perspective=0.25;
    Q_PROPERTY(float perspective MEMBER perspective NOTIFY changed)

    QQuaternion axis;
    Q_PROPERTY(QQuaternion axis MEMBER axis NOTIFY changed)

    QPropertyAnimation anim;
};
} // namespace FielDes
