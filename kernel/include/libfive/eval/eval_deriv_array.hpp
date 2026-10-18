/*
libfive: a CAD kernel for modeling with implicit functions
Copyright (C) 2017  Matt Keeter

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#pragma once

#include <Eigen/Eigen>

#include "libfive/eval/eval_array.hpp"

namespace libfive {

class DerivArrayEvaluator : public ArrayEvaluator
{
public:
    DerivArrayEvaluator(const Tree& root);
    DerivArrayEvaluator(const Tree& root,
                        const std::map<Tree::Id, float>& vars);
    DerivArrayEvaluator(std::shared_ptr<Deck> t);
    DerivArrayEvaluator(std::shared_ptr<Deck> t,
                        const std::map<Tree::Id, float>& vars);

protected:
    /*  d(clause).col(index) is a set of partial derivatives [dx, dy, dz] */
    Eigen::Array<Eigen::Array<float, 3, N>, Eigen::Dynamic, 1> d;

    /*  out(col) is a result [dx, dy, dz, w] */
    Eigen::Array<float, 4, N> out;

    /* When evaluating from a parent JacobianEvaluator, we want the
     * CONST_VARS opcode to clear the derivatives, which is special-cased
     * in this flag.  */
    bool clear_vars = false;

    /*  The points at which a min or a max had two sides of the same value and DIFFERENT derivatives, in the last derivs() pass made with trackTies set -- the
     *  points where the derivative is not one number, a crease -- and, through calls, the same for the fields that are called.  Where two sides tie and
     *  have the same derivative (min(0, 0): a clamp of a value that is exactly its bound, in a field that is the same value over most of space) there is
     *  nothing to choose between, and the point is not ambiguous.  getAmbiguous() asks the same question of the VALUES alone, which marks every such tie,
     *  and runs every field that is called again to do so. */
    Eigen::Array<bool, 1, N> tied;

    /*  A call clause, in the derivative pass: the derivatives of the callee
     *  at the points (a, b, c) with respect to what the derivatives here are
     *  with respect to.  The callee's X, Y and Z are the arguments, so the
     *  derivatives of the arguments are what the callee starts from: carrying
     *  them in is the chain rule.  The value pass has set the call's value. */
    void callDeriv(Clause::Id id, Clause::Id a, Clause::Id b, Clause::Id c,
                   size_t count);

public:
    /*  Set before derivs() to have the pass note where it met a tie with different derivatives (see `tied`); it costs a comparison at each min and max.  The
     *  points it notes are read with tiedDerivs() after the pass.  Off by default. */
    bool trackTies = false;
    Eigen::Block<decltype(ambig), 1, Eigen::Dynamic> tiedDerivs(size_t count)
    {
        return tied.head(count);
    }

    /*
     *  Multi-point evaluation (values must be stored with set)
     */
    Eigen::Block<decltype(out), 4, Eigen::Dynamic> derivs(size_t count);
    Eigen::Block<decltype(out), 4, Eigen::Dynamic> derivs(
            size_t count, const Tape& tape);

    /*
     *  Single-point evaluation (return dx, dy, dz, distance)
     *  Invalidates slot 0 in the data array.
     */
    Eigen::Vector4f deriv(const Eigen::Vector3f& pt);
    Eigen::Vector4f deriv(const Eigen::Vector3f& pt,
                          const Tape& tape);

    /*
     *  Per-clause evaluation, used in tape walking.  c is the third
     *  argument, that of a call only.
     */
    void operator()(Opcode::Opcode op, Clause::Id id,
                    Clause::Id a, Clause::Id b, Clause::Id c = 0);

    /*  The same for exactly L points, and the walk of a tape with it (see ArrayEvaluator::exec: the compiler knows the sizes of the
     *  blocks, and sets up no loops of a size it does not know).  L = Eigen::Dynamic is operator() */
    template <int L>
    void execDeriv(Opcode::Opcode op, Clause::Id id,
                   Clause::Id a, Clause::Id b, Clause::Id c);
    template <int L>
    void walkDeriv(const Tape& tape);

    /*
     *  Returns a list of ambiguous items from indices 0 to i that
     *  have derivatives that vary.  This is similar to getAmbiguous
     *  in the ArrayEvaluator parent class, but helps to more precisely
     *  determine if the ambiguous points matter, or whether it's just
     *  a case where the tree has ended up calculating min(X, X)
     *  (for example).
     *
     *  This call performs O(i) work to set up the ambig array
     */
    Eigen::Block<decltype(ambig), 1, Eigen::Dynamic> getAmbiguousDerivs(
            size_t count, const Tape& tape);
    Eigen::Block<decltype(ambig), 1, Eigen::Dynamic> getAmbiguousDerivs(
            size_t count);

    /*  Make an aligned new operator, as this class has Eigen structs
     *  inside of it (which are aligned for SSE) */
    ALIGNED_OPERATOR_NEW_AND_DELETE(DerivArrayEvaluator)
};

}   // namespace libfive


