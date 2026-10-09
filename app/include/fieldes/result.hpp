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

/*  A number of the script that a run put right: replace this span of the text (0-based) by `text` -- the editor does, once the run is over  */
struct Resync {
    int line0, col0, line1, col1;
    QString text;
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

    /*  The (1-based) line of the breakpoint the script stopped before, or -1
     *  when it ran to the end.  The rest of the result is what the
     *  statements before it produced. */
    int pausedLine = -1;

    /*  When the script stopped before a statement that has a placeholder (`...` where an argument goes): how many it has (pausedLine is
     *  its line) and the statement's first line.  Such a run cannot be continued: the placeholder has to be filled in  */
    int holeCount = 0;
    QString holeText;

    /*  The run was stopped by an edit of the script (the empty Exception that halt() raises in it): a newer run is on its way, and this one
     *  shows nothing -- not even that stop as an error  */
    bool replaced = false;

    /*  Numbers of the script the run put right: a number edited in the call of a shape whose dragged numbers are listed below it is the number
     *  the shape has, and the list says so (see Resync)  */
    QList<Resync> resync;
};

} // namespace FielDes
