/*
libfive: a CAD kernel for modeling with implicit functions

The overhang lattice: the material that makes an overhang stand, worked out once and kept as a field of its own.

A part that leans out over the air under it (a ledge, the underside of a bridge) cannot be printed without a support, unless material is added under it so that every face leans no more than the
slope asked: a gusset.  The gusset under a point p of the air is there when a disc of the radius (slope x its height above p) fits inside the part at that height, centred over p -- a ledge is
filled as deep as it is wide, and no deeper -- at some height from where the part starts above p up to a reach above that.  Asking that of a formula at every point the mesher samples costs
about a hundred looks at the part for each one, and the mesher cannot tell where nothing is added without working them through: the drawing took minutes where the part alone takes seconds.

Here it is worked out once, on a lattice laid in the build direction over the part:
  - the part's field is sampled at the nodes, with the same pruning as the mesher's (a block of nodes that interval arithmetic puts wholly inside or outside is not looked at again);
  - in every slice (a plane across the build direction) the distance, in that plane, from each node of the part to the nearest air is found, to the edge itself (marching squares) and not to a node;
  - in every column (along the build direction) a node p in the air whose part above it starts at node s (within the reach) is in the gusset when, at some height z' from s up to s + reach,
    D(z') >= (z' - z_p) x slope, i.e. z_p >= min over z' of (z' - D(z') / slope).  That minimum is kept as the value of the field: (min - z_p) / sqrt(1 + 1 / slope^2), negative inside
    the gusset, about a distance to its surface (the vertical gap over the length of its normal) -- it reproduces the planes of the gusset exactly, since D grows by one for one.
The field is a lattice of values with trilinear reading and a pyramid of ranges (so that the mesher rules out a cell that is nowhere near a gusset at once).  It holds `cap` (twice the reach)
everywhere except in a gusset and at the nodes of the part that touch one, which hold the same formula: so the part united with it is the part to the value everywhere else -- no skin of
interpolation round its edges, no surface of the part that a node happens to lie on touching 0 -- and the mesher meets `cap` in every cell nearer to the part than that and drops the field from
it.  What a result is made of is then the part united with this field: one read of the part and one of the field at a point.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <limits>
#include <memory>
#include <thread>
#include <vector>

#include "libfive/fields/field_oracles.hpp"
#include "libfive/eval/evaluator.hpp"
#include "libfive/eval/tape.hpp"
#include "libfive/oracle/oracle_clause.hpp"
#include "libfive/oracle/oracle_storage.hpp"

namespace libfive {
namespace fields {

namespace {

////////////////////////////////////////////////////////////////////////////////
// The lattice, and its reading

struct LatticeData
{
    int n[3] = {1, 1, 1};
    double h = 1.0;
    double o[3] = {0, 0, 0};
    double e[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};        // (the frame: world = o + x e[0] + y e[1] + z e[2])
    float cap = 1.0f;                                          // (the value outside the lattice and far from any gusset)
    std::vector<float> g;                                      // (the value at node (i, j, k): (k * n[1] + j) * n[0] + i)
    struct Level
    {
        int n[3];
        std::vector<float> lo, hi;
    };
    std::vector<Level> levels;                                 // (levels[l] holds the least and greatest value of each block of 2^(l+1) nodes along each axis)
    std::string key;

    size_t index(int i, int j, int k) const { return (size_t(k) * n[1] + j) * n[0] + i; }

    void toFrame(const double w[3], double f[3]) const
    {
        const double d[3] = {w[0] - o[0], w[1] - o[1], w[2] - o[2]};
        for (int a = 0; a < 3; ++a) f[a] = e[a][0] * d[0] + e[a][1] * d[1] + e[a][2] * d[2];
    }

    // The value at frame coordinates (and, if asked, its gradient in the frame); `cap` outside the lattice
    float value(double x, double y, double z, double* grad) const
    {
        const double u[3] = {x / h, y / h, z / h};
        for (int a = 0; a < 3; ++a)
        {
            if (!(u[a] >= 0.0 && u[a] <= n[a] - 1))
            {
                if (grad) grad[0] = grad[1] = grad[2] = 0.0;
                return cap;
            }
        }
        int c[3];
        double t[3];
        for (int a = 0; a < 3; ++a)
        {
            c[a] = std::min(int(std::floor(u[a])), std::max(0, n[a] - 2));
            t[a] = u[a] - c[a];
        }
        const int i1 = std::min(c[0] + 1, n[0] - 1), j1 = std::min(c[1] + 1, n[1] - 1), k1 = std::min(c[2] + 1, n[2] - 1);
        const double v000 = g[index(c[0], c[1], c[2])], v100 = g[index(i1, c[1], c[2])];
        const double v010 = g[index(c[0], j1, c[2])], v110 = g[index(i1, j1, c[2])];
        const double v001 = g[index(c[0], c[1], k1)], v101 = g[index(i1, c[1], k1)];
        const double v011 = g[index(c[0], j1, k1)], v111 = g[index(i1, j1, k1)];
        const double x00 = v000 + t[0] * (v100 - v000), x10 = v010 + t[0] * (v110 - v010);
        const double x01 = v001 + t[0] * (v101 - v001), x11 = v011 + t[0] * (v111 - v011);
        const double y0 = x00 + t[1] * (x10 - x00), y1 = x01 + t[1] * (x11 - x01);
        if (grad)
        {
            grad[0] = ((1 - t[1]) * (1 - t[2]) * (v100 - v000) + t[1] * (1 - t[2]) * (v110 - v010) + (1 - t[1]) * t[2] * (v101 - v001) + t[1] * t[2] * (v111 - v011)) / h;
            grad[1] = ((1 - t[2]) * (x10 - x00) + t[2] * (x11 - x01)) / h;
            grad[2] = (y1 - y0) / h;
        }
        return float(y0 + t[2] * (y1 - y0));
    }

    void buildPyramid()
    {
        levels.clear();
        const float* lo = g.data();
        const float* hi = g.data();
        int pn[3] = {n[0], n[1], n[2]};
        while (std::max(pn[0], std::max(pn[1], pn[2])) > 1 && levels.size() < 12)
        {
            Level L;
            for (int a = 0; a < 3; ++a) L.n[a] = (pn[a] + 1) / 2;
            L.lo.assign(size_t(L.n[0]) * L.n[1] * L.n[2], std::numeric_limits<float>::max());
            L.hi.assign(L.lo.size(), -std::numeric_limits<float>::max());
            for (int k = 0; k < pn[2]; ++k)
                for (int j = 0; j < pn[1]; ++j)
                    for (int i = 0; i < pn[0]; ++i)
                    {
                        const size_t s = (size_t(k) * pn[1] + j) * pn[0] + i;
                        const size_t d = (size_t(k / 2) * L.n[1] + j / 2) * L.n[0] + i / 2;
                        L.lo[d] = std::min(L.lo[d], lo[s]);
                        L.hi[d] = std::max(L.hi[d], hi[s]);
                    }
            levels.push_back(std::move(L));
            lo = levels.back().lo.data();
            hi = levels.back().hi.data();
            for (int a = 0; a < 3; ++a) pn[a] = levels.back().n[a];
        }
    }

    // The least and the greatest value the field takes in a box of the world (a bound: never too narrow)
    void range(const Eigen::Vector3f& lower, const Eigen::Vector3f& upper, float& mn, float& mx) const
    {
        double fl[3] = {std::numeric_limits<double>::max(), std::numeric_limits<double>::max(), std::numeric_limits<double>::max()};
        double fu[3] = {-std::numeric_limits<double>::max(), -std::numeric_limits<double>::max(), -std::numeric_limits<double>::max()};
        for (int c = 0; c < 8; ++c)
        {
            const double w[3] = {(c & 1) ? upper.x() : lower.x(), (c & 2) ? upper.y() : lower.y(), (c & 4) ? upper.z() : lower.z()};
            double f[3];
            toFrame(w, f);
            for (int a = 0; a < 3; ++a)
            {
                fl[a] = std::min(fl[a], f[a]);
                fu[a] = std::max(fu[a], f[a]);
            }
        }
        mn = std::numeric_limits<float>::max();
        mx = -std::numeric_limits<float>::max();
        int i0[3], i1[3];
        bool outside = false;
        for (int a = 0; a < 3; ++a)
        {
            if (fl[a] < 0 || fu[a] > (n[a] - 1) * h) outside = true;
            if (fu[a] < 0 || fl[a] > (n[a] - 1) * h)
            {
                // (wholly outside the lattice: nothing but the value out there)
                mn = mx = cap;
                return;
            }
            i0[a] = std::max(0, int(std::floor(fl[a] / h)));
            i1[a] = std::min(n[a] - 1, int(std::floor(fu[a] / h)) + 1);
        }
        if (outside)
        {
            mn = std::min(mn, cap);
            mx = std::max(mx, cap);
        }
        int span = 1;
        for (int a = 0; a < 3; ++a) span = std::max(span, i1[a] - i0[a] + 1);
        int level = 0;
        while ((2 << level) <= span / 2 && level < int(levels.size())) ++level;          // (blocks of 2^level nodes, a few of them across the box)
        if (level == 0)
        {
            for (int k = i0[2]; k <= i1[2]; ++k)
                for (int j = i0[1]; j <= i1[1]; ++j)
                    for (int i = i0[0]; i <= i1[0]; ++i)
                    {
                        const float v = g[index(i, j, k)];
                        mn = std::min(mn, v);
                        mx = std::max(mx, v);
                    }
            return;
        }
        const Level& L = levels[size_t(level - 1)];
        int b0[3], b1[3];
        for (int a = 0; a < 3; ++a)
        {
            b0[a] = std::min(L.n[a] - 1, i0[a] >> level);
            b1[a] = std::min(L.n[a] - 1, i1[a] >> level);
        }
        for (int k = b0[2]; k <= b1[2]; ++k)
            for (int j = b0[1]; j <= b1[1]; ++j)
                for (int i = b0[0]; i <= b1[0]; ++i)
                {
                    const size_t s = (size_t(k) * L.n[1] + j) * L.n[0] + i;
                    mn = std::min(mn, L.lo[s]);
                    mx = std::max(mx, L.hi[s]);
                }
    }
};

class LatticeOracle : public OracleStorage<>
{
public:
    explicit LatticeOracle(std::shared_ptr<const LatticeData> d) : data(std::move(d)) {}

    void evalInterval(Interval& out) override
    {
        float mn, mx;
        data->range(lower, upper, mn, mx);
        out = Interval(mn, mx);
    }

    void evalPoint(float& out, size_t index) override
    {
        const double w[3] = {points(0, long(index)), points(1, long(index)), points(2, long(index))};
        double f[3];
        data->toFrame(w, f);
        out = data->value(f[0], f[1], f[2], nullptr);
    }

    void checkAmbiguous(Eigen::Block<Eigen::Array<bool, 1, LIBFIVE_EVAL_ARRAY_SIZE>,
                                     1, Eigen::Dynamic> /* out */) override {}

    void evalFeatures(boost::container::small_vector<Feature, 4>& out) override
    {
        const double w[3] = {points(0, 0), points(1, 0), points(2, 0)};
        double f[3], gf[3];
        data->toFrame(w, f);
        data->value(f[0], f[1], f[2], gf);
        Eigen::Vector3f g;
        for (int c = 0; c < 3; ++c)
        {
            g[c] = float(gf[0] * data->e[0][c] + gf[1] * data->e[1][c] + gf[2] * data->e[2][c]);
        }
        out.push_back(Feature(g));
    }

