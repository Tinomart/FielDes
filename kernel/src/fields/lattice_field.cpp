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

// The squared distance transform of Felzenszwalb and Huttenlocher (index units: 0 at a source, a large value elsewhere), here carrying with every value the number of the source it came from
// (`s`: a number for each node, the source's own at a source, anything where there is none): the number of the nearest source of every node.  One line of `n` values
static void dt1s(float* f, uint32_t* s, int n, std::vector<int>& v, std::vector<double>& z, std::vector<double>& out, std::vector<uint32_t>& outs)
{
    if (n <= 1) return;
    v.assign(size_t(n), 0);
    z.assign(size_t(n) + 1, 0.0);
    out.assign(size_t(n), 0.0);
    outs.assign(size_t(n), 0u);
    int k = 0;
    v[0] = 0;
    z[0] = -1.0e30;
    z[1] = 1.0e30;
    auto cross = [&](int q, int p) { return ((double(f[q]) + double(q) * q) - (double(f[p]) + double(p) * p)) / (2.0 * q - 2.0 * p); };
    for (int q = 1; q < n; ++q)
    {
        double sx = cross(q, v[size_t(k)]);
        while (sx <= z[size_t(k)])
        {
            --k;
            sx = cross(q, v[size_t(k)]);
        }
        ++k;
        v[size_t(k)] = q;
        z[size_t(k)] = sx;
        z[size_t(k) + 1] = 1.0e30;
    }
    k = 0;
    for (int q = 0; q < n; ++q)
    {
        while (z[size_t(k) + 1] < q) ++k;
        const double dq = double(q - v[size_t(k)]);
        out[size_t(q)] = dq * dq + double(f[v[size_t(k)]]);
        outs[size_t(q)] = s[v[size_t(k)]];
    }
    for (int q = 0; q < n; ++q)
    {
        f[q] = float(std::min(out[size_t(q)], 1.0e18));
        s[q] = outs[size_t(q)];
    }
}

// The squared distance (index units) to the nearest source over the three axes, and which source it is (D holds 0 at a source and a large value elsewhere, S the source's number at a source)
static void edt3src(std::vector<float>& D, std::vector<uint32_t>& S, int nx, int ny, int nz)
{
    parallelFor(size_t(ny) * nz, [&](size_t line) {
        std::vector<int> v;
        std::vector<double> z, out;
        std::vector<uint32_t> outs;
        dt1s(&D[line * size_t(nx)], &S[line * size_t(nx)], nx, v, z, out, outs);
    });
    parallelFor(size_t(nz) * nx, [&](size_t id) {
        const size_t k = id / size_t(nx), i = id % size_t(nx);
        std::vector<int> v;
        std::vector<double> z, out;
        std::vector<uint32_t> outs;
        std::vector<float> col(static_cast<size_t>(ny));
        std::vector<uint32_t> cs(static_cast<size_t>(ny));
        for (int j = 0; j < ny; ++j) { col[size_t(j)] = D[(k * ny + j) * nx + i]; cs[size_t(j)] = S[(k * ny + j) * nx + i]; }
        dt1s(col.data(), cs.data(), ny, v, z, out, outs);
        for (int j = 0; j < ny; ++j) { D[(k * ny + j) * nx + i] = col[size_t(j)]; S[(k * ny + j) * nx + i] = cs[size_t(j)]; }
    });
    parallelFor(size_t(ny) * nx, [&](size_t id) {
        const size_t j = id / size_t(nx), i = id % size_t(nx);
        std::vector<int> v;
        std::vector<double> z, out;
        std::vector<uint32_t> outs;
        std::vector<float> col(static_cast<size_t>(nz));
        std::vector<uint32_t> cs(static_cast<size_t>(nz));
        for (int k = 0; k < nz; ++k) { col[size_t(k)] = D[(size_t(k) * ny + j) * nx + i]; cs[size_t(k)] = S[(size_t(k) * ny + j) * nx + i]; }
        dt1s(col.data(), cs.data(), nz, v, z, out, outs);
        for (int k = 0; k < nz; ++k) { D[(size_t(k) * ny + j) * nx + i] = col[size_t(k)]; S[(size_t(k) * ny + j) * nx + i] = cs[size_t(k)]; }
    });
}

// The sources of a distance transform of a surface: the points where the sign of `V` changes along an edge of the lattice (the two ends of opposite sign, or one of them 0), found by linear interpolation along it --
// the point where the value is 0, which on a face is the face's own point and at an edge of the part is a point of one of its two faces within a node of the edge, where the foot of a perpendicular taken from a
// node's gradient (a mix of the two faces' there) fell up to a node off it.  Each point is given to the nearer end of its edge; ends with a value of `limit` or more were never looked at and are left out.  On return
// `E` is 0 at a node that holds a source and a large value elsewhere, `Src` the number of the source (or none), `foot` the points, three floats each
static void signChangeSources(const std::vector<float>& V, float limit, int nx, int ny, int nz, double h, std::vector<float>& E, std::vector<uint32_t>& Src, std::vector<float>& foot, std::vector<uint32_t>& srcNode)
{
    struct Seed { uint32_t node; float x, y, z; };
    const uint32_t none = 0xFFFFFFFFu;
    std::vector<std::vector<Seed>> slab(static_cast<size_t>(nz));
    parallelFor(size_t(nz), [&](size_t k) {
        auto& out = slab[k];
        for (int j = 0; j < ny; ++j)
            for (int i = 0; i < nx; ++i)
            {
                const size_t at = (k * ny + j) * nx + i;
                const float va = V[at];
                if (std::fabs(va) >= limit) continue;
                for (int axis = 0; axis < 3; ++axis)
                {
                    const int ii = i + (axis == 0 ? 1 : 0), jj = j + (axis == 1 ? 1 : 0);
                    const size_t kk = k + (axis == 2 ? 1 : 0);
                    if (ii >= nx || jj >= ny || kk >= size_t(nz)) continue;
                    const size_t bt = (kk * ny + jj) * nx + ii;
                    const float vb = V[bt];
                    if (std::fabs(vb) >= limit || !(va * vb <= 0.0f) || va == vb) continue;
                    const double t = double(va) / (double(va) - double(vb));
                    Seed s;
                    s.node = uint32_t(t < 0.5 ? at : bt);
                    s.x = float((double(i) + (axis == 0 ? t : 0.0)) * h);
                    s.y = float((double(j) + (axis == 1 ? t : 0.0)) * h);
                    s.z = float((double(k) + (axis == 2 ? t : 0.0)) * h);
                    out.push_back(s);
                }
            }
    });
    E.assign(V.size(), 1.0e18f);
    Src.assign(V.size(), none);
    foot.clear();
    srcNode.clear();
    const size_t plane = size_t(nx) * ny;
    auto away = [&](uint32_t node, float x, float y, float z) {          // (how far the point is from the node)
        const double dx = double(x) - double(node % plane % size_t(nx)) * h, dy = double(y) - double((node % plane) / size_t(nx)) * h, dz = double(z) - double(node / plane) * h;
        return dx * dx + dy * dy + dz * dz;
    };
    for (size_t k = 0; k < size_t(nz); ++k)
        for (const Seed& s : slab[k])
        {
            if (Src[s.node] != none)
            {
                // (the node has a source already: the one that lies nearer to it stays -- the others are the crossings of the other edges of the node, and those are looked at again from the nodes round it)
                const size_t id = Src[s.node];
                if (away(s.node, s.x, s.y, s.z) < away(s.node, foot[3 * id], foot[3 * id + 1], foot[3 * id + 2]))
                {
                    foot[3 * id] = s.x;
                    foot[3 * id + 1] = s.y;
                    foot[3 * id + 2] = s.z;
                }
                continue;
            }
            Src[s.node] = uint32_t(foot.size() / 3);
            E[s.node] = 0.0f;
            srcNode.push_back(s.node);
            foot.push_back(s.x);
            foot.push_back(s.y);
            foot.push_back(s.z);
        }
}

