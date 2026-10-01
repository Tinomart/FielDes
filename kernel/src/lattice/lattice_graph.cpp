/*
libfive: a CAD kernel for modeling with implicit functions

Graph (beam) lattices and their generators; see lattice_graph.hpp.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <map>
#include <memory>
#include <random>
#include <unordered_map>

#include "libfive/lattice/lattice_graph.hpp"
#include "libfive/oracle/oracle_clause.hpp"
#include "libfive/oracle/oracle_storage.hpp"
#include "libfive/eval/eval_array.hpp"
#include "libfive/eval/eval_deriv_array.hpp"
#include "libfive/render/brep/mesh.hpp"
#include "libfive/render/brep/region.hpp"
#include "libfive/render/brep/settings.hpp"

namespace libfive {
namespace lattice {

using V3 = Eigen::Vector3d;

namespace {

////////////////////////////////////////////////////////////////////////////////
// Beams through a bounding volume hierarchy

struct Beam
{
    V3 a, b;
    double ra, rb;
    V3 lo, hi;          // bounding box, radius included
};

double beamDistance(const Beam& s, const V3& p)
{
    const V3 ab = s.b - s.a;
    const double L2 = ab.squaredNorm();
    if (L2 <= 1e-300)
    {
        return (p - s.a).norm() - std::max(s.ra, s.rb);
    }
    const double h = std::min(1.0, std::max(0.0, (p - s.a).dot(ab) / L2));
    return (p - s.a - h * ab).norm() - (s.ra + (s.rb - s.ra) * h);
}

class BeamData
{
public:
    BeamData(const Graph& g, const std::vector<double>& radius, double blend)
        : blend(std::max(0.0, blend))
    {
        double maxSlope = 0;
        for (const auto& e : g.beams)
        {
            Beam s;
            s.a = g.nodes[size_t(e[0])];
            s.b = g.nodes[size_t(e[1])];
            s.ra = std::max(0.0, radius[size_t(e[0])]);
            s.rb = std::max(0.0, radius[size_t(e[1])]);
            const double r = std::max(s.ra, s.rb);
            s.lo = (s.a.cwiseMin(s.b).array() - r).matrix();
            s.hi = (s.a.cwiseMax(s.b).array() + r).matrix();
            const double L = (s.b - s.a).norm();
            if (L > 0) maxSlope = std::max(maxSlope, std::abs(s.rb - s.ra) / L);
            beams.push_back(s);
        }
        lip = 1.0 + maxSlope;
        order.resize(beams.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = int(i);
        if (!beams.empty())
        {
            nodes.reserve(2 * beams.size());
            build(0, int(beams.size()));
        }
        // content key (so identical lattices share one mesh)
        uint64_t h = 1469598103934665603ull;
        auto mix = [&h](const void* data, size_t n) {
            const unsigned char* c = static_cast<const unsigned char*>(data);
            for (size_t i = 0; i < n; ++i) { h ^= c[i]; h *= 1099511628211ull; }
        };
        for (const auto& s : beams)
        {
            mix(s.a.data(), sizeof(double) * 3);
            mix(s.b.data(), sizeof(double) * 3);
            mix(&s.ra, sizeof(double));
            mix(&s.rb, sizeof(double));
        }
        mix(&this->blend, sizeof(double));
        char buf[64];
        std::snprintf(buf, sizeof(buf), "beams#%016llx#%zu", (unsigned long long)h, beams.size());
        key = buf;
    }

    double value(const V3& p, std::vector<double>& near) const
    {
        if (beams.empty()) return 1e9;
        double best = std::numeric_limits<double>::infinity();
        near.clear();
        int stack[128];
        int top = 0;
        stack[top++] = 0;
        while (top)
        {
            const Node& n = nodes[size_t(stack[--top])];
            const double bd = std::sqrt(boxDist2(p, n.lo, n.hi));
            const double lb = bd > 0 ? bd : -n.rmax;
            if (lb >= best + blend) continue;
            if (n.left < 0)
            {
                for (int k = n.first; k < n.first + n.count; ++k)
                {
                    const double d = beamDistance(beams[size_t(order[size_t(k)])], p);
                    if (blend > 0 && d < best + blend) near.push_back(d);
                    if (d < best) best = d;
                }
            }
            else if (top + 2 <= 128)
            {
                const Node& L = nodes[size_t(n.left)];
                const Node& R = nodes[size_t(n.right)];
                const double dl = boxDist2(p, L.lo, L.hi), dr = boxDist2(p, R.lo, R.hi);
                // nearer child on top
                if (dl < dr) { stack[top++] = n.right; stack[top++] = n.left; }
                else         { stack[top++] = n.left;  stack[top++] = n.right; }
            }
        }
        if (blend > 0 && near.size() > 1)
        {
            std::sort(near.begin(), near.end());
            double out = near[0];
            for (size_t i = 1; i < near.size() && near[i] < best + blend; ++i)
            {
                const double b = near[i];
                const double hh = std::max(blend - std::abs(out - b), 0.0) / blend;
                out = std::min(out, b) - hh * hh * blend / 4;
            }
            return out;
        }
        return best;
    }

    V3 gradient(const V3& p, std::vector<double>& near) const
    {
        const double e = step;
        V3 g;
        for (int a = 0; a < 3; ++a)
        {
            V3 q = p, r = p;
            q[a] += e;
            r[a] -= e;
            g[a] = (value(q, near) - value(r, near)) / (2 * e);
        }
        return g;
    }

    double lipschitz() const { return lip; }
    std::string key;
    double step = 1e-4;

private:
    struct Node
    {
        V3 lo, hi;
        double rmax = 0;
        int left = -1, right = -1, first = 0, count = 0;
    };

    static double boxDist2(const V3& p, const V3& lo, const V3& hi)
    {
        const V3 d = (lo - p).cwiseMax(p - hi).cwiseMax(V3::Zero());
        return d.squaredNorm();
    }

    int build(int first, int count)
    {
        const int id = int(nodes.size());
        nodes.push_back(Node());
        V3 lo = V3::Constant(1e300), hi = V3::Constant(-1e300), clo = lo, chi = hi;
        double rmax = 0;
        for (int k = first; k < first + count; ++k)
        {
            const Beam& s = beams[size_t(order[size_t(k)])];
            lo = lo.cwiseMin(s.lo);
            hi = hi.cwiseMax(s.hi);
            const V3 c = (s.a + s.b) / 2;
            clo = clo.cwiseMin(c);
            chi = chi.cwiseMax(c);
            rmax = std::max(rmax, std::max(s.ra, s.rb));
        }
        nodes[size_t(id)].lo = lo;
        nodes[size_t(id)].hi = hi;
        nodes[size_t(id)].rmax = rmax;
        if (count <= 4)
        {
            nodes[size_t(id)].first = first;
            nodes[size_t(id)].count = count;
            if (id == 0) step = std::max(1e-7, 1e-6 * (hi - lo).norm());
            return id;
        }
        if (id == 0) step = std::max(1e-7, 1e-6 * (hi - lo).norm());
        int axis = 0;
        const V3 ext = chi - clo;
        if (ext[1] > ext[axis]) axis = 1;
        if (ext[2] > ext[axis]) axis = 2;
        const int mid = first + count / 2;
        std::nth_element(order.begin() + first, order.begin() + mid, order.begin() + first + count,
                         [&](int i, int j) {
            return (beams[size_t(i)].a[axis] + beams[size_t(i)].b[axis]) <
                   (beams[size_t(j)].a[axis] + beams[size_t(j)].b[axis]);
        });
        const int l = build(first, mid - first);
        const int r = build(mid, first + count - mid);
        nodes[size_t(id)].left = l;
        nodes[size_t(id)].right = r;
        return id;
    }

    std::vector<Beam> beams;
    std::vector<int> order;
    std::vector<Node> nodes;
    double blend;
    double lip = 1;
};

class BeamOracle : public OracleStorage<>
{
public:
    explicit BeamOracle(std::shared_ptr<const BeamData> d)
        : data(std::move(d)) {}

    // (the nearest beam of a graph: remembered by the graph's key)
    const std::string* memoKey() const override { return &data->key; }

    void evalInterval(Interval& out) override
    {
        const V3 lo = lower.cast<double>(), hi = upper.cast<double>();
        const double r = (hi - lo).norm() / 2;
        const double d = data->value((lo + hi) / 2, scratch);
        const double k = data->lipschitz();
        out = Interval(float(d - k * r), float(d + k * r));
    }

    void evalPoint(float& out, size_t index) override
    {
        out = float(data->value(points.col(index).matrix().cast<double>(), scratch));
    }

    void checkAmbiguous(
            Eigen::Block<Eigen::Array<bool, 1, LIBFIVE_EVAL_ARRAY_SIZE>,
                         1, Eigen::Dynamic> /* out */) override {}

    void evalFeatures(boost::container::small_vector<Feature, 4>& out) override
    {
        const V3 g = data->gradient(points.col(0).matrix().cast<double>(), scratch);
        out.push_back(Feature(Eigen::Vector3f(g.cast<float>())));
    }