private:
    std::shared_ptr<const LatticeData> data;
};

class LatticeClause : public OracleClause
{
public:
    explicit LatticeClause(std::shared_ptr<const LatticeData> d) : data(std::move(d)) {}
    std::unique_ptr<Oracle> getOracle() const override { return std::make_unique<LatticeOracle>(data); }
    std::string name() const override { return "OverhangLattice"; }
    std::string contentKey() const override { return data->key; }
    std::string persistentKey() const override { return data->key; }

private:
    std::shared_ptr<const LatticeData> data;
};

////////////////////////////////////////////////////////////////////////////////
// Threads

void parallelFor(size_t count, const std::function<void(size_t)>& fn)
{
    if (count == 0) return;
    const size_t threads = std::max<size_t>(1, std::min<size_t>(std::thread::hardware_concurrency(), count));
    if (threads == 1)
    {
        for (size_t i = 0; i < count; ++i) fn(i);
        return;
    }
    std::atomic<size_t> next(0);
    std::vector<std::thread> pool;
    for (size_t t = 0; t < threads; ++t)
    {
        pool.emplace_back([&]() {
            for (;;)
            {
                const size_t i = next.fetch_add(1);
                if (i >= count) break;
                fn(i);
            }
        });
    }
    for (auto& th : pool) th.join();
}

double secondsSince(const std::chrono::steady_clock::time_point& t0)
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

////////////////////////////////////////////////////////////////////////////////
// Sampling the part at the nodes, with the pruning of the mesher

// (what a node holds that interval arithmetic put inside or outside a whole block without looking at it: only the sign is known, and a value this large is how the node is told from one that was
// looked at, a distance)
const float kBound = 1.0e6f;

struct Sampler
{
    const OverhangLatticeParams& P;
    int n[3];
    double h;
    std::vector<float>& F;
    Evaluator& ev;
    std::atomic<uint64_t>& looked;
    double band = 0.0;          // (the draft needs the part's value in the air out to this far from its surface: a block is "outside" only when the interval says it is more than this away)
    double bandIn = 0.0;        // (the walls need it inside the part as well, out to as far as the thickness is looked for: a block is "inside" only when the interval says it is more than this deep)

    Eigen::Vector3f world(double x, double y, double z) const
    {
        Eigen::Vector3f w;
        for (int c = 0; c < 3; ++c) w[c] = float(P.origin[c] + x * P.axes[c] + y * P.axes[3 + c] + z * P.axes[6 + c]);
        return w;
    }

    void fill(const int lo[3], const int hi[3], float v)
    {
        for (int k = lo[2]; k <= hi[2]; ++k)
            for (int j = lo[1]; j <= hi[1]; ++j)
            {
                float* row = &F[(size_t(k) * n[1] + j) * n[0]];
                for (int i = lo[0]; i <= hi[0]; ++i) row[i] = v;
            }
    }

