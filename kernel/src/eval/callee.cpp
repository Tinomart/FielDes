/*
libfive: a CAD kernel for modeling with implicit functions

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#include "libfive/eval/callee.hpp"
#include "libfive/eval/deck.hpp"
#include "libfive/eval/eval_array.hpp"
#include "libfive/eval/eval_deriv_array.hpp"
#include "libfive/eval/eval_feature.hpp"
#include "libfive/eval/eval_interval.hpp"

namespace libfive {

Callee::Callee(const Tree& f)
    : deck(std::make_shared<Deck>(f))
{
    // Nothing to do here
}

// (out of line: the evaluators are complete types here)
Callee::~Callee() = default;

ArrayEvaluator& Callee::makeArray(const std::map<Tree::Id, float>& vars)
{
    m_array = std::make_unique<ArrayEvaluator>(deck, vars);
    return *m_array;
}

DerivArrayEvaluator& Callee::makeDeriv(const std::map<Tree::Id, float>& vars)
{
    m_deriv = std::make_unique<DerivArrayEvaluator>(deck, vars);
    return *m_deriv;
}

FeatureEvaluator& Callee::makeFeature(const std::map<Tree::Id, float>& vars)
{
    m_feature = std::make_unique<FeatureEvaluator>(deck, vars);
    return *m_feature;
}

IntervalEvaluator& Callee::makeInterval(const std::map<Tree::Id, float>& vars)
{
    m_interval = std::make_unique<IntervalEvaluator>(deck, vars);
    return *m_interval;
}

void Callee::setVar(Tree::Id var, float value)
{
    if (m_array)    m_array->setVar(var, value);
    if (m_deriv)    m_deriv->setVar(var, value);
    if (m_feature)  m_feature->setVar(var, value);
    if (m_interval) m_interval->setVar(var, value);
}

}   // namespace libfive
