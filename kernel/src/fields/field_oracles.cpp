/*
libfive: a CAD kernel for modeling with implicit functions

Analysis fields derived from another shape's field; see field_oracles.hpp.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <sstream>

#include "libfive/fields/field_oracles.hpp"
#include "libfive/tree/content_key.hpp"
#include "libfive/oracle/oracle_clause.hpp"
#include "libfive/oracle/oracle_storage.hpp"
#include "libfive/eval/eval_array.hpp"
#include "libfive/eval/eval_deriv_array.hpp"

namespace libfive {
namespace fields {

namespace {

enum Kind { GRADIENT, THICKNESS, CURVATURE };

class FieldOracle : public OracleStorage<>
{
public:
    FieldOracle(const Tree& t, Kind kind, int mode, double param, std::string key = std::string())
        : deriv(t), value(t), kind(kind), mode(mode), param(param), key(std::move(key)) {}

    // (a thickness or curvature costs ray casts or a dozen evaluations of the shape at each point:
    // its answers are remembered by what it is of; the gradient is one derivative evaluation)
    const std::string* memoKey() const override { return kind == GRADIENT ? nullptr : &key; }

    void evalInterval(Interval& out) override
    {
        const float inf = std::numeric_limits<float>::infinity();
        if (kind == GRADIENT)
        {
            if (mode < 3) out = Interval(-1.0f, 1.0f);
            else if (mode == 3) out = Interval(0.0f, inf);
            else out = Interval(-inf, inf);
        }
        else if (kind == THICKNESS)
        {
            out = Interval(0.0f, float(param));
        }
        else
        {
            out = Interval(-inf, inf);
        }
    }

    void evalPoint(float& out, size_t index) override
    {
        out = float(compute(points.col(index).matrix().cast<double>()));
    }

    void evalArray(Eigen::Block<Eigen::Array<float, Eigen::Dynamic, LIBFIVE_EVAL_ARRAY_SIZE,
                                             Eigen::RowMajor>, 1, Eigen::Dynamic> out) override
    {
        if (kind != GRADIENT)
        {
            evalArrayMemo(out);
            return;
        }
        // all points through the derivative evaluator at once
        const size_t n = size_t(out.cols());
        for (size_t i = 0; i < n; ++i) deriv.set(points.col(long(i)).matrix(), i);
        const auto d = deriv.derivs(n);
        for (size_t i = 0; i < n; ++i)
        {
            Eigen::Vector3d g(d(0, long(i)), d(1, long(i)), d(2, long(i)));
            if (!g.allFinite())
            {
                g = numericGrad(points.col(long(i)).matrix().cast<double>());
            }
            out(long(i)) = float(fromGradient(g));
        }
    }

    void checkAmbiguous(Eigen::Block<Eigen::Array<bool, 1, LIBFIVE_EVAL_ARRAY_SIZE>,
                                     1, Eigen::Dynamic> /* out */) override {}

    void evalFeatures(boost::container::small_vector<Feature, 4>& out) override
    {
        // (numerically: these fields are for analysis, rarely meshed)
        const Eigen::Vector3d p = points.col(0).matrix().cast<double>();
        const double h = 1e-4 * std::max(1.0, p.norm());
        Eigen::Vector3f g;
        for (int a = 0; a < 3; ++a)
        {
            Eigen::Vector3d q = p, r = p;
            q[a] += h;
            r[a] -= h;
            g[a] = float((compute(q) - compute(r)) / (2 * h));
        }
        out.push_back(Feature(g));
    }

