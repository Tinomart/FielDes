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

/*  The icon of a kind; `block` adds the mark a model made by a custom block carries.  A kind that is not known is a solid.  `domain` (structural, thermal, fluid)
 *  is the engineering domain of a simulation and of what an analysis is given: each has the colour and the picture of its domain.
 *
 *  `primitive` is the name of the primitive the model is (`sphere`, `torus`, `fixed`, `steel`, `cell_gyroid` ... : the names of the primitives of the menus, a cell
 *  as `cell_` and its kind): each has a picture of its own, in the colour of its kind.  What has none -- a name that is not a primitive, an empty one -- has the picture
 *  of its kind  */
QIcon icon(const QString& type, bool block = false, const QString& domain = QString(), const QString& primitive = QString());

/*  The colour and the words for a kind (for tooltips); the colour of a simulation or of its conditions is that of their domain  */
QColor color(const QString& type, const QString& domain = QString());
QString label(const QString& type);

/*  The colours and words the interpreter says the kinds have ({kind: {label, color}}, the "kinds" of the model tree's scene
 *  and of the menus' catalog); what it does not say stays as it is  */
void setKinds(const QJsonObject& kinds);

/*  The domains the interpreter says there are ({domain: {label, color, conditions}}: the colour of a simulation and of what it is given)  */
void setDomains(const QJsonObject& domains);

}   // namespace TypeIcons

}   // namespace FielDes
