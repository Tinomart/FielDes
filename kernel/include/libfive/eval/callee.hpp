/*
libfive: a CAD kernel for modeling with implicit functions

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#pragma once

#include <map>
#include <memory>

#include "libfive/tree/tree.hpp"

namespace libfive {

/*  Forward declarations (the evaluators include the Deck, which holds callees) */
class Deck;
class ArrayEvaluator;
class DerivArrayEvaluator;
class FeatureEvaluator;
class IntervalEvaluator;

/*
 *  A field that a Deck's tape calls (a CALL clause): a Deck of its own, compiled from the called tree once however many
 *  places call it, and the evaluators that run it.
 *
 *  An evaluator that meets a CALL clause evaluates the callee at the point the clause's arguments give (for an interval
 *  evaluator, in the box they give); each kind of evaluator runs the callee with the same kind, made when it is first
 *  asked for so that an evaluator that never meets a call pays nothing, and given the values that the variables have in the
 *  evaluator that asks (the callee's variables are the Deck's variables).
 *
 *  A Callee belongs to the Deck that holds it, as the evaluators do: one thread.
 */
class Callee
{
public:
    explicit Callee(const Tree& f);
    ~Callee();

    /*  The compiled field  */
    const std::shared_ptr<Deck> deck;

    /*  The evaluator of each kind, or null if it was not made yet.  `make` makes it with the variables' numbers `vars`. */
    ArrayEvaluator* array() const { return m_array.get(); }
    ArrayEvaluator& makeArray(const std::map<Tree::Id, float>& vars);

    DerivArrayEvaluator* deriv() const { return m_deriv.get(); }
    DerivArrayEvaluator& makeDeriv(const std::map<Tree::Id, float>& vars);

    FeatureEvaluator* feature() const { return m_feature.get(); }
    FeatureEvaluator& makeFeature(const std::map<Tree::Id, float>& vars);

    IntervalEvaluator* interval() const { return m_interval.get(); }
    IntervalEvaluator& makeInterval(const std::map<Tree::Id, float>& vars);

    /*  Changes a variable's number in every evaluator made so far */
    void setVar(Tree::Id var, float value);

private:
    std::unique_ptr<ArrayEvaluator> m_array;
    std::unique_ptr<DerivArrayEvaluator> m_deriv;
    std::unique_ptr<FeatureEvaluator> m_feature;
    std::unique_ptr<IntervalEvaluator> m_interval;
};

}   // namespace libfive
