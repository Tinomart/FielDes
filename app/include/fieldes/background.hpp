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
    /*  Lets go of the buffers of a graphics context that is ending (a viewport pulled out into a window of its own is given a new one)  */
    void releaseGL() { if (vao.isCreated()) vao.destroy(); if (vbo.isCreated()) vbo.destroy(); delete program; program = nullptr; ready = false; failed = false; }

protected:
    /*  Makes the buffers and the program in the context that is current: what an earlier context left in them goes first  */
    void build();
    QOpenGLBuffer vbo;
    QOpenGLVertexArrayObject vao;
    // (made again for each graphics context: a program remembers the context it was first made in, and its shaders removed and added again in another one do not link -- measured in a viewport pulled
    // out into a window of its own, where the backdrop was black: "did not link", with nothing in the log)
    QOpenGLShaderProgram* program = nullptr;
    bool ready = false;
    bool failed = false;                   // (the program did not link in this context: it is not tried again at every frame)
};
} // namespace FielDes