private:
    std::shared_ptr<const BeamData> data;
    std::vector<double> scratch;    // (one oracle per evaluator, i.e. per thread)
};

class BeamClause : public OracleClause
{
public:
    explicit BeamClause(std::shared_ptr<const BeamData> d) : data(std::move(d)) {}
    std::unique_ptr<Oracle> getOracle() const override
    {
        return std::make_unique<BeamOracle>(data);
    }
    std::string name() const override { return "BeamLattice"; }
    std::string contentKey() const override { return data->key; }

private:
    std::shared_ptr<const BeamData> data;
};

////////////////////////////////////////////////////////////////////////////////
// Delaunay (Bowyer-Watson)

struct Tet
{
    int v[4];
    int n[4];           // neighbour across the face opposite v[i]; -1 none
    V3 c;               // circumcentre
    double r2;
    bool alive;
};

double orient(const V3& a, const V3& b, const V3& c, const V3& d)
{
    return (b - a).cross(c - a).dot(d - a);
}

void circumsphere(const V3& a, const V3& b, const V3& c, const V3& d, V3& centre, double& r2)
{
    const V3 B = b - a, C = c - a, D = d - a;
    const double den = 2 * B.dot(C.cross(D));
    if (std::abs(den) < 1e-300)
    {
        centre = (a + b + c + d) / 4;
        r2 = std::numeric_limits<double>::infinity();
        return;
    }
    const V3 x = (B.squaredNorm() * C.cross(D) + C.squaredNorm() * D.cross(B) +
                  D.squaredNorm() * B.cross(C)) / den;
    centre = a + x;
    r2 = x.squaredNorm();
}