    // The nodes lo..hi (inclusive): found whole by interval arithmetic over the box they and their neighbours stand in, or looked at one by one on the shortened tape
    void box(const int lo[3], const int hi[3], const Tape::Handle& tape)
    {
        // (a block is "decided" over the box that reaches one node past it on each side: a node of the part is then never next to a node of the air that was not looked at, so no edge of the
        // surface runs between a node that holds a bound and one that holds a value)
        double fl[3], fu[3];
        for (int a = 0; a < 3; ++a)
        {
            fl[a] = (lo[a] - 1) * h;
            fu[a] = (hi[a] + 1) * h;
        }
        Eigen::Vector3f wl(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
        Eigen::Vector3f wu(-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max());
        for (int c = 0; c < 8; ++c)
        {
            const Eigen::Vector3f w = world((c & 1) ? fu[0] : fl[0], (c & 2) ? fu[1] : fl[1], (c & 4) ? fu[2] : fl[2]);
            wl = wl.cwiseMin(w);
            wu = wu.cwiseMax(w);
        }
        auto o = ev.intervalAndPush(wl, wu, tape);
        const Tape::Handle& used = o.first.isSafe() ? o.second : tape;
        if (o.first.isSafe() && o.first.upper() < -bandIn)
        {
            fill(lo, hi, -kBound);
        }
        else if (o.first.isSafe() && o.first.lower() > band)
        {
            fill(lo, hi, kBound);
        }
        else
        {
            const size_t count = size_t(hi[0] - lo[0] + 1) * (hi[1] - lo[1] + 1) * (hi[2] - lo[2] + 1);
            if (count <= 512)
            {
                size_t pending = 0;
                size_t where[ArrayEvaluator::N];
                auto flush = [&]() {
                    if (!pending) return;
                    const auto vs = ev.values(pending, *used);
                    for (size_t q = 0; q < pending; ++q) F[where[q]] = vs(long(q));
                    looked += pending;
                    pending = 0;
                };
                for (int k = lo[2]; k <= hi[2]; ++k)
                    for (int j = lo[1]; j <= hi[1]; ++j)
                        for (int i = lo[0]; i <= hi[0]; ++i)
                        {
                            ev.set(world(i * h, j * h, k * h), pending);
                            where[pending++] = (size_t(k) * n[1] + j) * n[0] + i;
                            if (pending == ArrayEvaluator::N) flush();
                        }
                flush();
            }
            else
            {
                int axis = 0;
                for (int a = 1; a < 3; ++a)
                {
                    if (hi[a] - lo[a] > hi[axis] - lo[axis]) axis = a;
                }
                const int mid = (lo[axis] + hi[axis]) / 2;
                int h1[3] = {hi[0], hi[1], hi[2]};
                int l2[3] = {lo[0], lo[1], lo[2]};
                h1[axis] = mid;
                l2[axis] = mid + 1;
                box(lo, h1, used);
                box(l2, hi, used);
            }
        }
        if (o.second != tape)
        {
            ev.getDeck()->claim(std::move(o.second));
        }
    }
};

////////////////////////////////////////////////////////////////////////////////
// The distance, in one slice, from each node of the part to the nearest air

struct Seg
{
    float x0, y0, x1, y1;
};

inline float segDist2(const Seg& s, float px, float py)
{
    const float dx = s.x1 - s.x0, dy = s.y1 - s.y0;
    const float L2 = dx * dx + dy * dy;
    float t = L2 > 0 ? ((px - s.x0) * dx + (py - s.y0) * dy) / L2 : 0.0f;
    t = std::max(0.0f, std::min(1.0f, t));
    const float qx = s.x0 + t * dx - px, qy = s.y0 + t * dy - py;
    return qx * qx + qy * qy;
}

struct SliceWork
{
    std::vector<Seg> segs;
    std::vector<int> cell;          // (the cell each segment lies in: its x index and y index, packed)
    std::vector<int> best;
    std::vector<float> bd;
};

// F: the nx * ny values of a slice (a node is in the part where its value is 0 or less); D: the distance from each node of the part to the edge of the part (marching squares), up to
// `limit`; 0 for a node of the air
void sliceDistance(const float* F, int nx, int ny, float h, float limit, SliceWork& W, float* D)
{
    W.segs.clear();
    W.cell.clear();
    for (int j = 0; j + 1 < ny; ++j)
    {
        for (int i = 0; i + 1 < nx; ++i)
        {
            const float fa = F[j * nx + i], fb = F[j * nx + i + 1], fc = F[(j + 1) * nx + i + 1], fd = F[(j + 1) * nx + i];
            const bool ia = fa <= 0, ib = fb <= 0, ic = fc <= 0, id = fd <= 0;
            if (ia == ib && ib == ic && ic == id) continue;
            const float x0 = i * h, y0 = j * h, x1 = (i + 1) * h, y1 = (j + 1) * h;
            float px[4], py[4];
            bool has[4] = {false, false, false, false};          // bottom, right, top, left
            if (ia != ib) { const float s = fa / (fa - fb); px[0] = x0 + s * h; py[0] = y0; has[0] = true; }
            if (ib != ic) { const float s = fb / (fb - fc); px[1] = x1; py[1] = y0 + s * h; has[1] = true; }
            if (id != ic) { const float s = fd / (fd - fc); px[2] = x0 + s * h; py[2] = y1; has[2] = true; }
            if (ia != id) { const float s = fa / (fa - fd); px[3] = x0; py[3] = y0 + s * h; has[3] = true; }
            auto add = [&](int p, int q) {
                W.segs.push_back({px[p], py[p], px[q], py[q]});
                W.cell.push_back(j * nx + i);
            };
            const int count = int(has[0]) + int(has[1]) + int(has[2]) + int(has[3]);
            if (count == 2)
            {
                int p = -1, q = -1;
                for (int c = 0; c < 4; ++c)
                {
                    if (!has[c]) continue;
                    if (p < 0) p = c; else q = c;
                }
                add(p, q);
            }
            else if (count == 4)
            {
                const bool centre = 0.25f * (fa + fb + fc + fd) <= 0;
                // (two corners that are alike, on a diagonal: the ones on the other side of the centre's are cut off from it)
                if (ia == centre) { add(0, 1); add(2, 3); }       // (b and d are cut off: bottom-right, top-left)
                else { add(0, 3); add(1, 2); }                     // (a and c are cut off: bottom-left, right-top)
            }
        }
    }
    const size_t nodes = size_t(nx) * ny;
    if (W.segs.empty())
    {
        for (size_t q = 0; q < nodes; ++q) D[q] = F[q] <= 0 ? limit : 0.0f;
        return;
    }
    W.best.assign(nodes, -1);
    W.bd.assign(nodes, std::numeric_limits<float>::max());
    auto consider = [&](int node, int s) {
        const float d2 = segDist2(W.segs[size_t(s)], (node % nx) * h, (node / nx) * h);
        if (d2 < W.bd[size_t(node)])
        {
            W.bd[size_t(node)] = d2;
            W.best[size_t(node)] = s;
        }
    };
    for (size_t s = 0; s < W.segs.size(); ++s)
    {
        const int c = W.cell[s];
        consider(c, int(s));
        consider(c + 1, int(s));
        consider(c + nx, int(s));
        consider(c + nx + 1, int(s));
    }
    auto relax = [&](int node, int from) {
        const int s = W.best[size_t(from)];
        if (s >= 0) consider(node, s);
    };
    for (int pass = 0; pass < 2; ++pass)
    {
        for (int j = 0; j < ny; ++j)
        {
            for (int i = 0; i < nx; ++i)
            {
                const int q = j * nx + i;
                if (i > 0) relax(q, q - 1);
                if (j > 0)
                {
                    relax(q, q - nx);
                    if (i > 0) relax(q, q - nx - 1);
                    if (i + 1 < nx) relax(q, q - nx + 1);
                }
            }
            for (int i = nx - 2; i >= 0; --i) relax(j * nx + i, j * nx + i + 1);
        }
        for (int j = ny - 1; j >= 0; --j)
        {
            for (int i = nx - 1; i >= 0; --i)
            {
                const int q = j * nx + i;
                if (i + 1 < nx) relax(q, q + 1);
                if (j + 1 < ny)
                {
                    relax(q, q + nx);
                    if (i + 1 < nx) relax(q, q + nx + 1);
                    if (i > 0) relax(q, q + nx - 1);
                }
            }
            for (int i = 1; i < nx; ++i) relax(j * nx + i, j * nx + i - 1);
        }
    }
    for (size_t q = 0; q < nodes; ++q)
    {
        D[q] = F[q] <= 0 ? std::min(limit, std::sqrt(W.bd[q])) : 0.0f;
    }
}

}   // anonymous namespace

////////////////////////////////////////////////////////////////////////////////

// The squared distance transform of Felzenszwalb and Huttenlocher, in place, of one line of `n` values (0 at a source, a large value elsewhere), in index units
static void dt1(float* f, int n, std::vector<int>& v, std::vector<double>& z, std::vector<double>& out)
{
    if (n <= 1) return;
    v.assign(size_t(n), 0);
    z.assign(size_t(n) + 1, 0.0);
    out.assign(size_t(n), 0.0);
    int k = 0;
    v[0] = 0;
    z[0] = -1.0e30;
    z[1] = 1.0e30;
    auto cross = [&](int q, int p) { return ((double(f[q]) + double(q) * q) - (double(f[p]) + double(p) * p)) / (2.0 * q - 2.0 * p); };
    for (int q = 1; q < n; ++q)
    {
        double s = cross(q, v[size_t(k)]);
        while (s <= z[size_t(k)])            // (z[0] is -1e30: k stops at 0 at the latest)
        {
            --k;
            s = cross(q, v[size_t(k)]);
        }
        ++k;
        v[size_t(k)] = q;
        z[size_t(k)] = s;
        z[size_t(k) + 1] = 1.0e30;
    }
    k = 0;
    for (int q = 0; q < n; ++q)
    {
        while (z[size_t(k) + 1] < q) ++k;
        const double dq = double(q - v[size_t(k)]);
        out[size_t(q)] = dq * dq + double(f[v[size_t(k)]]);
    }
    for (int q = 0; q < n; ++q) f[q] = float(std::min(out[size_t(q)], 1.0e18));
}

