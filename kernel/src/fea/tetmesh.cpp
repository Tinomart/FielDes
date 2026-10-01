/*
libfive: a CAD kernel for modeling with implicit functions

Tetrahedral meshing of an implicit shape by isosurface stuffing; see
tetmesh.hpp.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <unordered_map>
#include <unordered_set>

#include "libfive/fea/tetmesh.hpp"
#include "libfive/fea/fea.hpp"
#include "libfive/eval/eval_array.hpp"

namespace libfive {
namespace fea {

namespace {

using Vec3 = Eigen::Vector3d;
using Id = uint32_t;
using Tet4 = std::array<Id, 4>;

// The body-centred cubic lattice: the corners of nx * ny * nz cubes, then
// their centres.  A vertex's id is its index in that order.
struct Lattice
{
    int nx = 0, ny = 0, nz = 0;
    Vec3 origin = Vec3::Zero();
    double h = 1;
    size_t nL = 0, nC = 0;

    size_t corner(int i, int j, int k) const { return (size_t(k) * (ny + 1) + j) * (nx + 1) + i; }
    size_t centre(int i, int j, int k) const { return nL + (size_t(k) * ny + j) * nx + i; }
    size_t total() const { return nL + nC; }

    Vec3 pos(size_t id) const
    {
        if (id < nL)
        {
            const size_t i = id % (nx + 1), j = (id / (nx + 1)) % (ny + 1), k = id / (size_t(nx + 1) * (ny + 1));
            return origin + h * Vec3(double(i), double(j), double(k));
        }
        id -= nL;
        const size_t i = id % nx, j = (id / nx) % ny, k = id / (size_t(nx) * ny);
        return origin + h * Vec3(i + 0.5, j + 0.5, k + 0.5);
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

// A tree's values at many points (on all cores)
void evalTreePoints(const Tree& tree, const std::vector<Eigen::Vector3f>& pts, std::vector<float>& out)
{
    out.resize(pts.size());
    const size_t N = ArrayEvaluator::N;
    parallelRange((pts.size() + N - 1) / N, [&](size_t b0, size_t b1) {
        ArrayEvaluator e(tree);
        for (size_t b = b0; b < b1; ++b)
        {
            const size_t start = b * N;
            const size_t count = std::min(N, pts.size() - start);
            for (size_t k = 0; k < count; ++k) e.set(pts[start + k], k);
            const auto vs = e.values(count);
            for (size_t k = 0; k < count; ++k) out[start + k] = vs(k);
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
void findBadTets(const std::vector<Tet4>& tets, Pos&& position, double h, std::vector<size_t>& bad)
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
        if (std::abs(signedVolume(p[0], p[1], p[2], p[3])) < 0.004 * nominal) isBad[e] = 1;
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
                        if (deg < 12.0 || deg > 160.0) { isBad[e] = 1; break; }
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

}   // anonymous namespace

bool meshShape(const Tree& shape, const Eigen::Vector3d& lo, const Eigen::Vector3d& hi,
               double h, TetMesh& out, std::string& error, const MeshOptions& optIn)
{
    MeshOptions opt = optIn;
    // (experiments: FIELDES_TET_REACH, FIELDES_TET_FEATURES=0)
    if (const char* r = std::getenv("FIELDES_TET_REACH")) opt.featureReach = std::atof(r);
    if (const char* f = std::getenv("FIELDES_TET_FEATURES")) opt.features = std::atoi(f) != 0;
    out = TetMesh();
    if (!(h > 0))
    {
        error = "the element size must be positive";
        return false;
    }
    const Vec3 size = hi - lo;
    Lattice L;
    L.h = h;
    int n[3];
    double cells = 1;
    for (int a = 0; a < 3; ++a)
    {
        n[a] = std::max(3, int(std::ceil(size[a] / h - 1e-9)) + 2);
        cells *= n[a];
    }
    // (one cube of padding on every side: the lattice planes coincide with lo + k h)
    if (cells > 80e6)
    {
        error = "too many elements: use a larger element size";
        return false;
    }
    L.nx = n[0];
    L.ny = n[1];
    L.nz = n[2];
    L.origin = lo - Vec3(h, h, h);
    L.nL = size_t(L.nx + 1) * (L.ny + 1) * (L.nz + 1);
    L.nC = size_t(L.nx) * L.ny * L.nz;
    const size_t nTotal = L.total();
    out.h = h;

    // The shape's field at every lattice vertex, in blocks
    std::vector<float> val(nTotal);
    {
        const size_t block = size_t(1) << 21;
        std::vector<Eigen::Vector3f> pts;
        std::vector<float> v;
        for (size_t start = 0; start < nTotal; start += block)
        {
            const size_t count = std::min(block, nTotal - start);
            pts.resize(count);
            for (size_t q = 0; q < count; ++q) pts[q] = L.pos(start + q).cast<float>();
            evalPoints(shape, pts, v);
            std::copy(v.begin(), v.end(), val.begin() + std::ptrdiff_t(start));
        }
    }
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
    forEachTet(L, [&](Id a, Id b, Id c, Id d) {
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
            const double len = h * (sameKind ? 1.0 : 0.8660254037844386);
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
        size_t dbgNear = 0, dbgUse = 0, dbgSharp = 0, dbgReach = 0, dbgOn = 0;
        for (size_t v = 0; v < nTotal; ++v)
        {
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
            fprintf(stderr, "[tetmesh] features: %zu vertices with >= 2 crossings, %zu with >= 2 near, %zu sharp, %zu within reach, %zu on the surface\n",
                    dbgNear, dbgUse, dbgSharp, dbgReach, dbgOn);
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

        for (const Tet4& t : inside) emit(t[0], t[1], t[2], t[3]);
        for (const Tet4& t : mixed)
        {
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
        if (feature.empty()) break;
        std::vector<size_t> bad;
        findBadTets(outT, position, h, bad);
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

    finalizeMesh(out);
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

}   // namespace fea
}   // namespace libfive
