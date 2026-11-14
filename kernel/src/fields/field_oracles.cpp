/*
libfive: a CAD kernel for modeling with implicit functions

Analysis fields derived from another shape's field; see field_oracles.hpp.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>
#include <memory>
#include <sstream>
#include <vector>

#include "libfive/fields/field_oracles.hpp"
#include "libfive/tree/data.hpp"
#include "libfive/tree/content_key.hpp"
#include "libfive/oracle/oracle_clause.hpp"
#include "libfive/oracle/oracle_memo.hpp"
#include "libfive/oracle/oracle_storage.hpp"
#include "libfive/eval/deck.hpp"
#include "libfive/eval/eval_array.hpp"
#include "libfive/eval/eval_deriv_array.hpp"

namespace libfive {
namespace fields {

namespace {

enum Kind { GRADIENT, THICKNESS, CURVATURE };

// (FIELDES_ORACLE_TIMING=1: where the time of a field of this file goes, written to stderr -- H214, the measurement before the fix)
bool timingOn()
{
    static const bool on = std::getenv("FIELDES_ORACLE_TIMING") != nullptr;
    return on;
}

struct Stopwatch
{
    std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    double ms() const { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); }
};

class FieldOracle : public OracleStorage<>
{
public:
    FieldOracle(const Tree& t, Kind kind, int mode, double param, std::string key = std::string())
        : deck_(std::make_shared<Deck>(t)), value(deck_, varsOf(deck_)), tree_(t), vars_(varsOf(deck_)), kind(kind), mode(mode), param(param), key(std::move(key))
    {
        if (timingOn()) std::fprintf(stderr, "[oracle] a FieldOracle (kind %d) built in %.1f ms\n", int(this->kind), born_.ms());
    }

    // The shape's var()s are the script's numbers (a part moved by handles(): its move, rotate and scale): the evaluators of the shape
    // are given them the way the evaluator that holds this oracle is -- with every variable it is given, and whenever one changes
    void setVar(const void* var, float v) override
    {
        vars_[static_cast<Tree::Id>(var)] = v;
        if (deriv_) deriv_->setVar(static_cast<Tree::Id>(var), v);
        value.setVar(static_cast<Tree::Id>(var), v);
    }

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
        if (kind == THICKNESS)
        {
            thicknessArray(out);
            return;
        }
        if (kind != GRADIENT)
        {
            evalArrayMemo(out);
            return;
        }
        // all points through the derivative evaluator at once
        const size_t n = size_t(out.cols());
        for (size_t i = 0; i < n; ++i) deriv().set(points.col(long(i)).matrix(), i);
        const auto d = deriv().derivs(n);
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
    // (every free variable of the tree, at zero until the evaluator that holds this oracle says what it is)
    // (every free variable of the shape, at zero until the evaluator that holds this oracle says what it is: the ones the shape's own deck holds.  They were found by flattening the tree and walking
    // every node of it, twice for every oracle an evaluator made -- for an imported part that did not end in a minute: H214, measured with FIELDES_ORACLE_TIMING)
    static std::map<Tree::Id, float> varsOf(const std::shared_ptr<Deck>& deck)
    {
        Stopwatch w;
        std::map<Tree::Id, float> m;
        for (const auto& v : deck->vars.right) m[v.first] = 0.0f;
        if (timingOn()) std::fprintf(stderr, "[oracle]   variables of the deck: %.2f ms, %zu free variables\n", w.ms(), m.size());
        return m;
    }

    double f(const Eigen::Vector3d& p)
    {
        return value.value(p.cast<float>());
    }

    // (analytic; numeric where that isn't finite, e.g. on the faces of a
    // box whose distance takes a square root of zero there)
    Eigen::Vector3d grad(const Eigen::Vector3d& p, double* v = nullptr)
    {
        const auto d = deriv().deriv(p.cast<float>());
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
        const std::vector<Eigen::Vector3d> one{p};
        return thicknessMany(one)[0];
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

    // The shape's value at a list of points, in blocks of the evaluator's own size: one call of the array evaluator costs the same for one point as for a whole block (416 us against 3 us a point on the
    // imported bracket of example 02), so everything here asks for blocks
    std::vector<float> valuesAt(const std::vector<Eigen::Vector3d>& pts)
    {
        std::vector<float> out(pts.size());
        const size_t N = LIBFIVE_EVAL_ARRAY_SIZE;
        for (size_t a = 0; a < pts.size(); a += N)
        {
            const size_t n = std::min(N, pts.size() - a);
            Stopwatch w;
            for (size_t j = 0; j < n; ++j) value.set(pts[a + j].cast<float>(), j);
            const auto v = value.values(n);
            for (size_t j = 0; j < n; ++j) out[a + j] = v[long(j)];
            ++valueCalls_;
            valueMs_ += w.ms();
        }
        return out;
    }

    struct Ray
    {
        Eigen::Vector3d p, dir;
        bool wantInside;
    };

    // The marches of many rays in lock step: from p along dir until the field's sign becomes `wantInside` (inside: < 0); the distance, or maxT if it never does.  The step is 0.8 of the value (at
    // least maxT / 400), the crossing is found by 20 halvings: what the march of one point did, with every round a block call for all the rays that are still going
    std::vector<double> marchMany(const std::vector<Ray>& rays, double maxT)
    {
        const size_t R = rays.size();
        const double minStep = maxT / 400.0;
        // The smallest step of a ray grows by a quarter every round, up to a fortieth of maxT: inside a wall the value is the distance to the nearest face, which is next to nothing at the start of the march
        // (the ray begins ON the face), so the step was always the smallest one -- 400 rounds for a wall of maxT -- and each round is a call of the evaluator with a cost of its own.  Measured on the imported
        // bracket of example 02: one evaluation of the part costs 2 us a point in a block, and one thickness took about 12,000 of them (24 ms), a box 25.  The crossing is still found by halving, so a thickness
        // is the same; only a gap thinner than the largest step can be passed over
        const double maxStep = maxT / 40.0;
        enum { MARCHING, BISECTING, DONE };
        std::vector<char> state(R, MARCHING);
        std::vector<double> t(R, 0.0), step(R, 0.0), lo(R, 0.0), hi(R, 0.0), result(R, maxT), floorStep(R, minStep);
        auto crossed = [&](size_t r, double v) { return (v < 0) == rays[r].wantInside; };
        {
            std::vector<Eigen::Vector3d> start(R);
            for (size_t r = 0; r < R; ++r) start[r] = rays[r].p;
            const std::vector<float> v = valuesAt(start);
            for (size_t r = 0; r < R; ++r)
                if (crossed(r, v[r])) { result[r] = 0.0; state[r] = DONE; }
            // (a ray's value is kept for its next step)
            vcur_ = v;
        }
        std::vector<size_t> going;
        std::vector<Eigen::Vector3d> where;
        for (int round = 0; round < 2000; ++round)
        {
            going.clear();
            where.clear();
            for (size_t r = 0; r < R; ++r)
            {
                if (state[r] != MARCHING) continue;
                step[r] = std::max(floorStep[r], 0.8 * std::abs(double(vcur_[r])));
                floorStep[r] = std::min(maxStep, floorStep[r] * 1.25);
                t[r] += step[r];
                if (t[r] >= maxT) { result[r] = maxT; state[r] = DONE; continue; }
                going.push_back(r);
                where.push_back(rays[r].p + t[r] * rays[r].dir);
            }
            if (going.empty()) break;
            if (timingOn() && (round == 5 || round == 10 || round == 20 || round == 40 || round == 80 || round == 160 || round == 320))
            {
                // (H214: how many rays are still marching, and how many of them creep at the least step -- grazing a surface -- at this round)
                double least = 1e30;
                size_t atLeast = 0;
                for (size_t r : going)
                {
                    least = std::min(least, std::abs(double(vcur_[r])));
                    if (step[r] <= minStep * 1.0001) ++atLeast;
                }
                std::fprintf(stderr, "[oracle]   march of %zu rays: round %d, %zu still going, %zu of them at the least step, the least |value| %.4g\n", R, round, going.size(), atLeast, least);
            }
            const std::vector<float> v = valuesAt(where);
            for (size_t k = 0; k < going.size(); ++k)
            {
                const size_t r = going[k];
                vcur_[r] = v[k];
                if (crossed(r, v[k]))
                {
                    lo[r] = std::max(0.0, t[r] - step[r]);
                    hi[r] = t[r];
                    state[r] = BISECTING;
                }
            }
        }
        for (int k = 0; k < 20; ++k)
        {
            going.clear();
            where.clear();
            for (size_t r = 0; r < R; ++r)
            {
                if (state[r] != BISECTING) continue;
                going.push_back(r);
                where.push_back(rays[r].p + 0.5 * (lo[r] + hi[r]) * rays[r].dir);
            }
            if (going.empty()) break;
            const std::vector<float> v = valuesAt(where);
            for (size_t i = 0; i < going.size(); ++i)
            {
                const size_t r = going[i];
                const double m = 0.5 * (lo[r] + hi[r]);
                if (crossed(r, v[i])) hi[r] = m; else lo[r] = m;
            }
        }
        for (size_t r = 0; r < R; ++r)
            if (state[r] == BISECTING) result[r] = 0.5 * (lo[r] + hi[r]);
        return result;
    }

    // The local thickness of a list of points: the chord through the material along the surface normal, capped at the parameter.  The normal is the central difference of the value (the derivative evaluator of
    // a part made of many faces is far dearer than its value, and a thickness needs no more than a direction), the marches are the lock-step ones above
    std::vector<double> thicknessMany(const std::vector<Eigen::Vector3d>& P)
    {
        const size_t n = P.size();
        const double maxT = param;
        std::vector<double> result(n, maxT);
        std::vector<Eigen::Vector3d> at;
        at.reserve(7 * n);
        std::vector<double> h(n);
        for (size_t i = 0; i < n; ++i)
        {
            h[i] = 1e-5 * std::max(1.0, P[i].norm());
            at.push_back(P[i]);
            for (int a = 0; a < 3; ++a)
            {
                Eigen::Vector3d q = P[i], r = P[i];
                q[a] += h[i];
                r[a] -= h[i];
                at.push_back(q);
                at.push_back(r);
            }
        }
        const std::vector<float> f = valuesAt(at);
        std::vector<Eigen::Vector3d> normal(n);
        std::vector<double> v0(n);
        std::vector<char> usable(n, 0);
        for (size_t i = 0; i < n; ++i)
        {
            v0[i] = f[7 * i];
            Eigen::Vector3d g;
            for (int a = 0; a < 3; ++a) g[a] = (double(f[7 * i + 1 + 2 * a]) - double(f[7 * i + 2 + 2 * a])) / (2.0 * h[i]);
            const double gn = g.norm();
            if (g.allFinite() && gn > 1e-30) { normal[i] = g / gn; usable[i] = 1; }
        }
        // the marches of every point: outside (or on the surface) the wall the normal points into is entered first (-n, into the material) and left again from just inside it;
        // inside, the chord goes both ways
        std::vector<Ray> first;
        std::vector<size_t> owner;
        for (size_t i = 0; i < n; ++i)
        {
            if (!usable[i]) continue;
            if (v0[i] >= 0) { first.push_back({P[i], -normal[i], true}); owner.push_back(i); }
            else
            {
                first.push_back({P[i], normal[i], false}); owner.push_back(i);
                first.push_back({P[i], -normal[i], false}); owner.push_back(i);
            }
        }
        const std::vector<double> d1 = marchMany(first, maxT);
        std::vector<Ray> second;
        std::vector<size_t> owner2;
        std::vector<double> backs(n, 0.0);
        for (size_t k = 0; k < first.size(); ++k)
        {
            const size_t i = owner[k];
            if (v0[i] >= 0)
            {
                if (d1[k] >= maxT) { result[i] = maxT; continue; }
                const Eigen::Vector3d q = P[i] - (d1[k] + 1e-6 * maxT) * normal[i];
                second.push_back({q, -normal[i], false});
                owner2.push_back(i);
            }
            else
            {
                // (inside: the first of the two marches of the point is the way out along the normal, the second the way along its opposite)
                backs[i] += d1[k];
            }
        }
        for (size_t i = 0; i < n; ++i)
            if (usable[i] && v0[i] < 0) result[i] = std::min(maxT, backs[i]);
        if (!second.empty())
        {
            const std::vector<double> d2 = marchMany(second, maxT);
            for (size_t k = 0; k < second.size(); ++k) result[owner2[k]] = std::min(maxT, d2[k]);
        }
        return result;
    }

    // A block of points: what the memory of the thickness has, and the rest marched together
    void thicknessArray(Eigen::Block<Eigen::Array<float, Eigen::Dynamic, LIBFIVE_EVAL_ARRAY_SIZE,
                                                  Eigen::RowMajor>, 1, Eigen::Dynamic> out)
    {
        const size_t n = size_t(out.cols());
        Stopwatch whole;
        if (!memo_ && !key.empty()) memo_ = OracleMemo::get(key);
        std::vector<Eigen::Vector3d> todo;
        std::vector<size_t> index;
        for (size_t i = 0; i < n; ++i)
        {
            const Eigen::Vector3f p = points.col(long(i)).matrix();
            float got;
            if (memo_ && memo_->lookup(p, got)) out(long(i)) = got;
            else { todo.push_back(p.cast<double>()); index.push_back(i); }
        }
        if (todo.empty()) return;
        const double beforeMarch = whole.ms();
        const size_t callsBefore = valueCalls_;
        const double msBefore = valueMs_;
        const std::vector<double> r = thicknessMany(todo);
        if (timingOn()) std::fprintf(stderr, "[oracle] thickness of %zu points: memo %.1f ms, march %.1f ms with %zu calls of the value evaluator (%.1f ms in them)\n", todo.size(), beforeMarch, whole.ms() - beforeMarch,
                                     valueCalls_ - callsBefore, valueMs_ - msBefore);
        for (size_t k = 0; k < todo.size(); ++k)
        {
            out(long(index[k])) = float(r[k]);
            if (memo_) memo_->store(points.col(long(index[k])).matrix(), float(r[k]));
        }
    }

    // (the derivative evaluator of the shape is built the first time a field asks for it -- the gradient and the curvature -- and never for a thickness: for an imported part it is dear)
    DerivArrayEvaluator& deriv()
    {
        if (!deriv_) deriv_.reset(new DerivArrayEvaluator(tree_, vars_));
        return *deriv_;
    }

    Stopwatch born_;
    std::shared_ptr<Deck> deck_;
    ArrayEvaluator value;
    size_t valueCalls_ = 0;
    double valueMs_ = 0;
    Tree tree_;
    std::map<Tree::Id, float> vars_;
    std::unique_ptr<DerivArrayEvaluator> deriv_;
    std::shared_ptr<OracleMemo> memo_;
    std::vector<float> vcur_;
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
        Stopwatch w;
        key = prefix + treeContentKey(tree);
        if (timingOn()) std::fprintf(stderr, "[oracle] FieldClause: the content key of the shape took %.1f ms (%zu characters)\n", w.ms(), key.size());
    }

    std::unique_ptr<Oracle> getOracle() const override
    {
        return std::make_unique<FieldOracle>(tree, kind, mode, param, key);
    }
    std::string contentKey() const override { return key; }
    std::string persistentKey() const override
    {
        // (of the shape's expression, when that can be kept: it has no free variables there)
        Stopwatch w;
        const std::string t = treePersistentKey(tree, {});
        if (timingOn()) std::fprintf(stderr, "[oracle] FieldClause::persistentKey took %.1f ms (%zu characters)\n", w.ms(), t.size());
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
