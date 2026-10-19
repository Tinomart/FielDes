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
        if (o.first.isSafe() && o.first.upper() < 0)
        {
            fill(lo, hi, -kBound);
        }
        else if (o.first.isSafe() && o.first.lower() > 0)
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
    if (nx < 3 || ny < 3 || nz < 3 || !(h > 0) || !(P.tanSlope > 0) || !(P.reach > 0)) return fail(1);
    const size_t nodes = size_t(nx) * ny * nz;
    if (nodes > size_t(160) * 1000 * 1000) return fail(2);          // (a lattice of more than 160 million nodes: a coarser pitch is asked for)

    // 1. The part at the nodes
    std::vector<float> F(nodes, kBound);
    std::atomic<uint64_t> looked(0);
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
