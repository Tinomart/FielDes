/*
libfive: a CAD kernel for modeling with implicit functions

Steady-state heat conduction and its topology optimization (see
thermal.hpp).

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <deque>
#include <thread>

#include "libfive/fea/thermal.hpp"
#include "libfive/eval/eval_array.hpp"
#include "libfive/run_progress.hpp"

namespace libfive {
namespace fea {

namespace {

const int CORNER[8][3] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0},
                          {0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}};

// A tree's values at many points, on several threads
void evalAll(const Tree& tree, const std::vector<Eigen::Vector3f>& pts, std::vector<float>& out)
{
    out.resize(pts.size());
    const size_t N = ArrayEvaluator::N;
    const size_t batches = (pts.size() + N - 1) / N;
    const unsigned T = std::max(1u, std::min(8u, std::thread::hardware_concurrency()));
    std::atomic<size_t> next{0};
    auto work = [&]() {
        ArrayEvaluator e(tree);
        for (size_t b; (b = next.fetch_add(1)) < batches;)
        {
            const size_t start = b * N, count = std::min(N, pts.size() - start);
            for (size_t k = 0; k < count; ++k) e.set(pts[start + k], k);
            const auto vs = e.values(count);
            for (size_t k = 0; k < count; ++k) out[start + k] = vs(k);
        }
    };
    std::vector<std::thread> threads;
    for (unsigned t = 1; t < T && size_t(t) < batches; ++t) threads.emplace_back(work);
    work();
    for (auto& t : threads) t.join();
}

// Gradients of the unit cube's 8 trilinear shape functions at (x, y, z)
Eigen::Matrix<double, 3, 8> shapeGradients(double x, double y, double z)
{
    Eigen::Matrix<double, 3, 8> G;
    for (int c = 0; c < 8; ++c)
    {
        const double fx = CORNER[c][0] ? x : 1 - x, dx = CORNER[c][0] ? 1 : -1;
        const double fy = CORNER[c][1] ? y : 1 - y, dy = CORNER[c][1] ? 1 : -1;
        const double fz = CORNER[c][2] ? z : 1 - z, dz = CORNER[c][2] ? 1 : -1;
        G(0, c) = dx * fy * fz;
        G(1, c) = fx * dy * fz;
        G(2, c) = fx * fy * dz;
    }
    return G;
}

// The unit cube element's conductivity matrix: the integral of
// grad Ni . grad Nj (2 x 2 x 2 Gauss points, exact for trilinear elements);
// an element of size h and conductivity k scales it by k h
Eigen::Matrix<double, 8, 8> hexUnitConductivity()
{
    Eigen::Matrix<double, 8, 8> K = Eigen::Matrix<double, 8, 8>::Zero();
    const double g[2] = {0.5 - 0.5 / std::sqrt(3.0), 0.5 + 0.5 / std::sqrt(3.0)};
    for (double x : g)
        for (double y : g)
            for (double z : g)
            {
                const auto G = shapeGradients(x, y, z);
                K += G.transpose() * G * 0.125;
            }
    return K;
}

// The unit cube as six tetrahedra (Kuhn subdivision, as in fea.cpp): for each
// order (a, b, c) of the axes the corners 000, e_a, e_a + e_b and 111.  Each has
// constant shape function gradients; G[t] maps the cube's 8 corner values to
// its temperature gradient, its volume is 1/6.
struct UnitTets
{
    Eigen::Matrix<double, 3, 8> G[6];
    UnitTets()
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
            int corner[4];
            for (int i = 0; i < 4; ++i)
            {
                M.row(i) << 1.0, p[i][0], p[i][1], p[i][2];
                corner[i] = cornerOf(p[i][0], p[i][1], p[i][2]);
            }
            const Eigen::Matrix4d C = M.inverse();      // N_i = C(0,i) + C(1,i) x + C(2,i) y + C(3,i) z
            G[t].setZero();
            for (int i = 0; i < 4; ++i)
                for (int a = 0; a < 3; ++a) G[t](a, corner[i]) = C(1 + a, i);
        }
    }
};

// The unit cell's conductivity matrix for the element (see Element in fea.hpp)
Eigen::Matrix<double, 8, 8> unitConductivity(Element e)
{
    if (e != Element::Tet) return hexUnitConductivity();
    const UnitTets cell;
    Eigen::Matrix<double, 8, 8> K = Eigen::Matrix<double, 8, 8>::Zero();
    for (int t = 0; t < 6; ++t) K += cell.G[t].transpose() * cell.G[t] / 6.0;
    return K;
}

// The temperature gradient of the cell's corner values, at its centre (the
// tetrahedra's average, by volume, for the tetrahedral element)
Eigen::Matrix<double, 3, 8> centreGradients(Element e)
{
    if (e != Element::Tet) return shapeGradients(0.5, 0.5, 0.5);
    const UnitTets cell;
    Eigen::Matrix<double, 3, 8> G = Eigen::Matrix<double, 3, 8>::Zero();
    for (int t = 0; t < 6; ++t) G += cell.G[t] / 6.0;
    return G;
}

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

template <typename T>
uint64_t fnvVec(uint64_t h, const std::vector<T>& v)
{
    return v.empty() ? h : fnv(h, v.data(), v.size() * sizeof(T));
}

}   // namespace

// The voxelized problem: the grid, the unknowns (one per node in use), the
// boundary conditions on them, the elements
struct ThermalProblem::Prepared
{
    int nxn = 0, nyn = 0, nzn = 0;
    size_t nNode = 0;
    std::vector<float> fraction;            // per grid element: how much is inside
    std::vector<int> active;                // the grid elements with material
    std::vector<size_t> usedNodes;          // per unknown: its grid node
    int n = 0;                              // unknowns
    std::vector<double> area;               // exposed area round each unknown
    std::vector<char> fixed;                // held at a temperature
    std::vector<double> T0;                 // the fixed temperatures (0 elsewhere)
    std::vector<double> q;                  // heat loads (W)
    std::vector<char> heated;               // loaded by a surface heat input
    std::vector<double> conv, ambient;      // h A, and the ambient temperature
    std::vector<std::array<int, 8>> dof;    // per active element: its unknowns
    std::vector<double> base;               // per active element: k h fraction
    std::vector<int> neStart, neEl;         // the active elements at each unknown,
    std::vector<unsigned char> neCorner;    //   and which corner the unknown is
    Eigen::Matrix<double, 8, 8> K1;
};

ThermalProblem::ThermalProblem(const Tree& shape, Eigen::Vector3d lo, Eigen::Vector3d hi,
                               double h, double conductivity)
    : m_shape(shape), m_lo(lo), m_h(h), m_k(conductivity)
{
    const Eigen::Vector3d size = hi - lo;
    m_ex = std::max(1, int(std::ceil(size.x() / h - 1e-9)));
    m_ey = std::max(1, int(std::ceil(size.y() / h - 1e-9)));
    m_ez = std::max(1, int(std::ceil(size.z() / h - 1e-9)));
}

ThermalProblem::~ThermalProblem() = default;

void ThermalProblem::addTemperature(const Tree& region, double value)
{
    m_temperatures.push_back(Temperature{region, value});
    m_prep.reset();
}

void ThermalProblem::addHeat(const Tree& region, double power)
{
    m_heats.push_back(Heat{region, power, false});
    m_prep.reset();
}

void ThermalProblem::addGeneration(const Tree& region, double power)
{
    m_heats.push_back(Heat{region, power, true});
    m_prep.reset();
}

void ThermalProblem::addConvection(const Tree& region, double coefficient, double ambient)
{
    m_convections.push_back(Convection{region, coefficient, ambient});
    m_prep.reset();
}

bool ThermalProblem::prepare(std::string& error)
{
    if (m_prep) return true;
    if (!(m_h > 0) || !(m_k > 0))
    {
        error = "invalid element size or conductivity (both must be > 0)";
        return false;
    }
    const size_t nElem = size_t(m_ex) * m_ey * m_ez;
    if (nElem > 8000000)
    {
        error = "too many elements (" + std::to_string(nElem) + "): use a larger element size";
        return false;
    }
    auto P = std::make_unique<Prepared>();
    const int nxn = m_ex + 1, nyn = m_ey + 1, nzn = m_ez + 1;
    const size_t nNode = size_t(nxn) * nyn * nzn;
    P->nxn = nxn;
    P->nyn = nyn;
    P->nzn = nzn;
    P->nNode = nNode;
    auto node = [&](int i, int j, int k) { return (size_t(k) * nyn + j) * nxn + i; };
    auto elem = [&](int i, int j, int k) { return (size_t(k) * m_ey + j) * m_ex + i; };
    auto nodePos = [&](int i, int j, int k) {
        return Eigen::Vector3f(float(m_lo.x() + m_h * i), float(m_lo.y() + m_h * j),
                               float(m_lo.z() + m_h * k));
    };

    // How much of each element lies inside the part: from its 8 corners and
    // centre, refined with a 3 x 3 x 3 sub-sample where they disagree
    std::vector<Eigen::Vector3f> pts(nNode);
    for (int k = 0; k < nzn; ++k)
        for (int j = 0; j < nyn; ++j)
            for (int i = 0; i < nxn; ++i) pts[node(i, j, k)] = nodePos(i, j, k);
    std::vector<float> nodeVal, centreVal;
    evalAll(m_shape, pts, nodeVal);
    pts.resize(nElem);
    const float hh = float(m_h);
    for (int k = 0; k < m_ez; ++k)
        for (int j = 0; j < m_ey; ++j)
            for (int i = 0; i < m_ex; ++i)
                pts[elem(i, j, k)] = nodePos(i, j, k) + Eigen::Vector3f(hh, hh, hh) * 0.5f;
    evalAll(m_shape, pts, centreVal);
    std::vector<float>& fraction = P->fraction;
    fraction.assign(nElem, 0.0f);
    std::vector<size_t> mixed;
    for (int k = 0; k < m_ez; ++k)
        for (int j = 0; j < m_ey; ++j)
            for (int i = 0; i < m_ex; ++i)
            {
                int inside = centreVal[elem(i, j, k)] <= 0;
                for (auto& c : CORNER) inside += nodeVal[node(i + c[0], j + c[1], k + c[2])] <= 0;
                if (inside == 9) fraction[elem(i, j, k)] = 1.0f;
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
        evalAll(m_shape, sub, subVal);
        for (size_t m = 0; m < mixed.size(); ++m)
        {
            int inside = 0;
            for (int s = 0; s < 27; ++s) inside += subVal[m * 27 + s] <= 0;
            fraction[mixed[m]] = inside / 27.0f;
        }
    }
    std::vector<int>& active = P->active;
    for (size_t e = 0; e < nElem; ++e) if (fraction[e] > 0) active.push_back(int(e));
    if (active.empty())
    {
        error = "the part has no material inside the analysis region";
        return false;
    }

    // Nodes in use, one unknown each; the exposed area around each node
    // (element faces with no material across them), for convection
    std::vector<int> dofOf(nNode, -1);
    int n = 0;
    std::vector<size_t>& usedNodes = P->usedNodes;
    for (int e : active)
    {
        const int i = e % m_ex, j = (e / m_ex) % m_ey, k = e / (m_ex * m_ey);
        for (auto& c : CORNER)
        {
            const size_t nd = node(i + c[0], j + c[1], k + c[2]);
            if (dofOf[nd] < 0)
            {
                dofOf[nd] = n++;
                usedNodes.push_back(nd);
            }
        }
    }
    P->n = n;
    const size_t un = static_cast<size_t>(n);
    std::vector<double>& area = P->area;
    area.assign(un, 0.0);
    {
        // each face: the 4 corner indices (into CORNER) and the neighbour's offset
        const int faces[6][5] = {{0, 3, 7, 4, -1}, {1, 2, 6, 5, +1}, {0, 1, 5, 4, -2},
                                 {3, 2, 6, 7, +2}, {0, 1, 2, 3, -3}, {4, 5, 6, 7, +3}};
        const double quarter = m_h * m_h / 4.0;
        for (int e : active)
        {
            const int i = e % m_ex, j = (e / m_ex) % m_ey, k = e / (m_ex * m_ey);
            for (const auto& f : faces)
            {
                int a = i, b = j, c = k;
                const int axis = std::abs(f[4]) - 1, dir = f[4] > 0 ? 1 : -1;
                (axis == 0 ? a : axis == 1 ? b : c) += dir;
                const bool open = a < 0 || b < 0 || c < 0 || a >= m_ex || b >= m_ey || c >= m_ez ||
                                  fraction[elem(a, b, c)] <= 0;
                if (!open) continue;
                for (int q = 0; q < 4; ++q)
                {
                    const auto& cc = CORNER[f[q]];
                    area[size_t(dofOf[node(i + cc[0], j + cc[1], k + cc[2])])] += quarter;
                }
            }
        }
    }
    std::vector<Eigen::Vector3f> usedPts(usedNodes.size());
    for (size_t u = 0; u < usedNodes.size(); ++u)
    {
        const size_t nd = usedNodes[u];
        usedPts[u] = nodePos(int(nd % nxn), int((nd / nxn) % nyn), int(nd / (size_t(nxn) * nyn)));
    }

    // Boundary conditions (a node within half an element of a region counts
    // as inside it: grid nodes rarely lie exactly on a face)
    const float reach = float(0.5 * m_h);
    P->fixed.assign(un, 0);
    P->T0.assign(un, 0.0);
    P->q.assign(un, 0.0);
    P->heated.assign(un, 0);
    P->conv.assign(un, 0.0);
    P->ambient.assign(un, 0.0);
    for (const auto& t : m_temperatures)
    {
        std::vector<float> v;
        evalAll(t.region, usedPts, v);
        int hit = 0;
        for (size_t u = 0; u < un; ++u)
        {
            if (!(v[u] <= reach)) continue;
            P->fixed[u] = 1;
            P->T0[u] = t.value;
            hit++;
        }
        if (!hit)
        {
            error = "a fixed-temperature region doesn't touch the part";
            return false;
        }
    }
    heatIn = 0;
    for (const auto& ht : m_heats)
    {
        if (ht.volume)
        {
            // over the part's volume inside the region: each element whose
            // centre is inside it, by its material, an eighth to each corner
            std::vector<Eigen::Vector3f> centres(active.size());
            for (size_t a = 0; a < active.size(); ++a)
            {
                const int e = active[a];
                const int i = e % m_ex, j = (e / m_ex) % m_ey, k = e / (m_ex * m_ey);
                centres[a] = nodePos(i, j, k) + Eigen::Vector3f(hh, hh, hh) * 0.5f;
            }
            std::vector<float> v;
            evalAll(ht.region, centres, v);
            double total = 0;
            for (size_t a = 0; a < active.size(); ++a) if (v[a] <= 0) total += fraction[size_t(active[a])];
            if (!(total > 0))
            {
                error = "a heat generation region doesn't overlap the part";
                return false;
            }
            for (size_t a = 0; a < active.size(); ++a)
            {
                if (!(v[a] <= 0)) continue;
                const int e = active[a];
                const int i = e % m_ex, j = (e / m_ex) % m_ey, k = e / (m_ex * m_ey);
                const double share = ht.power * fraction[size_t(e)] / total / 8.0;
                for (auto& c : CORNER) P->q[size_t(dofOf[node(i + c[0], j + c[1], k + c[2])])] += share;
            }
            heatIn += ht.power;
            continue;
        }
        std::vector<float> v;
        evalAll(ht.region, usedPts, v);
        std::vector<size_t> onSurface, anywhere;
        for (size_t u = 0; u < un; ++u)
        {
            if (!(v[u] <= reach)) continue;
            anywhere.push_back(u);
            if (area[u] > 0) onSurface.push_back(u);
        }
        const auto& target = onSurface.empty() ? anywhere : onSurface;
        if (target.empty())
        {
            error = "a heat input region doesn't touch the part";
            return false;
        }
        // (over the surface: by the exposed area round each node, a uniform
        // flux -- an equal share per node heated the corners of a face,
        // which stand for a quarter of the area, four times as much)
        double total = 0;
        if (!onSurface.empty()) for (size_t u : target) total += area[u];
        for (size_t u : target)
        {
            P->q[u] += total > 0 ? ht.power * area[u] / total : ht.power / double(target.size());
            P->heated[u] = 1;
        }
        heatIn += ht.power;
    }
    for (const auto& cv : m_convections)
    {
        std::vector<float> v;
        evalAll(cv.region, usedPts, v);
        int hit = 0;
        for (size_t u = 0; u < un; ++u)
        {
            if (!(v[u] <= reach) || !(area[u] > 0)) continue;
            P->conv[u] += cv.coefficient * area[u];
            P->ambient[u] = cv.ambient;      // (the last region's, where they overlap)
            hit++;
        }
        if (!hit)
        {
            error = "a convection region doesn't touch the part's surface";
            return false;
        }
    }
    bool anchored = false;
    for (size_t u = 0; u < un; ++u) anchored = anchored || P->fixed[u] || P->conv[u] > 0;
    if (!anchored)
    {
        error = "no fixed temperature or convection: the temperature isn't determined "
                "(add fixed_temperature(region, T) or convection(region, h, ambient))";
        return false;
    }

    // The elements, and the elements at each unknown (the product K x is
    // gathered per unknown, so it runs on all cores without write clashes)
    P->dof.resize(active.size());
    P->base.resize(active.size());
    std::vector<int> count(un + 1, 0);
    for (size_t a = 0; a < active.size(); ++a)
    {
        const int e = active[a];
        const int i = e % m_ex, j = (e / m_ex) % m_ey, k = e / (m_ex * m_ey);
        for (int c = 0; c < 8; ++c)
        {
            const int d = dofOf[node(i + CORNER[c][0], j + CORNER[c][1], k + CORNER[c][2])];
            P->dof[a][size_t(c)] = d;
            count[size_t(d) + 1]++;
        }
        P->base[a] = m_k * m_h * std::max(0.01, double(fraction[size_t(e)]));
    }
    for (size_t u = 0; u < un; ++u) count[u + 1] += count[u];
    P->neStart = count;
    P->neEl.assign(size_t(count[un]), 0);
    P->neCorner.assign(size_t(count[un]), 0);
    for (size_t a = 0; a < active.size(); ++a)
        for (int c = 0; c < 8; ++c)
        {
            const size_t d = size_t(P->dof[a][size_t(c)]);
            const size_t at = size_t(count[d]++);
            P->neEl[at] = int(a);
            P->neCorner[at] = static_cast<unsigned char>(c);
        }
    P->K1 = unitConductivity(m_element);

    elements = int(active.size());
    nodes = n;
    m_prep = std::move(P);
    return true;
}

uint64_t ThermalProblem::hash() const
{
    if (!m_prep) return 0;
    const Prepared& P = *m_prep;
    uint64_t h = 1469598103934665603ull;
    const int dims[3] = {m_ex, m_ey, m_ez};
    const double num[6] = {m_h, m_k, m_lo.x(), m_lo.y(), m_lo.z(), double(int(m_element))};
    h = fnv(h, dims, sizeof(dims));
    h = fnv(h, num, sizeof(num));
    h = fnvVec(h, P.fraction);
    h = fnvVec(h, P.fixed);
    h = fnvVec(h, P.T0);
    h = fnvVec(h, P.q);
    h = fnvVec(h, P.heated);
    h = fnvVec(h, P.conv);
    h = fnvVec(h, P.ambient);
    return h;
}

void ThermalProblem::conduct(const std::vector<double>& mult, const std::vector<double>& conv,
                             const std::vector<double>& x, std::vector<double>& y) const
{
    const Prepared& P = *m_prep;
    y.resize(size_t(P.n));
    parallelRange(size_t(P.n), [&](size_t u0, size_t u1) {
        for (size_t u = u0; u < u1; ++u)
        {
            double s = 0;
            for (int m = P.neStart[u]; m < P.neStart[u + 1]; ++m)
            {
                const size_t a = size_t(P.neEl[size_t(m)]);
                const int c = P.neCorner[size_t(m)];
                const auto& d = P.dof[a];
                double r = 0;
                for (int c2 = 0; c2 < 8; ++c2) r += P.K1(c, c2) * x[size_t(d[size_t(c2)])];
                s += P.base[a] * mult[a] * r;
            }
            y[u] = s + conv[u] * x[u];
        }
    }, 2048);
}

bool ThermalProblem::pcg(const std::vector<double>& mult, const std::vector<double>& conv,
                         const std::vector<double>& b, std::vector<double>& x,
                         int maxIterations, double tolerance, int& its, double& rel,
                         std::string& error, const std::function<void(int, double)>& progress)
{
    const Prepared& P = *m_prep;
    const size_t n = size_t(P.n);
    std::vector<double> inv(n, 0.0);
    for (size_t a = 0; a < P.dof.size(); ++a)
        for (int c = 0; c < 8; ++c) inv[size_t(P.dof[a][size_t(c)])] += P.base[a] * mult[a] * P.K1(c, c);
    for (size_t u = 0; u < n; ++u) inv[u] = P.fixed[u] ? 0.0 : 1.0 / (inv[u] + conv[u]);
    auto dot = [&](const std::vector<double>& a, const std::vector<double>& c) {
        return parallelTotal(n, [&](size_t u0, size_t u1) {
            double s = 0;
            for (size_t u = u0; u < u1; ++u) s += a[u] * c[u];
            return s;
        });
    };
    x.resize(n, 0.0);
    std::vector<double> r(n), z(n), p(n), Ap(n);
    for (size_t u = 0; u < n; ++u) if (P.fixed[u]) x[u] = 0.0;
    conduct(mult, conv, x, Ap);
    double bb = 0;
    for (size_t u = 0; u < n; ++u)
    {
        r[u] = P.fixed[u] ? 0.0 : b[u] - Ap[u];
        z[u] = r[u] * inv[u];
        p[u] = z[u];
        if (!P.fixed[u]) bb += b[u] * b[u];
    }
    its = 0;
    const double bnorm = std::sqrt(bb);
    if (!(bnorm > 0))
    {
        for (size_t u = 0; u < n; ++u) x[u] = 0.0;
        rel = 0;
        return true;
    }
    rel = std::sqrt(dot(r, r)) / bnorm;
    double rz = dot(r, z);
    for (; its < maxIterations && rel > tolerance; ++its)
    {
        conduct(mult, conv, p, Ap);
        for (size_t u = 0; u < n; ++u) if (P.fixed[u]) Ap[u] = 0.0;
        const double pAp = dot(p, Ap);
        if (!(pAp > 0)) break;
        const double alpha = rz / pAp;
        parallelRange(n, [&](size_t u0, size_t u1) {
            for (size_t u = u0; u < u1; ++u)
            {
                x[u] += alpha * p[u];
                r[u] -= alpha * Ap[u];
                z[u] = r[u] * inv[u];
            }
        }, 4096);
        rel = std::sqrt(dot(r, r)) / bnorm;
        if ((its & 15) == 0 && progress) progress(its, rel);
        const double rzNew = dot(r, z);
        const double beta = rzNew / rz;
        rz = rzNew;
        parallelRange(n, [&](size_t u0, size_t u1) {
            for (size_t u = u0; u < u1; ++u) p[u] = z[u] + beta * p[u];
        }, 4096);
    }
    if (!(rel <= std::max(tolerance * 100, 1e-4)))
    {
        error = "the thermal solver did not converge (" + std::to_string(its) + " iterations, residual " +
                std::to_string(rel) + ")";
        return false;
    }
    return true;
}

bool ThermalProblem::solve(int maxIterations, double tolerance, std::string& error)
{
    const auto t0 = std::chrono::steady_clock::now();
    run_progress::Task task("thermal analysis");
    task.set(0.0, "preparing the grid");
    if (!prepare(error)) return false;
    const Prepared& P = *m_prep;
    const size_t n = size_t(P.n), na = P.active.size();
    const std::vector<double> ones(na, 1.0);

    // b = q + h A T_ambient - K T_fixed, solved for the free nodes
    std::vector<double> T = P.T0, KT, b(n), x(n, 0.0);
    conduct(ones, P.conv, T, KT);
    for (size_t u = 0; u < n; ++u)
        b[u] = P.fixed[u] ? 0.0 : P.q[u] + P.conv[u] * P.ambient[u] - KT[u];
    int it = 0;
    double rel = 0;
    const bool ok = pcg(ones, P.conv, b, x, maxIterations, tolerance, it, rel, error, [&](int i, double r) {
        double f = double(i) / std::max(1, maxIterations);
        if (r > 0 && tolerance < 1) f = std::max(f, std::log(1.0 / r) / std::log(1.0 / tolerance));
        char buf[64];
        snprintf(buf, sizeof(buf), "solving, residual %.0e of %.0e", r, tolerance);
        task.set(f, buf);
    });
    if (!ok) return false;
    for (size_t u = 0; u < n; ++u) if (!P.fixed[u]) T[u] += x[u];

    // Heat balance: out through the fixed nodes (their reaction) and to the
    // ambient
    conduct(ones, P.conv, T, KT);
    heatOutFixed = 0;
    heatOutConvection = 0;
    for (size_t u = 0; u < n; ++u)
    {
        heatOutConvection += P.conv[u] * (T[u] - P.ambient[u]);
        // (a fixed node's balance: what its boundary supplies + its heat
        // input = what it conducts away + what it loses to the ambient)
        if (P.fixed[u])
            heatOutFixed += P.q[u] + P.conv[u] * P.ambient[u] - KT[u];
    }

    // Results on the nodes: the temperature, and the heat flux -k grad T
    // (per element at its centre, averaged over the elements at each node)
    const size_t nNode = P.nNode;
    const int nxn = P.nxn, nyn = P.nyn, nzn = P.nzn;
    auto node = [&](int i, int j, int k) { return (size_t(k) * nyn + j) * nxn + i; };
    auto R = std::make_shared<Result>();
    R->lo = m_lo;
    R->h = m_h;
    R->tetrahedra = m_element == Element::Tet;
    R->nx = nxn;
    R->ny = nyn;
    R->nz = nzn;
    for (auto& f : R->fields) f.clear();
    for (int f = 0; f < 5; ++f) R->fields[f].assign(nNode, 0.0f);
    std::vector<char> known(nNode, 0);
    std::vector<Eigen::Vector3d> flux(nNode, Eigen::Vector3d::Zero());
    std::vector<double> weight(nNode, 0.0);
    const Eigen::Matrix<double, 3, 8> Gc = centreGradients(m_element) / m_h;
    for (size_t a = 0; a < na; ++a)
    {
        Eigen::Matrix<double, 8, 1> te;
        for (int c = 0; c < 8; ++c) te[c] = T[size_t(P.dof[a][size_t(c)])];
        const Eigen::Vector3d qe = -m_k * (Gc * te);
        const double w = P.base[a];
        for (int c = 0; c < 8; ++c)
        {
            const size_t nd = P.usedNodes[size_t(P.dof[a][size_t(c)])];
            flux[nd] += w * qe;
            weight[nd] += w;
        }
    }
    for (size_t u = 0; u < n; ++u)
    {
        const size_t nd = P.usedNodes[u];
        known[nd] = 1;
        const Eigen::Vector3d qn = weight[nd] > 0 ? Eigen::Vector3d(flux[nd] / weight[nd]) : Eigen::Vector3d::Zero();
        R->fields[TEMPERATURE][nd] = float(T[u]);
        R->fields[HEAT_FLUX][nd] = float(qn.norm());
        R->fields[QX][nd] = float(qn.x());
        R->fields[QY][nd] = float(qn.y());
        R->fields[QZ][nd] = float(qn.z());
    }
    for (int f = 0; f < 5; ++f)
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
    // (nodes off the part take their nearest part node's values, so a
    // surface between grid nodes reads the part's values)
    {
        std::deque<size_t> queue;
        std::vector<size_t> source(nNode, size_t(-1));
        for (size_t nd = 0; nd < nNode; ++nd) if (known[nd]) { queue.push_back(nd); source[nd] = nd; }
        while (!queue.empty())
        {
            const size_t nd = queue.front();
            queue.pop_front();
            const int i = int(nd % nxn), j = int((nd / nxn) % nyn), k = int(nd / (size_t(nxn) * nyn));
            const int nb[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
            for (auto& o : nb)
            {
                const int a = i + o[0], b2 = j + o[1], c = k + o[2];
                if (a < 0 || b2 < 0 || c < 0 || a >= nxn || b2 >= nyn || c >= nzn) continue;
                const size_t m = node(a, b2, c);
                if (source[m] != size_t(-1)) continue;
                source[m] = source[nd];
                queue.push_back(m);
            }
        }
        for (size_t nd = 0; nd < nNode; ++nd)
        {
            if (known[nd] || source[nd] == size_t(-1)) continue;
            for (int f = 0; f < 5; ++f) R->fields[f][nd] = R->fields[f][source[nd]];
        }
    }
    R->elements = int(na);
    R->nodes = int(n);
    R->iterations = it;
    R->residual = rel;
    m_result = R;
    iterations = it;
    residual = rel;
    seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    R->seconds = seconds;
    return true;
}

////////////////////////////////////////////////////////////////////////////////
// Topology optimization: SIMP on the conductivity, density filter,
// optimality criteria (as for stiffness in fea.cpp)

bool ThermalProblem::optimize(const TopOpt& s, std::string& error)
{
    const auto t0 = std::chrono::steady_clock::now();
    {
        run_progress::Task prep("preparing the grid");
        if (!prepare(error)) return false;
    }
    if (!(s.volumeFraction > 0 && s.volumeFraction < 1))
    {
        error = "the volume fraction must be between 0 and 1";
        return false;
    }
    const Prepared& P = *m_prep;
    const size_t n = size_t(P.n), na = P.active.size();
    const double h = m_h;

    // The objective: the heat-weighted mean temperature where heat goes in
    double Q = 0;
    for (size_t u = 0; u < n; ++u)
    {
        if (P.q[u] < 0)
        {
            error = "the heat inputs must be positive (the optimization keeps them cool)";
            return false;
        }
        Q += P.q[u];
    }
    if (!(Q > 0))
    {
        error = "thermal topology optimization needs a heat input or heat generation";
        return false;
    }

    // Element centres, which elements are fixed solid / empty
    std::vector<Eigen::Vector3f> centres(na);
    std::vector<int> activeOf(size_t(m_ex) * m_ey * m_ez, -1);
    for (size_t a = 0; a < na; ++a)
    {
        const int e = P.active[a];
        activeOf[size_t(e)] = int(a);
        const int i = e % m_ex, j = (e / m_ex) % m_ey, k = e / (m_ex * m_ey);
        centres[a] = (m_lo + Eigen::Vector3d(i + 0.5, j + 0.5, k + 0.5) * h).cast<float>();
    }
    std::vector<char> state(na, 0);             // 0 design, 1 solid, 2 empty
    std::vector<float> vals;
    for (const auto& t : s.avoid)
    {
        evalAll(t, centres, vals);
        for (size_t a = 0; a < na; ++a) if (vals[a] < 0) state[a] = 2;
    }
    for (const auto& t : s.keep)
    {
        evalAll(t, centres, vals);
        for (size_t a = 0; a < na; ++a) if (vals[a] < 0) state[a] = 1;
    }
    // Elements that hold the fixed temperatures and the surface heat inputs
    // stay solid (heat generation is spread through the design: not kept)
    for (size_t a = 0; a < na; ++a)
        for (int c = 0; c < 8 && state[a] != 1; ++c)
        {
            const size_t d = size_t(P.dof[a][size_t(c)]);
            if (P.fixed[d] || P.heated[d]) state[a] = 1;
        }

    // Extrusion: the design elements on each line along the axis share one
    // value (their sensitivities summed), so every hole runs through
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
            const int e = P.active[a];
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
        const int e = P.active[a];
        const int i = e % m_ex, j = (e / m_ex) % m_ey, k = e / (m_ex * m_ey);
        for (int dk = -R; dk <= R; ++dk)
            for (int dj = -R; dj <= R; ++dj)
                for (int di = -R; di <= R; ++di)
                {
                    const int a2 = i + di, b2 = j + dj, c2 = k + dk;
                    if (a2 < 0 || b2 < 0 || c2 < 0 || a2 >= m_ex || b2 >= m_ey || c2 >= m_ez) continue;
                    const int o = activeOf[(size_t(c2) * m_ey + b2) * m_ex + a2];
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
        parallelRange(na, [&](size_t b0, size_t b1) {
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
        frac[a] = std::max(0.01, double(P.fraction[size_t(P.active[a])]));
        total += frac[a];
    }
    const double target = s.volumeFraction * total;

    // Convection follows the design: it acts on the element faces inside a
    // convection region, each weighted by the density step across it --
    // |rho_a - rho_b| between two elements (smoothed), rho_a on the design
    // space's boundary -- so however wide the grey transition, a surface
    // gets its full h A.  Each face's conductance goes a quarter to each of
    // its corners.
    struct Face { int a, b; int u[4]; double g, ambient; };
    std::vector<Face> faces;
    if (!m_convections.empty())
    {
        const int sides[6][5] = {{0, 3, 7, 4, -1}, {1, 2, 6, 5, +1}, {0, 1, 5, 4, -2},
                                 {3, 2, 6, 7, +2}, {0, 1, 2, 3, -3}, {4, 5, 6, 7, +3}};
        std::vector<Eigen::Vector3f> fc;
        for (size_t a = 0; a < na; ++a)
        {
            const int e = P.active[a];
            const int i = e % m_ex, j = (e / m_ex) % m_ey, k = e / (m_ex * m_ey);
            for (const auto& f : sides)
            {
                int ni = i, nj = j, nk = k;
                const int axis = std::abs(f[4]) - 1, dir = f[4] > 0 ? 1 : -1;
                (axis == 0 ? ni : axis == 1 ? nj : nk) += dir;
                int b = -1;
                if (ni >= 0 && nj >= 0 && nk >= 0 && ni < m_ex && nj < m_ey && nk < m_ez)
                    b = activeOf[(size_t(nk) * m_ey + nj) * m_ex + ni];
                if (b >= 0 && size_t(b) < a) continue;      // (counted from b)
                Face F;
                F.a = int(a);
                F.b = b;
                for (int q = 0; q < 4; ++q) F.u[q] = P.dof[a][size_t(f[q])];
                F.g = 0;
                F.ambient = 0;
                faces.push_back(F);
                Eigen::Vector3f c = centres[a];
                c[axis] += float(0.5 * h * dir);
                fc.push_back(c);
            }
        }
        const float reach = float(0.5 * h);
        for (const auto& cv : m_convections)
        {
            std::vector<float> v;
            evalAll(cv.region, fc, v);
            for (size_t f = 0; f < faces.size(); ++f)
            {
                if (!(v[f] <= reach)) continue;
                faces[f].g += cv.coefficient * h * h;
                faces[f].ambient = cv.ambient;          // (the last region's, where they overlap)
            }
        }
        faces.erase(std::remove_if(faces.begin(), faces.end(),
                                   [](const Face& F) { return !(F.g > 0); }), faces.end());
    }
    const double eps = 0.01;       // the smoothing of |rho_a - rho_b|
    std::vector<double> conv(n, 0.0), convAmb(n, 0.0);
    auto convection = [&](const std::vector<double>& rho) {
        std::fill(conv.begin(), conv.end(), 0.0);
        std::fill(convAmb.begin(), convAmb.end(), 0.0);
        for (const auto& F : faces)
        {
            const double d = F.b < 0 ? 0 : rho[size_t(F.a)] - rho[size_t(F.b)];
            const double w = F.b < 0 ? rho[size_t(F.a)] : std::sqrt(d * d + eps * eps) - eps;
            const double c = 0.25 * F.g * w;
            for (int q = 0; q < 4; ++q)
            {
                conv[size_t(F.u[q])] += c;
                convAmb[size_t(F.u[q])] += c * F.ambient;
            }
        }
    };

    // With one reference temperature (every fixed temperature and ambient
    // the same), T - reference solves K theta = q: the adjoint is theta / Q
    // and needs no solve of its own
    bool uniform = true, any = false;
    double ref = 0;
    auto see = [&](double v) {
        if (!any) { ref = v; any = true; }
        else if (std::abs(v - ref) > 1e-9 * (1 + std::abs(ref))) uniform = false;
    };
    for (size_t u = 0; u < n; ++u) if (P.fixed[u]) see(P.T0[u]);
    for (const auto& F : faces) see(F.ambient);

    const double p = s.penalty, kmin = s.minConductivity;
    std::vector<double> x(na, s.volumeFraction), xPhys, mult(na), dc(na), dv(na), dcF, dvF, xNew(na);
    for (size_t a = 0; a < na; ++a) if (state[a]) x[a] = state[a] == 1 ? 1.0 : 0.0;
    auto physical = [&](const std::vector<double>& xs, std::vector<double>& out) {
        filter(xs, out);
        for (size_t a = 0; a < na; ++a)
        {
            if (state[a] == 1) out[a] = 1.0;
            else if (state[a] == 2) out[a] = 0.0;
        }
    };
    std::vector<double> T(n), KT, b(n), bq(n), Tx(n, 0.0), L(n, 0.0);
    for (size_t u = 0; u < n; ++u) bq[u] = P.fixed[u] ? 0.0 : P.q[u] / Q;
    m_history.clear();

    run_progress::Task task("optimising");
    std::vector<double> changes;
    int expected = std::max(1, s.iterations);
    auto toReach = [](const std::vector<double>& v, double goal) {
        // iterations until v reaches goal, from its fall over the last (up
        // to) 5 values; -1 when it isn't falling
        const size_t H = v.size();
        if (H < 3 || !(v.back() > 0)) return -1.0;
        if (v.back() <= goal) return 0.0;
        const size_t m = std::min<size_t>(5, H - 1);
        if (!(v[H - 1 - m] > 0)) return -1.0;
        const double rate = std::pow(v.back() / v[H - 1 - m], 1.0 / double(m));
        if (!(rate < 0.999)) return -1.0;
        return std::log(goal / v.back()) / std::log(rate);
    };

    int it = 0;
    for (; it < s.iterations; ++it)
    {
        {
            char buf[64];
            snprintf(buf, sizeof(buf), "optimising, iteration %d of ~%d", it + 1, expected);
            task.set(double(it) / expected, buf);
        }
        physical(x, xPhys);
        for (size_t a = 0; a < na; ++a) mult[a] = kmin + (1 - kmin) * std::pow(xPhys[a], p);
        convection(xPhys);

        // the temperatures (warm-started from the last design's)
        conduct(mult, conv, P.T0, KT);
        for (size_t u = 0; u < n; ++u)
            b[u] = P.fixed[u] ? 0.0 : P.q[u] + convAmb[u] - KT[u];
        int its = 0;
        double rel = 0;
        if (!pcg(mult, conv, b, Tx, s.solverIterations, s.tolerance, its, rel, error, nullptr)) return false;
        double J = 0;
        for (size_t u = 0; u < n; ++u)
        {
            T[u] = P.fixed[u] ? P.T0[u] : Tx[u];
            J += P.q[u] * T[u];
        }
        J /= Q;
        m_history.push_back(J);

        // the adjoint: K L = q / Q (zero at the fixed nodes)
        if (uniform)
            for (size_t u = 0; u < n; ++u) L[u] = P.fixed[u] ? 0.0 : (T[u] - ref) / Q;
        else if (!pcg(mult, conv, bq, L, s.solverIterations, s.tolerance, its, rel, error, nullptr))
            return false;

        // dJ / dx_e = -L_e . dK_e / dx_e T_e
        parallelRange(na, [&](size_t b0, size_t b1) {
            Eigen::Matrix<double, 8, 1> te, le;
            for (size_t a = b0; a < b1; ++a)
            {
                for (int c = 0; c < 8; ++c)
                {
                    const size_t d = size_t(P.dof[a][size_t(c)]);
                    te[c] = T[d];
                    le[c] = L[d];
                }
                const double ce = le.dot(P.K1 * te);
                dc[a] = -p * std::pow(std::max(xPhys[a], 1e-9), p - 1) * (1 - kmin) * P.base[a] * ce;
                dv[a] = frac[a];
            }
        }, 512);
        // + the convection's: dJ / dw_f = L . (d b / dw_f - d K / dw_f T)
        //   = sum over the face's corners of L_u (ambient - T_u) g / 4
        for (const auto& F : faces)
        {
            double sf = 0;
            for (int q = 0; q < 4; ++q)
            {
                const size_t u = size_t(F.u[q]);
                sf += L[u] * (F.ambient - T[u]);
            }
            sf *= 0.25 * F.g;
            if (F.b < 0)
            {
                dc[size_t(F.a)] += sf;
                continue;
            }
            const double d = xPhys[size_t(F.a)] - xPhys[size_t(F.b)];
            const double dw = d / std::sqrt(d * d + eps * eps);
            dc[size_t(F.a)] += sf * dw;
            dc[size_t(F.b)] -= sf * dw;
        }
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
            for (size_t hh = 6; hh <= m_history.size(); ++hh)
                flatness.push_back(std::abs(m_history[hh - 1] - m_history[hh - 6]) /
                                   std::max(1e-300, std::abs(m_history[hh - 1] - (uniform ? ref : 0))));
            double more = -1;
            for (double m : {toReach(changes, 0.01), toReach(flatness, 2e-3)})
                if (m >= 0) more = more < 0 ? m : std::min(more, m);
            if (more >= 0)
                expected = std::min(s.iterations, std::max(16, it + 1 + int(std::ceil(more))));
            task.set(double(it + 1) / expected);
        }
        // converged: the design stopped moving, or the temperature stopped
        // improving over the last 5 iterations (relative to its rise over
        // the reference, when there is one)
        const size_t H = m_history.size();
        const double scale = std::abs(m_history[H - 1] - (uniform ? ref : 0));
        const bool flat = H > 6 && std::abs(m_history[H - 1] - m_history[H - 6]) < 2e-3 * scale;
        if (it >= 15 && (change < 0.01 || flat)) { ++it; break; }
    }
    physical(x, xPhys);

    // The density as a field on the element centres
    auto R2 = std::make_shared<Result>();
    R2->lo = m_lo + Eigen::Vector3d::Constant(0.5 * h);
    R2->h = h;
    R2->nx = m_ex;
    R2->ny = m_ey;
    R2->nz = m_ez;
    R2->fields[0].assign(size_t(m_ex) * m_ey * m_ez, 0.0f);
    for (size_t a = 0; a < na; ++a) R2->fields[0][size_t(P.active[a])] = float(xPhys[a]);
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
    seconds = R2->seconds;
    return true;
}

}   // namespace fea
}   // namespace libfive
