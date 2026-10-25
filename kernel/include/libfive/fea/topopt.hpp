/*
libfive: a CAD kernel for modeling with implicit functions

What every density-based topology optimization shares, whatever physics it designs for:

  Program   what is asked of the design.  The objective and the constraints are formulas of MEASURES of the analyses (the volume, the
            compliance, the peak stress, a frequency ...): the script's text is compiled into a postfix program over the measures, and
            the program is evaluated with the partial derivatives of each output over each measure.  A measure has a sensitivity (the
            physics gives it: an adjoint solve, an eigenvector ...), a formula is differentiated here -- so ANY formula of measures
            can be optimized, and one sensitivity per measure is all the physics has to supply.

  Mma       the method of moving asymptotes (Svanberg 1987): a convex separable subproblem is made of the sensitivities of the
            objective and of every constraint at the design, and solved through its dual (a few multipliers, one per constraint).
            It replaces the optimality-criteria update, which can hold one volume constraint only.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#pragma once

#include <string>
#include <vector>

namespace libfive {
namespace fea {
namespace topopt {

/*  A formula over measures, in postfix.  Each output (the objective first, then the constraints, each written so that it is kept when
 *  it is 0 or less) is a run of operations ended by End.  Constant pushes `value`, Reading pushes measure number `index`.  */
struct Program
{
    enum Code { Constant = 0, Reading = 1, Add = 2, Sub = 3, Mul = 4, Div = 5, Pow = 6, Neg = 7, Abs = 8, Min = 9, Max = 10, Sqrt = 11, Log = 12, Exp = 13, End = 14 };
    struct Op
    {
        int code = 0;
        double value = 0;
        int index = 0;
    };
    std::vector<Op> ops;
    int measures = 0;                       // how many measures the formulas read

    int outputs() const;
    /*  The value of every output, and its partial derivative over each measure (partial[output][measure]), at these values of the
     *  measures.  False with a message when a formula cannot be evaluated (a division by 0 ...)  */
    bool evaluate(const std::vector<double>& base, std::vector<double>& value, std::vector<std::vector<double>>& partial,
                  std::string& error) const;
};

/*  The method of moving asymptotes: minimise f0(x) subject to g_i(x) <= 0 (i = 1..m), lo <= x <= hi.  Call step() once per design,
 *  with the value and the gradient of the objective and of every constraint at x; it gives the next design.  A constraint that cannot
 *  be met within the moves is softened by a penalty (`penalty` per unit), so the step always exists  */
class Mma
{
public:
    Mma(size_t n, int m);

    /*  g: the m constraint values, dg: their gradients (m vectors of n).  `move`: how far a variable may go in one step, as a share of
     *  its range.  x and xNew have n entries.  Returns false only when the subproblem cannot be solved (never in practice)  */
    bool step(const std::vector<double>& x, double f0, const std::vector<double>& df0, const std::vector<double>& g,
              const std::vector<std::vector<double>>& dg, const std::vector<double>& lo, const std::vector<double>& hi, double move,
              std::vector<double>& xNew);

    /*  The multipliers of the last subproblem (one per constraint): how much the objective would change for a unit more room  */
    const std::vector<double>& multipliers() const { return m_lambda; }
    double penalty = 1000.0;

private:
    size_t m_n;
    int m_m;
    int m_iter = 0;
    std::vector<double> m_xold1, m_xold2, m_low, m_upp, m_lambda;
};

}   // namespace topopt
}   // namespace fea
}   // namespace libfive