class Delaunay
{
public:
    explicit Delaunay(const std::vector<V3>& input) : pts(input)
    {
        N = int(pts.size());
        if (N == 0) return;
        V3 lo = pts[0], hi = pts[0];
        for (const auto& p : pts) { lo = lo.cwiseMin(p); hi = hi.cwiseMax(p); }
        const V3 c = (lo + hi) / 2;
        const double S = 60 * std::max(1e-9, (hi - lo).maxCoeff());
        pts.push_back(c + S * V3(1, 1, 1));
        pts.push_back(c + S * V3(-1, -1, 1));
        pts.push_back(c + S * V3(-1, 1, -1));
        pts.push_back(c + S * V3(1, -1, -1));
        addTet(N, N + 1, N + 2, N + 3);

        // insertion order: along a coarse z-order curve (locality for the walk)
        std::vector<std::pair<uint64_t, int>> keyed;
        keyed.reserve(size_t(N));
        const V3 ext = (hi - lo).cwiseMax(V3::Constant(1e-12));
        for (int i = 0; i < N; ++i)
        {
            const V3 u = (pts[size_t(i)] - lo).cwiseQuotient(ext) * 1023.0;
            uint64_t key = 0;
            const uint32_t q[3] = {uint32_t(u[0]), uint32_t(u[1]), uint32_t(u[2])};
            for (int bit = 9; bit >= 0; --bit)
                for (int a = 0; a < 3; ++a)
                    key = (key << 1) | ((q[a] >> bit) & 1u);
            keyed.push_back({key, i});
        }
        std::sort(keyed.begin(), keyed.end());
        for (const auto& k : keyed) insert(k.second);
    }

    // Finite tetrahedra (no super vertex)
    std::vector<int> finite() const
    {
        std::vector<int> out;
        for (int t = 0; t < int(tets.size()); ++t)
        {
            const Tet& T = tets[size_t(t)];
            if (T.alive && T.v[0] < N && T.v[1] < N && T.v[2] < N && T.v[3] < N)
                out.push_back(t);
        }
        return out;
    }
    bool isFinite(int t) const
    {
        if (t < 0) return false;
        const Tet& T = tets[size_t(t)];
        return T.alive && T.v[0] < N && T.v[1] < N && T.v[2] < N && T.v[3] < N;
    }

    std::vector<V3> pts;
    std::vector<Tet> tets;
    int N = 0;

private:
    int addTet(int a, int b, int c, int d)
    {
        if (orient(pts[size_t(a)], pts[size_t(b)], pts[size_t(c)], pts[size_t(d)]) < 0) std::swap(a, b);
        Tet T;
        T.v[0] = a; T.v[1] = b; T.v[2] = c; T.v[3] = d;
        for (int i = 0; i < 4; ++i) T.n[i] = -1;
        circumsphere(pts[size_t(a)], pts[size_t(b)], pts[size_t(c)], pts[size_t(d)], T.c, T.r2);
        T.alive = true;
        int id;
        if (!freeList.empty())
        {
            id = freeList.back();
            freeList.pop_back();
            tets[size_t(id)] = T;
            stamp[size_t(id)] = 0;
        }
        else
        {
            id = int(tets.size());
            tets.push_back(T);
            stamp.push_back(0);
        }
        return id;
    }

    bool inSphere(int t, const V3& p) const
    {
        const Tet& T = tets[size_t(t)];
        return (p - T.c).squaredNorm() < T.r2 * (1 - 1e-12);
    }

