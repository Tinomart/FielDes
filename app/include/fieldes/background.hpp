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
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLFunctions>

namespace FielDes {

// The backdrop of the viewport: a quad over the whole of it, dark teal to a lighter teal.  It is drawn with a shader of its own that has no uniforms and no state of the shapes' to depend on:
// drawn with the shapes' shader (the clip planes, the section's colour, the patch mode that it reads) it came out BLACK under Mesa's software renderer (WSLg), where the shapes themselves were fine
class Background : public QOpenGLFunctions
{
public:
    void draw();
    void initializeGL();

protected:
    QOpenGLBuffer vbo;
    QOpenGLVertexArrayObject vao;
    QOpenGLShaderProgram program;
    bool ready = false;
};
} // namespace FielDes
