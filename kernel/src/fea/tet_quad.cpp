/*
libfive: a CAD kernel for modeling with implicit functions

The 10-node tetrahedron; see tet_quad.hpp.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#include "tet_quad.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <unordered_map>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace libfive {
namespace fea {
namespace tet {

namespace {

// The corners of the six edges, in the order of an element's nodes 4 .. 9
const int kEdge[6][2] = {{0, 1}, {0, 2}, {0, 3}, {1, 2}, {1, 3}, {2, 3}};

// The edge between corners a < b
int edgeIndex(int a, int b)
{
    for (int e = 0; e < 6; ++e)
        if (kEdge[e][0] == a && kEdge[e][1] == b) return e;
    return -1;
}

// The four-point rule (exact for a quadratic times a constant: what a straight-sided element's stiffness is): the points in natural
// coordinates (the barycentric coordinates 1 2 3), their weight (a sixth of the volume of the reference tetrahedron over four), and the
// shape functions' derivatives there
struct Rule
{
    double xi[4][3];
    double w;
    double N[4][10];
    double dN[4][10][3];
};

void shapeValues(const double xi[3], double N[10])
{
    const double l[4] = {1.0 - xi[0] - xi[1] - xi[2], xi[0], xi[1], xi[2]};
    for (int i = 0; i < 4; ++i) N[i] = l[i] * (2.0 * l[i] - 1.0);
    for (int e = 0; e < 6; ++e) N[4 + e] = 4.0 * l[kEdge[e][0]] * l[kEdge[e][1]];
}

Rule makeRule()
{
    Rule r;
    const double a = 0.5854101966249685, b = 0.1381966011250105;
    const double xi[4][3] = {{b, b, b}, {a, b, b}, {b, a, b}, {b, b, a}};
    r.w = 1.0 / 24.0;
    for (int g = 0; g < 4; ++g)
    {
        for (int k = 0; k < 3; ++k) r.xi[g][k] = xi[g][k];
        shapeValues(r.xi[g], r.N[g]);
        quadShapeDerivatives(r.xi[g], r.dN[g]);
    }
    return r;
}

const Rule& rule()
{
    static const Rule r = makeRule();
    return r;
}

// The shape functions' derivatives with respect to x, y, z at a point of an element whose nodes are at X, and the volume factor; false if the
// element is turned inside out there
bool globalDerivatives(const double X[10][3], const double dN[10][3], double dNdx[10][3], double& detJ)
{
    Eigen::Matrix3d J = Eigen::Matrix3d::Zero();
    for (int n = 0; n < 10; ++n)
        for (int i = 0; i < 3; ++i)
            for (int k = 0; k < 3; ++k) J(i, k) += X[n][i] * dN[n][k];
    detJ = J.determinant();
    if (!(detJ > 0)) return false;
    const Eigen::Matrix3d Ji = J.inverse();
    for (int n = 0; n < 10; ++n)
        for (int i = 0; i < 3; ++i) dNdx[n][i] = dN[n][0] * Ji(0, i) + dN[n][1] * Ji(1, i) + dN[n][2] * Ji(2, i);
    return true;
}

void gather(const QuadMesh& q, size_t e, double X[10][3])
{
    for (int n = 0; n < 10; ++n)
    {
        const Vec3& p = q.pos[size_t(q.elem[e][size_t(n)])];
        X[n][0] = p.x();
        X[n][1] = p.y();
        X[n][2] = p.z();
    }
}

}   // anonymous namespace

const double kNodeXi[10][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1},
                               {0.5, 0, 0}, {0, 0.5, 0}, {0, 0, 0.5}, {0.5, 0.5, 0}, {0.5, 0, 0.5}, {0, 0.5, 0.5}};

// An element is cut into the four tetrahedra at its corners and the four that fill the octahedron between its midside nodes; the octahedron
// has three diagonals (the nodes 4-9, 5-8, 6-7: the edges 01 and 23, 02 and 13, 03 and 12), and the four tetrahedra turn round the one that is cut by
const int kSubTets[3][8][4] = {
    {{0, 4, 5, 6}, {1, 4, 7, 8}, {2, 5, 7, 9}, {3, 6, 8, 9}, {4, 9, 5, 6}, {4, 9, 6, 8}, {4, 9, 8, 7}, {4, 9, 7, 5}},
    {{0, 4, 5, 6}, {1, 4, 7, 8}, {2, 5, 7, 9}, {3, 6, 8, 9}, {5, 8, 4, 6}, {5, 8, 6, 9}, {5, 8, 9, 7}, {5, 8, 7, 4}},
    {{0, 4, 5, 6}, {1, 4, 7, 8}, {2, 5, 7, 9}, {3, 6, 8, 9}, {6, 7, 4, 5}, {6, 7, 5, 9}, {6, 7, 9, 8}, {6, 7, 8, 4}},
};

int QuadMesh::midNode(size_t e, int a, int b) const
{
    int la = -1, lb = -1;
    for (int p = 0; p < 4; ++p)
    {
        if (elem[e][size_t(p)] == a) la = p;
        if (elem[e][size_t(p)] == b) lb = p;
    }
    if (la < 0 || lb < 0 || la == lb) return -1;
    const int idx = edgeIndex(std::min(la, lb), std::max(la, lb));
    return elem[e][size_t(4 + idx)];
}

void quadShapeDerivatives(const double xi[3], double dN[10][3])
{
    const double l[4] = {1.0 - xi[0] - xi[1] - xi[2], xi[0], xi[1], xi[2]};
    static const double dl[4][3] = {{-1, -1, -1}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    for (int i = 0; i < 4; ++i)
        for (int k = 0; k < 3; ++k) dN[i][k] = (4.0 * l[i] - 1.0) * dl[i][k];
    for (int e = 0; e < 6; ++e)
    {
        const int a = kEdge[e][0], b = kEdge[e][1];
        for (int k = 0; k < 3; ++k) dN[4 + e][k] = 4.0 * (l[b] * dl[a][k] + l[a] * dl[b][k]);
    }
}

bool quadGlobalDerivatives(const QuadMesh& q, size_t e, const double dN[10][3], double dNdx[10][3], double& detJ)
{
    double X[10][3];
    gather(q, e, X);
    return globalDerivatives(X, dN, dNdx, detJ);
}

void quadIntegrals(const QuadMesh& q, size_t e, double volume[1], double intN[10], double intGrad[10][3])
{
    const Rule& R = rule();
    double X[10][3];
    gather(q, e, X);
    volume[0] = 0;
    for (int n = 0; n < 10; ++n)
    {
        intN[n] = 0;
        for (int k = 0; k < 3; ++k) intGrad[n][k] = 0;
    }
    for (int g = 0; g < 4; ++g)
    {
        double dNdx[10][3], detJ;
        if (!globalDerivatives(X, R.dN[g], dNdx, detJ)) continue;
        const double w = R.w * detJ;
        volume[0] += w;
        for (int n = 0; n < 10; ++n)
        {
            intN[n] += w * R.N[g][n];
            for (int k = 0; k < 3; ++k) intGrad[n][k] += w * dNdx[n][k];
        }
    }
}

void quadVolumes(const QuadMesh& q, std::vector<double>& volumes)
{
    const size_t ne = q.elem.size();
    volumes.assign(ne, 0.0);
    parallelRange(ne, [&](size_t b0, size_t b1) {
        const Rule& R = rule();
        for (size_t e = b0; e < b1; ++e)
        {
            double X[10][3];
            gather(q, e, X);
            double v = 0;
            for (int g = 0; g < 4; ++g)
            {
                double dNdx[10][3], detJ;
                if (globalDerivatives(X, R.dN[g], dNdx, detJ)) v += R.w * detJ;
            }
            volumes[e] = v;
        }
    }, 256);
}

Vec6 quadStrainAt(const QuadMesh& q, size_t e, const double xi[3], const double ue[30])
{
    double dN[10][3], dNdx[10][3], detJ;
    quadShapeDerivatives(xi, dN);
    Vec6 eps = Vec6::Zero();
    if (!quadGlobalDerivatives(q, e, dN, dNdx, detJ)) return eps;
    for (int n = 0; n < 10; ++n)
    {
        const double dx = dNdx[n][0], dy = dNdx[n][1], dz = dNdx[n][2];
        const double ux = ue[3 * n], uy = ue[3 * n + 1], uz = ue[3 * n + 2];
        eps[0] += dx * ux;
        eps[1] += dy * uy;
        eps[2] += dz * uz;
        eps[3] += dy * ux + dx * uy;
        eps[4] += dz * uy + dy * uz;
        eps[5] += dz * ux + dx * uz;
    }
    return eps;
}

////////////////////////////////////////////////////////////////////////////////
// The matrix

void buildPatternQuad(const QuadMesh& q, Assembly& A)
{
    const size_t nn = q.pos.size(), ne = q.elem.size();
    A.geom.clear();
    A.vtStart.assign(nn + 1, 0);
    A.vtList.assign(10 * ne, 0);
    for (const auto& el : q.elem)
        for (int p = 0; p < 10; ++p) A.vtStart[size_t(el[size_t(p)]) + 1]++;
    for (size_t i = 0; i < nn; ++i) A.vtStart[i + 1] += A.vtStart[i];
    {
        std::vector<uint32_t> cursor(A.vtStart.begin(), A.vtStart.end() - 1);
        for (size_t e = 0; e < ne; ++e)
            for (int p = 0; p < 10; ++p) A.vtList[cursor[size_t(q.elem[e][size_t(p)])]++] = uint32_t(e);
    }
    A.K.n = nn;
    A.K.rowPtr.assign(nn + 1, 0);
    // (two passes, so that no row holds a list of its own while the others are made: the neighbours of a node are counted, then written)
    std::vector<uint32_t> count(nn, 0);
    auto neighbours = [&](size_t i, std::vector<uint32_t>& v) {
        v.clear();
        for (uint32_t e = A.vtStart[i]; e < A.vtStart[i + 1]; ++e)
            for (int p = 0; p < 10; ++p) v.push_back(uint32_t(q.elem[A.vtList[e]][size_t(p)]));
        std::sort(v.begin(), v.end());
        v.erase(std::unique(v.begin(), v.end()), v.end());
    };
    parallelRange(nn, [&](size_t b0, size_t b1) {
        std::vector<uint32_t> v;
        for (size_t i = b0; i < b1; ++i)
        {
            neighbours(i, v);
            count[i] = uint32_t(v.size());
        }
    }, 64);
    for (size_t i = 0; i < nn; ++i) A.K.rowPtr[i + 1] = A.K.rowPtr[i] + count[i];
    A.K.col.resize(A.K.rowPtr[nn]);
    parallelRange(nn, [&](size_t b0, size_t b1) {
        std::vector<uint32_t> v;
        for (size_t i = b0; i < b1; ++i)
        {
            neighbours(i, v);
            std::copy(v.begin(), v.end(), A.K.col.begin() + A.K.rowPtr[i]);
        }
    }, 64);
    A.K.val.assign(size_t(A.K.rowPtr[nn]) * 9, 0.0);
}

void assembleQuad(const QuadMesh& q, const Mat6& D, const std::vector<double>* scale, Assembly& A)
{
    const size_t nn = q.pos.size();
    // Row by row, as for the linear tetrahedra (no two threads write one block): every element at a node adds its stiffness rows there
    parallelRange(nn, [&](size_t b0, size_t b1) {
        const Rule& R = rule();
        for (size_t i = b0; i < b1; ++i)
        {
            for (uint32_t e = A.K.rowPtr[i]; e < A.K.rowPtr[i + 1]; ++e)
                std::fill(A.K.val.begin() + std::ptrdiff_t(size_t(e) * 9), A.K.val.begin() + std::ptrdiff_t(size_t(e) * 9 + 9), 0.0);
            for (uint32_t ee = A.vtStart[i]; ee < A.vtStart[i + 1]; ++ee)
            {
                const size_t el = A.vtList[ee];
                const auto& nodes = q.elem[el];
                int li = 0;
                for (int p = 0; p < 10; ++p) if (size_t(nodes[size_t(p)]) == i) li = p;
                double X[10][3];
                gather(q, el, X);
                const double s = scale ? (*scale)[el] : 1.0;
                Eigen::Matrix3d acc[10];
                for (auto& m : acc) m.setZero();
                for (int g = 0; g < 4; ++g)
                {
                    double dNdx[10][3], detJ;
                    if (!globalDerivatives(X, R.dN[g], dNdx, detJ)) continue;
                    const double w = R.w * detJ * s;
                    Eigen::Matrix<double, 6, 3> Bi;
                    auto fill = [&](Eigen::Matrix<double, 6, 3>& B, const double* d) {
                        B.setZero();
                        B(0, 0) = d[0];
                        B(1, 1) = d[1];
                        B(2, 2) = d[2];
                        B(3, 0) = d[1]; B(3, 1) = d[0];
                        B(4, 1) = d[2]; B(4, 2) = d[1];
                        B(5, 0) = d[2]; B(5, 2) = d[0];
                    };
                    fill(Bi, dNdx[li]);
                    const Eigen::Matrix<double, 3, 6> BtD = (w * Bi.transpose()) * D;
                    for (int lj = 0; lj < 10; ++lj)
                    {
                        Eigen::Matrix<double, 6, 3> Bj;
                        fill(Bj, dNdx[lj]);
                        acc[lj] += BtD * Bj;
                    }
                }
                for (int lj = 0; lj < 10; ++lj)
                {
                    const uint32_t j = uint32_t(nodes[size_t(lj)]);
                    const auto pos = std::lower_bound(A.K.col.begin() + A.K.rowPtr[i], A.K.col.begin() + A.K.rowPtr[i + 1], j) - A.K.col.begin();
                    double* blk = &A.K.val[size_t(pos) * 9];
                    for (int r = 0; r < 3; ++r)
                        for (int c = 0; c < 3; ++c) blk[3 * r + c] += acc[lj](r, c);
                }
            }
        }
    }, 16);
}

////////////////////////////////////////////////////////////////////////////////
// The mesh

double availableMemory()
{
#ifdef _WIN32
    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof(ms);
    return GlobalMemoryStatusEx(&ms) ? double(ms.ullAvailPhys) : 0.0;
#else
    const long pages = sysconf(_SC_AVPHYS_PAGES), size = sysconf(_SC_PAGESIZE);
    return pages > 0 && size > 0 ? double(pages) * double(size) : 0.0;
#endif
}

namespace {

// Whether element e is not turned inside out, or squeezed, anywhere that matters: at its nodes and the points of the rule, compared with the
// straight-sided element on its four corners
bool elementSound(const QuadMesh& q, size_t e)
{
    double X[10][3];
    gather(q, e, X);
    const Vec3 a(X[1][0] - X[0][0], X[1][1] - X[0][1], X[1][2] - X[0][2]);
    const Vec3 b(X[2][0] - X[0][0], X[2][1] - X[0][1], X[2][2] - X[0][2]);
    const Vec3 c(X[3][0] - X[0][0], X[3][1] - X[0][1], X[3][2] - X[0][2]);
    const double straight = a.dot(b.cross(c));
    if (!(straight > 0)) return false;
    const Rule& R = rule();
    double dN[10][3], dNdx[10][3], detJ;
    for (int g = 0; g < 4; ++g)
    {
        if (!globalDerivatives(X, R.dN[g], dNdx, detJ) || detJ < 0.15 * straight) return false;
    }
    for (int n = 0; n < 10; ++n)
    {
        quadShapeDerivatives(kNodeXi[n], dN);
        if (!globalDerivatives(X, dN, dNdx, detJ) || detJ < 0.15 * straight) return false;
    }
    return true;
}

}   // anonymous namespace

bool buildQuadMesh(const TetMesh& m, const Tree& shape, QuadMesh& q, std::string& error)
{
    const size_t nv = m.pos.size(), nt = m.tets.size();
    if (nt == 0 || nv == 0)
    {
        error = "the part has no tetrahedra";
        return false;
    }
    if (nt > size_t(300000000))
    {
        error = "the mesh is too large for the 10-node elements: use a larger element_size";
        return false;
    }

    // The edges: every one of the 6 nt (tetrahedron, edge) pairs by its two vertices, sorted, and numbered
    struct Ref { uint64_t key; uint32_t slot; };
    std::vector<Ref> refs(6 * nt);
    parallelRange(nt, [&](size_t b0, size_t b1) {
        for (size_t t = b0; t < b1; ++t)
            for (int l = 0; l < 6; ++l)
            {
                const uint64_t a = uint64_t(m.tets[t][size_t(kEdge[l][0])]), b = uint64_t(m.tets[t][size_t(kEdge[l][1])]);
                refs[6 * t + size_t(l)] = {(std::min(a, b) << 32) | std::max(a, b), uint32_t(6 * t + size_t(l))};
            }
    }, 256);
    std::sort(refs.begin(), refs.end(), [](const Ref& x, const Ref& y) { return x.key < y.key; });
    std::vector<uint64_t> keys;
    keys.reserve(refs.size() / 5 + 16);
    std::vector<uint32_t> edgeOfSlot(6 * nt);
    for (size_t r = 0; r < refs.size(); ++r)
    {
        if (r == 0 || refs[r].key != refs[r - 1].key) keys.push_back(refs[r].key);
        edgeOfSlot[refs[r].slot] = uint32_t(keys.size() - 1);
    }
    std::vector<Ref>().swap(refs);
    const size_t ne = keys.size();

    q.corners = nv;
    q.pos.resize(nv + ne);
    std::copy(m.pos.begin(), m.pos.end(), q.pos.begin());
    for (size_t i = 0; i < ne; ++i)
        q.pos[nv + i] = 0.5 * (m.pos[size_t(keys[i] >> 32)] + m.pos[size_t(keys[i] & 0xffffffffu)]);
    q.elem.resize(nt);
    parallelRange(nt, [&](size_t b0, size_t b1) {
        for (size_t t = b0; t < b1; ++t)
        {
            for (int p = 0; p < 4; ++p) q.elem[t][size_t(p)] = m.tets[t][size_t(p)];
            for (int l = 0; l < 6; ++l) q.elem[t][size_t(4 + l)] = int(nv + edgeOfSlot[6 * t + size_t(l)]);
        }
    }, 256);
    q.moved = q.straightened = 0;

    // The midside nodes of the boundary's edges go onto the surface: along the mean normal of the boundary faces that have the edge, to where
    // the part's field is zero (found by bisection between a third of the edge's length out and in; an edge the surface does not cross there
    // keeps its middle)
    std::vector<char> movedFlag(ne, 0);
    {
        std::unordered_map<uint32_t, Vec3> normals;
        auto edgeId = [&](int a, int b) -> uint32_t {
            const uint64_t key = (uint64_t(std::min(a, b)) << 32) | uint64_t(std::max(a, b));
            const auto it = std::lower_bound(keys.begin(), keys.end(), key);
            return it != keys.end() && *it == key ? uint32_t(it - keys.begin()) : 0xffffffffu;
        };
        for (const auto& f : m.faces)
        {
            const Vec3 n = (m.pos[size_t(f[1])] - m.pos[size_t(f[0])]).cross(m.pos[size_t(f[2])] - m.pos[size_t(f[0])]);
            const double len = n.norm();
            if (!(len > 0)) continue;
            for (int p = 0; p < 3; ++p)
            {
                const uint32_t id = edgeId(f[size_t(p)], f[size_t((p + 1) % 3)]);
                if (id == 0xffffffffu) continue;
                auto it = normals.find(id);
                if (it == normals.end()) normals.emplace(id, n / len);
                else it->second += n / len;
            }
        }
        std::vector<uint32_t> ids;
        std::vector<Vec3> p0, dir;
        std::vector<double> reach;
        ids.reserve(normals.size());
        for (const auto& kv : normals)
        {
            const double len = kv.second.norm();
            if (!(len > 1e-9)) continue;
            const uint32_t id = kv.first;
            const Vec3 a = m.pos[size_t(keys[id] >> 32)], b = m.pos[size_t(keys[id] & 0xffffffffu)];
            ids.push_back(id);
            p0.push_back(q.pos[nv + id]);
            dir.push_back(kv.second / len);
            reach.push_back(0.3 * (b - a).norm());
        }
        const size_t M = ids.size();
        if (M > 0)
        {
            std::vector<double> lo(M), hi(M);
            std::vector<float> flo(M), fhi(M), fm(M);
            std::vector<Eigen::Vector3f> pts(M);
            auto evalAt = [&](const std::vector<double>& s, std::vector<float>& out) {
                for (size_t i = 0; i < M; ++i) pts[i] = (p0[i] + s[i] * dir[i]).cast<float>();
                evalTreePoints(shape, pts, out);
            };
            for (size_t i = 0; i < M; ++i) { lo[i] = -reach[i]; hi[i] = reach[i]; }
            evalAt(lo, flo);
            evalAt(hi, fhi);
            std::vector<char> active(M, 0);
            size_t count = 0;
            for (size_t i = 0; i < M; ++i)
            {
                active[i] = std::isfinite(flo[i]) && std::isfinite(fhi[i]) && ((flo[i] <= 0) != (fhi[i] <= 0));
                count += active[i] != 0;
            }
            if (count > 0)
            {
                std::vector<double> mid(M);
                for (int it = 0; it < 14; ++it)
                {
                    for (size_t i = 0; i < M; ++i) mid[i] = 0.5 * (lo[i] + hi[i]);
                    evalAt(mid, fm);
                    for (size_t i = 0; i < M; ++i)
                    {
                        if (!active[i]) continue;
                        if ((fm[i] <= 0) == (flo[i] <= 0)) { lo[i] = mid[i]; flo[i] = fm[i]; }
                        else hi[i] = mid[i];
                    }
                }
                for (size_t i = 0; i < M; ++i)
                {
                    if (!active[i]) continue;
                    q.pos[nv + ids[i]] = p0[i] + 0.5 * (lo[i] + hi[i]) * dir[i];
                    movedFlag[ids[i]] = 1;
                }
            }
        }
    }

    // An element that the move has turned inside out, or squeezed, has its moved nodes put back in the middle of their edges; the elements that
    // share such a node are looked at again
    {
        std::vector<uint32_t> candidates;
        for (size_t t = 0; t < nt; ++t)
            for (int l = 0; l < 6; ++l)
                if (movedFlag[size_t(q.elem[t][size_t(4 + l)]) - nv]) { candidates.push_back(uint32_t(t)); break; }
        for (int pass = 0; pass < 6 && !candidates.empty(); ++pass)
        {
            std::vector<char> bad(candidates.size(), 0);
            parallelRange(candidates.size(), [&](size_t b0, size_t b1) {
                for (size_t c = b0; c < b1; ++c) bad[c] = !elementSound(q, candidates[c]);
            }, 64);
            bool any = false;
            for (size_t c = 0; c < candidates.size(); ++c)
            {
                if (!bad[c]) continue;
                const size_t t = candidates[c];
                for (int l = 0; l < 6; ++l)
                {
                    const size_t id = size_t(q.elem[t][size_t(4 + l)]) - nv;
                    if (!movedFlag[id]) continue;
                    q.pos[nv + id] = 0.5 * (m.pos[size_t(keys[id] >> 32)] + m.pos[size_t(keys[id] & 0xffffffffu)]);
                    movedFlag[id] = 0;
                    ++q.straightened;
                    any = true;
                }
            }
            if (!any) break;
            std::vector<uint32_t> again;
            for (uint32_t t : candidates)
                for (int l = 0; l < 6; ++l)
                    if (movedFlag[size_t(q.elem[t][size_t(4 + l)]) - nv]) { again.push_back(t); break; }
            candidates.swap(again);
        }
    }
    for (char f : movedFlag) q.moved += f != 0;

    // The diagonal of the octahedron in the middle of each element that makes the better tetrahedra: the shortest
    q.diagonal.assign(nt, 0);
    parallelRange(nt, [&](size_t b0, size_t b1) {
        static const int pairs[3][2] = {{4, 9}, {5, 8}, {6, 7}};
        for (size_t t = b0; t < b1; ++t)
        {
            double best = std::numeric_limits<double>::max();
            for (int d = 0; d < 3; ++d)
            {
                const double len = (q.pos[size_t(q.elem[t][size_t(pairs[d][0])])] - q.pos[size_t(q.elem[t][size_t(pairs[d][1])])]).squaredNorm();
                if (len < best)
                {
                    best = len;
                    q.diagonal[t] = uint8_t(d);
                }
            }
        }
    }, 256);
    return true;
}

std::shared_ptr<TetMesh> refinedMesh(const TetMesh& c, const QuadMesh& q)
{
    auto r = std::make_shared<TetMesh>();
    const size_t ne = q.elem.size();
    r->pos = q.pos;
    r->h = c.h * 0.5;
    r->degenerate = c.degenerate;
    r->featureVertices = c.featureVertices;
    r->featuresReverted = c.featuresReverted;
    r->tets.resize(8 * ne);
    parallelRange(ne, [&](size_t b0, size_t b1) {
        for (size_t e = b0; e < b1; ++e)
        {
            const int (*sub)[4] = kSubTets[q.diagonal[e]];
            for (int s = 0; s < 8; ++s)
            {
                std::array<int, 4> t;
                for (int p = 0; p < 4; ++p) t[size_t(p)] = q.elem[e][size_t(sub[s][p])];
                // (positively oriented, as a linear mesh's tetrahedra are)
                const Vec3 a = q.pos[size_t(t[1])] - q.pos[size_t(t[0])], b = q.pos[size_t(t[2])] - q.pos[size_t(t[0])],
                           cc = q.pos[size_t(t[3])] - q.pos[size_t(t[0])];
                if (a.dot(b.cross(cc)) < 0) std::swap(t[2], t[3]);
                r->tets[8 * e + size_t(s)] = t;
            }
        }
    }, 256);
    // The boundary: each face of the linear mesh in four, the same way round, each in the tetrahedron that has it
    r->onSurface.assign(r->pos.size(), 0);
    r->faces.reserve(4 * c.faces.size());
    r->faceTet.reserve(4 * c.faces.size());
    for (size_t f = 0; f < c.faces.size(); ++f)
    {
        const auto& tri = c.faces[f];
        const size_t e = size_t(c.faceTet[f]);
        const int a = tri[0], b = tri[1], cc = tri[2];
        const int mab = q.midNode(e, a, b), mbc = q.midNode(e, b, cc), mca = q.midNode(e, cc, a);
        if (mab < 0 || mbc < 0 || mca < 0) continue;
        auto corner = [&](int v) {
            for (int p = 0; p < 4; ++p) if (q.elem[e][size_t(p)] == v) return p;
            return 0;
        };
        const int (*sub)[4] = kSubTets[q.diagonal[e]];
        int middle = 4;
        for (int s = 4; s < 8; ++s)
        {
            int found = 0;
            for (int p = 0; p < 4; ++p)
            {
                const int node = q.elem[e][size_t(sub[s][p])];
                found += node == mab || node == mbc || node == mca;
            }
            if (found == 3) { middle = s; break; }
        }
        const std::array<int, 3> faces[4] = {{a, mab, mca}, {b, mbc, mab}, {cc, mca, mbc}, {mab, mbc, mca}};
        const int owners[4] = {corner(a), corner(b), corner(cc), middle};
        for (int k = 0; k < 4; ++k)
        {
            r->faces.push_back(faces[k]);
            r->faceTet.push_back(int(8 * e + size_t(owners[k])));
            for (int p = 0; p < 3; ++p) r->onSurface[size_t(faces[k][size_t(p)])] = 1;
        }
    }
    return r;
}

}   // namespace tet
}   // namespace fea
}   // namespace libfive