// The distance of the node (i, j, k) to the surface, after the distance transform has found its nearest source `id`: the transform chooses by the distance to the NODE that holds a source, and the crossing point of
// a source lies up to half a node from its node, so a source a node farther can hold a point that is nearer.  The distance is the least over the points of the sources held by the 27 nodes round the chosen one
static double nearestSourceDistance(int i, int j, int k, uint32_t id, int nx, int ny, int nz, double h, const std::vector<float>& E, const std::vector<uint32_t>& Src, const std::vector<uint32_t>& srcNode,
                                    const std::vector<float>& foot)
{
    const size_t plane = size_t(nx) * ny;
    const size_t sn = srcNode[id];
    const int si = int(sn % size_t(nx)), sj = int((sn / size_t(nx)) % size_t(ny)), sk = int(sn / plane);
    const double px = double(i) * h, py = double(j) * h, pz = double(k) * h;
    double best = 1.0e30;
    for (int dk = -1; dk <= 1; ++dk)
    {
        const int kk = sk + dk;
        if (kk < 0 || kk >= nz) continue;
        for (int dj = -1; dj <= 1; ++dj)
        {
            const int jj = sj + dj;
            if (jj < 0 || jj >= ny) continue;
            for (int di = -1; di <= 1; ++di)
            {
                const int ii = si + di;
                if (ii < 0 || ii >= nx) continue;
                const size_t nb = (size_t(kk) * ny + jj) * nx + ii;
                if (E[nb] != 0.0f) continue;          // (not a node that holds a source: the transform wrote the nearest source's number there)
                const size_t sid = Src[nb];
                const double dx = px - double(foot[3 * sid]), dy = py - double(foot[3 * sid + 1]), dz = pz - double(foot[3 * sid + 2]);
                best = std::min(best, dx * dx + dy * dy + dz * dz);
            }
        }
    }
    return std::sqrt(best);
}

