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

#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLFunctions>

namespace FielDes {
class Arrow : public QOpenGLFunctions
{
public:
    Arrow();

    void draw(QMatrix4x4 M, QVector3D pos, float scale,
              QVector3D norm, QColor color);
    void initializeGL(int res=32);

protected:
    QOpenGLBuffer vbo;
    QOpenGLVertexArrayObject vao;

    unsigned tri_count=0;
};

/*
 *  The symbols of boundary conditions, drawn over the model the way structural analysis programs draw them: an arrow
 *  (a force, gravity) and a pad (a support: a low disc lying on the surface).  Both are modelled along +z in a unit
 *  box, the arrow from its tail at z=0 to its tip at z=1, the pad from the surface at z=0 up to its flat top.
 */
class Glyphs : public QOpenGLFunctions
{
public:
    void initializeGL();

    /*  An arrow of this length along `dir`; `pos` is its tip when tipAtPos, else its tail  */
    void drawArrow(const QMatrix4x4& M, QVector3D pos, float length, QVector3D dir, QColor color, bool tipAtPos);
    /*  A pad of this diameter lying on the surface at `pos`, its axis along `normal` (out of the surface)  */
    void drawPad(const QMatrix4x4& M, QVector3D pos, float diameter, QVector3D normal, QColor color);

protected:
    void draw(QMatrix4x4 M, QVector3D origin, float size, QVector3D dir, QColor color, int first, int count);

    QOpenGLBuffer vbo;
    QOpenGLVertexArrayObject vao;
    int arrow_verts = 0, pad_verts = 0;
};

}   // namespace FielDes