    int locate(const V3& p)
    {
        int t = last;
        if (t < 0 || !tets[size_t(t)].alive)
        {
            for (t = 0; t < int(tets.size()) && !tets[size_t(t)].alive; ++t) {}
        }
        for (int steps = 0; steps < 100000; ++steps)
        {
            const Tet& T = tets[size_t(t)];
            int next = -1;
            for (int i = 0; i < 4 && next < 0; ++i)
            {
                // the face opposite v[i]; p beyond it?
                int f[3], k = 0;
                for (int j = 0; j < 4; ++j) if (j != i) f[k++] = T.v[j];
                const double sv = orient(pts[size_t(f[0])], pts[size_t(f[1])], pts[size_t(f[2])], pts[size_t(T.v[i])]);
                const double sp = orient(pts[size_t(f[0])], pts[size_t(f[1])], pts[size_t(f[2])], p);
                if (sv * sp < 0 && T.n[i] >= 0) next = T.n[i];
            }
            if (next < 0) return t;
            t = next;
        }
        return t;
    }

    void insert(int pi)
    {
        const V3& p = pts[size_t(pi)];
        int t = locate(p);
        if (!inSphere(t, p))
        {
            // walk failed (degenerate): find any tet whose sphere holds p
            t = -1;
            for (int k = 0; k < int(tets.size()); ++k)
                if (tets[size_t(k)].alive && inSphere(k, p)) { t = k; break; }
            if (t < 0) return;          // (duplicate point)
        }
        ++gen;
        cavity.clear();
        cavity.push_back(t);
        stamp[size_t(t)] = gen;
        boundary.clear();
        for (size_t q = 0; q < cavity.size(); ++q)
        {
            const int ct = cavity[q];
            for (int i = 0; i < 4; ++i)
            {
                const int nb = tets[size_t(ct)].n[i];
                if (nb >= 0 && stamp[size_t(nb)] == gen) continue;
                if (nb >= 0 && inSphere(nb, p))
                {
                    stamp[size_t(nb)] = gen;
                    cavity.push_back(nb);
                    continue;
                }
                int f[3], k = 0;
                for (int j = 0; j < 4; ++j) if (j != i) f[k++] = tets[size_t(ct)].v[j];
                boundary.push_back({f[0], f[1], f[2], nb});
            }
        }
        for (int ct : cavity)
        {
            tets[size_t(ct)].alive = false;
            freeList.push_back(ct);
        }
        // (the cavity's slots are reused for the new tets: nothing below
        // reads the cavity tets)
        std::vector<int> reuse;
        reuse.swap(freeList);
        edgeMap.clear();
        int made = -1;
        for (const auto& b : boundary)
        {
            int id;
            {
                // new tet (face, p)
                int a = b.a, bb = b.b, c = b.c, d = pi;
                if (orient(pts[size_t(a)], pts[size_t(bb)], pts[size_t(c)], pts[size_t(d)]) < 0) std::swap(a, bb);
                Tet T;
                T.v[0] = a; T.v[1] = bb; T.v[2] = c; T.v[3] = d;
                for (int i = 0; i < 4; ++i) T.n[i] = -1;
                circumsphere(pts[size_t(a)], pts[size_t(bb)], pts[size_t(c)], pts[size_t(d)], T.c, T.r2);
                T.alive = true;
                if (!reuse.empty()) { id = reuse.back(); reuse.pop_back(); tets[size_t(id)] = T; stamp[size_t(id)] = 0; }
                else { id = int(tets.size()); tets.push_back(T); stamp.push_back(0); }
            }
            made = id;
            Tet& T = tets[size_t(id)];
            // the face opposite p borders the outside neighbour
            T.n[3] = b.outside;
            if (b.outside >= 0)
            {
                Tet& O = tets[size_t(b.outside)];
                for (int i = 0; i < 4; ++i)
                {
                    // O's face that is {a, b, c}: opposite the vertex not in it
                    const int v = O.v[i];
                    if (v != b.a && v != b.b && v != b.c) { O.n[i] = id; break; }
                }
            }
            // faces opposite v[0..2] contain p and an edge of the face
            for (int i = 0; i < 3; ++i)
            {
                int e0 = -1, e1 = -1;
                for (int j = 0; j < 3; ++j)
                {
                    if (j == i) continue;
                    if (e0 < 0) e0 = T.v[j]; else e1 = T.v[j];
                }
                const uint64_t key = (uint64_t(uint32_t(std::min(e0, e1))) << 32) | uint32_t(std::max(e0, e1));
                auto it = edgeMap.find(key);
                if (it == edgeMap.end())
                {
                    edgeMap.emplace(key, std::make_pair(id, i));
                }
                else
                {
                    T.n[i] = it->second.first;
                    tets[size_t(it->second.first)].n[it->second.second] = id;
                    edgeMap.erase(it);
                }
            }
        }
        freeList.swap(reuse);
        last = made;
    }

