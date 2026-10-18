/*
libfive: a CAD kernel for modeling with implicit functions

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#pragma once
#include "libfive/tree/tree.hpp"

namespace libfive {

/*  The partial derivative of a field with respect to x (axis 0), y (axis 1) or z (axis 2), as a field of its own: a tree of ordinary operations
 *  (and calls) that is the symbolic derivative of the tree it is made from, by the rules of calculus.
 *
 *  The renderer needs this for the fields that are built from the gradient of another (the remedies of manufacturing.py: a surface normal, a
 *  step along it).  The gradient used to be found by differences of calls, (f(p + h) - f(p - h)) / 2h, and an interval over a cell of the
 *  mesher's is then (cell / h) times too wide: the mesher cannot rule out a cell, refines the whole of a band round the surface, and a
 *  field that a few hundred cells should draw took gigabytes.  The derivative has the intervals of the derivative: where a face is flat it is
 *  the one number, over the whole cell.
 *
 *      constants, free variables: 0       x, y, z: 1 for their own axis, else 0       sums, products, quotients, powers, roots, the elementary
 *      functions: the rules       min and max: the derivative of the one that is smaller (larger) -- weights made of compare(a, b), which is
 *      -1, 1 or 0, so that where the mesher's interval decides it the derivative is exactly one of the two, and where it does not it is their hull
 *      abs: sign * derivative     compare: 0 (it is piecewise constant)       call f(x, y, z): the chain rule, f_u(x, y, z) x' + f_v(x, y, z) y' + f_w(x, y, z) z',
 *      f_u being the partial of the CALLED field in its own coordinates (a call of that field's derivative: one tape per derivative, as for any call)
 *
 *  An oracle (a native field: a mesh's distance, noise ...) has no symbolic derivative.  With `oracle_step` > 0 its partial is the central difference
 *  over that step (the old way, for that one node: its intervals are as loose as they were); with 0 a tree that has an oracle in it gives
 *  Tree::invalid().  The tree may hold remaps, applies and calls; it is flattened first.
 *
 *  A call of the derivative is three calls of partials (the chain rule), and a partial of a partial is a second derivative: a field whose calls are nested deep -- a field built
 *  from the gradient of another, a Newton walk, the result of a remedy that was made from remedies -- has a derivative that costs several times what the field does to evaluate, for
 *  every level of nesting.  With `max_call_depth` >= 0 a tree whose calls are nested deeper than that gives Tree::invalid(), and the caller reads its gradient by differences, as
 *  it did.  -1 has no limit.
 *
 *  Not recursive in the depth of the tree (a walk, as flatten does), recursive in the depth of calls inside calls only. */
Tree partial_derivative(const Tree& t, int axis, float oracle_step = 0.0f, int max_call_depth = -1);

}   // namespace libfive