// The squared distance (in index units) from every node to the nearest source (a node holding 0; the others hold a large value), over the three axes
static void edt3(std::vector<float>& D, int nx, int ny, int nz)
{
    parallelFor(size_t(ny) * nz, [&](size_t line) {
        std::vector<int> v;
        std::vector<double> z, out;
        dt1(&D[line * size_t(nx)], nx, v, z, out);
    });
    parallelFor(size_t(nz) * nx, [&](size_t id) {
        const size_t k = id / size_t(nx), i = id % size_t(nx);
        std::vector<int> v;
        std::vector<double> z, out;
        std::vector<float> col(static_cast<size_t>(ny));          // (not `col(size_t(ny))`: that reads as a function)
        for (int j = 0; j < ny; ++j) col[size_t(j)] = D[(k * ny + j) * nx + i];
        dt1(col.data(), ny, v, z, out);
        for (int j = 0; j < ny; ++j) D[(k * ny + j) * nx + i] = col[size_t(j)];
    });
    parallelFor(size_t(ny) * nx, [&](size_t id) {
        const size_t j = id / size_t(nx), i = id % size_t(nx);
        std::vector<int> v;
        std::vector<double> z, out;
        std::vector<float> col(static_cast<size_t>(nz));
        for (int k = 0; k < nz; ++k) col[size_t(k)] = D[(size_t(k) * ny + j) * nx + i];
        dt1(col.data(), nz, v, z, out);
        for (int k = 0; k < nz; ++k) D[(size_t(k) * ny + j) * nx + i] = col[size_t(k)];
    });
}

// The finished lattice as a field
static Tree finishLattice(const OverhangLatticeParams& P, std::vector<float>&& values, float cap, const char* tag, double* stats, double volume, double nodes,
                          double looked, double tSample, double tTotal, double partNodes)
{
    auto data = std::make_shared<LatticeData>();
    for (int a = 0; a < 3; ++a)
    {
        data->n[a] = P.n[a];
        data->o[a] = P.origin[a];
        for (int c = 0; c < 3; ++c) data->e[a][c] = P.axes[3 * a + c];
    }
    data->h = P.pitch;
    data->cap = cap;
    data->g = std::move(values);
    {
        uint64_t hash = 1469598103934665603ull;
        const unsigned char* bytes = reinterpret_cast<const unsigned char*>(data->g.data());
        const size_t total = data->g.size() * sizeof(float);
        for (size_t i = 0; i < total; ++i)
        {
            hash ^= bytes[i];
            hash *= 1099511628211ull;
        }
        char buf[160];
        std::snprintf(buf, sizeof(buf), "%s#%016llx#%d#%d#%d#%.9g", tag, (unsigned long long)hash, P.n[0], P.n[1], P.n[2], P.pitch);
        data->key = buf;
    }
    data->buildPyramid();
    if (stats)
    {
        stats[0] = 0.0;
        stats[1] = volume;
        stats[2] = nodes;
        stats[3] = looked;
        stats[4] = tSample;
        stats[5] = tTotal;
        stats[6] = partNodes;
        stats[7] = nodes * sizeof(float) * 1.15 / (1024.0 * 1024.0) * 2.0;
    }
    return Tree(std::make_unique<LatticeClause>(data));
}