    std::vector<int> freeList;
    std::vector<int> stamp;
    std::vector<int> cavity;
    struct FaceRec { int a, b, c, outside; };
    std::vector<FaceRec> boundary;
    std::unordered_map<uint64_t, std::pair<int, int>> edgeMap;
    int gen = 0;
    int last = -1;
};

////////////////////////////////////////////////////////////////////////////////
// Helpers

std::vector<float> evalMany(const Tree& t, const std::vector<V3>& pts)
{
    std::vector<float> out(pts.size());
    ArrayEvaluator e(t);
    const size_t N = ArrayEvaluator::N;
    for (size_t start = 0; start < pts.size(); start += N)
    {
        const size_t count = std::min(N, pts.size() - start);
        for (size_t k = 0; k < count; ++k)
            e.set(pts[start + k].cast<float>(), k);
        const auto vs = e.values(count);
        for (size_t k = 0; k < count; ++k) out[start + k] = vs(k);
    }
    return out;
}

class PoissonGrid
{
public:
    PoissonGrid(const V3& lo_, const V3& hi_, double r_) : lo(lo_), r(r_)
    {
        cell = r / std::sqrt(3.0);
        for (int a = 0; a < 3; ++a)
            n[a] = std::max(1, int(std::ceil((hi_[a] - lo[a]) / cell)) + 1);
        const double total = double(n[0]) * n[1] * n[2];
        ok = total < 6e7;
        if (ok) grid.assign(size_t(total), -1);
    }
    bool ok = false;
    bool tryAdd(const V3& p, std::vector<V3>& pts)
    {
        int c[3];
        for (int a = 0; a < 3; ++a)
        {
            c[a] = int(std::floor((p[a] - lo[a]) / cell));
            if (c[a] < 0 || c[a] >= n[a]) return false;
        }
        const double r2 = r * r;
        for (int k = std::max(0, c[2] - 2); k <= std::min(n[2] - 1, c[2] + 2); ++k)
            for (int j = std::max(0, c[1] - 2); j <= std::min(n[1] - 1, c[1] + 2); ++j)
                for (int i = std::max(0, c[0] - 2); i <= std::min(n[0] - 1, c[0] + 2); ++i)
                {
                    const int q = grid[index(i, j, k)];
                    if (q >= 0 && (pts[size_t(q)] - p).squaredNorm() < r2) return false;
                }
        grid[index(c[0], c[1], c[2])] = int(pts.size());
        pts.push_back(p);
        return true;
    }

private:
    size_t index(int i, int j, int k) const { return (size_t(k) * n[1] + j) * n[0] + i; }
    V3 lo;
    double r, cell;
    int n[3];
    std::vector<int> grid;
};

// Keeps only the nodes that beams use
Graph compact(const std::vector<V3>& nodes, const std::vector<std::array<int, 2>>& beams)
{
    Graph g;
    std::vector<int> remap(nodes.size(), -1);
    for (const auto& b : beams)
    {
        std::array<int, 2> e;
        for (int k = 0; k < 2; ++k)
        {
            int& m = remap[size_t(b[size_t(k)])];
            if (m < 0)
            {
                m = int(g.nodes.size());
                g.nodes.push_back(nodes[size_t(b[size_t(k)])]);
            }
            e[size_t(k)] = m;
        }
        if (e[0] != e[1]) g.beams.push_back(e);
    }
    return g;
}

// Beams of a Delaunay (0) or Voronoi (1) graph
void delaunayBeams(const Delaunay& D, int mode, std::vector<V3>& nodes,
                   std::vector<std::array<int, 2>>& beams)
{
    beams.clear();
    if (mode == 0)
    {
        nodes.assign(D.pts.begin(), D.pts.begin() + D.N);
        std::vector<uint64_t> keys;
        for (int t : D.finite())
        {
            const Tet& T = D.tets[size_t(t)];
            for (int i = 0; i < 4; ++i)
                for (int j = i + 1; j < 4; ++j)
                {
                    const int a = std::min(T.v[i], T.v[j]), b = std::max(T.v[i], T.v[j]);
                    keys.push_back((uint64_t(uint32_t(a)) << 32) | uint32_t(b));
                }
        }
        std::sort(keys.begin(), keys.end());
        keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
        for (auto k : keys) beams.push_back({int(k >> 32), int(k & 0xffffffffu)});
    }
    else
    {
        nodes.clear();
        std::vector<int> id(D.tets.size(), -1);
        for (int t : D.finite())
        {
            id[size_t(t)] = int(nodes.size());
            nodes.push_back(D.tets[size_t(t)].c);
        }
        for (int t : D.finite())
            for (int i = 0; i < 4; ++i)
            {
                const int u = D.tets[size_t(t)].n[i];
                if (u > t && D.isFinite(u)) beams.push_back({id[size_t(t)], id[size_t(u)]});
            }
    }
}

}   // anonymous namespace

////////////////////////////////////////////////////////////////////////////////

