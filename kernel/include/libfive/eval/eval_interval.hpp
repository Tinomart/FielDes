/*
libfive: a CAD kernel for modeling with implicit functions
Copyright (C) 2017  Matt Keeter

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#pragma once

#include <Eigen/Eigen>

#include "libfive/eval/base.hpp"
#include "libfive/eval/interval.hpp"
#include "libfive/eval/clause.hpp"

namespace libfive {
// Forward declarations
class Tape;
class Tree;

class IntervalEvaluator : public virtual BaseEvaluator
{
public:
    IntervalEvaluator(const Tree& root);
    IntervalEvaluator(const Tree& root,
                      const std::map<Tree::Id, float>& vars);
    IntervalEvaluator(std::shared_ptr<Deck> d);
    IntervalEvaluator(std::shared_ptr<Deck> d,
                      const std::map<Tree::Id, float>& vars);

    /*
     *  Interval evaluation
     */
    Interval eval(const Eigen::Vector3f& lower,
                  const Eigen::Vector3f& upper);
    Interval eval(const Eigen::Vector3f& lower,
                  const Eigen::Vector3f& upper,
                  const std::shared_ptr<Tape>& tape);

    /*  ... for intervals that may be NaN (an interval of a coordinate is what a call asks of the field it calls) */
    Interval eval(const Interval& x, const Interval& y, const Interval& z,
                  const std::shared_ptr<Tape>& tape);

    std::pair<Interval, std::shared_ptr<Tape>> intervalAndPush(
            const Eigen::Vector3f& lower,
            const Eigen::Vector3f& upper);
    std::pair<Interval, std::shared_ptr<Tape>> intervalAndPush(
            const Eigen::Vector3f& lower,
            const Eigen::Vector3f& upper,
            const std::shared_ptr<Tape>& tape);
    std::pair<Interval, std::shared_ptr<Tape>> intervalAndPush(
            const Interval& x, const Interval& y, const Interval& z,
            const std::shared_ptr<Tape>& tape);

    /*
     *  Returns a shortened tape based on the most recent evaluation.
     *
     *  Normally, this is invoked through intervalAndPush, but in some cases,
     *  we need to call it as a standalone function.  If you're not using
     *  Oracles, then you probably don't need to call it.
     */
    std::shared_ptr<Tape> push(/* uses top-level tape */);
    std::shared_ptr<Tape> push(const std::shared_ptr<Tape>& tape);

    /*
     *  Changes a variable's value
     *
     *  If the variable isn't present in the tree, does nothing
     *  Returns true if the variable's value changes
     */
    bool setVar(Tree::Id var, float value);

protected:
    /*  i[clause] is the interval result for that clause, */
    std::vector<Interval> i;

     /* Sets i[index] = f and maybe_nan[index] = std::isnan(f) */
     void store(float f, size_t index);

    /*  The tape being walked.  The calls on it run their callees on tapes of
     *  their own, which pushing shortened (Tape::callTape) */
    const Tape* tape_ = nullptr;

    /*  The numbers that the variables have here (what an evaluator of a
     *  callee is made with) */
    std::map<Tree::Id, float> varValues() const;

    /*  Pushing shortens the tapes of the callees too: each call site's callee
     *  is evaluated (in the box the arguments have) and pushed while the tape
     *  is walked, if the walk is one that is pushed afterwards (intervalAndPush),
     *  and the tapes it made wait here for push().  `pending_[site]` is null for
     *  a site whose tape came out as it was.  After a plain eval() the calls
     *  are run again, by push(). */
    std::vector<std::shared_ptr<Tape>> pending_;
    bool collecting_ = false;
    bool pendingValid_ = false;

    /*
     *  Per-clause evaluation, used in tape walking.  c is the third
     *  argument, that of a call only.
     */
    void operator()(Opcode::Opcode op, Clause::Id id,
                    Clause::Id a, Clause::Id b, Clause::Id c = 0);

    /*  A call clause: the callee over the box that the arguments have */
    void call(Clause::Id id, Clause::Id a, Clause::Id b, Clause::Id c);
};

}   // namespace libfive
