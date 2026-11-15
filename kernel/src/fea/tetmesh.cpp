/*
libfive: a CAD kernel for modeling with implicit functions

Tetrahedral meshing of an implicit shape by isosurface stuffing; see
tetmesh.hpp.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <iterator>
#include <memory>
#include <mutex>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include "libfive/fea/tetmesh.hpp"
#include "libfive/fea/tetmender.hpp"
#include "libfive/fea/fea.hpp"
#include "libfive/eval/eval_array.hpp"
#include "libfive/eval/eval_interval.hpp"
#include "libfive/run_progress.hpp"

namespace libfive {
namespace fea {

namespace {

// Whether the application has asked the run to stop (a newer edit, the red dot): the meshing of a part takes
// seconds on a fine element size, and the run must not go on with it -- the edit is waiting for it to end
inline bool stopped()
{
    return run_progress::cancelFlag().load(std::memory_order_relaxed);
}

using Vec3 = Eigen::Vector3d;
using Id = uint32_t;
using Tet4 = std::array<Id, 4>;

// The body-centred cubic lattice: the corners of nx * ny * nz cubes, then
// their centres.  A vertex's id is its index in that order.
struct Lattice
{
    int nx = 0, ny = 0, nz = 0;
    Vec3 origin = Vec3::Zero();
    Vec3 hv = Vec3::Ones();                 // the spacing along each axis (H202: fitted to the region, see meshShape; a cube of the lattice is a box of hv)
    size_t nL = 0, nC = 0;

    size_t corner(int i, int j, int k) const { return (size_t(k) * (ny + 1) + j) * (nx + 1) + i; }
    size_t centre(int i, int j, int k) const { return nL + (size_t(k) * ny + j) * nx + i; }
    size_t total() const { return nL + nC; }

    Vec3 pos(size_t id) const
    {
        if (id < nL)
        {
            const size_t i = id % (nx + 1), j = (id / (nx + 1)) % (ny + 1), k = id / (size_t(nx + 1) * (ny + 1));
            return origin + Vec3(hv.x() * double(i), hv.y() * double(j), hv.z() * double(k));
        }
        id -= nL;
        const size_t i = id % nx, j = (id / nx) % ny, k = id / (size_t(nx) * ny);
        return origin + Vec3(hv.x() * (double(i) + 0.5), hv.y() * (double(j) + 0.5), hv.z() * (double(k) + 0.5));
    }
};

// The tetrahedra of the lattice: for every pair of cubes that share a face,
// one for each of the four edges of that face -- the two cube centres and the
// edge's ends.  Each has two edges of length h (centre to centre, and the face
// edge) and four of length 0.87 h (centre to corner).
template <class F>
void forEachTet(const Lattice& L, F&& f)
{
    for (int k = 0; k < L.nz; ++k)
        for (int j = 0; j < L.ny; ++j)
            for (int i = 0; i < L.nx; ++i)
            {
                const size_t A = L.centre(i, j, k);
                for (int a = 0; a < 3; ++a)
                {
                    const int ni = i + (a == 0), nj = j + (a == 1), nk = k + (a == 2);
                    if (ni >= L.nx || nj >= L.ny || nk >= L.nz) continue;
                    const size_t B = L.centre(ni, nj, nk);
                    size_t c[4];
                    if (a == 0)
                    {
                        c[0] = L.corner(i + 1, j, k);     c[1] = L.corner(i + 1, j + 1, k);
                        c[2] = L.corner(i + 1, j + 1, k + 1); c[3] = L.corner(i + 1, j, k + 1);
                    }
                    else if (a == 1)
                    {
                        c[0] = L.corner(i, j + 1, k);     c[1] = L.corner(i, j + 1, k + 1);
                        c[2] = L.corner(i + 1, j + 1, k + 1); c[3] = L.corner(i + 1, j + 1, k);
                    }
                    else
                    {
                        c[0] = L.corner(i, j, k + 1);     c[1] = L.corner(i + 1, j, k + 1);
                        c[2] = L.corner(i + 1, j + 1, k + 1); c[3] = L.corner(i, j + 1, k + 1);
                    }
                    for (int m = 0; m < 4; ++m) f(Id(A), Id(B), Id(c[m]), Id(c[(m + 1) & 3]));
                }
            }
}

}   // anonymous namespace

namespace {

// H223 (the user, 2026-11-15: the extra 27 s of the analysis of the real bracket at 2 mm were "YOUR SLOW ASS CODE", and the field of the imported part is fast: measured 1.6 microseconds a point, at random points
// and near the surface alike): every call of evalTreePoints made an evaluator of the whole tree in each thread -- a few calls of the passes on the skin, each with a handful of points, paid for the evaluator
// again and again.  While a pass on one tree is running (an EvalScope on that tree), the evaluators that a call has made are kept and the next call takes them again
struct EvalCache
{
    std::mutex lock;
    std::unordered_map<const void*, std::vector<std::unique_ptr<ArrayEvaluator>>> idle;       // the evaluators that are not in use, by the tree
    std::unordered_map<const void*, int> open;                                                // the trees with an open scope
};

EvalCache& evalCache()
{
    static EvalCache cache;
    return cache;
}

struct EvalScope
{
    const void* key;
    explicit EvalScope(const Tree& tree) : key(tree.id())
    {
        std::lock_guard<std::mutex> g(evalCache().lock);
        ++evalCache().open[key];
    }
    ~EvalScope()
    {
        std::lock_guard<std::mutex> g(evalCache().lock);
        if (--evalCache().open[key] <= 0)
        {
            evalCache().open.erase(key);
            evalCache().idle.erase(key);
        }
    }
    EvalScope(const EvalScope&) = delete;
    EvalScope& operator=(const EvalScope&) = delete;
};

}   // anonymous namespace

// H225: the limits that a MeshOptions starts from while the user's choice (surface_fit) stands; see tetmesh.hpp.  The default is what the mesher has always done
MeshLimits& meshLimits()
{
    static MeshLimits limits;
    return limits;
}

// A tree's values at many points (on all cores)
void evalTreePoints(const Tree& tree, const std::vector<Eigen::Vector3f>& pts, std::vector<float>& out)
{
    out.resize(pts.size());
    const size_t N = ArrayEvaluator::N;
    const void* key = tree.id();
    bool keep = false;
    {
        std::lock_guard<std::mutex> g(evalCache().lock);
        keep = evalCache().open.count(key) != 0;
    }
    parallelRange((pts.size() + N - 1) / N, [&](size_t b0, size_t b1) {
        std::unique_ptr<ArrayEvaluator> e;
        if (keep)
        {
            std::lock_guard<std::mutex> g(evalCache().lock);
            auto& pool = evalCache().idle[key];
            if (!pool.empty())
            {
                e = std::move(pool.back());
                pool.pop_back();
            }
        }
        if (!e) e.reset(new ArrayEvaluator(tree));
        for (size_t b = b0; b < b1; ++b)
        {
            const size_t start = b * N;
            const size_t count = std::min(N, pts.size() - start);
            for (size_t k = 0; k < count; ++k) e->set(pts[start + k], k);
            const auto vs = e->values(count);
            for (size_t k = 0; k < count; ++k) out[start + k] = vs(k);
        }
        if (keep)
        {
            std::lock_guard<std::mutex> g(evalCache().lock);
            if (evalCache().open.count(key)) evalCache().idle[key].push_back(std::move(e));
        }
    }, 4);
}

namespace {

void evalPoints(const Tree& tree, const std::vector<Eigen::Vector3f>& pts, std::vector<float>& out)
{
    evalTreePoints(tree, pts, out);
}

struct CutEdge
{
    Id a, b;            // a < b
    double t;           // where the surface crosses it, from a (0) to b (1)
    Vec3 p;             // that point
};

uint64_t edgeKey(Id a, Id b)
{
    if (a > b) std::swap(a, b);
    return (uint64_t(a) << 32) | b;
}

double signedVolume(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d)
{
    return (b - a).cross(c - a).dot(d - a) / 6.0;
}

// The tetrahedra that are folded (two sharing a face lie on the same side of it) or slivers
// (a small fraction of the nominal volume, or a very flat dihedral angle), given their vertices'
// positions
template <class Pos>
void findBadTets(const std::vector<Tet4>& tets, Pos&& position, double h, std::vector<size_t>& bad, double minDihedral = 12.0, double minVolume = 0.004)
{
    bad.clear();
    struct Rec { Id a, b, c; uint32_t tet; uint8_t local; };
    std::vector<Rec> recs;
    recs.reserve(tets.size() * 4);
    const double nominal = h * h * h / 12.0;
    std::vector<char> isBad(tets.size(), 0);
    for (size_t e = 0; e < tets.size(); ++e)
    {
        const Vec3 p[4] = {position(tets[e][0]), position(tets[e][1]), position(tets[e][2]), position(tets[e][3])};
        if (std::abs(signedVolume(p[0], p[1], p[2], p[3])) < minVolume * nominal) isBad[e] = 1;
        else
        {
            // a very flat or very thin dihedral angle
            Vec3 nrm[4];
            bool ok = true;
            for (int i = 0; i < 4 && ok; ++i)
            {
                int f[3], m = 0;
                for (int k = 0; k < 4; ++k) if (k != i) f[m++] = k;
                Vec3 v = (p[f[1]] - p[f[0]]).cross(p[f[2]] - p[f[0]]);
                const double len = v.norm();
                if (!(len > 0)) { ok = false; break; }
                v /= len;
                if (v.dot(p[i] - p[f[0]]) > 0) v = -v;
                nrm[i] = v;
            }
            if (!ok) isBad[e] = 1;
            else
                for (int i = 0; i < 4 && !isBad[e]; ++i)
                    for (int j = i + 1; j < 4; ++j)
                    {
                        const double deg = 180.0 - std::acos(std::max(-1.0, std::min(1.0, nrm[i].dot(nrm[j])))) * 57.29577951308232;
                        if (deg < minDihedral || deg > 160.0) { isBad[e] = 1; break; }
                    }
        }
        for (int l = 0; l < 4; ++l)
        {
            Id f[3];
            int m = 0;
            for (int q = 0; q < 4; ++q) if (q != l) f[m++] = tets[e][size_t(q)];
            std::sort(f, f + 3);
            recs.push_back({f[0], f[1], f[2], uint32_t(e), uint8_t(l)});
        }
    }
    std::sort(recs.begin(), recs.end(), [](const Rec& x, const Rec& y) {
        if (x.a != y.a) return x.a < y.a;
        if (x.b != y.b) return x.b < y.b;
        return x.c < y.c;
    });
    for (size_t r = 0; r + 1 < recs.size(); ++r)
    {
        if (!(recs[r].a == recs[r + 1].a && recs[r].b == recs[r + 1].b && recs[r].c == recs[r + 1].c)) continue;
        const Vec3 f0 = position(recs[r].a), f1 = position(recs[r].b), f2 = position(recs[r].c);
        const Vec3 n = (f1 - f0).cross(f2 - f0);
        const Vec3 o1 = position(tets[recs[r].tet][recs[r].local]), o2 = position(tets[recs[r + 1].tet][recs[r + 1].local]);
        // (on the same side: folded)
        if (n.dot(o1 - f0) * n.dot(o2 - f0) > 0)
        {
            isBad[recs[r].tet] = 1;
            isBad[recs[r + 1].tet] = 1;
        }
    }
    for (size_t e = 0; e < tets.size(); ++e) if (isBad[e]) bad.push_back(e);
}

// The lattice of the region [lo, hi] at the spacing h asked for, fitted to it (H202); meshShape and probeSurface make theirs here, so that the probe looks at the very lattice the mesh is cut from
bool makeLattice(const Vec3& lo, const Vec3& hi, double h, bool fit, Lattice& L, std::string& error)
{
    const Vec3 size = hi - lo;
    // The element size is ADJUSTED TO FIT (H202, the user, 2026-11-13: "MAXIMUM CONSISTENCY over the entire mesh. If the mesh cannot fit the exact element size, then the element size are adjusted TO FIT
    // AS BEST AS POSSIBLE"): along each axis the spacing is the one nearest to h at which the region is a whole number of CUBES -- so that both faces of the region, which the caller sets to the extent
    // of the body, lie on the planes of the cube corners, and so do the edges and the corners of the body, on lines and at points of the lattice: a face at a plane that cuts the cells at an arbitrary
    // offset gave cut cells from a twenty-fourth of a regular tetrahedron's volume up (the cantilever beam of an analysis, at its element size 4: the largest tetrahedron 24 times the smallest,
    // 13 % of them under a fifth of the median), and a face on the plane of the cube centres, with its neighbour on a plane of corners, left the edge between them between lattice vertices, where the
    // lattice cut a notch of half an element out of it (measured on the first version of this fit, which made the region a whole number of half cubes: the beam lost 0.67 % of its volume to notches
    // at the seams of its mirror planes and none of the four long edges had a vertex there).  Fitted so, the spread is the half-size cells that the boundary of any lattice has (2 to 1).  The spacing
    // moves by up to a quarter of h where the region is a few cells across, by far less in a larger one; an axis along which the region is thinner than 0.6 h is not fitted
    Vec3 hv(h, h, h);
    if (fit)
        for (int a = 0; a < 3; ++a)
        {
            // (the whole number of cubes that is nearest to the size asked for, but never one that makes the elements more than a twentieth larger than asked: the mesh is as fine as was asked or
            // finer, never coarser.  A beam 1.35 elements thick is two cubes of 0.68, not one of 1.35 -- the known answer of the 10-node cantilever, v1, went 49 % off the beam theory with one
            // cube through the thickness -- and a plate 2.14 elements thick is three cubes of 0.5 mm, not two of 0.75: the 10-node plate with a hole, v2, was 4.3 % off Peterson's concentration
            // with two layers and 0.4 % with three)
            const double up = std::max(1.0, std::ceil(size[a] / h - 1e-9));
            const double down = up - 1.0;
            double cubes = up;
            if (down >= 1.0 && size[a] / down <= 1.05 * h && std::abs(size[a] / down - h) < std::abs(size[a] / up - h)) cubes = down;
            // at least two cubes: a region one cube across -- the octant of a 10 mm beam at element size 5, between its middle planes and its faces -- left a tunnel along the axis where the planes of
            // symmetry meet (the mesh of the 10-node cantilever, v1, filled 9,650 of 10,000 mm^3 and its tip went 49 % off the beam theory; the debug output said "mirror about 3 planes does not hold")
            cubes = std::max(2.0, cubes);
            const double fitted = size[a] / cubes;
            if (fitted >= 0.5 * h) hv[a] = fitted;
        }
    L.hv = hv;
    int n[3];
    double cells = 1;
    for (int a = 0; a < 3; ++a)
    {
        n[a] = std::max(3, int(std::ceil(size[a] / hv[a] - 1e-9)) + 2);
        cells *= n[a];
    }
    // (one cube of padding on every side: the lattice planes coincide with lo + k hv)
    if (cells > 80e6)
    {
        error = "too many elements: use a larger element size";
        return false;
    }
    L.nx = n[0];
    L.ny = n[1];
    L.nz = n[2];
    L.origin = lo - hv;
    L.nL = size_t(L.nx + 1) * (L.ny + 1) * (L.nz + 1);
    L.nC = size_t(L.nx) * L.ny * L.nz;
    return true;
}

// The shape's field at every vertex of the lattice, in blocks (false when the run was stopped)
bool latticeValues(const Tree& shape, const Lattice& L, std::vector<float>& val)
{
    const size_t nTotal = L.total();
    val.assign(nTotal, 0.0f);
    const size_t block = size_t(1) << 21;
    std::vector<Eigen::Vector3f> pts;
    std::vector<float> v;
    for (size_t start = 0; start < nTotal; start += block)
    {
        const size_t count = std::min(block, nTotal - start);
        pts.resize(count);
        for (size_t q = 0; q < count; ++q) pts[q] = L.pos(start + q).cast<float>();
        evalPoints(shape, pts, v);
        if (stopped()) return false;
        std::copy(v.begin(), v.end(), val.begin() + std::ptrdiff_t(start));
    }
    return true;
}

// H223 (the user, 2026-11-14, of the real example 05: "THE MESH IS FUCKED UP NOW" -- the rims of the bores of the bracket were sawtoothed): the nodes on the RIM of a flat face put on it.  A port of meshing.py _snap_rims, which
// mesh_tet4 had and the mesh of an analysis did not (measured on the bracket at 4 mm: 60 of the 62 rim nodes of four bores on the rim with mesh_tet4, 2 with the analysis, the rest up to 0.6 of an element off it, in a
// sawtooth; the 1.2.1 library the same).  The field is zero all over a flat face (it is the maximum of the face's and the wall's), so the distance of a node on the face to the rim cannot be read AT the node: a node that
// has a group of faces on one plane and others that turn away from it by more than 30 degrees is moved along that plane -- the field read a quarter of an element inside the body, where it is the wall's alone -- by Newton
// steps onto the wall, and kept there when it ended on the surface and no tetrahedron round it is turned inside out or made poorer than a mean ratio of 0.3 (or than it was).  A node on a plane the mesh is mirrored about stays
size_t snapRims(TetMesh& m, const Tree& shape, const MeshOptions& opt)
{
    const double kPi = 3.14159265358979323846;
    const double cosCrease = std::cos(30.0 * kPi / 180.0);
    const double floorRatio = opt.rimFloor;
    const size_t nV = m.pos.size();
    if (m.faces.empty() || nV == 0) return 0;
    const int kPairs[6][2] = {{0, 1}, {0, 2}, {0, 3}, {1, 2}, {1, 3}, {2, 3}};
    // the tetrahedra round each node
    std::vector<std::vector<int>> nodeTets(nV);
    for (size_t i = 0; i < m.tets.size(); ++i)
        for (int v : m.tets[i]) nodeTets[size_t(v)].push_back(int(i));
    // the mean ratio of a tetrahedron, 12 (3 V)^(2/3) over the sum of its squared edges: 1 for a regular one, -1 for one that is inside out
    auto ratio = [&](const std::array<int, 4>& t) {
        const double v = (m.pos[size_t(t[1])] - m.pos[size_t(t[0])]).cross(m.pos[size_t(t[2])] - m.pos[size_t(t[0])]).dot(m.pos[size_t(t[3])] - m.pos[size_t(t[0])]) / 6.0;
        if (v <= 0.0) return -1.0;
        double s = 0.0;
        for (const auto& p : kPairs) s += (m.pos[size_t(t[size_t(p[0])])] - m.pos[size_t(t[size_t(p[1])])]).squaredNorm();
        return 12.0 * std::pow(3.0 * v, 2.0 / 3.0) / s;
    };
    auto minRatio = [&](int v) {
        double r = 1.0;
        for (int t : nodeTets[size_t(v)]) r = std::min(r, ratio(m.tets[size_t(t)]));
        return r;
    };
    const double holdTol = 2e-3 * m.h;
    auto held = [&](size_t v) {
        for (const auto& pl : opt.holdPlanes)
            if (std::abs(m.pos[v][pl.first] - pl.second) <= holdTol) return true;
        return false;
    };
    // (a node is never put ON a plane the mesh is mirrored about: the half mesh keeps its planes' nodes where meshShapeMirrored puts them)
    auto onPlane = [&](const Vec3& p) {
        for (const auto& pl : opt.holdPlanes)
            if (std::abs(p[pl.first] - pl.second) <= holdTol) return true;
        return false;
    };
    size_t movedTotal = 0;
    for (int round = 0; round < 3; ++round)
    {
        if (stopped()) break;
        // the unit normal of every boundary face, once; the normals round every node; the mean length of the boundary edges at every node
        std::vector<std::vector<Vec3>> normalsOf(nV);
        std::vector<double> lengthSum(nV, 0.0);
        std::vector<int> lengthCount(nV, 0);
        for (const auto& f : m.faces)
        {
            const Vec3 n = (m.pos[size_t(f[1])] - m.pos[size_t(f[0])]).cross(m.pos[size_t(f[2])] - m.pos[size_t(f[0])]);
            const double size = n.norm();
            for (int q = 0; q < 3; ++q)
            {
                const size_t a = size_t(f[size_t(q)]), b = size_t(f[size_t((q + 1) % 3)]);
                lengthSum[a] += (m.pos[a] - m.pos[b]).norm();
                lengthCount[a] += 1;
                if (size > 0.0) normalsOf[a].push_back(n / size);
            }
        }
        // (H226: a node on a plane the mesh is mirrored about is moved too, but only along the plane: pinA and pinB are the unit directions across the planes it is on, which the Newton steps leave out)
        struct Want { int v; Vec3 plane; double length; Vec3 pinA; Vec3 pinB; };
        std::vector<Want> wanted;
        for (size_t v = 0; v < nV; ++v)
        {
            const auto& normals = normalsOf[v];
            if (normals.size() < 3 || lengthCount[v] == 0 || held(v)) continue;
            // the largest group of faces on one plane (their normals within 0.995 of each other)
            size_t bestCount = 0;
            Vec3 bestPlane = Vec3::Zero();
            for (const Vec3& n : normals)
            {
                Vec3 sum = Vec3::Zero();
                size_t count = 0;
                for (const Vec3& o : normals)
                    if (n.dot(o) > 0.995) { sum += o; ++count; }
                if (count >= 2 && count > bestCount && sum.norm() > 1e-12)
                {
                    bestCount = count;
                    bestPlane = sum.normalized();
                }
            }
            if (bestCount == 0) continue;
            bool others = false;
            for (const Vec3& o : normals) others = others || bestPlane.dot(o) < cosCrease;
            if (!others) continue;
            Vec3 pinA = Vec3::Zero(), pinB = Vec3::Zero();
            for (const auto& pl : opt.holdPlanes)
                if (std::abs(m.pos[v][pl.first] - pl.second) <= holdTol)
                {
                    Vec3 axis = Vec3::Zero();
                    axis[pl.first] = 1.0;
                    if (pinA.squaredNorm() == 0.0) pinA = axis;
                    else pinB = axis;
                }
            wanted.push_back({int(v), bestPlane, lengthSum[v] / lengthCount[v], pinA, pinB});
        }
        if (wanted.empty()) break;
        // the Newton steps of all the nodes together, a call of the field for each round of them
        std::vector<Vec3> current(wanted.size());
        for (size_t k = 0; k < wanted.size(); ++k) current[k] = m.pos[size_t(wanted[k].v)];
        std::vector<size_t> live(wanted.size());
        for (size_t k = 0; k < live.size(); ++k) live[k] = k;
        for (int it = 0; it < 4 && !live.empty(); ++it)
        {
            std::vector<Eigen::Vector3f> pts;
            pts.reserve(7 * live.size());
            std::vector<double> steps(live.size());
            for (size_t j = 0; j < live.size(); ++j)
            {
                const Want& w = wanted[live[j]];
                const Vec3 asked = current[live[j]] - 0.25 * w.length * w.plane;       // (a little way inside the body: there the field is the wall's)
                const double e = std::max(1e-7, 0.02 * w.length);
                steps[j] = e;
                pts.push_back(asked.cast<float>());
                for (int a = 0; a < 3; ++a)
                {
                    Vec3 lo3 = asked, hi3 = asked;
                    lo3[a] += e;       // (the order of the python probe: +e, -e)
                    hi3[a] -= e;
                    pts.push_back(lo3.cast<float>());
                    pts.push_back(hi3.cast<float>());
                }
            }
            std::vector<float> v;
            evalPoints(shape, pts, v);
            if (stopped()) return movedTotal;
            std::vector<size_t> still;
            for (size_t j = 0; j < live.size(); ++j)
            {
                const Want& w = wanted[live[j]];
                const double e = steps[j];
                const double value = double(v[7 * j]);
                Vec3 g;
                for (int a = 0; a < 3; ++a) g[a] = (double(v[7 * j + size_t(1 + 2 * a)]) - double(v[7 * j + size_t(2 + 2 * a)])) / (2 * e);
                Vec3 gp = g - g.dot(w.plane) * w.plane;
                if (w.pinA.squaredNorm() > 0.0) gp -= gp.dot(w.pinA) * w.pinA;          // (a node on a plane of the mirror moves along that plane only)
                if (w.pinB.squaredNorm() > 0.0) gp -= gp.dot(w.pinB) * w.pinB;
                const double g2 = gp.squaredNorm();
                if (g2 < 1e-12 || std::abs(value) > 0.25 * w.length) continue;          // (no wall there to go to: the flat face's own field)
                Vec3 step = value * gp / g2;
                const double size = step.norm();
                if (size > 0.5 * w.length) step *= 0.5 * w.length / size;
                current[live[j]] -= step;
                if (size >= 1e-4 * w.length) still.push_back(live[j]);
            }
            live.swap(still);
        }
        // where they ended: only a node that ended ON the surface is moved
        std::vector<Eigen::Vector3f> ends(wanted.size());
        for (size_t k = 0; k < wanted.size(); ++k) ends[k] = current[k].cast<float>();
        std::vector<float> endValue;
        evalPoints(shape, ends, endValue);
        if (stopped()) return movedTotal;
        std::vector<std::pair<int, Vec3>> goals;
        for (size_t k = 0; k < wanted.size(); ++k)
        {
            const Want& w = wanted[k];
            if ((current[k] - m.pos[size_t(w.v)]).norm() > 1e-4 * w.length && std::abs(double(endValue[k])) < 0.01 * w.length && (held(size_t(w.v)) || !onPlane(current[k]))) goals.emplace_back(w.v, current[k]);
        }
        if (goals.empty()) break;
        size_t moved = 0;
        for (const auto& g : goals)
        {
            const size_t v = size_t(g.first);
            const Vec3 old = m.pos[v];
            const double before = minRatio(g.first);
            bool done = false;
            for (const double share : {1.0, 0.5, 0.25})
            {
                m.pos[v] = old + share * (g.second - old);
                const double now = minRatio(g.first);
                if (now > 0.0 && now >= std::min(floorRatio, before))
                {
                    done = true;
                    break;
                }
            }
            if (done) ++moved;
            else m.pos[v] = old;
        }
        movedTotal += moved;
        if (moved == 0) break;
    }
    return movedTotal;
}

// The value and the gradient of the shape's field at each of the points, by central differences of 2 % of the scale there (meshing.py _field_gradients: the samples are the point, then +e and -e along x, y and z)
struct FieldAt
{
    double value;
    Vec3 g;
};

std::vector<FieldAt> fieldAt(const Tree& shape, const std::vector<Vec3>& points, const std::vector<double>& scales)
{
    std::vector<Eigen::Vector3f> pts;
    pts.reserve(7 * points.size());
    std::vector<double> steps(points.size());
    for (size_t i = 0; i < points.size(); ++i)
    {
        const double e = std::max(1e-7, 0.02 * scales[i]);
        steps[i] = e;
        pts.push_back(points[i].cast<float>());
        for (int a = 0; a < 3; ++a)
        {
            Vec3 up = points[i], down = points[i];
            up[a] += e;
            down[a] -= e;
            pts.push_back(up.cast<float>());
            pts.push_back(down.cast<float>());
        }
    }
    std::vector<float> v;
    evalPoints(shape, pts, v);
    std::vector<FieldAt> out(points.size());
    for (size_t i = 0; i < points.size(); ++i)
    {
        out[i].value = double(v[7 * i]);
        for (int a = 0; a < 3; ++a)
            out[i].g[a] = (double(v[7 * i + size_t(1 + 2 * a)]) - double(v[7 * i + size_t(2 + 2 * a)])) / (2.0 * steps[i]);
    }
    return out;
}

// The points moved onto the surface of the field (its value 0) by Newton steps along its gradient, no further than half of `scales` at a time (meshing.py _snap_to_surface)
std::vector<Vec3> snapToSurface(const Tree& shape, std::vector<Vec3> cur, const std::vector<double>& scales, int iterations)
{
    for (int it = 0; it < iterations; ++it)
    {
        if (stopped()) break;
        const std::vector<FieldAt> at = fieldAt(shape, cur, scales);
        for (size_t i = 0; i < cur.size(); ++i)
        {
            const double g2 = at[i].g.squaredNorm();
            if (g2 < 1e-24) continue;
            Vec3 d = (at[i].value / g2) * at[i].g;
            const double length = d.norm();
            const double limit = 0.5 * scales[i];
            if (length > limit) d *= limit / length;
            cur[i] -= d;
        }
    }
    return cur;
}

double det3(const double (&m)[3][3])
{
    return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
         + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
}

// x of A x = b for the 3 x 3 matrix whose rows are `rows` (Cramer's rule); false when there is no single answer (meshing.py _solve3)
bool solve3(const Vec3* rows, const double* b, Vec3& out)
{
    double A[3][3];
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) A[r][c] = rows[r][c];
    const double d = det3(A);
    if (std::abs(d) < 1e-18) return false;
    for (int k = 0; k < 3; ++k)
    {
        double M[3][3];
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c) M[r][c] = (c == k) ? b[r] : A[r][c];
        out[k] = det3(M) / d;
    }
    return true;
}

// The point of the line where the planes (nA, dA) and (nB, dB) meet that is nearest to p; false when they are one plane (meshing.py _two_planes)
bool twoPlanes(const Vec3& p, const Vec3& nA, double dA, const Vec3& nB, double dB, Vec3& out)
{
    const double c = nA.dot(nB);
    const double det = 1.0 - c * c;
    if (det < 1e-6) return false;
    const double rA = dA - nA.dot(p), rB = dB - nB.dot(p);
    const double a = (rA - c * rB) / det, b = (rB - c * rA) / det;
    out = p + a * nA + b * nB;
    return true;
}

// The mean ratio of a tetrahedron, 12 (3 V)^(2/3) over the sum of its squared edges: 1 for a regular one, -1 for one that is inside out (meshing.py _tet_ratio)
double meanRatio(const std::vector<Vec3>& pos, const std::array<int, 4>& t)
{
    const Vec3& a = pos[size_t(t[0])];
    const double v = (pos[size_t(t[1])] - a).cross(pos[size_t(t[2])] - a).dot(pos[size_t(t[3])] - a) / 6.0;
    if (v <= 0.0) return -1.0;
    static const int pairs[6][2] = {{0, 1}, {0, 2}, {0, 3}, {1, 2}, {1, 3}, {2, 3}};
    double s = 0.0;
    for (const auto& p : pairs) s += (pos[size_t(t[size_t(p[0])])] - pos[size_t(t[size_t(p[1])])]).squaredNorm();
    return 12.0 * std::pow(3.0 * v, 2.0 / 3.0) / s;
}

// The smallest dihedral angle of a tetrahedron in degrees (meshing.py _tet_min_dihedral)
double minDihedral(const std::vector<Vec3>& pos, const std::array<int, 4>& t)
{
    static const int pairs[6][2] = {{0, 1}, {0, 2}, {0, 3}, {1, 2}, {1, 3}, {2, 3}};
    double best = 180.0;
    for (const auto& pr : pairs)
    {
        int c = -1, d = -1;
        for (int x = 0; x < 4; ++x)
            if (x != pr[0] && x != pr[1])
            {
                if (c < 0) c = x;
                else d = x;
            }
        const Vec3& ia = pos[size_t(t[size_t(pr[0])])];
        Vec3 e = pos[size_t(t[size_t(pr[1])])] - ia;
        const double el = e.norm();
        if (el == 0.0) return 0.0;
        e /= el;
        Vec3 normals[2];
        for (int k = 0; k < 2; ++k)
        {
            Vec3 w = pos[size_t(t[size_t(k == 0 ? c : d)])] - ia;
            w -= w.dot(e) * e;
            const double pl = w.norm();
            if (pl == 0.0) return 0.0;
            normals[k] = w / pl;
        }
        const double cs = std::max(-1.0, std::min(1.0, normals[0].dot(normals[1])));
        best = std::min(best, std::acos(cs) * 57.29577951308232);
    }
    return best;
}

// H223 / H226 (the user, 2026-11-15, zooming into my picture of the optimal beam of validation/v11: "WHERE THE MESH IS FUCKED UP" -- a plain box 8 x 14 x 100 mm, meshed at 4 mm, has a jog in each of its four long edges in the
// middle, where they cross the plane that the analysis mesh is mirrored about): a STRAIGHT EDGE of the body that crosses a plane of the mirror has no node at the crossing.  snapFeatures leaves the nodes ON a plane alone, so
// the plane's nodes stay where the lattice cut them, on one of the two faces beside the edge, the corner of the section is cut off and the faces round it lean.  The node of the plane nearest to such a corner is moved to
// it: the faces round the node that do not lie in the plane make two groups of normals that differ by more than 40 degrees; the planes of the two groups and the plane of the mirror meet in one point, and that point is taken
// when it lies on the surface (the field there under a thousandth of an element over its gradient: a straight edge -- the corner of a bent rim is not, and the passes for rims have put those nodes) and within 0.7 of a mean edge
// length of the node, and no tetrahedron round the node is turned inside out or made poorer than the floor (or than it was).  Only nodes on ONE plane, and only the nearest node to each point.
size_t snapPlaneEdges(TetMesh& m, const Tree& shape, const MeshOptions& opt)
{
    if (opt.holdPlanes.empty() || m.faces.empty() || m.pos.empty()) return 0;
    const double kPi = 3.14159265358979323846;
    const double cosCrease = std::cos(40.0 * kPi / 180.0);
    const size_t nV = m.pos.size();
    const double holdTol = 2e-3 * m.h;
    // the plane a node is on: its place in holdPlanes, -1 when it is on none, -2 when it is on two or more
    auto planeOf = [&](const Vec3& p) {
        int which = -1;
        for (size_t k = 0; k < opt.holdPlanes.size(); ++k)
            if (std::abs(p[opt.holdPlanes[k].first] - opt.holdPlanes[k].second) <= holdTol) which = (which == -1) ? int(k) : -2;
        return which;
    };
    std::vector<int> plane(nV, -1);
    for (size_t v = 0; v < nV; ++v) plane[v] = planeOf(m.pos[v]);
    std::vector<std::vector<int>> nodeTets(nV);
    for (size_t i = 0; i < m.tets.size(); ++i)
        for (int v : m.tets[i]) nodeTets[size_t(v)].push_back(int(i));
    auto minRatio = [&](int v) {
        double r = 1.0;
        for (int t : nodeTets[size_t(v)]) r = std::min(r, meanRatio(m.pos, m.tets[size_t(t)]));
        return r;
    };
    // the faces round each node that do not lie in a plane of the mirror (normal, middle, area), and the mean length of the boundary edges at the node
    struct Round { Vec3 n; Vec3 c; double w; size_t idx; };
    std::vector<std::vector<Round>> around(nV);
    std::vector<std::vector<int>> beside(nV);          // (the nodes that share a boundary face with a node)
    std::vector<double> lengthSum(nV, 0.0);
    std::vector<int> lengthCount(nV, 0);
    for (size_t fi = 0; fi < m.faces.size(); ++fi)
    {
        const auto& f = m.faces[fi];
        for (int q = 0; q < 3; ++q) beside[size_t(f[size_t(q)])].push_back(f[size_t((q + 1) % 3)]), beside[size_t(f[size_t(q)])].push_back(f[size_t((q + 2) % 3)]);
        const Vec3 cross = (m.pos[size_t(f[1])] - m.pos[size_t(f[0])]).cross(m.pos[size_t(f[2])] - m.pos[size_t(f[0])]);
        const double size = cross.norm();
        if (size <= 0.0) continue;
        bool inPlane = false;
        for (const auto& pl : opt.holdPlanes)
        {
            bool all = true;
            for (int q = 0; q < 3; ++q) all = all && std::abs(m.pos[size_t(f[size_t(q)])][pl.first] - pl.second) <= holdTol;
            inPlane = inPlane || all;
        }
        const Vec3 c = (m.pos[size_t(f[0])] + m.pos[size_t(f[1])] + m.pos[size_t(f[2])]) / 3.0;
        for (int q = 0; q < 3; ++q)
        {
            const size_t a = size_t(f[size_t(q)]), b = size_t(f[size_t((q + 1) % 3)]);
            if (!inPlane) lengthSum[a] += (m.pos[a] - m.pos[b]).norm(), lengthCount[a] += 1;
            if (!inPlane) around[a].push_back({Vec3(cross / size), c, 0.5 * size, fi});
        }
    }
    struct Goal { int v; Vec3 x; double dist; double length; };
    std::vector<Goal> goals;
    size_t candidates = 0;
    // THE TRUE FACES (second version, read from the debug lines on the beam of v11): the planes of the two faces of an edge come from the NODES round a node and the field's own normal at each of them -- a node that stands
    // on a face has the face's normal there --, not from the normals of the triangles: near a missing corner the triangles are bevels (the top face of the beam read as tilted by 6 degrees and 0.26 mm low at the plane, and the
    // corner was found on the bevel's plane, not on the face's).  A node on an edge has the normals of two faces blended, its gradient is shorter (0.71 of a flat node's at a right angle): only the nodes whose gradient is
    // nearly the longest of the ring are taken.
    std::vector<std::vector<int>> ringOf(nV);
    std::vector<int> need;
    {
        std::vector<char> wanted(nV, 0);
        for (size_t v = 0; v < nV; ++v)
        {
            if (plane[v] < 0 || around[v].size() < 2 || lengthCount[v] == 0) continue;
            std::vector<int> ids = beside[v];
            for (int u : beside[v])
                for (int w : beside[size_t(u)]) ids.push_back(w);
            ids.push_back(int(v));
            std::sort(ids.begin(), ids.end());
            ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
            ringOf[v] = ids;
            for (int u : ids) wanted[size_t(u)] = 1;
        }
        for (size_t u = 0; u < nV; ++u)
            if (wanted[u]) need.push_back(int(u));
    }
    std::vector<FieldAt> nodeField(nV);
    std::vector<double> nodeLength(nV, m.h);
    if (!need.empty())
    {
        std::vector<Vec3> pts;
        std::vector<double> scl;
        for (int u : need)
        {
            const double l = lengthCount[size_t(u)] ? lengthSum[size_t(u)] / lengthCount[size_t(u)] : m.h;
            nodeLength[size_t(u)] = l;
            pts.push_back(m.pos[size_t(u)]);
            scl.push_back(l);
        }
        const std::vector<FieldAt> got = fieldAt(shape, pts, scl);
        if (stopped()) return 0;
        for (size_t k = 0; k < need.size(); ++k) nodeField[size_t(need[k])] = got[k];
    }
    for (size_t v = 0; v < nV; ++v)
    {
        if (ringOf[v].empty()) continue;
        const double length = lengthSum[v] / lengthCount[v];
        double gmax = 0.0;
        for (int u : ringOf[v]) gmax = std::max(gmax, nodeField[size_t(u)].g.norm());
        if (gmax <= 1e-12) continue;
        struct Group { Vec3 n; Vec3 p; int count; };
        std::vector<Group> groups;
        for (int u : ringOf[v])
        {
            const FieldAt& fa = nodeField[size_t(u)];
            const double gl = fa.g.norm();
            if (gl < 0.9 * gmax || std::abs(fa.value) / gl > 0.05 * nodeLength[size_t(u)]) continue;          // (a node on an edge, or one that is off the surface: no face's normal)
            if ((m.pos[size_t(u)] - m.pos[v]).norm() > 1.5 * length) continue;          // (the faces of the edge here, not the opposite faces of a part two elements wide)
            const Vec3 nu = fa.g / gl;
            // (the half mesh is meshed against the plane of the mirror as a wall, so a node ON the plane has the plane's normal there, not the face's: the plane is the third face of the corner, not one of the two of the edge)
            if (std::abs(nu[opt.holdPlanes[size_t(plane[v])].first]) > 0.9) continue;
            bool placed = false;
            for (auto& g : groups)
            {
                const double len = g.n.norm();
                if (len > 1e-12 && g.n.dot(nu) / len >= cosCrease)
                {
                    g.n += nu;
                    g.p += m.pos[size_t(u)];
                    g.count += 1;
                    placed = true;
                    break;
                }
            }
            if (!placed) groups.push_back({nu, m.pos[size_t(u)], 1});
        }
        std::stable_sort(groups.begin(), groups.end(), [](const Group& a, const Group& b) { return a.count > b.count; });
        // (the two faces of an edge: the pair of groups with the most nodes whose normals are more than 40 degrees and less than 140 degrees apart)
        int gi = -1, gj = -1;
        for (size_t i = 0; i < groups.size() && gi < 0; ++i)
            for (size_t j = i + 1; j < groups.size(); ++j)
                if (groups[i].count >= 1 && groups[j].count >= 1 && std::abs(groups[i].n.normalized().dot(groups[j].n.normalized())) < cosCrease)
                {
                    gi = int(i);
                    gj = int(j);
                    break;
                }
        if (gi < 0) continue;
        const Vec3 n1 = groups[size_t(gi)].n.normalized(), n2 = groups[size_t(gj)].n.normalized();
        ++candidates;
        Vec3 axis = Vec3::Zero();
        axis[opt.holdPlanes[size_t(plane[v])].first] = 1.0;
        const Vec3 rows[3] = {n1, n2, axis};
        const double rhs[3] = {n1.dot(groups[size_t(gi)].p / double(groups[size_t(gi)].count)), n2.dot(groups[size_t(gj)].p / double(groups[size_t(gj)].count)), opt.holdPlanes[size_t(plane[v])].second};
        Vec3 x = Vec3::Zero();
        if (!solve3(rows, rhs, x))
        {
            if (std::getenv("FIELDES_TET_DEBUG")) fprintf(stderr, "[snapPlaneEdges]    node %zu (%.3f %.3f %.3f): the two faces (n1 %.2f %.2f %.2f, n2 %.2f %.2f %.2f) and the plane of the mirror (axis %.0f %.0f %.0f) do not meet in one point\n", v, m.pos[v][0], m.pos[v][1], m.pos[v][2], n1[0], n1[1], n1[2], n2[0], n2[1], n2[2], axis[0], axis[1], axis[2]);
            continue;
        }
        const double dist = (x - m.pos[v]).norm();
        if (std::getenv("FIELDES_TET_DEBUG")) fprintf(stderr, "[snapPlaneEdges]    node %zu (%.3f %.3f %.3f): %zu groups of nodes with a face's normal (%zu nodes in the ring), the two largest n1 (%.2f %.2f %.2f) n2 (%.2f %.2f %.2f), nodes %d %d; the corner of them and the plane (%.3f %.3f %.3f) is %.2f of a length of %.2f away%s\n", v, m.pos[v][0], m.pos[v][1], m.pos[v][2], groups.size(), ringOf[v].size(), n1[0], n1[1], n1[2], n2[0], n2[1], n2[2], groups[size_t(gi)].count, groups[size_t(gj)].count, x[0], x[1], x[2], dist / length, length, (dist > 1.0 * length || dist < 1e-4 * length) ? ": OUT OF REACH or no move" : "");
        if (dist > 1.0 * length || dist < 1e-4 * length) continue;
        goals.push_back({int(v), x, dist, length});
    }    if (goals.empty())
    {
        if (std::getenv("FIELDES_TET_DEBUG")) fprintf(stderr, "[snapPlaneEdges] %zu nodes of a plane with two groups of faces, none with a corner of the edge within reach\n", candidates);
        return 0;
    }
    // (the field at the corners: a straight edge has the surface right there)
    std::vector<Vec3> points;
    std::vector<double> scales;
    for (const auto& g : goals)
    {
        points.push_back(g.x);
        scales.push_back(g.length);
    }
    const std::vector<FieldAt> at = fieldAt(shape, points, scales);
    if (stopped()) return 0;
    std::vector<size_t> order;
    for (size_t k = 0; k < goals.size(); ++k)
    {
        const double gl = at[k].g.norm();
        const bool onSurface = gl > 1e-12 && std::abs(at[k].value) / gl <= 1e-3 * goals[k].length;
        if (std::getenv("FIELDES_TET_DEBUG")) fprintf(stderr, "[snapPlaneEdges]    node %d: the field at its corner %.5f, its gradient %.3f: %s\n", goals[k].v, at[k].value, gl, onSurface ? "on the surface" : "NOT ON THE SURFACE (a bent edge, or the planes of the groups are not those of the edge)");
        if (onSurface) order.push_back(k);
    }
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return goals[a].dist < goals[b].dist; });
    std::vector<Vec3> used;
    size_t moved = 0;
    for (size_t k : order)
    {
        const Goal& g = goals[k];
        bool taken = false;
        for (const Vec3& u : used) taken = taken || (u - g.x).norm() <= 0.05 * g.length;
        if (taken) continue;
        const size_t v = size_t(g.v);
        const Vec3 old = m.pos[v];
        const double before = minRatio(g.v);
        m.pos[v] = g.x;
        const double now = minRatio(g.v);
        const bool kept = now > 0.0 && now >= std::min(opt.floorRatio, before);
        if (std::getenv("FIELDES_TET_DEBUG")) fprintf(stderr, "[snapPlaneEdges]    node %d (%.3f %.3f %.3f) -> corner (%.3f %.3f %.3f), %.2f of a length; mean ratio round it %.3f -> %.3f: %s\n", g.v, old[0], old[1], old[2], g.x[0], g.x[1], g.x[2], g.dist / g.length, before, now, kept ? "moved" : "REFUSED by the tetrahedra");
        if (kept)
        {
            ++moved;
            used.push_back(g.x);
        }
        else m.pos[v] = old;
    }
    if (std::getenv("FIELDES_TET_DEBUG")) fprintf(stderr, "[snapPlaneEdges] %zu nodes of a plane with two groups of faces, %zu with a corner of a straight edge within reach, %zu moved onto it\n", candidates, order.size(), moved);
    return moved;
}

// H223 (the user, 2026-11-14, of the real example 05: "THE MESH IS FUCKED UP NOW" -- the edges along the arms of the bracket were wavy and there were spiky triangles along the creases): the EDGES and CORNERS of the body that
// the lattice cut across put back.  A port of meshing.py _snap_features, which mesh_tet4 had and the mesh of an analysis did not (measured on the bracket at 4 mm: 8.2 % of the boundary faces of the analysis mesh lean more than
// 12 degrees away from the field in their middle, 4.2 % of mesh_tet4's).  Where the lattice does not lie on a sharp edge the nodes stand on the two faces beside it and none on the edge, and the faces between them are a
// slope the body does not have.  The boundary faces that lean away from the field -- their normal against its direction in their middle more than 12 degrees, or their middle off the surface -- name the nodes to look at.  The
// neighbours of such a node on the surface say which planes meet there (their normals, the field's direction at them, in clusters of 40 degrees, one plane each; a node on a crease has no single normal and says nothing): the node
// is moved to the point of the line where two of them meet that is nearest to it, or to the corner of three when that is within an element -- and put back on the surface along the field's own direction.  It stays where it
// is when that point is not on the surface.  The nodes of the two faces beside an edge stand in turns along it, and only the nodes of one of the faces move (the same one all along the edge): a node does not move when a node
// beside it that is of another plane has.  A move that turns a tetrahedron inside out, leaves one poorer than a mean ratio of 0.15, or makes the smallest dihedral angle round the node worse where it is below 10 degrees is taken
// back.  A node on a plane the mesh is mirrored about stays, and none is put on one.
size_t snapFeatures(TetMesh& m, const Tree& shape, const MeshOptions& opt)
{
    const double kPi = 3.14159265358979323846;
    const double cosCrease = std::cos(40.0 * kPi / 180.0);
    const double cosLean = std::cos(12.0 * kPi / 180.0);
    // (H223: the limits of the pass can be tried without a build: FIELDES_TET_EDGE_ROUNDS (3), FIELDES_TET_EDGE_OTHER (1: a node is not moved when a node of the other face of its edge has been), FIELDES_TET_EDGE_FLOOR (0.15), FIELDES_TET_EDGE_SLIVER (10))
    auto envNumber = [](const char* name, double fallback) {
        const char* s = std::getenv(name);
        return s ? std::atof(s) : fallback;
    };
    // (H223: FIELDES_TET_DEBUG_BOX="x0,y0,z0,x1,y1,z1": what happens to each node inside the box at every stage of this pass -- why a node of a row of leaning faces is left)
    double dbgBox[6] = {0, 0, 0, 0, 0, 0};
    bool dbgOn = false;
    if (const char* s = std::getenv("FIELDES_TET_DEBUG_BOX")) dbgOn = std::sscanf(s, "%lf,%lf,%lf,%lf,%lf,%lf", &dbgBox[0], &dbgBox[1], &dbgBox[2], &dbgBox[3], &dbgBox[4], &dbgBox[5]) == 6;
    auto inBox = [&](const Vec3& p) {
        return dbgOn && p[0] >= dbgBox[0] && p[1] >= dbgBox[1] && p[2] >= dbgBox[2] && p[0] <= dbgBox[3] && p[1] <= dbgBox[4] && p[2] <= dbgBox[5];
    };
    const int edgeRounds = std::max(1, int(envNumber("FIELDES_TET_EDGE_ROUNDS", 6)));
    const bool otherFaceRule = envNumber("FIELDES_TET_EDGE_OTHER", 1) != 0.0;
    const double floorRatio = envNumber("FIELDES_TET_EDGE_FLOOR", opt.floorRatio);
    const double sliver = envNumber("FIELDES_TET_EDGE_SLIVER", opt.sliverAngle);
    const size_t nV = m.pos.size();
    if (m.faces.empty() || nV == 0) return 0;
    // the tetrahedra round each node
    std::vector<std::vector<int>> nodeTets(nV);
    for (size_t i = 0; i < m.tets.size(); ++i)
        for (int v : m.tets[i]) nodeTets[size_t(v)].push_back(int(i));
    const double holdTol = 2e-3 * m.h;
    auto held = [&](size_t v) {
        for (const auto& pl : opt.holdPlanes)
            if (std::abs(m.pos[v][pl.first] - pl.second) <= holdTol) return true;
        return false;
    };
    auto onPlane = [&](const Vec3& p) {
        for (const auto& pl : opt.holdPlanes)
            if (std::abs(p[pl.first] - pl.second) <= holdTol) return true;
        return false;
    };
    // (H226, 2026-11-15: symmetry must not leave defects at the plane.  The nodes ON a plane the mesh is mirrored about were left out of the pass altogether, so a rim or an edge that crosses the plane -- the top of the
    // large circle of the real bracket's face, at x = 0 -- had no node on it there, and the faces round the nearest plane node leaned: four leaning faces at each crossing.  A node on a plane now MOVES, but only within the
    // plane: its coordinate across the plane is put back to the plane's own value, so the mirror image continues the mesh)
    auto pin = [&](size_t v, Vec3 x) {
        for (const auto& pl : opt.holdPlanes)
            if (std::abs(m.pos[v][pl.first] - pl.second) <= holdTol) x[pl.first] = pl.second;
        return x;
    };
    size_t movedTotal = 0;
    // (H223, performance: the seconds of each phase of the rounds, printed with FIELDES_TET_DEBUG)
    const bool timingOn = std::getenv("FIELDES_TET_DEBUG") != nullptr;
    double secs[6] = {0, 0, 0, 0, 0, 0};
    auto stampAt = std::chrono::steady_clock::now();
    auto lapSec = [&](int k) {
        if (!timingOn) return;
        const auto now = std::chrono::steady_clock::now();
        secs[k] += std::chrono::duration<double>(now - stampAt).count();
        stampAt = now;
    };
    // (H223, performance: the field is read again only where its inputs have changed since the round before.  The faces and their order are the same in every round of this pass, and a face whose middle and
    // size are the same numbers as before has the same field: reading the field at every face in every round was most of the 2.6 s of the pass on the real bracket at 2 mm)
    std::vector<Vec3> cachedMids;
    std::vector<double> cachedScale;
    std::vector<FieldAt> cachedFaceField;
    std::vector<Vec3> nodePosAt(nV, Vec3::Zero());
    std::vector<double> nodeLenAt(nV, -1.0);
    std::vector<FieldAt> nodeFieldAt(nV);
    for (int round = 0; round < edgeRounds; ++round)
    {
        if (stopped()) break;
        lapSec(5);
        // the neighbours of every node on the surface and the mean length of the edges to them
        std::vector<std::vector<int>> nbrs(nV);
        for (const auto& f : m.faces)
            for (int q = 0; q < 3; ++q)
            {
                const int a = f[size_t(q)], b = f[size_t((q + 1) % 3)];
                nbrs[size_t(a)].push_back(b);
                nbrs[size_t(b)].push_back(a);
            }
        std::vector<double> length(nV, 0.0);
        for (size_t v = 0; v < nV; ++v)
        {
            auto& ring = nbrs[v];
            if (ring.empty()) continue;
            std::sort(ring.begin(), ring.end());
            ring.erase(std::unique(ring.begin(), ring.end()), ring.end());
            double sum = 0.0;
            for (int u : ring) sum += (m.pos[v] - m.pos[size_t(u)]).norm();
            length[v] = sum / double(ring.size());
        }
        lapSec(0);
        // the faces that lean away from the field: the middle of every face, its size, the field there
        const size_t nF = m.faces.size();
        std::vector<Vec3> mids(nF);
        std::vector<double> faceScale(nF);
        for (size_t i = 0; i < nF; ++i)
        {
            const auto& f = m.faces[i];
            mids[i] = (m.pos[size_t(f[0])] + m.pos[size_t(f[1])] + m.pos[size_t(f[2])]) / 3.0;
            faceScale[i] = (length[size_t(f[0])] + length[size_t(f[1])] + length[size_t(f[2])]) / 3.0;
        }
        if (cachedFaceField.size() != nF)
            cachedFaceField = fieldAt(shape, mids, faceScale);
        else
        {
            std::vector<size_t> redo;
            for (size_t i = 0; i < nF; ++i)
                if (mids[i] != cachedMids[i] || faceScale[i] != cachedScale[i]) redo.push_back(i);
            if (!redo.empty())
            {
                std::vector<Vec3> redoPoints;
                std::vector<double> redoScales;
                for (size_t i : redo)
                {
                    redoPoints.push_back(mids[i]);
                    redoScales.push_back(faceScale[i]);
                }
                const std::vector<FieldAt> got = fieldAt(shape, redoPoints, redoScales);
                for (size_t k = 0; k < redo.size(); ++k) cachedFaceField[redo[k]] = got[k];
            }
        }
        cachedMids = mids;
        cachedScale = faceScale;
        const std::vector<FieldAt>& faceField = cachedFaceField;
        if (stopped()) return movedTotal;
        std::vector<char> leaning(nV, 0);
        bool anyLeaning = false;
        for (size_t i = 0; i < nF; ++i)
        {
            const double gl = faceField[i].g.norm();
            if (gl < 1e-12) continue;
            const auto& f = m.faces[i];
            const Vec3 area = 0.5 * (m.pos[size_t(f[1])] - m.pos[size_t(f[0])]).cross(m.pos[size_t(f[2])] - m.pos[size_t(f[0])]);
            const double al = area.norm();
            if (al <= 0.0) continue;
            if (std::abs(area.dot(faceField[i].g)) / (al * gl) < cosLean || std::abs(faceField[i].value) / gl > 0.03 * faceScale[i])
            {
                for (int q = 0; q < 3; ++q) leaning[size_t(f[size_t(q)])] = 1;
                anyLeaning = true;
            }
        }
        lapSec(1);
        if (!anyLeaning) break;
        // (H223, the user's teeth along the edges of the real bracket: why does the pass leave a node?  the counts of each round, printed with FIELDES_TET_DEBUG)
        size_t cLeaning = 0, cOther = 0, cClash = 0;
        for (size_t v = 0; v < nV; ++v) cLeaning += leaning[v] ? 1 : 0;
        // the nodes to look at: the leaning ones and their neighbours, with the direction of the field at each (the normal) and its strength
        std::vector<char> needed(nV, 0);
        for (size_t v = 0; v < nV; ++v)
            if (leaning[v])
            {
                needed[v] = 1;
                for (int u : nbrs[v]) needed[size_t(u)] = 1;
            }
        std::vector<int> need;
        std::vector<Vec3> needPoints;
        std::vector<double> needScales;
        for (size_t v = 0; v < nV; ++v)
            if (needed[v])
            {
                need.push_back(int(v));
                needPoints.push_back(m.pos[v]);
                needScales.push_back(length[v]);
            }
        {
            std::vector<size_t> redo;                    // (the places in `need` where the node's position or its length is not the one the field was read at)
            for (size_t k = 0; k < need.size(); ++k)
            {
                const size_t v = size_t(need[k]);
                if (nodeLenAt[v] != length[v] || nodePosAt[v] != m.pos[v]) redo.push_back(k);
            }
            if (!redo.empty())
            {
                std::vector<Vec3> redoPoints;
                std::vector<double> redoScales;
                for (size_t k : redo)
                {
                    redoPoints.push_back(needPoints[k]);
                    redoScales.push_back(needScales[k]);
                }
                const std::vector<FieldAt> got = fieldAt(shape, redoPoints, redoScales);
                for (size_t j = 0; j < redo.size(); ++j)
                {
                    const size_t v = size_t(need[redo[j]]);
                    nodeFieldAt[v] = got[j];
                    nodeLenAt[v] = length[v];
                    nodePosAt[v] = m.pos[v];
                }
            }
        }
        std::vector<FieldAt> needField(need.size());
        for (size_t k = 0; k < need.size(); ++k) needField[k] = nodeFieldAt[size_t(need[k])];
        if (stopped()) return movedTotal;
        std::vector<Vec3> normal(nV, Vec3::Zero());
        std::vector<double> strength(nV, 0.0);
        std::vector<char> hasNormal(nV, 0);
        std::vector<double> strengths;
        for (size_t k = 0; k < need.size(); ++k)
        {
            const double gl = needField[k].g.norm();
            if (gl > 1e-12)
            {
                const size_t v = size_t(need[k]);
                normal[v] = needField[k].g / gl;
                strength[v] = gl;
                hasNormal[v] = 1;
                strengths.push_back(gl);
            }
        }
        if (strengths.empty()) break;
        std::sort(strengths.begin(), strengths.end());
        // (the gradient of a distance has the length 1 on a face and less on a crease, where the differences straddle it)
        const double typical = strengths[strengths.size() / 2];
        auto sure = [&](int u) { return hasNormal[size_t(u)] != 0 && strength[size_t(u)] >= 0.9 * typical; };
        lapSec(2);
        // where each leaning node should go: the line where two of the planes round it meet, or the corner of three
        struct Found { int v; Vec3 x; };
        std::vector<Found> found;
        for (size_t vv = 0; vv < nV; ++vv)
        {
            if (inBox(m.pos[vv])) fprintf(stderr, "[dbg] round %d node %zu (%.2f %.2f %.2f): leaning %d, has a normal %d, held %d, strength %.2f of typical\n", round, vv, m.pos[vv][0], m.pos[vv][1], m.pos[vv][2], int(leaning[vv]), int(hasNormal[vv]), int(held(vv)), hasNormal[vv] ? strength[vv] / typical : 0.0);
            // (H226 tried to let the nodes ON a plane of the mirror move within the plane: on the real bracket the mirror step then moved two plane vertices at the large circle 1 mm along the plane onto the wrong
            // place and the mirror was refused in every variant; the nodes of the plane are left out of the pass as they were, and the four leaning faces at each crossing of a rim with the plane stay: an open
            // issue of H226, not a limit)
            if (!leaning[vv] || !hasNormal[vv] || held(vv)) continue;
            const int v = int(vv);
            const Vec3 pv = m.pos[vv];
            const bool onFace = sure(v);
            // (H223, 2026-11-15, the notch at the foot of the ring of the real bracket's arms, found with the debug box: the neighbours of a node on the shoulder face x = 42 were put into planes by their NORMALS alone, and the
            // nodes of the neck's top (z 19.5 to 19.8) and those of the ring's top (z 18.3 to 18.8), a shoulder of 1.4 mm between them, have normals within a few degrees of each other: one plane was fitted through both, half way
            // up the shoulder, and the node was moved to its meeting with the shoulder face, 0.55 mm below the true edge, where it stood in the way of the nodes that were right.  A neighbour now joins a plane only when it stands
            // within half a mean edge length of that plane as well -- parallel faces a step apart are different planes)
            const double planeStep = 0.5 * length[vv];
            struct Cluster { Vec3 sum; Vec3 posSum; std::vector<int> members; };
            std::vector<Cluster> clusters;
            if (onFace)
            {
                Cluster own;
                own.sum = normal[vv];
                own.posSum = pv;
                own.members.push_back(v);
                clusters.push_back(own);
            }
            for (int u : nbrs[vv])
            {
                if (!sure(u)) continue;
                const Vec3 n = normal[size_t(u)];
                bool placed = false;
                for (auto& c : clusters)
                {
                    const double len = c.sum.norm();
                    const Vec3 mean = len > 1e-12 ? Vec3(c.sum / len) : Vec3(0.0, 0.0, 1.0);
                    const Vec3 middle = c.posSum / double(c.members.size());
                    if (n.dot(mean) >= cosCrease && std::abs(mean.dot(m.pos[size_t(u)] - middle)) <= planeStep)
                    {
                        c.sum += n;
                        c.posSum += m.pos[size_t(u)];
                        c.members.push_back(u);
                        placed = true;
                        break;
                    }
                }
                if (!placed)
                {
                    Cluster fresh;
                    fresh.sum = n;
                    fresh.posSum = m.pos[size_t(u)];
                    fresh.members.push_back(u);
                    clusters.push_back(fresh);
                }
            }
            if (clusters.size() < (onFace ? size_t(2) : size_t(3)))
            {
                if (inBox(m.pos[vv])) fprintf(stderr, "[dbg]    node %zu: %zu clusters of normals, too few (on a face %d)\n", vv, clusters.size(), int(onFace));
                continue;          // (the planes round it are one plane: no crease)
            }
            struct Plane { size_t count; Vec3 n; double d; };
            std::vector<Plane> planes;
            for (const auto& c : clusters)
            {
                const double len = c.sum.norm();
                const Vec3 mean = len > 1e-12 ? Vec3(c.sum / len) : Vec3(0.0, 0.0, 1.0);
                double d = 0.0;
                for (int u : c.members) d += mean.dot(m.pos[size_t(u)]);
                Plane pl;
                pl.count = c.members.size();
                pl.n = mean;
                pl.d = d / double(c.members.size());
                planes.push_back(pl);
            }
            Plane own;
            own.count = 0;
            own.n = Vec3::Zero();
            own.d = 0.0;
            std::vector<Plane> others;
            if (onFace)
            {
                own = planes[0];
                others.assign(planes.begin() + 1, planes.end());
            }
            else others = planes;
            std::stable_sort(others.begin(), others.end(), [](const Plane& a, const Plane& b) { return a.count > b.count; });
            Vec3 x = Vec3::Zero();
            bool have = false;
            if (others.size() >= (onFace ? size_t(2) : size_t(3)))
            {
                std::vector<Plane> three;
                if (onFace) three.push_back(own);
                three.insert(three.end(), others.begin(), others.end());
                const Vec3 rows[3] = {three[0].n, three[1].n, three[2].n};
                const double rhs[3] = {three[0].d, three[1].d, three[2].d};
                Vec3 solved = Vec3::Zero();
                if (solve3(rows, rhs, solved) && (solved - pv).norm() <= length[vv])
                {
                    x = solved;
                    have = true;
                }
            }
            if (!have && onFace) have = twoPlanes(pv, own.n, own.d, others[0].n, others[0].d, x);
            if (inBox(m.pos[vv])) fprintf(stderr, "[dbg]    node %zu: %zu clusters, place on the edge %d%s (%.2f %.2f %.2f)\n", vv, clusters.size(), int(have), have ? " at" : "", x[0], x[1], x[2]);
            if (!have) continue;
            Found fd;
            fd.v = v;
            fd.x = held(vv) ? pin(vv, x) : x;
            found.push_back(fd);
        }
        lapSec(3);
        if (found.empty())
        {
            if (std::getenv("FIELDES_TET_DEBUG")) fprintf(stderr, "[snapFeatures] round %d: %zu nodes of leaning faces, none with a place on an edge\n", round, cLeaning);
            break;
        }
        // put back on the surface along the field's own direction (at a crease the bisector of its faces: a curved edge, the rim of a hole)
        std::vector<Vec3> goalPoints;
        std::vector<double> goalScales;
        for (const auto& f : found)
        {
            goalPoints.push_back(f.x);
            goalScales.push_back(length[size_t(f.v)]);
        }
        std::vector<Vec3> snapped = snapToSurface(shape, goalPoints, goalScales, 3);
        if (stopped()) return movedTotal;
        for (size_t k = 0; k < found.size(); ++k)
            if (held(size_t(found[k].v))) snapped[k] = pin(size_t(found[k].v), snapped[k]);          // (a node on a plane of the mirror stays in it: the Newton steps may have taken it a hair off)
        struct Target { int v; Vec3 x; double move; Vec3 n0; };
        std::vector<Target> targets;
        for (size_t k = 0; k < found.size(); ++k)
        {
            const size_t v = size_t(found[k].v);
            const double move = (snapped[k] - m.pos[v]).norm();
            if (inBox(m.pos[v])) fprintf(stderr, "[dbg]    node %zu: snapped onto the surface at (%.2f %.2f %.2f), move %.2f of a length of %.2f%s\n", v, snapped[k][0], snapped[k][1], snapped[k][2], move, length[v], (move > length[v] || move < 1e-9 * length[v]) ? ": REFUSED (too far or no move)" : "");
            if (move > length[v] || move < 1e-9 * length[v]) continue;
            Target t;
            t.v = found[k].v;
            t.x = snapped[k];
            t.move = move / length[v];
            t.n0 = normal[v];
            targets.push_back(t);
        }
        if (targets.empty())
        {
            if (std::getenv("FIELDES_TET_DEBUG")) fprintf(stderr, "[snapFeatures] round %d: %zu nodes of leaning faces, %zu with a place on an edge, none within a length of the node\n", round, cLeaning, found.size());
            break;
        }
        std::vector<Vec3> targetPoints;
        std::vector<double> targetScales;
        for (const auto& t : targets)
        {
            targetPoints.push_back(t.x);
            targetScales.push_back(length[size_t(t.v)]);
        }
        const std::vector<FieldAt> checks = fieldAt(shape, targetPoints, targetScales);
        if (stopped()) return movedTotal;
        std::vector<Target> kept;
        for (size_t k = 0; k < targets.size(); ++k)
        {
            const double gl = checks[k].g.norm();
            // (it is on the surface: an edge or a corner; and not on a plane the mesh is mirrored about)
            const bool onTheSurface = gl > 1e-12 && std::abs(checks[k].value) / gl <= 0.02 * length[size_t(targets[k].v)] && (held(size_t(targets[k].v)) || !onPlane(targets[k].x));
            if (inBox(m.pos[size_t(targets[k].v)])) fprintf(stderr, "[dbg]    node %d: at the place the field is %.4f, its gradient %.3f: %s\n", targets[k].v, checks[k].value, gl, onTheSurface ? "kept" : "NOT ON THE SURFACE: dropped");
            if (onTheSurface) kept.push_back(targets[k]);
        }
        lapSec(4);
        // (the same face first all along an edge, then the closest)
        auto r2 = [](double x) { return std::round(x * 100.0) / 100.0; };
        auto r1 = [](double x) { return std::round(x * 10.0) / 10.0; };
        std::sort(kept.begin(), kept.end(), [&](const Target& a, const Target& b) {
            for (int c = 0; c < 3; ++c)
            {
                const double ka = -r2(a.n0[c]), kb = -r2(b.n0[c]);
                if (ka != kb) return ka < kb;
            }
            const double ma = r1(a.move), mb = r1(b.move);
            if (ma != mb) return ma < mb;
            return a.v < b.v;
        });
        // (H223, 2026-11-15, the notches along the straight edges of the real bracket, "ONE SIDE WAS NOT GOOD ENOUGH"; the debug box on the worst row of leaning faces of its left arm: seven nodes, each with its place on the edge
        // found and on the surface, all seven moved together and all seven PUT BACK, in every round.  The moves were made all at once and a tetrahedron that turned inside out took back every moved node of it, so a row of nodes
        // along an edge, each moved a different part of an element, could never move at all: about 480 nodes of the bracket at 2 mm were refused again and again, and the rounds after the third moved 30 nodes each and left
        // 2,170 nodes in leaning faces.  Each move is now made ONE AT A TIME against the mesh as it is, and kept when every tetrahedron round the node is whole and not poorer than the floor (or than it was); when the node alone
        // cannot go, the interior nodes round it go part of the way with it -- a smooth displacement, 0.5 of the step for the nodes of the first ring and 0.2 for the second, then 0.75 and 0.4 --, because a shift of a whole
        // neighbourhood keeps every tetrahedron whole where the step of one node squeezes the ones under it.  Only then the node stays where it is.)
        auto isSurf = [&](int u) { return !nbrs[size_t(u)].empty(); };
        auto tryMove = [&](int v, const Vec3& x, bool& followed) -> bool {
            followed = false;
            const Vec3 old = m.pos[size_t(v)];
            const Vec3 step = x - old;
            // (H223, performance: the first plan, the node alone, changes only the tetrahedra that have the node -- the others keep the numbers they had, and a tetrahedron that was whole is not refused for having them --, so
            // they alone are looked at; the two rings of interior nodes and all the tetrahedra round them are looked up only when the node alone cannot go.  The rings were most of the 1.8 s that the moves of this pass took
            // on the real bracket at 2 mm, and the node alone is what goes in nine moves of ten)
            {
                const std::vector<int>& mine = nodeTets[size_t(v)];
                std::vector<double> ratioBefore(mine.size()), dihedralBefore(mine.size());
                for (size_t k = 0; k < mine.size(); ++k)
                {
                    ratioBefore[k] = meanRatio(m.pos, m.tets[size_t(mine[k])]);
                    dihedralBefore[k] = minDihedral(m.pos, m.tets[size_t(mine[k])]);
                }
                m.pos[size_t(v)] = x;
                bool alone = true;
                for (size_t k = 0; k < mine.size() && alone; ++k)
                {
                    const auto& tt = m.tets[size_t(mine[k])];
                    const double r = meanRatio(m.pos, tt);
                    if (!(r > 0.0) || r < std::min(floorRatio, ratioBefore[k])) alone = false;
                    else if (minDihedral(m.pos, tt) < std::min(sliver, dihedralBefore[k])) alone = false;
                }
                if (alone) return true;
                m.pos[size_t(v)] = old;
            }
            std::unordered_set<int> seen;
            seen.insert(v);
            std::vector<int> ring1, ring2;
            for (int t : nodeTets[size_t(v)])
                for (int u : m.tets[size_t(t)])
                    if (seen.insert(u).second && !isSurf(u) && !held(size_t(u))) ring1.push_back(u);
            for (int u : ring1)
                for (int t : nodeTets[size_t(u)])
                    for (int w : m.tets[size_t(t)])
                        if (seen.insert(w).second && !isSurf(w) && !held(size_t(w))) ring2.push_back(w);
            std::vector<int> around(nodeTets[size_t(v)].begin(), nodeTets[size_t(v)].end());
            for (int u : ring1) around.insert(around.end(), nodeTets[size_t(u)].begin(), nodeTets[size_t(u)].end());
            for (int u : ring2) around.insert(around.end(), nodeTets[size_t(u)].begin(), nodeTets[size_t(u)].end());
            std::sort(around.begin(), around.end());
            around.erase(std::unique(around.begin(), around.end()), around.end());
            std::vector<double> beforeRatio(around.size()), beforeDihedral(around.size());
            for (size_t k = 0; k < around.size(); ++k)
            {
                beforeRatio[k] = meanRatio(m.pos, m.tets[size_t(around[k])]);
                beforeDihedral[k] = minDihedral(m.pos, m.tets[size_t(around[k])]);
            }
            std::vector<Vec3> old1(ring1.size()), old2(ring2.size());
            for (size_t k = 0; k < ring1.size(); ++k) old1[k] = m.pos[size_t(ring1[k])];
            for (size_t k = 0; k < ring2.size(); ++k) old2[k] = m.pos[size_t(ring2[k])];
            const double plans[3][2] = {{0.0, 0.0}, {0.5, 0.2}, {0.75, 0.4}};
            for (const auto& plan : plans)
            {
                m.pos[size_t(v)] = x;
                for (size_t k = 0; k < ring1.size(); ++k) m.pos[size_t(ring1[k])] = old1[k] + plan[0] * step;
                for (size_t k = 0; k < ring2.size(); ++k) m.pos[size_t(ring2[k])] = old2[k] + plan[1] * step;
                bool ok = true;
                for (size_t k = 0; k < around.size() && ok; ++k)
                {
                    const auto& tt = m.tets[size_t(around[k])];
                    const double r = meanRatio(m.pos, tt);
                    // (a tetrahedron that is inside out, or poorer than the floor and poorer than it was, or a sliver that is worse than it was: the move is not made)
                    if (!(r > 0.0) || r < std::min(floorRatio, beforeRatio[k])) ok = false;
                    else if (minDihedral(m.pos, tt) < std::min(sliver, beforeDihedral[k])) ok = false;
                }
                if (ok)
                {
                    followed = plan[0] > 0.0;
                    return true;
                }
            }
            m.pos[size_t(v)] = old;
            for (size_t k = 0; k < ring1.size(); ++k) m.pos[size_t(ring1[k])] = old1[k];
            for (size_t k = 0; k < ring2.size(); ++k) m.pos[size_t(ring2[k])] = old2[k];
            return false;
        };
        // the moves, in that order: not a node of the other face beside an edge that is on it already, and not one that comes within a quarter of a length of a node that has been put there
        std::unordered_map<int64_t, std::vector<Vec3>> taken;
        std::unordered_map<int, Vec3> accepted;
        std::unordered_set<int> moved;
        size_t cRefused = 0, cFollowed = 0;
        auto cellKey = [](int64_t i, int64_t j, int64_t k) { return (i * 73856093LL) ^ (j * 19349663LL) ^ (k * 83492791LL); };
        for (const auto& t : kept)
        {
            const size_t v = size_t(t.v);
            const double s = length[v];
            if (otherFaceRule && sure(t.v))
            {
                bool otherFace = false;
                for (int u : nbrs[v])
                    if (accepted.count(u) && normal[size_t(u)].dot(t.n0) < cosCrease)
                    {
                        otherFace = true;
                        break;
                    }
                if (otherFace) { ++cOther; if (inBox(m.pos[v])) fprintf(stderr, "[dbg]    node %zu: stopped by a node of the other face of its edge that has been put there\n", v); continue; }
            }
            const int64_t cx = int64_t(std::floor(t.x[0] / (0.5 * s))), cy = int64_t(std::floor(t.x[1] / (0.5 * s))), cz = int64_t(std::floor(t.x[2] / (0.5 * s)));
            bool clash = false;
            for (int dx = -1; dx <= 1 && !clash; ++dx)
                for (int dy = -1; dy <= 1 && !clash; ++dy)
                    for (int dz = -1; dz <= 1 && !clash; ++dz)
                    {
                        const auto it = taken.find(cellKey(cx + dx, cy + dy, cz + dz));
                        if (it == taken.end()) continue;
                        for (const Vec3& y : it->second)
                            if ((t.x - y).norm() < 0.25 * s)
                            {
                                clash = true;
                                break;
                            }
                    }
            if (clash) { ++cClash; if (inBox(m.pos[v])) fprintf(stderr, "[dbg]    node %zu: stopped by a node that is there already\n", v); continue; }
            const Vec3 was = m.pos[v];
            bool followed = false;
            if (!tryMove(t.v, t.x, followed))
            {
                ++cRefused;
                if (inBox(was)) fprintf(stderr, "[dbg]    node %zu: REFUSED by the tetrahedra round it (inside out, poorer than %.2f, or a sliver), also with the nodes round it moving along\n", v, floorRatio);
                continue;
            }
            if (followed) ++cFollowed;
            taken[cellKey(cx, cy, cz)].push_back(t.x);
            accepted[t.v] = t.n0;
            moved.insert(t.v);
            if (inBox(was)) fprintf(stderr, "[dbg]    node %zu: MOVED to (%.2f %.2f %.2f)%s\n", v, t.x[0], t.x[1], t.x[2], followed ? ", the nodes round it moving along" : "");
        }
        if (std::getenv("FIELDES_TET_DEBUG")) fprintf(stderr, "[snapFeatures] round %d: %zu nodes of leaning faces, %zu with a place on an edge, %zu of them within a length of the node, %zu on the surface; moved %zu (%zu of them with the nodes round them moving along), refused by the tetrahedra %zu, stopped by the other face of an edge %zu, by a node that is there already %zu\n", round, cLeaning, found.size(), targets.size(), kept.size(), moved.size(), cFollowed, cRefused, cOther, cClash);
        if (moved.empty()) break;
        movedTotal += moved.size();
    }
    lapSec(5);
    if (timingOn)
        fprintf(stderr, "[snapFeatures] seconds: the neighbours and lengths %.2f, the field at every face %.2f, the normals of the leaning nodes %.2f, the place of each node %.2f, putting it on the surface %.2f, the moves %.2f\n", secs[0], secs[1], secs[2], secs[3], secs[4], secs[5]);
    return movedTotal;
}

// H223 (the user, 2026-11-15, of the teeth along the edges of the real bracket: "THE EDGES ARE STILL NOT CLEAN"; the debug box on the foot row of the bracket at 2 mm, an inside corner of an L: body y <= 0 above z = -50, wider
// below): the lattice leaves the nodes of such an edge in PAIRS across the corner -- a node on the wall 1 mm above the corner and a node on the shelf 1 mm out from it, none on the corner -- with a thin wedge of tetrahedra
// between them, material that the body does not have.  snapFeatures finds the corner for every one of them and moves them there, and the move is taken back, because a tetrahedron of the wedge turns inside out (loosening the
// quality limits changes nothing: it is inverted, not poor).  What an inside corner needs is that the wedge goes: the two nodes of a pair become ONE node on the corner, the edge between them collapsed with the corner as its
// target.  A pair is a skin edge a-b whose two nodes each have a flat group of faces (the wall's and the shelf's, planes between 60 and 120 degrees apart), each node off the plane of the other, the two faces on the edge
// neither on one plane nor on the other (a cut corner), the corner point on the field's surface and within 0.85 of the edge's length of both nodes; the collapse is made when the nodes that a and b have in common are the ones
// round the edge (the link condition: no tetrahedra are pinched together) and every tetrahedron that is left has a volume and a mean ratio of at least 0.03.  Pairs that touch each other are done in different rounds (four)
size_t collapseCorners(TetMesh& m, const Tree& shape, const MeshOptions& opt)
{
    if (const char* s = std::getenv("FIELDES_TET_CORNERS")) if (std::atoi(s) == 0) return 0;
    double dbgBox[6] = {0, 0, 0, 0, 0, 0};
    bool dbgOn = false;
    if (const char* s = std::getenv("FIELDES_TET_DEBUG_BOX")) dbgOn = std::sscanf(s, "%lf,%lf,%lf,%lf,%lf,%lf", &dbgBox[0], &dbgBox[1], &dbgBox[2], &dbgBox[3], &dbgBox[4], &dbgBox[5]) == 6;
    auto note = [&](const Vec3& p, int a, int b, const char* what) {
        if (dbgOn && p[0] >= dbgBox[0] && p[1] >= dbgBox[1] && p[2] >= dbgBox[2] && p[0] <= dbgBox[3] && p[1] <= dbgBox[4] && p[2] <= dbgBox[5])
            fprintf(stderr, "[corner] edge %d-%d at (%.2f %.2f %.2f): %s\n", a, b, p[0], p[1], p[2], what);
    };
    size_t cEdges = 0, cPlanes = 0, cCut = 0, cPoint = 0, cSurface = 0, cLink = 0, cBusy = 0, cQuality = 0;
    const double holdTol = 2e-3 * m.h;
    auto onPlane = [&](const Vec3& p) {
        for (const auto& pl : opt.holdPlanes)
            if (std::abs(p[pl.first] - pl.second) <= holdTol) return true;
        return false;
    };
    size_t total = 0;
    for (int round = 0; round < 4; ++round)
    {
        if (stopped()) break;
        const size_t nV = m.pos.size();
        if (m.faces.empty() || nV == 0) break;
        std::vector<std::vector<int>> nodeTets(nV);
        for (size_t i = 0; i < m.tets.size(); ++i)
            for (int v : m.tets[i]) nodeTets[size_t(v)].push_back(int(i));
        const size_t nF = m.faces.size();
        std::vector<Vec3> faceNormal(nF);
        std::vector<std::vector<Vec3>> normalsOf(nV);
        std::unordered_map<int64_t, std::array<int, 3>> edgeFaces;          // {first face, second face, how many faces}
        for (size_t i = 0; i < nF; ++i)
        {
            const auto& f = m.faces[i];
            const Vec3 n = (m.pos[size_t(f[1])] - m.pos[size_t(f[0])]).cross(m.pos[size_t(f[2])] - m.pos[size_t(f[0])]);
            const double len = n.norm();
            faceNormal[i] = len > 0.0 ? Vec3(n / len) : Vec3(Vec3::Zero());
            for (int q = 0; q < 3; ++q)
            {
                const int a = f[size_t(q)], b = f[size_t((q + 1) % 3)];
                if (len > 0.0) normalsOf[size_t(a)].push_back(faceNormal[i]);
                auto& e = edgeFaces[int64_t(std::min(a, b)) * int64_t(nV) + int64_t(std::max(a, b))];
                if (e[2] == 0) e[0] = int(i);
                else if (e[2] == 1) e[1] = int(i);
                e[2] += 1;
            }
        }
        // the plane of the flat group of faces at a node (the largest group of normals within 0.995 of each other, two faces at least)
        // (the groups on the plane of the two faces of the edge itself -- the cut corner -- are left out: round the two nodes of a pair the largest group is that plane, the same for both, and the wall's and the shelf's
        // groups are the next ones; measured with the debug box on the foot row of the real bracket: every pair stopped at "the planes are not 60 to 120 degrees apart")
        auto flat = [&](int v, Vec3& out, const Vec3& skipA, const Vec3& skipB) {
            const auto& normals = normalsOf[size_t(v)];
            size_t bestCount = 0;
            Vec3 best = Vec3::Zero();
            for (const Vec3& n : normals)
            {
                Vec3 sum = Vec3::Zero();
                size_t count = 0;
                for (const Vec3& o : normals)
                    if (n.dot(o) > 0.995) { sum += o; ++count; }
                if (count < 2 || sum.norm() < 1e-12) continue;
                const Vec3 mean = sum.normalized();
                if (std::abs(mean.dot(skipA)) > 0.97 || std::abs(mean.dot(skipB)) > 0.97) continue;
                if (count > bestCount)
                {
                    bestCount = count;
                    best = mean;
                }
            }
            if (bestCount == 0) return false;
            out = best;
            return true;
        };
        struct Pair { int a, b; Vec3 c; double len; };
        std::vector<Pair> candidates;
        for (const auto& kv : edgeFaces)
        {
            const auto& e = kv.second;
            if (e[2] != 2) continue;
            const int a = int(kv.first / int64_t(nV)), b = int(kv.first % int64_t(nV));
            const Vec3 ab = m.pos[size_t(b)] - m.pos[size_t(a)];
            const double len = ab.norm();
            if (len < 0.15 * m.h || len > 1.3 * m.h) { note(m.pos[size_t(a)], a, b, "length of the edge outside 0.15 .. 1.3 of the element size"); continue; }
            ++cEdges;
            Vec3 na, nb;
            if (!flat(a, na, faceNormal[size_t(e[0])], faceNormal[size_t(e[1])]) || !flat(b, nb, faceNormal[size_t(e[0])], faceNormal[size_t(e[1])])) { note(m.pos[size_t(a)], a, b, "a node without a flat group of two faces"); continue; }
            if (std::abs(na.dot(nb)) > 0.5) { note(m.pos[size_t(a)], a, b, "the planes are not 60 to 120 degrees apart"); continue; }                                             // (planes between 60 and 120 degrees apart)
            if (std::abs(na.dot(ab)) < 0.3 * len || std::abs(nb.dot(ab)) < 0.3 * len) { note(m.pos[size_t(a)], a, b, "a node is not off the plane of the other"); continue; }   // (each node is off the plane of the other)
            ++cPlanes;
            bool cut = true;
            for (int k = 0; k < 2; ++k)
            {
                const double d1 = std::abs(faceNormal[size_t(e[size_t(k)])].dot(na)), d2 = std::abs(faceNormal[size_t(e[size_t(k)])].dot(nb));
                if (!(d1 > 0.35 && d1 < 0.95 && d2 > 0.35 && d2 < 0.95)) cut = false;           // (the faces on the edge are neither the wall nor the shelf)
            }
            if (!cut) { note(m.pos[size_t(a)], a, b, "the faces on the edge are not cut-corner faces"); continue; }
            ++cCut;
            Vec3 c;
            if (!twoPlanes(0.5 * (m.pos[size_t(a)] + m.pos[size_t(b)]), na, na.dot(m.pos[size_t(a)]), nb, nb.dot(m.pos[size_t(b)]), c)) { note(m.pos[size_t(a)], a, b, "no corner line"); continue; }
            if ((c - m.pos[size_t(a)]).norm() > 0.85 * len || (c - m.pos[size_t(b)]).norm() > 0.85 * len || onPlane(c)) { note(m.pos[size_t(a)], a, b, "the corner point is too far from a node"); continue; }
            ++cPoint;
            Pair p;
            p.a = a;
            p.b = b;
            p.c = c;
            p.len = len;
            candidates.push_back(p);
        }
        if (candidates.empty())
        {
            if (std::getenv("FIELDES_TET_DEBUG")) fprintf(stderr, "[corners] round %d: %zu skin edges of a length that a cut corner has, %zu with two flat planes at about 90 degrees, %zu with cut-corner faces, %zu with a corner point near: none left\n", round, cEdges, cPlanes, cCut, cPoint);
            break;
        }
        {
            std::vector<Vec3> points;
            std::vector<double> scales;
            for (const auto& p : candidates)
            {
                points.push_back(p.c);
                scales.push_back(p.len);
            }
            const std::vector<FieldAt> at = fieldAt(shape, points, scales);
            if (stopped()) break;
            std::vector<Pair> kept;
            for (size_t k = 0; k < candidates.size(); ++k)
            {
                const double gl = at[k].g.norm();
                if (gl > 1e-12 && std::abs(at[k].value) / gl <= 0.05 * candidates[k].len) { kept.push_back(candidates[k]); ++cSurface; }          // (the corner point is on the surface)
                else note(m.pos[size_t(candidates[k].a)], candidates[k].a, candidates[k].b, "the corner point is not on the surface");
            }
            candidates.swap(kept);
        }
        std::sort(candidates.begin(), candidates.end(), [](const Pair& x, const Pair& y) { return x.len != y.len ? x.len < y.len : x.a < y.a; });
        std::vector<char> touched(nV, 0);
        std::vector<char> dead(m.tets.size(), 0);
        auto has = [&](int t, int v) {
            const auto& tt = m.tets[size_t(t)];
            return tt[0] == v || tt[1] == v || tt[2] == v || tt[3] == v;
        };
        size_t done = 0;
        for (const auto& p : candidates)
        {
            if (touched[size_t(p.a)] || touched[size_t(p.b)]) continue;
            std::vector<int> both, onlyA, onlyB;
            for (int t : nodeTets[size_t(p.a)]) (has(t, p.b) ? both : onlyA).push_back(t);
            for (int t : nodeTets[size_t(p.b)]) if (!has(t, p.a)) onlyB.push_back(t);
            if (both.empty()) continue;
            std::set<int> ring, nearA, nearB, common;
            for (int t : both) for (int v : m.tets[size_t(t)]) ring.insert(v);
            for (int t : nodeTets[size_t(p.a)]) for (int v : m.tets[size_t(t)]) nearA.insert(v);
            for (int t : nodeTets[size_t(p.b)]) for (int v : m.tets[size_t(t)]) nearB.insert(v);
            ring.erase(p.a); ring.erase(p.b);
            nearA.erase(p.a); nearA.erase(p.b); nearB.erase(p.a); nearB.erase(p.b);
            std::set_intersection(nearA.begin(), nearA.end(), nearB.begin(), nearB.end(), std::inserter(common, common.begin()));
            if (common != ring) { ++cLink; note(m.pos[size_t(p.a)], p.a, p.b, "the link condition fails"); continue; }                                                        // (the link condition: otherwise tetrahedra would be pinched together)
            bool busy = false;
            for (int v : nearA) busy = busy || touched[size_t(v)];
            for (int v : nearB) busy = busy || touched[size_t(v)];
            if (busy) { ++cBusy; continue; }                                                                  // (a pair beside it is done in the next round)
            const Vec3 keep = m.pos[size_t(p.a)];
            m.pos[size_t(p.a)] = p.c;
            bool ok = true;
            for (int t : onlyA) ok = ok && meanRatio(m.pos, m.tets[size_t(t)]) >= 0.03;
            for (int t : onlyB)
            {
                std::array<int, 4> tt = m.tets[size_t(t)];
                for (int& v : tt) if (v == p.b) v = p.a;
                ok = ok && meanRatio(m.pos, tt) >= 0.03;
            }
            if (!ok)
            {
                m.pos[size_t(p.a)] = keep;
                ++cQuality;
                note(m.pos[size_t(p.a)], p.a, p.b, "a tetrahedron that is left would have a mean ratio under 0.03 or be inside out");
                continue;
            }
            note(m.pos[size_t(p.a)], p.a, p.b, "COLLAPSED onto the corner");
            for (int t : both) dead[size_t(t)] = 1;
            for (int t : onlyB) for (int& v : m.tets[size_t(t)]) if (v == p.b) v = p.a;
            for (int v : nearA) touched[size_t(v)] = 1;
            for (int v : nearB) touched[size_t(v)] = 1;
            touched[size_t(p.a)] = touched[size_t(p.b)] = 1;
            ++done;
        }
        if (std::getenv("FIELDES_TET_DEBUG")) fprintf(stderr, "[corners] round %d: %zu skin edges of a length that a cut corner has, %zu with two flat planes at about 90 degrees, %zu with cut-corner faces, %zu with a corner point near, %zu with it on the surface; stopped by the link condition %zu, by a pair beside it %zu, by a tetrahedron %zu; collapsed %zu\n", round, cEdges, cPlanes, cCut, cPoint, cSurface, cLink, cBusy, cQuality, done);
        if (!done) break;
        std::vector<std::array<int, 4>> alive;
        alive.reserve(m.tets.size());
        for (size_t t = 0; t < m.tets.size(); ++t)
            if (!dead[t]) alive.push_back(m.tets[t]);
        m.tets.swap(alive);
        finalizeMesh(m);                                                                          // (the faces again, the nodes nobody uses dropped)
        total += done;
    }
    return total;
}

// H223 (the user, 2026-11-15, of the edges of the real bracket: "ALL OF THEM ARE EDGE DEFECTS ... clearly not elements that would have tiny volume"; the debug box on the bite of the left arm's neck edge, a V-shaped dent of half an
// element in an otherwise straight edge): on the shoulder face (the plane x = -42) a node stood 0.5 to 0.8 mm BELOW the edge between two nodes that were on the edge already, 1 mm from each.  Its place on the edge is found right
// and is on the surface, and the move is refused, because the node would lie on the line between its two neighbours on the edge and a triangle of no area is what is left: the pass that moves nodes cannot help here, and the
// faces round the node (it, the edge nodes beside it and the nodes of the top face) stay tilted, which is the bite.  What the place needs is that the node goes: the skin edge from it to a neighbour is COLLAPSED, the node
// becomes its neighbour.  A node v of a face that leans away from the field (the test of snapFeatures: the normal against the field's direction in the middle of the face more than 12 degrees, or the middle off the surface
// by more than 0.03 of the face's size) is collapsed into a neighbour u on the skin when (1) none of the faces that v has and u has not -- they now have u -- turns round (their normal against the old one), (2) none of them
// leans any more, by the same test, (3) the nodes that v and u have in common are those round the edge between them (the link condition: no tetrahedra are pinched together), and (4) no tetrahedron that is left is inside out,
// poorer than the floor of the edge pass (or than it was) or a sliver that is worse than it was.  Nodes of the edge pass that stand where they should are not touched: a collapse that would put the neighbour's faces off the
// surface is refused by (2).  A node on a plane the mesh is mirrored about stays.  Four rounds; FIELDES_TET_BITES=0 turns the pass off
size_t collapseBites(TetMesh& m, const Tree& shape, const MeshOptions& opt)
{
    if (const char* s = std::getenv("FIELDES_TET_BITES")) if (std::atoi(s) == 0) return 0;
    const double kPi = 3.14159265358979323846;
    const double cosLean = std::cos(12.0 * kPi / 180.0);
    auto envNumber = [](const char* name, double fallback) {
        const char* s = std::getenv(name);
        return s ? std::atof(s) : fallback;
    };
    const double floorRatio = envNumber("FIELDES_TET_EDGE_FLOOR", opt.floorRatio);
    const double sliver = envNumber("FIELDES_TET_EDGE_SLIVER", opt.sliverAngle);
    double dbgBox[6] = {0, 0, 0, 0, 0, 0};
    bool dbgOn = false;
    if (const char* s = std::getenv("FIELDES_TET_DEBUG_BOX")) dbgOn = std::sscanf(s, "%lf,%lf,%lf,%lf,%lf,%lf", &dbgBox[0], &dbgBox[1], &dbgBox[2], &dbgBox[3], &dbgBox[4], &dbgBox[5]) == 6;
    auto note = [&](const Vec3& p, int v, int u, const char* what) {
        if (dbgOn && p[0] >= dbgBox[0] && p[1] >= dbgBox[1] && p[2] >= dbgBox[2] && p[0] <= dbgBox[3] && p[1] <= dbgBox[4] && p[2] <= dbgBox[5])
            fprintf(stderr, "[bite] node %d into %d at (%.2f %.2f %.2f): %s\n", v, u, p[0], p[1], p[2], what);
    };
    const double holdTol = 2e-3 * m.h;
    auto held = [&](const Vec3& p) {
        for (const auto& pl : opt.holdPlanes)
            if (std::abs(p[pl.first] - pl.second) <= holdTol) return true;
        return false;
    };
    size_t total = 0;
    for (int round = 0; round < 4; ++round)
    {
        if (stopped()) break;
        const size_t nV = m.pos.size(), nF = m.faces.size();
        if (nF == 0 || nV == 0) break;
        std::vector<std::vector<int>> nodeTets(nV), facesOf(nV);
        for (size_t i = 0; i < m.tets.size(); ++i)
            for (int v : m.tets[i]) nodeTets[size_t(v)].push_back(int(i));
        for (size_t i = 0; i < nF; ++i)
            for (int q = 0; q < 3; ++q) facesOf[size_t(m.faces[i][size_t(q)])].push_back(int(i));
        // the faces that lean away from the field
        std::vector<Vec3> mids(nF);
        std::vector<double> sizes(nF);
        for (size_t i = 0; i < nF; ++i)
        {
            const auto& f = m.faces[i];
            mids[i] = (m.pos[size_t(f[0])] + m.pos[size_t(f[1])] + m.pos[size_t(f[2])]) / 3.0;
            sizes[i] = ((m.pos[size_t(f[0])] - m.pos[size_t(f[1])]).norm() + (m.pos[size_t(f[1])] - m.pos[size_t(f[2])]).norm() + (m.pos[size_t(f[2])] - m.pos[size_t(f[0])]).norm()) / 3.0;
        }
        const std::vector<FieldAt> at = fieldAt(shape, mids, sizes);
        if (stopped()) break;
        auto leans = [&](const Vec3& area, const FieldAt& a, double size) {
            const double gl = a.g.norm(), al = area.norm();
            if (gl < 1e-12 || al <= 0.0) return false;
            return std::abs(area.dot(a.g)) / (al * gl) < cosLean || std::abs(a.value) / gl > 0.03 * size;
        };
        auto areaOf = [&](const std::array<int, 3>& f) { return Vec3(0.5 * (m.pos[size_t(f[1])] - m.pos[size_t(f[0])]).cross(m.pos[size_t(f[2])] - m.pos[size_t(f[0])])); };
        std::vector<char> leaning(nF, 0);
        size_t nLean = 0;
        for (size_t i = 0; i < nF; ++i)
            if (leans(areaOf(m.faces[i]), at[i], sizes[i])) { leaning[i] = 1; ++nLean; }
        if (!nLean) break;
        // the collapses to try: a node of a leaning face into one of the other two nodes of it
        struct Cand { int v, u; double len; };
        std::vector<Cand> cands;
        std::set<std::pair<int, int>> seen;
        for (size_t i = 0; i < nF; ++i)
        {
            if (!leaning[i]) continue;
            const auto& f = m.faces[i];
            for (int q = 0; q < 3; ++q)
            {
                const int v = f[size_t(q)];
                if (held(m.pos[size_t(v)])) continue;
                for (int r = 1; r <= 2; ++r)
                {
                    const int u = f[size_t((q + r) % 3)];
                    if (seen.insert({v, u}).second) cands.push_back({v, u, (m.pos[size_t(v)] - m.pos[size_t(u)]).norm()});
                }
            }
        }
        // the faces round v that u takes over: they must not turn round and must not lean (one call of the field for all of them)
        struct NewFace { size_t cand; Vec3 old; Vec3 now; };
        std::vector<NewFace> newFaces;
        std::vector<Vec3> newMids;
        std::vector<double> newSizes;
        std::vector<char> good(cands.size(), 1);
        for (size_t k = 0; k < cands.size(); ++k)
        {
            const int v = cands[k].v, u = cands[k].u;
            for (int fi : facesOf[size_t(v)])
            {
                auto tri = m.faces[size_t(fi)];
                if (tri[0] == u || tri[1] == u || tri[2] == u) continue;
                const Vec3 before = areaOf(tri);
                for (int& w : tri) if (w == v) w = u;
                const Vec3 after = areaOf(tri);
                if (after.norm() <= 1e-12 || after.dot(before) <= 0.3 * after.norm() * before.norm()) { good[k] = 0; break; }
                newFaces.push_back({k, before, after});
                newMids.push_back((m.pos[size_t(tri[0])] + m.pos[size_t(tri[1])] + m.pos[size_t(tri[2])]) / 3.0);
                newSizes.push_back(((m.pos[size_t(tri[0])] - m.pos[size_t(tri[1])]).norm() + (m.pos[size_t(tri[1])] - m.pos[size_t(tri[2])]).norm() + (m.pos[size_t(tri[2])] - m.pos[size_t(tri[0])]).norm()) / 3.0);
            }
        }
        const std::vector<FieldAt> newAt = newFaces.empty() ? std::vector<FieldAt>() : fieldAt(shape, newMids, newSizes);
        if (stopped()) break;
        for (size_t j = 0; j < newFaces.size(); ++j)
            if (leans(newFaces[j].now, newAt[j], newSizes[j])) good[newFaces[j].cand] = 0;
        std::vector<Cand> valid;
        for (size_t k = 0; k < cands.size(); ++k)
            if (good[k]) valid.push_back(cands[k]);
            else note(m.pos[size_t(cands[k].v)], cands[k].v, cands[k].u, "its faces would turn round or still lean");
        std::sort(valid.begin(), valid.end(), [](const Cand& a, const Cand& b) { return a.len != b.len ? a.len < b.len : (a.v != b.v ? a.v < b.v : a.u < b.u); });
        std::vector<char> touched(nV, 0);
        std::vector<char> dead(m.tets.size(), 0);
        auto has = [&](int t, int v) {
            const auto& tt = m.tets[size_t(t)];
            return tt[0] == v || tt[1] == v || tt[2] == v || tt[3] == v;
        };
        size_t done = 0, cLink = 0, cBusy = 0, cQuality = 0;
        for (const auto& c : valid)
        {
            const int v = c.v, u = c.u;
            if (touched[size_t(v)] || touched[size_t(u)]) { ++cBusy; continue; }
            std::vector<int> both, onlyV;
            for (int t : nodeTets[size_t(v)]) (has(t, u) ? both : onlyV).push_back(t);
            if (both.empty()) continue;
            std::set<int> ring, nearV, nearU, common;
            for (int t : both) for (int w : m.tets[size_t(t)]) ring.insert(w);
            for (int t : nodeTets[size_t(v)]) for (int w : m.tets[size_t(t)]) nearV.insert(w);
            for (int t : nodeTets[size_t(u)]) for (int w : m.tets[size_t(t)]) nearU.insert(w);
            ring.erase(v); ring.erase(u);
            nearV.erase(v); nearV.erase(u); nearU.erase(v); nearU.erase(u);
            std::set_intersection(nearV.begin(), nearV.end(), nearU.begin(), nearU.end(), std::inserter(common, common.begin()));
            if (common != ring) { ++cLink; note(m.pos[size_t(v)], v, u, "the link condition fails"); continue; }
            bool busy = false;
            for (int w : nearV) busy = busy || touched[size_t(w)];
            for (int w : nearU) busy = busy || touched[size_t(w)];
            if (busy) { ++cBusy; continue; }
            bool ok = true;
            for (int t : onlyV)
            {
                const double r0 = meanRatio(m.pos, m.tets[size_t(t)]);
                const double d0 = minDihedral(m.pos, m.tets[size_t(t)]);
                std::array<int, 4> tt = m.tets[size_t(t)];
                for (int& w : tt) if (w == v) w = u;
                const double r1 = meanRatio(m.pos, tt);
                if (!(r1 > 0.0) || r1 < std::min(floorRatio, r0) || minDihedral(m.pos, tt) < std::min(sliver, d0)) { ok = false; break; }
            }
            if (!ok) { ++cQuality; note(m.pos[size_t(v)], v, u, "a tetrahedron that is left would be inside out, poorer than the floor or a sliver"); continue; }
            note(m.pos[size_t(v)], v, u, "COLLAPSED");
            for (int t : both) dead[size_t(t)] = 1;
            for (int t : onlyV) for (int& w : m.tets[size_t(t)]) if (w == v) w = u;
            for (int w : nearV) touched[size_t(w)] = 1;
            for (int w : nearU) touched[size_t(w)] = 1;
            touched[size_t(v)] = touched[size_t(u)] = 1;
            ++done;
        }
        if (std::getenv("FIELDES_TET_DEBUG")) fprintf(stderr, "[bites] round %d: %zu faces lean, %zu collapses to try, %zu would leave no lean; stopped by the link condition %zu, by a collapse beside it %zu, by a tetrahedron %zu; collapsed %zu\n", round, nLean, cands.size(), valid.size(), cLink, cBusy, cQuality, done);
        if (!done) break;
        std::vector<std::array<int, 4>> alive;
        alive.reserve(m.tets.size());
        for (size_t t = 0; t < m.tets.size(); ++t)
            if (!dead[t]) alive.push_back(m.tets[t]);
        m.tets.swap(alive);
        finalizeMesh(m);
        total += done;
    }
    return total;
}

// H223 (the user, 2026-11-14: "I think the defects are less frequent, but more noticable ... because it is inconsistent it looks like sore thumb"; measured on the analysis mesh of the real bracket at 4 mm, the probe
// skin_tail_probe.py): the worst triangles of the skin ALL touch a node of a crease (65 below 25 degrees, 61 of them with two crease nodes; 7 below 15 degrees, all on creases), and of the nodes on a crease with two crease
// edges 15 % have two edges that differ by more than 1.5 (3 % by more than 2): the passes above put every node ON the crease, but where it stands ALONG it is whatever the lattice left, a node every 4 mm and then
// 1 mm, and a triangle that holds the short piece is a sliver.  Netgen's surface optimizer spaces the points of an edge evenly; this does it on the chains of the skin: a node with exactly two crease neighbours (the
// faces on the edge between them turn by more than 30 degrees) goes 0.6 of the way to the middle of the two, ALONG the crease -- on a straight edge that is the straight line, on the rim of a flat face (the
// neighbours lie in the plane of its faces) it is the middle taken into that plane and put back on the rim by Newton steps on the field read a quarter of an element inside, as snapRims does --, and stays when no
// tetrahedron round it is turned inside out or made poorer than 0.3 (or than it was).  A node on a crease between two curved faces, a corner and a node on a mirror plane stay.  Twelve rounds
size_t smoothChains(TetMesh& m, const Tree& shape, const MeshOptions& opt)
{
    const double kPi = 3.14159265358979323846;
    const double cosCrease = std::cos(30.0 * kPi / 180.0);
    const double cosStraight = std::cos(10.0 * kPi / 180.0);           // the two chain edges within 10 degrees of one straight line
    const double floorRatio = opt.rimFloor;
    const size_t nV = m.pos.size();
    if (m.faces.empty() || nV == 0) return 0;
    std::vector<std::vector<int>> nodeTets(nV);
    for (size_t i = 0; i < m.tets.size(); ++i)
        for (int v : m.tets[i]) nodeTets[size_t(v)].push_back(int(i));
    auto minRatio = [&](int v) {
        double r = 1.0;
        for (int t : nodeTets[size_t(v)]) r = std::min(r, meanRatio(m.pos, m.tets[size_t(t)]));
        return r;
    };
    const double holdTol = 2e-3 * m.h;
    auto onPlane = [&](const Vec3& p) {
        for (const auto& pl : opt.holdPlanes)
            if (std::abs(p[pl.first] - pl.second) <= holdTol) return true;
        return false;
    };
    size_t movedTotal = 0;
    for (int round = 0; round < 4; ++round)
    {
        if (stopped()) break;
        const size_t nF = m.faces.size();
        std::vector<Vec3> faceNormal(nF);
        std::vector<std::vector<Vec3>> normalsOf(nV);
        std::vector<double> lengthSum(nV, 0.0);
        std::vector<int> lengthCount(nV, 0);
        std::unordered_map<int64_t, std::array<int, 3>> edgeFaces;          // {first face, second face, how many faces}
        for (size_t i = 0; i < nF; ++i)
        {
            const auto& f = m.faces[i];
            const Vec3 n = (m.pos[size_t(f[1])] - m.pos[size_t(f[0])]).cross(m.pos[size_t(f[2])] - m.pos[size_t(f[0])]);
            const double len = n.norm();
            faceNormal[i] = len > 0.0 ? Vec3(n / len) : Vec3(Vec3::Zero());
            for (int q = 0; q < 3; ++q)
            {
                const int a = f[size_t(q)], b = f[size_t((q + 1) % 3)];
                lengthSum[size_t(a)] += (m.pos[size_t(a)] - m.pos[size_t(b)]).norm();
                lengthCount[size_t(a)] += 1;
                if (len > 0.0) normalsOf[size_t(a)].push_back(faceNormal[i]);
                auto& e = edgeFaces[int64_t(std::min(a, b)) * int64_t(nV) + int64_t(std::max(a, b))];
                if (e[2] == 0) e[0] = int(i);
                else if (e[2] == 1) e[1] = int(i);
                e[2] += 1;
            }
        }
        std::vector<std::vector<int>> chain(nV);
        for (const auto& kv : edgeFaces)
        {
            const auto& e = kv.second;
            if (e[2] != 2) continue;
            if (faceNormal[size_t(e[0])].dot(faceNormal[size_t(e[1])]) >= cosCrease) continue;
            const int lo = int(kv.first / int64_t(nV)), hi = int(kv.first % int64_t(nV));
            chain[size_t(lo)].push_back(hi);
            chain[size_t(hi)].push_back(lo);
        }
        struct Want { int v; Vec3 start; Vec3 plane; double length; bool straight; double limit; };
        std::vector<Want> wanted;
        // the plane of the flat face at a node of a crease: the largest group of faces on one plane (normals within 0.995 of each other), when others turn away from it by more than 30 degrees
        auto flatPlane = [&](size_t v, Vec3& out) {
            const auto& normals = normalsOf[v];
            size_t bestCount = 0;
            Vec3 best = Vec3::Zero();
            for (const Vec3& n : normals)
            {
                Vec3 sum = Vec3::Zero();
                size_t count = 0;
                for (const Vec3& o : normals)
                    if (n.dot(o) > 0.995) { sum += o; ++count; }
                if (count >= 2 && count > bestCount && sum.norm() > 1e-12)
                {
                    bestCount = count;
                    best = sum.normalized();
                }
            }
            if (bestCount == 0) return false;
            bool others = false;
            for (const Vec3& o : normals) others = others || best.dot(o) < cosCrease;
            if (!others) return false;
            out = best;
            return true;
        };
        // THE RIM CUT OFF (measured on a plate with a hole at 3 mm, bottom rim: two rim nodes 4.5 mm apart, and between them no node on the rim -- a node w stands 1.3 mm up the wall and a node p on the face outside the
        // hole, so that two triangles (u, p, w) and (v, p, w) cut the corner off like a chamfer: the edge is not sharp there).  The nodes u and v each have three crease edges, to the next rim node, to p and to w, and u and v
        // have p and w in common: w is pulled DOWN onto the rim between u and v (on the straight line when the edge is straight, by the Newton steps on the rim of a flat face otherwise), and the chain is whole again
        std::vector<char> taken(nV, 0);
        for (size_t w = 0; w < nV; ++w)
        {
            if (chain[w].size() < 2 || lengthCount[w] == 0 || onPlane(m.pos[w])) continue;
            const std::vector<int> cw = chain[w];
            for (size_t i = 0; i < cw.size() && !taken[w]; ++i)
                for (size_t j = i + 1; j < cw.size() && !taken[w]; ++j)
                {
                    const int u = cw[i], v = cw[j];
                    Vec3 plane, planeV;
                    if (!flatPlane(size_t(u), plane) || !flatPlane(size_t(v), planeV) || plane.dot(planeV) < 0.995) continue;          // (both ends of the dip are rim nodes of the same flat face)
                    const Vec3 pu = m.pos[size_t(u)], pv = m.pos[size_t(v)], pw = m.pos[w];
                    const double len = lengthSum[w] / lengthCount[w];
                    const double uv = (pv - pu).norm();
                    if (uv < len || std::abs(plane.dot(pv - pu)) > 0.03 * uv) continue;
                    const double off = plane.dot(pw - pu);
                    if (!(off < -0.05 * len && off > -0.8 * len)) continue;           // (w stands inside the body, a little way off the plane of the face)
                    const Vec3 dir = (pv - pu) / uv;
                    const double along = (pw - pu).dot(dir);
                    if (along < 0.1 * uv || along > 0.9 * uv) continue;                    // (w lies between u and v)
                    // the edge is straight when the crease goes on in the same direction beyond u and beyond v (then the target is the point of the line u-v nearest to w)
                    bool straightU = false, straightV = false;
                    for (int x : chain[size_t(u)]) if (x != int(w) && x != v && (pu - m.pos[size_t(x)]).normalized().dot(dir) > cosStraight) straightU = true;
                    for (int x : chain[size_t(v)]) if (x != int(w) && x != u && (m.pos[size_t(x)] - pv).normalized().dot(dir) > cosStraight) straightV = true;
                    Want wt;
                    wt.v = int(w);
                    wt.length = len;
                    wt.limit = 1.2 * len;
                    wt.straight = straightU && straightV;
                    wt.plane = wt.straight ? Vec3(Vec3::Zero()) : plane;
                    // (the node stands on the wall, a little way above the rim: straight down onto the plane of the face it is on the rim of the wall, a rim; the Newton steps then put it exactly on it)
                    wt.start = wt.straight ? Vec3(pu + (pw - pu).dot(dir) * dir) : Vec3(pw - plane.dot(pw - pu) * plane);
                    wanted.push_back(wt);
                    taken[w] = 1;
                }
        }
        for (size_t v = 0; v < nV; ++v)
        {
            if (taken[v] || chain[v].size() != 2 || lengthCount[v] == 0 || onPlane(m.pos[v])) continue;
            const int a = chain[v][0], b = chain[v][1];
            const Vec3 pv = m.pos[v];
            const double la = (m.pos[size_t(a)] - pv).norm(), lb = (m.pos[size_t(b)] - pv).norm();
            if (la <= 0.0 || lb <= 0.0 || std::max(la, lb) < 1.3 * std::min(la, lb)) continue;          // the two pieces are even enough (the cost: every Newton step is a call of the field)
            const Vec3 mid = 0.5 * (m.pos[size_t(a)] + m.pos[size_t(b)]);
            const Vec3 da = (m.pos[size_t(a)] - pv) / la, db = (m.pos[size_t(b)] - pv) / lb;
            Want w;
            w.v = int(v);
            w.length = lengthSum[v] / lengthCount[v];
            w.limit = 0.6 * w.length;
            w.plane = Vec3::Zero();
            if (da.dot(db) < -cosStraight)
            {
                w.straight = true;
                w.start = pv + 0.6 * (mid - pv);
                wanted.push_back(w);
                continue;
            }
            // a corner of the crease (the chain turns by more than 37 degrees at the node: a corner of a polygon, a pocket) stays where it is; only a chain that bends gently is the rim of a round feature
            if (da.dot(db) > -0.8) continue;
            // the rim of a flat face: the group of faces on one plane (normals within 0.995 of each other) in whose plane both chain neighbours lie
            const auto& normals = normalsOf[v];
            double bestResidual = 1e300;
            Vec3 bestPlane = Vec3::Zero();
            for (const Vec3& n : normals)
            {
                Vec3 sum = Vec3::Zero();
                size_t count = 0;
                for (const Vec3& o : normals)
                    if (n.dot(o) > 0.995) { sum += o; ++count; }
                if (count < 2 || sum.norm() < 1e-12) continue;
                const Vec3 plane = sum.normalized();
                const double residual = std::max(std::abs(plane.dot(m.pos[size_t(a)] - pv)), std::abs(plane.dot(m.pos[size_t(b)] - pv)));
                if (residual < bestResidual)
                {
                    bestResidual = residual;
                    bestPlane = plane;
                }
            }
            if (bestResidual > 0.03 * std::min(la, lb)) continue;                      // (a crease between two curved faces, or a plane the neighbours do not lie in: left alone)
            bool others = false;
            for (const Vec3& o : normals) others = others || bestPlane.dot(o) < cosCrease;
            if (!others) continue;
            w.straight = false;
            w.plane = bestPlane;
            const Vec3 inPlane = mid - bestPlane.dot(mid - pv) * bestPlane;
            w.start = pv + 0.6 * (inPlane - pv);
            wanted.push_back(w);
        }
        if (wanted.empty()) break;
        // the Newton steps of the rims together (the node moves in the plane of its flat face; the field is read a quarter of an element inside the body, where it is the wall's alone)
        std::vector<Vec3> current(wanted.size());
        std::vector<size_t> live;
        for (size_t k = 0; k < wanted.size(); ++k)
        {
            current[k] = wanted[k].start;
            if (!wanted[k].straight) live.push_back(k);
        }
        // (the cost, measured on the real bracket at 2 mm: the imported part's field is slow near its surface and a Newton step with central differences takes seven calls of it for each node; forward
        // differences take four, the direction of the step is as good, and three steps are enough where the start is already near the rim)
        for (int it = 0; it < 3 && !live.empty(); ++it)
        {
            std::vector<Eigen::Vector3f> pts;
            pts.reserve(4 * live.size());
            std::vector<double> steps(live.size());
            for (size_t j = 0; j < live.size(); ++j)
            {
                const Want& w = wanted[live[j]];
                const Vec3 asked = current[live[j]] - 0.25 * w.length * w.plane;
                const double e = std::max(1e-7, 0.02 * w.length);
                steps[j] = e;
                pts.push_back(asked.cast<float>());
                for (int a = 0; a < 3; ++a)
                {
                    Vec3 up = asked;
                    up[a] += e;
                    pts.push_back(up.cast<float>());
                }
            }
            std::vector<float> v;
            evalPoints(shape, pts, v);
            if (stopped()) return movedTotal;
            std::vector<size_t> still;
            for (size_t j = 0; j < live.size(); ++j)
            {
                const Want& w = wanted[live[j]];
                const double e = steps[j];
                const double value = double(v[4 * j]);
                Vec3 g;
                for (int a = 0; a < 3; ++a) g[a] = (double(v[4 * j + size_t(1 + a)]) - value) / e;
                const Vec3 gp = g - g.dot(w.plane) * w.plane;
                const double g2 = gp.squaredNorm();
                if (g2 < 1e-12 || std::abs(value) > 0.25 * w.length) continue;
                Vec3 step = value * gp / g2;
                const double size = step.norm();
                if (size > 0.5 * w.length) step *= 0.5 * w.length / size;
                current[live[j]] -= step;
                if (size >= 1e-4 * w.length) still.push_back(live[j]);
            }
            live.swap(still);
        }
        // a rim node is moved only when it ended ON the surface
        std::vector<Eigen::Vector3f> ends(wanted.size());
        for (size_t k = 0; k < wanted.size(); ++k) ends[k] = current[k].cast<float>();
        std::vector<float> endValue;
        evalPoints(shape, ends, endValue);
        if (stopped()) return movedTotal;
        size_t moved = 0;
        for (size_t k = 0; k < wanted.size(); ++k)
        {
            const Want& w = wanted[k];
            const size_t v = size_t(w.v);
            // (H223, 2026-11-15, found by switching the passes off one at a time on the real bracket at 2 mm: with the spacing of the chains off the mirror holds, with it on four crease vertices stand 0.07 of an element off the
            // surface and the part is meshed whole.  A node of a chain that bends gently -- the arc of a round offset of radius 8 mm with nodes 2 mm apart turns by 14 degrees at each node, inside the 37 degrees of "straight" --
            // went 0.6 of the way to the middle of its neighbours, which for an arc lies INSIDE it by a few hundredths of a millimetre, and was not asked whether it ended on the surface, only the nodes that were put back by Newton
            // steps were: in twelve rounds an arc shrank like a rubber band by 0.14 mm.  Every node, straight or not, is moved only when it ends ON the surface)
            if (std::abs(double(endValue[k])) >= 0.01 * w.length) continue;
            const double travel = (current[k] - m.pos[v]).norm();
            if (travel < 1e-4 * w.length || travel > w.limit || onPlane(current[k])) continue;
            const Vec3 old = m.pos[v];
            const double before = minRatio(w.v);
            bool done = false;
            for (const double share : {1.0, 0.5, 0.25})
            {
                m.pos[v] = old + share * (current[k] - old);
                const double now = minRatio(w.v);
                if (now > 0.0 && now >= std::min(floorRatio, before))
                {
                    done = true;
                    break;
                }
            }
            if (done) ++moved;
            else m.pos[v] = old;
        }
        movedTotal += moved;
        if (moved == 0) break;
    }
    return movedTotal;
}

// H223 (the user, 2026-11-14, of the surface triangles of mesh_surface_tri on the real bracket: "LIKE LOOK AT THIS SHIT", "THAT IS HOW SALOME MESHES TETS IN YOUR VIEW OR WHAT"): the SKIN made regular.  The nodes of the
// surface stand on the true surface after the passes above, but the triangles between them are whatever the lattice cut left: neighbours of 2 mm and 9 mm at an element size of 6 mm, long thin ones along a bore.  Netgen
// lays near-equilateral triangles on the surface first and then runs its surface optimizer (MeshOptimize2d: smoothing of the nodes in the tangent plane of the surface, put back on it); this is that smoothing on the skin of
// the lattice mesh: every node of the surface whose faces all turn the same way (the normals of its faces within 18 degrees of their mean -- a node on an edge, a rim or a crease is left where the passes above put it)
// goes half way to the middle of its neighbours on the surface, along the surface (the step taken across the normal only), and is put back on the surface by Newton steps on the field.  A move that turns a tetrahedron
// inside out, leaves one poorer than 0.85 of what the node had (never asked to be better than 0.5), or turns a face of the skin more than 35 degrees is taken back, nodes on a plane the mesh is mirrored about stay.  Six rounds.
size_t smoothSurface(TetMesh& m, const Tree& shape, const MeshOptions& opt)
{
    const double kPi = 3.14159265358979323846;
    const double cosSmooth = std::cos(18.0 * kPi / 180.0);
    const double cosTurn = std::cos(35.0 * kPi / 180.0);
    const size_t nV = m.pos.size();
    if (m.faces.empty() || nV == 0) return 0;
    std::vector<std::vector<int>> nodeTets(nV);
    for (size_t i = 0; i < m.tets.size(); ++i)
        for (int v : m.tets[i]) nodeTets[size_t(v)].push_back(int(i));
    auto minRatio = [&](int v) {
        double r = 1.0;
        for (int t : nodeTets[size_t(v)]) r = std::min(r, meanRatio(m.pos, m.tets[size_t(t)]));
        return r;
    };
    const double holdTol = 2e-3 * m.h;
    auto held = [&](size_t v) {
        for (const auto& pl : opt.holdPlanes)
            if (std::abs(m.pos[v][pl.first] - pl.second) <= holdTol) return true;
        return false;
    };
    size_t movedTotal = 0;
    for (int round = 0; round < 6; ++round)
    {
        if (stopped()) break;
        const size_t nF = m.faces.size();
        std::vector<std::vector<int>> nbrs(nV), nodeFaces(nV);
        std::vector<Vec3> areaVec(nF);
        for (size_t i = 0; i < nF; ++i)
        {
            const auto& f = m.faces[i];
            areaVec[i] = (m.pos[size_t(f[1])] - m.pos[size_t(f[0])]).cross(m.pos[size_t(f[2])] - m.pos[size_t(f[0])]);
            for (int q = 0; q < 3; ++q)
            {
                const int a = f[size_t(q)], b = f[size_t((q + 1) % 3)];
                nbrs[size_t(a)].push_back(b);
                nbrs[size_t(b)].push_back(a);
                nodeFaces[size_t(a)].push_back(int(i));
            }
        }
        // THE COST (measured 2026-11-15 on the real bracket at 2 mm, the imported part's slow field: the analysis took 11 s on the build before this pass, 30 s with it and 42 s with the chain pass as well --
        // every node of the skin was moved by Newton steps on the field, 2 million evaluations a round): only where there is something to make regular -- a node with a triangle whose smallest angle is under 35
        // degrees, or whose edges differ by more than 1.45 -- is looked at; a node in the middle of even triangles stays
        std::vector<char> poor(nV, 0);
        {
            std::vector<double> longest(nV, 0.0), shortest(nV, 1e300);
            const double cosPoor = std::cos(35.0 * kPi / 180.0);
            for (size_t i = 0; i < nF; ++i)
            {
                const auto& f = m.faces[i];
                double l2[3];
                for (int q = 0; q < 3; ++q) l2[q] = (m.pos[size_t(f[size_t((q + 1) % 3)])] - m.pos[size_t(f[size_t(q)])]).squaredNorm();
                int s = 0;
                for (int q = 1; q < 3; ++q) if (l2[q] < l2[s]) s = q;
                const double b2 = l2[(s + 1) % 3], c2 = l2[(s + 2) % 3];
                const double denom = 2.0 * std::sqrt(b2 * c2);
                const bool thin = denom > 0.0 && (b2 + c2 - l2[s]) / denom > cosPoor;          // (the smallest angle is opposite the shortest edge)
                for (int q = 0; q < 3; ++q)
                {
                    const size_t a = size_t(f[size_t(q)]);
                    const double l = std::sqrt(l2[q]);
                    longest[a] = std::max(longest[a], l);
                    shortest[a] = std::min(shortest[a], l);
                    longest[size_t(f[size_t((q + 1) % 3)])] = std::max(longest[size_t(f[size_t((q + 1) % 3)])], l);
                    shortest[size_t(f[size_t((q + 1) % 3)])] = std::min(shortest[size_t(f[size_t((q + 1) % 3)])], l);
                    if (thin) poor[a] = 1;
                }
            }
            for (size_t v = 0; v < nV; ++v)
                if (longest[v] > 1.45 * shortest[v]) poor[v] = 1;
        }
        std::vector<int> ids;
        std::vector<Vec3> target;
        std::vector<double> scales;
        std::vector<Vec3> normalOf(nV, Vec3::Zero());
        for (size_t v = 0; v < nV; ++v)
        {
            auto& ring = nbrs[v];
            if (ring.size() < 3 || nodeFaces[v].empty() || held(v) || !poor[v]) continue;
            std::sort(ring.begin(), ring.end());
            ring.erase(std::unique(ring.begin(), ring.end()), ring.end());
            Vec3 sum = Vec3::Zero();
            for (int fi : nodeFaces[v])
            {
                const double al = areaVec[size_t(fi)].norm();
                if (al > 0.0) sum += areaVec[size_t(fi)] / al;
            }
            const double sl = sum.norm();
            if (sl < 1e-12) continue;
            const Vec3 n = sum / sl;
            bool smooth = true;
            for (int fi : nodeFaces[v])
            {
                const double al = areaVec[size_t(fi)].norm();
                if (al <= 0.0 || (areaVec[size_t(fi)] / al).dot(n) < cosSmooth)
                {
                    smooth = false;
                    break;
                }
            }
            if (!smooth) continue;
            Vec3 c = Vec3::Zero();
            double len = 0.0;
            for (int u : ring)
            {
                c += m.pos[size_t(u)];
                len += (m.pos[size_t(u)] - m.pos[v]).norm();
            }
            c /= double(ring.size());
            len /= double(ring.size());
            Vec3 d = c - m.pos[v];
            d -= d.dot(n) * n;
            if (d.norm() < 1e-6 * len) continue;
            ids.push_back(int(v));
            target.push_back(m.pos[v] + 0.5 * d);
            scales.push_back(len);
            normalOf[v] = n;
        }
        if (ids.empty()) break;
        const std::vector<Vec3> onto = snapToSurface(shape, target, scales, 2);
        if (stopped()) break;
        std::unordered_map<int, Vec3> old;
        std::unordered_map<int, double> before;
        std::unordered_set<int> moved;
        for (size_t k = 0; k < ids.size(); ++k)
        {
            const int v = ids[k];
            if ((onto[k] - target[k]).norm() > 0.3 * scales[k] || (onto[k] - m.pos[size_t(v)]).norm() > 0.6 * scales[k]) continue;
            old[v] = m.pos[size_t(v)];
            before[v] = minRatio(v);
            m.pos[size_t(v)] = onto[k];
            moved.insert(v);
        }
        for (int pass = 0; pass < 4 && !moved.empty(); ++pass)
        {
            std::unordered_set<int> bad;
            for (int v : moved)
            {
                const double now = minRatio(v);
                if (now <= 0.0 || now < 0.85 * std::min(before[v], 0.5)) bad.insert(v);
                for (int fi : nodeFaces[size_t(v)])
                {
                    const auto& f = m.faces[size_t(fi)];
                    const Vec3 a = (m.pos[size_t(f[1])] - m.pos[size_t(f[0])]).cross(m.pos[size_t(f[2])] - m.pos[size_t(f[0])]);
                    const double al = a.norm(), ao = areaVec[size_t(fi)].norm();
                    if (al <= 0.0 || ao <= 0.0 || a.dot(areaVec[size_t(fi)]) / (al * ao) < cosTurn) bad.insert(v);
                }
                // a neighbour that moved too: its tetrahedra are looked at through the node that has them
                for (int t : nodeTets[size_t(v)])
                    if (meanRatio(m.pos, m.tets[size_t(t)]) <= 0.0)
                        for (int w : m.tets[size_t(t)])
                            if (moved.count(w)) bad.insert(w);
            }
            if (bad.empty()) break;
            for (int v : bad)
            {
                m.pos[size_t(v)] = old.at(v);
                moved.erase(v);
            }
        }
        if (moved.empty()) break;
        movedTotal += moved.size();
    }
    return movedTotal;
}

// H223 (2026-11-15, found by the NAFEMS LE1 benchmark of validation/: at the element sizes 85, 60 and 55 mm of nine the stress at the tip of the hole read 14 to 29 % high, and the mesh was fine by every number of quality -- it had NO NODE AT THE
// CORNER where the wall of the hole, the plane of symmetry and a face of the plate meet; the two edges that end there stopped 35 mm short of it, half an element, and the faces cut across the corner; at 80 mm a node sat on the corner and the stress was right).
// The lattice stage puts a vertex on a corner only when a lattice vertex is within a fraction of an element of it, and the edge pass moves a node onto the LINE of two planes, never along it, so a node that stands on an edge at the
// END of its chain stays where it is and the corner beyond it is left off the mesh.  A node of the skin whose faces make two groups of normals more than 40 degrees apart lies on an edge; when its neighbours on that edge are on one side
// of it only, it is the end of a chain, and the line of the edge is followed beyond it in steps of a twentieth of an element out to one element: where the field changes side is the corner (the surface closes the edge there).  The node is
// moved to it when every tetrahedron round it stays whole and not poorer than the floor (or than it was) and no face of the skin turns round, and when no node is there already; the nearest chain end goes first, so that of the two edges
// that end in one corner only the one node moves.  A node on a plane the mesh is mirrored about stays, and none is put on one.  The corner is a point of the field's own surface, found by halving, not a guess
size_t captureCorners(TetMesh& m, const Tree& shape, const MeshOptions& opt)
{
    const double kPi = 3.14159265358979323846;
    const double cosCrease = std::cos(40.0 * kPi / 180.0);
    const size_t nV = m.pos.size();
    if (m.faces.empty() || nV == 0) return 0;
    const double h = m.h;
    const bool debug = std::getenv("FIELDES_TET_DEBUG") != nullptr;
    std::vector<std::vector<int>> nbrs(nV), nodeFaces(nV), nodeTets(nV);
    for (size_t f = 0; f < m.faces.size(); ++f)
        for (int q = 0; q < 3; ++q)
        {
            const int a = m.faces[f][size_t(q)], b = m.faces[f][size_t((q + 1) % 3)];
            nbrs[size_t(a)].push_back(b);
            nbrs[size_t(b)].push_back(a);
            nodeFaces[size_t(a)].push_back(int(f));
        }
    for (size_t i = 0; i < m.tets.size(); ++i)
        for (int v : m.tets[i]) nodeTets[size_t(v)].push_back(int(i));
    auto faceCross = [&](int f) {
        const auto& t = m.faces[size_t(f)];
        return Vec3((m.pos[size_t(t[1])] - m.pos[size_t(t[0])]).cross(m.pos[size_t(t[2])] - m.pos[size_t(t[0])]));
    };
    const double holdTol = 2e-3 * h;
    auto onMirror = [&](const Vec3& p) {
        for (const auto& pl : opt.holdPlanes)
            if (std::abs(p[pl.first] - pl.second) <= holdTol) return true;
        return false;
    };
    struct Cand { size_t v; Vec3 dir; double gn; int first; double lo; double hi; };
    std::vector<Cand> cands;
    // (the first version took the edge from the groups of normals of the faces round a node, two of them 40 degrees apart, and found nothing where it was needed: a node at the end of a chain that stops short of a corner has a THIRD group,
    // the faces that cut across the corner, 45 to 60 degrees from both planes.  The edge is now taken from the skin's own CREASE EDGES: an edge whose two faces differ by more than 40 degrees.  A node with a crease edge to a neighbour
    // and no crease edge that goes on in the opposite direction (within 30 degrees of it) is the end of that chain, whatever else its faces do)
    std::vector<Vec3> faceUnit(m.faces.size(), Vec3::Zero());
    for (size_t f = 0; f < m.faces.size(); ++f)
    {
        const Vec3 a = faceCross(int(f));
        if (a.norm() > 0.0) faceUnit[f] = a.normalized();
    }
    std::unordered_map<uint64_t, std::array<int, 2>> edgeFaces;
    for (size_t f = 0; f < m.faces.size(); ++f)
        for (int q = 0; q < 3; ++q)
        {
            const int a = m.faces[f][size_t(q)], b = m.faces[f][size_t((q + 1) % 3)];
            const uint64_t key = (uint64_t(uint32_t(std::min(a, b))) << 32) | uint64_t(uint32_t(std::max(a, b)));
            auto it = edgeFaces.find(key);
            if (it == edgeFaces.end()) edgeFaces.emplace(key, std::array<int, 2>{{int(f), -1}});
            else if (it->second[1] < 0) it->second[1] = int(f);
        }
    std::vector<std::vector<int>> creaseNbrs(nV);
    for (const auto& kv : edgeFaces)
    {
        if (kv.second[1] < 0) continue;
        if (faceUnit[size_t(kv.second[0])].dot(faceUnit[size_t(kv.second[1])]) >= cosCrease) continue;
        const int a = int(kv.first >> 32), b = int(kv.first & 0xFFFFFFFFull);
        creaseNbrs[size_t(a)].push_back(b);
        creaseNbrs[size_t(b)].push_back(a);
    }
    for (size_t v = 0; v < nV; ++v)
    {
        if (creaseNbrs[v].empty() || onMirror(m.pos[v])) continue;
        for (int u : creaseNbrs[v])
        {
            const Vec3 w = m.pos[size_t(u)] - m.pos[v];
            const double len = w.norm();
            if (!(len > 1e-12)) continue;
            const Vec3 e = -w / len;                          // (the way on, away from the chain)
            bool goesOn = false;
            for (int u2 : creaseNbrs[v])
            {
                if (u2 == u) continue;
                const Vec3 w2 = m.pos[size_t(u2)] - m.pos[v];
                const double l2 = w2.norm();
                const double along = w2.dot(e);
                if (l2 > 1e-12 && along > 0.0 && (w2 - along * e).norm() <= 0.5 * l2)
                {
                    goesOn = true;
                    break;
                }
            }
            if (!goesOn) cands.push_back({v, e, 0.0, -1, 0.0, 0.0});
        }
    }
    if (cands.empty()) return 0;
    // the field's gradient at each candidate (central differences), and the field along the line in steps of a twentieth of an element
    const double dd = 1e-3 * h;
    const int steps = 20;
    const double stepLen = 0.05 * h;
    {
        std::vector<Eigen::Vector3f> pts;
        for (const auto& c : cands)
        {
            const Vec3 p = m.pos[c.v];
            for (int ax = 0; ax < 3; ++ax)
            {
                Vec3 lo3 = p, hi3 = p;
                lo3[ax] -= dd;
                hi3[ax] += dd;
                pts.push_back(lo3.cast<float>());
                pts.push_back(hi3.cast<float>());
            }
        }
        std::vector<float> vals;
        evalPoints(shape, pts, vals);
        if (stopped()) return 0;
        for (size_t a = 0; a < cands.size(); ++a)
        {
            Vec3 g;
            for (int ax = 0; ax < 3; ++ax) g[ax] = (double(vals[6 * a + size_t(2 * ax + 1)]) - double(vals[6 * a + size_t(2 * ax)])) / (2 * dd);
            cands[a].gn = g.norm();
        }
        pts.clear();
        for (const auto& c : cands)
            for (int j = 1; j <= steps; ++j) pts.push_back((m.pos[c.v] + (j * stepLen) * c.dir).cast<float>());
        evalPoints(shape, pts, vals);
        if (stopped()) return 0;
        for (size_t a = 0; a < cands.size(); ++a)
        {
            const double tol = 1e-3 * h * cands[a].gn;        // (the value of the field that is the surface: a hair, so that the roundoff of a field on a plane is not taken for a crossing)
            for (int j = 1; j <= steps; ++j)
                if (cands[a].gn > 1e-12 && double(vals[a * size_t(steps) + size_t(j - 1)]) > tol)
                {
                    cands[a].first = j;
                    cands[a].lo = (j - 1) * stepLen;
                    cands[a].hi = j * stepLen;
                    break;
                }
        }
    }
    // halving between the step before the crossing and the step
    for (int it = 0; it < 28; ++it)
    {
        std::vector<size_t> who;
        std::vector<Eigen::Vector3f> pts;
        for (size_t a = 0; a < cands.size(); ++a)
        {
            if (cands[a].first < 0) continue;
            who.push_back(a);
            pts.push_back((m.pos[cands[a].v] + 0.5 * (cands[a].lo + cands[a].hi) * cands[a].dir).cast<float>());
        }
        if (who.empty()) break;
        std::vector<float> vals;
        evalPoints(shape, pts, vals);
        if (stopped()) return 0;
        for (size_t k = 0; k < who.size(); ++k)
        {
            Cand& c = cands[who[k]];
            const double mid = 0.5 * (c.lo + c.hi);
            if (double(vals[k]) > 1e-3 * h * c.gn) c.hi = mid;
            else c.lo = mid;
        }
    }
    // (the corner must be ON the surface, as the check of a mirrored mesh reads it -- the field over the larger of its gradient there and the gradient at the node: on the real bracket at 2 mm a node that this pass had moved
    // twice along the ring-neck junction stood 0.082 of an element off the surface, two vertices of the mirrored mesh failed the check of 0.06 and the whole mirror was refused: 17 s became 35 s)
    {
        std::vector<size_t> who;
        std::vector<Eigen::Vector3f> pts;
        for (size_t a = 0; a < cands.size(); ++a)
        {
            if (cands[a].first < 0) continue;
            who.push_back(a);
            const Vec3 end = m.pos[cands[a].v] + cands[a].lo * cands[a].dir;
            pts.push_back(end.cast<float>());
            for (int ax = 0; ax < 3; ++ax)
            {
                Vec3 lo3 = end, hi3 = end;
                lo3[ax] -= dd;
                hi3[ax] += dd;
                pts.push_back(lo3.cast<float>());
                pts.push_back(hi3.cast<float>());
            }
        }
        if (!who.empty())
        {
            // the usual length of the field's gradient at the surface (the median over a sample of the skin's nodes): a field that is not a distance has any usual length, and at a crease the gradient read across it is shorter
            double gUsual = 0.0;
            {
                std::vector<Eigen::Vector3f> sample;
                std::vector<size_t> ids;
                const size_t stride = std::max<size_t>(1, nV / 1500);
                for (size_t v = 0; v < nV; v += stride)
                {
                    if (nodeFaces[v].empty()) continue;
                    ids.push_back(v);
                    const Vec3 p = m.pos[v];
                    for (int ax = 0; ax < 3; ++ax)
                    {
                        Vec3 lo3 = p, hi3 = p;
                        lo3[ax] -= dd;
                        hi3[ax] += dd;
                        sample.push_back(lo3.cast<float>());
                        sample.push_back(hi3.cast<float>());
                    }
                }
                std::vector<float> sv;
                evalPoints(shape, sample, sv);
                if (stopped()) return 0;
                std::vector<double> norms;
                for (size_t k = 0; k < ids.size(); ++k)
                {
                    Vec3 g;
                    for (int ax = 0; ax < 3; ++ax) g[ax] = (double(sv[6 * k + size_t(2 * ax + 1)]) - double(sv[6 * k + size_t(2 * ax)])) / (2 * dd);
                    if (g.norm() > 1e-12) norms.push_back(g.norm());
                }
                if (!norms.empty())
                {
                    std::nth_element(norms.begin(), norms.begin() + std::ptrdiff_t(norms.size() / 2), norms.end());
                    gUsual = norms[norms.size() / 2];
                }
            }
            std::vector<float> vals;
            evalPoints(shape, pts, vals);
            if (stopped()) return 0;
            for (size_t k = 0; k < who.size(); ++k)
            {
                Vec3 g;
                for (int ax = 0; ax < 3; ++ax) g[ax] = (double(vals[7 * k + size_t(2 + 2 * ax)]) - double(vals[7 * k + size_t(1 + 2 * ax)])) / (2 * dd);
                const double gn = std::max(std::max(g.norm(), cands[who[k]].gn), gUsual);
                const double away = gn > 1e-12 ? std::abs(double(vals[7 * k])) / gn : 1e9;
                if (!(away <= 0.02 * h))
                {
                    if (debug) fprintf(stderr, "[captureCorners] node %zu: the corner is %.3f of an element off the surface (the mirror check would refuse it): not moved\n", cands[who[k]].v, away / h);
                    cands[who[k]].first = -1;
                }
            }
        }
    }
    std::vector<size_t> order;
    for (size_t a = 0; a < cands.size(); ++a)
        if (cands[a].first >= 0 && cands[a].lo >= 0.1 * h) order.push_back(a);
    std::sort(order.begin(), order.end(), [&](size_t x, size_t y) { return cands[x].lo < cands[y].lo; });
    size_t moved = 0, refused = 0, there = 0;
    // (a node is a candidate once for every crease edge it ends: it can stand at the end of two chains.  The corner of a candidate is measured from the place the node had when the corners were looked for, so a node that has been
    // moved already is not moved again: on the real bracket at 2 mm the second move of one node went from its new place by the length and the direction found for its old one, ended 0.082 of an element off the surface
    // and the check of the mirrored mesh refused the whole mirror -- 17 s became 35 s)
    std::vector<char> movedNode(nV, 0);
    for (size_t a : order)
    {
        const Cand& c = cands[a];
        if (movedNode[c.v]) continue;
        const Vec3 start = m.pos[c.v];
        const Vec3 end = start + c.lo * c.dir;
        if (onMirror(end)) continue;
        bool occupied = false;
        for (size_t u = 0; u < nV && !occupied; ++u)
            if (u != c.v && !nodeFaces[u].empty() && (m.pos[u] - end).norm() < 0.2 * h) occupied = true;
        if (occupied) { ++there; continue; }
        // the tetrahedra round the node and the faces of the skin: whole, not poorer than the floor (or than they were), none turned round
        std::vector<double> oldRatio(nodeTets[c.v].size());
        for (size_t k = 0; k < nodeTets[c.v].size(); ++k) oldRatio[k] = meanRatio(m.pos, m.tets[size_t(nodeTets[c.v][k])]);
        std::vector<Vec3> oldNormal(nodeFaces[c.v].size());
        for (size_t k = 0; k < nodeFaces[c.v].size(); ++k) oldNormal[k] = faceCross(nodeFaces[c.v][k]).normalized();
        m.pos[c.v] = end;
        bool ok = true;
        for (size_t k = 0; k < nodeTets[c.v].size() && ok; ++k)
        {
            const auto& t = m.tets[size_t(nodeTets[c.v][k])];
            if (!(signedVolume(m.pos[size_t(t[0])], m.pos[size_t(t[1])], m.pos[size_t(t[2])], m.pos[size_t(t[3])]) > 0)) ok = false;
            else if (meanRatio(m.pos, t) < std::min(opt.floorRatio, oldRatio[k])) ok = false;
        }
        for (size_t k = 0; k < nodeFaces[c.v].size() && ok; ++k)
        {
            const Vec3 now = faceCross(nodeFaces[c.v][k]);
            if (!(now.norm() > 0.0) || now.normalized().dot(oldNormal[k]) <= 0.0) ok = false;
        }
        if (!ok)
        {
            m.pos[c.v] = start;
            ++refused;
            if (debug) fprintf(stderr, "[captureCorners] node %zu at (%.2f %.2f %.2f): the corner at (%.2f %.2f %.2f), %.2f of an element away, is refused by the tetrahedra\n", c.v, start.x(), start.y(), start.z(), end.x(), end.y(), end.z(), c.lo / h);
            continue;
        }
        ++moved;
        movedNode[c.v] = 1;
        if (debug) fprintf(stderr, "[captureCorners] node %zu moved from (%.2f %.2f %.2f) to the corner at (%.2f %.2f %.2f), %.2f of an element\n", c.v, start.x(), start.y(), start.z(), end.x(), end.y(), end.z(), c.lo / h);
    }
    if (debug) fprintf(stderr, "[captureCorners] %zu ends of an edge, %zu with a corner within an element, %zu nodes moved onto the corner, %zu refused by the tetrahedra, %zu where a node is already\n", cands.size(), order.size(), moved, refused, there);
    return moved;
}

}   // anonymous namespace

// The clean-up of the surface of a tetrahedral mesh (see tetmesh.hpp): the edges and corners, then the rims, then the skin made regular.  The boundary is made first (finalizeMesh), because the passes read it
void snapSurface(TetMesh& mesh, const Tree& shape, const MeshOptions& options)
{
    mesh.edgesSnapped = 0;
    mesh.rimsSnapped = 0;
    mesh.surfaceSmoothed = 0;
    mesh.chainsSmoothed = 0;
    mesh.cornersCollapsed = 0;
    mesh.bitesCollapsed = 0;
    if (!options.edges && !options.rims && !options.smooth) return;
    EvalScope evaluators(shape);                  // (the evaluators of the shape are made once for all the calls of the passes below)
    // (H223, performance: the seconds of each pass, printed with FIELDES_TET_DEBUG -- the passes of the surface were 6.7 s of the 15.6 s of the half mesh of the real bracket at 2 mm)
    const bool timing = std::getenv("FIELDES_TET_DEBUG") != nullptr;
    auto clockMark = std::chrono::steady_clock::now();
    auto lapTo = [&](const char* name) {
        if (!timing) return;
        const auto now = std::chrono::steady_clock::now();
        fprintf(stderr, "[snapSurface] %s: %.2f s\n", name, std::chrono::duration<double>(now - clockMark).count());
        clockMark = now;
    };
    finalizeMesh(mesh);
    lapTo("the boundary of the mesh");
    if (options.edges) mesh.edgesSnapped = snapFeatures(mesh, shape, options);
    lapTo("edges and corners moved");
    if (stopped()) return;
    if (options.edges && options.corners) mesh.edgesSnapped += captureCorners(mesh, shape, options);
    lapTo("corners captured");
    if (stopped()) return;
    if (options.edges) mesh.edgesSnapped += snapPlaneEdges(mesh, shape, options);
    lapTo("edges at the planes of the mirror");
    if (stopped()) return;
    if (options.edges) mesh.cornersCollapsed = collapseCorners(mesh, shape, options);
    lapTo("inside corners collapsed");
    if (stopped()) return;
    if (options.edges) mesh.bitesCollapsed = collapseBites(mesh, shape, options);
    lapTo("bites collapsed");
    if (stopped()) return;
    if (options.rims) mesh.rimsSnapped = snapRims(mesh, shape, options);
    lapTo("rims");
    if (stopped()) return;
    if (options.smooth && options.chains) mesh.chainsSmoothed = smoothChains(mesh, shape, options);
    lapTo("spacing along the edges");
    if (stopped()) return;
    if (options.smooth) mesh.surfaceSmoothed = smoothSurface(mesh, shape, options);
    lapTo("skin smoothed");
}

// H193 (the user: "make it recoginze features properly"; measured 2026-11-14: a shell box with walls of 1.2 mm is refused at the default element size, a plate 1.5 mm thick holds 48 % of its volume, a tube with a
// wall of 1 mm 1 %): the lattice vertices NEAR THE SURFACE, for a look at how thick the material is there.  A wall thinner than the lattice is lost or half filled and says nothing: the plate of 1.5 mm held 41 % of its
// volume at spacings of 2.5 and 1.9 mm and all of it from 1.5 mm, the tube with a wall of 1 mm 58 % at 1.0 mm and all of it from 0.8 -- so it is the thickness of the walls against the spacing that decides, and it is
// measured where the surface is: a vertex whose value is under `reach` of the spacing, either side (the field taken as a distance: a vertex that near has the surface within it, and a wall that lies between planes
// of the lattice still has some vertex that near).  Netgen restricts the size by the distance across a thin part (stlgeom, RestrictHChartDistOneChart) so that a thin part is never dropped; here the size is the choice
// (meshing.py thin_size), and this says where to look
bool probeSurface(const Tree& shape, const Eigen::Vector3d& lo, const Eigen::Vector3d& hi, double h, double reach, size_t maxReturn, bool fit, SurfaceProbe& out, std::string& error)
{
    out = SurfaceProbe();
    if (!(h > 0))
    {
        error = "the element size must be positive";
        return false;
    }
    Lattice L;
    if (!makeLattice(lo, hi, h, fit, L, error)) return false;
    std::vector<float> val;
    if (!latticeValues(shape, L, val))
    {
        error = "cancelled";
        return false;
    }
    const size_t nTotal = L.total();
    const double hm = std::cbrt(L.hv.x() * L.hv.y() * L.hv.z());
    out.h = hm;
    out.vertices = nTotal;
    const float limit = float(reach * hm);
    size_t inside = 0, nearby = 0;
    for (size_t id = 0; id < nTotal; ++id)
    {
        if (val[id] <= 0) ++inside;
        if (std::abs(val[id]) < limit) ++nearby;
    }
    out.inside = inside;
    out.nearby = nearby;
    if (nearby && maxReturn)
    {
        const size_t stride = std::max<size_t>(1, (nearby + maxReturn - 1) / maxReturn);
        size_t seen = 0;
        for (size_t id = 0; id < nTotal; ++id)
            if (std::abs(val[id]) < limit && (seen++ % stride) == 0)
            {
                out.pos.push_back(L.pos(id));
                out.value.push_back(double(val[id]));
            }
        out.stride = stride;
    }
    return true;
}

bool meshShape(const Tree& shape, const Eigen::Vector3d& lo, const Eigen::Vector3d& hi,
               double h, TetMesh& out, std::string& error, const MeshOptions& optIn)
{
    MeshOptions opt = optIn;
    // (experiments: FIELDES_TET_REACH, FIELDES_TET_FEATURES=0)
    if (const char* r = std::getenv("FIELDES_TET_REACH")) opt.featureReach = std::atof(r);
    if (const char* f = std::getenv("FIELDES_TET_FEATURES")) opt.features = std::atoi(f) != 0;
    if (const char* f = std::getenv("FIELDES_TET_FIT")) opt.fit = std::atoi(f) != 0;
    if (const char* f = std::getenv("FIELDES_TET_MEND")) opt.mend = std::atoi(f) != 0;
    if (const char* f = std::getenv("FIELDES_TET_RIMS")) opt.rims = std::atoi(f) != 0;
    if (const char* f = std::getenv("FIELDES_TET_EDGES")) opt.edges = std::atoi(f) != 0;
    if (const char* f = std::getenv("FIELDES_TET_SMOOTH")) opt.smooth = std::atoi(f) != 0;
    if (const char* f = std::getenv("FIELDES_TET_CHAINS")) opt.chains = std::atoi(f) != 0;
    if (const char* f = std::getenv("FIELDES_TET_CORNERS")) opt.corners = std::atoi(f) != 0;
    out = TetMesh();
    if (!(h > 0))
    {
        error = "the element size must be positive";
        return false;
    }
    Lattice L;
    if (!makeLattice(lo, hi, h, opt.fit, L, error)) return false;
    const size_t nTotal = L.total();
    h = std::cbrt(L.hv.x() * L.hv.y() * L.hv.z());          // (the size that the tolerances and the lengths of the rest of the meshing are measured in)
    out.h = h;

    // (H226, performance: where the seconds of a mesh go, printed with FIELDES_TET_DEBUG: the field at the lattice, the cut and the numbering, the passes of the surface, the mender, the spacing along the edges)
    const auto clock0 = std::chrono::steady_clock::now();
    auto lap = [&](std::chrono::steady_clock::time_point& since) {
        const auto now = std::chrono::steady_clock::now();
        const double s = std::chrono::duration<double>(now - since).count();
        since = now;
        return s;
    };
    auto clockMark = clock0;
    // The shape's field at every lattice vertex, in blocks
    std::vector<float> val;
    if (!latticeValues(shape, L, val)) { error = "cancelled"; return false; }
    const double secondsField = lap(clockMark);
    // -1 inside (field <= 0), +1 outside; 0 after a vertex is moved onto the surface
    std::vector<int8_t> sgn(nTotal);
    for (size_t v = 0; v < nTotal; ++v) sgn[v] = val[v] <= 0 ? -1 : 1;
    std::vector<float>().swap(val);

    // The tetrahedra wholly inside, the ones the surface cuts, and the lattice
    // edges it crosses
    std::vector<Tet4> inside, mixed;
    std::unordered_map<uint64_t, uint32_t> edgeIndex;
    std::vector<CutEdge> cuts;
    bool tooMany = false;
    size_t visited = 0;
    forEachTet(L, [&](Id a, Id b, Id c, Id d) {
        if ((++visited & 0xFFFF) == 0 && stopped()) tooMany = true;     // (ends the loop's work: see below)
        if (tooMany) return;
        const Tet4 t = {a, b, c, d};
        const int sum = sgn[a] + sgn[b] + sgn[c] + sgn[d];
        if (sum == 4) return;
        if (sum == -4)
        {
            if (inside.size() + mixed.size() >= opt.maxTets) tooMany = true;
            else inside.push_back(t);
            return;
        }
        if (inside.size() + mixed.size() >= opt.maxTets) tooMany = true;
        else mixed.push_back(t);
        for (int p = 0; p < 4; ++p)
            for (int q = p + 1; q < 4; ++q)
            {
                if (sgn[t[p]] == sgn[t[q]]) continue;
                const uint64_t key = edgeKey(t[p], t[q]);
                if (edgeIndex.emplace(key, uint32_t(cuts.size())).second)
                {
                    CutEdge e;
                    e.a = std::min(t[p], t[q]);
                    e.b = std::max(t[p], t[q]);
                    e.t = 0.5;
                    cuts.push_back(e);
                }
            }
    });
    if (stopped()) { error = "cancelled"; return false; }
    if (tooMany)
    {
        error = "too many elements: use a larger element size";
        return false;
    }
    if (inside.empty() && mixed.empty())
    {
        error = "the part has no material inside the analysis region";
        return false;
    }

    // Where the surface crosses each of those edges: on the shape's own field
    // (bisection), not a straight-line guess
    {
        const size_t m = cuts.size();
        std::vector<Vec3> pa(m), pb(m);
        std::vector<double> tin(m), tout(m);
        for (size_t e = 0; e < m; ++e)
        {
            pa[e] = L.pos(cuts[e].a);
            pb[e] = L.pos(cuts[e].b);
            const bool aInside = sgn[cuts[e].a] < 0;
            tin[e] = aInside ? 0.0 : 1.0;
            tout[e] = aInside ? 1.0 : 0.0;
        }
        std::vector<Eigen::Vector3f> pts(m);
        std::vector<float> v;
        for (int it = 0; it < 22; ++it)
        {
            for (size_t e = 0; e < m; ++e)
                pts[e] = (pa[e] + 0.5 * (tin[e] + tout[e]) * (pb[e] - pa[e])).cast<float>();
            evalPoints(shape, pts, v);
            if (stopped()) { error = "cancelled"; return false; }
            for (size_t e = 0; e < m; ++e)
            {
                const double mid = 0.5 * (tin[e] + tout[e]);
                if (v[e] <= 0) tin[e] = mid;
                else tout[e] = mid;
            }
        }
        for (size_t e = 0; e < m; ++e)
        {
            cuts[e].t = 0.5 * (tin[e] + tout[e]);
            cuts[e].p = pa[e] + cuts[e].t * (pb[e] - pa[e]);
        }
    }

    // The standard warp: a vertex with a crossed edge whose crossing is within alpha of
    // the edge's length from it goes to the nearest such crossing
    std::vector<int32_t> warp(nTotal, -1);
    {
        std::vector<float> best(nTotal, std::numeric_limits<float>::max());
        for (size_t e = 0; e < cuts.size(); ++e)
        {
            const CutEdge& c = cuts[e];
            const bool sameKind = (c.a < L.nL) == (c.b < L.nL);
            const double len = (L.pos(c.b) - L.pos(c.a)).norm();          // (the spacing is not the same along the three axes: the edge's own length)
            const double alpha = sameKind ? opt.alphaLong : opt.alphaShort;
            const double dA = c.t, dB = 1.0 - c.t;
            if (dA < alpha && float(dA * len) < best[c.a])
            {
                best[c.a] = float(dA * len);
                warp[c.a] = int32_t(e);
            }
            if (dB < alpha && float(dB * len) < best[c.b])
            {
                best[c.b] = float(dB * len);
                warp[c.b] = int32_t(e);
            }
        }
    }

    // Sharp features.  The surface's normal at each crossing (from the field, by central
    // differences); a vertex whose nearby crossings have normals more than featureAngle apart
    // is near a crease, and the point where those tangent planes meet (nearest the vertex) is
    // where it should go -- an edge's line, or a corner
    std::unordered_map<Id, Vec3> featureAll;
    if (opt.features && !cuts.empty())
    {
        const size_t m = cuts.size();
        std::vector<Vec3> normal(m, Vec3::Zero());
        std::vector<double> gradNorm(m, 0.0);
        {
            const double d = 1e-3 * h;
            std::vector<Eigen::Vector3f> pts(6 * m);
            for (size_t e = 0; e < m; ++e)
                for (int a = 0; a < 3; ++a)
                {
                    Vec3 lo3 = cuts[e].p, hi3 = cuts[e].p;
                    lo3[a] -= d;
                    hi3[a] += d;
                    pts[6 * e + size_t(2 * a)] = lo3.cast<float>();
                    pts[6 * e + size_t(2 * a + 1)] = hi3.cast<float>();
                }
            std::vector<float> v;
            evalPoints(shape, pts, v);
            if (stopped()) { error = "cancelled"; return false; }
            for (size_t e = 0; e < m; ++e)
            {
                Vec3 g;
                for (int a = 0; a < 3; ++a) g[a] = (double(v[6 * e + size_t(2 * a + 1)]) - double(v[6 * e + size_t(2 * a)])) / (2 * d);
                gradNorm[e] = g.norm();
                if (gradNorm[e] > 1e-12) normal[e] = g / gradNorm[e];
            }
        }
        // the crossings at each vertex
        std::vector<uint32_t> start(nTotal + 1, 0), list(2 * m);
        for (const auto& c : cuts)
        {
            start[c.a + 1]++;
            start[c.b + 1]++;
        }
        for (size_t v = 0; v < nTotal; ++v) start[v + 1] += start[v];
        {
            std::vector<uint32_t> cursor(start.begin(), start.end() - 1);
            for (size_t e = 0; e < m; ++e)
            {
                list[cursor[cuts[e].a]++] = uint32_t(e);
                list[cursor[cuts[e].b]++] = uint32_t(e);
            }
        }
        const double cosSharp = std::cos(opt.featureAngle * 3.14159265358979323846 / 180.0);
        std::vector<Id> cand;
        std::vector<Vec3> target;
        std::vector<uint32_t> pairA, pairB;           // (for each candidate the two crossings whose normals differ most: the curve guard below looks between them)
        size_t dbgNear = 0, dbgUse = 0, dbgSharp = 0, dbgReach = 0, dbgOn = 0, dbgCurve = 0;
        for (size_t v = 0; v < nTotal; ++v)
        {
            if ((v & 0x3FFF) == 0 && stopped()) { error = "cancelled"; return false; }
            if (start[v + 1] - start[v] < 2) continue;
            dbgNear++;
            const Vec3 vp = L.pos(v);
            std::vector<uint32_t> use;
            for (uint32_t k = start[v]; k < start[v + 1]; ++k)
            {
                const uint32_t e = list[k];
                if (gradNorm[e] > 1e-12 && (cuts[e].p - vp).norm() <= 0.75 * h) use.push_back(e);
            }
            if (use.size() < 2) continue;
            dbgUse++;
            bool sharp = false;
            for (size_t i = 0; i < use.size() && !sharp; ++i)
                for (size_t j = i + 1; j < use.size() && !sharp; ++j)
                    sharp = normal[use[i]].dot(normal[use[j]]) < cosSharp;
            if (!sharp) continue;
            dbgSharp++;
            // (the point of the tangent planes nearest the vertex itself)
            const Vec3 mean = vp;
            Eigen::MatrixXd A(long(use.size()), 3);
            Eigen::VectorXd b(long(use.size()));
            for (size_t i = 0; i < use.size(); ++i)
            {
                A.row(long(i)) = normal[use[i]].transpose();
                b[long(i)] = normal[use[i]].dot(cuts[use[i]].p - mean);
            }
            Eigen::JacobiSVD<Eigen::MatrixXd> svd(A, Eigen::ComputeThinU | Eigen::ComputeThinV);
            const Eigen::VectorXd s = svd.singularValues();
            Eigen::VectorXd sInv = Eigen::VectorXd::Zero(s.size());
            for (long i = 0; i < s.size(); ++i)
                if (s[i] > 0.1 * s[0]) sInv[i] = 1.0 / s[i];
            const Eigen::VectorXd dx = svd.matrixV() * (sInv.asDiagonal() * (svd.matrixU().transpose() * b));
            const Vec3 x = mean + Vec3(dx[0], dx[1], dx[2]);
            if (!std::isfinite(x.x()) || !std::isfinite(x.y()) || !std::isfinite(x.z())) continue;
            if ((x - vp).norm() > opt.featureReach * h) continue;
            dbgReach++;
            cand.push_back(Id(v));
            target.push_back(x);
            {
                double worst = 2.0;
                uint32_t ea = use[0], eb = use[1];
                for (size_t i = 0; i < use.size(); ++i)
                    for (size_t j = i + 1; j < use.size(); ++j)
                    {
                        const double dd = normal[use[i]].dot(normal[use[j]]);
                        if (dd < worst)
                        {
                            worst = dd;
                            ea = use[i];
                            eb = use[j];
                        }
                    }
                pairA.push_back(ea);
                pairB.push_back(eb);
            }
        }
        // THE CURVE GUARD (H223, found by the validation of a wire two elements across, 2026-11-15: its volume was 6.9 % short and its modes 3 to 4 % low; with the features off 2.2 % and exact).  A curved
        // surface of a radius near the element size turns by more than featureAngle over the 0.75 h round a vertex, and was taken for a crease: the vertex went to the point of the tangent planes and was
        // put back on the surface, which pulls the nodes of a round part together.  A crease and a curve differ in what lies BETWEEN the two crossings whose normals differ most: on a crease the surface
        // at their middle is one of the two faces (its normal is one of the two), on a curve it is between them (its normal is near their bisector).  Such a vertex is left where it is.
        // FIELDES_TET_CURVE=0 turns it off
        if (!cand.empty() && !(std::getenv("FIELDES_TET_CURVE") && std::atoi(std::getenv("FIELDES_TET_CURVE")) == 0))
        {
            const double d = 1e-3 * h;
            std::vector<Vec3> mid(cand.size());
            std::vector<Vec3> nm(cand.size(), Vec3::Zero());
            for (size_t i = 0; i < cand.size(); ++i) mid[i] = 0.5 * (cuts[pairA[i]].p + cuts[pairB[i]].p);
            std::vector<float> v;
            std::vector<Eigen::Vector3f> pts;
            for (int iter = 0; iter < 3; ++iter)
            {
                pts.resize(7 * cand.size());
                for (size_t i = 0; i < cand.size(); ++i)
                {
                    pts[7 * i] = mid[i].cast<float>();
                    for (int a = 0; a < 3; ++a)
                    {
                        Vec3 lo3 = mid[i], hi3 = mid[i];
                        lo3[a] -= d;
                        hi3[a] += d;
                        pts[7 * i + size_t(1 + 2 * a)] = lo3.cast<float>();
                        pts[7 * i + size_t(2 + 2 * a)] = hi3.cast<float>();
                    }
                }
                evalPoints(shape, pts, v);
                if (stopped()) { error = "cancelled"; return false; }
                for (size_t i = 0; i < cand.size(); ++i)
                {
                    Vec3 g;
                    for (int a = 0; a < 3; ++a) g[a] = (double(v[7 * i + size_t(2 + 2 * a)]) - double(v[7 * i + size_t(1 + 2 * a)])) / (2 * d);
                    const double gg = g.squaredNorm();
                    if (gg <= 1e-20) continue;
                    if (iter < 2)
                    {
                        const double f = double(v[7 * i]);
                        mid[i] -= f * g / gg;
                    }
                    else
                        nm[i] = g / std::sqrt(gg);
                }
            }
            std::vector<Id> keepId;
            std::vector<Vec3> keepTarget;
            for (size_t i = 0; i < cand.size(); ++i)
            {
                bool curve = false;
                if (nm[i].squaredNorm() > 0.5)
                {
                    const Vec3 na = normal[pairA[i]], nb = normal[pairB[i]];
                    const double angle = std::acos(std::max(-1.0, std::min(1.0, na.dot(nb))));
                    Vec3 bis = Vec3::Zero();
                    if ((na + nb).squaredNorm() > 1e-12) bis = (na + nb).normalized();
                    if (bis.squaredNorm() > 0.5)
                    {
                        const double toBis = std::acos(std::max(-1.0, std::min(1.0, nm[i].dot(bis))));
                        curve = toBis < 0.25 * angle;
                    }
                }
                if (curve) dbgCurve++;
                else
                {
                    keepId.push_back(cand[i]);
                    keepTarget.push_back(target[i]);
                }
            }
            cand.swap(keepId);
            target.swap(keepTarget);
        }
        // (only those the surface really passes through: the tangent planes of a curved crease
        // meet a little off it, so a few Newton steps put the point back on the surface)
        if (!cand.empty())
        {
            const double d = 1e-3 * h;
            std::vector<float> v;
            std::vector<Eigen::Vector3f> pts;
            for (int iter = 0; iter < 4; ++iter)
            {
                pts.resize(7 * cand.size());
                for (size_t i = 0; i < cand.size(); ++i)
                {
                    pts[7 * i] = target[i].cast<float>();
                    for (int a = 0; a < 3; ++a)
                    {
                        Vec3 lo3 = target[i], hi3 = target[i];
                        lo3[a] -= d;
                        hi3[a] += d;
                        pts[7 * i + size_t(1 + 2 * a)] = lo3.cast<float>();
                        pts[7 * i + size_t(2 + 2 * a)] = hi3.cast<float>();
                    }
                }
                evalPoints(shape, pts, v);
                if (stopped()) { error = "cancelled"; return false; }
                for (size_t i = 0; i < cand.size(); ++i)
                {
                    Vec3 g;
                    for (int a = 0; a < 3; ++a) g[a] = (double(v[7 * i + size_t(2 + 2 * a)]) - double(v[7 * i + size_t(1 + 2 * a)])) / (2 * d);
                    const double gg = g.squaredNorm();
                    const double f = double(v[7 * i]);
                    if (gg > 1e-20 && std::abs(f) > 1e-6 * h) target[i] -= f * g / gg;
                }
            }
            std::vector<Eigen::Vector3f> fin(cand.size());
            for (size_t i = 0; i < cand.size(); ++i) fin[i] = target[i].cast<float>();
            evalPoints(shape, fin, v);
            for (size_t i = 0; i < cand.size(); ++i)
            {
                // a distance-like field: |f| is how far off the surface x is
                if (std::abs(double(v[i])) <= 1e-4 * h && (target[i] - L.pos(cand[i])).norm() <= opt.featureReach * h)
                {
                    featureAll[cand[i]] = target[i];
                    dbgOn++;
                }
            }
        }
        if (std::getenv("FIELDES_TET_DEBUG"))
            fprintf(stderr, "[tetmesh] features: %zu vertices with >= 2 crossings, %zu with >= 2 near, %zu sharp, %zu within reach, %zu of them taken for a curve, %zu on the surface\n",
                    dbgNear, dbgUse, dbgSharp, dbgReach, dbgCurve, dbgOn);
    }

    // The tetrahedra of the mesh, in lattice ids (a cut point's id is nTotal
    // plus its edge's index, so it orders after every lattice vertex).  Generated from
    // the signs, the warped positions and the features that are on.
    std::unordered_set<Id> featureOff;
    std::unordered_map<Id, Vec3> feature;
    std::vector<int8_t> sgn0 = sgn;
    std::vector<Tet4> outT;
    const double tiny = 1e-9 * h * h * h;
    size_t degenerate = 0;
    auto position = [&](Id id) -> Vec3 {
        if (id >= nTotal) return cuts[id - nTotal].p;
        if (!feature.empty())
        {
            const auto f = feature.find(id);
            if (f != feature.end()) return f->second;
        }
        if (warp[id] >= 0) return cuts[size_t(warp[id])].p;
        return L.pos(id);
    };
    auto cutId = [&](Id a, Id b) -> Id {
        const auto it = edgeIndex.find(edgeKey(a, b));
        return Id(nTotal + it->second);
    };
    auto generate = [&]() {
        // signs: 0 where a vertex was moved onto the surface
        sgn = sgn0;
        feature.clear();
        for (const auto& f : featureAll)
            if (!featureOff.count(f.first)) feature[f.first] = f.second;
        for (size_t v = 0; v < nTotal; ++v)
            if (warp[v] >= 0) sgn[v] = 0;
        for (const auto& f : feature) sgn[f.first] = 0;
        outT.clear();
        outT.reserve(inside.size() + mixed.size() * 2);
        degenerate = 0;
        auto emit = [&](Id a, Id b, Id c, Id d) {
            const double vol = signedVolume(position(a), position(b), position(c), position(d));
            if (std::abs(vol) < tiny)
            {
                degenerate++;
                return;
            }
            if (vol < 0) std::swap(c, d);
            outT.push_back({a, b, c, d});
        };
        // A triangular prism (bottom b[], top t[], the columns b[i]-t[i]) as three
        // tetrahedra: a fan from its lowest-numbered vertex, whose three
        // quadrilateral faces are cut along the diagonal through the lowest-numbered
        // vertex of each -- the rule every tetrahedron sharing a face applies to
        // it, so the faces match
        auto emitPrism = [&](Id b[3], Id t[3]) {
            Id m = b[0];
            bool inTop = false;
            int col = 0;
            for (int i = 0; i < 3; ++i)
            {
                if (b[i] < m) { m = b[i]; inTop = false; col = i; }
                if (t[i] < m) { m = t[i]; inTop = true; col = i; }
            }
            if (inTop) for (int i = 0; i < 3; ++i) std::swap(b[i], t[i]);
            const int u = (col + 1) % 3, w = (col + 2) % 3;
            emit(b[col], t[col], t[u], t[w]);
            // the far face: b[u], b[w], t[w], t[u]
            const Id fm = std::min(std::min(b[u], b[w]), std::min(t[w], t[u]));
            if (fm == b[u] || fm == t[w])
            {
                emit(b[col], b[u], b[w], t[w]);
                emit(b[col], b[u], t[w], t[u]);
            }
            else
            {
                emit(b[col], b[u], b[w], t[u]);
                emit(b[col], b[w], t[w], t[u]);
            }
        };

        size_t work = 0;
        for (const Tet4& t : inside)
        {
            if ((++work & 0x3FFFF) == 0 && stopped()) return;       // (the pass loop below stops the meshing)
            emit(t[0], t[1], t[2], t[3]);
        }
        for (const Tet4& t : mixed)
        {
            if ((++work & 0x3FFFF) == 0 && stopped()) return;
            Id in[4], on[4], ex[4];
            int nI = 0, nO = 0, nX = 0;
            for (int p = 0; p < 4; ++p)
            {
                if (sgn[t[p]] < 0) in[nI++] = t[p];
                else if (sgn[t[p]] == 0) on[nO++] = t[p];
                else ex[nX++] = t[p];
            }
            if (nI == 0) continue;
            if (nX == 0)
            {
                emit(t[0], t[1], t[2], t[3]);
                continue;
            }
            if (nI == 1)
            {
                // one inside vertex: it, the vertices on the surface and the crossings of its edges
                Id v[4];
                int c = 0;
                v[c++] = in[0];
                for (int q = 0; q < nO; ++q) v[c++] = on[q];
                for (int q = 0; q < nX; ++q) v[c++] = cutId(in[0], ex[q]);
                emit(v[0], v[1], v[2], v[3]);
            }
            else if (nI == 3)
            {
                Id b[3] = {in[0], in[1], in[2]};
                Id tp[3] = {cutId(in[0], ex[0]), cutId(in[1], ex[0]), cutId(in[2], ex[0])};
                emitPrism(b, tp);
            }
            else if (nX == 2)
            {
                // two inside, two outside: the prism between the inside edge and the surface
                Id b[3] = {in[0], cutId(in[0], ex[0]), cutId(in[0], ex[1])};
                Id tp[3] = {in[1], cutId(in[1], ex[0]), cutId(in[1], ex[1])};
                emitPrism(b, tp);
            }
            else
            {
                // two inside, one on the surface, one outside: a pyramid on the quadrilateral
                // (in0, in1, crossing of in1, crossing of in0) with its apex on the surface
                const Id q0 = in[0], q1 = in[1], q2 = cutId(in[1], ex[0]), q3 = cutId(in[0], ex[0]);
                const Id fm = std::min(std::min(q0, q1), std::min(q2, q3));
                if (fm == q0 || fm == q2)
                {
                    emit(on[0], q0, q1, q2);
                    emit(on[0], q0, q2, q3);
                }
                else
                {
                    emit(on[0], q0, q1, q3);
                    emit(on[0], q1, q2, q3);
                }
            }
        }
    };

    // Generate; put back the snaps that leave a folded tetrahedron or a sliver, and again
    // (the first pass is the one with every snap on)
    size_t reverted = 0;
    for (int pass = 0; pass < 8; ++pass)
    {
        generate();
        if (stopped()) { error = "cancelled"; return false; }
        if (feature.empty()) break;
        std::vector<size_t> bad;
        findBadTets(outT, position, h, bad);
        if (stopped()) { error = "cancelled"; return false; }
        std::unordered_set<Id> off;
        for (size_t t : bad)
            for (int p = 0; p < 4; ++p)
                if (feature.count(outT[t][size_t(p)])) off.insert(outT[t][size_t(p)]);
        if (off.empty()) break;
        for (Id v : off) featureOff.insert(v);
        reverted += off.size();
        if (pass == 7)
        {
            // still bad: no snapping at all
            for (const auto& f : featureAll) featureOff.insert(f.first);
            generate();
            if (stopped()) { error = "cancelled"; return false; }
        }
    }
    out.degenerate = degenerate;
    out.featureVertices = feature.size();
    out.featuresReverted = reverted;
    if (outT.empty())
    {
        error = "the part has no material inside the analysis region";
        return false;
    }

    // Compact vertex numbers, in order of first use
    std::vector<int> remap(nTotal + cuts.size(), -1);
    out.tets.resize(outT.size());
    for (size_t e = 0; e < outT.size(); ++e)
    {
        for (int p = 0; p < 4; ++p)
        {
            const Id id = outT[e][size_t(p)];
            if (remap[id] < 0)
            {
                remap[id] = int(out.pos.size());
                out.pos.push_back(position(id));
            }
            out.tets[e][size_t(p)] = remap[id];
        }
    }
    std::vector<Tet4>().swap(outT);

    // The edges and corners of the body that the lattice cut across put back, then the nodes on the rim of a flat face put on it (H223: see snapFeatures and snapRims: the order of mesh_tet4's python), before the mending,
    // which keeps them where they are on the surface
    // (H223, measured on the real bracket at 2 mm: with the mender off the spacing of the nodes along the edges costs 5 s, with it on 28 s -- the mender works that much longer on a mesh whose edge nodes were moved before it:
    // the spacing is done after the mender, as the last step, where the tetrahedra it leaves are the ones that count)
    const double secondsCut = lap(clockMark);
    {
        MeshOptions early = opt;
        early.chains = false;
        snapSurface(out, shape, early);
    }
    const double secondsPasses = lap(clockMark);
    if (stopped()) { error = "cancelled"; return false; }
    if (std::getenv("FIELDES_TET_DEBUG")) fprintf(stderr, "[tetmesh] edges: %zu nodes put on an edge or a corner, rims: %zu nodes put on the rim of a flat face, chains: %zu nodes spaced along an edge or a rim, corners: %zu pairs of nodes made one on an inside corner, bites: %zu nodes collapsed into a neighbour, skin: %zu nodes moved along the surface\n", out.edgesSnapped, out.rimsSnapped, out.chainsSmoothed, out.cornersCollapsed, out.bitesCollapsed, out.surfaceSmoothed);

    // The poor tetrahedra of the cut made better (tetmender.hpp): not the raw lattice, which an analysis would solve on, has the sliver at every crease
    if (opt.mend)
    {
        finalizeMesh(out);
        MendOptions mo;
        mo.surface = opt.mendSurface;
        mo.holdPlanes = opt.holdPlanes;
        mo.holdTolerance = 2e-3 * out.h;          // (the tolerance meshShapeMirrored puts the nodes on a plane exactly with)
        mo.surfaceTolerance = opt.surfaceTolerance;
        mo.poor = opt.menderPoor;
        // (H225, experiments without a build: the strength of the mender -- the rounds of its sequence, the sweeps of its smoothing, the mean ratio under which a tetrahedron is poor.  Measured on the real bracket
        // at 3 mm, the rounds and the sweeps change nothing -- the sequence has settled after two -- and the poor threshold is the one that does)
        if (const char* f = std::getenv("FIELDES_TET_MEND_ROUNDS")) mo.rounds = std::atoi(f);
        if (const char* f = std::getenv("FIELDES_TET_MEND_SWEEPS")) mo.smoothSweeps = std::atoi(f);
        if (const char* f = std::getenv("FIELDES_TET_MEND_POOR")) mo.poor = std::atof(f);
        MendStats ms;
        EvalScope evaluators(shape);              // (the evaluators of the shape that the mender asks for its gradients are made once, not for every call)
        if (!mendTets(out, &shape, mo, &ms) && stopped())
        {
            error = "cancelled";
            return false;
        }
        out.mended = ms.swaps + ms.flips + ms.splits;
    }
    finalizeMesh(out);
    const double secondsMender = lap(clockMark);
    if (opt.smooth && opt.chains && (opt.edges || opt.rims))
    {
        EvalScope evaluators(shape);
        out.chainsSmoothed = smoothChains(out, shape, opt);
        if (stopped()) { error = "cancelled"; return false; }
        if (std::getenv("FIELDES_TET_DEBUG")) fprintf(stderr, "[tetmesh] after the mender: chains: %zu nodes spaced along an edge or a rim\n", out.chainsSmoothed);
        finalizeMesh(out);
    }
    const double secondsChains = lap(clockMark);
    if (std::getenv("FIELDES_TET_DEBUG")) fprintf(stderr, "[tetmesh] time of this mesh of %zu tetrahedra: field at the lattice %.2f s, cut and numbering %.2f s, passes of the surface %.2f s, mender %.2f s, spacing along the edges %.2f s, together %.2f s\n",
                                                  out.tets.size(), secondsField, secondsCut, secondsPasses, secondsMender, secondsChains, std::chrono::duration<double>(std::chrono::steady_clock::now() - clock0).count());
    return true;
}

void finalizeMesh(TetMesh& mesh)
{
    // Drop the vertices nobody uses
    {
        std::vector<int> remap(mesh.pos.size(), -1);
        std::vector<Vec3> pos;
        pos.reserve(mesh.pos.size());
        for (auto& t : mesh.tets)
            for (int p = 0; p < 4; ++p)
            {
                int& v = t[size_t(p)];
                if (remap[size_t(v)] < 0)
                {
                    remap[size_t(v)] = int(pos.size());
                    pos.push_back(mesh.pos[size_t(v)]);
                }
                v = remap[size_t(v)];
            }
        mesh.pos.swap(pos);
    }

    if (mesh.pos.size() < (size_t(1) << 21))
    {
        // (H223, performance: the same boundary, found by sorting the faces by one number -- the three nodes packed into 63 bits -- in records of 16 bytes instead of 20 bytes with a triple that is compared a number
        // at a time; finalizeMesh is called a dozen times for a mesh of the real bracket and its sort was the greater part of each call.  The packed number sorts as the sorted triple does, so the faces come out in
        // the same order)
        struct Packed { uint64_t key; int tet; int local; };
        std::vector<Packed> packed;
        packed.reserve(mesh.tets.size() * 4);
        for (size_t e = 0; e < mesh.tets.size(); ++e)
        {
            const auto& t = mesh.tets[e];
            for (int l = 0; l < 4; ++l)
            {
                int f[3], m = 0;
                for (int p = 0; p < 4; ++p) if (p != l) f[m++] = t[size_t(p)];
                std::sort(f, f + 3);
                packed.push_back({(uint64_t(uint32_t(f[0])) << 42) | (uint64_t(uint32_t(f[1])) << 21) | uint64_t(uint32_t(f[2])), int(e), l});
            }
        }
        std::sort(packed.begin(), packed.end(), [](const Packed& x, const Packed& y) { return x.key < y.key; });
        mesh.faces.clear();
        mesh.faceTet.clear();
        mesh.onSurface.assign(mesh.pos.size(), 0);
        const uint64_t mask = (uint64_t(1) << 21) - 1;
        for (size_t r = 0; r < packed.size();)
        {
            size_t s = r + 1;
            while (s < packed.size() && packed[s].key == packed[r].key) ++s;
            if (s - r == 1)
            {
                const Packed& f = packed[r];
                const auto& t = mesh.tets[size_t(f.tet)];
                std::array<int, 3> tri = {int(f.key >> 42), int((f.key >> 21) & mask), int(f.key & mask)};
                const Vec3& q = mesh.pos[size_t(t[size_t(f.local)])];
                const Vec3 nrm = (mesh.pos[size_t(tri[1])] - mesh.pos[size_t(tri[0])])
                                     .cross(mesh.pos[size_t(tri[2])] - mesh.pos[size_t(tri[0])]);
                if (nrm.dot(q - mesh.pos[size_t(tri[0])]) > 0) std::swap(tri[1], tri[2]);
                mesh.faces.push_back(tri);
                mesh.faceTet.push_back(f.tet);
                for (int p = 0; p < 3; ++p) mesh.onSurface[size_t(tri[size_t(p)])] = 1;
            }
            r = s;
        }
        return;
    }

    // The boundary: faces of one tetrahedron only, normals out
    struct FaceRec { int a, b, c; int tet; int local; };
    std::vector<FaceRec> recs;
    recs.reserve(mesh.tets.size() * 4);
    for (size_t e = 0; e < mesh.tets.size(); ++e)
    {
        const auto& t = mesh.tets[e];
        for (int l = 0; l < 4; ++l)
        {
            int f[3], m = 0;
            for (int p = 0; p < 4; ++p) if (p != l) f[m++] = t[size_t(p)];
            std::sort(f, f + 3);
            recs.push_back({f[0], f[1], f[2], int(e), l});
        }
    }
    std::sort(recs.begin(), recs.end(), [](const FaceRec& x, const FaceRec& y) {
        if (x.a != y.a) return x.a < y.a;
        if (x.b != y.b) return x.b < y.b;
        return x.c < y.c;
    });
    mesh.faces.clear();
    mesh.faceTet.clear();
    mesh.onSurface.assign(mesh.pos.size(), 0);
    for (size_t r = 0; r < recs.size();)
    {
        size_t s = r + 1;
        while (s < recs.size() && recs[s].a == recs[r].a && recs[s].b == recs[r].b && recs[s].c == recs[r].c) ++s;
        if (s - r == 1)
        {
            const FaceRec& f = recs[r];
            const auto& t = mesh.tets[size_t(f.tet)];
            std::array<int, 3> tri = {f.a, f.b, f.c};
            const Vec3& q = mesh.pos[size_t(t[size_t(f.local)])];
            const Vec3 nrm = (mesh.pos[size_t(tri[1])] - mesh.pos[size_t(tri[0])])
                                 .cross(mesh.pos[size_t(tri[2])] - mesh.pos[size_t(tri[0])]);
            if (nrm.dot(q - mesh.pos[size_t(tri[0])]) > 0) std::swap(tri[1], tri[2]);
            mesh.faces.push_back(tri);
            mesh.faceTet.push_back(f.tet);
            for (int p = 0; p < 3; ++p) mesh.onSurface[size_t(tri[size_t(p)])] = 1;
        }
        r = s;
    }
}

// H226 (the user, 2026-11-15, three times in an hour: "IF IT LOOKS SYMMETRIC IT IS SYMMETRIC"; "THAT CAN BE CHECKED ON THE FIELD"; "TO TEST ACCURACY OF FUCKING SYMMETRY YOU CAN JUST OVERLAP THE MIRRORS MAKE AN INVERSE INTERSECTION AND THEN MEASURE THE
// VOLUME"; "THIS IS A FIELD PROGRAM USE THE FUCKING FAST FIELDS"): whether the part is symmetric about a plane is a question to the field and is answered with the field, before anything is meshed.  The part and its mirror image (the same field with one
// coordinate flipped about the plane) are overlapped and the region where they DIFFER is made as a field of its own -- inside the one and outside the other, both ways: min(max(f, -g), max(-f, g)) with g the mirrored f.  That region is empty
// for a symmetric part.  Interval arithmetic (the kernel's own evaluator, the one that meshing and rendering use) looks for it: a box over which the field is provably positive has none of it and is dropped, a box over which it is provably
// negative is entirely in it and its volume is added up, and the rest are halved down to half an element.  The plane holds when the volume of what is provably different is under half the volume of a cube of one element: a difference of the
// size of the mesh itself is the least that can be seen on it.  No mesh is looked at; the boxes that the interval test cannot decide at half an element are judged by the field at their centres (below)
static bool fieldSymmetric(const Tree& shape, const Vec3& lo, const Vec3& hi, int axis, double at, double h)
{
    Tree mx = Tree::X(), my = Tree::Y(), mz = Tree::Z();
    const Tree flipped = Tree(float(2.0 * at)) - (axis == 0 ? Tree::X() : (axis == 1 ? Tree::Y() : Tree::Z()));
    if (axis == 0) mx = flipped;
    else if (axis == 1) my = flipped;
    else mz = flipped;
    const Tree g = shape.remap(mx, my, mz);
    const Tree differ = libfive::min(libfive::max(shape, Tree(0.0f) - g), libfive::max(Tree(0.0f) - shape, g));
    IntervalEvaluator ev(differ);
    struct Box { Eigen::Vector3f lo, hi; std::shared_ptr<Tape> tape; };
    std::vector<Box> stack;
    stack.push_back({lo.cast<float>(), hi.cast<float>(), nullptr});
    const float smallest = float(0.5 * h);
    const double allowed = 0.5 * h * h * h;
    double different = 0.0;
    size_t evals = 0;
    const size_t budget = 600000;
    // (the boxes that stay UNDECIDED at half an element -- the interval test can prove neither that they are free of the difference nor that they are in it -- are not dropped: a difference thinner than a box, such as a cylinder 0.5 mm
    // off the plane in elements of 1.8 mm, lies in none of the boxes that are provably different, and the DFG cylinder case was mirrored about it (a drag 24 % too low, found 2026-11-15 by the validation run in the window).  Their centres
    // are looked at: the field of the part against the field of its mirror image, and a box whose two values differ by more than 0.4 of an element counts as different.  A part that is symmetric has the same value at both; the
    // volume that is counted is added to the volume of what is provably different)
    std::vector<Eigen::Vector3f> centres;
    std::vector<double> centreVolumes;
    bool tooMany = false;
    while (!stack.empty())
    {
        if (stopped()) return false;
        const Box b = stack.back();
        stack.pop_back();
        if (++evals > budget) return false;               // (too many boxes at the surface to look at them all: the plane is not used)
        // (the tape of the field is shortened for every box by what the box proves, as the renderer and the mesher do, so that a box deep in the part or far outside it costs almost nothing)
        const std::pair<Interval, std::shared_ptr<Tape>> r = b.tape ? ev.intervalAndPush(b.lo, b.hi, b.tape) : ev.intervalAndPush(b.lo, b.hi);
        const Interval i = r.first;
        if (i.lower() > 0.0f) continue;                   // (provably none of the difference in this box)
        const Eigen::Vector3f size = b.hi - b.lo;
        if (i.upper() < 0.0f)                             // (provably all of this box is different)
        {
            different += double(size.x()) * double(size.y()) * double(size.z());
            if (different >= allowed) return false;
            continue;
        }
        int d = 0;
        const float longest = size.maxCoeff(&d);
        if (longest <= smallest)                          // (not decided at half an element: its centre is looked at below)
        {
            if (centres.size() < 400000)
            {
                centres.push_back(Eigen::Vector3f(0.5f * (b.lo + b.hi)));
                centreVolumes.push_back(double(size.x()) * double(size.y()) * double(size.z()));
            }
            else tooMany = true;
            continue;
        }
        Box c1 = b, c2 = b;
        c1.tape = r.second;
        c2.tape = r.second;
        const float mid = 0.5f * (b.lo[d] + b.hi[d]);
        c1.hi[d] = mid;
        c2.lo[d] = mid;
        stack.push_back(c1);
        stack.push_back(c2);
    }
    if (tooMany) return false;                            // (too many boxes at the surface to look at them all: the plane is not used)
    if (!centres.empty())
    {
        std::vector<Eigen::Vector3f> mirrored(centres);
        for (auto& p : mirrored) p[axis] = float(2.0 * at) - p[axis];
        std::vector<float> fa, fb;
        evalPoints(shape, centres, fa);
        evalPoints(shape, mirrored, fb);
        if (stopped()) return false;
        const double limit = 0.4 * h;
        for (size_t k = 0; k < centres.size(); ++k)
            if (std::abs(double(fa[k]) - double(fb[k])) > limit)
            {
                different += centreVolumes[k];
                if (different >= allowed) return false;
            }
    }
    return true;
}

// Where the surface meets the plane at a SHALLOW angle the half body has a thin wedge along the plane, which no half mesh follows well (the shaft stand at 6.3 mm: 54 plane vertices 0.8 of an element off the surface).  That is a property of the
// field and is read on it, before anything is meshed: the plane's own section is read on a grid of half an element, and where the surface is within one and a quarter elements of a point of it the direction of the surface is asked for;
// when it is within 40 degrees of the plane at three points or more, the plane is not used -- the part is meshed whole, once.  A surface that crosses the plane at a right angle (a rim, a wall) has a normal across the plane's.
// (KNOWN ISSUE, measured on the shaft stand at 6.3 mm: the 54 plane vertices that no crossing could be found for lie 0.80 of an element from a surface that runs PARALLEL to the plane, a wall of 0.8 element beside it, not at a
// shallow angle across it: the limit is three quarters of an element, so the mirrored mesh is accepted with them off the surface and its largest stress is 2.1 where the whole part's is 0.96.  A wider limit (1.5 and 1.25 elements) was
// written and not built; it is parked by the user's order of 2026-11-15, see KNOWN_ISSUES.md)
static size_t shallowContacts(const Tree& shape, const Vec3& lo, const Vec3& hi, int axis, double at, double h)
{
    const int a1 = (axis + 1) % 3, a2 = (axis + 2) % 3;
    double step = 0.5 * h;
    while (((hi[a1] - lo[a1]) / step + 1.0) * ((hi[a2] - lo[a2]) / step + 1.0) > 60000.0) step *= 1.5;
    const size_t n1 = size_t(std::max(1.0, std::ceil((hi[a1] - lo[a1]) / step))), n2 = size_t(std::max(1.0, std::ceil((hi[a2] - lo[a2]) / step)));
    std::vector<Eigen::Vector3f> grid;
    for (size_t i = 0; i <= n1; ++i)
        for (size_t j = 0; j <= n2; ++j)
        {
            Vec3 p;
            p[axis] = at;
            p[a1] = lo[a1] + (hi[a1] - lo[a1]) * double(i) / double(n1);
            p[a2] = lo[a2] + (hi[a2] - lo[a2]) * double(j) / double(n2);
            grid.push_back(p.cast<float>());
        }
    std::vector<float> f;
    evalPoints(shape, grid, f);
    if (stopped()) return 0;
    const double dq = 1e-3 * h;
    std::vector<Eigen::Vector3f> q7;
    size_t counted = 0;
    for (size_t k = 0; k < grid.size(); ++k)
    {
        if (!(std::abs(double(f[k])) < h)) continue;
        const Eigen::Vector3f p = grid[k];
        q7.push_back(p);
        for (int ax = 0; ax < 3; ++ax)
        {
            Eigen::Vector3f lo3 = p, hi3 = p;
            lo3[ax] -= float(dq);
            hi3[ax] += float(dq);
            q7.push_back(lo3);
            q7.push_back(hi3);
        }
        ++counted;
    }
    if (counted == 0) return 0;
    std::vector<float> f7;
    evalPoints(shape, q7, f7);
    if (stopped()) return 0;
    const double cosShallow = std::cos(40.0 * 3.14159265358979323846 / 180.0);
    size_t shallow = 0;
    for (size_t k = 0; k < counted; ++k)
    {
        Vec3 g;
        for (int ax = 0; ax < 3; ++ax) g[ax] = (double(f7[7 * k + size_t(2 + 2 * ax)]) - double(f7[7 * k + size_t(1 + 2 * ax)])) / (2 * dq);
        const double gn = g.norm();
        if (!(gn > 1e-12)) continue;
        if (std::abs(double(f7[7 * k])) / gn > 0.75 * h) continue;
        if (std::abs(g[axis]) / gn > cosShallow) ++shallow;
    }
    return shallow;
}

std::vector<MeshMirror> findMeshMirrors(const Tree& shape, const Eigen::Vector3d& lo, const Eigen::Vector3d& hi, double h)
{
    std::vector<MeshMirror> found;
    const Vec3 size = hi - lo;
    if (!(size.minCoeff() > 0)) return found;
    for (int axis = 0; axis < 3; ++axis)
    {
        const double at = 0.5 * (lo[axis] + hi[axis]);
        bool same = fieldSymmetric(shape, lo, hi, axis, at, h);
        if (stopped()) return found;
        size_t shallow = 0;
        if (same)
        {
            shallow = shallowContacts(shape, lo, hi, axis, at, h);
            if (shallow >= 3) same = false;
        }
        if (same)
        {
            MeshMirror m;
            m.axis = axis;
            m.at = at;
            found.push_back(m);
        }
        if (std::getenv("FIELDES_TET_DEBUG")) fprintf(stderr, "[tetmesh] symmetry about the middle of the region along axis %d (at %.4f): %s (the part against its mirror image by interval arithmetic; %zu points of the plane where the surface is within 40 degrees of it)\n", axis, at, same ? "found" : "no", shallow);
    }
    return found;
}

bool meshShapeMirrored(const Tree& shape, const Eigen::Vector3d& lo, const Eigen::Vector3d& hi, double h, const std::vector<MeshMirror>& planesIn,
                       TetMesh& out, std::string& error, const MeshOptions& optIn)
{
    std::vector<MeshMirror> planes;
    for (const auto& p : planesIn)
    {
        if (p.axis < 0 || p.axis > 2) continue;
        bool twice = false;
        for (const auto& q : planes) twice = twice || q.axis == p.axis;
        // (a plane counts only when it runs through the middle of the region: the mesh of the upper side then mirrors onto the whole of the lower side's region)
        if (twice || std::abs(0.5 * (lo[p.axis] + hi[p.axis]) - p.at) > 1e-6 * h) continue;
        // (and only when each half is at least two elements across: a half of one element, the octant of a 10 mm beam at element size 5, is meshed with two cubes of half the size along that axis and one of the
        // full size along the others, a lattice stretched 2 to 1 that loses a layer of 0.7 mm at the faces across the long axis: the 10-node cantilever of the known answers, v1, filled 9,708 of 10,000 mm^3 and
        // went 43 % off the beam theory.  Such a part is meshed whole along that axis, with cubes, as it is when it is not symmetric)
        if (hi[p.axis] - p.at < 2.0 * h) continue;
        planes.push_back(p);
    }
    const bool debug = std::getenv("FIELDES_TET_DEBUG") != nullptr;
    // The mesh of the planes `use`: the shape on the upper side of every plane, over the region cut at the planes, meshed; the plane vertices that belong to the surface moved onto it and the boundary nodes of the half that
    // stand off the surface repaired; then each plane in turn: the vertices on it are put exactly on it and shared, every other vertex gets its image, every tetrahedron its mirror image (two vertices swapped: the other
    // orientation); and every volume must be positive.  1 made, 0 a tetrahedron without a volume, -1 cancelled
    auto attempt = [&](const std::vector<MeshMirror>& use, TetMesh& result) -> int {
        Tree part = shape;
        Vec3 lo2 = lo;
        for (const auto& p : use)
        {
            const Tree axis = p.axis == 0 ? Tree::X() : (p.axis == 1 ? Tree::Y() : Tree::Z());
            part = libfive::max(part, Tree(p.at) - axis);
            lo2[p.axis] = p.at;
        }
        TetMesh cur;
        std::string halfError;
        // (the half is mended, but not on its mirror planes: a node on a plane stays where it is and no face with such a node is flipped or taken into a neighbour, so that the mirror image
        // continues the mesh)
        MeshOptions halfOptions = optIn;
        for (const auto& p : use) halfOptions.holdPlanes.emplace_back(p.axis, p.at);
        if (!meshShape(part, lo2, hi, h, cur, halfError, halfOptions)) return stopped() ? -1 : 0;
        // The half mesh on the planes.  The vertices on a plane are put exactly on it, and so are those a hair off it that belong to a face of the plane (a face of the plane has all
        // three vertices in it).  Then the creases, where a plane meets the shape's surface: the mesh of the whole has no crease there -- the surface runs straight on across the plane --
        // but the half mesh has a corner that its lattice rounds: its boundary faces cut the corner by up to half an element, and a vertex on the plane that is not on the surface is a notch
        // in the whole (the mirror image cannot continue it: the boundary vertices "off the shape" of the check below).  Every vertex on a plane that belongs to a face of the surface is
        // moved along the plane onto the surface (Newton steps on the shape's field, the gradient taken along the axes the vertex is free to move on): it lands on the crease line, and
        // its mirror image continues it
        const double snap = 2e-3 * h;
        {
            std::vector<unsigned char> bit(cur.pos.size(), 0);
            for (size_t k = 0; k < use.size(); ++k)
                for (size_t i = 0; i < cur.pos.size(); ++i)
                    if (std::abs(cur.pos[i][use[k].axis] - use[k].at) <= snap)
                    {
                        cur.pos[i][use[k].axis] = use[k].at;
                        bit[i] |= (unsigned char)(1u << k);
                    }
            auto on = [&](int v, size_t k) { return ((bit[size_t(v)] >> k) & 1u) != 0; };
            for (int pass = 0; pass < 3; ++pass)
            {
                size_t moved = 0;
                for (const auto& f : cur.faces)
                    for (size_t k = 0; k < use.size(); ++k)
                    {
                        int count = 0, other = -1;
                        for (int q = 0; q < 3; ++q)
                        {
                            if (on(f[size_t(q)], k)) ++count;
                            else other = f[size_t(q)];
                        }
                        if (count != 2 || std::abs(cur.pos[size_t(other)][use[k].axis] - use[k].at) > 0.05 * h) continue;
                        cur.pos[size_t(other)][use[k].axis] = use[k].at;
                        bit[size_t(other)] |= (unsigned char)(1u << k);
                        ++moved;
                    }
                if (moved == 0) break;
            }
            std::vector<unsigned char> crease(cur.pos.size(), 0);
            for (const auto& f : cur.faces)
            {
                bool planeFace = false;
                for (size_t k = 0; k < use.size() && !planeFace; ++k) planeFace = on(f[0], k) && on(f[1], k) && on(f[2], k);
                if (planeFace) continue;
                for (int q = 0; q < 3; ++q)
                    if (bit[size_t(f[size_t(q)])]) crease[size_t(f[size_t(q)])] = 1;
            }
            struct Mover { size_t v; Vec3 along; Vec3 start; bool done; };
            std::vector<Mover> movers;
            for (size_t i = 0; i < cur.pos.size(); ++i)
            {
                if (!crease[i]) continue;
                Vec3 along = Vec3::Ones();                      // (1 on the axes the vertex can move on: not those of the planes it is on)
                for (size_t k = 0; k < use.size(); ++k)
                    if (on(int(i), k)) along[use[k].axis] = 0.0;
                if (along.squaredNorm() == 0.0) continue;       // (on all three planes: a corner of the quarter, held where it is)
                movers.push_back({i, along, cur.pos[i], false});
            }
            // Which of them are not on the surface (the distance from the field and its gradient, over 1e-3 h), then for those the crossing nearest to each: along 16 directions in the plane (two,
            // along the line, on the edge of two planes) the field is looked at in steps of a twentieth of an element out to an element, where it changes side the crossing is found by halving.
            // Only the side of the field is used there, not its gradient: a field made of parts, or a distance that is not one, has a side everywhere
            const double dd = 1e-3 * h;
            const int steps = 20;
            const double stepLen = 0.05 * h;
            size_t onSurface = 0, found = 0, lost = 0;
            double farthest = 0;
            {
                std::vector<Eigen::Vector3f> pts;
                for (const auto& mv : movers)
                {
                    const Vec3 p = cur.pos[mv.v];
                    pts.push_back(p.cast<float>());
                    for (int ax = 0; ax < 3; ++ax)
                    {
                        Vec3 lo3 = p, hi3 = p;
                        lo3[ax] -= dd;
                        hi3[ax] += dd;
                        pts.push_back(lo3.cast<float>());
                        pts.push_back(hi3.cast<float>());
                    }
                }
                std::vector<float> vals;
                evalPoints(shape, pts, vals);
                if (stopped()) return -1;
                std::vector<size_t> todo;
                for (size_t a = 0; a < movers.size(); ++a)
                {
                    Vec3 g;
                    for (int ax = 0; ax < 3; ++ax) g[ax] = (double(vals[7 * a + size_t(2 + 2 * ax)]) - double(vals[7 * a + size_t(1 + 2 * ax)])) / (2 * dd);
                    const double gn = g.norm();
                    // (a thousandth of an element: the vertices of the plane that are not ON the surface are put on it exactly.  H226 tried to leave those within a third and then two thirds of the tolerance alone: it made
                    // the two vertices at the top and the bottom of the large circle of the real bracket's face -- 0.98 mm off the surface, not 0.05 -- stay, and the mirror was refused; the original number is back)
                    if (gn > 1e-12 && std::abs(double(vals[7 * a])) / gn <= 1e-3 * h) ++onSurface;
                    else todo.push_back(a);
                }
                // the samples: the vertex, then each direction's steps
                std::vector<std::vector<Vec3>> dirsOf(todo.size());
                std::vector<size_t> first(todo.size() + 1, 0);
                pts.clear();
                for (size_t m = 0; m < todo.size(); ++m)
                {
                    first[m] = pts.size();
                    const Mover& mv = movers[todo[m]];
                    const Vec3 p = cur.pos[mv.v];
                    pts.push_back(p.cast<float>());
                    int ax[3], nax = 0;
                    for (int q = 0; q < 3; ++q)
                        if (mv.along[q] != 0.0) ax[nax++] = q;
                    auto& dv = dirsOf[m];
                    if (nax == 1)
                    {
                        Vec3 e = Vec3::Zero();
                        e[ax[0]] = 1.0;
                        dv.push_back(e);
                        dv.push_back(-e);
                    }
                    else
                        for (int k = 0; k < 16; ++k)
                        {
                            const double th = k * (2.0 * 3.14159265358979323846 / 16.0);
                            Vec3 e = Vec3::Zero();
                            e[ax[0]] = std::cos(th);
                            e[ax[1]] = std::sin(th);
                            dv.push_back(e);
                        }
                    for (const auto& e : dv)
                        for (int j = 1; j <= steps; ++j) pts.push_back((p + (j * stepLen) * e).cast<float>());
                }
                first[todo.size()] = pts.size();
                evalPoints(shape, pts, vals);
                if (stopped()) return -1;
                // the nearest change of side: the direction and the step it happens in
                std::vector<int> bestDir(todo.size(), -1), bestStep(todo.size(), steps + 1);
                for (size_t m = 0; m < todo.size(); ++m)
                {
                    const bool inside0 = vals[first[m]] <= 0;
                    for (size_t d = 0; d < dirsOf[m].size(); ++d)
                        for (int j = 1; j <= steps && j < bestStep[m]; ++j)
                            if ((vals[first[m] + 1 + d * size_t(steps) + size_t(j - 1)] <= 0) != inside0)
                            {
                                bestDir[m] = int(d);
                                bestStep[m] = j;
                                break;
                            }
                }
                // halving between the step before and the step
                std::vector<double> loT(todo.size(), 0.0), hiT(todo.size(), 0.0);
                std::vector<char> inside0(todo.size(), 0);
                for (size_t m = 0; m < todo.size(); ++m)
                {
                    inside0[m] = vals[first[m]] <= 0;
                    if (bestDir[m] < 0) continue;
                    loT[m] = (bestStep[m] - 1) * stepLen;
                    hiT[m] = bestStep[m] * stepLen;
                }
                for (int it = 0; it < 28; ++it)
                {
                    std::vector<size_t> who;
                    pts.clear();
                    for (size_t m = 0; m < todo.size(); ++m)
                    {
                        if (bestDir[m] < 0) continue;
                        who.push_back(m);
                        pts.push_back((cur.pos[movers[todo[m]].v] + 0.5 * (loT[m] + hiT[m]) * dirsOf[m][size_t(bestDir[m])]).cast<float>());
                    }
                    if (who.empty()) break;
                    evalPoints(shape, pts, vals);
                    if (stopped()) return -1;
                    for (size_t k = 0; k < who.size(); ++k)
                    {
                        const size_t m = who[k];
                        const double mid = 0.5 * (loT[m] + hiT[m]);
                        if ((vals[k] <= 0) == (inside0[m] != 0)) loT[m] = mid;
                        else hiT[m] = mid;
                    }
                }
                for (size_t m = 0; m < todo.size(); ++m)
                {
                    if (bestDir[m] < 0)
                    {
                        ++lost;
                        continue;
                    }
                    // (the point on the side of the vertex's own end of the bracket: the surface to 1e-8 h)
                    const double t = 0.5 * (loT[m] + hiT[m]);
                    cur.pos[movers[todo[m]].v] += t * dirsOf[m][size_t(bestDir[m])];
                    farthest = std::max(farthest, t);
                    ++found;
                }
            }
            // (H226, 2026-11-15: a vertex that was moved along its plane onto the surface can turn a tetrahedron round it inside out -- eight tetrahedra at the bottom of the large circle of the real bracket's face, where the passes had
            // put the nodes of the plane at the rim already and this step moved them again by up to 0.7 of an element: the whole mirror was refused for it.  The move is taken back half way, a quarter of the way, then
            // altogether, as soon as every tetrahedron round the vertex has a volume)
            size_t shortened = 0, undone = 0;
            {
                std::vector<int> moverOf(cur.pos.size(), -1);
                for (size_t a = 0; a < movers.size(); ++a) moverOf[movers[a].v] = int(a);
                std::vector<std::vector<int>> tetsOf(movers.size());
                for (size_t e = 0; e < cur.tets.size(); ++e)
                    for (int q = 0; q < 4; ++q)
                    {
                        const int a = moverOf[size_t(cur.tets[e][size_t(q)])];
                        if (a >= 0) tetsOf[size_t(a)].push_back(int(e));
                    }
                auto valid = [&](size_t a) {
                    for (int e : tetsOf[a])
                    {
                        const auto& t = cur.tets[size_t(e)];
                        if (!(signedVolume(cur.pos[size_t(t[0])], cur.pos[size_t(t[1])], cur.pos[size_t(t[2])], cur.pos[size_t(t[3])]) > 0)) return false;
                    }
                    return true;
                };
                std::vector<char> boundaryNode(cur.pos.size(), 0);           // (the vertices of the boundary faces of the half: they are not followers)
                for (const auto& f : cur.faces)
                    for (int q = 0; q < 3; ++q)
                        if (f[size_t(q)] >= 0 && size_t(f[size_t(q)]) < boundaryNode.size()) boundaryNode[size_t(f[size_t(q)])] = 1;
                for (size_t a = 0; a < movers.size(); ++a)
                {
                    if (valid(a)) continue;
                    const size_t v = movers[a].v;
                    const Vec3 end = cur.pos[v], start = movers[a].start;
                    // (taken back altogether, not half way: a vertex half way to the surface is as far from it as the move was long, where at its start it was nearer than the tolerance -- measured on the bracket:
                    // two wall vertices at the large circle stood 0.05 mm off the surface, the move went 1 mm along the plane, was shortened to half and left them 0.49 mm off)
                    // (H226, the user, 2026-11-15: "WHAT DO YOU MEAN CANNOT BE PLACED?? WHAT IS STOPPING IT?": the vertex was moved alone and the move taken back when a tetrahedron beside it turned inside out, where the edge pass of the same mesher
                    // moves a node together with the nodes round it.  It goes with them here: the full move, three quarters of it and half of it, each with the nodes round the vertex that stand in no plane and on no surface going half as far, and the largest
                    // one that leaves every tetrahedron that holds the vertex or one of them whole and not poorer than 0.1 is kept; the move is taken back only when none does)
                    bool fixedIt = false;
                    const Vec3 delta = end - start;
                    std::vector<int> followers;
                    {
                        std::set<int> seenNode;
                        for (int e : tetsOf[a])
                            for (int q = 0; q < 4; ++q)
                            {
                                const int w = cur.tets[size_t(e)][size_t(q)];
                                if (size_t(w) == v || !seenNode.insert(w).second) continue;
                                if (bit[size_t(w)] == 0 && !boundaryNode[size_t(w)]) followers.push_back(w);
                            }
                    }
                    const double scales[3] = {1.0, 0.75, 0.5};
                    for (int si = 0; si < 3 && !fixedIt; ++si)
                    {
                        const double s = scales[si];
                        std::vector<Vec3> saved;
                        for (int w : followers) saved.push_back(cur.pos[size_t(w)]);
                        cur.pos[v] = start + s * delta;
                        for (int w : followers) cur.pos[size_t(w)] += 0.5 * s * delta;
                        std::vector<char> touched(cur.pos.size(), 0);
                        touched[v] = 1;
                        for (int w : followers) touched[size_t(w)] = 1;
                        bool ok = true;
                        for (const auto& t : cur.tets)
                        {
                            bool hit = false;
                            for (int q = 0; q < 4; ++q) hit = hit || touched[size_t(t[size_t(q)])] != 0;
                            if (!hit) continue;
                            if (!(signedVolume(cur.pos[size_t(t[0])], cur.pos[size_t(t[1])], cur.pos[size_t(t[2])], cur.pos[size_t(t[3])]) > 0) || meanRatio(cur.pos, t) < 0.1)
                            {
                                ok = false;
                                break;
                            }
                        }
                        if (ok)
                        {
                            fixedIt = true;
                            ++shortened;
                        }
                        else
                        {
                            cur.pos[v] = start;
                            for (size_t k = 0; k < followers.size(); ++k) cur.pos[size_t(followers[k])] = saved[k];
                        }
                    }
                    if (!fixedIt)
                    {
                        cur.pos[v] = start;
                        ++undone;
                    }
                    if (debug) fprintf(stderr, "[tetmesh]   a plane vertex whose move was spoiling a tetrahedron: from (%.4f %.4f %.4f) toward (%.4f %.4f %.4f), %s (%.4f %.4f %.4f), %zu nodes round it followed\n", start.x(), start.y(), start.z(), end.x(), end.y(), end.z(),
                                       fixedIt ? "moved with its neighbours to" : "left at", cur.pos[v].x(), cur.pos[v].y(), cur.pos[v].z(), followers.size());
                }
            }
            if (debug) fprintf(stderr, "[tetmesh] mirror: %zu vertices on a plane belong to the surface: %zu on it already, %zu moved onto it (farthest %.3f h), %zu with no crossing within an element; %zu moves made with the nodes round the vertex following and %zu undone for a tetrahedron without a volume\n",
                               movers.size(), onSurface, found, farthest / h, lost, shortened, undone);
        }
        // (H226, the user, 2026-11-15: "NO NEED TO MESH SOMETHING AND THEN THROW AWAY THE MESH AND THEN RERUN THE MESH".  The mirrored mesh used to be checked against the surface and, when one node stood off it by more than the tolerance, thrown away and
        // the whole part meshed again; the symmetry is decided on the field now (findMeshMirrors) and nothing is checked on a mesh.  What is left to do on the half is to REPAIR it: a node of its boundary, not on a plane, that stands
        // farther from the surface than the tolerance of the whole mesher is put on the surface by Newton steps on the field -- when the move is not longer than an element and every tetrahedron round the node stays whole and
        // not poorer than 0.05 -- before the half is copied, so that its image is right as well.  The plane vertices that could not be put on the surface above stay, and the debug output says how many)
        {
            auto onAPlane = [&](const Vec3& p) {
                for (const auto& q : use)
                    if (std::abs(p[q.axis] - q.at) <= snap) return true;
                return false;
            };
            std::vector<char> mark(cur.pos.size(), 0);
            std::vector<size_t> ids;
            for (const auto& f : cur.faces)
                for (int q = 0; q < 3; ++q)
                {
                    const size_t w = size_t(f[size_t(q)]);
                    if (w < mark.size() && !mark[w] && !onAPlane(cur.pos[w]))
                    {
                        mark[w] = 1;
                        ids.push_back(w);
                    }
                }
            const double dr = 1e-3 * h;
            auto readField = [&](const std::vector<size_t>& who, std::vector<double>& value, std::vector<Vec3>& gradient) {
                std::vector<Eigen::Vector3f> q;
                for (size_t w : who)
                {
                    const Vec3 p = cur.pos[w];
                    q.push_back(p.cast<float>());
                    for (int ax = 0; ax < 3; ++ax)
                    {
                        Vec3 lo3 = p, hi3 = p;
                        lo3[ax] -= dr;
                        hi3[ax] += dr;
                        q.push_back(lo3.cast<float>());
                        q.push_back(hi3.cast<float>());
                    }
                }
                std::vector<float> fv;
                evalPoints(shape, q, fv);
                value.assign(who.size(), 0.0);
                gradient.assign(who.size(), Vec3::Zero());
                for (size_t k = 0; k < who.size(); ++k)
                {
                    value[k] = double(fv[7 * k]);
                    for (int ax = 0; ax < 3; ++ax) gradient[k][ax] = (double(fv[7 * k + size_t(2 + 2 * ax)]) - double(fv[7 * k + size_t(1 + 2 * ax)])) / (2 * dr);
                }
            };
            std::vector<double> value;
            std::vector<Vec3> gradient;
            readField(ids, value, gradient);
            if (stopped()) return -1;
            std::vector<double> norms;
            for (const auto& g : gradient)
                if (g.norm() > 1e-12) norms.push_back(g.norm());
            double gUsual = 0.0;
            if (!norms.empty())
            {
                std::nth_element(norms.begin(), norms.begin() + std::ptrdiff_t(norms.size() / 2), norms.end());
                gUsual = norms[norms.size() / 2];
            }
            std::vector<size_t> bad;
            for (size_t k = 0; k < ids.size(); ++k)
            {
                const double gn = std::max(gradient[k].norm(), gUsual);
                if (gn > 1e-12 && std::abs(value[k]) / gn > optIn.surfaceTolerance * h) bad.push_back(ids[k]);
            }
            size_t repaired = 0, kept = 0;
            if (!bad.empty())
            {
                std::vector<Vec3> start(bad.size());
                for (size_t k = 0; k < bad.size(); ++k) start[k] = cur.pos[bad[k]];
                for (int it = 0; it < 3; ++it)
                {
                    readField(bad, value, gradient);
                    if (stopped()) return -1;
                    for (size_t k = 0; k < bad.size(); ++k)
                    {
                        const double g2 = gradient[k].squaredNorm();
                        if (g2 > 1e-24) cur.pos[bad[k]] -= (value[k] / g2) * gradient[k];
                    }
                }
                std::vector<int> badIndex(cur.pos.size(), -1);
                for (size_t k = 0; k < bad.size(); ++k) badIndex[bad[k]] = int(k);
                std::vector<char> ok(bad.size(), 1);
                // (the quality of the tetrahedra round each node as it was, to compare: a node is put back when a tetrahedron round it loses its volume or falls under 0.05)
                for (const auto& t : cur.tets)
                {
                    bool touches = false;
                    for (int q = 0; q < 4; ++q) touches = touches || badIndex[size_t(t[size_t(q)])] >= 0;
                    if (!touches) continue;
                    const bool whole = signedVolume(cur.pos[size_t(t[0])], cur.pos[size_t(t[1])], cur.pos[size_t(t[2])], cur.pos[size_t(t[3])]) > 0 && meanRatio(cur.pos, t) >= 0.05;
                    if (whole) continue;
                    for (int q = 0; q < 4; ++q)
                        if (badIndex[size_t(t[size_t(q)])] >= 0) ok[size_t(badIndex[size_t(t[size_t(q)])])] = 0;
                }
                for (size_t k = 0; k < bad.size(); ++k)
                {
                    if ((cur.pos[bad[k]] - start[k]).norm() > h) ok[k] = 0;
                    if (!ok[k])
                    {
                        cur.pos[bad[k]] = start[k];
                        ++kept;
                    }
                    else ++repaired;
                }
            }
            if (debug) fprintf(stderr, "[tetmesh] mirror: %zu boundary nodes of the half off the plane: %zu stood off the surface, %zu put on it, %zu could not be\n", ids.size(), bad.size(), repaired, kept);
        }
        for (const auto& p : use)
        {
            const size_t n = cur.pos.size();
            std::vector<int> image(n, -1);
            for (size_t i = 0; i < n; ++i)
                if (std::abs(cur.pos[i][p.axis] - p.at) <= snap)
                {
                    cur.pos[i][p.axis] = p.at;
                    image[i] = int(i);
                }
            for (size_t i = 0; i < n; ++i)
            {
                if (image[i] >= 0) continue;
                Vec3 q = cur.pos[i];
                q[p.axis] = 2.0 * p.at - q[p.axis];
                image[i] = int(cur.pos.size());
                cur.pos.push_back(q);
            }
            const size_t nt = cur.tets.size();
            cur.tets.reserve(2 * nt);
            for (size_t e = 0; e < nt; ++e)
            {
                const std::array<int, 4> t = cur.tets[e];
                cur.tets.push_back({image[size_t(t[0])], image[size_t(t[1])], image[size_t(t[3])], image[size_t(t[2])]});
            }
            cur.degenerate *= 2;
            cur.featureVertices *= 2;
            cur.featuresReverted *= 2;
        }
        {
            // (H226: the refusal said nothing, so that a mirror that failed for a tetrahedron without a volume looked like one that failed for nothing at all: the count and the first place are printed)
            size_t noVolume = 0;
            Vec3 first = Vec3::Zero();
            for (const auto& t : cur.tets)
                if (!(signedVolume(cur.pos[size_t(t[0])], cur.pos[size_t(t[1])], cur.pos[size_t(t[2])], cur.pos[size_t(t[3])]) > 0))
                {
                    if (noVolume == 0) first = 0.25 * (cur.pos[size_t(t[0])] + cur.pos[size_t(t[1])] + cur.pos[size_t(t[2])] + cur.pos[size_t(t[3])]);
                    ++noVolume;
                }
            if (noVolume > 0)
            {
                if (debug) fprintf(stderr, "[tetmesh] mirror about %zu plane(s) does not hold: %zu tetrahedra of the mirrored mesh have no volume, the first at (%.3f %.3f %.3f)\n", use.size(), noVolume, first.x(), first.y(), first.z());
                return 0;
            }
        }
        {
            // The tetrahedra in the order of the lattice sweep (the cell of the centre, z then y then x): the vertices are numbered by their first use, and the incomplete LU of a flow wants the
            // neighbours of a vertex near it in the numbering, as the plain lattice has them -- the half mesh and its images are two or four or eight sweeps of their own, whose vertices
            // would be numbered in blocks that are far apart (an optimiser iteration of example 17 took half as long again on the mirrored mesh).  Stable, so the same mesh every time
            const size_t nt = cur.tets.size();
            std::vector<std::array<int64_t, 3>> key(nt);
            for (size_t e = 0; e < nt; ++e)
            {
                Vec3 c = Vec3::Zero();
                for (int q = 0; q < 4; ++q) c += cur.pos[size_t(cur.tets[e][size_t(q)])];
                c *= 0.25;
                key[e] = {int64_t(std::floor((c.z() - lo.z()) / h)), int64_t(std::floor((c.y() - lo.y()) / h)), int64_t(std::floor((c.x() - lo.x()) / h))};
            }
            std::vector<size_t> order(nt);
            for (size_t e = 0; e < nt; ++e) order[e] = e;
            std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return key[a] < key[b]; });
            std::vector<std::array<int, 4>> sorted(nt);
            for (size_t e = 0; e < nt; ++e) sorted[e] = cur.tets[order[e]];
            cur.tets.swap(sorted);
        }
        finalizeMesh(cur);
        // (cur.h is the spacing the half was fitted to, see meshShape)
        cur.mirrored = use.size();
        result = std::move(cur);
        return 1;
    };
    // One attempt with every plane that the field has: the symmetry is decided on the field (findMeshMirrors), so there is no subset of the planes to try and no second meshing of the half.  The only way left to a refusal is
    // a tetrahedron of the mirrored mesh without a volume, which a mesh made right does not have: the part is then meshed whole, and the line says so on the console, not only in the debug output
    if (!planes.empty())
    {
        TetMesh result;
        const int r = attempt(planes, result);
        if (r < 0)
        {
            error = "cancelled";
            return false;
        }
        if (r > 0)
        {
            if (debug) fprintf(stderr, "[tetmesh] mirrored about %zu plane(s)\n", planes.size());
            out = std::move(result);
            return true;
        }
        fprintf(stderr, "[tetmesh] the mirrored mesh has tetrahedra without a volume: the part is meshed whole\n");
    }
    return meshShape(shape, lo, hi, h, out, error, optIn);
}

MeshQuality meshQuality(const TetMesh& mesh)
{
    MeshQuality q;
    q.tets = mesh.tets.size();
    q.vertices = mesh.pos.size();
    q.boundaryFaces = mesh.faces.size();
    q.minDihedral = 180;
    q.maxDihedral = 0;
    q.minVolume = std::numeric_limits<double>::max();
    double sumMin = 0;
    const double nominal = mesh.h * mesh.h * mesh.h / 12.0;
    for (const auto& t : mesh.tets)
    {
        const Vec3 p[4] = {mesh.pos[size_t(t[0])], mesh.pos[size_t(t[1])], mesh.pos[size_t(t[2])],
                           mesh.pos[size_t(t[3])]};
        const double vol = signedVolume(p[0], p[1], p[2], p[3]);
        if (vol <= 0) q.inverted++;
        q.volume += vol;
        q.minVolume = std::min(q.minVolume, vol / nominal);
        // outward unit normals of the faces opposite each vertex
        Vec3 nrm[4];
        bool ok = true;
        for (int i = 0; i < 4; ++i)
        {
            int f[3], m = 0;
            for (int k = 0; k < 4; ++k) if (k != i) f[m++] = k;
            Vec3 v = (p[f[1]] - p[f[0]]).cross(p[f[2]] - p[f[0]]);
            const double len = v.norm();
            if (!(len > 0)) { ok = false; break; }
            v /= len;
            if (v.dot(p[i] - p[f[0]]) > 0) v = -v;
            nrm[i] = v;
        }
        if (!ok) continue;
        double tetMin = 180;
        for (int i = 0; i < 4; ++i)
            for (int j = i + 1; j < 4; ++j)
            {
                const double c = std::max(-1.0, std::min(1.0, nrm[i].dot(nrm[j])));
                const double deg = 180.0 - std::acos(c) * 180.0 / 3.14159265358979323846;
                q.minDihedral = std::min(q.minDihedral, deg);
                q.maxDihedral = std::max(q.maxDihedral, deg);
                tetMin = std::min(tetMin, deg);
            }
        sumMin += tetMin;
    }
    if (!mesh.tets.empty()) q.meanMinDihedral = sumMin / double(mesh.tets.size());
    for (const auto& f : mesh.faces)
        q.boundarySurfaceArea += 0.5 * (mesh.pos[size_t(f[1])] - mesh.pos[size_t(f[0])])
                                           .cross(mesh.pos[size_t(f[2])] - mesh.pos[size_t(f[0])]).norm();
    // tetrahedra folded over a shared face
    {
        struct Rec { int a, b, c; int tet; int local; };
        std::vector<Rec> recs;
        recs.reserve(mesh.tets.size() * 4);
        for (size_t e = 0; e < mesh.tets.size(); ++e)
            for (int l = 0; l < 4; ++l)
            {
                int f[3], m = 0;
                for (int p = 0; p < 4; ++p) if (p != l) f[m++] = mesh.tets[e][size_t(p)];
                std::sort(f, f + 3);
                recs.push_back({f[0], f[1], f[2], int(e), l});
            }
        std::sort(recs.begin(), recs.end(), [](const Rec& x, const Rec& y) {
            if (x.a != y.a) return x.a < y.a;
            if (x.b != y.b) return x.b < y.b;
            return x.c < y.c;
        });
        for (size_t r = 0; r + 1 < recs.size(); ++r)
        {
            if (!(recs[r].a == recs[r + 1].a && recs[r].b == recs[r + 1].b && recs[r].c == recs[r + 1].c)) continue;
            const Vec3 &f0 = mesh.pos[size_t(recs[r].a)], &f1 = mesh.pos[size_t(recs[r].b)], &f2 = mesh.pos[size_t(recs[r].c)];
            const Vec3 n = (f1 - f0).cross(f2 - f0);
            const Vec3& o1 = mesh.pos[size_t(mesh.tets[size_t(recs[r].tet)][size_t(recs[r].local)])];
            const Vec3& o2 = mesh.pos[size_t(mesh.tets[size_t(recs[r + 1].tet)][size_t(recs[r + 1].local)])];
            if (n.dot(o1 - f0) * n.dot(o2 - f0) > 0) q.folded++;
        }
    }
    // faces shared by more than two tetrahedra, and boundary edges not shared by two boundary faces
    {
        std::vector<std::array<int, 3>> all;
        all.reserve(mesh.tets.size() * 4);
        for (const auto& t : mesh.tets)
            for (int l = 0; l < 4; ++l)
            {
                std::array<int, 3> f;
                int m = 0;
                for (int p = 0; p < 4; ++p) if (p != l) f[size_t(m++)] = t[size_t(p)];
                std::sort(f.begin(), f.end());
                all.push_back(f);
            }
        std::sort(all.begin(), all.end());
        for (size_t r = 0; r < all.size();)
        {
            size_t s = r + 1;
            while (s < all.size() && all[s] == all[r]) ++s;
            if (s - r > 2) q.badFaces++;
            r = s;
        }
        std::vector<uint64_t> edges;
        edges.reserve(mesh.faces.size() * 3);
        for (const auto& f : mesh.faces)
            for (int p = 0; p < 3; ++p)
                edges.push_back(edgeKey(Id(f[size_t(p)]), Id(f[size_t((p + 1) % 3)])));
        std::sort(edges.begin(), edges.end());
        for (size_t r = 0; r < edges.size();)
        {
            size_t s = r + 1;
            while (s < edges.size() && edges[s] == edges[r]) ++s;
            if (s - r != 2) q.openEdges++;
            r = s;
        }
    }
    return q;
}

bool meshFromArrays(size_t vertexCount, const double* positions, size_t tetCount, const int32_t* tets, TetMesh& out, std::string& error)
{
    out = TetMesh();
    if (vertexCount == 0 || tetCount == 0 || !positions || !tets)
    {
        error = "the mesh has no tetrahedra";
        return false;
    }
    out.pos.resize(vertexCount);
    for (size_t i = 0; i < vertexCount; ++i)
    {
        const double x = positions[3 * i], y = positions[3 * i + 1], z = positions[3 * i + 2];
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
        {
            error = "node " + std::to_string(i) + " of the mesh has a coordinate that is not a number";
            return false;
        }
        out.pos[i] = Vec3(x, y, z);
    }
    out.tets.reserve(tetCount);
    for (size_t e = 0; e < tetCount; ++e)
    {
        std::array<int, 4> t;
        for (size_t a = 0; a < 4; ++a)
        {
            const int32_t v = tets[4 * e + a];
            if (v < 0 || size_t(v) >= vertexCount)
            {
                error = "tetrahedron " + std::to_string(e) + " of the mesh names node " + std::to_string(v) + ", and the mesh has " +
                        std::to_string(vertexCount) + " nodes";
                return false;
            }
            t[a] = v;
        }
        const Vec3& p0 = out.pos[size_t(t[0])];
        const double det = (out.pos[size_t(t[1])] - p0).cross(out.pos[size_t(t[2])] - p0).dot(out.pos[size_t(t[3])] - p0);
        double reach = 0;
        for (size_t a = 1; a < 4; ++a) reach = std::max(reach, (out.pos[size_t(t[a])] - p0).norm());
        if (!(std::abs(det) > 1e-12 * reach * reach * reach))
        {
            ++out.degenerate;
            continue;
        }
        if (det < 0) std::swap(t[2], t[3]);
        out.tets.push_back(t);
    }
    if (out.tets.empty())
    {
        error = "every tetrahedron of the mesh has no volume";
        return false;
    }

    // A face that more than two tetrahedra share is a mesh whose elements cross (or a node in the middle of another element's face): a solver would give an answer
    // for it, and a wrong one
    {
        std::vector<std::array<int, 3>> faces;
        faces.reserve(out.tets.size() * 4);
        for (const auto& t : out.tets)
            for (int l = 0; l < 4; ++l)
            {
                std::array<int, 3> f;
                int m = 0;
                for (int p = 0; p < 4; ++p) if (p != l) f[size_t(m++)] = t[size_t(p)];
                std::sort(f.begin(), f.end());
                faces.push_back(f);
            }
        std::sort(faces.begin(), faces.end());
        size_t shared = 0;
        for (size_t r = 0; r < faces.size();)
        {
            size_t s = r + 1;
            while (s < faces.size() && faces[s] == faces[r]) ++s;
            if (s - r > 2) ++shared;
            r = s;
        }
        if (shared)
        {
            error = std::to_string(shared) + " faces of the mesh belong to more than two tetrahedra: its elements cross each other";
            return false;
        }
    }

    // The size of an element: the middle edge of a sample of the tetrahedra
    {
        std::vector<double> lengths;
        const size_t step = std::max<size_t>(1, out.tets.size() / 50000);
        for (size_t e = 0; e < out.tets.size(); e += step)
        {
            const auto& t = out.tets[e];
            for (size_t a = 0; a < 3; ++a)
                for (size_t b = a + 1; b < 4; ++b)
                    lengths.push_back((out.pos[size_t(t[a])] - out.pos[size_t(t[b])]).norm());
        }
        std::nth_element(lengths.begin(), lengths.begin() + std::ptrdiff_t(lengths.size() / 2), lengths.end());
        out.h = lengths[lengths.size() / 2];
    }
    finalizeMesh(out);
    return true;
}

}   // namespace fea
}   // namespace libfive