Tree beamLattice(const Graph& g, const std::vector<double>& radius, double blend)
{
    auto data = std::make_shared<const BeamData>(g, radius, blend);
    return Tree(std::make_unique<BeamClause>(data));
}

std::vector<std::array<int, 4>> delaunay(const std::vector<V3>& pts)
{
    Delaunay D(pts);
    std::vector<std::array<int, 4>> out;
    for (int t : D.finite())
    {
        const Tet& T = D.tets[size_t(t)];
        out.push_back({T.v[0], T.v[1], T.v[2], T.v[3]});
    }
    return out;
}

Graph pointsGraph(const std::vector<V3>& pts, int mode)
{
    Delaunay D(pts);
    std::vector<V3> nodes;
    std::vector<std::array<int, 2>> beams;
    delaunayBeams(D, mode, nodes, beams);
    return compact(nodes, beams);
}

Graph volumeGraph(const Tree& body, const V3& lo, const V3& hi, double spacing, int mode,
                  int relax, unsigned seed, std::string& error)
{
    Graph g;
    if (!(spacing > 0))
    {
        error = "spacing must be positive";
        return g;
    }
    const V3 plo = (lo.array() - spacing).matrix(), phi = (hi.array() + spacing).matrix();
    PoissonGrid grid(plo, phi, spacing);
    if (!grid.ok)
    {
        error = "spacing too small for this region (too many points)";
        return g;
    }
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> U(0.0, 1.0);
    const V3 ext = phi - plo;
    const double expected = ext.prod() / (0.75 * spacing * spacing * spacing);
    if (expected > 2e6)
    {
        error = "spacing too small for this region (too many points)";
        return g;
    }
    std::vector<V3> pts;
    const size_t tries = size_t(std::min(8e6, 30 * expected + 1000));
    for (size_t k = 0; k < tries; ++k)
    {
        const V3 p(plo[0] + U(rng) * ext[0], plo[1] + U(rng) * ext[1], plo[2] + U(rng) * ext[2]);
        grid.tryAdd(p, pts);
    }
    if (pts.size() < 5)
    {
        error = "too few points: make the spacing smaller";
        return g;
    }
    // Relaxation: each point moves towards the mean of its Voronoi vertices
    for (int it = 0; it < relax; ++it)
    {
        Delaunay D(pts);
        std::vector<V3> sum(pts.size(), V3::Zero());
        std::vector<int> cnt(pts.size(), 0);
        for (int t : D.finite())
        {
            const Tet& T = D.tets[size_t(t)];
            for (int i = 0; i < 4; ++i)
            {
                sum[size_t(T.v[i])] += T.c;
                cnt[size_t(T.v[i])]++;
            }
        }
        for (size_t i = 0; i < pts.size(); ++i)
        {
            if (!cnt[i]) continue;
            V3 target = sum[i] / cnt[i];
            V3 step = target - pts[i];
            const double len = step.norm();
            if (len > 0.5 * spacing) step *= 0.5 * spacing / len;
            V3 q = pts[i] + 0.8 * step;
            q = q.cwiseMax(plo).cwiseMin(phi);
            pts[i] = q;
        }
    }
    Delaunay D(pts);
    std::vector<V3> nodes;
    std::vector<std::array<int, 2>> beams;
    delaunayBeams(D, mode, nodes, beams);

    // drop beams that stay outside the body (5 samples along each)
    std::vector<V3> samples;
    samples.reserve(beams.size() * 5);
    for (const auto& b : beams)
        for (int s = 0; s < 5; ++s)
        {
            const double t = s / 4.0;
            samples.push_back(nodes[size_t(b[0])] * (1 - t) + nodes[size_t(b[1])] * t);
        }
    const auto vals = evalMany(body, samples);
    std::vector<std::array<int, 2>> kept;
    for (size_t i = 0; i < beams.size(); ++i)
    {
        const V3 a = nodes[size_t(beams[i][0])], b = nodes[size_t(beams[i][1])];
        // (Voronoi vertices far outside the padding are hull artefacts)
        const double far = 4 * spacing;
        if ((a.array() < plo.array() - far).any() || (a.array() > phi.array() + far).any() ||
            (b.array() < plo.array() - far).any() || (b.array() > phi.array() + far).any())
            continue;
        float m = vals[i * 5];
        for (int s = 1; s < 5; ++s) m = std::min(m, vals[i * 5 + size_t(s)]);
        if (m < 0.5 * spacing) kept.push_back(beams[i]);
    }
    g = compact(nodes, kept);
    if (g.beams.empty()) error = "no beams inside the body";
    return g;
}

