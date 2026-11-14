/*
libfive: a CAD kernel for modeling with implicit functions

The mender of tetrahedral meshes; see tetmender.hpp.  A port of the python passes that mesh_tet4 had (meshing.py: _optimize, _improve, _split_improve, _smooth,
_smooth_boundary), which were written after Netgen's open source; here they run at the speed of the kernel, so that the mesh of an analysis gets them too.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <functional>
#include <iterator>
#include <memory>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "libfive/fea/tetmender.hpp"
#include "libfive/eval/eval_array.hpp"
#include "libfive/run_progress.hpp"

namespace libfive {
namespace fea {

namespace {

using Vec3 = Eigen::Vector3d;
using Tet = std::array<int, 4>;
using Tri = std::array<int, 3>;

const int kPairs[6][2] = {{0, 1}, {0, 2}, {0, 3}, {1, 2}, {1, 3}, {2, 3}};
const int kFaces[4][3] = {{0, 1, 2}, {0, 1, 3}, {0, 2, 3}, {1, 2, 3}};
const double kBadness = 0.0080187537;      // Netgen's constant: sqrt(216) / (6^4 sqrt(2)), which makes the badness of a regular tetrahedron 1

inline uint64_t edgeKey(int a, int b)
{
    if (a > b) std::swap(a, b);
    return (uint64_t(uint32_t(a)) << 32) | uint32_t(b);
}

inline Tri sortedTri(int a, int b, int c)
{
    Tri t = {a, b, c};
    std::sort(t.begin(), t.end());
    return t;
}

inline bool cancelled()
{
    return run_progress::cancelFlag().load(std::memory_order_relaxed);
}

// (a replacement of tetrahedra: what an operation would do, and what it does to the edges of the surface)
struct Change
{
    bool ok = false;
    double after = -2.0;                // the worst mean ratio among the new tetrahedra
    std::vector<Tet> news;
    std::vector<int> olds;
    std::vector<uint64_t> goneEdges;    // edges of the surface that go
    std::vector<uint64_t> addedEdges;   // edges of the surface that come
    bool surfaceChanged = false;
};

struct Minimum
{
    bool ok = false;
    double f = 0.0;
    Vec3 x = Vec3::Zero();
};

class Mender
{
public:
    Mender(const TetMesh& mesh, const Tree* shape, const MendOptions& options)
        : P(mesh.pos), T(mesh.tets.begin(), mesh.tets.end()), shape_(shape), opt(options), h(mesh.h > 0 ? mesh.h : 1.0)
    {
        if (!shape_) opt.surface = false;
        nodeTets.assign(P.size(), {});
        for (size_t i = 0; i < T.size(); ++i) attach(int(i));
        if (!opt.holdPlanes.empty())
        {
            held.assign(P.size(), 0);
            for (size_t v = 0; v < P.size(); ++v)
                for (const auto& plane : opt.holdPlanes)
                    if (std::abs(P[v][plane.first] - plane.second) <= opt.holdTolerance) held[v] = 1;
        }
    }

    bool run();
    void result(TetMesh& mesh) const
    {
        mesh.pos = P;
        mesh.tets.clear();
        for (const Tet& t : T)
            if (t[0] >= 0) mesh.tets.push_back(t);
    }

    MendStats stats;
    double secFlips = 0.0;                      // (H223, performance: the seconds that improve() spent on the flips of the surface, printed with FIELDES_TET_DEBUG)

private:
    std::vector<Vec3> P;
    std::vector<Tet> T;                         // a tetrahedron that is gone has T[i][0] = -1
    std::vector<std::vector<int>> nodeTets;
    std::unordered_set<uint64_t> boundaryEdges;
    std::vector<char> onSurface;
    std::vector<char> held;                    // (a node on a plane the mesh is mirrored about: see MendOptions::holdPlanes)
    const Tree* shape_;
    std::unique_ptr<ArrayEvaluator> evaluator;
    MendOptions opt;
    double h;

    // ---------------------------------------------------------------- the mesh
    bool alive(int i) const { return T[size_t(i)][0] >= 0; }
    bool isHeld(int v) const { return size_t(v) < held.size() && held[size_t(v)]; }

    // (H223, performance: every change of the tetrahedra -- a swap, a collapse, a split -- goes through attach() and detach(), which count it: the surface is made again only when the count has moved, the smoothing
    // and the splits that change nothing no longer pay for a sort of all the faces of the mesh)
    uint64_t topoVersion = 0;
    uint64_t surfaceVersion = ~uint64_t(0);                 // (the count at which onSurface and boundaryEdges were made)
    mutable uint64_t facesVersion = ~uint64_t(0);           // (the count at which facesCache was made)
    mutable std::vector<Tri> facesCache;

    void attach(int i)
    {
        ++topoVersion;
        for (int v : T[size_t(i)]) nodeTets[size_t(v)].push_back(i);
    }

    void detach(int i)
    {
        ++topoVersion;
        for (int v : T[size_t(i)])
        {
            auto& list = nodeTets[size_t(v)];
            list.erase(std::remove(list.begin(), list.end(), i), list.end());
        }
    }

    static bool has(const Tet& t, int v) { return t[0] == v || t[1] == v || t[2] == v || t[3] == v; }

    double det6(int a, int b, int c, int d) const
    {
        return (P[size_t(b)] - P[size_t(a)]).cross(P[size_t(c)] - P[size_t(a)]).dot(P[size_t(d)] - P[size_t(a)]);
    }

    // The mean ratio of a tetrahedron, 12 (3 V)^(2/3) over the sum of its squared edges: 1 for a regular one, 0 for one with no volume (-1 inside out)
    double ratio(const Tet& t) const
    {
        const double v = det6(t[0], t[1], t[2], t[3]) / 6.0;
        if (v <= 0.0) return -1.0;
        double s = 0.0;
        for (const auto& p : kPairs) s += (P[size_t(t[size_t(p[0])])] - P[size_t(t[size_t(p[1])])]).squaredNorm();
        return 12.0 * std::pow(3.0 * v, 2.0 / 3.0) / s;
    }

    double ratio4(int a, int b, int c, int d) const { return ratio(Tet{a, b, c, d}); }

    // Netgen's badness of a tetrahedron (CalcTetBadness)
    double badness(const Tet& t) const
    {
        const double d6 = det6(t[0], t[1], t[2], t[3]);
        double s = 0.0;
        for (const auto& p : kPairs) s += (P[size_t(t[size_t(p[0])])] - P[size_t(t[size_t(p[1])])]).squaredNorm();
        const double cube = s * std::sqrt(s);
        if (d6 / 6.0 <= 1e-24 * cube) return 1e24;
        const double err = kBadness * cube / (d6 / 6.0);
        return err * err;
    }

    std::vector<int> tetsOfEdge(int a, int b) const
    {
        std::vector<int> out;
        for (int i : nodeTets[size_t(a)])
            if (has(T[size_t(i)], b)) out.push_back(i);
        return out;
    }

    int owners(int a, int b, int c) const
    {
        int n = 0;
        for (int i : nodeTets[size_t(a)])
            if (has(T[size_t(i)], b) && has(T[size_t(i)], c)) ++n;
        return n;
    }

    bool edgeExists(int a, int b) const
    {
        for (int i : nodeTets[size_t(a)])
            if (has(T[size_t(i)], b)) return true;
        return false;
    }

    // The faces that only one tetrahedron has, as the tetrahedron has them (the normal out)
    std::vector<Tri> surfaceFaces() const
    {
        if (P.size() < (size_t(1) << 21))
        {
            // (H223, performance: the three nodes of a face packed into one number of 63 bits and 16 bytes a record instead of 24 bytes with the triple compared a number at a time: the sort of the 900,000 faces of a mesh
            // of 228,000 tetrahedra took 0.12 s and is made some thirty times.  The order of the faces that come out is the same: the packed number sorts as the sorted triple does)
            struct Packed { uint64_t key; uint32_t tet; uint32_t which; };
            auto pack = [](int x, int y, int z) {
                int s[3] = {x, y, z};
                if (s[0] > s[1]) std::swap(s[0], s[1]);
                if (s[1] > s[2]) std::swap(s[1], s[2]);
                if (s[0] > s[1]) std::swap(s[0], s[1]);
                return (uint64_t(uint32_t(s[0])) << 42) | (uint64_t(uint32_t(s[1])) << 21) | uint64_t(uint32_t(s[2]));
            };
            std::vector<Packed> packed;
            packed.reserve(T.size() * 4);
            for (size_t i = 0; i < T.size(); ++i)
            {
                const Tet& t = T[i];
                if (t[0] < 0) continue;
                const int a = t[0], b = t[1], c = t[2], d = t[3];
                packed.push_back({pack(a, c, b), uint32_t(i), 0u});
                packed.push_back({pack(a, b, d), uint32_t(i), 1u});
                packed.push_back({pack(a, d, c), uint32_t(i), 2u});
                packed.push_back({pack(b, c, d), uint32_t(i), 3u});
            }
            std::sort(packed.begin(), packed.end(), [](const Packed& x, const Packed& y) { return x.key < y.key; });
            std::vector<Tri> boundary;
            for (size_t i = 0; i < packed.size();)
            {
                size_t j = i + 1;
                while (j < packed.size() && packed[j].key == packed[i].key) ++j;
                if (j - i == 1)
                {
                    const Tet& t = T[size_t(packed[i].tet)];
                    const int a = t[0], b = t[1], c = t[2], d = t[3];
                    switch (packed[i].which)
                    {
                        case 0u: boundary.push_back(Tri{a, c, b}); break;
                        case 1u: boundary.push_back(Tri{a, b, d}); break;
                        case 2u: boundary.push_back(Tri{a, d, c}); break;
                        default: boundary.push_back(Tri{b, c, d}); break;
                    }
                }
                i = j;
            }
            return boundary;
        }
        struct Rec { Tri key; Tri face; };
        std::vector<Rec> recs;
        recs.reserve(T.size() * 4);
        for (const Tet& t : T)
        {
            if (t[0] < 0) continue;
            const int a = t[0], b = t[1], c = t[2], d = t[3];
            recs.push_back({sortedTri(a, c, b), Tri{a, c, b}});
            recs.push_back({sortedTri(a, b, d), Tri{a, b, d}});
            recs.push_back({sortedTri(a, d, c), Tri{a, d, c}});
            recs.push_back({sortedTri(b, c, d), Tri{b, c, d}});
        }
        std::sort(recs.begin(), recs.end(), [](const Rec& x, const Rec& y) { return x.key < y.key; });
        std::vector<Tri> out;
        for (size_t i = 0; i < recs.size();)
        {
            size_t j = i + 1;
            while (j < recs.size() && recs[j].key == recs[i].key) ++j;
            if (j - i == 1) out.push_back(recs[i].face);
            i = j;
        }
        return out;
    }

    // The faces of the surface as they are now: made once for each state of the tetrahedra (see topoVersion)
    const std::vector<Tri>& surfaceFacesNow() const
    {
        if (facesVersion != topoVersion)
        {
            facesCache = surfaceFaces();
            facesVersion = topoVersion;
        }
        return facesCache;
    }

    // The nodes and the edges on the surface, from the tetrahedra as they are (not made again when the tetrahedra have not changed since it was made: the smoothing only moves nodes)
    void refreshSurface()
    {
        if (surfaceVersion == topoVersion && onSurface.size() == P.size()) return;
        onSurface.assign(P.size(), 0);
        boundaryEdges.clear();
        for (const Tri& f : surfaceFacesNow())
        {
            for (int v : f) onSurface[size_t(v)] = 1;
            boundaryEdges.insert(edgeKey(f[0], f[1]));
            boundaryEdges.insert(edgeKey(f[1], f[2]));
            boundaryEdges.insert(edgeKey(f[2], f[0]));
        }
        surfaceVersion = topoVersion;
    }

    // ---------------------------------------------------------------- the field
    void values(const std::vector<Vec3>& pts, std::vector<double>& out)
    {
        out.assign(pts.size(), 0.0);
        if (!shape_ || pts.empty()) return;
        if (pts.size() > 8192)
        {
            std::vector<Eigen::Vector3f> in(pts.size());
            for (size_t i = 0; i < pts.size(); ++i) in[i] = pts[i].cast<float>();
            std::vector<float> res;
            evalTreePoints(*shape_, in, res);
            for (size_t i = 0; i < pts.size(); ++i) out[i] = double(res[i]);
            return;
        }
        if (!evaluator) evaluator.reset(new ArrayEvaluator(*shape_));
        const size_t N = ArrayEvaluator::N;
        for (size_t s = 0; s < pts.size(); s += N)
        {
            const size_t count = std::min(N, pts.size() - s);
            for (size_t k = 0; k < count; ++k) evaluator->set(pts[s + k].cast<float>(), k);
            const auto vs = evaluator->values(count);
            for (size_t k = 0; k < count; ++k) out[s + k] = double(vs(k));
        }
    }

    // The value and the gradient of the field at each point, by central differences of 2 % of the scale there
    void gradients(const std::vector<Vec3>& pts, const std::vector<double>& scales, std::vector<double>& val, std::vector<Vec3>& grad)
    {
        std::vector<Vec3> samples;
        samples.reserve(pts.size() * 7);
        std::vector<double> steps(pts.size());
        for (size_t i = 0; i < pts.size(); ++i)
        {
            const double e = std::max(1e-7, 0.02 * scales[i]);
            steps[i] = e;
            const Vec3& p = pts[i];
            samples.push_back(p);
            samples.push_back(p + Vec3(e, 0, 0));
            samples.push_back(p - Vec3(e, 0, 0));
            samples.push_back(p + Vec3(0, e, 0));
            samples.push_back(p - Vec3(0, e, 0));
            samples.push_back(p + Vec3(0, 0, e));
            samples.push_back(p - Vec3(0, 0, e));
        }
        std::vector<double> v;
        values(samples, v);
        val.resize(pts.size());
        grad.resize(pts.size());
        for (size_t i = 0; i < pts.size(); ++i)
        {
            const double* q = &v[7 * i];
            val[i] = q[0];
            grad[i] = Vec3((q[1] - q[2]) / (2 * steps[i]), (q[3] - q[4]) / (2 * steps[i]), (q[5] - q[6]) / (2 * steps[i]));
        }
    }

    // The points moved onto the surface of the field (its value 0), by Newton steps along its gradient, no further than half of the scale at a time
    std::vector<Vec3> snapToSurface(std::vector<Vec3> cur, const std::vector<double>& scales, int iterations = 3)
    {
        for (int it = 0; it < iterations; ++it)
        {
            std::vector<double> val;
            std::vector<Vec3> grad;
            gradients(cur, scales, val, grad);
            for (size_t i = 0; i < cur.size(); ++i)
            {
                const double g2 = grad[i].squaredNorm();
                if (g2 < 1e-24) continue;
                Vec3 d = (val[i] / g2) * grad[i];
                const double length = d.norm();
                const double limit = 0.5 * scales[i];
                if (length > limit) d *= limit / length;
                cur[i] -= d;
            }
        }
        return cur;
    }

    // ---------------------------------------------------------------- the badness of the tetrahedra round a node, and its gradient (Netgen's PointFunction)
    double badnessSumGrad(const std::vector<Tet>& tets, int n, const Vec3& x, Vec3& grad) const
    {
        double total = 0.0;
        grad.setZero();
        for (const Tet& t : tets)
        {
            int k = 0;
            Vec3 q[4];
            for (int m = 0; m < 4; ++m)
            {
                if (t[size_t(m)] == n) { q[m] = x; k = m; }
                else q[m] = P[size_t(t[size_t(m)])];
            }
            const Vec3 u = q[1] - q[0], v = q[2] - q[0], w = q[3] - q[0];
            const Vec3 cvw = v.cross(w);
            const double D = u.dot(cvw);
            const double L = u.squaredNorm() + v.squaredNorm() + w.squaredNorm() + (q[1] - q[2]).squaredNorm() + (q[1] - q[3]).squaredNorm() + (q[2] - q[3]).squaredNorm();
            const double cube = L * std::sqrt(L);
            if (D <= 6e-24 * cube)
            {
                grad.setZero();
                return 1e24;
            }
            const double err = kBadness * cube * 6.0 / D;
            const double b = err * err;
            total += b;
            Vec3 dD;
            if (k == 1) dD = cvw;
            else if (k == 2) dD = w.cross(u);
            else if (k == 3) dD = u.cross(v);
            else dD = -(cvw + w.cross(u) + u.cross(v));
            const Vec3 dL = 2.0 * (3.0 * q[k] - (q[0] + q[1] + q[2] + q[3] - q[k]));
            grad += b * (3.0 * dL / L - 2.0 * dD / D);
        }
        return total;
    }

    // The place for the node n that makes the sum of the badness of its tetrahedra least, found from `start` by BFGS with a backtracking line search
    Minimum minimizeNode(const std::vector<Tet>& tets, int n, double size, const Vec3& start) const
    {
        Minimum out;
        Vec3 x = start, g;
        double f = badnessSumGrad(tets, n, x, g);
        if (f >= 1e24) return out;
        Eigen::Matrix3d H = Eigen::Matrix3d::Identity();
        bool first = true;
        for (int it = 0; it < 20; ++it)
        {
            const double gg = g.squaredNorm();
            if (gg <= 0.0) break;
            Vec3 d = -(H * g);
            double slope = d.dot(g);
            if (slope >= 0.0)
            {
                H.setIdentity();
                d = -g;
                slope = -gg;
            }
            const double length = d.norm();
            const double reach = (first ? 0.25 : 0.5) * size;
            if (length <= 0.0) break;
            if (first || length > reach)
            {
                d *= reach / length;
                slope = d.dot(g);
            }
            first = false;
            double t = 1.0;
            bool found = false;
            Vec3 xn, gn;
            double fn = 0.0;
            for (int ls = 0; ls < 20; ++ls)
            {
                xn = x + t * d;
                fn = badnessSumGrad(tets, n, xn, gn);
                if (fn < f + 1e-4 * t * slope) { found = true; break; }
                t *= 0.5;
            }
            if (!found) break;
            const Vec3 s = xn - x, y = gn - g;
            const double sy = s.dot(y);
            const double gain = f - fn;
            x = xn;
            f = fn;
            g = gn;
            if (gain <= 1e-6 * f) break;
            if (sy > 1e-14)
            {
                const double rho = 1.0 / sy;
                const Vec3 Hy = H * y;
                const double yHy = y.dot(Hy);
                H = H + (1.0 + rho * yHy) * rho * (s * s.transpose()) - rho * (s * Hy.transpose() + Hy * s.transpose());
            }
        }
        out.ok = true;
        out.f = f;
        out.x = x;
        return out;
    }

    // Netgen's badness of a triangle (CalcTriangleBadness without its size term): the sum of the squared edges over 4 sqrt(3) times the area, less 1
    double triBadness(int a, int b, int c) const
    {
        const Vec3 u = P[size_t(b)] - P[size_t(a)], v = P[size_t(c)] - P[size_t(a)];
        const double twice = u.cross(v).norm();
        const double ss = u.squaredNorm() + v.squaredNorm() + (u - v).squaredNorm();
        if (twice <= 1e-12 * ss) return 1e10;
        return ss / (3.4641016151377544 * twice) - 1.0;
    }

    // ---------------------------------------------------------------- the smoothing of the interior nodes
    void smooth(int iterations, double below)
    {
        refreshSurface();
        std::vector<double> quality(T.size(), 0.0);
        for (size_t i = 0; i < T.size(); ++i)
            if (alive(int(i))) quality[i] = ratio(T[i]);
        std::vector<int> nodes;
        for (size_t n = 0; n < P.size(); ++n)
            if (!onSurface[n] && !nodeTets[n].empty()) nodes.push_back(int(n));
        for (int it = 0; it < iterations; ++it)
        {
            size_t moved = 0;
            for (int n : nodes)
            {
                if ((moved & 0xFFF) == 0 && cancelled()) return;
                const std::vector<int>& ids = nodeTets[size_t(n)];
                if (ids.empty()) continue;
                double worst = 1e30;
                for (int i : ids) worst = std::min(worst, quality[size_t(i)]);
                if (worst >= below) continue;
                std::vector<Tet> mine;
                std::vector<int> adjacent;
                for (int i : ids)
                {
                    mine.push_back(T[size_t(i)]);
                    for (int v : T[size_t(i)])
                        if (v != n) adjacent.push_back(v);
                }
                std::sort(adjacent.begin(), adjacent.end());
                adjacent.erase(std::unique(adjacent.begin(), adjacent.end()), adjacent.end());
                const Vec3 p0 = P[size_t(n)];
                double size = 0.0;
                for (int v : adjacent) size += (P[size_t(v)] - p0).norm();
                size /= double(adjacent.size());
                Vec3 g;
                const double before = badnessSumGrad(mine, n, p0, g);
                const Minimum m = minimizeNode(mine, n, size, p0);
                if (!m.ok || m.f >= before) continue;
                P[size_t(n)] = m.x;
                std::vector<double> values;
                double lowest = 1e30;
                for (int i : ids)
                {
                    values.push_back(ratio(T[size_t(i)]));
                    lowest = std::min(lowest, values.back());
                }
                if (lowest < worst - 1e-12)         // (the worst of them is never left poorer: the sum of the badness is dominated by it, but it can go with a move that mends two others)
                {
                    P[size_t(n)] = p0;
                    continue;
                }
                for (size_t k = 0; k < ids.size(); ++k) quality[size_t(ids[k])] = values[k];
                ++moved;
            }
            stats.moved += moved;
            if (!moved) break;
        }
    }

    // ---------------------------------------------------------------- the nodes of the surface moved along it
    void smoothBoundary(double crease = 40.0)
    {
        const double cosCrease = std::cos(crease * 3.14159265358979323846 / 180.0);
        const std::vector<Tri>& faces = surfaceFacesNow();
        std::unordered_map<int, Vec3> total;
        std::unordered_map<int, std::vector<Vec3>> vectors;
        std::unordered_map<int, std::vector<int>> adjacent;
        for (const Tri& f : faces)
        {
            const Vec3 area = 0.5 * (P[size_t(f[1])] - P[size_t(f[0])]).cross(P[size_t(f[2])] - P[size_t(f[0])]);
            for (int v : f)
            {
                auto it = total.find(v);
                if (it == total.end()) total[v] = area; else it->second += area;
                vectors[v].push_back(area);
            }
            for (int i = 0; i < 3; ++i)
            {
                const int a = f[size_t(i)], b = f[size_t((i + 1) % 3)];
                adjacent[a].push_back(b);
                adjacent[b].push_back(a);
            }
        }
        for (auto& kv : adjacent)
        {
            std::sort(kv.second.begin(), kv.second.end());
            kv.second.erase(std::unique(kv.second.begin(), kv.second.end()), kv.second.end());
        }
        std::vector<double> quality(T.size(), 0.0);
        for (size_t i = 0; i < T.size(); ++i)
            if (alive(int(i))) quality[i] = ratio(T[i]);
        struct Movable { int v; Vec3 n; };
        std::vector<Movable> movable;
        for (const auto& kv : total)
        {
            const int v = kv.first;
            if (isHeld(v)) continue;
            const double length = kv.second.norm();
            double worst = 1e30;
            for (int i : nodeTets[size_t(v)]) worst = std::min(worst, quality[size_t(i)]);
            if (length <= 0.0 || worst >= 0.6) continue;
            const Vec3 n = kv.second / length;
            bool flat = true;
            for (const Vec3& a : vectors[v])
            {
                const double al = a.norm();
                if (al > 0.0 && a.dot(n) / al < cosCrease) { flat = false; break; }
            }
            if (flat) movable.push_back({v, n});
        }
        if (movable.empty()) return;
        std::sort(movable.begin(), movable.end(), [](const Movable& x, const Movable& y) { return x.v < y.v; });
        std::vector<Vec3> proposals;
        std::vector<double> scales;
        for (const Movable& m : movable)
        {
            const std::vector<int>& ring = adjacent[m.v];
            Vec3 mean = Vec3::Zero();
            for (int u : ring) mean += P[size_t(u)];
            mean /= double(ring.size());
            const Vec3 d = mean - P[size_t(m.v)];
            const double along = d.dot(m.n);
            proposals.push_back(P[size_t(m.v)] + d - along * m.n);
            scales.push_back(d.norm() + 1e-9);
        }
        const std::vector<Vec3> snapped = snapToSurface(proposals, scales);
        for (size_t k = 0; k < movable.size(); ++k)
        {
            const int v = movable[k].v;
            const std::vector<int>& ids = nodeTets[size_t(v)];
            double before = 1e30;
            for (int i : ids) before = std::min(before, quality[size_t(i)]);
            const Vec3 old = P[size_t(v)];
            P[size_t(v)] = snapped[k];
            std::vector<double> values;
            double lowest = 1e30;
            for (int i : ids)
            {
                values.push_back(ratio(T[size_t(i)]));
                lowest = std::min(lowest, values.back());
            }
            if (lowest > before + 1e-9)
            {
                for (size_t q = 0; q < ids.size(); ++q) quality[size_t(ids[q])] = values[q];
                ++stats.moved;
            }
            else P[size_t(v)] = old;
        }
    }

    // ---------------------------------------------------------------- the tests of a flip of an edge of the surface
    static bool unit(const Vec3& u, Vec3& out)
    {
        const double length = u.norm();
        if (!(length > 0.0)) return false;
        out = u / length;
        return true;
    }

    // True when the two faces of the surface on the edge a-b (the one to c and the one to d) are one gentle piece of surface that the edge c-d can cross instead: their
    // normals within 15 degrees, a and b on the two sides of the line c-d, and the two faces on c-d turned no more than 25 degrees from the old ones
    bool flatQuad(int a, int b, int c, int d) const
    {
        const Vec3 pa = P[size_t(a)], pb = P[size_t(b)], pc = P[size_t(c)], pd = P[size_t(d)];
        const Vec3 ab = pb - pa;
        Vec3 n1, n2;
        if (!unit(ab.cross(pc - pa), n1) || !unit(ab.cross(pd - pa), n2)) return false;
        n1 = -n1;
        if (n1.dot(n2) < 0.9659) return false;
        Vec3 m;
        if (!unit(n1 + n2, m)) return false;
        const Vec3 cd = pd - pc;
        const double sideA = m.dot(cd.cross(pa - pc)), sideB = m.dot(cd.cross(pb - pc));
        if (!(sideA * sideB < 0.0)) return false;
        Vec3 u1, u2;
        if (!unit((pc - pa).cross(pd - pa), u1) || !unit((pd - pb).cross(pc - pb), u2)) return false;
        return std::abs(u1.dot(m)) >= 0.9063 && std::abs(u2.dot(m)) >= 0.9063;
    }

    // Netgen's test of a flip of an edge of the surface (improve2.cpp, EdgeSwapping): the quadrilateral a c b d is convex, and each of the two new triangles (a, c, d) and (b, c, d)
    // and each of the two old ones (a, b, c) and (a, b, d) is within 30 degrees of the surface's own normal -- the gradient of the field -- at the nodes c and d
    bool surfaceFlipOk(int a, int b, int c, int d)
    {
        const Vec3 pa = P[size_t(a)], pb = P[size_t(b)], pc = P[size_t(c)], pd = P[size_t(d)];
        const Vec3 ab = pb - pa;
        Vec3 nOld1, nOld2, mean;
        if (!unit((pc - pa).cross(ab), nOld1) || !unit(ab.cross(pd - pa), nOld2) || !unit(nOld1 + nOld2, mean)) return false;
        // H223 (the user, 2026-11-15, of the mesh of a plate with a hole: thin dark slivers along the rim; of the real bracket: all the worst triangles of the skin touch a node of a crease): at a node of a crease or a rim
        // the gradient of the field is the bisector of the two faces, 45 degrees off each of them, so the test below (the new triangles within 30 degrees of that normal at c and d) refused EVERY flip whose
        // opposite node is a crease node -- the flips next to the rims, where the slivers are.  Where the two old triangles are one flat piece of surface (their normals within 15 degrees) any flip across the
        // quadrilateral is harmless to the shape, and it is judged by flatQuad (convex, the new faces turned no more than 25 degrees); the field's normal is for the curved places only
        if (nOld1.dot(nOld2) >= 0.9659) return flatQuad(a, b, c, d);
        const Vec3 cd = pd - pc;
        if (!(mean.dot(cd.cross(pa - pc)) * mean.dot(cd.cross(pb - pc)) < 0.0)) return false;
        Vec3 nNew1, nNew2;
        if (!unit((pc - pa).cross(pd - pa), nNew1) || !unit((pd - pb).cross(pc - pb), nNew2)) return false;
        if (nNew1.dot(mean) < 0.0) nNew1 = -nNew1;
        if (nNew2.dot(mean) < 0.0) nNew2 = -nNew2;
        Vec3 gc, gd;
        if (!normalAt(c, gc) || !normalAt(d, gd)) return false;
        return nNew1.dot(gc) > 0.866 && nNew1.dot(gd) > 0.866 && nNew2.dot(gc) > 0.866 && nNew2.dot(gd) > 0.866 && nOld1.dot(gc) > 0.866 && nOld2.dot(gd) > 0.866;
    }

    std::vector<Vec3> surfaceNormal;                    // (the surface's own normal at a node of the surface, made when the flips start)
    std::vector<char> hasSurfaceNormal;

    bool normalAt(int v, Vec3& out) const
    {
        if (size_t(v) >= hasSurfaceNormal.size() || !hasSurfaceNormal[size_t(v)]) return false;
        out = surfaceNormal[size_t(v)];
        return true;
    }

    void makeSurfaceNormals()
    {
        surfaceNormal.assign(P.size(), Vec3::Zero());
        hasSurfaceNormal.assign(P.size(), 0);
        std::vector<int> nodes;
        std::vector<Vec3> pts;
        std::vector<double> scales;
        for (size_t v = 0; v < P.size(); ++v)
            if (onSurface[v] && !nodeTets[v].empty())
            {
                nodes.push_back(int(v));
                pts.push_back(P[v]);
                scales.push_back(h);
            }
        std::vector<double> val;
        std::vector<Vec3> grad;
        gradients(pts, scales, val, grad);
        for (size_t i = 0; i < nodes.size(); ++i)
        {
            const double length = grad[i].norm();
            if (length > 1e-12)
            {
                surfaceNormal[size_t(nodes[i])] = grad[i] / length;
                hasSurfaceNormal[size_t(nodes[i])] = 1;
            }
        }
    }

    // ---------------------------------------------------------------- the operations of the swaps
    // The best way to fill the ring round the edge a-b (an edge inside the mesh: three to eight tetrahedra; an edge of the surface: flipped, with `shape` to make its two triangles
    // better, the tetrahedra under it made again no poorer than 0.5)
    Change takeOutEdge(int a, int b, bool shape)
    {
        Change out;
        const std::vector<int> around = tetsOfEdge(a, b);
        if (around.empty()) return out;
        const bool surfaceEdge = boundaryEdges.count(edgeKey(a, b)) != 0;
        if (shape && !surfaceEdge) return out;
        if (surfaceEdge && !opt.surface) return out;
        int n = int(around.size());
        if (n < (surfaceEdge ? 2 : 3) || n > 8) return out;
        std::vector<std::pair<int, int>> link;
        for (int i : around)
        {
            int c = -1, d = -1;
            for (int v : T[size_t(i)])
                if (v != a && v != b) { if (c < 0) c = v; else d = v; }
            if (det6(a, b, c, d) < 0.0) std::swap(c, d);
            for (const auto& l : link)
                if (l.first == c) return out;
            link.emplace_back(c, d);
        }
        auto next = [&](int v) -> int {
            for (const auto& l : link)
                if (l.first == v) return l.second;
            return -1;
        };
        std::vector<int> ring;
        bool flipped = false;
        uint64_t goneEdge = 0, newEdge = 0;
        if (surfaceEdge)
        {
            std::vector<int> starts;
            for (const auto& l : link)
            {
                bool head = false;
                for (const auto& m : link)
                    if (m.second == l.first) { head = true; break; }
                if (!head) starts.push_back(l.first);
            }
            if (starts.size() != 1) return out;
            ring.push_back(starts[0]);
            while (next(ring.back()) >= 0 && int(ring.size()) <= n) ring.push_back(next(ring.back()));
            if (int(ring.size()) != n + 1 || next(ring.back()) >= 0) return out;
            const int c = ring.front(), d = ring.back();
            if (isHeld(a) || isHeld(b) || isHeld(c) || isHeld(d)) return out;      // (a face with a node on a mirror plane stays as it is)
            if (edgeExists(c, d) || owners(a, b, c) != 1 || owners(a, b, d) != 1) return out;
            if (!((shape && shape_) ? surfaceFlipOk(a, b, c, d) : flatQuad(a, b, c, d))) return out;
            if (shape && triBadness(a, c, d) + triBadness(b, c, d) >= triBadness(a, b, c) + triBadness(a, b, d) - 1e-6) return out;
            flipped = true;
            goneEdge = edgeKey(a, b);
            newEdge = edgeKey(c, d);
            n = int(ring.size());
        }
        else
        {
            const int start = link.front().first;
            ring.push_back(start);
            int v = link.front().second;
            while (v != start)
            {
                if (next(v) < 0 || int(ring.size()) > n) return out;
                ring.push_back(v);
                v = next(v);
            }
            if (int(ring.size()) != n) return out;
        }
        double oldQ = 1e30;
        for (int i : around) oldQ = std::min(oldQ, ratio(T[size_t(i)]));
        // the best triangulation of the ring: the dynamic program over the polygon
        double triQ[10][10][10];
        double bestQ[10][10];
        int bestK[10][10];
        bool done[10][10];
        for (int i = 0; i < 10; ++i)
            for (int j = 0; j < 10; ++j) { done[i][j] = false; for (int k = 0; k < 10; ++k) triQ[i][j][k] = -3.0; }
        auto triangle = [&](int i, int j, int k) -> double {
            double& q = triQ[i][j][k];
            if (q < -2.5)
            {
                const int x = ring[size_t(i)], y = ring[size_t(j)], z = ring[size_t(k)];
                q = std::min(ratio4(a, x, y, z), ratio4(b, z, y, x));
            }
            return q;
        };
        std::function<void(int, int)> fill = [&](int i, int j) {
            if (j - i < 2) { bestQ[i][j] = 2.0; bestK[i][j] = -1; done[i][j] = true; return; }
            if (done[i][j]) return;
            if (j - i > 1 && !(i == 0 && j == n - 1) && edgeExists(ring[size_t(i)], ring[size_t(j)])) { bestQ[i][j] = -1.0; bestK[i][j] = -1; done[i][j] = true; return; }
            double top = -1.0;
            int topK = -1;
            for (int k = i + 1; k < j; ++k)
            {
                fill(i, k);
                fill(k, j);
                const double q = std::min(triangle(i, k, j), std::min(bestQ[i][k], bestQ[k][j]));
                if (q > top) { top = q; topK = k; }
            }
            bestQ[i][j] = top;
            bestK[i][j] = topK;
            done[i][j] = true;
        };
        fill(0, n - 1);
        const double after = bestQ[0][n - 1];
        if (shape) { if (after <= 0.0 || after < std::min(oldQ, 0.5) - 1e-9) return out; }
        else if (after <= oldQ + 1e-9 || after <= 0.0) return out;
        std::vector<std::pair<int, int>> stack;
        stack.emplace_back(0, n - 1);
        while (!stack.empty())
        {
            const auto ij = stack.back();
            stack.pop_back();
            if (ij.second - ij.first < 2) continue;
            const int k = bestK[ij.first][ij.second];
            const int x = ring[size_t(ij.first)], y = ring[size_t(k)], z = ring[size_t(ij.second)];
            out.news.push_back(Tet{a, x, y, z});
            out.news.push_back(Tet{b, z, y, x});
            stack.emplace_back(ij.first, k);
            stack.emplace_back(k, ij.second);
        }
        out.ok = true;
        out.after = after;
        out.olds = around;
        if (flipped)
        {
            out.surfaceChanged = true;
            out.goneEdges.push_back(goneEdge);
            out.addedEdges.push_back(newEdge);
        }
        return out;
    }

    // The face a b c inside the mesh replaced by the three tetrahedra round the line between the apexes of the two that share it (the 2-3 swap)
    Change swapFace(int a, int b, int c)
    {
        Change out;
        std::vector<int> two;
        for (int i : nodeTets[size_t(a)])
            if (has(T[size_t(i)], b) && has(T[size_t(i)], c)) two.push_back(i);
        if (two.size() != 2) return out;
        int d = -1, e = -1;
        for (int v : T[size_t(two[0])]) if (v != a && v != b && v != c) d = v;
        for (int v : T[size_t(two[1])]) if (v != a && v != b && v != c) e = v;
        if (edgeExists(d, e)) return out;
        const int pairs[3][2] = {{a, b}, {b, c}, {c, a}};
        double vol[3];
        for (int k = 0; k < 3; ++k) vol[k] = det6(d, e, pairs[k][0], pairs[k][1]);
        const bool allPositive = vol[0] > 0.0 && vol[1] > 0.0 && vol[2] > 0.0;
        const bool allNegative = vol[0] < 0.0 && vol[1] < 0.0 && vol[2] < 0.0;
        if (!allPositive && !allNegative) return out;
        double after = 1e30;
        for (int k = 0; k < 3; ++k)
        {
            const Tet t = vol[k] > 0.0 ? Tet{d, e, pairs[k][0], pairs[k][1]} : Tet{d, e, pairs[k][1], pairs[k][0]};
            out.news.push_back(t);
            after = std::min(after, ratio(t));
        }
        if (after <= std::min(ratio(T[size_t(two[0])]), ratio(T[size_t(two[1])])) + 1e-9) { out.news.clear(); return out; }
        out.ok = true;
        out.after = after;
        out.olds = two;
        return out;
    }

    // The faces of the surface that a node is a corner of, as sorted triples
    std::set<Tri> surfaceFacesOf(int node) const
    {
        std::set<Tri> keys;
        for (int i : nodeTets[size_t(node)])
        {
            const Tet& t = T[size_t(i)];
            for (const auto& f : kFaces)
            {
                const int x = t[size_t(f[0])], y = t[size_t(f[1])], z = t[size_t(f[2])];
                if (x != node && y != node && z != node) continue;
                if (owners(x, y, z) == 1) keys.insert(sortedTri(x, y, z));
            }
        }
        return keys;
    }

    // True, with the edges of the surface that go and the ones it has instead, when the node d of the surface can be taken into its neighbour x on the surface without changing it
    bool surfaceCollapse(int d, int x, std::vector<uint64_t>& removed, std::vector<uint64_t>& added)
    {
        std::set<Tri> facesD = surfaceFacesOf(d), facesX = surfaceFacesOf(x);
        std::set<int> nearD, nearX, beside;
        for (const Tri& f : facesD)
        {
            for (int v : f) if (v != d && v != x) nearD.insert(v);
            if (f[0] == x || f[1] == x || f[2] == x)
                for (int v : f) if (v != d && v != x) beside.insert(v);
        }
        for (const Tri& f : facesX)
            for (int v : f) if (v != d && v != x) nearX.insert(v);
        std::set<int> common;
        std::set_intersection(nearD.begin(), nearD.end(), nearX.begin(), nearX.end(), std::inserter(common, common.begin()));
        if (beside.size() != 2 || common != beside) return false;
        const Vec3 pd = P[size_t(d)];
        double hLocal = (P[size_t(x)] - pd).norm();
        for (int u : nearD) hLocal = std::max(hLocal, (P[size_t(u)] - pd).norm());
        std::vector<std::pair<Tri, double>> faces;
        Vec3 nRef = Vec3::Zero();
        for (const Tri& key : facesD)
        {
            int owner = -1;
            for (int i : nodeTets[size_t(key[0])])
                if (has(T[size_t(i)], key[1]) && has(T[size_t(i)], key[2])) { owner = i; break; }
            int apex = -1;
            for (int v : T[size_t(owner)]) if (v != key[0] && v != key[1] && v != key[2]) apex = v;
            const Vec3 o0 = P[size_t(key[0])], o1 = P[size_t(key[1])], o2 = P[size_t(key[2])];
            const Vec3 nOld = (o1 - o0).cross(o2 - o0);
            const double s = nOld.dot(P[size_t(apex)] - o0) > 0.0 ? -1.0 : 1.0;
            nRef += s * nOld;
            faces.emplace_back(key, s);
        }
        std::vector<Vec3> centroids;
        for (const auto& fs : faces)
        {
            const Tri& key = fs.first;
            if (key[0] == x || key[1] == x || key[2] == x) continue;
            Vec3 nw[3];
            for (int q = 0; q < 3; ++q) nw[q] = key[size_t(q)] == d ? P[size_t(x)] : P[size_t(key[size_t(q)])];
            const Vec3 nNew = (nw[1] - nw[0]).cross(nw[2] - nw[0]);
            const double lNew = nNew.norm();
            if (lNew <= 0.0 || fs.second * nNew.dot(nRef) <= 0.0) return false;
            if (!shape_ || !opt.surface)
            {
                if (std::abs(nNew.dot(pd - nw[0])) / lNew > opt.surfaceTolerance * hLocal) return false;
            }
            else centroids.push_back((nw[0] + nw[1] + nw[2]) / 3.0);
        }
        if (!centroids.empty())
        {
            std::vector<double> val;
            std::vector<Vec3> grad;
            gradients(centroids, std::vector<double>(centroids.size(), hLocal), val, grad);
            for (size_t i = 0; i < centroids.size(); ++i)
            {
                const double gl = grad[i].norm();
                if (gl > 1e-12 && std::abs(val[i]) / gl > opt.surfaceTolerance * hLocal) return false;
            }
        }
        removed.clear();
        added.clear();
        for (int u : nearD) removed.push_back(edgeKey(d, u));
        removed.push_back(edgeKey(d, x));
        for (int u : nearD) added.push_back(edgeKey(x, u));
        return true;
    }

    // The node d taken into its neighbour x: allowed when the nodes that d and x have in common are the ones round the edge between them (so that no tetrahedra are pinched together)
    // and no tetrahedron is left without a volume; a node of the surface only into one on the surface, where that leaves the surface as it is
    Change collapse(int d, int x)
    {
        Change out;
        if (nodeTets[size_t(d)].empty()) return out;
        const bool surfaceNode = onSurface[size_t(d)] != 0;
        if (surfaceNode && (!opt.surface || isHeld(d) || isHeld(x) || !onSurface[size_t(x)] || nodeTets[size_t(x)].empty() || !boundaryEdges.count(edgeKey(d, x)))) return out;
        const std::vector<int> mine = nodeTets[size_t(d)];
        std::vector<int> gone;
        for (int i : mine) if (has(T[size_t(i)], x)) gone.push_back(i);
        if (gone.empty()) return out;
        std::set<int> ring, nearD, nearX;
        for (int i : gone) for (int v : T[size_t(i)]) ring.insert(v);
        ring.erase(d);
        ring.erase(x);
        for (int i : mine) for (int v : T[size_t(i)]) nearD.insert(v);
        for (int i : nodeTets[size_t(x)]) for (int v : T[size_t(i)]) nearX.insert(v);
        nearD.erase(d); nearD.erase(x); nearX.erase(d); nearX.erase(x);
        std::set<int> common;
        std::set_intersection(nearD.begin(), nearD.end(), nearX.begin(), nearX.end(), std::inserter(common, common.begin()));
        if (common != ring) return out;
        std::vector<Tet> news;
        for (int i : mine)
        {
            if (std::find(gone.begin(), gone.end(), i) != gone.end()) continue;
            Tet t = T[size_t(i)];
            for (int& v : t) if (v == d) v = x;
            news.push_back(t);
        }
        if (news.empty()) return out;
        if (surfaceNode && !surfaceCollapse(d, x, out.goneEdges, out.addedEdges)) { out.goneEdges.clear(); out.addedEdges.clear(); return out; }
        double after = 1e30, before = 1e30;
        for (const Tet& t : news) after = std::min(after, ratio(t));
        for (int i : mine) before = std::min(before, ratio(T[size_t(i)]));
        if (after <= before + 1e-9) { out.goneEdges.clear(); out.addedEdges.clear(); return out; }
        out.ok = true;
        out.after = after;
        out.news = news;
        out.olds = mine;
        out.surfaceChanged = surfaceNode;
        if (!surfaceNode) { out.goneEdges.clear(); out.addedEdges.clear(); }
        return out;
    }

    void apply(const Change& c, std::unordered_set<int>* touched)
    {
        for (uint64_t e : c.goneEdges) boundaryEdges.erase(e);
        for (uint64_t e : c.addedEdges) boundaryEdges.insert(e);
        for (int u : c.olds)
        {
            if (touched) for (int v : T[size_t(u)]) touched->insert(v);
            detach(u);
            T[size_t(u)][0] = -1;
        }
        for (const Tet& t : c.news)
        {
            T.push_back(t);
            attach(int(T.size()) - 1);
        }
    }

    // ---------------------------------------------------------------- the swaps (Netgen's SwapImprove, SwapImprove2, CombineImprove) and the flips of the surface
    void improve(bool surfaceFlips, int passes = 8)
    {
        const double limit = opt.poor;
        auto improveMark = std::chrono::steady_clock::now();
        refreshSurface();
        if (surfaceFlips && opt.surface)
        {
            // (Netgen makes the surface good before it fills the volume -- improve2.cpp, EdgeSwapping: an edge of the surface is flipped when its two triangles get better by their
            // badness -- and here the surface comes with the volume, so the tetrahedra under a flipped edge are made again with the new edge)
            // (H223, performance: the surface's normals at the nodes of the surface depend on where the nodes are, and a flip moves none: they are made once for the four rounds, not once for each --
            // the gradient of the field at 30,000 nodes was 2.6 s of the mender of the real bracket at 2 mm)
            makeSurfaceNormals();
            for (int round = 0; round < 4; ++round)
            {
                if (cancelled()) return;
                std::unordered_map<uint64_t, std::array<int, 3>> opposite;      // edge -> (count, first, second)
                for (const Tri& f : surfaceFacesNow())
                {
                    const int x = f[0], y = f[1], z = f[2];
                    const int edges[3][3] = {{x, y, z}, {y, z, x}, {z, x, y}};
                    for (const auto& e : edges)
                    {
                        auto& o = opposite[edgeKey(e[0], e[1])];
                        if (o[0] == 0) o = {1, e[2], 0};
                        else if (o[0] == 1) { o[2] = e[2]; o[0] = 2; }
                        else o[0] = 3;
                    }
                }
                std::vector<std::pair<double, uint64_t>> gains;
                for (const auto& kv : opposite)
                {
                    if (kv.second[0] != 2 || !boundaryEdges.count(kv.first)) continue;
                    const int a = int(kv.first >> 32), b = int(kv.first & 0xFFFFFFFFu);
                    const int c = kv.second[1], d = kv.second[2];
                    const double worst = std::max(triBadness(a, b, c), triBadness(a, b, d));
                    if (worst < 0.33) continue;     // (a smallest angle of 30 degrees: the triangle is left as it is -- Netgen flips all of them, but it has no volume to remake under every one)
                    const double gain = triBadness(a, b, c) + triBadness(a, b, d) - triBadness(a, c, d) - triBadness(b, c, d);
                    if (gain > 0.05) gains.emplace_back(gain, kv.first);
                }
                std::sort(gains.begin(), gains.end(), [](const std::pair<double, uint64_t>& x, const std::pair<double, uint64_t>& y) {
                    return x.first != y.first ? x.first > y.first : x.second < y.second; });
                size_t flipped = 0;
                for (const auto& g : gains)
                {
                    const int a = int(g.second >> 32), b = int(g.second & 0xFFFFFFFFu);
                    const Change c = takeOutEdge(a, b, true);
                    if (!c.ok) continue;
                    apply(c, nullptr);
                    ++flipped;
                }
                stats.flips += flipped;
                if (!flipped) break;
            }
        }
        {
            const auto now = std::chrono::steady_clock::now();
            secFlips += std::chrono::duration<double>(now - improveMark).count();
            improveMark = now;
        }
        std::unordered_set<int> touched;
        bool first = true;
        for (int pass = 0; pass < passes; ++pass)
        {
            if (cancelled()) return;
            std::vector<std::pair<double, int>> poor;
            for (size_t i = 0; i < T.size(); ++i)
            {
                if (!alive(int(i))) continue;
                if (!first)
                {
                    bool adjacent = false;
                    for (int v : T[i]) if (touched.count(v)) { adjacent = true; break; }
                    if (!adjacent) continue;
                }
                const double q = ratio(T[i]);
                if (q < limit) poor.emplace_back(q, int(i));
            }
            std::sort(poor.begin(), poor.end());
            touched.clear();
            first = false;
            size_t changed = 0;
            for (const auto& pi : poor)
            {
                const int i = pi.second;
                if (!alive(i)) continue;
                const Tet t = T[size_t(i)];
                if (ratio(t) >= limit) continue;
                Change best;
                for (const auto& p : kPairs)
                {
                    const Change c = takeOutEdge(t[size_t(p[0])], t[size_t(p[1])], false);
                    if (c.ok && (!best.ok || c.after > best.after)) best = c;
                }
                for (const auto& f : kFaces)
                {
                    const Change c = swapFace(t[size_t(f[0])], t[size_t(f[1])], t[size_t(f[2])]);
                    if (c.ok && (!best.ok || c.after > best.after)) best = c;
                }
                for (int d : t)
                    for (int x : t)
                    {
                        if (x == d) continue;
                        const Change c = collapse(d, x);
                        if (c.ok && (!best.ok || c.after > best.after)) best = c;
                    }
                if (!best.ok) continue;
                apply(best, &touched);
                ++changed;
            }
            stats.swaps += changed;
            if (!changed) break;
        }
    }

    // ---------------------------------------------------------------- the split of the edges of the worst tetrahedra (Netgen's SplitImprove)
    void split()
    {
        refreshSurface();
        std::vector<double> bad(T.size(), -1.0);
        auto badOf = [&](int i) -> double {
            if (size_t(i) >= bad.size()) bad.resize(T.size(), -1.0);
            double& b = bad[size_t(i)];
            if (b < 0.0) b = badness(T[size_t(i)]);
            return b;
        };
        // (the change of the sum of the badness, the place of the new node) of splitting the edge, or not ok
        struct Trial { bool ok = false; double delta = 0.0; Vec3 place = Vec3::Zero(); };
        auto trial = [&](int a, int b) -> Trial {
            Trial out;
            if (boundaryEdges.count(edgeKey(a, b))) return out;
            const std::vector<int> ring = tetsOfEdge(a, b);
            if (ring.empty()) return out;
            double worst = 0.0, old = 0.0;
            for (int i : ring) { const double v = badOf(i); worst = std::max(worst, v); old += v; }
            if (worst < opt.splitBadness) return out;
            const int m = int(P.size());                // (the number the new node would get: its place is the variable of the search)
            std::vector<Tet> news;
            for (int i : ring)
            {
                Tet t1 = T[size_t(i)], t2 = T[size_t(i)];
                for (int& v : t1) if (v == b) v = m;
                for (int& v : t2) if (v == a) v = m;
                news.push_back(t1);
                news.push_back(t2);
            }
            const Vec3 middle = 0.5 * (P[size_t(a)] + P[size_t(b)]);
            const Minimum r = minimizeNode(news, m, (P[size_t(a)] - P[size_t(b)]).norm(), middle);
            if (!r.ok || r.f >= old) return out;
            out.ok = true;
            out.delta = r.f - old;
            out.place = r.x;
            return out;
        };
        std::vector<std::pair<double, uint64_t>> candidates;
        std::unordered_set<uint64_t> seen;
        const size_t count = T.size();
        for (size_t i = 0; i < count; ++i)
        {
            if (!alive(int(i)) || badOf(int(i)) < opt.splitBadness) continue;
            const Tet t = T[i];
            for (const auto& p : kPairs)
            {
                const uint64_t e = edgeKey(t[size_t(p[0])], t[size_t(p[1])]);
                if (!seen.insert(e).second || boundaryEdges.count(e)) continue;
                const Trial r = trial(int(e >> 32), int(e & 0xFFFFFFFFu));
                if (r.ok) candidates.emplace_back(r.delta, e);
            }
        }
        std::sort(candidates.begin(), candidates.end());
        for (const auto& c : candidates)
        {
            const int a = int(c.second >> 32), b = int(c.second & 0xFFFFFFFFu);
            const Trial r = trial(a, b);        // (the ring may have changed since it was looked at)
            if (!r.ok) continue;
            const std::vector<int> ring = tetsOfEdge(a, b);
            const int m = int(P.size());
            P.push_back(r.place);
            nodeTets.emplace_back();
            if (!onSurface.empty()) onSurface.push_back(0);
            std::vector<Tet> news;
            for (int i : ring)
            {
                Tet t1 = T[size_t(i)], t2 = T[size_t(i)];
                for (int& v : t1) if (v == b) v = m;
                for (int& v : t2) if (v == a) v = m;
                news.push_back(t1);
                news.push_back(t2);
                detach(i);
                T[size_t(i)][0] = -1;
            }
            for (const Tet& t : news)
            {
                T.push_back(t);
                attach(int(T.size()) - 1);
            }
            ++stats.splits;
        }
    }
};

bool Mender::run()
{
    // (H223, performance: the seconds of each stage of the mender, printed with FIELDES_TET_DEBUG -- the mender was 6.8 s of the 14 s of the half mesh of the real bracket at 2 mm)
    const bool timing = std::getenv("FIELDES_TET_DEBUG") != nullptr;
    double secBoundary = 0.0, secSmooth = 0.0, secImprove = 0.0, secSplit = 0.0;
    auto mark = std::chrono::steady_clock::now();
    auto lap = [&](double& into) {
        const auto now = std::chrono::steady_clock::now();
        into += std::chrono::duration<double>(now - mark).count();
        mark = now;
    };
    for (int round = 0; round < opt.rounds; ++round)
    {
        if (cancelled()) return false;
        if (opt.surface && opt.smoothSweeps > 0)
        {
            refreshSurface();
            smoothBoundary();
        }
        lap(secBoundary);
        smooth(opt.smoothSweeps, opt.poor);
        lap(secSmooth);
        improve(true);
        lap(secImprove);
        if (cancelled()) return false;
        split();
        lap(secSplit);
        smooth(opt.smoothSweeps, opt.poor);
        lap(secSmooth);
    }
    smooth(opt.smoothSweeps, opt.poor);
    lap(secSmooth);
    if (timing)
        fprintf(stderr, "[mender] seconds: surface smoothing %.2f, node smoothing %.2f, swaps and collapses %.2f (flips of the surface %.2f), splits %.2f; %zu swaps, %zu flips, %zu splits, %zu nodes moved by the smoothing\n",
                secBoundary, secSmooth, secImprove, secFlips, secSplit, stats.swaps, stats.flips, stats.splits, stats.moved);
    return !cancelled();
}

}   // namespace

bool mendTets(TetMesh& mesh, const Tree* shape, const MendOptions& options, MendStats* stats)
{
    if (mesh.tets.empty()) return true;
    if (mesh.pos.size() >= size_t(2000000000)) return false;
    if (mesh.tets.size() > options.maxTets) return true;            // (too large to mend: left as it is)
    try
    {
        Mender mender(mesh, shape, options);
        if (!mender.run()) return false;
        mender.result(mesh);
        if (stats) *stats = mender.stats;
    }
    catch (const std::exception&)
    {
        return false;                                               // (out of memory, or anything else the mender cannot do: the mesh is as it was)
    }
    finalizeMesh(mesh);
    return true;
}

}   // namespace fea
}   // namespace libfive
