/*
libfive: a CAD kernel for modeling with implicit functions

Smoothed-aggregation algebraic multigrid, the preconditioner that conjugate gradients escalates to when the block-Jacobi one stalls (H193: a thin plate of
ten-node tetrahedra took 20000 iterations and did not finish).  See tet_common.hpp.

The method is the one of Vanek, Mandel and Brezina (Computing 56, 1996), with the near-null space of elasticity -- the six rigid-body modes -- in the
tentative prolongator, as it is done for the elasticity problems of Trilinos' ML and of PyAMG:
  1. the nodes of the matrix graph are cut into AGGREGATES (a root and its neighbours, the leftovers attached to a neighbouring aggregate);
  2. in each aggregate the rigid-body modes, restricted to its nodes, are made orthonormal (a rank-revealing QR): those columns are the tentative
     prolongator P0, and the R factors are the rigid-body modes of the next level;
  3. P = (I - w D^-1 A) P0: one damped-Jacobi step smooths the columns (w = 4 / (3 rho(D^-1 A)), D the block diagonal);
  4. the coarse matrix is the Galerkin product P^T A P;
  5. the same again on the coarse matrix, down to a few thousand unknowns, which a sparse LDL^T solves.
A cycle smooths with damped block-Jacobi before and after the correction from the coarser level, so the cycle is symmetric and positive definite and
conjugate gradients may use it.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#include "tet_common.hpp"

#include <Eigen/Sparse>
#include <Eigen/SparseCholesky>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <new>

namespace libfive {
namespace fea {
namespace tet {

namespace {

using Sp = Eigen::SparseMatrix<double, Eigen::RowMajor, int>;
using SpC = Eigen::SparseMatrix<double, Eigen::ColMajor, int>;
using Mat = Eigen::MatrixXd;
using Vec = Eigen::VectorXd;

const size_t kChunkRows = 3072;         // the rows of a matrix that one thread multiplies at a time
const size_t kCoarsest = 2500;          // the level with so few unknowns is solved directly
const size_t kDirectLimit = 14000;      // ... or the finest level that is, when it will not coarsen further
const size_t kMaxLevels = 14;

// (an index as a call, so that `Sp M(ix(rows), ix(cols));` is never read as the declaration of a function)
inline Eigen::Index ix(size_t v) { return Eigen::Index(v); }

void each(size_t n, size_t serialBelow, const std::function<void(size_t, size_t)>& fn, size_t minChunk)
{
    if (n < serialBelow)
    {
        if (n) fn(0, n);
        return;
    }
    parallelRange(n, fn, minChunk);
}

}   // anonymous namespace

struct AmgLevel
{
    size_t n = 0;                               // unknowns
    std::vector<size_t> nodeStart;              // the first unknown of each node (a node is a block of unknowns: 3 on the finest level, up to 6 on the others), nodes + 1 entries
    Sp A;                                       // the matrix (levels 1 ...; level 0 is the Assembly's)
    Sp P, R;                                    // prolongation (n x the next level's n) and its transpose
    std::vector<double> dinv;                   // the inverse of each node's diagonal block (row-major, node after node)
    std::vector<size_t> dinvStart;
    double omega = 1.0;                         // the damping of the smoother
    std::unique_ptr<Eigen::SimplicialLDLT<SpC>> direct;
    std::vector<double> x, b, r, t, s;          // the cycle's work
    size_t nodes() const { return nodeStart.size() - 1; }
};

struct Amg
{
    const Assembly* fine = nullptr;
    const std::vector<unsigned char>* fixed = nullptr;
    std::vector<AmgLevel> levels;
    int sweeps = 1;                             // smoothing steps before and after the correction
    std::string summary;
};

namespace {

////////////////////////////////////////////////////////////////////////////////
// The operators of a level

void levelMatvec(Amg& m, size_t l, const std::vector<double>& x, std::vector<double>& y)
{
    AmgLevel& L = m.levels[l];
    if (l == 0)
    {
        applyOperator(*m.fine, *m.fixed, x, y, L.s);
        return;
    }
    Eigen::Map<Vec>(y.data(), long(L.n)).noalias() = L.A * Eigen::Map<const Vec>(x.data(), long(L.n));
}

// out = D^-1 v, D the diagonal blocks
void blockInverse(const AmgLevel& L, const double* v, double* out)
{
    each(L.nodes(), 6000, [&](size_t i0, size_t i1) {
        for (size_t i = i0; i < i1; ++i)
        {
            const size_t s = L.nodeStart[i], r = L.nodeStart[i + 1] - s;
            const double* M = &L.dinv[L.dinvStart[i]];
            for (size_t a = 0; a < r; ++a)
            {
                double sum = 0;
                for (size_t b = 0; b < r; ++b) sum += M[a * r + b] * v[s + b];
                out[s + a] = sum;
            }
        }
    }, 64);
}

// x += omega D^-1 (b - A x)
void smoothOnce(Amg& m, size_t l)
{
    AmgLevel& L = m.levels[l];
    levelMatvec(m, l, L.x, L.r);
    each(L.n, 20000, [&](size_t b0, size_t b1) {
        for (size_t i = b0; i < b1; ++i) L.r[i] = L.b[i] - L.r[i];
    }, 8192);
    blockInverse(L, L.r.data(), L.t.data());
    each(L.n, 20000, [&](size_t b0, size_t b1) {
        for (size_t i = b0; i < b1; ++i) L.x[i] += L.omega * L.t[i];
    }, 8192);
}

void cycle(Amg& m, size_t l)
{
    AmgLevel& L = m.levels[l];
    if (l + 1 == m.levels.size())
    {
        if (L.direct)
        {
            Eigen::Map<Vec>(L.x.data(), long(L.n)) = L.direct->solve(Eigen::Map<const Vec>(L.b.data(), long(L.n)));
            return;
        }
        blockInverse(L, L.b.data(), L.x.data());
        for (size_t i = 0; i < L.n; ++i) L.x[i] *= L.omega;
        for (int s = 0; s < 3; ++s) smoothOnce(m, l);
        return;
    }
    // pre-smoothing, from zero
    blockInverse(L, L.b.data(), L.x.data());
    each(L.n, 20000, [&](size_t b0, size_t b1) {
        for (size_t i = b0; i < b1; ++i) L.x[i] *= L.omega;
    }, 8192);
    for (int s = 1; s < m.sweeps; ++s) smoothOnce(m, l);
    // the residual goes to the coarser level, the correction comes back
    levelMatvec(m, l, L.x, L.r);
    each(L.n, 20000, [&](size_t b0, size_t b1) {
        for (size_t i = b0; i < b1; ++i) L.r[i] = L.b[i] - L.r[i];
    }, 8192);
    AmgLevel& C = m.levels[l + 1];
    Eigen::Map<Vec>(C.b.data(), long(C.n)).noalias() = L.R * Eigen::Map<const Vec>(L.r.data(), long(L.n));
    cycle(m, l + 1);
    Eigen::Map<Vec>(L.t.data(), long(L.n)).noalias() = L.P * Eigen::Map<const Vec>(C.x.data(), long(C.n));
    each(L.n, 20000, [&](size_t b0, size_t b1) {
        for (size_t i = b0; i < b1; ++i) L.x[i] += L.t[i];
    }, 8192);
    for (int s = 0; s < m.sweeps; ++s) smoothOnce(m, l);
}

////////////////////////////////////////////////////////////////////////////////
// Setting a level up

void allocate(AmgLevel& L)
{
    L.x.assign(L.n, 0.0);
    L.b.assign(L.n, 0.0);
    L.r.assign(L.n, 0.0);
    L.t.assign(L.n, 0.0);
    L.s.assign(L.n, 0.0);
}

// The inverse of the diagonal block of each node of a coarse level
void blocksOf(AmgLevel& L)
{
    const size_t nodes = L.nodes();
    L.dinvStart.assign(nodes + 1, 0);
    for (size_t i = 0; i < nodes; ++i)
    {
        const size_t r = L.nodeStart[i + 1] - L.nodeStart[i];
        L.dinvStart[i + 1] = L.dinvStart[i] + r * r;
    }
    L.dinv.assign(L.dinvStart[nodes], 0.0);
    each(nodes, 4000, [&](size_t i0, size_t i1) {
        for (size_t i = i0; i < i1; ++i)
        {
            const size_t s = L.nodeStart[i], r = L.nodeStart[i + 1] - s;
            Mat B = Mat::Zero(long(r), long(r));
            for (size_t a = 0; a < r; ++a)
                for (Sp::InnerIterator it(L.A, int(s + a)); it; ++it)
                {
                    const size_t c = size_t(it.col());
                    if (c >= s && c < s + r) B(long(a), long(c - s)) = it.value();
                }
            Mat Bi = B.ldlt().solve(Mat::Identity(long(r), long(r)));
            if (!Bi.allFinite())
            {
                Bi = Mat::Zero(long(r), long(r));
                for (size_t a = 0; a < r; ++a) Bi(long(a), long(a)) = 1.0 / std::max(std::abs(B(long(a), long(a))), 1e-300);
            }
            for (size_t a = 0; a < r; ++a)
                for (size_t b = 0; b < r; ++b) L.dinv[L.dinvStart[i] + a * r + b] = Bi(long(a), long(b));
        }
    }, 64);
}

// The largest eigenvalue of D^-1 A, a little over (power iteration)
double spectralRadius(Amg& m, size_t l)
{
    AmgLevel& L = m.levels[l];
    std::vector<double>& v = L.x;
    uint64_t seed = 88172645463325252ull;
    double nv = 0;
    for (size_t i = 0; i < L.n; ++i)
    {
        seed ^= seed << 13;
        seed ^= seed >> 7;
        seed ^= seed << 17;
        v[i] = 0.5 + double(seed % 1000) / 1000.0;
        if (l == 0 && (*m.fixed)[i]) v[i] = 0.0;
        nv += v[i] * v[i];
    }
    nv = std::sqrt(nv);
    for (size_t i = 0; i < L.n; ++i) v[i] /= nv;
    double lambda = 1.0;
    for (int it = 0; it < 14; ++it)
    {
        levelMatvec(m, l, v, L.r);
        blockInverse(L, L.r.data(), L.t.data());
        double nw = 0;
        for (size_t i = 0; i < L.n; ++i) nw += L.t[i] * L.t[i];
        nw = std::sqrt(nw);
        if (!(nw > 0)) break;
        lambda = nw;
        for (size_t i = 0; i < L.n; ++i) v[i] = L.t[i] / nw;
    }
    return lambda * 1.08;
}

// Rows of the finest matrix, with the held directions replaced by the identity (as the operator does), for the nodes i0 .. i1
Sp fineChunk(const Assembly& A, const std::vector<unsigned char>& fixed, size_t i0, size_t i1)
{
    const size_t rows = 3 * (i1 - i0), cols = 3 * A.K.n;
    Sp M(ix(rows), ix(cols));
    Eigen::VectorXi sizes(ix(rows));
    for (size_t i = i0; i < i1; ++i)
        for (size_t r = 0; r < 3; ++r)
        {
            int count = 0;
            if (fixed[3 * i + r]) count = 1;
            else
                for (uint32_t e = A.K.rowPtr[i]; e < A.K.rowPtr[i + 1]; ++e)
                {
                    const size_t j = A.K.col[e];
                    for (size_t c = 0; c < 3; ++c) count += fixed[3 * j + c] ? 0 : 1;
                }
            sizes[long(3 * (i - i0) + r)] = count;
        }
    M.reserve(sizes);
    for (size_t i = i0; i < i1; ++i)
        for (size_t r = 0; r < 3; ++r)
        {
            const long row = long(3 * (i - i0) + r);
            if (fixed[3 * i + r])
            {
                M.insert(row, long(3 * i + r)) = 1.0;
                continue;
            }
            for (uint32_t e = A.K.rowPtr[i]; e < A.K.rowPtr[i + 1]; ++e)
            {
                const size_t j = A.K.col[e];
                const double* blk = &A.K.val[size_t(e) * 9];
                for (size_t c = 0; c < 3; ++c)
                    if (!fixed[3 * j + c]) M.insert(row, long(3 * j + c)) = blk[3 * r + c];
            }
        }
    M.makeCompressed();
    return M;
}

Sp chunkOf(const Amg& m, size_t l, size_t i0, size_t i1)
{
    if (l == 0) return fineChunk(*m.fine, *m.fixed, i0, i1);
    const AmgLevel& L = m.levels[l];
    const size_t base = L.nodeStart[i0], len = L.nodeStart[i1] - base;
    Sp M = L.A.middleRows(long(base), long(len));
    return M;
}

// The node ranges that are multiplied at a time
std::vector<size_t> chunkBounds(const AmgLevel& L)
{
    std::vector<size_t> bounds(1, 0);
    size_t last = 0;
    const size_t nodes = L.nodes();
    for (size_t i = 1; i <= nodes; ++i)
        if (L.nodeStart[i] - L.nodeStart[last] >= kChunkRows)
        {
            bounds.push_back(i);
            last = i;
        }
    if (bounds.back() != nodes) bounds.push_back(nodes);
    return bounds;
}

Sp blockDiagChunk(const AmgLevel& L, size_t i0, size_t i1)
{
    const size_t base = L.nodeStart[i0], len = L.nodeStart[i1] - base;
    std::vector<Eigen::Triplet<double>> t;
    t.reserve((L.dinvStart[i1] - L.dinvStart[i0]));
    for (size_t i = i0; i < i1; ++i)
    {
        const size_t s = L.nodeStart[i] - base, r = L.nodeStart[i + 1] - L.nodeStart[i];
        const double* M = &L.dinv[L.dinvStart[i]];
        for (size_t a = 0; a < r; ++a)
            for (size_t b = 0; b < r; ++b) t.emplace_back(int(s + a), int(s + b), M[a * r + b]);
    }
    Sp D(ix(len), ix(len));
    D.setFromTriplets(t.begin(), t.end());
    return D;
}

Sp stackRows(std::vector<Sp>& parts, size_t cols)
{
    size_t rows = 0, nnz = 0;
    for (auto& p : parts)
    {
        p.makeCompressed();
        rows += size_t(p.rows());
        nnz += size_t(p.nonZeros());
    }
    Sp M(ix(rows), ix(cols));
    M.resizeNonZeros(ix(nnz));
    int* outer = M.outerIndexPtr();
    int* inner = M.innerIndexPtr();
    double* val = M.valuePtr();
    size_t row = 0, pos = 0;
    outer[0] = 0;
    for (auto& p : parts)
    {
        for (long r = 0; r < p.rows(); ++r) outer[row + size_t(r) + 1] = int(pos + size_t(p.outerIndexPtr()[r + 1]));
        std::copy(p.innerIndexPtr(), p.innerIndexPtr() + p.nonZeros(), inner + pos);
        std::copy(p.valuePtr(), p.valuePtr() + p.nonZeros(), val + pos);
        row += size_t(p.rows());
        pos += size_t(p.nonZeros());
        p = Sp();
    }
    return M;
}

// The graph of the nodes (their couplings), the nodes that are held in every direction left out
struct Graph
{
    std::vector<size_t> ptr;
    std::vector<int> adj;
};

Graph nodeGraph(const Amg& m, size_t l, const std::vector<char>& alive)
{
    const AmgLevel& L = m.levels[l];
    const size_t nodes = L.nodes();
    std::vector<std::vector<int>> tmp(nodes);
    std::vector<int> dofNode;
    if (l > 0)
    {
        dofNode.resize(L.n);
        for (size_t i = 0; i < nodes; ++i)
            for (size_t d = L.nodeStart[i]; d < L.nodeStart[i + 1]; ++d) dofNode[d] = int(i);
    }
    each(nodes, 4000, [&](size_t i0, size_t i1) {
        for (size_t i = i0; i < i1; ++i)
        {
            if (!alive[i]) continue;
            auto& v = tmp[i];
            if (l == 0)
            {
                const Bsr& K = m.fine->K;
                for (uint32_t e = K.rowPtr[i]; e < K.rowPtr[i + 1]; ++e)
                {
                    const uint32_t j = K.col[e];
                    if (j != i && alive[j]) v.push_back(int(j));
                }
            }
            else
            {
                for (size_t d = L.nodeStart[i]; d < L.nodeStart[i + 1]; ++d)
                    for (Sp::InnerIterator it(L.A, int(d)); it; ++it)
                    {
                        const int j = dofNode[size_t(it.col())];
                        if (size_t(j) != i && alive[size_t(j)]) v.push_back(j);
                    }
                std::sort(v.begin(), v.end());
                v.erase(std::unique(v.begin(), v.end()), v.end());
            }
        }
    }, 64);
    Graph g;
    g.ptr.assign(nodes + 1, 0);
    for (size_t i = 0; i < nodes; ++i) g.ptr[i + 1] = g.ptr[i] + tmp[i].size();
    g.adj.resize(g.ptr[nodes]);
    for (size_t i = 0; i < nodes; ++i) std::copy(tmp[i].begin(), tmp[i].end(), g.adj.begin() + std::ptrdiff_t(g.ptr[i]));
    return g;
}

// Aggregates: a node with all its neighbours free is a root and takes them; a node left over joins the neighbouring aggregate it touches most (the
// smaller on a tie); a node that touches none founds an aggregate with its free neighbours
int aggregate(const Graph& g, const std::vector<char>& alive, std::vector<int>& agg)
{
    const size_t nodes = alive.size();
    agg.assign(nodes, -1);
    int count = 0;
    for (size_t i = 0; i < nodes; ++i)
    {
        if (!alive[i] || agg[i] >= 0) continue;
        bool free = true;
        for (size_t e = g.ptr[i]; e < g.ptr[i + 1] && free; ++e) free = agg[size_t(g.adj[e])] < 0;
        if (!free) continue;
        agg[i] = count;
        for (size_t e = g.ptr[i]; e < g.ptr[i + 1]; ++e) agg[size_t(g.adj[e])] = count;
        ++count;
    }
    std::vector<int> first = agg;
    std::vector<size_t> size(size_t(count), 0);
    for (size_t i = 0; i < nodes; ++i) if (first[i] >= 0) ++size[size_t(first[i])];
    std::vector<int> seen;
    for (size_t i = 0; i < nodes; ++i)
    {
        if (!alive[i] || agg[i] >= 0) continue;
        seen.clear();
        for (size_t e = g.ptr[i]; e < g.ptr[i + 1]; ++e)
            if (first[size_t(g.adj[e])] >= 0) seen.push_back(first[size_t(g.adj[e])]);
        if (seen.empty()) continue;
        std::sort(seen.begin(), seen.end());
        int best = seen[0];
        size_t bestCount = 0;
        for (size_t a = 0; a < seen.size();)
        {
            size_t b = a;
            while (b < seen.size() && seen[b] == seen[a]) ++b;
            const size_t c = b - a;
            if (c > bestCount || (c == bestCount && size[size_t(seen[a])] < size[size_t(best)]))
            {
                best = seen[a];
                bestCount = c;
            }
            a = b;
        }
        agg[i] = best;
    }
    for (size_t i = 0; i < nodes; ++i)
    {
        if (!alive[i] || agg[i] >= 0) continue;
        agg[i] = count;
        for (size_t e = g.ptr[i]; e < g.ptr[i + 1]; ++e)
            if (agg[size_t(g.adj[e])] < 0) agg[size_t(g.adj[e])] = count;
        ++count;
    }
    return count;
}

}   // anonymous namespace

////////////////////////////////////////////////////////////////////////////////

std::shared_ptr<Amg> buildAmg(const Assembly& A, const std::vector<unsigned char>& fixed, std::string& error)
{
    if (!A.turns.empty())
    {
        error = "the springs on the turns are not part of the multigrid";
        return nullptr;
    }
    const size_t nodes0 = A.K.n;
    if (A.coords.size() != 3 * nodes0 || nodes0 == 0)
    {
        error = "the multigrid needs the nodes' positions";
        return nullptr;
    }
    try
    {
        const auto t0 = std::chrono::steady_clock::now();
        auto m = std::make_shared<Amg>();
        m->fine = &A;
        m->fixed = &fixed;
        m->levels.reserve(kMaxLevels + 2);
        if (const char* s = std::getenv("FIELDES_AMG_SWEEPS")) m->sweeps = std::max(1, std::atoi(s));

        // The finest level: 3 unknowns to a node, the inverse blocks of the block-Jacobi preconditioner
        m->levels.emplace_back();
        {
            AmgLevel& L = m->levels[0];
            L.n = 3 * nodes0;
            L.nodeStart.resize(nodes0 + 1);
            for (size_t i = 0; i <= nodes0; ++i) L.nodeStart[i] = 3 * i;
            L.dinv = A.inv;
            L.dinvStart.resize(nodes0 + 1);
            for (size_t i = 0; i <= nodes0; ++i) L.dinvStart[i] = 9 * i;
            allocate(L);
        }
        // The rigid-body modes of the nodes (the part's own size and middle, so that all six are of one size), the held directions zero
        Mat B(long(3 * nodes0), 6);
        {
            Eigen::Vector3d lo(1e300, 1e300, 1e300), hi(-1e300, -1e300, -1e300);
            for (size_t i = 0; i < nodes0; ++i)
                for (int a = 0; a < 3; ++a)
                {
                    lo[a] = std::min(lo[a], A.coords[3 * i + size_t(a)]);
                    hi[a] = std::max(hi[a], A.coords[3 * i + size_t(a)]);
                }
            const Eigen::Vector3d mid = 0.5 * (lo + hi);
            const double scale = std::max(1e-300, (hi - lo).maxCoeff());
            B.setZero();
            for (size_t i = 0; i < nodes0; ++i)
            {
                const double x = (A.coords[3 * i] - mid[0]) / scale, y = (A.coords[3 * i + 1] - mid[1]) / scale,
                             z = (A.coords[3 * i + 2] - mid[2]) / scale;
                B(long(3 * i), 0) = 1;
                B(long(3 * i + 1), 1) = 1;
                B(long(3 * i + 2), 2) = 1;
                B(long(3 * i + 1), 3) = -z;
                B(long(3 * i + 2), 3) = y;
                B(long(3 * i), 4) = z;
                B(long(3 * i + 2), 4) = -x;
                B(long(3 * i), 5) = -y;
                B(long(3 * i + 1), 5) = x;
                for (int a = 0; a < 3; ++a)
                    if (fixed[3 * i + size_t(a)]) B.row(long(3 * i + size_t(a))).setZero();
            }
        }
        std::string summary;
        for (size_t l = 0;; ++l)
        {
            const size_t n = m->levels[l].n;
            summary += (l ? " > " : "") + std::to_string(n);
            if (n <= kCoarsest || l + 1 >= kMaxLevels)
            {
                if (n > kDirectLimit)
                {
                    error = "the multigrid does not coarsen enough (" + summary + ")";
                    return nullptr;
                }
                break;
            }
            const size_t nodes = m->levels[l].nodes();
            std::vector<char> alive(nodes, 1);
            if (l == 0)
                for (size_t i = 0; i < nodes; ++i) alive[i] = (fixed[3 * i] && fixed[3 * i + 1] && fixed[3 * i + 2]) ? 0 : 1;

            // Aggregates, and in each the orthonormal basis of the rigid-body modes
            std::vector<int> agg;
            int nAgg = 0;
            {
                const Graph g = nodeGraph(*m, l, alive);
                nAgg = aggregate(g, alive, agg);
            }
            std::vector<size_t> aggStart(size_t(nAgg) + 1, 0);
            for (size_t i = 0; i < nodes; ++i) if (agg[i] >= 0) ++aggStart[size_t(agg[i]) + 1];
            for (int a = 0; a < nAgg; ++a) aggStart[size_t(a) + 1] += aggStart[size_t(a)];
            std::vector<int> members(aggStart[size_t(nAgg)]);
            {
                std::vector<size_t> cursor(aggStart.begin(), aggStart.end() - 1);
                for (size_t i = 0; i < nodes; ++i) if (agg[i] >= 0) members[cursor[size_t(agg[i])]++] = int(i);
            }
            const AmgLevel& Lc = m->levels[l];
            const size_t aggCount = size_t(nAgg);
            std::vector<Mat> Q(aggCount), Rm(aggCount);
            parallelRange(size_t(nAgg), [&](size_t a0, size_t a1) {
                for (size_t a = a0; a < a1; ++a)
                {
                    size_t rows = 0;
                    for (size_t k = aggStart[a]; k < aggStart[a + 1]; ++k)
                    {
                        const size_t i = size_t(members[k]);
                        rows += Lc.nodeStart[i + 1] - Lc.nodeStart[i];
                    }
                    Mat M(long(rows), 6);
                    size_t row = 0;
                    for (size_t k = aggStart[a]; k < aggStart[a + 1]; ++k)
                    {
                        const size_t i = size_t(members[k]);
                        for (size_t d = Lc.nodeStart[i]; d < Lc.nodeStart[i + 1]; ++d) M.row(long(row++)) = B.row(long(d));
                    }
                    if (rows == 0 || M.cwiseAbs().maxCoeff() < 1e-300) continue;
                    Eigen::ColPivHouseholderQR<Mat> qr;
                    qr.setThreshold(1e-7);
                    qr.compute(M);
                    const long rank = qr.rank();
                    if (rank <= 0) continue;
                    Q[a] = qr.householderQ() * Mat::Identity(long(rows), rank);
                    Rm[a] = Q[a].transpose() * M;
                }
            }, 16);

            // The coarse nodes: the aggregates that kept a mode
            std::vector<int> cnode(size_t(nAgg), -1);
            std::vector<size_t> cStart(1, 0);
            for (int a = 0; a < nAgg; ++a)
                if (Q[size_t(a)].cols() > 0)
                {
                    cnode[size_t(a)] = int(cStart.size() - 1);
                    cStart.push_back(cStart.back() + size_t(Q[size_t(a)].cols()));
                }
            const size_t nc = cStart.back();
            if (nc == 0 || nc * 10 >= n * 9)
            {
                // no coarser level: the level is the last, when it is small enough to be solved
                if (n > kDirectLimit)
                {
                    error = "the multigrid does not coarsen (" + summary + ")";
                    return nullptr;
                }
                break;
            }
            Mat Bc(long(nc), 6);
            Sp P0(ix(n), ix(nc));
            {
                std::vector<Eigen::Triplet<double>> tri;
                tri.reserve(n * 6);
                for (int a = 0; a < nAgg; ++a)
                {
                    const int cn = cnode[size_t(a)];
                    if (cn < 0) continue;
                    const Mat& Qa = Q[size_t(a)];
                    size_t row = 0;
                    for (size_t k = aggStart[size_t(a)]; k < aggStart[size_t(a) + 1]; ++k)
                    {
                        const size_t i = size_t(members[k]);
                        for (size_t d = Lc.nodeStart[i]; d < Lc.nodeStart[i + 1]; ++d, ++row)
                            for (long j = 0; j < Qa.cols(); ++j)
                                tri.emplace_back(int(d), int(cStart[size_t(cn)] + size_t(j)), Qa(long(row), j));
                    }
                    Bc.middleRows(long(cStart[size_t(cn)]), Qa.cols()) = Rm[size_t(a)];
                }
                P0.setFromTriplets(tri.begin(), tri.end());
            }
            Q.clear();
            Q.shrink_to_fit();

            // Smoothed: P = (I - w D^-1 A) P0
            const double rho = spectralRadius(*m, l);
            const double w = 4.0 / (3.0 * rho);
            m->levels[l].omega = w;
            const std::vector<size_t> cb = chunkBounds(m->levels[l]);
            const size_t chunks = cb.size() - 1;
            std::atomic<bool> failed(false);
            Sp P;
            {
                std::vector<Sp> parts(chunks);
                parallelRange(chunks, [&](size_t c0, size_t c1) {
                    try
                    {
                        for (size_t c = c0; c < c1; ++c)
                        {
                            const size_t i0 = cb[c], i1 = cb[c + 1];
                            const AmgLevel& Lq = m->levels[l];
                            const size_t base = Lq.nodeStart[i0], len = Lq.nodeStart[i1] - base;
                            const Sp Ac = chunkOf(*m, l, i0, i1);
                            const Sp T = Ac * P0;
                            const Sp Dm = blockDiagChunk(Lq, i0, i1);
                            const Sp DT = Dm * T;
                            const Sp P0c = P0.middleRows(long(base), long(len));
                            parts[c] = P0c - w * DT;
                        }
                    }
                    catch (...) { failed = true; }
                }, 1);
                if (failed) throw std::bad_alloc();
                P = stackRows(parts, nc);
            }

            // The coarse matrix, P^T A P, the rows of A a chunk at a time
            Sp Ac;
            {
                std::mutex mu;
                std::vector<Sp> sums;
                parallelRange(chunks, [&](size_t c0, size_t c1) {
                    try
                    {
                        Sp local(ix(nc), ix(nc));
                        bool first = true;
                        for (size_t c = c0; c < c1; ++c)
                        {
                            const size_t i0 = cb[c], i1 = cb[c + 1];
                            const AmgLevel& Lq = m->levels[l];
                            const size_t base = Lq.nodeStart[i0], len = Lq.nodeStart[i1] - base;
                            const Sp Ar = chunkOf(*m, l, i0, i1);
                            const Sp AP = Ar * P;
                            Sp PT = P.middleRows(long(base), long(len)).transpose();
                            Sp part = PT * AP;
                            if (first) local = std::move(part);
                            else local = Sp(local + part);
                            first = false;
                        }
                        std::lock_guard<std::mutex> lock(mu);
                        sums.push_back(std::move(local));
                    }
                    catch (...) { failed = true; }
                }, 1);
                if (failed) throw std::bad_alloc();
                Ac = Sp(long(nc), long(nc));
                for (auto& s : sums) Ac = Sp(Ac + s);
                sums.clear();
                Sp At = Ac.transpose();
                Ac = Sp((Ac + At) * 0.5);
                const Vec dg = Ac.diagonal();
                Ac.prune([&](const int r, const int c, const double v) {
                    return r == c || std::abs(v) > 1e-14 * std::sqrt(std::abs(dg[r] * dg[c]));
                });
                Ac.makeCompressed();
            }

            AmgLevel C;
            C.n = nc;
            C.nodeStart = cStart;
            C.A = std::move(Ac);
            blocksOf(C);
            allocate(C);
            {
                AmgLevel& Lf = m->levels[l];
                Lf.P = std::move(P);
                Lf.R = Sp(Lf.P.transpose());
            }
            m->levels.push_back(std::move(C));
            B = std::move(Bc);
        }

        // The last level: solved directly
        {
            AmgLevel& L = m->levels.back();
            const size_t l = m->levels.size() - 1;
            if (l == 0)
            {
                error = "the multigrid has one level";
                return nullptr;
            }
            L.omega = 4.0 / (3.0 * spectralRadius(*m, l));
            SpC C = L.A;
            const Vec dg = C.diagonal();
            const double eps = 1e-12 * dg.cwiseAbs().maxCoeff();
            for (long i = 0; i < C.rows(); ++i) C.coeffRef(i, i) += eps;
            auto ldlt = std::make_unique<Eigen::SimplicialLDLT<SpC>>();
            ldlt->compute(C);
            if (ldlt->info() == Eigen::Success) L.direct = std::move(ldlt);
            else summary += " (no direct solve)";
        }
        const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        char buf[64];
        std::snprintf(buf, sizeof(buf), ", %.1f s", sec);
        m->summary = summary + buf;
        return m;
    }
    catch (const std::bad_alloc&)
    {
        error = "out of memory for the multigrid";
        return nullptr;
    }
}

void applyAmg(Amg& m, const double* r, double* z)
{
    AmgLevel& L = m.levels[0];
    std::copy(r, r + L.n, L.b.begin());
    cycle(m, 0);
    std::copy(L.x.begin(), L.x.end(), z);
}

std::string amgSummary(const Amg& m)
{
    return m.summary;
}

}   // namespace tet
}   // namespace fea
}   // namespace libfive
