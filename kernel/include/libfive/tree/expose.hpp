/*
libfive: a CAD kernel for modeling with implicit functions

The numbers of a tree that are geometry, made variables so they can be dragged;
see expose.cpp.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#pragma once

#include <vector>

#include "libfive/tree/tree.hpp"

namespace libfive {

/*
 *  The constants of a tree that place its surfaces: a plane's offset
 *  (x - 6), a cylinder's or sphere's radius and axis position, a box's faces --
 *  every constant that is one side of a subtraction with something that is not a
 *  constant.  Not the ones that multiply (a plane's normal, a scale) or that
 *  belong to the placement of a whole part (added to a transformed coordinate),
 *  which is how dragging a face moves that face and neither turns it nor
 *  drags the rest of the part along.  Constants of 1e6 and beyond (the
 *  "nothing here" of a union) and NaN are not geometry.
 *
 *  A surface through the origin has no number to be one of these (x - 0 is written
 *  x): a plane that is a side of a union or an intersection, a sphere's centre or
 *  a cylinder's axis or a centred box (x squared, or its absolute value) with no
 *  position number of its own is given one, 0, so that it can be dragged too.
 *
 *  The order is fixed by the tree (its nodes taken leaves first, left to
 *  right), so the same tree gives the same list every time.  A constant used
 *  by two different nodes counts once for each: the two are separate numbers.
 */
std::vector<float> exposableConstants(const Tree& t);

/*
 *  The tree with each of those constants replaced by the given tree (a free
 *  variable, usually), in the order of exposableConstants.  `with` must have
 *  exactly that many; otherwise an invalid tree is returned.
 */
Tree exposeConstants(const Tree& t, const std::vector<Tree>& with);

}   // namespace libfive
