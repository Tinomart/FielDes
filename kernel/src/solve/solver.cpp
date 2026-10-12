/*
libfive: a CAD kernel for modeling with implicit functions
Copyright (C) 2017  Matt Keeter

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#include <limits>
#include <numeric>

#include "libfive/solve/solver.hpp"
#include "libfive/tree/tree.hpp"
#include "libfive/eval/deck.hpp"
#include "libfive/eval/tape.hpp"
#include "libfive/eval/eval_jacobian.hpp"

namespace libfive {

namespace Solver
{

static float weightOf(const Weights& weights, Tree::Id id)
{
    const auto w = weights.find(id);
    return (w == weights.end() || !(w->second > 0)) ? 1.0f : w->second;
}

// A weight this big says: leave the variable alone when any other one can do it
static bool isHeavy(float w) { return w >= 1000.0f; }

static std::pair<float, Solution> findRoot(
        JacobianEvaluator& e, const Tape::Handle& tape,
        const Eigen::Vector3f pos, Solution vars, unsigned gas, const Weights& weights)
{
    const float EPSILON = 1e-6f;

    // Create a static map for all of our derivatives
    std::map<Tree::Id, float> ds;
    for (auto& v : vars)
    {
        ds.insert({v.first, 0});
    }

    float r = e.value(pos, *tape);
    bool converged = false;
    while (!converged && fabs(r) >= EPSILON && --gas)
    {
        // Evaluate and update our local gradient
        for (auto& d : e.gradient(pos, *tape))
        {
            auto v = ds.find(d.first);
            if (v != ds.end())
            {
                v->second = d.second;
            }
        }

        // Break if all of our gradients are nearly zero
        if (std::all_of(ds.begin(), ds.end(),
            [&EPSILON](decltype(ds)::value_type itr) {
                return fabs(itr.second) < EPSILON;
            }))
        {
            break;
        }

        // Solve for step size using a backtracking line search.  (The change of a variable is its gradient over its weight: of all the changes
        // that bring the value to zero the one with the smallest sum of weight * change^2.  A heavy variable is not changed at all while a light
        // one has a gradient: its weight is infinite then.)
        float lightSquares = 0;
        for (const auto& d : ds)
            if (!isHeavy(weightOf(weights, d.first))) lightSquares += d.second * d.second;
        const bool holdHeavy = lightSquares >= EPSILON * EPSILON;
        const auto effective = [&](Tree::Id id) {
            const float w = weightOf(weights, id);
            return (holdHeavy && isHeavy(w)) ? std::numeric_limits<float>::infinity() : w;
        };
        const float slope = std::accumulate(ds.begin(), ds.end(), 0.0f,
                [&effective](float d, const decltype(ds)::value_type& itr) {
                    return d + powf(itr.second, 2.0f) / effective(itr.first); });

        for (float step = r / slope; true; step /= 2)
        {
            for (auto& v : vars)
            {
                e.setVar(v.first, v.second - step * ds.at(v.first) / effective(v.first));
            }

            // Get new residual
            const float r_ = e.value(pos, *tape);

            // Find change in residuals
            const auto diff = r - r_;

            // If we've satisfied the Armijo–Goldstein condition, then break
            // (along with a bunch of other conditions to detect infinite loops)
            if (diff / step >= slope * 0.5 || fabs(diff) < EPSILON ||
                slope < EPSILON || r_ < EPSILON)
            {
                // If residuals are converging, then exit outer loop too
                converged = fabs(diff) < EPSILON;
                r = r_;

                // Store new variable values in our map
                for (auto& v : vars)
                {
                    v.second -= step * ds.at(v.first) / effective(v.first);
                }
                break;
            }
        }
    }
    return {r, vars};
}

////////////////////////////////////////////////////////////////////////////////

std::pair<float, Solution> findRoot(
        const Tree& t, const std::map<Tree::Id, float>& vars,
        const Eigen::Vector3f pos, const Mask& mask, unsigned gas, const Weights& weights)
{
    auto deck = std::make_shared<Deck>(t);
    JacobianEvaluator e(deck, vars);
    return findRoot(e, deck->tape, vars, pos, mask, gas, weights);
}

std::pair<float, Solution> findRoot(
        JacobianEvaluator& e, const Tape::Handle& tape,
        std::map<Tree::Id, float> vars, const Eigen::Vector3f pos,
        const Mask& mask, unsigned gas, const Weights& weights)
{
    // Load initial variable values here
    for (auto& v : vars)
    {
        e.setVar(v.first, v.second);
    }

    // Ignore masked variables
    for (const auto& v : mask)
    {
        vars.erase(v);
    }

    return findRoot(e, tape, pos, vars, gas, weights);
}

} // namespace Solver

}   // namespace libfive