Tree overhangLattice(const Tree& part, const std::map<Tree::Id, float>& vars, const OverhangLatticeParams& P, double* stats)
{
    const auto t0 = std::chrono::steady_clock::now();
    if (stats) for (int i = 0; i < 8; ++i) stats[i] = 0.0;
    auto fail = [&](double code) {
        if (stats) stats[0] = code;
        return Tree::invalid();
    };
    const int nx = P.n[0], ny = P.n[1], nz = P.n[2];
    const double h = P.pitch;
    // (a NEGATIVE slope is the mode of the undercut fill: no gusset, but in every column the air between the first and the last node of the part)
    if (nx < 3 || ny < 3 || nz < 3 || !(h > 0) || P.tanSlope == 0.0 || !(P.reach > 0)) return fail(1);
    const size_t nodes = size_t(nx) * ny * nz;
    if (nodes > size_t(160) * 1000 * 1000) return fail(2);          // (a lattice of more than 160 million nodes: a coarser pitch is asked for)

    // 1. The part at the nodes
    std::vector<float> F(nodes, kBound);
    std::atomic<uint64_t> looked(0);
    const double bandD = (P.mode == 2 || P.mode == 3 || P.mode == 4 || P.mode == 5) ? P.reach : 0.0;
    const double bandInD = (P.mode == 3) ? 1.1 * P.minZ + 4.0 * h : ((P.mode == 5) ? P.minZ + 4.0 * h : 0.0);
    {
        // tiles of 16 nodes, each worked through by one thread with an evaluator of its own
        const int T = 16;
        const int tx = (nx + T - 1) / T, ty = (ny + T - 1) / T, tz = (nz + T - 1) / T;
        std::atomic<int> failed(0);
        const size_t threads = std::max<size_t>(1, std::min<size_t>(std::thread::hardware_concurrency(), size_t(tx) * ty * tz));
        std::atomic<size_t> next(0);
        std::vector<std::thread> pool;
        for (size_t th = 0; th < threads; ++th)
        {
            pool.emplace_back([&]() {
                try
                {
                    Evaluator ev(part, vars);
                    const Tape::Handle root = ev.getDeck()->tape;
                    Sampler S{P, {nx, ny, nz}, h, F, ev, looked};
                    S.band = bandD;
                    S.bandIn = bandInD;
                    for (;;)
                    {
                        const size_t q = next.fetch_add(1);
                        if (q >= size_t(tx) * ty * tz) break;
                        const int ti = int(q % tx), tj = int((q / tx) % ty), tk = int(q / (size_t(tx) * ty));
                        const int lo[3] = {ti * T, tj * T, tk * T};
                        const int hi[3] = {std::min(nx - 1, lo[0] + T - 1), std::min(ny - 1, lo[1] + T - 1), std::min(nz - 1, lo[2] + T - 1)};
                        S.box(lo, hi, root);
                    }
                }
                catch (...)
                {
                    failed = 1;
                }
            });
        }
        for (auto& th : pool) th.join();
        if (failed) return fail(3);
    }
    const double tSample = secondsSince(t0);

    // The DRAFT (mode 2): the formula of `_remedy_draft` -- a wall that leans less than the angle from the pull direction (the lattice's third axis) is offset outwards by (the height below the top) x (the
    // tangent of the angle less the wall's tilt) x (the wall factor of its normal) -- worked out once on the lattice.  The part was looked at in a band of `reach` round its surface (the sampler's
    // `band`), so that every node of the air that the draft can move the surface over holds the part's own value, and the normal is the central difference of those values over the lattice.  The field
    // holds `F - growth` at the nodes of the air that have a growth, the same at the nodes of the part next to them (the growth of the neighbour of the air: the surface is placed between a node of the air
    // and one of the part by the same formula), and `reach` everywhere else (above 0, and above what the part's own value is next to the part: the zero of the field, where nothing grows, is inside the part,
    // so that the part united with the field is the part there)
    // The WALLS (mode 3) go through the same block: where a wall is thinner than `minZ` (the limit), the surface is offset outwards by half of what the wall lacks, to half the limit at most (the formula of
    // `_remedy_walls`).  How thick the wall is at a point of the air is the length of the line along the normal, from the nearest point of the surface (the foot, p - F n) through the material to where it meets
    // the air again, found at 24 places up to a tenth more than the limit and between them by where the part's value crosses 0 -- read off the lattice by trilinear interpolation, so the part was looked at
    // inside as well as outside (`bandIn`).  The same gates as the formula: the foot must be on the surface, the slope a distance's (0.9), a chord of nothing (an edge) counts for nothing, and the growth is
    // eased out to nothing at the limit over 5 % of it (GRACE)
    // The CORNERS by the ball itself (mode 5; `minZ` is the radius): no walk.  A point p of the air is inside the closing of the part -- the part with every inside corner rounded by a ball of radius r rolled in
    // the air -- when no ball of that radius that lies in the air covers it, that is when M(p), the largest value of the part's field F on the ball of radius r round p, is short of what open air gives, F(p) + r
    // times the slope (a distance gains one for one as the ball's centre moves away from the part; in a corner it gains less, because the walls close in).  The shortfall D(p) is how deep the fill is, and the
    // closing's surface is where F - D = 0 (checked by hand for a corner of 90 degrees: the apex of the arc comes out at 0.293 r from both walls, the rolling ball's own).  M is found by trilinear reads of the
    // lattice at 96 directions round p, the way the gradient points, and p itself; a tolerance of 3 % of r keeps a flat face and a convex edge exactly as they are.  The part's value was looked at in the air
    // out to a radius and inside to a radius (the sampler's `band` and `bandIn`).  Written as the walls are: F - D at the nodes of the air that are next to the part, the part's own value elsewhere
    if (P.mode == 2 || P.mode == 3 || P.mode == 5)
    {
        const double tanT = P.tanSlope, top = P.minZ;
        const double tWall = P.minZ;
        const int wallSteps = 24;
        const float capD = float(P.reach);
        const size_t plane = size_t(nx) * ny;
        const float bad = 0.5f * kBound;
        std::vector<float> Dl(nodes, -1.0f);
        if (P.mode == 5)
        {
            // (the part's field is renormalised to a distance by its own slope before any ball is rolled: the ball's test -- the value gains the radius, one for one, as the centre moves away -- holds for a
            // distance and not for a field whose slope wanders by a tenth or more, like the topology bracket's: there every node was a little "short" of open air and the fill came out as a skin of shards, 1,490 mm^3
            // where the formula says 62.  The slope at a node is the steepest change of the value towards any of the 26 neighbours -- one for one for a distance, on either side of a crease, where a central
            // difference dips -- and the value is divided by it, held between 0.6 and 1.6)
            std::vector<float> S(nodes, 1.0f);
            parallelFor(size_t(nz), [&](size_t k) {
                if (k < 1 || k + 1 >= size_t(nz)) return;
                for (int j = 1; j + 1 < ny; ++j)
                    for (int i = 1; i + 1 < nx; ++i)
                    {
                        const size_t at = (k * ny + j) * nx + i;
                        const float f0 = F[at];
                        if (std::fabs(f0) >= bad) continue;
                        double s = 0.0;
                        for (int dk = -1; dk <= 1; ++dk)
                            for (int dj = -1; dj <= 1; ++dj)
                                for (int di = -1; di <= 1; ++di)
                                {
                                    if (di == 0 && dj == 0 && dk == 0) continue;
                                    const float vq = F[((size_t(k) + dk) * ny + (j + dj)) * nx + (i + di)];
                                    if (std::fabs(vq) >= bad) continue;
                                    s = std::max(s, std::fabs(double(vq) - double(f0)) / (h * std::sqrt(double(di * di + dj * dj + dk * dk))));
                                }
                        if (s > 0.3) S[at] = float(std::max(0.6, std::min(1.6, s)));
                    }
            });
            parallelFor(size_t(nz), [&](size_t k) {
                for (size_t q = 0; q < plane; ++q)
                {
                    const size_t at = k * plane + q;
                    if (std::fabs(F[at]) < bad) F[at] = F[at] / S[at];
                }
            });
        }
        auto cornerValue = [&](size_t idx) -> double {
            const float v = F[idx];
            if (std::fabs(v) >= bad) return v < 0.0f ? -3.0 * tWall : 3.0 * tWall;
            return double(v);
        };
        auto sampleF = [&](double x, double y, double z) -> double {
            const double u[3] = {x / h, y / h, z / h};
            const int dim[3] = {nx, ny, nz};
            int c[3];
            double tt[3];
            for (int a = 0; a < 3; ++a)
            {
                if (!(u[a] >= 0.0 && u[a] <= double(dim[a] - 1))) return 3.0 * tWall;
                c[a] = std::min(int(std::floor(u[a])), dim[a] - 2);
                tt[a] = u[a] - c[a];
            }
            auto at3 = [&](int i, int j, int k) { return cornerValue((size_t(k) * ny + j) * nx + i); };
            const double v000 = at3(c[0], c[1], c[2]), v100 = at3(c[0] + 1, c[1], c[2]);
            const double v010 = at3(c[0], c[1] + 1, c[2]), v110 = at3(c[0] + 1, c[1] + 1, c[2]);
            const double v001 = at3(c[0], c[1], c[2] + 1), v101 = at3(c[0] + 1, c[1], c[2] + 1);
            const double v011 = at3(c[0], c[1] + 1, c[2] + 1), v111 = at3(c[0] + 1, c[1] + 1, c[2] + 1);
            const double x00 = v000 + tt[0] * (v100 - v000), x10 = v010 + tt[0] * (v110 - v010);
            const double x01 = v001 + tt[0] * (v101 - v001), x11 = v011 + tt[0] * (v111 - v011);
            const double y0 = x00 + tt[1] * (x10 - x00), y1 = x01 + tt[1] * (x11 - x01);
            return y0 + tt[2] * (y1 - y0);
        };
        auto wallGrowth = [&](double px, double py, double pz, double f0, double ux, double uy, double uz) -> double {
            const double sMax = 1.1 * tWall;
            const double qx = px - f0 * ux, qy = py - f0 * uy, qz = pz - f0 * uz;          // (the foot: the nearest point of the surface)
            const double tolFoot = 0.1 * tWall;
            const double on = std::max(0.0, std::min(1.0, (tolFoot - std::fabs(sampleF(qx, qy, qz))) / tolFoot));
            if (on <= 0.0) return 0.0;
            double prev = -1.0e-3 * tWall, chord = sMax;
            for (int s = 1; s <= wallSteps; ++s)
            {
                const double d = sMax * double(s) / wallSteps;
                const double v = sampleF(qx - d * ux, qy - d * uy, qz - d * uz);
                if (v >= 0.0 && prev < 0.0)
                {
                    const double d0 = sMax * double(s - 1) / wallSteps;
                    chord = d0 + (d - d0) * (-prev) / (v - prev + 1.0e-12);
                    break;
                }
                prev = v;
            }
            const double valid = std::max(0.0, std::min(1.0, (chord - 0.08 * tWall) / (0.04 * tWall)));
            const double gate = std::max(0.0, std::min(1.0, (tWall - chord) / (0.05 * tWall)));
            return std::min(0.5 * tWall, std::max(0.0, 0.5 * (tWall - chord))) * gate * valid * on;
        };
        const int sphereCount = 96;
        std::vector<double> dirs(size_t(3 * sphereCount));
        for (int s = 0; s < sphereCount; ++s)
        {
            const double zc = 1.0 - 2.0 * (s + 0.5) / sphereCount;
            const double rr = std::sqrt(std::max(0.0, 1.0 - zc * zc));
            const double phi = 2.399963229728653 * s;          // (the golden angle: a Fibonacci spiral over the sphere)
            dirs[size_t(3 * s)] = rr * std::cos(phi);
            dirs[size_t(3 * s + 1)] = rr * std::sin(phi);
            dirs[size_t(3 * s + 2)] = zc;
        }
        auto cornerDeficit = [&](double px, double py, double pz, double f0, double ux, double uy, double uz, double slope) -> double {
            const double r = tWall;
            double best = std::max(f0, sampleF(px + r * ux, py + r * uy, pz + r * uz));
            for (int s = 0; s < sphereCount; ++s)
                best = std::max(best, sampleF(px + r * dirs[size_t(3 * s)], py + r * dirs[size_t(3 * s + 1)], pz + r * dirs[size_t(3 * s + 2)]));
            return std::max(0.0, f0 + slope * r - best - 0.08 * r);          // (8 % of the radius of tolerance: the 26 directions can read the slope up to 7.5 % short, so a flat face, a convex edge and the slope error left after the renormalisation fill nothing; the deficit of a corner of 90 degrees, 29 % of r, keeps 21 %)
        };
        std::atomic<uint64_t> grownNodes(0);
        parallelFor(size_t(nz), [&](size_t k) {
            uint64_t grown = 0;
            if (k >= 1 && k + 1 < size_t(nz))
            {
                for (int j = 1; j + 1 < ny; ++j)
                    for (int i = 1; i + 1 < nx; ++i)
                    {
                        const size_t at = (k * ny + j) * nx + i;
                        const float f0 = F[at];
                        if (!(f0 > 0.0f && f0 < capD)) continue;
                        const float xm = F[at - 1], xp = F[at + 1], ym = F[at - nx], yp = F[at + nx], zm = F[at - plane], zp = F[at + plane];
                        if (std::fabs(xm) >= bad || std::fabs(xp) >= bad || std::fabs(ym) >= bad || std::fabs(yp) >= bad || std::fabs(zm) >= bad || std::fabs(zp) >= bad) continue;
                        const double gx = (double(xp) - double(xm)) / (2.0 * h), gy = (double(yp) - double(ym)) / (2.0 * h), gz = (double(zp) - double(zm)) / (2.0 * h);
                        const double mag = std::sqrt(gx * gx + gy * gy + gz * gz);
                        double delta = 0.0;
                        if (P.mode == 2)
                        {
                            if (mag < 0.3) { Dl[at] = 0.0f; continue; }          // (no normal here: nothing grows, and the node keeps the part's own value)
                            const double nd = gz / mag;
                            const double wall = std::sqrt(std::max(1.0 - nd * nd, 1.0e-6));
                            const double lack = std::max(0.0, tanT - std::fabs(nd) / wall);
                            delta = std::max(0.0, top - double(k) * h) * lack * wall;
                        }
                        else if (P.mode == 3)
                        {
                            if (mag < 0.9) { Dl[at] = 0.0f; continue; }          // (the slope of a distance: the formula's gate)
                            delta = wallGrowth(double(i) * h, double(j) * h, double(k) * h, double(f0), gx / mag, gy / mag, gz / mag);
                        }
                        else
                        {
                            if (mag < 0.3) { Dl[at] = 0.0f; continue; }          // (no normal here: no ball to roll on)
                            // (the field is a distance here after the renormalisation above: open air gives the value plus the radius, slope 1)
                            delta = cornerDeficit(double(i) * h, double(j) * h, double(k) * h, double(f0), gx / mag, gy / mag, gz / mag, 1.0);
                        }
                        Dl[at] = float(delta);                              // (0 where nothing grows: the node holds the part's own value, not a large one -- a cell between a node that grows and one that does not would otherwise cut the growth short)
                        if (double(f0) < delta) ++grown;
                    }
            }
            grownNodes += grown;
        });
        std::vector<float> V(nodes, capD);
        std::atomic<uint64_t> partCount(0);
        parallelFor(size_t(nz), [&](size_t k) {
            uint64_t parts = 0;
            for (int j = 0; j < ny; ++j)
                for (int i = 0; i < nx; ++i)
                {
                    const size_t at = (k * ny + j) * nx + i;
                    const float f0 = F[at];
                    if (f0 > 0.0f)
                    {
                        if (Dl[at] >= 0.0f) V[at] = std::min(capD, f0 - Dl[at]);
                        continue;
                    }
                    ++parts;
                    float best = 0.0f;
                    for (int dk = -1; dk <= 1; ++dk)
                    {
                        const long kk = long(k) + dk;
                        if (kk < 0 || kk >= nz) continue;
                        for (int dj = -1; dj <= 1; ++dj)
                        {
                            const int jj = j + dj;
                            if (jj < 0 || jj >= ny) continue;
                            for (int di = -1; di <= 1; ++di)
                            {
                                const int ii = i + di;
                                if (ii < 0 || ii >= nx) continue;
                                best = std::max(best, Dl[(size_t(kk) * ny + jj) * nx + ii]);
                            }
                        }
                    }
                    const float own = f0 <= -bad ? -capD : std::max(f0, -capD);
                    V[at] = own - best;
                }
            partCount += parts;
        });
        return finishLattice(P, std::move(V), capD, (P.mode == 3 ? "walls-lattice" : (P.mode == 5 ? "corners-ball-lattice" : "draft-lattice")), stats, double(grownNodes.load()) * h * h * h, double(nodes), double(looked.load()), tSample, secondsSince(t0),
                             double(partCount.load()));
    }

    // The CORNER ZONE (mode 4): where the corner formula has to be asked at all, for a ball too small for the lattice to hold (`minZ` is its radius).  The part's value was looked at in the air out to
    // 1.2 radii and more (the sampler's `band`).  An inside corner, or any concave place tighter than the ball, is a CREASE of that value -- the distance to the nearer of two faces has a kink on their
    // bisector, and the slope read across it by a central difference over the lattice is well under a distance's, 1 -- and the ball's fill lies within about a radius and a half of it.  So the nodes of the
    // air within 1.2 radii of the part whose slope is under 0.9 are the creases, and the zone is every node within 1.5 radii (and two nodes) of one, by a distance transform.  The field holds -1000 in the
    // zone and +1000 everywhere else: united by a `max` with the formula it lets the mesher drop the formula wherever the zone's range is above that of the formula, which is everywhere but at the corners
    if (P.mode == 4)
    {
        const double r = P.minZ;
        const float capZ = 1000.0f;
        const size_t plane = size_t(nx) * ny;
        const float bad = 0.5f * kBound;
        const float airLimit = float(1.2 * r);
        const double jumpLimit = std::max(0.2, std::min(1.0, h / (1.3 * r)));          // (over one node a concave arc of radius R turns the gradient by h / R: flagged where R is under 1.3 radii)
        std::vector<float> E(nodes, 1.0e18f);
        std::atomic<uint64_t> creases(0);
        parallelFor(size_t(nz), [&](size_t k) {
            uint64_t cnt = 0;
            if (k >= 1 && k + 1 < size_t(nz))
            {
                for (int j = 1; j + 1 < ny; ++j)
                    for (int i = 1; i + 1 < nx; ++i)
                    {
                        const size_t at = (k * ny + j) * nx + i;
                        const float f0 = F[at];
                        if (!(f0 > 0.0f && f0 < airLimit)) continue;
                        const float xm = F[at - 1], xp = F[at + 1], ym = F[at - nx], yp = F[at + nx], zm = F[at - plane], zp = F[at + plane];
                        if (std::fabs(xm) >= bad || std::fabs(xp) >= bad || std::fabs(ym) >= bad || std::fabs(yp) >= bad || std::fabs(zm) >= bad || std::fabs(zp) >= bad) continue;
                        const double gx = (double(xp) - double(xm)) / (2.0 * h), gy = (double(yp) - double(ym)) / (2.0 * h), gz = (double(zp) - double(zm)) / (2.0 * h);
                        // (a crease is where the gradient read forward and the gradient read backward JUMP -- by more than the arc of a ball a third bigger than the radius turns them over one node -- whatever the
                        // length of the slope is, which wanders by a tenth or more in a field that is only roughly a distance: with the length under 0.9 as the test the zone covered most of the bracket's surface)
                        const double fx = (double(xp) - double(f0)) / h, fy = (double(yp) - double(f0)) / h, fz = (double(zp) - double(f0)) / h;
                        const double bx = (double(f0) - double(xm)) / h, by = (double(f0) - double(ym)) / h, bz = (double(f0) - double(zm)) / h;
                        const double jump = std::sqrt((fx - bx) * (fx - bx) + (fy - by) * (fy - by) + (fz - bz) * (fz - bz));
                        if (jump > jumpLimit)
                        {
                            E[at] = 0.0f;
                            ++cnt;
                        }
                    }
            }
            creases += cnt;
        });
        edt3(E, nx, ny, nz);
        const double reachNodes = (1.5 * r + 2.0 * h) / h;
        const double limit2 = reachNodes * reachNodes;
        std::vector<float> V(nodes, capZ);
        std::atomic<uint64_t> zoneNodes(0);
        parallelFor(size_t(nz), [&](size_t k) {
            uint64_t cnt = 0;
            for (size_t q = 0; q < plane; ++q)
            {
                const size_t at = k * plane + q;
                if (double(E[at]) <= limit2)
                {
                    V[at] = -capZ;
                    ++cnt;
                }
            }
            zoneNodes += cnt;
        });
        return finishLattice(P, std::move(V), capZ, "corner-zone-lattice", stats, double(zoneNodes.load()) * h * h * h, double(nodes), double(looked.load()), tSample, secondsSince(t0),
                             double(creases.load()));
    }

    // (the lattice's own business: the complement for a cut, and what is held outside it)
    parallelFor(size_t(nz), [&](size_t k) {
        const double z = double(k) * h;
        const bool held = z >= P.forceLo && z <= P.forceHi;
        float* slice = &F[k * size_t(nx) * ny];
        for (size_t q = 0; q < size_t(nx) * ny; ++q)
        {
            float v = P.negate ? -slice[q] : slice[q];
            if (held) v = kBound;
            slice[q] = v;
        }
    });

    // The UNDERCUT FILL (a negative slope): what lies both under part of the part and over part of it along the lattice's up axis is filled -- in every column, the air between the first and the last
    // node of the part (the formula `_remedy_undercut` is a minimum of the part's field over the places above and over the places below a point: part above AND part below).  The field holds
    // -h at the nodes of such a column from its first to its last node of the part (the part's own nodes there too, so that the part united with the field has no skin of interpolation) and
    // `cap` everywhere else: the surface of the fill is the part's own faces at the two ends of each cavity, and, at the side, a wall between the columns that have a cavity and those that have none,
    // within half a node of the edge of what lies over it.  One read of the part and one of the field at a point.
    // The CLOSING (a slope of -2 or less; `reach` is the radius of the ball): the part with a ball of that radius rolled in the air round it -- every inside corner rounded to the radius, every slot or hole
    // narrower than the ball filled -- worked out on the lattice by two distance transforms: the nodes within a radius of the part (the part grown by the ball), then the nodes that are a radius or
    // more inside what is left of the air beyond it (the grown part shrunk by the ball again).  Only the nodes of the AIR that lie well inside that closing (more than three quarters of a node) are the fill;
    // the nodes round them hold the true value (so that the surface of the fill is placed between nodes), every other node `cap`: where the closing is the part itself -- a flat face, a convex edge --
    // the field is `cap` and the part united with it is the part, with no skin of the staircase of a distance transform on a lattice
    if (P.tanSlope <= -1.5)
    {
        const double r = P.reach;
        const float INF = 1.0e18f;
        std::vector<float> D(nodes);
        parallelFor(size_t(nz), [&](size_t k) {
            for (size_t q = 0; q < size_t(nx) * ny; ++q) D[k * size_t(nx) * ny + q] = F[k * size_t(nx) * ny + q] <= 0.0f ? 0.0f : INF;
        });
        edt3(D, nx, ny, nz);
        // (the part grown by the ball: a node is in it when its distance to the nearest node of the part, less half a node -- the face lies between nodes -- is within the radius)
        std::vector<float> E(nodes);
        parallelFor(size_t(nz), [&](size_t k) {
            for (size_t q = 0; q < size_t(nx) * ny; ++q)
            {
                const double d = std::sqrt(double(D[k * size_t(nx) * ny + q])) * h;
                E[k * size_t(nx) * ny + q] = (F[k * size_t(nx) * ny + q] <= 0.0f || d - 0.5 * h <= r) ? INF : 0.0f;
            }
        });
        edt3(E, nx, ny, nz);
        const float capR = float(r + 0.5 * h);
        const float delta = float(0.75 * h);
        std::vector<float> G(nodes);
        std::vector<uint8_t> core(nodes, 0);
        std::atomic<uint64_t> filledNodes(0), partCount(0);
        parallelFor(size_t(nz), [&](size_t k) {
            uint64_t cnt = 0, parts = 0;
            for (size_t q = 0; q < size_t(nx) * ny; ++q)
            {
                const size_t at = k * size_t(nx) * ny + q;
                const double e = std::sqrt(double(E[at])) * h;
                const float g = float(r - (e - 0.5 * h));
                G[at] = g;
                if (F[at] <= 0.0f) ++parts;
                else if (g <= -delta) { core[at] = 1; ++cnt; }
            }
            filledNodes += cnt;
            partCount += parts;
        });
        std::vector<float> V(nodes, capR);
        parallelFor(size_t(nz), [&](size_t k) {
            for (int j = 0; j < ny; ++j)
                for (int i = 0; i < nx; ++i)
                {
                    const size_t at = (k * ny + j) * nx + i;
                    if (core[at]) { V[at] = G[at]; continue; }
                    bool next = false;
                    for (int dk = -1; dk <= 1 && !next; ++dk)
                    {
                        const long kk = long(k) + dk;
                        if (kk < 0 || kk >= nz) continue;
                        for (int dj = -1; dj <= 1 && !next; ++dj)
                        {
                            const int jj = j + dj;
                            if (jj < 0 || jj >= ny) continue;
                            for (int di = -1; di <= 1; ++di)
                            {
                                const int ii = i + di;
                                if (ii < 0 || ii >= nx) continue;
                                if (core[(size_t(kk) * ny + jj) * nx + ii]) { next = true; break; }
                            }
                        }
                    }
                    if (next) V[at] = std::min(G[at], capR);
                }
        });
        return finishLattice(P, std::move(V), capR, "closing-lattice", stats, double(filledNodes.load()) * h * h * h, double(nodes), double(looked.load()), tSample, secondsSince(t0),
                             double(partCount.load()));
    }
    if (P.tanSlope < 0.0)
    {
        const size_t columns = size_t(nx) * ny;
        const float capU = float(2.0 * P.reach);
        std::atomic<uint64_t> filled(0), partNodes(0);
        parallelFor(columns, [&](size_t c) {
            int first = -1, last = -1;
            for (int k = 0; k < nz; ++k)
            {
                if (F[size_t(k) * columns + c] <= 0.0f)
                {
                    if (first < 0) first = k;
                    last = k;
                }
            }
            bool cavity = false;
            if (first >= 0)
                for (int k = first + 1; k < last && !cavity; ++k)
                    if (F[size_t(k) * columns + c] > 0.0f) cavity = true;
            uint64_t added = 0, nodesOfPart = 0;
            for (int k = 0; k < nz; ++k)
            {
                float& v = F[size_t(k) * columns + c];
                const bool isPart = v <= 0.0f;
                if (isPart) ++nodesOfPart;
                if (cavity && k >= first && k <= last)
                {
                    if (!isPart) ++added;
                    v = -float(h);
                }
                else
                {
                    v = capU;
                }
            }
            filled += added;
            partNodes += nodesOfPart;
        });
        auto data = std::make_shared<LatticeData>();
        for (int a = 0; a < 3; ++a)
        {
            data->n[a] = P.n[a];
            data->o[a] = P.origin[a];
            for (int c = 0; c < 3; ++c) data->e[a][c] = P.axes[3 * a + c];
        }
        data->h = h;
        data->cap = capU;
        data->g = std::move(F);
        {
            uint64_t hash = 1469598103934665603ull;
            const unsigned char* bytes = reinterpret_cast<const unsigned char*>(data->g.data());
            const size_t total = data->g.size() * sizeof(float);
            for (size_t i = 0; i < total; ++i)
            {
                hash ^= bytes[i];
                hash *= 1099511628211ull;
            }
            char buf[160];
            std::snprintf(buf, sizeof(buf), "undercut-lattice#%016llx#%d#%d#%d#%.9g", (unsigned long long)hash, nx, ny, nz, h);
            data->key = buf;
        }
        data->buildPyramid();
        if (stats)
        {
            stats[0] = 0.0;
            stats[1] = double(filled.load()) * h * h * h;          // (mm^3 of air in a cavity, counted on the nodes)
            stats[2] = double(nodes);
            stats[3] = double(looked.load());
            stats[4] = tSample;
            stats[5] = secondsSince(t0);
            stats[6] = double(partNodes.load());
            stats[7] = double(nodes) * sizeof(float) * 1.15 / (1024.0 * 1024.0) * 2.0;
        }
        return Tree(std::make_unique<LatticeClause>(data));
    }

    // 2. In each slice, the distance from the part to the air, kept as W = z - D / slope for the nodes of the part (and infinity for the air)
    const float slope = float(P.tanSlope);
    const float limit = slope * (2.0f * float(P.reach) + 4.0f * float(h));          // (a disc as wide as that fits wherever the part is that far from the air: no need for the distance beyond it)
    const float inf = std::numeric_limits<float>::infinity();
    std::vector<float> Wv(nodes);
    {
        parallelFor(size_t(nz), [&](size_t k) {
            SliceWork work;
            std::vector<float> D(size_t(nx) * ny);
            sliceDistance(&F[k * size_t(nx) * ny], nx, ny, float(h), limit, work, D.data());
            float* w = &Wv[k * size_t(nx) * ny];
            const float* f = &F[k * size_t(nx) * ny];
            const float z = float(k * h);
            for (size_t q = 0; q < size_t(nx) * ny; ++q) w[q] = f[q] <= 0 ? z - D[q] / slope : inf;
        });
    }
    const double tSlices = secondsSince(t0);

    // 3. In each column, the lowest height the gusset reaches below each stretch of the part.  The field is `cap` -- far from any gusset -- everywhere but in a gusset and at the nodes of the
    // part right next to it, which hold the same formula (negative: the gusset goes on into the part, with no gap between them, and the surface of the part under a cut is strictly inside what
    // is cut, not touching 0 where a node happens to lie on it).  So the part united with the field is the part to the value away from a gusset: no skin of interpolation round its edges, and
    // the mesher, which meets `cap` in every cell that is nearer to the part than that, drops the field from the cell.
    const double norm = std::sqrt(1.0 + 1.0 / (double(slope) * slope));
    const float cap = float(2.0 * P.reach);
    const int reachNodes = int(std::ceil(P.reach / h));
    std::atomic<uint64_t> gusset(0), inside(0);
    const size_t columns = size_t(nx) * ny;
    std::vector<uint8_t> S(nodes, 0);                 // (1 where a node of the air has a gusset in it)
    std::vector<uint8_t> has(columns, 0);             // (1 where a column has such a node)
    // (the stretches of the part in a column: for every node the first node of its stretch, and for a node of the air the nearest node of the part above it)
    auto stretches = [&](size_t c, std::vector<int>& bottom, std::vector<int>& above) {
        for (int k = 0; k < nz; ++k)
        {
            if (std::isfinite(Wv[size_t(k) * columns + c]))
            {
                bottom[size_t(k)] = (k > 0 && bottom[size_t(k - 1)] >= 0) ? bottom[size_t(k - 1)] : k;
            }
            else
            {
                bottom[size_t(k)] = -1;
            }
        }
        int nearest = -1;
        for (int k = nz - 1; k >= 0; --k)
        {
            if (bottom[size_t(k)] >= 0) nearest = k;
            above[size_t(k)] = nearest;
        }
    };
    // (the lowest height the gusset reaches under a stretch of the part, from the first node `s` of it: the least of z - D / slope up to a reach above)
    auto reachOf = [&](size_t c, int s, std::vector<float>& M) {
        if (!std::isfinite(M[size_t(s)]))
        {
            float m = inf;
            for (int q = s; q <= std::min(nz - 1, s + reachNodes); ++q) m = std::min(m, Wv[size_t(q) * columns + c]);
            M[size_t(s)] = m;
        }
        return M[size_t(s)];
    };
    parallelFor(columns, [&](size_t c) {
        std::vector<int> bottom(static_cast<size_t>(nz), -1), above(static_cast<size_t>(nz), -1);          // (not `bottom(size_t(nz))`: that reads as a function)
        std::vector<float> M(static_cast<size_t>(nz), inf);
        stretches(c, bottom, above);
        uint64_t added = 0, partNodes = 0;
        for (int k = 0; k < nz; ++k)
        {
            const size_t at = size_t(k) * columns + c;
            float g = cap;
            if (bottom[size_t(k)] >= 0)
            {
                ++partNodes;
            }
            else
            {
                const int s = above[size_t(k)];
                const double z = double(k) * h;
                if (s >= 0 && (s - k) * h <= P.reach && z >= P.minZ)
                {
                    g = std::max(-cap, std::min(cap, float((double(reachOf(c, s, M)) - z) / norm)));
                    if (g <= 0)
                    {
                        S[at] = 1;
                        has[c] = 1;
                        ++added;
                    }
                }
            }
            F[at] = g;
        }
        gusset += added;
        inside += partNodes;
    });
    // The nodes of the part that touch a gusset (a gusset node among the 26 round them): the same formula as the air under them
    parallelFor(columns, [&](size_t c) {
        const int ci = int(c % size_t(nx)), cj = int(c / size_t(nx));
        bool any = false;
        for (int dj = -1; dj <= 1 && !any; ++dj)
            for (int di = -1; di <= 1 && !any; ++di)
            {
                const int ii = ci + di, jj = cj + dj;
                if (ii >= 0 && jj >= 0 && ii < nx && jj < ny && has[size_t(jj) * nx + ii]) any = true;
            }
        if (!any) return;
        std::vector<int> bottom(static_cast<size_t>(nz), -1), above(static_cast<size_t>(nz), -1);
        std::vector<float> M(static_cast<size_t>(nz), inf);
        stretches(c, bottom, above);
        for (int k = 0; k < nz; ++k)
        {
            if (bottom[size_t(k)] < 0) continue;
            bool touches = false;
            for (int dk = -1; dk <= 1 && !touches; ++dk)
            {
                const int kk = k + dk;
                if (kk < 0 || kk >= nz) continue;
                for (int dj = -1; dj <= 1 && !touches; ++dj)
                    for (int di = -1; di <= 1 && !touches; ++di)
                    {
                        const int ii = ci + di, jj = cj + dj;
                        if (ii >= 0 && jj >= 0 && ii < nx && jj < ny && S[(size_t(kk) * ny + jj) * nx + ii]) touches = true;
                    }
            }
            if (!touches) continue;
            const double z = double(k) * h;
            F[size_t(k) * columns + c] = std::max(-cap, std::min(cap, float((double(reachOf(c, bottom[size_t(k)], M)) - z) / norm)));
        }
    });
    const double tColumns = secondsSince(t0);

    // 4. The field
    auto data = std::make_shared<LatticeData>();
    for (int a = 0; a < 3; ++a)
    {
        data->n[a] = P.n[a];
        data->o[a] = P.origin[a];
        for (int c = 0; c < 3; ++c) data->e[a][c] = P.axes[3 * a + c];
    }
    data->h = h;
    data->cap = cap;
    data->g = std::move(F);
    {
        uint64_t hash = 1469598103934665603ull;
        const unsigned char* bytes = reinterpret_cast<const unsigned char*>(data->g.data());
        const size_t total = data->g.size() * sizeof(float);
        for (size_t i = 0; i < total; ++i)
        {
            hash ^= bytes[i];
            hash *= 1099511628211ull;
        }
        char buf[160];
        std::snprintf(buf, sizeof(buf), "overhang-lattice#%016llx#%d#%d#%d#%.9g", (unsigned long long)hash, nx, ny, nz, h);
        data->key = buf;
    }
    data->buildPyramid();
    if (stats)
    {
        stats[0] = 0.0;
        stats[1] = double(gusset.load()) * h * h * h;          // (mm^3 of air with a gusset in it, counted on the nodes)
        stats[2] = double(nodes);
        stats[3] = double(looked.load());
        stats[4] = tSample;
        stats[5] = secondsSince(t0);
        stats[6] = double(inside.load());
        stats[7] = double(nodes) * sizeof(float) * 1.15 / (1024.0 * 1024.0) * 2.0;
        (void)tSlices;
        (void)tColumns;
    }
    return Tree(std::make_unique<LatticeClause>(data));
}

}   // namespace fields
}   // namespace libfive
