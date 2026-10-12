/*
libfive: a CAD kernel for modeling with implicit functions

Copyright (C) 2017  Matt Keeter

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#pragma once
#include <map>
#include <set>

#include <Eigen/Eigen>

#include "libfive/eval/eval_jacobian.hpp"

namespace libfive {

class Tree;

namespace Solver
{
    typedef std::map<Tree::Id, float> Solution;
    typedef std::set<Tree::Id> Mask;

    /*
     *  How much a variable resists being changed: the solver takes the change of the variables that is smallest in the sum of weight * change^2
     *  (a variable that is not in the map has weight 1).  With every weight equal that is the plain smallest change; a variable with a big weight
     *  is only changed when nothing else can do it (see dragWeights in expose.hpp)
     */
    typedef std::map<Tree::Id, float> Weights;

    /*
     *  Finds a set of variables that drive t to zero
     *
     *  pos is the x,y,z coordinates at which to solve
     *  Initial conditions are the variable values in vars
     */
    std::pair<float, Solution> findRoot(
            const Tree& t, const std::map<Tree::Id, float>& vars,
            const Eigen::Vector3f pos={0,0,0}, const Mask& mask=Mask(),
            unsigned gas=25000, const Weights& weights=Weights());
    std::pair<float, Solution> findRoot(
            JacobianEvaluator& e, const std::shared_ptr<Tape>& tape,
            std::map<Tree::Id, float> vars,
            const Eigen::Vector3f pos={0,0,0}, const Mask& mask=Mask(),
            unsigned gas=25000, const Weights& weights=Weights());

}   // namespace Solver
}   // namespace libfive
