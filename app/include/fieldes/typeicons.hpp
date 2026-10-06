/*
FielDes: field-driven design
Derived from Studio, a simple GUI for the libfive CAD kernel

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/
#pragma once

#include <QColor>
#include <QIcon>
#include <QJsonObject>
#include <QString>

namespace FielDes {

/*
 *  What a model is shown as: an icon of its own colour and shape for each kind of thing a script makes -- the model tree
 *  and the right-click menus use the same ones.  The kinds are those of fieldes/kinds.py:
 *
 *      solid (a 3D shape), profile (a 2D shape), field, surface, point, simulation, conditions (what an analysis is
 *      given), cell (what a lattice is made of), selection, import, and block (an entry of a custom block)
 */
namespace TypeIcons {

/*  The icon of a kind; `block` adds the mark a model made by a custom block carries.  A kind that is not known is a solid  */
QIcon icon(const QString& type, bool block = false);

/*  The colour and the words for a kind (for tooltips)  */
QColor color(const QString& type);
QString label(const QString& type);

/*  The colours and words the interpreter says the kinds have ({kind: {label, color}}, the "kinds" of the model tree's scene
 *  and of the menus' catalog); what it does not say stays as it is  */
void setKinds(const QJsonObject& kinds);

}   // namespace TypeIcons

}   // namespace FielDes