// The part's field at the nodes made a TRUE signed distance (negative inside), as far as `farLimit` from the surface: first renormalised by its own slope, then, from 1.5 nodes away from the surface, replaced by the
// Euclidean distance to the nearest point of the surface.  `F` holds the sampled values (`bad` or more: a node of a block that was not looked at, only its sign is known).
// (The part's field is renormalised to a distance by its own slope before any ball is rolled: the ball's test -- the value gains the radius, one for one, as the centre moves away -- holds for a
// distance and not for a field whose slope wanders by a tenth or more, like the topology bracket's: there every node was a little "short" of open air and the fill came out as a skin of shards, 1,490 mm^3
// where the formula says 62.  The slope at a node is the steepest change of the value towards any of the 26 neighbours -- one for one for a distance, on either side of a crease, where a central
// difference dips -- and the value is divided by it, held between 0.6 and 1.6.
// The field of a union of boxes is, beyond an edge or a corner, the largest of the distances to the planes of a box -- more than the true distance -- and a ball rolled on that came out a fifth short of its
// volume in the inside corner of an L.  So every node but the first ones holds the true Euclidean distance, found on the lattice itself: the points where the value changes sign along an edge of the lattice are
// the sources (`signChangeSources`: points of the surface itself, also along the sharp rim of a hole, where a foot taken from a node's gradient was up to a node off and moved the sag of a ball resting on the
// rim by a quarter of a millimetre), a distance transform finds the nearest source of every node, and the distance is to that source's point.  The sign is the field's)
static void trueDistanceField(std::vector<float>& F, int nx, int ny, int nz, double h, float bad, double farLimit)
{
    const size_t nodes = size_t(nx) * ny * nz;
    const size_t plane = size_t(nx) * ny;
    {
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
    const uint32_t none = 0xFFFFFFFFu;
    std::vector<float> E, foot;
    std::vector<uint32_t> Src, srcNode;
    signChangeSources(F, bad, nx, ny, nz, h, E, Src, foot, srcNode);
    if (foot.empty()) return;
    edt3src(E, Src, nx, ny, nz);
    const float nearBand = float(1.5 * h);
    parallelFor(size_t(nz), [&](size_t k) {
        for (size_t q = 0; q < plane; ++q)
        {
            const size_t at = k * plane + q;
            const uint32_t id = Src[at];
            if (id == none) continue;
            const float f0 = F[at];
            if (std::fabs(f0) < nearBand) continue;          // (near the surface the field's own value, renormalised by its slope, is the better distance)
            const int j = int(q / size_t(nx)), i = int(q % size_t(nx));
            const double dist = std::min(nearestSourceDistance(i, j, int(k), id, nx, ny, nz, h, E, Src, srcNode, foot), farLimit);
            F[at] = f0 >= 0.0f ? float(dist) : -float(dist);
        }
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

Tree overhangLattice(const Tree& part, const std::map<Tree::Id, float>& vars, const OverhangLatticeParams& P, double* stats, std::vector<uint8_t>* checkOut)
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

    // The GUSSET OF THE CHECK.  The gusset is the cone under every stretch of the part, and its flank leans 5 % of the tangent inside the angle asked (43.5 degrees for 45: the check reads a face to a few per cent).
    // On a part an optimizer shaped every face is a facet, most of them lean a degree or two past the angle, and the cone puts a wedge of fill under each: a skin a node or two thick, which nothing hangs on and no
    // check marks -- the check lets a face through up to the angle and 5 % of it, 47.25 degrees for 45 -- drawn as the red crescents and shards that stand out of the struts of the topology bracket.  So the cone is
    // made twice: once at the angle the check lets through, to find where a face leans past it (`checkGusset`, the nodes of that gusset), and once at the flank; a piece of the second is kept only where it holds
    // a node of the first.  Where a face needs the fill the flank still lies inside the angle, and where it does not the part is left as it is.  Not for a cut (`negate`): the teardrop's apex is thin on purpose.
    // (0.95 is the share of the tangent that `_remedy_overhang` takes for the flank, 1.05 the grace of the rules: the same numbers)
    std::vector<uint8_t> checkGusset;
    if (!checkOut && !P.negate && P.tanSlope > 0.0 && P.mode != 2 && P.mode != 3 && P.mode != 5 && P.mode != 6 && P.mode != 7)
    {
        OverhangLatticeParams limitParams = P;
        limitParams.tanSlope = std::tan(std::atan(P.tanSlope / 0.95) * 1.05);
        overhangLattice(part, vars, limitParams, nullptr, &checkGusset);
    }

    // 1. The part at the nodes
    std::vector<float> F(nodes, kBound);
    std::atomic<uint64_t> looked(0);
    const double bandD = (P.mode == 2 || P.mode == 3 || P.mode == 5 || P.mode == 6 || P.mode == 7) ? P.reach : 0.0;
    const double bandInD = (P.mode == 3) ? 1.1 * P.minZ + 4.0 * h : ((P.mode == 5) ? P.minZ + 4.0 * h : 0.0);
    // (a field read at the nodes, with the pruning of the mesher: the part, and the kept regions)
    auto sampleTree = [&](const Tree& tree, std::vector<float>& into, std::atomic<uint64_t>& counter, double bandOut, double bandInside) -> bool
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
                    Evaluator ev(tree, vars);
                    const Tape::Handle root = ev.getDeck()->tape;
                    Sampler S{P, {nx, ny, nz}, h, into, ev, counter};
                    S.band = bandOut;
                    S.bandIn = bandInside;
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
        return !failed;
    };
    if (!sampleTree(part, F, looked, bandD, bandInD)) return fail(3);
    const double tSample = secondsSince(t0);

    // The KEPT REGIONS (the gusset, mode 0): `K` is 1 at a node inside one.  A kept region is as the part has it: nothing is added inside it, and nothing is asked of its faces.  The gusset of a stretch of the
    // part is a cone that hangs from the stretch down, and the region's face cuts it where it crosses air that lies inside the region -- the box of a lug stands off the lug's own underside, a box is no disc --:
    // what lies below the cut is left hanging on the part by a line or by one end, a flat-topped V under a lug, a blade of 14 mm with a ragged end under an arm, on the topology bracket.  So (1) a stretch
    // whose first node lies inside a kept region has no gusset under it, and (2) a node of the air is in no gusset when, between it and the stretch above it, there is a node of the air inside a kept region.
    // What hangs from a stretch outside the regions, and reaches the face of a region from the side, is made as ever: a gusset joined to a kept plate from outside is material of the part it was made for.
    std::vector<uint8_t> K;
    if (P.keep && P.keep->is_valid() && !P.negate && P.tanSlope > 0.0 && P.mode == 0)
    {
        std::vector<float> Kv(nodes, kBound);
        std::atomic<uint64_t> keptLooked(0);
        if (!sampleTree(*P.keep, Kv, keptLooked, 0.0, 0.0)) return fail(3);
        K.assign(nodes, 0);
        for (size_t q = 0; q < nodes; ++q) K[q] = Kv[q] <= 0.0f ? 1 : 0;
    }

    // The ACCESS FILL (mode 6: a tool that comes along the third axis from its top; mode 7: from either end).  `minZ` is the radius R of the tool's ball, `tanSlope` the tangent of the taper of its shaft (the
    // shaft is as wide as the ball at the ball's equator and widens by that much for every unit it goes on out: a mould half that has to slide out; a tiny number for a cutter, whose shaft is straight).  The air
    // the tool can touch is the union of the balls it can stand in; what is left of the air is filled.  A ball can stand at a node c when it fits (the part is at least R from it) and the shaft can go on out
    // from there: at every node c' above c in its column the part is at least R + (z' - z) tan from the axis.  The clearance D at a node of the air is the true distance to the part (trueDistanceField); the
    // running least of D(z') - (z' - z) tan, taken down each column from the open end, is the clearance of the whole path, and the node is a centre where it is R or more.  The set of centres has a surface
    // of its own (where the path's clearance, less R, changes sign along an edge of the lattice), the distance to it is found as the distance to the part is (sources, transform), and a point is reached by a
    // ball when that distance is at most R: the field R - (the signed distance to the centres) is negative exactly where the tool cannot go, and its surface is the envelope of the balls -- the arc of a
    // rolling ball in a corner, a wall that leans where the shaft widens, a vertical wall at the edge of a cavity that nothing reaches from either end.  One operator for the undercut, the draft and the radius of
    // the corners: applied to its own result it changes nothing (what it leaves is the union of balls that fit, and the balls still fit), where the remedies done one after another each undid some of the
    // last.  The field holds the least of the part's own distance and that field, so it is the result itself, as the corners' ball is
    if (P.mode == 6 || P.mode == 7)
    {
        const double R = P.minZ, tanT = P.tanSlope;
        const size_t plane = size_t(nx) * ny;
        const float bad = 0.5f * kBound;
        const float capL = float(P.reach);                                           // (what the field holds far from every surface)
        const double dMax = R + tanT * double(nz) * h + 4.0 * h;                     // (the clearance is looked at out to here: the path of a shaft that widens needs no more)
        trueDistanceField(F, nx, ny, nz, h, bad, dMax);
        for (size_t q = 0; q < nodes; ++q) F[q] = std::max(-float(dMax), std::min(float(dMax), F[q]));
        const uint32_t none = 0xFFFFFFFFu;
        // (the field of the fill for one end of the axis: R less the signed distance of each node to the set of centres, a negative number where the tool cannot go)
        auto fillField = [&](bool fromTop) {
            std::vector<float> W(nodes);
            parallelFor(plane, [&](size_t c) {
                double m = 1.0e30;                                                   // (open air beyond the end of the lattice)
                for (int s = 0; s < nz; ++s)
                {
                    const int k = fromTop ? nz - 1 - s : s;
                    const size_t at = size_t(k) * plane + c;
                    const double d = F[at] > 0.0f ? double(F[at]) : 0.0;
                    m = std::min(d, m - tanT * h);
                    W[at] = float(R - m);                                            // (0 or less: a ball fits here and the shaft goes on out)
                }
            });
            std::vector<float> E, foot;
            std::vector<uint32_t> Src, srcNode;
            signChangeSources(W, 1.0e30f, nx, ny, nz, h, E, Src, foot, srcNode);
            const bool anySource = !foot.empty();
            if (anySource) edt3src(E, Src, nx, ny, nz);
            parallelFor(size_t(nz), [&](size_t k) {
                for (size_t q = 0; q < plane; ++q)
                {
                    const size_t at = k * plane + q;
                    const bool centre = W[at] <= 0.0f;
                    double dist = anySource ? 1.0e30 : 0.0;
                    if (anySource && Src[at] != none)
                    {
                        const int j = int(q / size_t(nx)), i = int(q % size_t(nx));
                        dist = nearestSourceDistance(i, j, int(k), Src[at], nx, ny, nz, h, E, Src, srcNode, foot);
                    }
                    double signedDist = centre ? -dist : dist;
                    if (!anySource) signedDist = centre ? -1.0e9 : 1.0e9;          // (no surface of the centres: all of them, or none)
                    W[at] = float(std::max(-double(capL) * 4.0, std::min(double(capL) * 4.0, R - signedDist)));
                }
            });
            return W;
        };
        std::vector<float> G = fillField(true);
        if (P.mode == 7)
        {
            std::vector<float> H2 = fillField(false);
            parallelFor(size_t(nz), [&](size_t k) {
                for (size_t q = 0; q < plane; ++q) G[k * plane + q] = std::max(G[k * plane + q], H2[k * plane + q]);
            });
        }
        std::atomic<uint64_t> filled(0), partCount(0);
        std::vector<float> V(nodes);
        parallelFor(size_t(nz), [&](size_t k) {
            uint64_t f = 0, p = 0;
            for (size_t q = 0; q < plane; ++q)
            {
                const size_t at = k * plane + q;
                const float own = F[at];
                if (own <= 0.0f) ++p;
                else if (G[at] < 0.0f) ++f;
                V[at] = std::max(-capL, std::min(capL, std::min(own, G[at])));
            }
            filled += f;
            partCount += p;
        });
        return finishLattice(P, std::move(V), capL, "access-lattice", stats, double(filled.load()) * h * h * h, double(nodes), double(looked.load()), tSample, secondsSince(t0), double(partCount.load()));
    }

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
        std::vector<float> Fc;                          // (walls: at each node that grows, how well the face the line ends on faces back, 0 elsewhere)
        if (P.mode == 3) Fc.assign(nodes, 0.0f);
        std::vector<int> topMat, botMat;                // (the draft: in each column the highest and the lowest node of the part, to tell a wall under a roof from one in the open)
        int botAll = nz;                                 // (and the lowest node of the part in the whole lattice)
        if (P.mode == 2)
        {
            topMat.assign(plane, -1);
            botMat.assign(plane, nz);
            parallelFor(plane, [&](size_t c) {
                for (int k = 0; k < nz; ++k)
                    if (F[size_t(k) * plane + c] <= 0.0f)
                    {
                        if (botMat[c] == nz) botMat[c] = k;
                        topMat[c] = k;
                    }
            });
            for (size_t c = 0; c < plane; ++c) botAll = std::min(botAll, botMat[c]);
        }
        if (P.mode == 5)
        {
            // (the part's field is made a true distance before any ball is rolled -- see trueDistanceField: renormalised by its own slope, and from 1.5 nodes off the surface the Euclidean distance to it, out to
            // three wall limits)
            trueDistanceField(F, nx, ny, nz, h, bad, 3.0 * tWall);
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
        // (a wall is thin below `t0`, the rule's limit: `P.tanSlope` is its share of the target thickness `tWall`, which has a margin on it; a wall the rule tolerates is not made thicker for that margin)
        const double tThin = tWall * ((P.mode == 3 && P.tanSlope > 0.5 && P.tanSlope <= 1.0) ? P.tanSlope : 1.0);
        // (the growth of a wall at a node is returned in two parts: the growth the thickness asks for, and `facingOut`, how well the face the line ends on faces back, 0..1: the facing is spread a little before it
        // is taken in, see below)
        auto wallGrowth = [&](double px, double py, double pz, double f0, double ux, double uy, double uz, double& facingOut) -> double {
            facingOut = 0.0;
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
            const double gate1 = std::max(0.0, std::min(1.0, (tThin - chord) / (0.05 * tThin)));
            const double gate = gate1 * gate1;          // (eased out to nothing at the limit, and in the grace band by the square: a wall the rule tolerates is left as it is, to a few thousandths of a millimetre)
            if (gate * valid * on <= 0.0) return 0.0;
            // A wall is two faces facing each other: the face the line ends on has to face back at where it began (its outward normal within about 37 degrees of the way the line goes on).  Without it a
            // flat face that a sheet meets at a glancing angle -- the cut face of a lattice -- had a wedge behind it taken for a thin wall, and was pushed OUT past the box it was cut from: a thin lip
            // beyond every cut face, which the check of the result marks
            const double ex = qx - chord * ux, ey = qy - chord * uy, ez = qz - chord * uz, step = 0.5 * tWall * 0.1;
            const double gxe = sampleF(ex + step, ey, ez) - sampleF(ex - step, ey, ez), gye = sampleF(ex, ey + step, ez) - sampleF(ex, ey - step, ez), gze = sampleF(ex, ey, ez + step) - sampleF(ex, ey, ez - step);
            const double ge = std::sqrt(gxe * gxe + gye * gye + gze * gze);
            const double back = ge > 1.0e-12 ? -(gxe * ux + gye * uy + gze * uz) / ge : 0.0;
            facingOut = std::max(0.0, std::min(1.0, (back - 0.7) / 0.1));
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
            double bx = ux, by = uy, bz = uz;
            double bv = sampleF(px + r * ux, py + r * uy, pz + r * uz);
            for (int s = 0; s < sphereCount; ++s)
            {
                const double dx = dirs[size_t(3 * s)], dy = dirs[size_t(3 * s + 1)], dz = dirs[size_t(3 * s + 2)];
                const double v = sampleF(px + r * dx, py + r * dy, pz + r * dz);
                if (v > bv) { bv = v; bx = dx; by = dy; bz = dz; }
            }
            // (the largest value on the sphere is between the 96 directions, which are about 20 degrees apart -- half a millimetre of a ball of 3 -- and the value found short of it by up to that much: the ball was
            // read as smaller than it is, and the corners came out filled to a radius a third too large (a wide slot's floor corners: 122 mm^3 where an arc of the radius is 77).  The best direction is
            // refined by looking round it, eight ways at a smaller angle each round: a ball's centre found to a few hundredths of a millimetre)
            double theta = 0.2;
            for (int pass = 0; pass < 6; ++pass, theta *= 0.5)
            {
                const double ax = std::fabs(bx) < 0.9 ? 1.0 : 0.0, ay = std::fabs(bx) < 0.9 ? 0.0 : 1.0;
                double e1x = by * 0.0 - bz * ay, e1y = bz * ax - bx * 0.0, e1z = bx * ay - by * ax;
                const double n1 = std::sqrt(e1x * e1x + e1y * e1y + e1z * e1z);
                e1x /= n1; e1y /= n1; e1z /= n1;
                const double e2x = by * e1z - bz * e1y, e2y = bz * e1x - bx * e1z, e2z = bx * e1y - by * e1x;
                double nx_ = bx, ny_ = by, nz_ = bz, nv = bv;
                for (int a = 0; a < 8; ++a)
                {
                    const double ca = std::cos(0.7853981633974483 * a), sa = std::sin(0.7853981633974483 * a);
                    double dx = bx + theta * (ca * e1x + sa * e2x), dy = by + theta * (ca * e1y + sa * e2y), dz = bz + theta * (ca * e1z + sa * e2z);
                    const double dn = std::sqrt(dx * dx + dy * dy + dz * dz);
                    dx /= dn; dy /= dn; dz /= dn;
                    const double v = sampleF(px + r * dx, py + r * dy, pz + r * dz);
                    if (v > nv) { nv = v; nx_ = dx; ny_ = dy; nz_ = dz; }
                }
                bx = nx_; by = ny_; bz = nz_; bv = nv;
            }
            const double best = std::max(f0, bv);
            // (the tolerance keeps a flat face and a convex edge exactly as they are, against the noise of reading a lattice: the lattice holds the true distance now, read by trilinear steps and with a ball's centre
            // found to a few hundredths of a millimetre, so the noise is a small part of the NODE, and the tolerance is 0.15 of it, and 3 % of the radius at most.  It was 3 % of the radius whatever the pitch,
            // 0.18 mm for a radius of 6, which cut the tails of every fillet short of its tangent points and left the arc a twelfth smaller than the ball -- 85 % of the volume of an L's fillet --, then 0.4 of the node,
            // which still left 91 %.  Before that it was 8 %, 28 % short of its radius in a corner of 90 degrees)
            return std::max(0.0, f0 + slope * r - best - std::min(0.03 * r, 0.15 * h));
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
                            // (a wall has a face to read only where the field is a distance, or near enough: of a RESULT of this tool it is not off the faces it grew -- the band of the pass that made it ends in a ramp
                            // that rises several times as fast as a distance, and the "wall" the ramp makes follows the old face straight, with no lean at all.  A pass over it grew a skin on the ramp, 0.1 to 0.2 mm
                            // standing off faces that were right already: 56 mm^3 on the two end faces of d5's base on the third pass.  No growth beyond three times a distance's slope, eased in from two (the topology
                            // bracket's own slope wanders between 0.7 and 1.3, the ramp is at 7); `_remedy_draft` has the same gate)
                            const double isDistance = std::max(0.0, std::min(1.0, 3.0 - mag));
                            if (isDistance <= 0.0) { Dl[at] = 0.0f; continue; }
                            const double nd = gz / mag;
                            const double wall = std::sqrt(std::max(1.0 - nd * nd, 1.0e-6));
                            const double lack = std::max(0.0, tanT - std::fabs(nd) / wall);
                            double rise = std::max(0.0, top - double(k) * h);          // (the skin grows with the height below the top of the part: wider at the foot, the half of the mould that is pulled up)
                            const size_t col = size_t(j) * nx + i;
                            if (topMat[col] > int(k) && botMat[col] > int(k))
                            {
                                // (a roof over this node and nothing under it: the half of the mould that forms this wall is the one pulled DOWN, so the wall leans the other way, wider at the top -- the height above the bottom of
                                // the part.  Given the usual skin, wider at the foot, the vertical line a quarter of a millimetre off the wall met the roof above it and the skin below: an undercut that the draft made, 3.5 % of the
                                // surface of the bracket's mould.  The lean of a wall counts the same either way)
                                rise = std::max(0.0, double(int(k) - botAll) * h);
                            }
                            delta = rise * lack * wall * isDistance;
                        }
                        else if (P.mode == 3)
                        {
                            if (mag < 0.3) { Dl[at] = 0.0f; continue; }          // (no normal here: nothing grows)
                            // (the field is a distance only roughly -- a lattice's, a topology bracket's: where its slope is 0.7 or 1.3 the foot is f / slope away, not f, and the surface moves by the value less
                            // slope times the growth.  A gate on the slope left the wall ungrown wherever the slope was under 0.9: white streaks over the gyroid)
                            double facing = 0.0;
                            delta = mag * wallGrowth(double(i) * h, double(j) * h, double(k) * h, double(f0) / mag, gx / mag, gy / mag, gz / mag, facing);
                            if (delta > 0.0) Fc[at] = float(facing);
                        }
                        else
                        {
                            // (no normal on the middle of a gap, where the two faces' slopes cancel -- but that is exactly a ridge of the field, the node is higher than its neighbours on both sides along some axis, and a
                            // ball is rolled there like anywhere else: the deficit does not need the normal, only a first direction to look in.  Skipped, the middle plane of every gap narrower than the ball was a crack of
                            // air down to its floor.  Where there is no ridge and no slope, a plateau of the field, there is no ball to roll on)
                            double ux = 0.0, uy = 0.0, uz = 1.0;
                            if (mag < 0.3)
                            {
                                const bool ridge = (double(f0) - double(xm) > 0.2 * h && double(f0) - double(xp) > 0.2 * h) || (double(f0) - double(ym) > 0.2 * h && double(f0) - double(yp) > 0.2 * h) ||
                                                   (double(f0) - double(zm) > 0.2 * h && double(f0) - double(zp) > 0.2 * h);
                                if (!ridge) { Dl[at] = 0.0f; continue; }
                            }
                            else { ux = gx / mag; uy = gy / mag; uz = gz / mag; }
                            // (the field is a distance here after the renormalisation above: open air gives the value plus the radius, slope 1)
                            delta = cornerDeficit(double(i) * h, double(j) * h, double(k) * h, double(f0), ux, uy, uz, 1.0);
                        }
                        Dl[at] = float(delta);                              // (0 where nothing grows: the node holds the part's own value, not a large one -- a cell between a node that grows and one that does not would otherwise cut the growth short)
                        if (double(f0) < delta) ++grown;
                    }
            }
            grownNodes += grown;
        });
        if (P.mode == 3)
        {
            // (the growth switches on and off from one node to the next where the gates of the wall meet a crease -- the facing of the face a line ends on, the thickness crossing the limit, the rim of the flat face a sheet
            // was cut by -- and a surface that is the part offset by such a growth carries that as a ragged edge: a step a few hundredths of a millimetre wide that the mesher draws as spikes and shards, whatever
            // its resolution (the same at 5 and at 10 cells to the millimetre).  The growth is a smooth function of where it is on the surface in what a wall needs, so it is smoothed: a box filter of a fifth of the wall
            // limit each way (0.3 mm for a wall of 1.6), twice, in each axis, over the nodes that have a growth at all (a node with none counts for nothing).  A filter thins the growth within its reach of the edge
            // of what grows, and where that edge is the edge of the FACING (a wedge behind a flat face, which is not a wall) the thin place on the growing side would be left short of the limit: so the facing is
            // spread by the filter's reach before it is taken in (the largest value within it, in each axis).  The smoothing has to run ACROSS the crease where a cut face meets a sheet -- that is where the rim is
            // ragged -- so it also rounds off the end of a small flat plate by the same reach: that is why a part with an exact distance field keeps the formula, whose ends are exact, and the lattice is for the
            // parts the formula cannot do (see `_remedy_walls`).  The part's own value is not touched: the surface is the part's, moved by a smooth growth)
            const int R = std::max(1, int(std::lround(0.2 * tWall / h)));
            std::vector<float> A(nodes);
            auto lines = [&](int axis, auto&& body) {
                const int len = axis == 0 ? nx : (axis == 1 ? ny : nz);
                const size_t stride = axis == 0 ? size_t(1) : (axis == 1 ? size_t(nx) : plane);
                const size_t count = nodes / size_t(len);
                parallelFor(count, [&](size_t ln) {
                    size_t base;
                    if (axis == 0) base = ln * size_t(nx);
                    else if (axis == 1) base = (ln / size_t(nx)) * plane + (ln % size_t(nx));
                    else base = ln;
                    body(base, stride, len);
                });
            };
            auto dilate = [&](const std::vector<float>& in, std::vector<float>& out, int axis) {
                lines(axis, [&](size_t base, size_t stride, int len) {
                    for (int t = 0; t < len; ++t)
                    {
                        float best = 0.0f;
                        const int lo = std::max(0, t - R), hi = std::min(len - 1, t + R);
                        for (int s = lo; s <= hi; ++s) best = std::max(best, in[base + size_t(s) * stride]);
                        out[base + size_t(t) * stride] = best;
                    }
                });
            };
            dilate(Fc, A, 0);
            dilate(A, Fc, 1);
            dilate(Fc, A, 2);
            parallelFor(size_t(nz), [&](size_t k) {
                for (size_t q = 0; q < plane; ++q)
                {
                    const size_t at = k * plane + q;
                    if (Dl[at] > 0.0f) Dl[at] *= std::min(1.0f, A[at]);
                }
            });
            auto blur = [&](const std::vector<float>& in, std::vector<float>& out, int axis) {
                lines(axis, [&](size_t base, size_t stride, int len) {
                    for (int t = 0; t < len; ++t)
                    {
                        const size_t at = base + size_t(t) * stride;
                        if (in[at] < 0.0f) { out[at] = in[at]; continue; }
                        double num = 0.0, den = 0.0;
                        const int lo = std::max(0, t - R), hi = std::min(len - 1, t + R);
                        for (int s = lo; s <= hi; ++s)
                        {
                            const size_t m = base + size_t(s) * stride;
                            if (in[m] < 0.0f) continue;
                            num += double(in[m]);
                            den += 1.0;
                        }
                        out[at] = den > 0.0 ? float(num / den) : in[at];
                    }
                });
            };
            for (int round = 0; round < 2; ++round)
            {
                blur(Dl, A, 0);
                blur(A, Dl, 1);
                blur(Dl, A, 2);
                Dl.swap(A);
            }
        }
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
        if (P.mode == 5)
        {
            // The ball's field is the part's value less the fill's depth, and the depth changes fast along the tail of an arc, where it leaves the wall: the slope of the field there is 1.2 to 1.75, where the
            // part's own field, a few millimetres off, has slope 1.  A tool that reads the clearance of a ball's centre from the field and divides it by the slope at the surface (the check of a result does)
            // reads a ball that fits as 20 to 40 % short.  Near the surface the value is divided by the field's own slope there, so the field of the result is a distance to the first order, as the part's is;
            // the surface -- the zero of the value -- is where it was, and the scale is eased out over the third node from it
            std::vector<float> W(V);
            const float flat = 0.98f * capD;
            const float reachV = float(2.5 * h);          // (not `near`: a macro of the Windows headers)
            parallelFor(size_t(nz), [&](size_t k) {
                if (k < 1 || k + 1 >= size_t(nz)) return;
                for (int j = 1; j + 1 < ny; ++j)
                    for (int i = 1; i + 1 < nx; ++i)
                    {
                        const size_t at = (k * ny + j) * nx + i;
                        const float v0 = V[at];
                        if (std::fabs(v0) >= reachV) continue;
                        const float xm = V[at - 1], xp = V[at + 1], ym = V[at - nx], yp = V[at + nx], zm = V[at - plane], zp = V[at + plane];
                        if (std::fabs(xm) >= flat || std::fabs(xp) >= flat || std::fabs(ym) >= flat || std::fabs(yp) >= flat || std::fabs(zm) >= flat || std::fabs(zp) >= flat) continue;
                        const double gx = (double(xp) - double(xm)) / (2.0 * h), gy = (double(yp) - double(ym)) / (2.0 * h), gz = (double(zp) - double(zm)) / (2.0 * h);
                        const double g = std::min(2.0, std::max(0.5, std::sqrt(gx * gx + gy * gy + gz * gz)));
                        const double w = std::max(0.0, std::min(1.0, (double(reachV) - std::fabs(double(v0))) / h));
                        W[at] = float(double(v0) / (1.0 + w * (g - 1.0)));
                    }
            });
            V.swap(W);
            // Away from the surface the value is the margin of the ball test -- how much clearance the best ball has to spare -- and not a distance: along the diagonal of a corner of 90 degrees it gains 0.71 for every
            // millimetre, so the centre of the rolling ball, 6 mm from both walls and from the arc, read 4.24.  A tool that reads the clearance of a ball's centre from the field (the check of a result) took a ball that
            // fits for one 30 % short.  So beyond a few nodes from the surface the field is the true distance to the surface of the result, found as the distance of the part's was above: the sources are the points where
            // the sign of the value changes along a lattice edge (found by linear interpolation), each given to the nearer node of its edge, and the distance is to the source's point; the sign is the value's
            {
                const uint32_t none = 0xFFFFFFFFu;
                std::vector<float> E, foot;
                std::vector<uint32_t> Src, srcNode;
                signChangeSources(V, 1.0e30f, nx, ny, nz, h, E, Src, foot, srcNode);
                if (!foot.empty())
                {
                    edt3src(E, Src, nx, ny, nz);
                    const float nearBand = float(2.5 * h);
                    parallelFor(size_t(nz), [&](size_t k) {
                        for (size_t q = 0; q < plane; ++q)
                        {
                            const size_t at = k * plane + q;
                            const uint32_t id = Src[at];
                            if (id == none) continue;
                            const float v0 = V[at];
                            if (std::fabs(v0) < nearBand) continue;
                            const int j = int(q / size_t(nx)), i = int(q % size_t(nx));
                            const float dist = float(std::min(nearestSourceDistance(i, j, int(k), id, nx, ny, nz, h, E, Src, srcNode, foot), double(capD)));
                            V[at] = v0 >= 0.0f ? dist : -dist;
                        }
                    });
                }
            }
        }
        return finishLattice(P, std::move(V), capD, (P.mode == 3 ? "walls-lattice" : (P.mode == 5 ? "corners-ball-lattice" : "draft-lattice")), stats, double(grownNodes.load()) * h * h * h, double(nodes), double(looked.load()), tSample, secondsSince(t0),
                             double(partCount.load()));
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
    // `cap` everywhere else: the surface of the fill is the part's own faces at the two ends of each cavity, and, at the side, a wall just outside the columns that have a cavity.  The edge of a cavity lies
    // somewhere between a column that has it and the next that has not; the value of the field between their nodes is a mix of -h and `cap`, and a wall put at the nodes of the columns that have the cavity left
    // a slit of air, up to most of a node wide, between the floor and the ceiling along every wall of the fill (the independent check of the result found 'undercut' there, and so did the judge of the kit).
    // So the columns next to a column with a cavity (the eight round it) are filled over the span of the cavity as well: every cell of the lattice that the cavity touches has all its corners at -h, and the wall
    // of the fill stands between 0.14 and 1.14 of a node outside the edge, never inside it.  One read of the part and one of the field at a point.
    if (P.tanSlope < 0.0)
    {
        const size_t columns = size_t(nx) * ny;
        const float capU = float(2.0 * P.reach);
        std::atomic<uint64_t> filled(0), partNodes(0);
        std::vector<int> firstK(columns, -1), lastK(columns, -1);          // (the first and the last node of the part in each column, -1 where it has none)
        std::vector<uint8_t> cavityAt(columns, 0);                         // (1 where the column holds air between two nodes of the part)
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
            firstK[c] = first;
            lastK[c] = last;
            cavityAt[c] = cavity ? 1 : 0;
        });
        parallelFor(columns, [&](size_t c) {
            int lo = -1, hi = -1;
            if (cavityAt[c])
            {
                lo = firstK[c];
                hi = lastK[c];
            }
            else
            {
                const int i = int(c % size_t(nx)), j = int(c / size_t(nx));
                for (int dj = -1; dj <= 1; ++dj)
                    for (int di = -1; di <= 1; ++di)
                    {
                        const int ii = i + di, jj = j + dj;
                        if ((di == 0 && dj == 0) || ii < 0 || ii >= nx || jj < 0 || jj >= ny) continue;
                        const size_t cn = size_t(jj) * nx + ii;
                        if (!cavityAt[cn]) continue;
                        lo = lo < 0 ? firstK[cn] : std::min(lo, firstK[cn]);
                        hi = std::max(hi, lastK[cn]);
                    }
            }
            uint64_t added = 0, nodesOfPart = 0;
            for (int k = 0; k < nz; ++k)
            {
                float& v = F[size_t(k) * columns + c];
                const bool isPart = v <= 0.0f;
                if (isPart) ++nodesOfPart;
                if (lo >= 0 && k >= lo && k <= hi)
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
    // (The gusset is made again with what has been made counted as part, until nothing is added: a gusset made once lies beside the part in a slice -- the wall and the gusset on it, merged -- and the discs
    // that fit in the two together are larger than those that fit in either, so the tool given its own result would make more under it.  What this returns is the part united with its gusset to a fixed point,
    // so that the result of the tool is stable under the tool: a second pass makes nothing.)
    const float slope = float(P.tanSlope);
    const float limit = slope * (2.0f * float(P.reach) + 4.0f * float(h));          // (a disc as wide as that fits wherever the part is that far from the air: no need for the distance beyond it)
    const float inf = std::numeric_limits<float>::infinity();
    std::vector<float> Wv(nodes);
    const size_t slicePlane = size_t(nx) * ny;
    std::vector<uint8_t> part0(nodes, 0);               // (the nodes of the part as it was given: how deep a gusset may go is measured from these)
    parallelFor(size_t(nz), [&](size_t k) {
        for (size_t q = 0; q < slicePlane; ++q) part0[k * slicePlane + q] = F[k * slicePlane + q] <= 0.0f ? 1 : 0;
    });
    double tSlices = 0.0;

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
    std::vector<float> G(nodes, cap);                 // (the field: `F` stays the part as it was sampled; what the rounds make is kept apart from it)
    // (the stretches of the part in a column -- the part and the gusset made so far: for every node the first node of its stretch, and for a node of the air the nearest node of the part above it; `above0`:
    // the nearest node above of the part as it was GIVEN)
    auto stretches = [&](size_t c, std::vector<int>& bottom, std::vector<int>& above, std::vector<int>& above0) {
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
        int nearest = -1, nearest0 = -1;
        for (int k = nz - 1; k >= 0; --k)
        {
            if (bottom[size_t(k)] >= 0) nearest = k;
            if (part0[size_t(k) * columns + c]) nearest0 = k;
            above[size_t(k)] = nearest;
            above0[size_t(k)] = nearest0;
        }
    };
    // (the lowest height the gusset reaches under a stretch of the part, from the first node `s` of it: the least of z - D / slope up to a reach above the part as it was given -- the nodes of the gusset made so
    // far are counted, so that a stretch that starts lower reads the same nodes above it as before and the gusset's own besides)
    auto reachOf = [&](size_t c, int s, std::vector<float>& M, const std::vector<int>& above0) {
        if (!std::isfinite(M[size_t(s)]))
        {
            float m = inf;
            const int base = above0[size_t(s)] >= 0 ? above0[size_t(s)] : s;
            for (int q = s; q <= std::min(nz - 1, base + reachNodes); ++q) m = std::min(m, Wv[size_t(q) * columns + c]);
            M[size_t(s)] = m;
        }
        return M[size_t(s)];
    };
    const int kRounds = 6;
    for (int round = 0; round < kRounds; ++round)
    {
        // (the slices: the distance from every node of the part, and of the gusset made so far, to the air)
        parallelFor(size_t(nz), [&](size_t k) {
            SliceWork work;
            std::vector<float> D(slicePlane), fi(slicePlane);
            for (size_t q = 0; q < slicePlane; ++q) fi[q] = part0[k * slicePlane + q] ? F[k * slicePlane + q] : G[k * slicePlane + q];
            sliceDistance(fi.data(), nx, ny, float(h), limit, work, D.data());
            float* w = &Wv[k * slicePlane];
            const float z = float(k * h);
            for (size_t q = 0; q < slicePlane; ++q) w[q] = fi[q] <= 0 ? z - D[q] / slope : inf;
        });
        if (round == 0) tSlices = secondsSince(t0);
        const uint64_t before = gusset.load();
        inside.store(0);
        parallelFor(columns, [&](size_t c) {
            std::vector<int> bottom(static_cast<size_t>(nz), -1), above(static_cast<size_t>(nz), -1), above0(static_cast<size_t>(nz), -1);          // (not `bottom(size_t(nz))`: that reads as a function)
            std::vector<float> M(static_cast<size_t>(nz), inf);
            stretches(c, bottom, above, above0);
            // (the nodes of the air that have a node of the air inside a kept region between them and the stretch of the part above: no gusset there, see `K`)
            std::vector<uint8_t> shut;
            if (!K.empty())
            {
                shut.assign(static_cast<size_t>(nz), 0);
                bool cut = false;
                for (int k = nz - 1; k >= 0; --k)
                {
                    if (bottom[size_t(k)] >= 0)
                    {
                        cut = false;
                        continue;
                    }
                    if (K[size_t(k) * columns + c]) cut = true;
                    shut[size_t(k)] = cut ? 1 : 0;
                }
            }
            uint64_t added = 0, partNodes = 0;
            for (int k = 0; k < nz; ++k)
            {
                const size_t at = size_t(k) * columns + c;
                if (bottom[size_t(k)] >= 0)
                {
                    ++partNodes;
                    continue;
                }
                float g = cap;
                const int s = above[size_t(k)], s0 = above0[size_t(k)];
                const double z = double(k) * h;
                if (s >= 0 && s0 >= 0 && (s0 - k) * h <= P.reach && z >= P.minZ && (shut.empty() || (!shut[size_t(k)] && !K[size_t(s) * columns + c])))
                {
                    g = std::max(-cap, std::min(cap, float((double(reachOf(c, s, M, above0)) - z) / norm)));
                    if (g <= 0)
                    {
                        if (S[at] != 1) { S[at] = 1; ++added; }
                        has[c] = 1;
                    }
                    else if (g <= float(h))
                    {
                        // (a node of the air within a node of the gusset's surface: no node of the gusset may lie in a sliver thinner than a node -- the flank of a teardrop beside the circle it is tangent to --,
                        // and the part's nodes next to this one then hold the formula too, so that the surface is placed between the nodes by the formula and not lost)
                        if (!S[at]) S[at] = 2;
                        has[c] = 1;
                    }
                }
                G[at] = g;
            }
            gusset += added;
            inside += partNodes;
        });
        if (gusset.load() == before) break;           // (nothing new: the part with its gusset is what a gusset of it would be)
    }
    // A gusset is made only where it can be PRINTED: it must REST on something.  The cone under a stretch of the part is a keel that hangs from it and ends in an edge; a keel under the flat underside of an arm
    // with nothing under it for sixty millimetres is of no use: the printer would have to start its lowest layer, the apex, in the air, and every layer above rests on that one.  That is the red blade, the wedges and
    // the ragged fragment that hang under the arms of the topology bracket by their upper side only -- material made for the printer that the printer cannot print.  A wedge in the corner under a ledge that sticks
    // out of a wall rests on the wall.  So the layers are gone through from the plate up: a node of the gusset stays when, in the layer under it, one of the nine nodes under and beside it is a node of the part, or
    // a node of the gusset that stays, or the plate is under it; what rests on nothing -- and so everything that was made to rest on that -- is not made, and the faces it was to put right are listed as they are
    // (the shell leaves them to a support).  The flank leans less than a node a layer, so a gusset that rests on a wall always has a node of it under and beside each of its own.  (Not for a cut: the teardrop is air.)
    if (!P.negate && P.tanSlope > 0.0 && P.mode == 0)
    {
        uint64_t unsupported = 0;
        for (int k = 0; k < nz; ++k)
        {
            const bool onPlate = k == 0 || double(k - 1) * h < P.minZ;
            if (onPlate) continue;
            for (int j = 0; j < ny; ++j)
                for (int i = 0; i < nx; ++i)
                {
                    const size_t q = (size_t(k) * size_t(ny) + size_t(j)) * size_t(nx) + size_t(i);
                    if (S[q] != 1) continue;
                    bool rests = false;
                    for (int dj = -1; dj <= 1 && !rests; ++dj)
                        for (int di = -1; di <= 1 && !rests; ++di)
                        {
                            const int ii = i + di, jj = j + dj;
                            if (ii < 0 || jj < 0 || ii >= nx || jj >= ny) continue;
                            const size_t below = (size_t(k - 1) * size_t(ny) + size_t(jj)) * size_t(nx) + size_t(ii);
                            if (part0[below] || S[below] == 1) rests = true;
                        }
                    if (!rests)
                    {
                        S[q] = 0;
                        G[q] = cap;
                        ++unsupported;
                    }
                }
        }
        if (unsupported)
        {
            // (a node of the air that was within a node of the surface of what was dropped and has no gusset left beside it: nothing of the gusset is there any more)
            parallelFor(size_t(nz), [&](size_t k) {
                for (int j = 0; j < ny; ++j)
                    for (int i = 0; i < nx; ++i)
                    {
                        const size_t q = (k * size_t(ny) + size_t(j)) * size_t(nx) + size_t(i);
                        if (S[q] != 2) continue;
                        bool beside = false;
                        for (int dk = -1; dk <= 1 && !beside; ++dk)
                            for (int dj = -1; dj <= 1 && !beside; ++dj)
                                for (int di = -1; di <= 1 && !beside; ++di)
                                {
                                    const int ii = i + di, jj = j + dj, kk = int(k) + dk;
                                    if (ii < 0 || jj < 0 || kk < 0 || ii >= nx || jj >= ny || kk >= nz) continue;
                                    if (S[(size_t(kk) * size_t(ny) + size_t(jj)) * size_t(nx) + size_t(ii)] == 1) beside = true;
                                }
                        if (!beside)
                        {
                            S[q] = 0;
                            G[q] = cap;
                        }
                    }
            });
            parallelFor(columns, [&](size_t c) {
                uint8_t any = 0;
                for (int k = 0; k < nz && !any; ++k) any = S[size_t(k) * columns + c] ? 1 : 0;
                has[c] = any;
            });
            gusset.store(gusset.load() > unsupported ? gusset.load() - unsupported : 0);
        }
    }

    // The gusset of the CHECK, asked of the function by itself (see the top): only its nodes are wanted, as a map of where a face leans past what the check lets through
    if (checkOut)
    {
        checkOut->assign(nodes, 0);
        for (size_t q = 0; q < nodes; ++q) (*checkOut)[q] = S[q] == 1 ? 1 : 0;
        return Tree::invalid();
    }
    // A piece of gusset (or of air cut) that is smaller than a few nodes in every direction is not made.  On a part that an optimizer shaped -- struts of every thickness, every face a facet -- most faces lean
    // a degree or two past the angle, and the gusset that would put them right is a skin a fraction of a node thick: hundreds of pieces, each under a cubic millimetre, which no lattice can carry (they
    // come out as slabs one node thick or are lost between nodes), no printer can print (a skin of a tenth of a millimetre on a 0.4 mm nozzle) and which are drawn as red specks and shards scattered over the
    // part.  Pieces are the connected sets of gusset nodes (26 neighbours); one that holds less than 64 nodes -- 4 nodes each way -- or none of the nodes of the check's gusset (see the top) is dropped, with the nodes of the air that touch it.  What is left is the
    // gusset of the overhangs that are big enough to matter; the independent check still lists the faces of a dropped piece as they were, a few tenths of a per cent of the surface
    {
        const size_t minNodes = 64;
        std::vector<uint8_t> seen(nodes, 0);
        std::vector<size_t> stack, piece;
        uint64_t dropped = 0, droppedNodes = 0;
        for (size_t start = 0; start < nodes; ++start)
        {
            if (S[start] != 1 || seen[start]) continue;
            piece.clear();
            stack.clear();
            stack.push_back(start);
            seen[start] = 1;
            bool needed = checkGusset.empty();          // (a piece of the gusset is kept only where a face leans past what the check lets through: see the top)
            while (!stack.empty())
            {
                const size_t q = stack.back();
                stack.pop_back();
                piece.push_back(q);
                if (!needed && checkGusset[q]) needed = true;
                const int i = int(q % size_t(nx)), j = int((q / size_t(nx)) % size_t(ny)), k = int(q / columns);
                for (int dk = -1; dk <= 1; ++dk)
                    for (int dj = -1; dj <= 1; ++dj)
                        for (int di = -1; di <= 1; ++di)
                        {
                            const int ii = i + di, jj = j + dj, kk = k + dk;
                            if (ii < 0 || jj < 0 || kk < 0 || ii >= nx || jj >= ny || kk >= nz) continue;
                            const size_t n = (size_t(kk) * ny + jj) * nx + ii;
                            if (S[n] == 1 && !seen[n])
                            {
                                seen[n] = 1;
                                stack.push_back(n);
                            }
                        }
            }
            if (piece.size() < minNodes || !needed)
            {
                for (size_t q : piece)
                {
                    S[q] = 0;
                    G[q] = cap;
                }
                ++dropped;
                droppedNodes += piece.size();
            }
        }
        if (dropped)
        {
            // (a node of the air that was within a node of the surface of a dropped piece, and has no piece left beside it: nothing of the gusset is there any more)
            parallelFor(size_t(nz), [&](size_t k) {
                for (int j = 0; j < ny; ++j)
                    for (int i = 0; i < nx; ++i)
                    {
                        const size_t q = (k * size_t(ny) + size_t(j)) * size_t(nx) + size_t(i);
                        if (S[q] != 2) continue;
                        bool beside = false;
                        for (int dk = -1; dk <= 1 && !beside; ++dk)
                            for (int dj = -1; dj <= 1 && !beside; ++dj)
                                for (int di = -1; di <= 1 && !beside; ++di)
                                {
                                    const int ii = i + di, jj = j + dj, kk = int(k) + dk;
                                    if (ii < 0 || jj < 0 || kk < 0 || ii >= nx || jj >= ny || kk >= nz) continue;
                                    if (S[(size_t(kk) * ny + jj) * nx + ii] == 1) beside = true;
                                }
                        if (!beside)
                        {
                            S[q] = 0;
                            G[q] = cap;
                        }
                    }
            });
            parallelFor(columns, [&](size_t c) {
                uint8_t any = 0;
                for (int k = 0; k < nz && !any; ++k) any = S[size_t(k) * columns + c] ? 1 : 0;
                has[c] = any;
            });
            gusset.store(gusset.load() > droppedNodes ? gusset.load() - droppedNodes : 0);
        }
    }

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
        std::vector<int> bottom(static_cast<size_t>(nz), -1), above(static_cast<size_t>(nz), -1), above0(static_cast<size_t>(nz), -1);
        std::vector<float> M(static_cast<size_t>(nz), inf);
        stretches(c, bottom, above, above0);
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
            G[size_t(k) * columns + c] = std::max(-cap, std::min(cap, float((double(reachOf(c, bottom[size_t(k)], M, above0)) - z) / norm)));
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
    data->g = std::move(G);
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

////////////////////////////////////////////////////////////////////////////////
// The CORRECTION lattice: what a field differs from the part it is made from, worked out once
//
// The remedies of the manufacturing tool for a part whose field is cheap -- walls thickened, a draft, a gap closed -- are formulas: each asks the part's field dozens of times at every point
// the mesher samples (the draft's foot closing alone ~60 lookups).  The mesher needs ~1500 points for each cell on the surface, so a surface cell of the mould of showcase 03_mould.py costs ten milliseconds
// where the part's own costs a fraction of one: 74 seconds for what the part draws in 0.2.  Direct math cannot make that formula cheaper without changing the field (six ways were measured).
// Here the formula is LOOKED AT ONCE, in the tool: its difference to the part -- a smooth correction, the growth of the walls, 0 away from every face the remedies touch -- is sampled at the nodes
// of a lattice (with the pruning of the mesher: a block that interval arithmetic puts farther than `reach` from the surface of the field is not looked at, the difference there is 0), and the
// result is the part's own field PLUS that lattice.  The part's edges and faces stay exact (they are the part's), the lattice only carries the correction, which is linear along a wall (the draft)
// and smooth elsewhere: reading it by trilinear interpolation is exact where it is linear and good to a fraction of a node at its kinks.  The mesher reads one lookup of a lattice, not the formula.
Tree bakeLattice(const Tree& field, const Tree& part, const std::map<Tree::Id, float>& vars, const OverhangLatticeParams& P, double* stats, Tree* zone)
{
    const auto t0 = std::chrono::steady_clock::now();
    if (stats) for (int i = 0; i < 8; ++i) stats[i] = 0.0;
    auto fail = [&](double code) {
        if (stats) stats[0] = code;
        return Tree::invalid();
    };
    const int nx = P.n[0], ny = P.n[1], nz = P.n[2];
    const double h = P.pitch;
    if (nx < 3 || ny < 3 || nz < 3 || !(h > 0) || !(P.reach > 0)) return fail(1);
    const size_t nodes = size_t(nx) * ny * nz;
    if (nodes > size_t(120) * 1000 * 1000) return fail(2);          // (a lattice of more than 120 million nodes: a coarser pitch is asked for)

    std::vector<float> F(nodes, kBound);
    std::vector<uint8_t> inside(nodes, 0);          // (1 where the part is solid at the node: see the extension below)
    std::vector<float> partAt(zone ? nodes : 0, 0.0f);          // (the part's value at the nodes that were looked at: the zone is only where the result's surface is)
    std::atomic<uint64_t> looked(0);
    {
        // tiles of 16 nodes, each worked through by one thread with evaluators of its own: the field is sampled with the pruning of the mesher, and at the nodes that were looked at the part is
        // subtracted; a node that interval arithmetic put far from the surface holds 0 (the field is the part's there)
        const int T = 16;
        const int tx = (nx + T - 1) / T, ty = (ny + T - 1) / T, tz = (nz + T - 1) / T;
        const size_t tiles = size_t(tx) * ty * tz;
        std::atomic<int> failed(0);
        const size_t threads = std::max<size_t>(1, std::min<size_t>(std::thread::hardware_concurrency(), tiles));
        std::atomic<size_t> next(0);
        std::vector<std::thread> pool;
        for (size_t th = 0; th < threads; ++th)
        {
            pool.emplace_back([&]() {
                try
                {
                    Evaluator ev(field, vars);
                    Evaluator evP(part, vars);
                    const Tape::Handle root = ev.getDeck()->tape;
                    const Tape::Handle rootP = evP.getDeck()->tape;
                    Sampler S{P, {nx, ny, nz}, h, F, ev, looked};
                    S.band = P.reach;
                    S.bandIn = P.reach;
                    size_t where[ArrayEvaluator::N];
                    for (;;)
                    {
                        const size_t q = next.fetch_add(1);
                        if (q >= tiles) break;
                        const int ti = int(q % tx), tj = int((q / tx) % ty), tk = int(q / (size_t(tx) * ty));
                        const int lo[3] = {ti * T, tj * T, tk * T};
                        const int hi[3] = {std::min(nx - 1, lo[0] + T - 1), std::min(ny - 1, lo[1] + T - 1), std::min(nz - 1, lo[2] + T - 1)};
                        S.box(lo, hi, root);
                        size_t pending = 0;
                        auto flush = [&]() {
                            if (!pending) return;
                            const auto vs = evP.values(pending, *rootP);
                            for (size_t c = 0; c < pending; ++c)
                            {
                                F[where[c]] -= vs(long(c));
                                inside[where[c]] = vs(long(c)) < 0.0f ? 1 : 0;
                                if (zone) partAt[where[c]] = vs(long(c));
                            }
                            pending = 0;
                        };
                        for (int k = lo[2]; k <= hi[2]; ++k)
                            for (int j = lo[1]; j <= hi[1]; ++j)
                                for (int i = lo[0]; i <= hi[0]; ++i)
                                {
                                    const size_t idx = (size_t(k) * ny + j) * nx + i;
                                    if (std::fabs(F[idx]) < 0.5f * kBound)
                                    {
                                        evP.set(S.world(i * h, j * h, k * h), pending);
                                        where[pending++] = idx;
                                        if (pending == ArrayEvaluator::N) flush();
                                    }
                                    else
                                    {
                                        F[idx] = 0.0f;
                                    }
                                }
                        flush();
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
    // The correction of a remedy that ADDS material (a wall grown, a gap filled, a draft) is -the growth outside the part's surface and 0 inside it, because the formula grows what is outside and has no reason to
    // do anything to what is solid already -- a jump at the part's old surface.  A lattice reads a jump as a ramp one node wide, and the result, the part's value plus that ramp, then comes back to 0 inside the
    // grown wall, along the old surface: a fin of 1.2 mm grown to 2 mm had a ridge of 0 at both its old faces, and the check, which walks through material until the field turns positive, measured the fin as 1.2 mm
    // thick.  So the correction is carried a few nodes into the material (by the most negative value of the nodes beside it, one layer a pass): inside the part the result is solid whatever the correction is (it only
    // adds), so no surface moves, the jump is gone, and the field inside the grown wall is clearly negative.  A node that is cut (a correction above 0) is left as it is.
    {
        const size_t sx = 1, sy = size_t(nx), sz = size_t(nx) * size_t(ny);
        std::vector<std::pair<size_t, float>> take;
        for (int pass = 0; pass < 4; ++pass)
        {
            take.clear();
            for (int k = 0; k < nz; ++k)
                for (int j = 0; j < ny; ++j)
                    for (int i = 0; i < nx; ++i)
                    {
                        const size_t idx = (size_t(k) * ny + j) * nx + i;
                        if (inside[idx] != 1 || F[idx] > 0.0f) continue;
                        float cand = 0.0f;
                        auto see = [&](size_t n) { if (inside[n] != 1) cand = std::min(cand, F[n]); };
                        if (i > 0) see(idx - sx);
                        if (i + 1 < nx) see(idx + sx);
                        if (j > 0) see(idx - sy);
                        if (j + 1 < ny) see(idx + sy);
                        if (k > 0) see(idx - sz);
                        if (k + 1 < nz) see(idx + sz);
                        if (cand < -1e-6f && cand < F[idx] - 1e-6f) take.emplace_back(idx, cand);
                    }
            if (take.empty()) break;
            for (const auto& t : take)
            {
                F[t.first] = t.second;
                inside[t.first] = 2;
            }
        }
    }
    // THE ZONE: where a trilinear reading of the correction is not exact.  A lattice reads a correction that is linear exactly (a draft's lean, a wall grown by a constant) and a kink of it -- the top of
    // a wall's draft, a convex edge, the foot of a wall -- over one node, which is finer than the cells a picture is made in and coarser than a check that reads the lean of a face a few hundredths of a
    // millimetre from it (the mould's draft was marked on 0.24 % of its surface, the undercut on 0.16 %).  So the result is the lattice where the correction is linear and the formula only in the zones
    // round its kinks: a node is a kink where a second difference of the correction along an axis is more than 4e-4 mm (a linear correction has none) AND the result's surface is within three nodes of it
    // (a kink far from the surface -- the band's edge -- changes nothing that is drawn or measured), and the zone is the nodes within two of such a node.  -1 inside the zone, +1 elsewhere and outside
    // the lattice
    if (zone)
    {
        std::vector<uint8_t> kink(nodes, 0), spread(nodes, 0);
        const size_t sx = 1, sy = size_t(nx), sz = size_t(nx) * size_t(ny);
        const float tau = 4.0e-4f;
        const float nearSurface = float(3.0 * h);
        for (int k = 0; k < nz; ++k)
            for (int j = 0; j < ny; ++j)
                for (int i = 0; i < nx; ++i)
                {
                    const size_t idx = (size_t(k) * ny + j) * nx + i;
                    if (std::fabs(partAt[idx] + F[idx]) >= nearSurface) continue;
                    const float d0 = F[idx];
                    bool bent = false;
                    if (i > 0 && i + 1 < nx) bent = bent || std::fabs(F[idx - sx] - 2.0f * d0 + F[idx + sx]) > tau;
                    if (j > 0 && j + 1 < ny) bent = bent || std::fabs(F[idx - sy] - 2.0f * d0 + F[idx + sy]) > tau;
                    if (k > 0 && k + 1 < nz) bent = bent || std::fabs(F[idx - sz] - 2.0f * d0 + F[idx + sz]) > tau;
                    if (bent) kink[idx] = 1;
                }
        // (a box of two nodes round each kink, one axis after the other)
        const int R = 2;
        auto widen = [&](const std::vector<uint8_t>& from, std::vector<uint8_t>& to, int axis) {
            const size_t stride = axis == 0 ? sx : (axis == 1 ? sy : sz);
            const int len = axis == 0 ? nx : (axis == 1 ? ny : nz);
            for (int k = 0; k < nz; ++k)
                for (int j = 0; j < ny; ++j)
                    for (int i = 0; i < nx; ++i)
                    {
                        const size_t idx = (size_t(k) * ny + j) * nx + i;
                        const int at = axis == 0 ? i : (axis == 1 ? j : k);
                        uint8_t v = 0;
                        for (int s = std::max(0, at - R); s <= std::min(len - 1, at + R) && !v; ++s) v = from[idx + size_t(s) * stride - size_t(at) * stride];
                        to[idx] = v;
                    }
        };
        widen(kink, spread, 0);
        widen(spread, kink, 1);
        widen(kink, spread, 2);
        std::vector<float> Z(nodes, 1.0f);
        for (size_t q = 0; q < nodes; ++q)
            if (spread[q]) Z[q] = -1.0f;
        *zone = finishLattice(P, std::move(Z), 1.0f, "zone", nullptr, 0.0, double(nodes), 0.0, 0.0, 0.0, 0.0);
    }
    const double tSample = secondsSince(t0);
    return finishLattice(P, std::move(F), 0.0f, "bake", stats, 0.0, double(nodes), double(looked.load()), tSample, secondsSince(t0), 0.0);
}

}   // namespace fields
}   // namespace libfive
