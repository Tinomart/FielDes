/*
libfive: a CAD kernel for modeling with implicit functions

The program of measures and the method of moving asymptotes (see topopt.hpp).

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#include "libfive/fea/topopt.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include <Eigen/Dense>

namespace libfive {
namespace fea {
namespace topopt {

////////////////////////////////////////////////////////////////////////////////
// The program

int Program::outputs() const
{
    int n = 0;
    for (const Op& op : ops) n += op.code == End;
    return n;
}

namespace {

// A value and its partial derivative over each measure (forward mode)
struct Dual
{
    double v = 0;
    std::vector<double> d;
};

}   // anonymous namespace

bool Program::evaluate(const std::vector<double>& base, std::vector<double>& value, std::vector<std::vector<double>>& partial,
                       std::string& error) const
{
    const size_t nb = base.size();
    value.clear();
    partial.clear();
    std::vector<Dual> stack;
    auto fail = [&](const std::string& why) {
        error = why;
        return false;
    };
    for (const Op& op : ops)
    {
        switch (op.code)
        {
        case Constant:
        {
            Dual a;
            a.v = op.value;
            a.d.assign(nb, 0.0);
            stack.push_back(std::move(a));
            break;
        }
        case Reading:
        {
            if (op.index < 0 || size_t(op.index) >= nb) return fail("a formula reads a measure that the optimisation does not have");
            Dual a;
            a.v = base[size_t(op.index)];
            a.d.assign(nb, 0.0);
            a.d[size_t(op.index)] = 1.0;
            stack.push_back(std::move(a));
            break;
        }
        case Add: case Sub: case Mul: case Div: case Pow: case Min: case Max:
        {
            if (stack.size() < 2) return fail("a formula is not complete");
            Dual b = std::move(stack.back());
            stack.pop_back();
            Dual a = std::move(stack.back());
            stack.pop_back();
            Dual r;
            r.d.assign(nb, 0.0);
            switch (op.code)
            {
            case Add:
                r.v = a.v + b.v;
                for (size_t k = 0; k < nb; ++k) r.d[k] = a.d[k] + b.d[k];
                break;
            case Sub:
                r.v = a.v - b.v;
                for (size_t k = 0; k < nb; ++k) r.d[k] = a.d[k] - b.d[k];
                break;
            case Mul:
                r.v = a.v * b.v;
                for (size_t k = 0; k < nb; ++k) r.d[k] = a.d[k] * b.v + b.d[k] * a.v;
                break;
            case Div:
                if (b.v == 0.0) return fail("a formula divides by 0");
                r.v = a.v / b.v;
                for (size_t k = 0; k < nb; ++k) r.d[k] = (a.d[k] - r.v * b.d[k]) / b.v;
                break;
            case Pow:
            {
                r.v = std::pow(a.v, b.v);
                if (!std::isfinite(r.v)) return fail("a formula raises a number to a power it does not have");
                bool exponentVaries = false;
                for (size_t k = 0; k < nb; ++k) exponentVaries = exponentVaries || b.d[k] != 0.0;
                const double slope = b.v * std::pow(a.v, b.v - 1.0);
                for (size_t k = 0; k < nb; ++k)
                {
                    r.d[k] = std::isfinite(slope) ? slope * a.d[k] : 0.0;
                    if (exponentVaries && a.v > 0) r.d[k] += r.v * std::log(a.v) * b.d[k];
                }
                break;
            }
            case Min: r = a.v <= b.v ? a : b; break;
            case Max: r = a.v >= b.v ? a : b; break;
            }
            stack.push_back(std::move(r));
            break;
        }
        case Neg: case Abs: case Sqrt: case Log: case Exp:
        {
            if (stack.empty()) return fail("a formula is not complete");
            Dual& a = stack.back();
            switch (op.code)
            {
            case Neg:
                a.v = -a.v;
                for (double& x : a.d) x = -x;
                break;
            case Abs:
                if (a.v < 0)
                {
                    a.v = -a.v;
                    for (double& x : a.d) x = -x;
                }
                break;
            case Sqrt:
            {
                if (a.v < 0) return fail("a formula takes the square root of a negative number");
                const double s = std::sqrt(a.v);
                const double k = s > 0 ? 0.5 / s : 0.0;
                for (double& x : a.d) x *= k;
                a.v = s;
                break;
            }
            case Log:
            {
                if (!(a.v > 0)) return fail("a formula takes the logarithm of a number that is not positive");
                for (double& x : a.d) x /= a.v;
                a.v = std::log(a.v);
                break;
            }
            case Exp:
            {
                a.v = std::exp(a.v);
                for (double& x : a.d) x *= a.v;
                break;
            }
            }
            break;
        }
        case End:
        {
            if (stack.size() != 1) return fail("a formula is not complete");
            if (!std::isfinite(stack.back().v)) return fail("a formula is not a number");
            value.push_back(stack.back().v);
            partial.push_back(std::move(stack.back().d));
            stack.clear();
            break;
        }
        default:
            return fail("a formula has an operation that is not known");
        }
    }
    if (!stack.empty()) return fail("a formula is not complete");
    return true;
}

////////////////////////////////////////////////////////////////////////////////
// The method of moving asymptotes

Mma::Mma(size_t n, int m) : m_n(n), m_m(m)
{
    m_xold1.assign(n, 0.0);
    m_xold2.assign(n, 0.0);
    m_low.assign(n, 0.0);
    m_upp.assign(n, 0.0);
    m_lambda.assign(size_t(std::max(0, m)), 0.0);
}

bool Mma::step(const std::vector<double>& xIn, double f0, const std::vector<double>& df0, const std::vector<double>& g,
               const std::vector<std::vector<double>>& dg, const std::vector<double>& lo, const std::vector<double>& hi, double move,
               std::vector<double>& xNew)
{
    (void)f0;                                                           // (the objective's value itself does not enter: only its slope)
    const size_t n = m_n;
    const int m = m_m;
    ++m_iter;
    constexpr double asyInit = 0.5, asyIncr = 1.2, asyDecr = 0.7, albefa = 0.1, raa = 1e-5;
    std::vector<double> x(n), range(n), alfa(n), beta(n);
    for (size_t j = 0; j < n; ++j)
    {
        x[j] = std::max(lo[j], std::min(hi[j], xIn[j]));
        range[j] = std::max(hi[j] - lo[j], 1e-5);
    }
    if (m_iter < 3)
    {
        for (size_t j = 0; j < n; ++j)
        {
            m_low[j] = x[j] - asyInit * range[j];
            m_upp[j] = x[j] + asyInit * range[j];
        }
    }
    else
    {
        for (size_t j = 0; j < n; ++j)
        {
            const double z = (x[j] - m_xold1[j]) * (m_xold1[j] - m_xold2[j]);
            const double factor = z > 0 ? asyIncr : (z < 0 ? asyDecr : 1.0);
            double low = x[j] - factor * (m_xold1[j] - m_low[j]);
            double upp = x[j] + factor * (m_upp[j] - m_xold1[j]);
            low = std::min(std::max(low, x[j] - 10.0 * range[j]), x[j] - 0.01 * range[j]);
            upp = std::max(std::min(upp, x[j] + 10.0 * range[j]), x[j] + 0.01 * range[j]);
            m_low[j] = low;
            m_upp[j] = upp;
        }
    }
    for (size_t j = 0; j < n; ++j)
    {
        alfa[j] = std::max(std::max(m_low[j] + albefa * (x[j] - m_low[j]), x[j] - move * range[j]), lo[j]);
        beta[j] = std::min(std::min(m_upp[j] - albefa * (m_upp[j] - x[j]), x[j] + move * range[j]), hi[j]);
        if (alfa[j] > beta[j]) alfa[j] = beta[j] = x[j];
    }

    // The approximations: p/(upp - x) + q/(x - low) for the objective and each constraint
    std::vector<double> p0(n), q0(n);
    std::vector<std::vector<double>> P(static_cast<size_t>(m), std::vector<double>(n, 0.0)), Q(static_cast<size_t>(m), std::vector<double>(n, 0.0));
    std::vector<double> b(size_t(m), 0.0);
    for (size_t j = 0; j < n; ++j)
    {
        const double ux = m_upp[j] - x[j], xl = x[j] - m_low[j];
        const double ux2 = ux * ux, xl2 = xl * xl;
        auto coefficients = [&](double slope, double& p, double& q) {
            const double dp = std::max(slope, 0.0), dq = std::max(-slope, 0.0);
            const double pq = 0.001 * (dp + dq) + raa / range[j];
            p = (dp + pq) * ux2;
            q = (dq + pq) * xl2;
        };
        coefficients(df0[j], p0[j], q0[j]);
        for (int i = 0; i < m; ++i)
        {
            coefficients(dg[size_t(i)][j], P[size_t(i)][j], Q[size_t(i)][j]);
            b[size_t(i)] += P[size_t(i)][j] / ux + Q[size_t(i)][j] / xl;
        }
    }
    for (int i = 0; i < m; ++i) b[size_t(i)] -= g[size_t(i)];

    // The dual: maximise W(lambda) over lambda >= 0 (concave).  x(lambda) minimises the Lagrangian within the moves
    std::vector<double> lam(size_t(m), 1.0), xs(n), grad(static_cast<size_t>(m));
    const double c = penalty, d = 1.0;
    auto evaluate = [&](const std::vector<double>& l, bool derivatives, std::vector<double>& gradient, Eigen::MatrixXd* H) {
        double W = 0;
        if (derivatives)
        {
            gradient.assign(size_t(m), 0.0);
            if (H) H->setZero(m, m);
        }
        std::vector<double> aj(static_cast<size_t>(m));
        for (size_t j = 0; j < n; ++j)
        {
            double Pj = p0[j], Qj = q0[j];
            for (int i = 0; i < m; ++i)
            {
                Pj += l[size_t(i)] * P[size_t(i)][j];
                Qj += l[size_t(i)] * Q[size_t(i)][j];
            }
            const double U = m_upp[j], L = m_low[j];
            double xj;
            const double sp = std::sqrt(std::max(Pj, 0.0)), sq = std::sqrt(std::max(Qj, 0.0));
            if (sp + sq < 1e-300) xj = 0.5 * (alfa[j] + beta[j]);
            else xj = (L * sp + U * sq) / (sp + sq);
            const bool interior = xj > alfa[j] && xj < beta[j];
            xj = std::max(alfa[j], std::min(beta[j], xj));
            xs[j] = xj;
            const double ux = U - xj, xl = xj - L;
            W += Pj / ux + Qj / xl;
            if (derivatives)
            {
                for (int i = 0; i < m; ++i) gradient[size_t(i)] += P[size_t(i)][j] / ux + Q[size_t(i)][j] / xl;
                if (H && interior)
                {
                    const double curvature = 2.0 * Pj / (ux * ux * ux) + 2.0 * Qj / (xl * xl * xl);
                    if (curvature > 0)
                    {
                        for (int i = 0; i < m; ++i) aj[size_t(i)] = P[size_t(i)][j] / (ux * ux) - Q[size_t(i)][j] / (xl * xl);
                        for (int i = 0; i < m; ++i)
                            for (int k = 0; k <= i; ++k)
                            {
                                const double h = -aj[size_t(i)] * aj[size_t(k)] / curvature;
                                (*H)(i, k) += h;
                                if (k != i) (*H)(k, i) += h;
                            }
                    }
                }
            }
        }
        for (int i = 0; i < m; ++i)
        {
            const double li = l[size_t(i)];
            W -= li * b[size_t(i)];
            const double over = std::max(0.0, li - c);
            W -= over * over / (2.0 * d);
            if (derivatives)
            {
                gradient[size_t(i)] -= b[size_t(i)] + over / d;
                if (H && li > c) (*H)(i, i) -= 1.0 / d;
            }
        }
        return W;
    };
    if (m > 0)
    {
        Eigen::MatrixXd H(m, m);
        double mu = 1e-6;
        std::vector<double> trial(static_cast<size_t>(m)), trialGrad;
        double W = evaluate(lam, true, grad, &H);
        for (int it = 0; it < 200; ++it)
        {
            // free multipliers: positive, or wanting to grow
            std::vector<int> freeSet;
            for (int i = 0; i < m; ++i)
                if (lam[size_t(i)] > 1e-14 || grad[size_t(i)] > 0) freeSet.push_back(i);
            double worst = 0;
            for (int i : freeSet) worst = std::max(worst, std::abs(grad[size_t(i)]));
            if (freeSet.empty() || worst < 1e-9) break;
            const int k = int(freeSet.size());
            Eigen::MatrixXd Hf(k, k);
            Eigen::VectorXd gf(k);
            for (int r = 0; r < k; ++r)
            {
                gf(r) = grad[size_t(freeSet[size_t(r)])];
                for (int s = 0; s < k; ++s) Hf(r, s) = -H(freeSet[size_t(r)], freeSet[size_t(s)]);
            }
            bool accepted = false;
            for (int tries = 0; tries < 40 && !accepted; ++tries)
            {
                Eigen::MatrixXd M = Hf;
                for (int r = 0; r < k; ++r) M(r, r) += mu * (1.0 + Hf(r, r));
                Eigen::VectorXd step = M.ldlt().solve(gf);
                trial = lam;
                for (int r = 0; r < k; ++r)
                {
                    const int i = freeSet[size_t(r)];
                    trial[size_t(i)] = std::max(0.0, lam[size_t(i)] + step(r));
                }
                const double Wn = evaluate(trial, false, trialGrad, nullptr);
                if (std::isfinite(Wn) && Wn >= W - 1e-12 * (1.0 + std::abs(W)))
                {
                    accepted = true;
                    const double gain = Wn - W;
                    lam = trial;
                    W = evaluate(lam, true, grad, &H);
                    mu = std::max(1e-12, mu * 0.3);
                    if (gain < 1e-14 * (1.0 + std::abs(W))) it = 200;
                }
                else
                {
                    mu *= 10.0;
                }
            }
            if (!accepted) break;
        }
        evaluate(lam, false, trialGrad, nullptr);                       // (x at the multipliers that were found)
        m_lambda = lam;
    }
    else
    {
        std::vector<double> none;
        evaluate(lam, false, none, nullptr);
    }
    xNew.resize(n);
    for (size_t j = 0; j < n; ++j) xNew[j] = xs[j];
    m_xold2 = m_xold1;
    m_xold1 = x;
    if (m_iter == 1) m_xold2 = x;
    return true;
}

}   // namespace topopt
}   // namespace fea
}   // namespace libfive