Graph surfaceGraph(const Tree& body, const V3& lo, const V3& hi, double spacing, int mode,
                   unsigned seed, std::string& error)
{
    Graph g;
    if (!(spacing > 0))
    {
        error = "spacing must be positive";
        return g;
    }
    // 1. The surface as triangles (a moderately fine mesh)
    BRepSettings settings;
    settings.min_feature = spacing / 4;
    settings.workers = 8;
    const V3 plo = (lo.array() - 0.5 * spacing).matrix(), phi = (hi.array() + 0.5 * spacing).matrix();
    Region<3> region(plo.array(), phi.array());
    auto mesh = Mesh::render(body, region, settings);
    if (!mesh || mesh->branes.empty())
    {
        error = "the body has no surface in this region";
        return g;
    }
    std::vector<double> cdf;
    cdf.reserve(mesh->branes.size());
    double area = 0;
    for (const auto& t : mesh->branes)
    {
        const V3 a = mesh->verts[t[0]].cast<double>(), b = mesh->verts[t[1]].cast<double>(),
                 c = mesh->verts[t[2]].cast<double>();
        area += 0.5 * (b - a).cross(c - a).norm();
        cdf.push_back(area);
    }
    const double expected = area / (0.7 * spacing * spacing);
    if (expected > 1e6)
    {
        error = "spacing too small for this surface (too many points)";
        return g;
    }
    // 2. Candidates, area-weighted, projected onto the exact surface
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> U(0.0, 1.0);
    const size_t tries = size_t(std::min(4e6, 30 * expected + 1000));
    std::vector<V3> cand;
    cand.reserve(tries);
    for (size_t k = 0; k < tries; ++k)
    {
        const double x = U(rng) * area;
        const size_t ti = size_t(std::lower_bound(cdf.begin(), cdf.end(), x) - cdf.begin());
        const auto& t = mesh->branes[std::min(ti, mesh->branes.size() - 1)];
        double u = U(rng), v = U(rng);
        if (u + v > 1) { u = 1 - u; v = 1 - v; }
        const V3 a = mesh->verts[t[0]].cast<double>(), b = mesh->verts[t[1]].cast<double>(),
                 c = mesh->verts[t[2]].cast<double>();
        cand.push_back(a + u * (b - a) + v * (c - a));
    }
    {
        DerivArrayEvaluator e(body);
        const size_t N = ArrayEvaluator::N;
        for (int iter = 0; iter < 2; ++iter)
            for (size_t start = 0; start < cand.size(); start += N)
            {
                const size_t count = std::min(N, cand.size() - start);
                for (size_t k = 0; k < count; ++k) e.set(cand[start + k].cast<float>(), k);
                const auto d = e.derivs(count);
                for (size_t k = 0; k < count; ++k)
                {
                    const V3 gr(d(0, k), d(1, k), d(2, k));
                    const double g2 = gr.squaredNorm();
                    if (g2 > 1e-20 && std::isfinite(d(3, k)))
                    {
                        V3 stepv = gr * (d(3, k) / g2);
                        if (stepv.norm() > spacing) stepv *= spacing / stepv.norm();
                        cand[start + k] -= stepv;
                    }
                }
            }
    }
    // 3. Poisson-disk selection (with a tiny jitter off the surface, so
    //    points on flat faces aren't exactly coplanar for the Delaunay step)
    PoissonGrid grid((plo.array() - spacing).matrix(), (phi.array() + spacing).matrix(), spacing);
    if (!grid.ok)
    {
        error = "spacing too small for this region (too many points)";
        return g;
    }
    std::vector<V3> pts;
    std::normal_distribution<double> Nrm(0.0, 1.0);
    for (const auto& p : cand)
    {
        if (!(p.array() >= plo.array()).all() || !(p.array() <= phi.array()).all()) continue;
        if (grid.tryAdd(p, pts))
        {
            pts.back() += 2e-3 * spacing * V3(Nrm(rng), Nrm(rng), Nrm(rng));
        }
    }
    if (pts.size() < 5)
    {
        error = "too few points on the surface: make the spacing smaller";
        return g;
    }

    // 4. Delaunay; a face is on the surface when its dual Voronoi edge
    //    crosses it (restricted Delaunay triangulation)
    Delaunay D(pts);
    const double diag = (phi - plo).norm();
    struct Crossing { int a, b, c; V3 x; };
    std::vector<Crossing> faces;
    std::vector<V3> segA, segB;
    std::vector<std::array<int, 3>> segFace;
    for (int t = 0; t < int(D.tets.size()); ++t)
    {
        if (!D.isFinite(t)) continue;
        const Tet& T = D.tets[size_t(t)];
        for (int i = 0; i < 4; ++i)
        {
            const int u = T.n[i];
            if (D.isFinite(u) && u < t) continue;       // (each face once)
            int f[3], k = 0;
            for (int j = 0; j < 4; ++j) if (j != i) f[k++] = T.v[j];
            V3 end;
            if (D.isFinite(u))
            {
                end = D.tets[size_t(u)].c;
            }
            else
            {
                // hull face: a ray away from the tet
                const V3 A = D.pts[size_t(f[0])], B = D.pts[size_t(f[1])], C = D.pts[size_t(f[2])];
                V3 nrm = (B - A).cross(C - A).normalized();
                if (nrm.dot(D.pts[size_t(T.v[i])] - A) > 0) nrm = -nrm;
                end = T.c + nrm * diag;
            }
            V3 start = T.c;
            // keep the dual segments within reach of the region
            segA.push_back(start);
            segB.push_back(end);
            segFace.push_back({f[0], f[1], f[2]});
        }
    }
    // sample each dual segment at 6 points
    const int S = 6;
    std::vector<V3> samples;
    samples.reserve(segA.size() * S);
    for (size_t i = 0; i < segA.size(); ++i)
        for (int s = 0; s < S; ++s)
        {
            const double t = s / double(S - 1);
            samples.push_back(segA[i] * (1 - t) + segB[i] * t);
        }
    const auto vals = evalMany(body, samples);
    std::vector<V3> bisectA, bisectB;
    std::vector<size_t> which;
    for (size_t i = 0; i < segA.size(); ++i)
    {
        for (int s = 0; s + 1 < S; ++s)
        {
            const float va = vals[i * S + size_t(s)], vb = vals[i * S + size_t(s) + 1];
            if ((va < 0) != (vb < 0))
            {
                const double t0 = s / double(S - 1), t1 = (s + 1) / double(S - 1);
                bisectA.push_back(segA[i] * (1 - t0) + segB[i] * t0);
                bisectB.push_back(segA[i] * (1 - t1) + segB[i] * t1);
                which.push_back(i);
                break;
            }
        }
    }
    // bisection for the crossing points (all at once)
    std::vector<V3> lo_(bisectA), hi_(bisectB);
    std::vector<float> vlo = evalMany(body, lo_);
    for (int it = 0; it < 24; ++it)
    {
        std::vector<V3> mid(lo_.size());
        for (size_t i = 0; i < mid.size(); ++i) mid[i] = (lo_[i] + hi_[i]) / 2;
        const auto vm = evalMany(body, mid);
        for (size_t i = 0; i < mid.size(); ++i)
        {
            if ((vm[i] < 0) == (vlo[i] < 0)) { lo_[i] = mid[i]; vlo[i] = vm[i]; }
            else hi_[i] = mid[i];
        }
    }
    for (size_t k = 0; k < which.size(); ++k)
    {
        const auto& f = segFace[which[k]];
        const V3 x = (lo_[k] + hi_[k]) / 2;
        // only surface triangles near their crossing point (long hull
        // faces spanning the whole body are artefacts)
        const V3 A = D.pts[size_t(f[0])], B = D.pts[size_t(f[1])], C = D.pts[size_t(f[2])];
        const double longest = std::max({(A - B).norm(), (B - C).norm(), (C - A).norm()});
        if (longest > 3.5 * spacing) continue;
        faces.push_back({f[0], f[1], f[2], x});
    }
    if (faces.empty())
    {
        error = "no surface triangles found";
        return g;
    }

    std::vector<V3> nodes;
    std::vector<std::array<int, 2>> beams;
    if (mode == 0)
    {
        nodes = D.pts;
        nodes.resize(size_t(D.N));
        std::vector<uint64_t> keys;
        for (const auto& f : faces)
        {
            const int v[3] = {f.a, f.b, f.c};
            for (int i = 0; i < 3; ++i)
            {
                const int a = std::min(v[i], v[(i + 1) % 3]), b = std::max(v[i], v[(i + 1) % 3]);
                keys.push_back((uint64_t(uint32_t(a)) << 32) | uint32_t(b));
            }
        }
        std::sort(keys.begin(), keys.end());
        keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
        for (auto k : keys) beams.push_back({int(k >> 32), int(k & 0xffffffffu)});
    }
    else
    {
        // dual: crossing points of faces that share an edge
        std::map<uint64_t, std::vector<int>> byEdge;
        for (size_t i = 0; i < faces.size(); ++i)
        {
            nodes.push_back(faces[i].x);
            const int v[3] = {faces[i].a, faces[i].b, faces[i].c};
            for (int e = 0; e < 3; ++e)
            {
                const int a = std::min(v[e], v[(e + 1) % 3]), b = std::max(v[e], v[(e + 1) % 3]);
                byEdge[(uint64_t(uint32_t(a)) << 32) | uint32_t(b)].push_back(int(i));
            }
        }
        for (const auto& kv : byEdge)
        {
            const auto& fs = kv.second;
            if (fs.size() == 2) beams.push_back({fs[0], fs[1]});
            else if (fs.size() > 2)
                for (size_t i = 0; i + 1 < fs.size(); ++i) beams.push_back({fs[i], fs[i + 1]});
        }
    }
    g = compact(nodes, beams);
    return g;
}

}   // namespace lattice
}   // namespace libfive
