/*
libfive: a CAD kernel for modeling with implicit functions

Static linear-elastic FEA on a voxel grid; see fea.hpp.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <cmath>
#include <deque>
#include <functional>
#include <thread>

#include <boost/container/small_vector.hpp>

#include "libfive/fea/fea.hpp"
#include "libfive/eval/eval_array.hpp"
#include "libfive/eval/feature.hpp"
#include "libfive/oracle/oracle_clause.hpp"
#include "libfive/oracle/oracle_storage.hpp"
#include "libfive/run_progress.hpp"

namespace libfive {
namespace fea {

namespace {

using Mat24 = Eigen::Matrix<double, 24, 24>;
using Vec24 = Eigen::Matrix<double, 24, 1>;
using Mat6 = Eigen::Matrix<double, 6, 6>;
using Mat6x24 = Eigen::Matrix<double, 6, 24>;
using Mat6x9 = Eigen::Matrix<double, 6, 9>;
using Mat9x24 = Eigen::Matrix<double, 9, 24>;

// Corner offsets of an element's nodes (and their natural coordinates)
const int CORNER[8][3] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0},
                          {0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}};

// A small persistent thread pool: the solver runs a dozen parallel loops
// per iteration, too many to start threads for each
class Pool
{
public:
    Pool()
    {
        const unsigned nt = std::max(1u, std::min(16u, std::thread::hardware_concurrency()));
        for (unsigned t = 1; t < nt; ++t)
        {
            workers.emplace_back([this]() { loop(); });
        }
    }
    ~Pool()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stop = true;
        }
        wake.notify_all();
        for (auto& w : workers) w.join();
    }
    size_t threads() const { return workers.size() + 1; }

    // fn(chunk) for chunk in [0, chunks), the calling thread helping
    void run(size_t chunks, const std::function<void(size_t)>& fn)
    {
        std::unique_lock<std::mutex> job(jobMutex);   // one job at a time
        {
            std::lock_guard<std::mutex> lock(mutex);
            task = &fn;
            total = chunks;
            next = 0;
            pending = chunks;
            ++generation;
        }
        wake.notify_all();
        work();
        std::unique_lock<std::mutex> lock(mutex);
        done.wait(lock, [this]() { return pending == 0; });
        task = nullptr;
    }

private:
    void work()
    {
        for (;;)
        {
            size_t c;
            const std::function<void(size_t)>* fn;
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (!task || next >= total) return;
                c = next++;
                fn = task;
            }
            (*fn)(c);
            std::lock_guard<std::mutex> lock(mutex);
            if (--pending == 0) done.notify_all();
        }
    }
    void loop()
    {
        size_t seen = 0;
        for (;;)
        {
            {
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait(lock, [&]() { return stop || (generation != seen && task && next < total); });
                if (stop) return;
                seen = generation;
            }
            work();
        }
    }
    std::vector<std::thread> workers;
    std::mutex mutex, jobMutex;
    std::condition_variable wake, done;
    const std::function<void(size_t)>* task = nullptr;
    size_t total = 0, next = 0, pending = 0, generation = 0;
    bool stop = false;
};

Pool& pool()
{
    static Pool* p = new Pool;   // never destroyed: joining threads while the DLL unloads can deadlock
    return *p;
}

// Runs fn(begin, end) over [0, n) on all cores
void parallelFor(size_t n, const std::function<void(size_t, size_t)>& fn, size_t minChunk=256)
{
    Pool& P = pool();
    if (n < 2 * minChunk || P.threads() == 1)
    {
        if (n) fn(0, n);
        return;
    }
    const size_t chunk = std::max(minChunk, (n + P.threads() * 4 - 1) / (P.threads() * 4));
    const size_t chunks = (n + chunk - 1) / chunk;
    P.run(chunks, [&](size_t c) { fn(c * chunk, std::min(n, (c + 1) * chunk)); });
}

// Sum of fn(i) over [0, n) on all cores
double parallelSum(size_t n, const std::function<double(size_t, size_t)>& fn)
{
    Pool& P = pool();
    const size_t chunk = std::max<size_t>(4096, (n + P.threads() * 4 - 1) / (P.threads() * 4));
    const size_t chunks = (n + chunk - 1) / chunk;
    std::vector<double> part(chunks, 0.0);
    if (chunks <= 1)
    {
        return n ? fn(0, n) : 0.0;
    }
    P.run(chunks, [&](size_t c) { part[c] = fn(c * chunk, std::min(n, (c + 1) * chunk)); });
    double s = 0;
    for (double x : part) s += x;
    return s;
}

// Points evaluated against the points to evaluate: prepare()'s progress
// (its time goes into evaluating the shape and the regions)
struct EvalCount
{
    run_progress::Task* task = nullptr;
    std::atomic<size_t> done{0};
    std::atomic<size_t> total{1};
    void add(size_t n)
    {
        const size_t d = done.fetch_add(n) + n;
        task->set(double(d) / double(std::max<size_t>(1, total.load())));
    }
};
thread_local EvalCount* t_evalCount = nullptr;

// Evaluates a tree at many points (on all cores)
void evalPoints(const Tree& tree, const std::vector<Eigen::Vector3f>& pts, std::vector<float>& out)
{
    out.resize(pts.size());
    const size_t N = ArrayEvaluator::N;
    EvalCount* counted = t_evalCount;
    parallelFor((pts.size() + N - 1) / N, [&](size_t b0, size_t b1) {
        ArrayEvaluator e(tree);
        for (size_t b = b0; b < b1; ++b)
        {
            const size_t start = b * N;
            const size_t count = std::min(N, pts.size() - start);
            for (size_t k = 0; k < count; ++k) e.set(pts[start + k], k);
            const auto vs = e.values(count);
            for (size_t k = 0; k < count; ++k) out[start + k] = vs(k);
            if (counted) counted->add(count);
        }
    }, 4);
}

Mat6 elasticity(double E, double nu)
{
    const double lambda = E * nu / ((1 + nu) * (1 - 2 * nu));
    const double mu = E / (2 * (1 + nu));
    Mat6 D = Mat6::Zero();
    for (int i = 0; i < 3; ++i)
    {
        for (int j = 0; j < 3; ++j) D(i, j) = lambda;
        D(i, i) = lambda + 2 * mu;
        D(i + 3, i + 3) = mu;
    }
    return D;
}

// Strain-displacement matrix of a cube element of side h at natural
// coordinates (xi, eta, zeta) in [-1, 1]^3 (engineering shear strains)
Mat6x24 strainMatrix(double h, double xi, double eta, double zeta)
{
    Mat6x24 B = Mat6x24::Zero();
    for (int a = 0; a < 8; ++a)
    {
        const double sx = 2.0 * CORNER[a][0] - 1, sy = 2.0 * CORNER[a][1] - 1, sz = 2.0 * CORNER[a][2] - 1;
        // dN/dxi etc., then d/dx = (2 / h) d/dxi
        const double dx = 0.125 * sx * (1 + sy * eta) * (1 + sz * zeta) * 2 / h;
        const double dy = 0.125 * sy * (1 + sx * xi) * (1 + sz * zeta) * 2 / h;
        const double dz = 0.125 * sz * (1 + sx * xi) * (1 + sy * eta) * 2 / h;
        const int c = 3 * a;
        B(0, c) = dx;
        B(1, c + 1) = dy;
        B(2, c + 2) = dz;
        B(3, c) = dy; B(3, c + 1) = dx;
        B(4, c + 1) = dz; B(4, c + 2) = dy;
        B(5, c) = dz; B(5, c + 2) = dx;
    }
    return B;
}

// Strains of the element's incompatible modes (Wilson-Taylor): each
// displacement component may also bulge as 1 - xi^2, 1 - eta^2, 1 - zeta^2
// inside the element (internal DOF 3 * mode + component). Plain trilinear
// cubes can't bend without spurious shear ("shear locking": a cantilever
// four elements thick came out 40 % too stiff); these modes let them, and
// represent a linearly varying stress -- pure bending -- exactly.
Mat6x9 incompatibleStrain(double h, double xi, double eta, double zeta)
{
    Mat6x9 G = Mat6x9::Zero();
    const double grad[3][3] = {{-2 * xi * 2 / h, 0, 0},
                               {0, -2 * eta * 2 / h, 0},
                               {0, 0, -2 * zeta * 2 / h}};
    for (int m = 0; m < 3; ++m)
    {
        const double dx = grad[m][0], dy = grad[m][1], dz = grad[m][2];
        const int c = 3 * m;
        G(0, c) = dx;
        G(1, c + 1) = dy;
        G(2, c + 2) = dz;
        G(3, c) = dy; G(3, c + 1) = dx;
        G(4, c + 1) = dz; G(4, c + 2) = dy;
        G(5, c) = dz; G(5, c + 2) = dx;
    }
    return G;
}

// The element stiffness with the incompatible modes condensed out, and the
// matrix that recovers their amplitudes from the nodal displacements
// (alpha = recover * u_e). The same for every element of the grid.
Mat24 hexStiffness(double h, const Mat6& D, Mat9x24* recover=nullptr)
{
    const double g = 1.0 / std::sqrt(3.0);
    const double detJ = std::pow(h / 2, 3);
    Mat24 Kuu = Mat24::Zero();
    Eigen::Matrix<double, 24, 9> Kua = Eigen::Matrix<double, 24, 9>::Zero();
    Eigen::Matrix<double, 9, 9> Kaa = Eigen::Matrix<double, 9, 9>::Zero();
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 2; ++j)
            for (int k = 0; k < 2; ++k)
            {
                const double xi = i ? g : -g, eta = j ? g : -g, zeta = k ? g : -g;
                const Mat6x24 B = strainMatrix(h, xi, eta, zeta);
                const Mat6x9 G = incompatibleStrain(h, xi, eta, zeta);
                Kuu += B.transpose() * D * B * detJ;
                Kua += B.transpose() * D * G * detJ;
                Kaa += G.transpose() * D * G * detJ;
            }
    const Eigen::Matrix<double, 9, 9> KaaInv = Kaa.inverse();
    if (recover) *recover = -KaaInv * Kua.transpose();
    return Kuu - Kua * KaaInv * Kua.transpose();
}

// ---- The element models (see Element in fea.hpp).  A cell is a cube of side
// h with the 8 nodes of CORNER; its stiffness is the same for every cell of
// the grid, scaled by the material there.

// The plain trilinear cube: full 2 x 2 x 2 integration, no incompatible modes
Mat24 hexBasicStiffness(double h, const Mat6& D)
{
    const double g = 1.0 / std::sqrt(3.0);
    const double detJ = std::pow(h / 2, 3);
    Mat24 K = Mat24::Zero();
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 2; ++j)
            for (int k = 0; k < 2; ++k)
            {
                const Mat6x24 B = strainMatrix(h, i ? g : -g, j ? g : -g, k ? g : -g);
                K += B.transpose() * D * B * detJ;
            }
    return K;
}

// The six tetrahedra of a cell (Kuhn subdivision): for each order (a, b, c) of
// the three axes, the corners 000, e_a, e_a + e_b and 111.  Each is a linear
// tetrahedron: a constant strain matrix (in the cell's 24 DOFs) and a volume.
struct TetCell
{
    Mat6x24 B[6];
    double volume[6];
    int corner[6][4];       // the cell corner each tetrahedron node is
    TetCell(double h)
    {
        static const int order[6][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
        auto cornerOf = [](int x, int y, int z) {
            for (int c = 0; c < 8; ++c)
                if (CORNER[c][0] == x && CORNER[c][1] == y && CORNER[c][2] == z) return c;
            return 0;
        };
        for (int t = 0; t < 6; ++t)
        {
            int p[4][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {1, 1, 1}};
            p[1][order[t][0]] = 1;
            p[2][order[t][0]] = 1;
            p[2][order[t][1]] = 1;
            Eigen::Matrix4d M;
            for (int i = 0; i < 4; ++i)
            {
                M.row(i) << 1.0, p[i][0] * h, p[i][1] * h, p[i][2] * h;
                corner[t][i] = cornerOf(p[i][0], p[i][1], p[i][2]);
            }
            const Eigen::Matrix4d C = M.inverse();       // N_i = C(0,i) + C(1,i) x + C(2,i) y + C(3,i) z
            volume[t] = std::abs(M.determinant()) / 6.0;
            B[t].setZero();
            for (int i = 0; i < 4; ++i)
            {
                const double dx = C(1, i), dy = C(2, i), dz = C(3, i);
                const int c = 3 * corner[t][i];
                B[t](0, c) = dx;
                B[t](1, c + 1) = dy;
                B[t](2, c + 2) = dz;
                B[t](3, c) = dy; B[t](3, c + 1) = dx;
                B[t](4, c + 1) = dz; B[t](4, c + 2) = dy;
                B[t](5, c) = dz; B[t](5, c + 2) = dx;
            }
        }
    }
};

Mat24 tetStiffness(double h, const Mat6& D)
{
    const TetCell cell(h);
    Mat24 K = Mat24::Zero();
    for (int t = 0; t < 6; ++t) K += cell.B[t].transpose() * D * cell.B[t] * cell.volume[t];
    return K;
}

bool elementHasModes(Element e) { return e == Element::Hex; }

// The stiffness of one cell of unit modulus; with modes, also the matrix that
// recovers their amplitudes from the nodal displacements (else it is zero)
Mat24 elementStiffness(Element e, double h, const Mat6& D, Mat9x24* recover = nullptr)
{
    if (recover) recover->setZero();
    if (e == Element::Tet) return tetStiffness(h, D);
    if (e == Element::HexBasic) return hexBasicStiffness(h, D);
    return hexStiffness(h, D, recover);
}

// The strain of the cell's nodal displacements at its corner c (the tetrahedra
// meeting there share it: their volume-weighted average)
Mat6x24 cornerStrain(Element e, double h, int c)
{
    if (e != Element::Tet)
        return strainMatrix(h, 2.0 * CORNER[c][0] - 1, 2.0 * CORNER[c][1] - 1, 2.0 * CORNER[c][2] - 1);
    const TetCell cell(h);
    Mat6x24 sum = Mat6x24::Zero();
    double vol = 0;
    for (int t = 0; t < 6; ++t)
    {
        bool has = false;
        for (int i = 0; i < 4; ++i) has = has || cell.corner[t][i] == c;
        if (!has) continue;
        sum += cell.B[t] * cell.volume[t];
        vol += cell.volume[t];
    }
    return sum / vol;
}

// The integral over the cell of B^T (nodal loads of a uniform strain, times D)
Eigen::Matrix<double, 24, 6> integratedStrain(Element e, double h)
{
    Eigen::Matrix<double, 24, 6> Bint = Eigen::Matrix<double, 24, 6>::Zero();
    if (e == Element::Tet)
    {
        const TetCell cell(h);
        for (int t = 0; t < 6; ++t) Bint += cell.B[t].transpose() * cell.volume[t];
        return Bint;
    }
    const double g = 1.0 / std::sqrt(3.0);
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 2; ++j)
            for (int k = 0; k < 2; ++k)
                Bint += strainMatrix(h, i ? g : -g, j ? g : -g, k ? g : -g).transpose() * std::pow(h / 2, 3);
    return Bint;
}

// Geometric multigrid preconditioner for the voxel grid: V-cycles with
// damped Jacobi smoothing, trilinear transfer between grids, coarse grids
// rediscretized from the averaged stiffness of their 8 children, and a
// Jacobi-preconditioned CG solve on the coarsest grid.  Turns thousands of
// Jacobi-PCG iterations into a few dozen.
class Multigrid
{
public:
    struct FineElem { int i, j, k; double scale; int dof[8]; };

    Multigrid(int ex, int ey, int ez, double h, double nu, const std::vector<FineElem>& fine,
              const std::vector<int>& dofOfNode, const std::vector<char>& fixed, size_t ndof,
              Element type)
        : type_(type)
    {
        const Mat6 D1 = elasticity(1.0, nu);
        L.emplace_back();
        {
            Level& l = L.back();
            l.ex = ex; l.ey = ey; l.ez = ez; l.h = h;
            l.K = elementStiffness(type_, h, D1);
            l.dofOfNode = dofOfNode;
            l.fixed = fixed;
            l.n = ndof;
            for (const auto& fe : fine)
            {
                l.ei.push_back(fe.i); l.ej.push_back(fe.j); l.ek.push_back(fe.k);
                std::array<int, 8> d;
                for (int c = 0; c < 8; ++c) d[size_t(c)] = fe.dof[c];
                l.dof.push_back(d);
                l.scale.push_back(fe.scale);
            }
            finish(l);
        }
        while (L.size() < 10 && L.back().n > 1500 &&
               (L.back().ex > 2 || L.back().ey > 2 || L.back().ez > 2))
        {
            coarsen(D1);
        }
    }

    size_t levels() const { return L.size(); }

    // z = (approximately) K^-1 r
    void apply(const std::vector<double>& r, std::vector<double>& z)
    {
        z.assign(L[0].n, 0.0);
        vcycle(0, r, z);
    }

private:
    struct Level
    {
        int ex = 0, ey = 0, ez = 0;
        double h = 1;
        std::vector<int> ei, ej, ek;
        std::vector<std::array<int, 8>> dof;
        std::vector<double> scale;
        std::vector<std::vector<int>> colour;
        Mat24 K;
        std::vector<int> dofOfNode;
        std::vector<char> fixed;
        std::vector<double> minv;
        double omega = 0.5;             // Jacobi damping (from the spectrum)
        size_t n = 0;
        // prolongation from the next coarser level, one row per node of
        // this level that has DOFs: its first DOF, then (coarse DOF, weight)
        std::vector<int> pDof, pStart, pIdx;
        std::vector<double> pW;
        std::vector<double> x, b, t;        // scratch
        size_t nodeIndex(int i, int j, int k) const
        { return (size_t(k) * (ey + 1) + j) * (ex + 1) + i; }
    };
    Element type_ = Element::Tet;
    std::vector<Level> L;

    void finish(Level& l)
    {
        l.colour.assign(8, {});
        for (size_t a = 0; a < l.scale.size(); ++a)
            l.colour[size_t((l.ei[a] & 1) | ((l.ej[a] & 1) << 1) | ((l.ek[a] & 1) << 2))].push_back(int(a));
        std::vector<double> diag(l.n, 0.0);
        for (size_t a = 0; a < l.scale.size(); ++a)
            for (int c = 0; c < 8; ++c)
                for (int q = 0; q < 3; ++q)
                    diag[size_t(l.dof[a][size_t(c)] + q)] += l.scale[a] * l.K(3 * c + q, 3 * c + q);
        l.minv.resize(l.n);
        for (size_t i = 0; i < l.n; ++i)
            l.minv[i] = (l.fixed[i] || !(diag[i] > 0)) ? 0.0 : 1.0 / diag[i];
        l.x.assign(l.n, 0.0);
        // Damping: the largest eigenvalue of D^-1 K by power iteration (the
        // fixed 0.6 of textbooks diverged with these elements); 1 / lambda
        // keeps the smoother convergent with room for the estimate's error
        {
            std::vector<double> v(l.n), w(l.n);
            uint64_t seed = 88172645463325252ull;
            for (size_t i = 0; i < l.n; ++i)
            {
                seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17;
                v[i] = l.fixed[i] ? 0.0 : double(seed % 1000) / 1000.0 - 0.5;
            }
            double lambda = 1;
            for (int it = 0; it < 15; ++it)
            {
                matvec(l, v, w);
                double nv = 0, nw = 0;
                for (size_t i = 0; i < l.n; ++i)
                {
                    w[i] *= l.minv[i];
                    nv += v[i] * v[i];
                    nw += w[i] * w[i];
                }
                if (!(nv > 0) || !(nw > 0)) break;
                lambda = std::sqrt(nw / nv);
                const double inv = 1.0 / std::sqrt(nw);
                for (size_t i = 0; i < l.n; ++i) v[i] = w[i] * inv;
            }
            l.omega = std::min(0.7, 1.0 / std::max(lambda, 1e-9));
        }
        l.b.assign(l.n, 0.0);
        l.t.assign(l.n, 0.0);
    }

    void coarsen(const Mat6& D1)
    {
        const Level& f = L.back();
        Level c;
        c.ex = (f.ex + 1) / 2; c.ey = (f.ey + 1) / 2; c.ez = (f.ez + 1) / 2;
        c.h = 2 * f.h;
        c.K = elementStiffness(type_, c.h, D1);
        std::vector<double> sum(size_t(c.ex) * c.ey * c.ez, 0.0);
        std::vector<char> used(sum.size(), 0);
        for (size_t a = 0; a < f.scale.size(); ++a)
        {
            const size_t e = (size_t(f.ek[a] / 2) * c.ey + f.ej[a] / 2) * c.ex + f.ei[a] / 2;
            sum[e] += f.scale[a];
            used[e] = 1;
        }
        c.dofOfNode.assign(size_t(c.ex + 1) * (c.ey + 1) * (c.ez + 1), -1);
        int next = 0;
        for (int k = 0; k < c.ez; ++k)
            for (int j = 0; j < c.ey; ++j)
                for (int i = 0; i < c.ex; ++i)
                {
                    const size_t e = (size_t(k) * c.ey + j) * c.ex + i;
                    if (!used[e]) continue;
                    c.ei.push_back(i); c.ej.push_back(j); c.ek.push_back(k);
                    c.scale.push_back(sum[e] / 8.0);
                    std::array<int, 8> d;
                    for (int q = 0; q < 8; ++q)
                    {
                        int& nd = c.dofOfNode[c.nodeIndex(i + CORNER[q][0], j + CORNER[q][1], k + CORNER[q][2])];
                        if (nd < 0) { nd = next; next += 3; }
                        d[size_t(q)] = nd;
                    }
                    c.dof.push_back(d);
                }
        c.n = size_t(next);
        c.fixed.assign(c.n, 0);
        // a coarse DOF is held when any fine DOF its basis function covers is
        for (int k = 0; k <= f.ez; ++k)
            for (int j = 0; j <= f.ey; ++j)
                for (int i = 0; i <= f.ex; ++i)
                {
                    const int fd = f.dofOfNode[f.nodeIndex(i, j, k)];
                    if (fd < 0) continue;
                    for (int q = 0; q < 3; ++q)
                    {
                        if (!f.fixed[size_t(fd + q)]) continue;
                        for (int K2 = k / 2; K2 <= (k + 1) / 2; ++K2)
                            for (int J2 = j / 2; J2 <= (j + 1) / 2; ++J2)
                                for (int I2 = i / 2; I2 <= (i + 1) / 2; ++I2)
                                {
                                    if (I2 > c.ex || J2 > c.ey || K2 > c.ez) continue;
                                    const int cd = c.dofOfNode[c.nodeIndex(I2, J2, K2)];
                                    if (cd >= 0) c.fixed[size_t(cd + q)] = 1;
                                }
                    }
                }
        // prolongation rows for the fine level
        Level& fm = L.back();
        fm.pDof.clear(); fm.pStart.assign(1, 0); fm.pIdx.clear(); fm.pW.clear();
        for (int k = 0; k <= f.ez; ++k)
            for (int j = 0; j <= f.ey; ++j)
                for (int i = 0; i <= f.ex; ++i)
                {
                    const int fd = f.dofOfNode[f.nodeIndex(i, j, k)];
                    if (fd < 0) continue;
                    int ci[2], cj[2], ck[2], ni, nj, nk;
                    double wi[2], wj[2], wk[2];
                    auto axis = [](int v, int* cv, double* w, int& cnt) {
                        if (v % 2 == 0) { cv[0] = v / 2; w[0] = 1; cnt = 1; }
                        else { cv[0] = (v - 1) / 2; cv[1] = (v + 1) / 2; w[0] = w[1] = 0.5; cnt = 2; }
                    };
                    axis(i, ci, wi, ni); axis(j, cj, wj, nj); axis(k, ck, wk, nk);
                    fm.pDof.push_back(fd);
                    for (int a = 0; a < nk; ++a)
                        for (int b = 0; b < nj; ++b)
                            for (int d = 0; d < ni; ++d)
                            {
                                if (ci[d] > c.ex || cj[b] > c.ey || ck[a] > c.ez) continue;
                                const int cd = c.dofOfNode[c.nodeIndex(ci[d], cj[b], ck[a])];
                                if (cd < 0) continue;
                                fm.pIdx.push_back(cd);
                                fm.pW.push_back(wi[d] * wj[b] * wk[a]);
                            }
                    fm.pStart.push_back(int(fm.pIdx.size()));
                }
        finish(c);
        L.push_back(std::move(c));
    }

    void matvec(const Level& l, const std::vector<double>& x, std::vector<double>& y) const
    {
        std::fill(y.begin(), y.end(), 0.0);
        for (const auto& cls : l.colour)
        {
            parallelFor(cls.size(), [&](size_t b0, size_t b1) {
                Vec24 ue, ye;
                for (size_t q = b0; q < b1; ++q)
                {
                    const size_t a = size_t(cls[q]);
                    for (int c = 0; c < 8; ++c)
                        for (int d = 0; d < 3; ++d) ue[3 * c + d] = x[size_t(l.dof[a][size_t(c)] + d)];
                    ye.noalias() = l.K * ue;
                    ye *= l.scale[a];
                    for (int c = 0; c < 8; ++c)
                        for (int d = 0; d < 3; ++d) y[size_t(l.dof[a][size_t(c)] + d)] += ye[3 * c + d];
                }
            }, 512);
        }
        for (size_t i = 0; i < l.n; ++i) if (l.fixed[i]) y[i] = 0;
    }

    static int sweeps()
    {
        static const int n = std::getenv("FIELDES_FEA_MG_SWEEPS") ? std::atoi(std::getenv("FIELDES_FEA_MG_SWEEPS")) : 2;
        return n;
    }

    void smooth(Level& l, const std::vector<double>& b, std::vector<double>& x, int sweeps)
    {
        const double omega = l.omega;
        for (int s = 0; s < sweeps; ++s)
        {
            matvec(l, x, l.t);
            parallelFor(l.n, [&](size_t b0, size_t b1) {
                for (size_t i = b0; i < b1; ++i) x[i] += omega * l.minv[i] * (b[i] - l.t[i]);
            }, 8192);
        }
    }

    void coarseSolve(Level& l, const std::vector<double>& b, std::vector<double>& x)
    {
        // Jacobi-PCG, solved tightly (so the preconditioner stays linear)
        const size_t n = l.n;
        std::vector<double> r(b), z(n), p(n), Ap(n);
        std::fill(x.begin(), x.end(), 0.0);
        for (size_t i = 0; i < n; ++i) if (l.fixed[i]) r[i] = 0;
        double bn = 0;
        for (size_t i = 0; i < n; ++i) bn += r[i] * r[i];
        if (!(bn > 0)) return;
        for (size_t i = 0; i < n; ++i) z[i] = l.minv[i] * r[i];
        p = z;
        double rz = 0;
        for (size_t i = 0; i < n; ++i) rz += r[i] * z[i];
        for (int it = 0; it < 300; ++it)
        {
            matvec(l, p, Ap);
            double pAp = 0;
            for (size_t i = 0; i < n; ++i) pAp += p[i] * Ap[i];
            if (!(pAp > 0)) break;
            const double alpha = rz / pAp;
            double rn = 0;
            for (size_t i = 0; i < n; ++i)
            {
                x[i] += alpha * p[i];
                r[i] -= alpha * Ap[i];
                rn += r[i] * r[i];
            }
            if (rn < 1e-12 * bn) break;
            double rzNew = 0;
            for (size_t i = 0; i < n; ++i) { z[i] = l.minv[i] * r[i]; rzNew += r[i] * z[i]; }
            const double beta = rzNew / rz;
            rz = rzNew;
            for (size_t i = 0; i < n; ++i) p[i] = z[i] + beta * p[i];
        }
    }

    void vcycle(size_t li, const std::vector<double>& b, std::vector<double>& x)
    {
        Level& l = L[li];
        if (li + 1 == L.size())
        {
            coarseSolve(l, b, x);
            return;
        }
        smooth(l, b, x, sweeps());
        // residual, restricted to the coarser grid
        matvec(l, x, l.t);
        std::vector<double> res(l.n);
        for (size_t i = 0; i < l.n; ++i) res[i] = l.fixed[i] ? 0.0 : b[i] - l.t[i];
        Level& c = L[li + 1];
        std::vector<double> rc(c.n, 0.0), xc(c.n, 0.0);
        for (size_t row = 0; row < l.pDof.size(); ++row)
        {
            const int fd = l.pDof[row];
            for (int q = l.pStart[row]; q < l.pStart[row + 1]; ++q)
                for (int d = 0; d < 3; ++d)
                    rc[size_t(l.pIdx[size_t(q)] + d)] += l.pW[size_t(q)] * res[size_t(fd + d)];
        }
        for (size_t i = 0; i < c.n; ++i) if (c.fixed[i]) rc[i] = 0;
        vcycle(li + 1, rc, xc);
        // prolongate the correction
        parallelFor(l.pDof.size(), [&](size_t r0, size_t r1) {
            for (size_t row = r0; row < r1; ++row)
            {
                const int fd = l.pDof[row];
                for (int d = 0; d < 3; ++d)
                {
                    if (l.fixed[size_t(fd + d)]) continue;
                    double s = 0;
                    for (int q = l.pStart[row]; q < l.pStart[row + 1]; ++q)
                        s += l.pW[size_t(q)] * xc[size_t(l.pIdx[size_t(q)] + d)];
                    x[size_t(fd + d)] += s;
                }
            }
        }, 4096);
        smooth(l, b, x, sweeps());
    }
};

uint64_t fnv(uint64_t h, const void* data, size_t n)
{
    const uint8_t* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < n; ++i)
    {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}

}   // anonymous namespace

void parallelRange(size_t n, const std::function<void(size_t, size_t)>& fn, size_t minChunk)
{
    parallelFor(n, fn, minChunk);
}

double parallelTotal(size_t n, const std::function<double(size_t, size_t)>& fn)
{
    return parallelSum(n, fn);
}

////////////////////////////////////////////////////////////////////////////////

StaticProblem::StaticProblem(const Tree& shape, Eigen::Vector3d lo, Eigen::Vector3d hi,
                             double h, double E, double nu)
    : m_shape(shape), m_lo(lo), m_h(h), m_E(E), m_nu(nu)
{
    if (h > 0)
    {
        m_ex = std::max(1, int(std::ceil((hi.x() - lo.x()) / h - 1e-9)));
        m_ey = std::max(1, int(std::ceil((hi.y() - lo.y()) / h - 1e-9)));
        m_ez = std::max(1, int(std::ceil((hi.z() - lo.z()) / h - 1e-9)));
    }
}

void StaticProblem::addSupport(const Tree& region, bool x, bool y, bool z)
{
    m_supports.push_back(Support{region, {x, y, z}});
    m_prepared = false;
}

void StaticProblem::addForce(const Tree& region, Eigen::Vector3d total, int loadCase)
{
    m_forces.push_back(Force{region, total, std::max(0, loadCase)});
    m_prepared = false;
}

int StaticProblem::loadCases() const
{
    int n = 1;
    for (const auto& f : m_forces) n = std::max(n, f.loadCase + 1);
    return n;
}

void StaticProblem::setThermal(const Tree& temperature, double alpha, double reference)
{
    m_temperature = temperature;
    m_alpha = alpha;
    m_reference = reference;
    m_prepared = false;
}

void StaticProblem::setGravity(Eigen::Vector3d g, double density)
{
    m_gravity = g;
    m_density = density;
    m_prepared = false;
}

bool StaticProblem::prepare(std::string& error)
{
    m_prepared = false;
    if (!(m_h > 0) || !(m_E > 0) || !(m_nu > -1 && m_nu < 0.5))
    {
        error = "invalid element size or material (need E > 0 and -1 < nu < 0.5)";
        return false;
    }
    const size_t nElem = size_t(m_ex) * m_ey * m_ez;
    if (nElem > 8000000)
    {
        error = "too many elements (" + std::to_string(nElem) + "): use a larger element size";
        return false;
    }
    const int nxn = m_ex + 1, nyn = m_ey + 1, nzn = m_ez + 1;
    const size_t nNode = size_t(nxn) * nyn * nzn;

    auto nodePos = [&](int i, int j, int k) {
        return Eigen::Vector3f(float(m_lo.x() + m_h * i), float(m_lo.y() + m_h * j),
                               float(m_lo.z() + m_h * k));
    };

    // Progress: the points evaluated (nearly all of the time here) against
    // the points to evaluate -- the nodes, the element centres, 27 in each
    // element the surface crosses, and the nodes in use once per support
    // and load region.  The last two are known once the nodes are; until
    // then, from a coarse grid (every 4th node: 1/64 of the work).
    run_progress::Task task("preparing the FEA grid");
    EvalCount counted;
    counted.task = &task;
    t_evalCount = nullptr;
    struct Uncount { ~Uncount() { t_evalCount = nullptr; } } uncount;
    const size_t regions = m_supports.size() + m_forces.size();
    {
        const int cx = (nxn + 3) / 4, cy = (nyn + 3) / 4, cz = (nzn + 3) / 4;
        std::vector<Eigen::Vector3f> coarse;
        coarse.reserve(size_t(cx) * cy * cz);
        for (int k = 0; k < cz; ++k)
            for (int j = 0; j < cy; ++j)
                for (int i = 0; i < cx; ++i)
                    coarse.push_back(nodePos(std::min(4 * i, m_ex), std::min(4 * j, m_ey), std::min(4 * k, m_ez)));
        std::vector<float> cv;
        evalPoints(m_shape, coarse, cv);
        auto at = [&](int i, int j, int k) { return cv[(size_t(k) * cy + j) * cx + i]; };
        size_t crossed = 0, inside = 0;
        for (float v : cv) inside += v <= 0;
        for (int k = 0; k + 1 < cz; ++k)
            for (int j = 0; j + 1 < cy; ++j)
                for (int i = 0; i + 1 < cx; ++i)
                {
                    int in = 0;
                    for (auto& c : CORNER) in += at(i + c[0], j + c[1], k + c[2]) <= 0;
                    crossed += in > 0 && in < 8;
                }
        // a coarse cell the surface crosses holds ~4x4 fine ones it crosses
        counted.total = nNode + nElem + 27 * 16 * crossed + regions * 64 * inside;
    }
    t_evalCount = &counted;

    // The shape's field at every node and element centre
    std::vector<Eigen::Vector3f> pts(nNode);
    for (int k = 0; k < nzn; ++k)
        for (int j = 0; j < nyn; ++j)
            for (int i = 0; i < nxn; ++i)
                pts[node(i, j, k)] = nodePos(i, j, k);
    std::vector<float> nodeVal;
    evalPoints(m_shape, pts, nodeVal);
    {   // (now known: the elements whose corners disagree, the nodes near the part)
        size_t crossed = 0, nearPart = 0;
        for (int k = 0; k < m_ez; ++k)
            for (int j = 0; j < m_ey; ++j)
                for (int i = 0; i < m_ex; ++i)
                {
                    int in = 0;
                    for (auto& c : CORNER) in += nodeVal[node(i + c[0], j + c[1], k + c[2])] <= 0;
                    crossed += in > 0 && in < 8;
                }
        for (float v : nodeVal) nearPart += v <= m_h;
        counted.total = nNode + nElem + 27 * crossed + regions * nearPart;
    }

    pts.resize(nElem);
    const float hh = float(m_h);
    for (int k = 0; k < m_ez; ++k)
        for (int j = 0; j < m_ey; ++j)
            for (int i = 0; i < m_ex; ++i)
                pts[elem(i, j, k)] = nodePos(i, j, k) + Eigen::Vector3f(hh, hh, hh) * 0.5f;
    std::vector<float> centreVal;
    evalPoints(m_shape, pts, centreVal);
    std::vector<Eigen::Vector3f>().swap(pts);

    // Volume fraction: from the 8 corners and the centre, refined with a
    // 3x3x3 sub-sample where they disagree
    m_fraction.assign(nElem, 0.0f);
    std::vector<size_t> mixed;
    for (int k = 0; k < m_ez; ++k)
        for (int j = 0; j < m_ey; ++j)
            for (int i = 0; i < m_ex; ++i)
            {
                int inside = centreVal[elem(i, j, k)] <= 0;
                for (auto& c : CORNER) inside += nodeVal[node(i + c[0], j + c[1], k + c[2])] <= 0;
                if (inside == 9) m_fraction[elem(i, j, k)] = 1.0f;
                else if (inside > 0) mixed.push_back(elem(i, j, k));
            }
    if (!mixed.empty())
    {
        std::vector<Eigen::Vector3f> sub(mixed.size() * 27);
        for (size_t m = 0; m < mixed.size(); ++m)
        {
            const size_t e = mixed[m];
            const int i = int(e % m_ex), j = int((e / m_ex) % m_ey), k = int(e / (size_t(m_ex) * m_ey));
            const Eigen::Vector3f o = nodePos(i, j, k);
            int s = 0;
            for (int a = 0; a < 3; ++a)
                for (int b = 0; b < 3; ++b)
                    for (int c = 0; c < 3; ++c)
                        sub[m * 27 + s++] = o + hh * Eigen::Vector3f((a + 0.5f) / 3, (b + 0.5f) / 3, (c + 0.5f) / 3);
        }
        std::vector<float> subVal;
        evalPoints(m_shape, sub, subVal);
        for (size_t m = 0; m < mixed.size(); ++m)
        {
            int inside = 0;
            for (int s = 0; s < 27; ++s) inside += subVal[m * 27 + s] <= 0;
            m_fraction[mixed[m]] = inside / 27.0f;
        }
    }

    // Active elements, their nodes, DOF numbering
    m_active.clear();
    for (size_t e = 0; e < nElem; ++e)
    {
        if (m_fraction[e] > 0) m_active.push_back(int(e));
    }
    if (m_active.empty())
    {
        error = "the part has no material inside the analysis region";
        return false;
    }

    // Material that no support holds -- a loose piece, not connected to the
    // part that is held -- can carry nothing and makes the system singular
    // (a topology optimization's result, cut at a density, can leave small
    // islands; an imported part a loose sliver): left out, and counted.
    // Connected = sharing at least a node, flooded from the elements that
    // touch a support region.
    m_looseElements = 0;
    if (!m_supports.empty())
    {
        std::vector<char> isActive(nElem, 0);
        for (int e : m_active) isActive[size_t(e)] = 1;
        std::vector<Eigen::Vector3f> ep;
        ep.reserve(m_active.size() * 8);
        for (int e : m_active)
        {
            const int i = e % m_ex, j = (e / m_ex) % m_ey, k = e / (m_ex * m_ey);
            for (auto& c : CORNER) ep.push_back(nodePos(i + c[0], j + c[1], k + c[2]));
        }
        std::vector<char> reached(nElem, 0);
        std::deque<int> queue;
        const float reachS = float(0.5 * m_h);
        for (const auto& s : m_supports)
        {
            std::vector<float> v;
            evalPoints(s.region, ep, v);
            for (size_t a = 0; a < m_active.size(); ++a)
            {
                bool touches = false;
                for (int c = 0; c < 8 && !touches; ++c) touches = v[a * 8 + size_t(c)] <= reachS;
                if (touches && !reached[size_t(m_active[a])])
                {
                    reached[size_t(m_active[a])] = 1;
                    queue.push_back(m_active[a]);
                }
            }
        }
        if (!queue.empty())
        {
            while (!queue.empty())
            {
                const int e = queue.front();
                queue.pop_front();
                const int i = e % m_ex, j = (e / m_ex) % m_ey, k = e / (m_ex * m_ey);
                for (int dk = -1; dk <= 1; ++dk)
                    for (int dj = -1; dj <= 1; ++dj)
                        for (int di = -1; di <= 1; ++di)
                        {
                            const int a = i + di, b = j + dj, c = k + dk;
                            if (a < 0 || b < 0 || c < 0 || a >= m_ex || b >= m_ey || c >= m_ez) continue;
                            const size_t o = elem(a, b, c);
                            if (!isActive[o] || reached[o]) continue;
                            reached[o] = 1;
                            queue.push_back(int(o));
                        }
            }
            std::vector<int> kept;
            for (int e : m_active)
            {
                if (reached[size_t(e)]) kept.push_back(e);
                else
                {
                    m_fraction[size_t(e)] = 0.0f;
                    m_looseElements++;
                }
            }
            m_active.swap(kept);
        }
    }
    std::vector<unsigned char> used(nNode, 0), surface(nNode, 0);
    std::vector<unsigned char> activeCount(nNode, 0);
    for (int e : m_active)
    {
        const int i = e % m_ex, j = (e / m_ex) % m_ey, k = e / (m_ex * m_ey);
        for (auto& c : CORNER)
        {
            const size_t n = node(i + c[0], j + c[1], k + c[2]);
            used[n] = 1;
            activeCount[n]++;
        }
    }
    m_dofOfNode.assign(nNode, -1);
    int dofs = 0;
    for (int k = 0; k < nzn; ++k)
        for (int j = 0; j < nyn; ++j)
            for (int i = 0; i < nxn; ++i)
            {
                const size_t n = node(i, j, k);
                if (!used[n]) continue;
                m_dofOfNode[n] = dofs;
                dofs += 3;
                // A node is on the surface when some element around it (or
                // the grid's edge) is empty
                int around = 0;
                for (int dk = -1; dk <= 0; ++dk)
                    for (int dj = -1; dj <= 0; ++dj)
                        for (int di = -1; di <= 0; ++di)
                        {
                            const int a = i + di, b = j + dj, c = k + dk;
                            if (a >= 0 && b >= 0 && c >= 0 && a < m_ex && b < m_ey && c < m_ez &&
                                m_fraction[elem(a, b, c)] >= 0.999f)
                            {
                                around++;
                            }
                        }
                surface[n] = around < 8;
            }
    m_dofs = dofs;

    // Supports and loads, from their regions at the used nodes
    std::vector<size_t> usedNodes;
    std::vector<Eigen::Vector3f> usedPts;
    for (int k = 0; k < nzn; ++k)
        for (int j = 0; j < nyn; ++j)
            for (int i = 0; i < nxn; ++i)
                if (used[node(i, j, k)])
                {
                    usedNodes.push_back(node(i, j, k));
                    usedPts.push_back(nodePos(i, j, k));
                }
    m_fixed.assign(size_t(dofs), 0);
    m_force.assign(size_t(dofs), 0.0);
    // Grid nodes rarely lie exactly on a face: a node within half an element
    // of a region counts as inside it
    const float reach = float(0.5 * m_h);
    std::vector<unsigned char> fixedNode(nNode, 0);
    for (const auto& s : m_supports)
    {
        std::vector<float> v;
        evalPoints(s.region, usedPts, v);
        for (size_t u = 0; u < usedNodes.size(); ++u)
        {
            if (!(v[u] <= reach)) continue;
            const int d = m_dofOfNode[usedNodes[u]];
            for (int a = 0; a < 3; ++a) if (s.fix[a]) m_fixed[size_t(d + a)] = 1;
            fixedNode[usedNodes[u]] = 1;
        }
    }
    m_fixedNodes = 0;
    for (auto f : fixedNode) m_fixedNodes += f;
    if (m_fixedNodes == 0)
    {
        error = m_supports.empty() ? "no supports: add fixed(region) touching the part"
                                   : "the support regions don't touch the part";
        return false;
    }

    m_loadedNodes = 0;
    Eigen::Vector3d totalLoad = Eigen::Vector3d::Zero();
    const int nCases = loadCases();
    m_caseForce.clear();
    if (nCases > 1) m_caseForce.assign(size_t(nCases), std::vector<double>(size_t(dofs), 0.0));
    for (const auto& f : m_forces)
    {
        std::vector<float> v;
        evalPoints(f.region, usedPts, v);
        std::vector<size_t> onSurface, anywhere;
        for (size_t u = 0; u < usedNodes.size(); ++u)
        {
            if (!(v[u] <= reach)) continue;
            anywhere.push_back(usedNodes[u]);
            if (surface[usedNodes[u]]) onSurface.push_back(usedNodes[u]);
        }
        const auto& target = onSurface.empty() ? anywhere : onSurface;
        if (target.empty())
        {
            error = "a load region doesn't touch the part";
            return false;
        }
        const Eigen::Vector3d each = f.total / double(target.size());
        for (size_t n : target)
        {
            const int d = m_dofOfNode[n];
            for (int a = 0; a < 3; ++a) m_force[size_t(d + a)] += each[a];
            if (nCases > 1)
                for (int a = 0; a < 3; ++a) m_caseForce[size_t(f.loadCase)][size_t(d + a)] += each[a];
        }
        m_loadedNodes += int(target.size());
        totalLoad += f.total;
    }
    if (m_density > 0 && m_gravity.norm() > 0)
    {
        const double vol = std::pow(m_h, 3);
        for (int e : m_active)
        {
            const int i = e % m_ex, j = (e / m_ex) % m_ey, k = e / (m_ex * m_ey);
            const Eigen::Vector3d body = m_gravity * (m_density * vol * m_fraction[size_t(e)] / 8.0);
            for (auto& c : CORNER)
            {
                const int d = m_dofOfNode[node(i + c[0], j + c[1], k + c[2])];
                for (int a = 0; a < 3; ++a) m_force[size_t(d + a)] += body[a];
                for (auto& cf : m_caseForce)     // (gravity: in every case)
                    for (int a = 0; a < 3; ++a) cf[size_t(d + a)] += body[a];
            }
            totalLoad += body * 8.0;
        }
    }
    // Thermal expansion: each element's strain alpha (T - reference) at its
    // centre, in every direction, becomes nodal loads -- the integral of
    // B^T D eps over the element (the incompatible modes' strains average
    // to zero over it, so they take none), scaled like its stiffness
    m_thermalStrain.clear();
    if (m_temperature.is_valid() && m_alpha != 0)
    {
        std::vector<Eigen::Vector3f> centres(m_active.size());
        for (size_t a = 0; a < m_active.size(); ++a)
        {
            const int e = m_active[a];
            const int i = e % m_ex, j = (e / m_ex) % m_ey, k = e / (m_ex * m_ey);
            centres[a] = (m_lo + Eigen::Vector3d(i + 0.5, j + 0.5, k + 0.5) * m_h).cast<float>();
        }
        std::vector<float> T;
        evalPoints(m_temperature, centres, T);
        const Eigen::Matrix<double, 24, 6> Bint = integratedStrain(m_element, m_h);
        const Mat6 D1 = elasticity(1.0, m_nu);
        m_thermalStrain.assign(m_active.size(), 0.0);
        for (size_t a = 0; a < m_active.size(); ++a)
        {
            if (!std::isfinite(T[a]))
            {
                error = "the temperature field isn't defined everywhere in the part";
                return false;
            }
            const double et = m_alpha * (double(T[a]) - m_reference);
            m_thermalStrain[a] = et;
            if (et == 0) continue;
            Eigen::Matrix<double, 6, 1> eps;
            eps << et, et, et, 0, 0, 0;
            const int e = m_active[a];
            const double scale = m_E * std::max(0.01, double(m_fraction[size_t(e)]));
            const Eigen::Matrix<double, 24, 1> fe = scale * (Bint * (D1 * eps));
            const int i = e % m_ex, j = (e / m_ex) % m_ey, k = e / (m_ex * m_ey);
            for (int c = 0; c < 8; ++c)
            {
                const int d = m_dofOfNode[node(i + CORNER[c][0], j + CORNER[c][1], k + CORNER[c][2])];
                for (int q = 0; q < 3; ++q)
                {
                    m_force[size_t(d + q)] += fe[3 * c + q];
                    for (auto& cf : m_caseForce) cf[size_t(d + q)] += fe[3 * c + q];
                }
            }
        }
    }
    double fnorm = 0;
    for (size_t d = 0; d < m_force.size(); ++d) if (!m_fixed[d]) fnorm += m_force[d] * m_force[d];
    if (!(fnorm > 0) && !m_noLoads)
    {
        error = (m_forces.empty() && !(m_density > 0) && m_thermalStrain.empty())
            ? "no loads: add force(region, (fx, fy, fz)), gravity or thermal_expansion"
            : m_forces.empty() && !(m_density > 0)
            ? "the part is held everywhere it would expand, or the temperature is the reference "
              "temperature everywhere: no load"
            : "every load acts on fixed nodes only";
        return false;
    }
    for (size_t c = 0; c < m_caseForce.size(); ++c)
    {
        double fc = 0;
        for (size_t d = 0; d < m_caseForce[c].size(); ++d) if (!m_fixed[d]) fc += m_caseForce[c][d] * m_caseForce[c][d];
        if (!(fc > 0))
        {
            error = "load case " + std::to_string(c + 1) + " has no load (or only on fixed nodes)";
            return false;
        }
    }

    // Fingerprint (for caching solves)
    uint64_t hsh = 1469598103934665603ull;
    const double hdr[9] = {m_lo.x(), m_lo.y(), m_lo.z(), m_h, m_E, m_nu, double(m_ex), double(m_ey * 1e4 + m_ez),
                           double(int(m_element))};
    hsh = fnv(hsh, hdr, sizeof(hdr));
    hsh = fnv(hsh, m_fraction.data(), m_fraction.size() * sizeof(float));
    hsh = fnv(hsh, m_fixed.data(), m_fixed.size());
    hsh = fnv(hsh, m_force.data(), m_force.size() * sizeof(double));
    for (const auto& cf : m_caseForce) hsh = fnv(hsh, cf.data(), cf.size() * sizeof(double));
    m_hash = hsh;

    m_result = std::make_shared<Result>();
    m_result->totalLoad = totalLoad;
    m_result->looseElements = m_looseElements;
    m_prepared = true;
    return true;
}

bool StaticProblem::solve(int maxIterations, double tolerance, std::string& error,
                          const std::atomic<bool>* cancel)
{
    if (!m_prepared && !prepare(error)) return false;
    const auto t0 = std::chrono::steady_clock::now();
    // Progress: how far the residual has fallen from where it started to
    // the tolerance, on a log scale (it falls about geometrically), or the
    // iterations used of the most allowed, whichever is further
    run_progress::Task task("solving");
    task.set(0.0, "setting up the solver");

    const Mat6 D1 = elasticity(1.0, m_nu);
    Mat9x24 recover;
    const Mat24 K1 = elementStiffness(m_element, m_h, D1, &recover);
    const size_t n = size_t(m_dofs);

    // Per active element: stiffness scale and the DOFs of its nodes; the
    // elements in 8 colour classes that share no nodes (parallel scatter)
    struct Elem { int dof[8]; double scale; };
    std::vector<Elem> elems(m_active.size());
    std::vector<std::vector<int>> colour(8);
    for (size_t a = 0; a < m_active.size(); ++a)
    {
        const int e = m_active[a];
        const int i = e % m_ex, j = (e / m_ex) % m_ey, k = e / (m_ex * m_ey);
        for (int c = 0; c < 8; ++c)
        {
            elems[a].dof[c] = m_dofOfNode[node(i + CORNER[c][0], j + CORNER[c][1], k + CORNER[c][2])];
        }
        // Partly filled elements are proportionally softer (not below 1%,
        // which would make the system badly conditioned)
        elems[a].scale = m_E * std::max(0.01, double(m_fraction[size_t(e)])) *
                         (m_elemScale.size() == m_active.size() ? m_elemScale[a] : 1.0);
        colour[size_t((i & 1) | ((j & 1) << 1) | ((k & 1) << 2))].push_back(int(a));
    }

    auto matvec = [&](const std::vector<double>& x, std::vector<double>& y) {
        std::fill(y.begin(), y.end(), 0.0);
        for (const auto& cls : colour)
        {
            parallelFor(cls.size(), [&](size_t b0, size_t b1) {
                Vec24 ue, ye;
                for (size_t q = b0; q < b1; ++q)
                {
                    const Elem& el = elems[size_t(cls[q])];
                    for (int c = 0; c < 8; ++c)
                        for (int a = 0; a < 3; ++a) ue[3 * c + a] = x[size_t(el.dof[c] + a)];
                    ye.noalias() = K1 * ue;
                    ye *= el.scale;
                    for (int c = 0; c < 8; ++c)
                        for (int a = 0; a < 3; ++a) y[size_t(el.dof[c] + a)] += ye[3 * c + a];
                }
            }, 512);
        }
    };
    auto dot = [&](const std::vector<double>& a, const std::vector<double>& b) {
        return parallelSum(n, [&](size_t b0, size_t b1) {
            double s = 0;
            for (size_t i = b0; i < b1; ++i) s += a[i] * b[i];
            return s;
        });
    };

    // Jacobi preconditioner
    std::vector<double> diag(n, 0.0);
    for (const auto& el : elems)
        for (int c = 0; c < 8; ++c)
            for (int a = 0; a < 3; ++a) diag[size_t(el.dof[c] + a)] += el.scale * K1(3 * c + a, 3 * c + a);
    std::vector<double> minv(n);
    for (size_t i = 0; i < n; ++i) minv[i] = (m_fixed[i] || !(diag[i] > 0)) ? 0.0 : 1.0 / diag[i];

    // A direction nothing holds, with a net force along it: the part would
    // just slide away (the solver would "converge" to a huge rigid motion)
    {
        const char* axisName[3] = {"x", "y", "z"};
        for (int a = 0; a < 3; ++a)
        {
            bool held = false;
            double net = 0, scale = 0;
            for (size_t i = size_t(a); i < n; i += 3)
            {
                held = held || m_fixed[i];
                net += m_force[i];
                scale += std::abs(m_force[i]);
            }
            if (!held && std::abs(net) > 1e-9 * std::max(scale, 1e-30))
            {
                error = std::string("nothing holds the part in the ") + axisName[a] +
                        " direction, but the loads push it that way: fix it in " +
                        axisName[a] + " somewhere (fixed(region) holds all directions)";
                return false;
            }
        }
    }

    // Multigrid preconditioning for all but small problems (Jacobi alone
    // needs thousands of iterations on fine grids)
    std::unique_ptr<Multigrid> mg;
    if (m_active.size() > 1000 && !std::getenv("FIELDES_FEA_NO_MG"))
    {
        std::vector<Multigrid::FineElem> fine(m_active.size());
        for (size_t a = 0; a < m_active.size(); ++a)
        {
            const int e = m_active[a];
            fine[a].i = e % m_ex;
            fine[a].j = (e / m_ex) % m_ey;
            fine[a].k = e / (m_ex * m_ey);
            fine[a].scale = elems[a].scale;
            for (int c = 0; c < 8; ++c) fine[a].dof[c] = elems[a].dof[c];
        }
        mg.reset(new Multigrid(m_ex, m_ey, m_ez, m_h, m_nu, fine, m_dofOfNode, m_fixed, n, m_element));
        if (std::getenv("FIELDES_FEA_DEBUG"))
            fprintf(stderr, "[fea] multigrid: %zu levels\n", mg->levels());
    }
    auto precondition = [&](const std::vector<double>& rr, std::vector<double>& zz) {
        if (mg)
        {
            mg->apply(rr, zz);
            for (size_t i = 0; i < n; ++i) if (m_fixed[i]) zz[i] = 0;
        }
        else
        {
            for (size_t i = 0; i < n; ++i) zz[i] = minv[i] * rr[i];
        }
    };

    std::vector<double> u(n, 0.0), r(n), z(n), p(n), Ap(n);
    for (size_t i = 0; i < n; ++i) r[i] = m_fixed[i] ? 0.0 : m_force[i];
    const double fnorm = std::sqrt(dot(r, r));
    if (m_quick && m_lastU.size() == n)
    {
        // warm start from the previous solution (optimization iterations)
        u = m_lastU;
        matvec(u, Ap);
        for (size_t i = 0; i < n; ++i) r[i] = m_fixed[i] ? 0.0 : m_force[i] - Ap[i];
    }
    precondition(r, z);
    p = z;
    double rz = dot(r, z);
    std::vector<double> zOld;
    int it = 0;
    double rel = fnorm > 0 ? std::sqrt(dot(r, r)) / fnorm : 0.0;
    const double rel0 = rel;
    auto report = [&]() {
        double f = double(it) / std::max(1, maxIterations);
        if (rel0 > tolerance && rel > 0 && std::isfinite(rel))
            f = std::max(f, std::log(rel0 / rel) / std::log(rel0 / tolerance));
        char buf[64];
        snprintf(buf, sizeof(buf), "solving, residual %.0e of %.0e", rel, tolerance);
        task.set(f, buf);
    };
    report();
    // (a warm start may already be converged)
    for (; it < maxIterations && !(rel < tolerance); ++it)
    {
        if (cancel && cancel->load())
        {
            error = "cancelled";
            return false;
        }
        matvec(p, Ap);
        for (size_t i = 0; i < n; ++i) if (m_fixed[i]) Ap[i] = 0;
        const double pAp = dot(p, Ap);
        if (!(pAp > 0))
        {
            error = "the supports don't hold the part: fix more of it, or in more directions";
            return false;
        }
        const double alpha = rz / pAp;
        parallelFor(n, [&](size_t b0, size_t b1) {
            for (size_t i = b0; i < b1; ++i)
            {
                u[i] += alpha * p[i];
                r[i] -= alpha * Ap[i];
            }
        }, 8192);
        rel = std::sqrt(dot(r, r)) / fnorm;
        report();
        if (std::getenv("FIELDES_FEA_DEBUG") && (it % 100 == 0))
            fprintf(stderr, "[fea]   iteration %d residual %.3e\n", it, rel);
        if (rel < tolerance)
        {
            ++it;
            break;
        }
        // (flexible CG: the multigrid preconditioner is only nearly linear)
        zOld = z;
        precondition(r, z);
        const double rzNew = dot(r, z);
        double beta = rzNew / rz;
        static const bool flex = !std::getenv("FIELDES_FEA_MG_FLEX") || std::atoi(std::getenv("FIELDES_FEA_MG_FLEX")) != 0;
        if (mg && flex)
        {
            double rzo = 0;
            for (size_t i = 0; i < n; ++i) rzo += r[i] * zOld[i];
            beta = std::max(0.0, (rzNew - rzo) / rz);
        }
        rz = rzNew;
        parallelFor(n, [&](size_t b0, size_t b1) {
            for (size_t i = b0; i < b1; ++i) p[i] = z[i] + beta * p[i];
        }, 8192);
        if (!std::isfinite(rel))
        {
            error = "the solver diverged";
            return false;
        }
    }
    if (rel > std::max(tolerance * 100, 1e-3))
    {
        error = "the solver did not converge (" + std::to_string(it) + " iterations, residual " +
                std::to_string(rel) + "): the supports may not hold the part in place";
        return false;
    }

    // Held in every direction, but still free to turn (e.g. held along a
    // single line): the solver then drifts into a huge rigid rotation.
    // Real deflections are far smaller than the part.
    {
        double umax = 0;
        for (size_t i = 0; i < n; ++i) umax = std::max(umax, std::abs(u[i]));
        const double size = m_h * std::max(m_ex, std::max(m_ey, m_ez));
        if (!(umax < 10 * size))
        {
            error = "the supports don't hold the part: fix more of it, or in more directions";
            return false;
        }
    }

    // Reactions at the supports, compliance
    std::vector<double> Ku(n);
    matvec(u, Ku);
    Eigen::Vector3d reaction = Eigen::Vector3d::Zero();
    for (size_t i = 0; i < n; ++i) if (m_fixed[i]) reaction[int(i % 3)] += Ku[i] - m_force[i];
    double compliance = 0;
    for (size_t i = 0; i < n; ++i) compliance += m_force[i] * u[i];
    static const bool feaDebug = std::getenv("FIELDES_FEA_DEBUG") != nullptr;
    if (feaDebug)
    {
        fprintf(stderr, "[fea] %zu elements, %s, %d iterations, residual %.2e, %.3f s\n",
                m_active.size(), mg ? (std::to_string(mg->levels()) + " grid levels").c_str() : "jacobi",
                it, rel, std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    }
    if (m_quick)
    {
        m_lastU = u;
        m_result->compliance = compliance;
        m_result->iterations = it;
        m_result->residual = rel;
        return true;
    }

    // Stresses at each element's own corners (with its incompatible modes,
    // which make a linear stress variation exact), then fraction-weighted
    // averages of the stress tensor per node; von Mises and principal
    // stresses from the averaged tensor
    const Mat6 D = elasticity(m_E, m_nu);
    Result& R = *m_result;
    R.lo = m_lo;
    R.h = m_h;
    R.tetrahedra = m_element == Element::Tet;
    R.nx = m_ex + 1;
    R.ny = m_ey + 1;
    R.nz = m_ez + 1;
    const size_t nNode = size_t(R.nx) * R.ny * R.nz;
    for (auto& f : R.fields) f.assign(nNode, 0.0f);
    std::vector<double> weight(nNode, 0.0);
    std::vector<std::array<double, 7>> acc(nNode);      // 6 stresses, energy
    for (auto& a : acc) a.fill(0.0);
    Mat6x24 Bc[8];
    Mat6x9 Gc[8];
    for (int c = 0; c < 8; ++c)
    {
        const double xi = 2.0 * CORNER[c][0] - 1, eta = 2.0 * CORNER[c][1] - 1,
                     zeta = 2.0 * CORNER[c][2] - 1;
        Bc[c] = cornerStrain(m_element, m_h, c);
        Gc[c] = incompatibleStrain(m_h, xi, eta, zeta);      // (its amplitudes are zero without modes)
    }
    for (size_t a = 0; a < m_active.size(); ++a)
    {
        const Elem& el = elems[a];
        const int e = m_active[a];
        const int i = e % m_ex, j = (e / m_ex) % m_ey, k = e / (m_ex * m_ey);
        Vec24 ue;
        for (int c = 0; c < 8; ++c)
            for (int q = 0; q < 3; ++q) ue[3 * c + q] = u[size_t(el.dof[c] + q)];
        const Eigen::Matrix<double, 9, 1> alpha = recover * ue;
        const double w = std::max(0.02, double(m_fraction[size_t(e)]));
        for (int c = 0; c < 8; ++c)
        {
            Eigen::Matrix<double, 6, 1> eps = Bc[c] * ue + Gc[c] * alpha;
            if (!m_thermalStrain.empty())
                for (int q = 0; q < 3; ++q) eps[q] -= m_thermalStrain[a];   // (the stress-free part)
            const Eigen::Matrix<double, 6, 1> sig = D * eps;
            const size_t nd = R.index(i + CORNER[c][0], j + CORNER[c][1], k + CORNER[c][2]);
            weight[nd] += w;
            for (int q = 0; q < 6; ++q) acc[nd][size_t(q)] += w * sig[q];
            acc[nd][6] += w * 0.5 * sig.dot(eps);
        }
    }
    std::vector<unsigned char> known(nNode, 0);
    for (int k = 0; k < R.nz; ++k)
        for (int j = 0; j < R.ny; ++j)
            for (int i = 0; i < R.nx; ++i)
            {
                const size_t nd = R.index(i, j, k);
                const int d = m_dofOfNode[node(i, j, k)];
                if (d < 0 || !(weight[nd] > 0)) continue;
                known[nd] = 1;
                const double sxx = acc[nd][0] / weight[nd], syy = acc[nd][1] / weight[nd],
                             szz = acc[nd][2] / weight[nd], sxy = acc[nd][3] / weight[nd],
                             syz = acc[nd][4] / weight[nd], szx = acc[nd][5] / weight[nd];
                const double vm = std::sqrt(0.5 * ((sxx - syy) * (sxx - syy) + (syy - szz) * (syy - szz) +
                                                   (szz - sxx) * (szz - sxx)) +
                                            3 * (sxy * sxy + syz * syz + szx * szx));
                Eigen::Matrix3d S;
                S << sxx, sxy, szx, sxy, syy, syz, szx, syz, szz;
                const Eigen::Vector3d pr =
                    Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d>(S, Eigen::EigenvaluesOnly).eigenvalues();
                R.fields[Result::VON_MISES][nd] = float(vm);
                R.fields[Result::SXX][nd] = float(sxx);
                R.fields[Result::SYY][nd] = float(syy);
                R.fields[Result::SZZ][nd] = float(szz);
                R.fields[Result::SXY][nd] = float(sxy);
                R.fields[Result::SYZ][nd] = float(syz);
                R.fields[Result::SZX][nd] = float(szx);
                R.fields[Result::MAX_PRINCIPAL][nd] = float(pr[2]);
                R.fields[Result::MIN_PRINCIPAL][nd] = float(pr[0]);
                R.fields[Result::STRAIN_ENERGY][nd] = float(acc[nd][6] / weight[nd]);
                const double ux = u[size_t(d)], uy = u[size_t(d + 1)], uz = u[size_t(d + 2)];
                R.fields[Result::UX][nd] = float(ux);
                R.fields[Result::UY][nd] = float(uy);
                R.fields[Result::UZ][nd] = float(uz);
                R.fields[Result::DISPLACEMENT][nd] = float(std::sqrt(ux * ux + uy * uy + uz * uz));
            }
    for (int f = 0; f < Result::FIELD_COUNT; ++f)
    {
        float mn = 1e30f, mx = -1e30f;
        for (size_t nd = 0; nd < nNode; ++nd)
        {
            if (!known[nd]) continue;
            mn = std::min(mn, R.fields[f][nd]);
            mx = std::max(mx, R.fields[f][nd]);
        }
        R.minValue[f] = mn;
        R.maxValue[f] = mx;
    }

    // Nodes off the part take their nearest part node's values, so a
    // surface lying between grid nodes reads the part's values
    {
        std::deque<size_t> queue;
        for (size_t nd = 0; nd < nNode; ++nd) if (known[nd]) queue.push_back(nd);
        std::vector<size_t> source(nNode, size_t(-1));
        for (size_t nd : queue) source[nd] = nd;
        while (!queue.empty())
        {
            const size_t nd = queue.front();
            queue.pop_front();
            const int i = int(nd % R.nx), j = int((nd / R.nx) % R.ny), k = int(nd / (size_t(R.nx) * R.ny));
            const int nb[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
            for (auto& o : nb)
            {
                const int a = i + o[0], b = j + o[1], c = k + o[2];
                if (a < 0 || b < 0 || c < 0 || a >= R.nx || b >= R.ny || c >= R.nz) continue;
                const size_t m = R.index(a, b, c);
                if (source[m] != size_t(-1)) continue;
                source[m] = source[nd];
                queue.push_back(m);
            }
        }
        for (size_t nd = 0; nd < nNode; ++nd)
        {
            if (known[nd] || source[nd] == size_t(-1)) continue;
            for (int f = 0; f < Result::FIELD_COUNT; ++f) R.fields[f][nd] = R.fields[f][source[nd]];
        }
    }

    R.elements = int(m_active.size());
    R.nodes = m_dofs / 3;
    R.dofs = m_dofs;
    R.fixedNodes = m_fixedNodes;
    R.loadedNodes = m_loadedNodes;
    R.iterations = it;
    R.residual = rel;
    R.compliance = compliance;
    R.reaction = reaction;
    double vol = 0;
    for (int e : m_active) vol += m_fraction[size_t(e)];
    R.volume = vol * std::pow(m_h, 3);
    R.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return true;
}

////////////////////////////////////////////////////////////////////////////////
// Each element's own values

bool StaticProblem::elementValues(int field, std::vector<float>& out) const
{
    out.clear();
    if (!m_result || m_result->nx == 0 || field < 0 || field >= Result::FIELD_COUNT) return false;
    const Result& R = *m_result;
    const bool tet = m_element == Element::Tet;
    const int per = elementsPerCell();
    out.assign(m_active.size() * size_t(per), 0.0f);
    const Mat6 D = elasticity(m_E, m_nu);
    const TetCell cell(m_h);
    // A hexahedron's strain at its centre: the incompatible modes' strains
    // vanish there (they are linear in the natural coordinates), so the
    // plain trilinear strain is the element's
    const Mat6x24 Bcentre = strainMatrix(m_h, 0.0, 0.0, 0.0);
    const bool displacement = field == Result::DISPLACEMENT || field == Result::UX ||
                              field == Result::UY || field == Result::UZ;
    parallelFor(m_active.size(), [&](size_t b0, size_t b1) {
        for (size_t a = b0; a < b1; ++a)
        {
            const int e = m_active[a];
            const int i = e % m_ex, j = (e / m_ex) % m_ey, k = e / (m_ex * m_ey);
            Vec24 ue;
            Eigen::Vector3d un[8];
            for (int c = 0; c < 8; ++c)
            {
                const size_t nd = R.index(i + CORNER[c][0], j + CORNER[c][1], k + CORNER[c][2]);
                un[c] = Eigen::Vector3d(R.fields[Result::UX][nd], R.fields[Result::UY][nd],
                                        R.fields[Result::UZ][nd]);
                for (int q = 0; q < 3; ++q) ue[3 * c + q] = un[c][q];
            }
            for (int s = 0; s < per; ++s)
            {
                float v = 0;
                if (displacement)
                {
                    // at the element's centre: the mean of its nodes' displacements
                    Eigen::Vector3d u = Eigen::Vector3d::Zero();
                    if (tet)
                    {
                        for (int q = 0; q < 4; ++q) u += un[cell.corner[s][q]];
                        u /= 4.0;
                    }
                    else
                    {
                        for (int c = 0; c < 8; ++c) u += un[c];
                        u /= 8.0;
                    }
                    v = float(field == Result::UX ? u.x() : field == Result::UY ? u.y()
                              : field == Result::UZ ? u.z() : u.norm());
                }
                else
                {
                    Eigen::Matrix<double, 6, 1> eps = (tet ? cell.B[s] : Bcentre) * ue;
                    if (!m_thermalStrain.empty())
                        for (int q = 0; q < 3; ++q) eps[q] -= m_thermalStrain[a];   // (the stress-free part)
                    const Eigen::Matrix<double, 6, 1> sig = D * eps;
                    const double sxx = sig[0], syy = sig[1], szz = sig[2], sxy = sig[3], syz = sig[4],
                                 szx = sig[5];
                    switch (field)
                    {
                        case Result::VON_MISES:
                            v = float(std::sqrt(0.5 * ((sxx - syy) * (sxx - syy) + (syy - szz) * (syy - szz) +
                                                       (szz - sxx) * (szz - sxx)) +
                                                3 * (sxy * sxy + syz * syz + szx * szx)));
                            break;
                        case Result::SXX: v = float(sxx); break;
                        case Result::SYY: v = float(syy); break;
                        case Result::SZZ: v = float(szz); break;
                        case Result::SXY: v = float(sxy); break;
                        case Result::SYZ: v = float(syz); break;
                        case Result::SZX: v = float(szx); break;
                        case Result::MAX_PRINCIPAL:
                        case Result::MIN_PRINCIPAL:
                        {
                            Eigen::Matrix3d S;
                            S << sxx, sxy, szx, sxy, syy, syz, szx, syz, szz;
                            const Eigen::Vector3d pr =
                                Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d>(S, Eigen::EigenvaluesOnly).eigenvalues();
                            v = float(field == Result::MAX_PRINCIPAL ? pr[2] : pr[0]);
                            break;
                        }
                        case Result::STRAIN_ENERGY: v = float(0.5 * sig.dot(eps)); break;
                        default: break;
                    }
                }
                out[a * size_t(per) + size_t(s)] = v;
            }
        }
    }, 256);
    return true;
}

////////////////////////////////////////////////////////////////////////////////
// Modal analysis: subspace iteration, K Y = M X solved with the static
// solver's operator (the same element stiffness, colouring and multigrid as
// solve()), Rayleigh-Ritz on the subspace

bool StaticProblem::modal(int count, double density, int maxIterations, double tolerance,
                          std::string& error)
{
    if (count < 1)
    {
        error = "ask for at least one mode";
        return false;
    }
    if (!(density > 0))
    {
        error = "modal analysis needs the material's density";
        return false;
    }
    if (m_supports.empty())
    {
        error = "modal analysis needs supports (fixed(...)): a free part's first six modes are "
                "just it moving and turning as a whole";
        return false;
    }
    m_noLoads = true;
    const bool ok = m_prepared || prepare(error);
    m_noLoads = false;
    if (!ok) return false;
    const auto t0 = std::chrono::steady_clock::now();
    run_progress::Task task("modal analysis");
    task.set(0.0, "setting up the solver");

    const Mat6 D1 = elasticity(1.0, m_nu);
    const Mat24 K1 = elementStiffness(m_element, m_h, D1);
    const size_t n = size_t(m_dofs);

    // (as in solve())
    struct Elem { int dof[8]; double scale; };
    std::vector<Elem> elems(m_active.size());
    std::vector<std::vector<int>> colour(8);
    std::vector<double> mass(n, 0.0);           // lumped
    const double h3 = m_h * m_h * m_h;
    for (size_t a = 0; a < m_active.size(); ++a)
    {
        const int e = m_active[a];
        const int i = e % m_ex, j = (e / m_ex) % m_ey, k = e / (m_ex * m_ey);
        const double fill = std::max(0.01, double(m_fraction[size_t(e)]));
        for (int c = 0; c < 8; ++c)
        {
            elems[a].dof[c] = m_dofOfNode[node(i + CORNER[c][0], j + CORNER[c][1], k + CORNER[c][2])];
            for (int q = 0; q < 3; ++q) mass[size_t(elems[a].dof[c] + q)] += density * h3 * fill / 8.0;
        }
        elems[a].scale = m_E * fill;
        colour[size_t((i & 1) | ((j & 1) << 1) | ((k & 1) << 2))].push_back(int(a));
    }
    for (size_t d = 0; d < n; ++d) if (m_fixed[d]) mass[d] = 0.0;
    size_t nFree = 0;
    for (size_t d = 0; d < n; ++d) nFree += !m_fixed[d];
    if (nFree < size_t(count))
    {
        error = "the part has fewer free directions than modes asked for";
        return false;
    }
    auto matvec = [&](const std::vector<double>& x, std::vector<double>& y) {
        std::fill(y.begin(), y.end(), 0.0);
        for (const auto& cls : colour)
        {
            parallelFor(cls.size(), [&](size_t b0, size_t b1) {
                Vec24 ue, ye;
                for (size_t q = b0; q < b1; ++q)
                {
                    const Elem& el = elems[size_t(cls[q])];
                    for (int c = 0; c < 8; ++c)
                        for (int a = 0; a < 3; ++a) ue[3 * c + a] = x[size_t(el.dof[c] + a)];
                    ye.noalias() = K1 * ue;
                    ye *= el.scale;
                    for (int c = 0; c < 8; ++c)
                        for (int a = 0; a < 3; ++a) y[size_t(el.dof[c] + a)] += ye[3 * c + a];
                }
            }, 512);
        }
    };
    auto dot = [&](const std::vector<double>& a, const std::vector<double>& b) {
        return parallelSum(n, [&](size_t b0, size_t b1) {
            double s = 0;
            for (size_t i = b0; i < b1; ++i) s += a[i] * b[i];
            return s;
        });
    };
    std::vector<double> minv(n, 0.0);
    {
        std::vector<double> diag(n, 0.0);
        for (const auto& el : elems)
            for (int c = 0; c < 8; ++c)
                for (int a = 0; a < 3; ++a) diag[size_t(el.dof[c] + a)] += el.scale * K1(3 * c + a, 3 * c + a);
        for (size_t i = 0; i < n; ++i) minv[i] = (m_fixed[i] || !(diag[i] > 0)) ? 0.0 : 1.0 / diag[i];
    }
    std::unique_ptr<Multigrid> mg;
    if (m_active.size() > 1000)
    {
        std::vector<Multigrid::FineElem> fine(m_active.size());
        for (size_t a = 0; a < m_active.size(); ++a)
        {
            const int e = m_active[a];
            fine[a].i = e % m_ex;
            fine[a].j = (e / m_ex) % m_ey;
            fine[a].k = e / (m_ex * m_ey);
            fine[a].scale = elems[a].scale;
            for (int c = 0; c < 8; ++c) fine[a].dof[c] = elems[a].dof[c];
        }
        mg.reset(new Multigrid(m_ex, m_ey, m_ez, m_h, m_nu, fine, m_dofOfNode, m_fixed, n, m_element));
    }
    auto precondition = [&](const std::vector<double>& rr, std::vector<double>& zz) {
        if (mg)
        {
            mg->apply(rr, zz);
            for (size_t i = 0; i < n; ++i) if (m_fixed[i]) zz[i] = 0;
        }
        else
        {
            for (size_t i = 0; i < n; ++i) zz[i] = minv[i] * rr[i];
        }
    };
    // K x = b for the free DOFs, from the start x (flexible PCG, as solve())
    std::vector<double> r(n), z(n), p(n), Ap(n), zOld;
    auto pcg = [&](const std::vector<double>& b, std::vector<double>& x) {
        const double bnorm = std::sqrt(dot(b, b));
        if (!(bnorm > 0))
        {
            std::fill(x.begin(), x.end(), 0.0);
            return true;
        }
        matvec(x, Ap);
        for (size_t i = 0; i < n; ++i) r[i] = m_fixed[i] ? 0.0 : b[i] - Ap[i];
        precondition(r, z);
        p = z;
        double rz = dot(r, z);
        double rel = std::sqrt(dot(r, r)) / bnorm;
        for (int it = 0; it < 20000 && rel > tolerance; ++it)
        {
            matvec(p, Ap);
            for (size_t i = 0; i < n; ++i) if (m_fixed[i]) Ap[i] = 0;
            const double pAp = dot(p, Ap);
            if (!(pAp > 0)) return false;
            const double alpha = rz / pAp;
            parallelFor(n, [&](size_t b0, size_t b1) {
                for (size_t i = b0; i < b1; ++i)
                {
                    x[i] += alpha * p[i];
                    r[i] -= alpha * Ap[i];
                }
            }, 8192);
            rel = std::sqrt(dot(r, r)) / bnorm;
            if (!(rel > tolerance)) break;
            zOld = z;
            precondition(r, z);
            const double rzNew = dot(r, z);
            double beta = rzNew / rz;
            if (mg)
            {
                double rzo = 0;
                for (size_t i = 0; i < n; ++i) rzo += r[i] * zOld[i];
                beta = std::max(0.0, (rzNew - rzo) / rz);
            }
            rz = rzNew;
            parallelFor(n, [&](size_t b0, size_t b1) {
                for (size_t i = b0; i < b1; ++i) p[i] = z[i] + beta * p[i];
            }, 8192);
        }
        return rel <= std::max(tolerance * 100, 1e-4);
    };

    // The subspace: more vectors than modes (converges faster, and the
    // highest wanted mode is well separated from the rest)
    const int ns = int(std::min<size_t>(nFree, size_t(std::max(2 * count, count + 8))));
    std::vector<std::vector<double>> X(size_t(ns), std::vector<double>(n, 0.0)), Y = X, MX = X;
    {
        uint64_t state = 88172645463325252ull;      // (a fixed seed: repeatable)
        for (auto& x : X)
            for (size_t d = 0; d < n; ++d)
            {
                state ^= state << 13; state ^= state >> 7; state ^= state << 17;
                x[d] = m_fixed[d] ? 0.0 : double(state % 2000001) / 1000000.0 - 1.0;
            }
    }
    std::vector<double> lambda(size_t(ns), 0.0), previous(size_t(count), 0.0);
    int it = 0;
    bool converged = false;
    for (; it < maxIterations && !converged; ++it)
    {
        {
            char buf[64];
            snprintf(buf, sizeof(buf), "modal analysis, iteration %d", it + 1);
            task.set(std::min(0.95, double(it) / std::max(1, std::min(maxIterations, 40))), buf);
        }
        // Y = K^-1 M X (warm-started at X / lambda: an eigenvector's own image)
        for (int j = 0; j < ns; ++j)
        {
            auto& mx = MX[size_t(j)];
            for (size_t d = 0; d < n; ++d) mx[d] = mass[d] * X[size_t(j)][d];
            auto& y = Y[size_t(j)];
            const double l = lambda[size_t(j)];
            for (size_t d = 0; d < n; ++d) y[d] = l > 0 ? X[size_t(j)][d] / l : 0.0;
            if (!pcg(mx, y))
            {
                error = "the solver failed: the supports may not hold the part in place";
                return false;
            }
        }
        // Rayleigh-Ritz: (Y^T K Y) q = lambda (Y^T M Y) q, with K Y = M X
        Eigen::MatrixXd Kr(ns, ns), Mr(ns, ns);
        for (int a = 0; a < ns; ++a)
            for (int b = a; b < ns; ++b)
            {
                double kab = 0, mab = 0;
                const auto& ya = Y[size_t(a)];
                const auto& yb = Y[size_t(b)];
                const auto& mxb = MX[size_t(b)];
                for (size_t d = 0; d < n; ++d)
                {
                    kab += ya[d] * mxb[d];
                    mab += ya[d] * mass[d] * yb[d];
                }
                Kr(a, b) = Kr(b, a) = kab;
                Mr(a, b) = Mr(b, a) = mab;
            }
        Kr = 0.5 * (Kr + Kr.transpose());
        Eigen::GeneralizedSelfAdjointEigenSolver<Eigen::MatrixXd> ges(Kr, Mr);
        if (ges.info() != Eigen::Success)
        {
            error = "modal analysis: the subspace eigenproblem failed";
            return false;
        }
        const Eigen::VectorXd ev = ges.eigenvalues();
        const Eigen::MatrixXd Q = ges.eigenvectors();
        for (int j = 0; j < ns; ++j)
        {
            auto& x = X[size_t(j)];
            std::fill(x.begin(), x.end(), 0.0);
            for (int a = 0; a < ns; ++a)
            {
                const double q = Q(a, j);
                const auto& ya = Y[size_t(a)];
                for (size_t d = 0; d < n; ++d) x[d] += q * ya[d];
            }
            lambda[size_t(j)] = ev[j];
        }
        converged = it > 0;
        for (int j = 0; j < count; ++j)
        {
            if (!(lambda[size_t(j)] > 0)) { converged = false; continue; }
            if (std::abs(lambda[size_t(j)] - previous[size_t(j)]) > tolerance * 10 * lambda[size_t(j)])
                converged = false;
            previous[size_t(j)] = lambda[size_t(j)];
        }
    }
    if (!converged)
    {
        error = "modal analysis did not converge in " + std::to_string(it) + " iterations";
        return false;
    }

    // Frequencies, and the shapes as fields (largest movement 1)
    m_frequencies.clear();
    m_modes.clear();
    const int nx = m_ex + 1, ny = m_ey + 1, nz = m_ez + 1;
    const size_t nNode = size_t(nx) * ny * nz;
    for (int j = 0; j < count; ++j)
    {
        m_frequencies.push_back(std::sqrt(std::max(0.0, lambda[size_t(j)])) / 6.283185307179586);
        const auto& x = X[size_t(j)];
        double umax = 0;
        for (size_t d = 0; d + 2 < n; d += 3)
            umax = std::max(umax, std::sqrt(x[d] * x[d] + x[d + 1] * x[d + 1] + x[d + 2] * x[d + 2]));
        const double s = umax > 0 ? 1.0 / umax : 1.0;
        auto R = std::make_shared<Result>();
        R->lo = m_lo;
        R->h = m_h;
        R->tetrahedra = m_element == Element::Tet;
        R->nx = nx;
        R->ny = ny;
        R->nz = nz;
        for (auto& f : R->fields) f.assign(nNode, 0.0f);
        std::vector<unsigned char> known(nNode, 0);
        for (int k = 0; k < nz; ++k)
            for (int jj = 0; jj < ny; ++jj)
                for (int i = 0; i < nx; ++i)
                {
                    const int d = m_dofOfNode[node(i, jj, k)];
                    if (d < 0) continue;
                    const size_t nd = R->index(i, jj, k);
                    known[nd] = 1;
                    const double ux = s * x[size_t(d)], uy = s * x[size_t(d + 1)], uz = s * x[size_t(d + 2)];
                    R->fields[Result::UX][nd] = float(ux);
                    R->fields[Result::UY][nd] = float(uy);
                    R->fields[Result::UZ][nd] = float(uz);
                    R->fields[Result::DISPLACEMENT][nd] = float(std::sqrt(ux * ux + uy * uy + uz * uz));
                }
        for (int f = 0; f < Result::FIELD_COUNT; ++f)
        {
            float mn = 1e30f, mx = -1e30f;
            for (size_t nd = 0; nd < nNode; ++nd)
            {
                if (!known[nd]) continue;
                mn = std::min(mn, R->fields[f][nd]);
                mx = std::max(mx, R->fields[f][nd]);
            }
            R->minValue[f] = mn;
            R->maxValue[f] = mx;
        }
        // (nodes off the part take their nearest part node's values)
        {
            std::deque<size_t> queue;
            std::vector<size_t> source(nNode, size_t(-1));
            for (size_t nd = 0; nd < nNode; ++nd) if (known[nd]) { queue.push_back(nd); source[nd] = nd; }
            while (!queue.empty())
            {
                const size_t nd = queue.front();
                queue.pop_front();
                const int i = int(nd % nx), jj = int((nd / nx) % ny), k = int(nd / (size_t(nx) * ny));
                const int nb[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
                for (auto& o : nb)
                {
                    const int a = i + o[0], b = jj + o[1], c = k + o[2];
                    if (a < 0 || b < 0 || c < 0 || a >= nx || b >= ny || c >= nz) continue;
                    const size_t m = R->index(a, b, c);
                    if (source[m] != size_t(-1)) continue;
                    source[m] = source[nd];
                    queue.push_back(m);
                }
            }
            for (size_t nd = 0; nd < nNode; ++nd)
            {
                if (known[nd] || source[nd] == size_t(-1)) continue;
                for (int f = 1; f <= 4; ++f) R->fields[f][nd] = R->fields[f][source[nd]];
            }
        }
        R->elements = int(m_active.size());
        R->iterations = it;
        R->seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        m_modes.push_back(R);
    }
    return true;
}

////////////////////////////////////////////////////////////////////////////////
// Topology optimization (SIMP, density filter, optimality criteria)

bool StaticProblem::optimize(const TopOpt& s, std::string& error, const std::atomic<bool>* cancel)
{
    if (!m_prepared && !prepare(error)) return false;
    if (!m_thermalStrain.empty())
    {
        // (its load depends on the design: the sensitivities would be wrong)
        error = "thermal expansion isn't supported in topology optimization";
        return false;
    }
    if (!(s.volumeFraction > 0 && s.volumeFraction < 1))
    {
        error = "the volume fraction must be between 0 and 1";
        return false;
    }
    const auto t0 = std::chrono::steady_clock::now();
    const size_t na = m_active.size();
    const double h = m_h;

    // Element centres, dofs, and which elements are fixed solid / empty
    std::vector<Eigen::Vector3f> centres(na);
    std::vector<std::array<int, 8>> dofs(na);
    std::vector<int> activeOf(size_t(m_ex) * m_ey * m_ez, -1);
    for (size_t a = 0; a < na; ++a)
    {
        const int e = m_active[a];
        activeOf[size_t(e)] = int(a);
        const int i = e % m_ex, j = (e / m_ex) % m_ey, k = e / (m_ex * m_ey);
        centres[a] = (m_lo + Eigen::Vector3d(i + 0.5, j + 0.5, k + 0.5) * h).cast<float>();
        for (int c = 0; c < 8; ++c)
            dofs[a][size_t(c)] = m_dofOfNode[node(i + CORNER[c][0], j + CORNER[c][1], k + CORNER[c][2])];
    }
    std::vector<char> state(na, 0);             // 0 design, 1 solid, 2 empty
    std::vector<float> vals;
    for (const auto& t : s.avoid)
    {
        evalPoints(t, centres, vals);
        for (size_t a = 0; a < na; ++a) if (vals[a] < 0) state[a] = 2;
    }
    for (const auto& t : s.keep)
    {
        evalPoints(t, centres, vals);
        for (size_t a = 0; a < na; ++a) if (vals[a] < 0) state[a] = 1;
    }
    // Elements that hold the supports and loads stay solid
    for (size_t a = 0; a < na; ++a)
        for (int c = 0; c < 8 && state[a] != 1; ++c)
        {
            const size_t d = size_t(dofs[a][size_t(c)]);
            for (int q = 0; q < 3; ++q)
                if (m_fixed[d + size_t(q)] || m_force[d + size_t(q)] != 0) state[a] = 1;
        }

    // Extrusion: the design elements on each line along the axis share one
    // value (their sensitivities summed), so the design is a profile
    // extruded through
    std::vector<int> column(na, -1);
    int nCol = 0;
    if (s.extrude >= 0 && s.extrude <= 2)
    {
        const int dims[3] = {m_ex, m_ey, m_ez};
        const int u0 = s.extrude == 0 ? 1 : 0, u1 = s.extrude == 2 ? 1 : 2;
        std::vector<int> id(size_t(dims[u0]) * size_t(dims[u1]), -1);
        for (size_t a = 0; a < na; ++a)
        {
            if (state[a]) continue;
            const int e = m_active[a];
            const int ijk[3] = {e % m_ex, (e / m_ex) % m_ey, e / (m_ex * m_ey)};
            const size_t key = size_t(ijk[u0]) + size_t(dims[u0]) * size_t(ijk[u1]);
            if (id[key] < 0) id[key] = nCol++;
            column[a] = id[key];
        }
    }

    // Filter: weights rmin - distance to the neighbours within rmin
    const double rmin = s.filterRadius > 0 ? s.filterRadius : 1.5 * h;
    const int R = std::max(1, int(std::floor(rmin / h - 1e-9)));
    std::vector<size_t> fStart(na + 1, 0);
    std::vector<int> fIdx;
    std::vector<float> fW;
    std::vector<double> Hs(na, 0.0);
    for (size_t a = 0; a < na; ++a)
    {
        const int e = m_active[a];
        const int i = e % m_ex, j = (e / m_ex) % m_ey, k = e / (m_ex * m_ey);
        for (int dk = -R; dk <= R; ++dk)
            for (int dj = -R; dj <= R; ++dj)
                for (int di = -R; di <= R; ++di)
                {
                    const int a2 = i + di, b2 = j + dj, c2 = k + dk;
                    if (a2 < 0 || b2 < 0 || c2 < 0 || a2 >= m_ex || b2 >= m_ey || c2 >= m_ez) continue;
                    const int o = activeOf[size_t(elem(a2, b2, c2))];
                    if (o < 0) continue;
                    const double w = rmin - h * std::sqrt(double(di * di + dj * dj + dk * dk));
                    if (w <= 0) continue;
                    fIdx.push_back(o);
                    fW.push_back(float(w));
                    Hs[a] += w;
                }
        fStart[a + 1] = fIdx.size();
    }
    auto filter = [&](const std::vector<double>& in, std::vector<double>& out) {
        out.resize(na);
        parallelFor(na, [&](size_t b0, size_t b1) {
            for (size_t a = b0; a < b1; ++a)
            {
                double sum = 0;
                for (size_t q = fStart[a]; q < fStart[a + 1]; ++q) sum += fW[q] * in[size_t(fIdx[q])];
                out[a] = sum / Hs[a];
            }
        }, 1024);
    };
    // the transpose, for the sensitivities: out_b = sum_a w_ab in_a / Hs_a
    auto filterT = [&](const std::vector<double>& in, std::vector<double>& out) {
        std::vector<double> scaled(na);
        for (size_t a = 0; a < na; ++a) scaled[a] = in[a] / Hs[a];
        filter(scaled, out);
        for (size_t a = 0; a < na; ++a) out[a] *= Hs[a];
    };

    std::vector<double> frac(na);
    double total = 0;
    for (size_t a = 0; a < na; ++a)
    {
        frac[a] = std::max(0.01, double(m_fraction[size_t(m_active[a])]));
        total += frac[a];
    }
    const double target = s.volumeFraction * total;

    const Mat6 D1 = elasticity(1.0, m_nu);
    const Mat24 K1 = elementStiffness(m_element, h, D1);
    const double p = s.penalty, emin = s.minStiffness;

    std::vector<double> x(na, s.volumeFraction), xPhys, dc(na), dv(na), dcF, dvF, xNew(na);
    for (size_t a = 0; a < na; ++a) if (state[a]) x[a] = state[a] == 1 ? 1.0 : 0.0;
    // (A Heaviside projection of the filtered density -- beta doubling to
    // 16 -- was tried 2026-09-30: compliance 40 % lower in the optimizer's
    // own model, but the part cut from the density came apart at the grid's
    // resolution (one-element diagonal members): it needs a minimum length
    // scale first.  Not used.)
    auto physical = [&](const std::vector<double>& xs, std::vector<double>& out) {
        filter(xs, out);
        for (size_t a = 0; a < na; ++a)
        {
            if (state[a] == 1) out[a] = 1.0;
            else if (state[a] == 2) out[a] = 0.0;
        }
    };
    m_history.clear();
    m_lastU.clear();
    m_quick = true;
    // Load cases: each solved on its own (its own warm start), the
    // sensitivities summed -- the part stiff for all of them
    const int nc = std::max<int>(1, int(m_caseForce.size()));
    std::vector<std::vector<double>> caseU(static_cast<size_t>(nc));
    const std::vector<double> allForce = m_force;
    struct Restore
    {
        StaticProblem* sp;
        const std::vector<double>* all;
        ~Restore() { sp->m_quick = false; sp->m_elemScale.clear(); sp->m_lastU.clear(); sp->m_force = *all; }
    } restore{this, &allForce};

    // Progress: iterations done of the iterations it will take -- all of
    // s.iterations, or fewer when it converges: from how fast the design
    // change and the compliance change have been falling (about
    // geometrically) toward their thresholds.  Each iteration's solve
    // counts within its iteration.
    run_progress::Task task("optimising");
    std::vector<double> changes;
    int expected = std::max(1, s.iterations);
    auto toReach = [](const std::vector<double>& v, double target) {
        // iterations until v reaches target, from its fall over the last
        // (up to) 5 values; -1 when it isn't falling
        const size_t H = v.size();
        if (H < 3 || !(v.back() > 0)) return -1.0;
        if (v.back() <= target) return 0.0;
        const size_t m = std::min<size_t>(5, H - 1);
        if (!(v[H - 1 - m] > 0)) return -1.0;
        const double rate = std::pow(v.back() / v[H - 1 - m], 1.0 / double(m));
        if (!(rate < 0.999)) return -1.0;
        return std::log(target / v.back()) / std::log(rate);
    };

    int it = 0;
    for (; it < s.iterations; ++it)
    {
        if (cancel && cancel->load())
        {
            error = "cancelled";
            return false;
        }
        task.span(double(it) / expected, double(it + 1) / expected);
        {
            char buf[64];
            snprintf(buf, sizeof(buf), "optimising, iteration %d of ~%d", it + 1, expected);
            task.set(double(it) / expected, buf);
        }
        physical(x, xPhys);
        m_elemScale.resize(na);
        for (size_t a = 0; a < na; ++a) m_elemScale[a] = emin + (1 - emin) * std::pow(xPhys[a], p);
        // (the sensitivities don't need a tight solve; warm starts keep
        // each one short)
        std::fill(dc.begin(), dc.end(), 0.0);
        double compliance = 0.0;
        for (int lc = 0; lc < nc; ++lc)
        {
            if (nc > 1)
            {
                m_force = m_caseForce[size_t(lc)];
                m_lastU = caseU[size_t(lc)];
                task.span((double(it) + double(lc) / nc) / expected, (double(it) + double(lc + 1) / nc) / expected);
            }
            if (!solve(s.solverIterations, std::max(s.tolerance, 1e-4), error, cancel)) return false;
            compliance += m_result->compliance;
            if (nc > 1) caseU[size_t(lc)] = m_lastU;

            // compliance sensitivities from each element's strain energy
            const auto& u = m_lastU;
            parallelFor(na, [&](size_t b0, size_t b1) {
                Vec24 ue;
                for (size_t a = b0; a < b1; ++a)
                {
                    for (int c = 0; c < 8; ++c)
                        for (int q = 0; q < 3; ++q) ue[3 * c + q] = u[size_t(dofs[a][size_t(c)] + q)];
                    const double ce = ue.dot(K1 * ue);
                    dc[a] += -p * std::pow(std::max(xPhys[a], 1e-9), p - 1) * (1 - emin) * m_E * frac[a] * ce;
                    dv[a] = frac[a];
                }
            }, 512);
        }
        m_history.push_back(compliance);
        filterT(dc, dcF);
        filterT(dv, dvF);
        if (nCol)
        {
            // (one value per line: the line's sensitivities summed; x stays
            // equal along each line, starting equal)
            std::vector<double> cs(size_t(nCol), 0.0), vs(size_t(nCol), 0.0);
            for (size_t a = 0; a < na; ++a)
                if (column[a] >= 0)
                {
                    cs[size_t(column[a])] += dcF[a];
                    vs[size_t(column[a])] += dvF[a];
                }
            for (size_t a = 0; a < na; ++a)
                if (column[a] >= 0)
                {
                    dcF[a] = cs[size_t(column[a])];
                    dvF[a] = vs[size_t(column[a])];
                }
        }

        // optimality criteria: bisection on the volume's Lagrange multiplier
        double l1 = 0, l2 = 1e12;
        std::vector<double> xp;
        for (int bis = 0; bis < 200 && (l2 - l1) / (l1 + l2 + 1e-300) > 1e-6; ++bis)
        {
            const double lm = 0.5 * (l1 + l2);
            for (size_t a = 0; a < na; ++a)
            {
                if (state[a])
                {
                    xNew[a] = x[a];
                    continue;
                }
                const double B = std::sqrt(std::max(0.0, -dcF[a]) / (lm * std::max(dvF[a], 1e-30)));
                xNew[a] = std::max(0.0, std::max(x[a] - s.move,
                          std::min(1.0, std::min(x[a] + s.move, x[a] * B))));
            }
            physical(xNew, xp);
            double vol = 0;
            for (size_t a = 0; a < na; ++a) vol += frac[a] * xp[a];
            if (vol > target) l1 = lm; else l2 = lm;
        }
        double change = 0;
        for (size_t a = 0; a < na; ++a) change = std::max(change, std::abs(xNew[a] - x[a]));
        x.swap(xNew);
        {   // the iterations it will take, now
            changes.push_back(change);
            std::vector<double> flatness;
            for (size_t h = 6; h <= m_history.size(); ++h)
                flatness.push_back(std::abs(m_history[h - 1] - m_history[h - 6]) /
                                   std::max(1e-300, std::abs(m_history[h - 1])));
            double more = -1;
            for (double m : {toReach(changes, 0.01), toReach(flatness, 2e-3)})
                if (m >= 0) more = more < 0 ? m : std::min(more, m);
            if (more >= 0)
                expected = std::min(s.iterations, std::max(16, it + 1 + int(std::ceil(more))));
            task.set(double(it + 1) / expected);
        }
        // converged: the design stopped moving, or the compliance stopped
        // improving over the last 5 iterations
        const size_t H = m_history.size();
        const bool flat = H > 6 &&
            std::abs(m_history[H - 1] - m_history[H - 6]) < 2e-3 * std::abs(m_history[H - 1]);
        if (it >= 15 && (change < 0.01 || flat)) { ++it; break; }
    }
    physical(x, xPhys);

    // The density per element, and as a field on the element centres
    m_topDensity.assign(size_t(m_ex) * m_ey * m_ez, 0.0f);
    for (size_t a = 0; a < na; ++a) m_topDensity[size_t(m_active[a])] = float(xPhys[a]);
    auto R2 = std::make_shared<Result>();
    R2->lo = m_lo + Eigen::Vector3d::Constant(0.5 * h);
    R2->h = h;
    R2->nx = m_ex;
    R2->ny = m_ey;
    R2->nz = m_ez;
    R2->fields[0] = m_topDensity;
    R2->minValue[0] = 0;
    R2->maxValue[0] = 1;
    R2->iterations = it;
    R2->compliance = m_history.empty() ? 0 : m_history.back();
    double vol = 0;
    for (size_t a = 0; a < na; ++a) vol += frac[a] * xPhys[a];
    R2->volume = vol * h * h * h;
    R2->elements = int(na);
    R2->seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    m_densityResult = R2;
    return true;
}

////////////////////////////////////////////////////////////////////////////////
// Result fields as Trees

namespace {

class FieldOracle : public OracleStorage<>
{
public:
    FieldOracle(std::shared_ptr<const Result> r, int f) : res(std::move(r)), field(f) {}

    float sample(const Eigen::Vector3f& p, Eigen::Vector3f* grad=nullptr) const
    {
        const Result& R = *res;
        const auto& v = R.fields[field];
        float g[3], t[3];
        int i0[3];
        const int n[3] = {R.nx, R.ny, R.nz};
        for (int a = 0; a < 3; ++a)
        {
            g[a] = float((p[a] - R.lo[a]) / R.h);
            g[a] = std::max(0.0f, std::min(float(n[a] - 1), g[a]));
            i0[a] = std::min(n[a] - 2, int(std::floor(g[a])));
            if (i0[a] < 0) i0[a] = 0;
            t[a] = n[a] > 1 ? g[a] - i0[a] : 0.0f;
        }
        auto at = [&](int di, int dj, int dk) {
            const int i = std::min(n[0] - 1, i0[0] + di), j = std::min(n[1] - 1, i0[1] + dj),
                      k = std::min(n[2] - 1, i0[2] + dk);
            return v[R.index(i, j, k)];
        };
        if (R.tetrahedra)
        {
            // The cell's tetrahedron with 0 <= t[c] <= t[b] <= t[a] <= 1 (the Kuhn
            // subdivision the solver used): its corners are 000, e_a, e_a + e_b,
            // 111, and the value is linear between them
            int o[3] = {0, 1, 2};
            if (t[o[0]] < t[o[1]]) std::swap(o[0], o[1]);
            if (t[o[1]] < t[o[2]]) std::swap(o[1], o[2]);
            if (t[o[0]] < t[o[1]]) std::swap(o[0], o[1]);
            const int a = o[0], b = o[1], c = o[2];
            int q[3] = {0, 0, 0};
            const float v0 = at(0, 0, 0);
            q[a] = 1;
            const float v1 = at(q[0], q[1], q[2]);
            q[b] = 1;
            const float v2 = at(q[0], q[1], q[2]);
            const float v3 = at(1, 1, 1);
            if (grad)
            {
                (*grad)[a] = (v1 - v0) / float(R.h);
                (*grad)[b] = (v2 - v1) / float(R.h);
                (*grad)[c] = (v3 - v2) / float(R.h);
            }
            return (1 - t[a]) * v0 + (t[a] - t[b]) * v1 + (t[b] - t[c]) * v2 + t[c] * v3;
        }
        const float c000 = at(0, 0, 0), c100 = at(1, 0, 0), c010 = at(0, 1, 0), c110 = at(1, 1, 0);
        const float c001 = at(0, 0, 1), c101 = at(1, 0, 1), c011 = at(0, 1, 1), c111 = at(1, 1, 1);
        const float c00 = c000 + (c100 - c000) * t[0], c10 = c010 + (c110 - c010) * t[0];
        const float c01 = c001 + (c101 - c001) * t[0], c11 = c011 + (c111 - c011) * t[0];
        const float c0 = c00 + (c10 - c00) * t[1], c1 = c01 + (c11 - c01) * t[1];
        if (grad)
        {
            const float dx0 = ((c100 - c000) * (1 - t[1]) + (c110 - c010) * t[1]);
            const float dx1 = ((c101 - c001) * (1 - t[1]) + (c111 - c011) * t[1]);
            const float dy0 = (c10 - c00), dy1 = (c11 - c01);
            (*grad)[0] = (dx0 * (1 - t[2]) + dx1 * t[2]) / float(R.h);
            (*grad)[1] = (dy0 * (1 - t[2]) + dy1 * t[2]) / float(R.h);
            (*grad)[2] = (c1 - c0) / float(R.h);
        }
        return c0 + (c1 - c0) * t[2];
    }

    void evalInterval(Interval& out) override
    {
        const Result& R = *res;
        const auto& v = R.fields[field];
        int a0[3], a1[3];
        const int n[3] = {R.nx, R.ny, R.nz};
        size_t count = 1;
        for (int a = 0; a < 3; ++a)
        {
            a0[a] = std::max(0, std::min(n[a] - 1, int(std::floor((lower[a] - R.lo[a]) / R.h))));
            a1[a] = std::max(0, std::min(n[a] - 1, int(std::ceil((upper[a] - R.lo[a]) / R.h))));
            count *= size_t(a1[a] - a0[a] + 1);
        }
        float mn, mx;
        if (count > 20000)
        {
            mn = *std::min_element(v.begin(), v.end());
            mx = *std::max_element(v.begin(), v.end());
        }
        else
        {
            mn = 1e30f;
            mx = -1e30f;
            for (int k = a0[2]; k <= a1[2]; ++k)
                for (int j = a0[1]; j <= a1[1]; ++j)
                    for (int i = a0[0]; i <= a1[0]; ++i)
                    {
                        const float x = v[R.index(i, j, k)];
                        mn = std::min(mn, x);
                        mx = std::max(mx, x);
                    }
        }
        out = Interval(mn, mx);
    }

    void evalPoint(float& out, size_t index) override
    {
        out = sample(points.col(index).matrix());
    }

    void checkAmbiguous(
            Eigen::Block<Eigen::Array<bool, 1, LIBFIVE_EVAL_ARRAY_SIZE>,
                         1, Eigen::Dynamic> /* out */) override
    {
    }

    void evalFeatures(boost::container::small_vector<Feature, 4>& out) override
    {
        Eigen::Vector3f g;
        sample(points.col(0).matrix(), &g);
        out.push_back(Feature(g));
    }

private:
    std::shared_ptr<const Result> res;
    int field;
};

class FieldClause : public OracleClause
{
public:
    FieldClause(std::shared_ptr<const Result> r, int f) : res(std::move(r)), field(f) {}
    std::unique_ptr<Oracle> getOracle() const override
    {
        return std::make_unique<FieldOracle>(res, field);
    }
    std::string name() const override { return "fea_field_" + std::to_string(field); }
    // The same field of the same solved result (a re-run script reusing a
    // cached analysis) is the same node
    std::string contentKey() const override
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "fea#%llu#%d", static_cast<unsigned long long>(res->serial), field);
        return buf;
    }

private:
    std::shared_ptr<const Result> res;
    int field;
};

}   // anonymous namespace

Tree fieldTree(std::shared_ptr<const Result> result, int field)
{
    if (!result || field < 0 || field >= Result::FIELD_COUNT) return Tree::invalid();
    return Tree(std::make_unique<FieldClause>(std::move(result), field));
}

}   // namespace fea
}   // namespace libfive