private:
    double f(const Eigen::Vector3d& p)
    {
        return value.value(p.cast<float>());
    }

    // (analytic; numeric where that isn't finite, e.g. on the faces of a
    // box whose distance takes a square root of zero there)
    Eigen::Vector3d grad(const Eigen::Vector3d& p, double* v = nullptr)
    {
        const auto d = deriv.deriv(p.cast<float>());
        if (v) *v = d[3];
        const Eigen::Vector3d g(d[0], d[1], d[2]);
        return g.allFinite() ? g : numericGrad(p);
    }

    Eigen::Vector3d numericGrad(const Eigen::Vector3d& p)
    {
        const double h = 1e-5 * std::max(1.0, p.norm());
        Eigen::Vector3d g;
        for (int a = 0; a < 3; ++a)
        {
            Eigen::Vector3d q = p, r = p;
            q[a] += h;
            r[a] -= h;
            g[a] = (f(q) - f(r)) / (2 * h);
        }
        return g.allFinite() ? g : Eigen::Vector3d::Zero();
    }

    double fromGradient(const Eigen::Vector3d& g) const
    {
        const double len = g.norm();
        if (mode == 3) return len;
        if (mode >= 4) return g[mode - 4];
        return len > 1e-30 ? g[mode] / len : 0.0;
    }

    double compute(const Eigen::Vector3d& p)
    {
        if (kind == GRADIENT) return fromGradient(grad(p));
        if (kind == CURVATURE) return curvature(p);
        return thickness(p);
    }

    Eigen::Vector3d normal(const Eigen::Vector3d& p)
    {
        const Eigen::Vector3d g = grad(p);
        const double n = g.norm();
        return n > 1e-30 ? Eigen::Vector3d(g / n) : Eigen::Vector3d::Zero();
    }

    double curvature(const Eigen::Vector3d& p)
    {
        const double h = param;
        double div = 0;
        for (int a = 0; a < 3; ++a)
        {
            Eigen::Vector3d q = p, r = p;
            q[a] += h;
            r[a] -= h;
            div += (normal(q)[a] - normal(r)[a]) / (2 * h);
        }
        return 0.5 * div;
    }

    // Marches from p along dir until the field's sign becomes `want`
    // (inside: < 0); returns the distance, or maxT if it never does
    double march(const Eigen::Vector3d& p, const Eigen::Vector3d& dir, bool wantInside, double maxT)
    {
        const double minStep = maxT / 400.0;
        double t = 0, v = f(p);
        for (int it = 0; it < 2000 && t < maxT; ++it)
        {
            if ((v < 0) == wantInside)
            {
                // bisect the crossing between the last two positions
                if (t == 0) return 0;
                double a = std::max(0.0, t - lastStep), b = t;
                for (int k = 0; k < 20; ++k)
                {
                    const double m = 0.5 * (a + b);
                    if ((f(p + m * dir) < 0) == wantInside) b = m; else a = m;
                }
                return 0.5 * (a + b);
            }
            lastStep = std::max(minStep, 0.8 * std::abs(v));
            t += lastStep;
            v = f(p + t * dir);
        }
        return maxT;
    }

    double thickness(const Eigen::Vector3d& p)
    {
        const double maxT = param;
        double v;
        Eigen::Vector3d g = grad(p, &v);
        const double gn = g.norm();
        if (!(gn > 1e-30)) return maxT;
        const Eigen::Vector3d n = g / gn;         // outward
        if (v >= 0)
        {
            // outside (or on the surface): the wall the normal points into
            const double entry = march(p, -n, true, maxT);
            if (entry >= maxT) return maxT;
            const Eigen::Vector3d q = p - (entry + 1e-6 * maxT) * n;
            return std::min(maxT, march(q, -n, false, maxT));
        }
        // inside: the chord through p
        const double back = march(p, n, false, maxT);
        const double fwd = march(p, -n, false, maxT);
        return std::min(maxT, back + fwd);
    }

    DerivArrayEvaluator deriv;
    ArrayEvaluator value;
    Kind kind;
    int mode;
    double param;
    std::string key;
    double lastStep = 0;
};

class FieldClause : public OracleClause
{
public:
    FieldClause(const Tree& t, Kind kind, int mode, double param)
        : tree(t), kind(kind), mode(mode), param(param)
    {
        // What it is of, by the shape's expression (not its identity): the same shape built again --
        // a script run again -- is the same field.  (The exact structural key of the tree: printing it
        // writes constants to six digits and an oracle by its name alone.)
        char number[40];
        std::snprintf(number, sizeof(number), "%.17g", param);
        prefix = std::string(kind == GRADIENT ? "gradient" : kind == THICKNESS ? "thickness" : "curvature") + "#" +
                 std::to_string(mode) + "#" + number + "#";
        key = prefix + treeContentKey(tree);
    }

    std::unique_ptr<Oracle> getOracle() const override
    {
        return std::make_unique<FieldOracle>(tree, kind, mode, param, key);
    }
    std::string contentKey() const override { return key; }
    std::string persistentKey() const override
    {
        // (of the shape's expression, when that can be kept: it has no free variables there)
        const std::string t = treePersistentKey(tree, {});
        return t.empty() ? std::string() : prefix + t;
    }
    std::string name() const override
    {
        return kind == GRADIENT ? "GradientField" : kind == THICKNESS ? "ThicknessField"
                                                                      : "CurvatureField";
    }
    std::vector<Tree> dependencies() const override { return {tree}; }

private:
    Tree tree;
    Kind kind;
    int mode;
    double param;
    std::string key, prefix;
};

}   // anonymous namespace

Tree gradientField(const Tree& t, int mode)
{
    return Tree(std::make_unique<FieldClause>(t, GRADIENT, std::max(0, std::min(6, mode)), 0.0));
}

Tree thicknessField(const Tree& t, double maxThickness)
{
    return Tree(std::make_unique<FieldClause>(t, THICKNESS, 0, std::max(1e-6, maxThickness)));
}

Tree curvatureField(const Tree& t, double h)
{
    return Tree(std::make_unique<FieldClause>(t, CURVATURE, 0, std::max(1e-9, h)));
}

}   // namespace fields
}   // namespace libfive
