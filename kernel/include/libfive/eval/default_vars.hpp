/*
libfive: a CAD kernel for modeling with implicit functions

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#pragma once

#include <functional>

#include "libfive/tree/tree.hpp"

namespace libfive {

/*  The numbers the free variables of the script stand for, for an evaluator that is made without them.
 *
 *  A tree with free variables (var(): the numbers of a handle, of a surface that is dragged) is evaluated with the numbers it is given:
 *  the viewport's meshing passes them.  The analyses and the other functions of the kernel that make an evaluator of a tree of their
 *  own were never given any, and read every variable as 0 -- the part collapsed, and "the part has no material inside the analysis
 *  region".  The application tells the kernel the numbers of the script's variables as it makes them; an evaluator that is not given a
 *  variable's number takes it from here (a number it is given always wins).  Cleared when a run of the script begins.  */
class DefaultVars
{
public:
    static void set(Tree::Id var, float value);
    static void clear();
    /*  The number of `var`; false when the script has no such variable  */
    static bool find(Tree::Id var, float& value);
    static bool empty();
    /*  Calls `f` with every variable and its number (under a lock: `f` must not call back into DefaultVars)  */
    static void forEach(const std::function<void(Tree::Id, float)>& f);
};

}   // namespace libfive
