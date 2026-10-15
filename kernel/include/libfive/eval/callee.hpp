/*
libfive: a CAD kernel for modeling with implicit functions

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#pragma once

#include <map>
#include <memory>
#include <unordered_map>

#include "libfive/tree/tree.hpp"

namespace libfive {

/*  Forward declarations (the evaluators include the Deck, which holds callees) */
class Deck;
class ArrayEvaluator;
class FeatureEvaluator;
class IntervalEvaluator;
class CalleeRegistry;

/*
 *  A field that a Deck's tape calls (a CALL clause): a Deck of its own, compiled from the called tree once however many
 *  places call it, and the evaluators that run it.
 *
 *  An evaluator that meets a CALL clause evaluates the callee at the point the clause's arguments give (for an interval
 *  evaluator, in the box they give).  The evaluators are made when first asked for, so that an evaluator that never meets a
 *  call pays nothing, and are given the values that the variables have in the evaluator that asks (the callee's variables are
 *  the Deck's variables):
 *    - `array`    for the values only;
 *    - `feature`  for the derivatives and the features (it is a derivative evaluator, and one of those is the whole of what both
 *                 need: a derivative evaluator and a feature evaluator each would be two heavy evaluators of the same tape);
 *    - `interval` for the boxes.
 *
 *  A callee is shared by every deck that calls it -- the Deck that holds it, and the decks of the callees that call it in turn
 *  (see CalleeRegistry) -- so a field that is asked from many places, and by fields that are themselves asked from many places,
 *  is compiled and held once.  One thread, like the Deck: an evaluator of a callee is used by one call at a time, since a field
 *  never calls itself.
 */
class Callee
{
public:
    Callee(const Tree& f, CalleeRegistry* registry);
    ~Callee();

    /*  The compiled field  */
    const std::shared_ptr<Deck> deck;

    /*  The evaluator of each kind, or null if it was not made yet.  `make` makes it with the variables' numbers `vars`. */
    ArrayEvaluator* array() const { return m_array.get(); }
    ArrayEvaluator& makeArray(const std::map<Tree::Id, float>& vars);

    FeatureEvaluator* feature() const { return m_feature.get(); }
    FeatureEvaluator& makeFeature(const std::map<Tree::Id, float>& vars);

    IntervalEvaluator* interval() const { return m_interval.get(); }
    IntervalEvaluator& makeInterval(const std::map<Tree::Id, float>& vars);

    /*  Changes a variable's number in every evaluator made so far */
    void setVar(Tree::Id var, float value);

private:
    std::unique_ptr<ArrayEvaluator> m_array;
    std::unique_ptr<FeatureEvaluator> m_feature;
    std::unique_ptr<IntervalEvaluator> m_interval;
};

/*
 *  The callees of one tree: owned by the Deck that is made from it, and used by that Deck and by the decks of the callees (which
 *  are made with a pointer to it).  The same tree called from different places is one callee.
 */
class CalleeRegistry
{
public:
    /*  The callee of the tree f: made (with its deck, and so the callees of that) the first time it is asked for  */
    Callee* get(const Tree& f);

private:
    std::unordered_map<Tree::Id, std::unique_ptr<Callee>> callees;
};

}   // namespace libfive
