/*
FielDes: field-driven design
Derived from Studio, a simple GUI for the libfive CAD kernel
Copyright (C) 2021  Matt Keeter

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
#include <QList>

#include "fieldes/section.hpp"
#include "fieldes/settings.hpp"
#include "fieldes/shape.hpp"

#include "libfive/tree/tree.hpp"

namespace FielDes {

struct Error {
    QString error;
    QRect range;
};

/*  A field model of the script (not drawn: the section viewer shows it when it is selected): `key` is the variable's name,
 *  or "line:N" for a field that is only displayed at the (1-based) line N  */
struct FieldEntry {
    QString key;
    FieldSource source;
    QVector3D centre;               // where the field is about (the point or the middle of the body it was made from) ...
    bool hasCentre = false;         // ... when it is known; else the field viewer starts at the origin
};

struct Result {
    /*  Sets whether result or error is valid */
    bool okay;

    /* A valid result, which should be shown in the GUI */
    QString result;

    /*  An error, which should be shown in the GUI and highlighted */
    Error error;

    /*  All of the other things which a valid script can produce */
    Settings settings;
    QList<Shape*> shapes;
    QList<FieldEntry> fields;
    QMap<libfive::Tree::Id, QRect> vars;

    /*  The text of the script this result is of: the positions in `vars` (and in `scene`) are positions in THAT text.  The text in the editor
     *  can be another one by the time the result is there (a click of the tree, a key): see Editor::setVarSpans  */
    QString script;

    /*  Warnings to be drawn in the GUI, along with quick-fixes.  This is used
     *  when the script does not define bounds, resolution, etc. */
    QList<QPair<QString, QString>> warnings;

    /*  JSON description of the script for the model tree (see
     *  fieldes/app_support.py:scene_json), or empty  */
    QString scene;

    /*  The (1-based) line of the breakpoint the script stopped before, or -1
     *  when it ran to the end.  The rest of the result is what the
     *  statements before it produced. */
    int pausedLine = -1;
};

} // namespace FielDes
