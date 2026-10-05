/*
libfive: a CAD kernel for modeling with implicit functions

Steady incompressible laminar flow on a body-fitted tetrahedral mesh; see tetflow.hpp.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <array>
#include <limits>
#include <map>
#include <numeric>
#include <string>
#include <thread>
#include <unordered_map>

#include <Eigen/Sparse>
#include <Eigen/SparseLU>
#include <Eigen/IterativeLinearSolvers>

#include "libfive/fea/tetflow.hpp"
#include "libfive/tree/content_key.hpp"
#include "libfive/run_progress.hpp"
#include "tet_common.hpp"

namespace libfive {
namespace fea {

using namespace tet;

// The preconditioner of the flow's linear solves: the incomplete LU of the scaled system in BLOCKS -- the fluid cut
// into as many compact boxes as there are cores (recursive median splits of the nodes along the box's longest axis),
// each box factorised on its own together with the nodes within an overlap of its neighbours and applied on its own,
// answering for its own nodes (additive Schwarz with geometric overlap) -- so the factorisation and every application
// run on all cores.  The price is more BiCGSTAB iterations than one factorisation of the whole would need; each is
// many times cheaper.
struct FlowIlut
{
    struct Block
    {
        std::vector<int> nodes;         // the nodes factorised: the box's own first, then the overlap
        int own = 0;                    // how many of them the block answers for
        std::unique_ptr<Eigen::IncompleteLUT<double>> f;
    };
    std::vector<Block> blocks;
    bool ok = false;
    long N = 0;

    // (nodes: the mesh's positions, four unknowns each in the order of the nodes; h the element size)
    void build(const std::vector<int>& outer, const std::vector<int>& inner, const std::vector<double>& val,
               long rows, const std::vector<Eigen::Vector3d>& pos, double h, int fill, double drop)
    {
        N = rows;
        const int nv = int(pos.size());
        int threads = int(std::thread::hardware_concurrency());
        if (threads < 1) threads = 1;
        if (const char* e = std::getenv("FIELDES_FLOW_BLOCKS")) threads = std::max(1, std::atoi(e));     // (for the developer)
        const int P = std::max(1, std::min(threads, nv / 1000));
        double overlap = 1.5 * h;
        if (const char* e = std::getenv("FIELDES_FLOW_OVERLAP")) overlap = std::atof(e) * h;             // (in elements)
        // the boxes: a set of nodes split at the median of its longest axis, the halves in proportion to the
        // boxes each is to hold
        std::vector<std::vector<int>> own;
        std::vector<int> all;
        all.resize(size_t(nv));
        for (int i = 0; i < nv; ++i) all[size_t(i)] = i;
        std::function<void(std::vector<int>&, int)> split = [&](std::vector<int>& set, int parts) {
            if (parts <= 1 || set.size() < 2)
            {
                own.push_back(std::move(set));
                return;
            }
            Eigen::Vector3d lo = pos[size_t(set[0])], hi = lo;
            for (int i : set)
            {
                lo = lo.cwiseMin(pos[size_t(i)]);
                hi = hi.cwiseMax(pos[size_t(i)]);
            }
            int axis = 0;
            for (int k = 1; k < 3; ++k) if (hi[k] - lo[k] > hi[axis] - lo[axis]) axis = k;
            const int p1 = parts / 2, p2 = parts - p1;
            const size_t cut = set.size() * size_t(p1) / size_t(parts);
            std::nth_element(set.begin(), set.begin() + long(cut), set.end(),
                             [&](int a, int b) { return pos[size_t(a)][axis] < pos[size_t(b)][axis]; });
            std::vector<int> left(set.begin(), set.begin() + long(cut)), right(set.begin() + long(cut), set.end());
            split(left, p1);
            split(right, p2);
        };
        split(all, P);
        blocks.clear();
        blocks.resize(own.size());
        // each box's bounds, grown by the overlap: the nodes inside are factorised with it
        std::vector<Eigen::Vector3d> blo(own.size()), bhi(own.size());
        for (size_t b = 0; b < own.size(); ++b)
        {
            blo[b] = pos[size_t(own[b][0])];
            bhi[b] = blo[b];
            for (int i : own[b])
            {
                blo[b] = blo[b].cwiseMin(pos[size_t(i)]);
                bhi[b] = bhi[b].cwiseMax(pos[size_t(i)]);
            }
            blo[b].array() -= overlap;
            bhi[b].array() += overlap;
            blocks[b].nodes = own[b];
            blocks[b].own = int(own[b].size());
        }
        {
            std::vector<int> home(size_t(nv), -1);
            for (size_t b = 0; b < own.size(); ++b) for (int i : own[b]) home[size_t(i)] = int(b);
            for (int i = 0; i < nv; ++i)
                for (size_t b = 0; b < own.size(); ++b)
                {
                    if (home[size_t(i)] == int(b)) continue;
                    const Eigen::Vector3d& q = pos[size_t(i)];
                    if ((q.array() >= blo[b].array()).all() && (q.array() <= bhi[b].array()).all()) blocks[b].nodes.push_back(i);
                }
        }
        std::atomic<bool> allOk{true};
        parallelRange(blocks.size(), [&](size_t b0, size_t b1) {
            std::vector<int> local(size_t(nv), -1);
            for (size_t b = b0; b < b1; ++b)
            {
                Block& B = blocks[b];
                const int n = int(B.nodes.size());
                for (int k = 0; k < n; ++k) local[size_t(B.nodes[size_t(k)])] = k;
                std::vector<Eigen::Triplet<double>> trip;
                trip.reserve(size_t(n) * 4 * 48);
                for (int k = 0; k < n; ++k)
                    for (int c0 = 0; c0 < 4; ++c0)
                    {
                        const size_t r = size_t(B.nodes[size_t(k)]) * 4 + size_t(c0);
                        for (int e = outer[r]; e < outer[r + 1]; ++e)
                        {
                            const int c = inner[size_t(e)];
                            const int lc = local[size_t(c / 4)];
                            if (lc >= 0) trip.emplace_back(4 * k + c0, 4 * lc + c % 4, val[size_t(e)]);
                        }
                    }
                Eigen::SparseMatrix<double> M(4 * n, 4 * n);
                M.setFromTriplets(trip.begin(), trip.end());
                B.f.reset(new Eigen::IncompleteLUT<double>());
                B.f->setFillfactor(fill);
                B.f->setDroptol(drop);
                B.f->compute(M);
                if (B.f->info() != Eigen::Success) allOk = false;
                for (int k = 0; k < n; ++k) local[size_t(B.nodes[size_t(k)])] = -1;
            }
        }, 1);
        ok = allOk && !blocks.empty();
    }

    void solve(const Eigen::VectorXd& in, Eigen::VectorXd& out) const
    {
        out.resize(N);
        parallelRange(blocks.size(), [&](size_t b0, size_t b1) {
            Eigen::VectorXd rhs, y;
            for (size_t b = b0; b < b1; ++b)
            {
                const Block& B = blocks[b];
                const int n = int(B.nodes.size());
                rhs.resize(4 * n);
                for (int k = 0; k < n; ++k)
                    for (int c = 0; c < 4; ++c) rhs[4 * k + c] = in[long(B.nodes[size_t(k)]) * 4 + c];
                y = B.f->solve(rhs);
                for (int k = 0; k < B.own; ++k)
                    for (int c = 0; c < 4; ++c) out[long(B.nodes[size_t(k)]) * 4 + c] = y[4 * k + c];
            }
        }, 1);
    }
};

TetFlowProblem::~TetFlowProblem() = default;

namespace {

const double kBackflow = 1.0;           // the backflow term on the outlets (Bazilevs et al.)

// The rows of one node of a tetrahedron in the stabilised P1/P1 system: r[row component][4 * local
// column node + column component], and the right side b[row component]; the components 0..2 are the
// velocity, 3 the pressure.  The weak form (w, q the test functions; a the advection velocity of the
// last iterate, linear in the element, abar its mean):
//   (w, rho a.grad u) + mu (grad w, grad u) - (div w, p) + (q, div u)
//   + sum_e tau (a.grad w, R) + sum_e (tau / rho) (grad q, R) + sum_e tau_L rho (div w, div u)
// with the momentum residual R = rho a.grad u + alpha u + grad p - f (the viscous term's divergence
// is zero in a linear element), and Newton's linearisation rho (u.grad) a of the convection when
// asked for.  All integrals are exact (the shape functions are linear, the gradients constant).
struct ElemRows
{
    double r[4][16];
    double b[4];
};

void elementRows(const TetGeom& G, int li, double he, const Vec3 a[4], const double pk[4], bool convect, bool newton, double alpha,
                 const Vec3& f, double mu, double rho, double dtInv, const Vec3 uo[4], ElemRows& out)
{
    const double V = G.vol;
    Vec3 g[4];
    for (int k = 0; k < 4; ++k) g[k] = Vec3(G.g[k][0], G.g[k][1], G.g[k][2]);
    Vec3 abar = Vec3::Zero();
    if (convect) abar = 0.25 * (a[0] + a[1] + a[2] + a[3]);
    const double nu = mu / rho;
    const double speed = abar.norm();
    // (no time term in tau: with it the discrete steady state of a flow in time depended on the step -- +3 % on the
    // duct's pressure drop at dt 0.05 s -- and a flow that has settled must be the steady solver's flow)
    (void)dtInv;
    const double inv = std::sqrt(std::pow(2.0 * speed / he, 2) + std::pow(4.0 * nu / (he * he), 2) + std::pow(alpha / rho, 2));
    const double tau = inv > 0 ? 1.0 / inv : 0.0;
    double tauL = 0.0;
    if (convect && speed > 0)
    {
        const double re = speed * he / (2.0 * nu);
        tauL = 0.5 * he * speed * std::min(1.0, re / 3.0);
    }
    double gradA[3][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
    if (newton)
        for (int k = 0; k < 4; ++k)
            for (int b = 0; b < 3; ++b)
                for (int c = 0; c < 3; ++c) gradA[b][c] += a[k][b] * g[k][c];
    const Vec3& gi = g[li];
    const double agi = abar.dot(gi);
    // the linearisation of the stabilisation terms through abar (see above): J[a][c] on the momentum rows, Jc[c] on the
    // continuity row, the same for every column node
    double J[3][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}}, Jc[3] = {0, 0, 0};
    if (newton)
    {
        double conv[3], gradP[3] = {0, 0, 0};
        for (int b = 0; b < 3; ++b)
        {
            conv[b] = 0;
            for (int c = 0; c < 3; ++c) conv[b] += abar[c] * gradA[b][c];
        }
        for (int k = 0; k < 4; ++k)
            for (int c = 0; c < 3; ++c) gradP[c] += pk[k] * g[k][c];
        double sumDiff[3] = {0, 0, 0};          // (the time term's sum over the nodes of u - u_old)
        if (dtInv > 0)
            for (int k = 0; k < 4; ++k)
                for (int b = 0; b < 3; ++b) sumDiff[b] += a[k][b] - uo[k][b];
        for (int b = 0; b < 3; ++b)
            for (int c = 0; c < 3; ++c)
                J[b][c] = tau * rho * V * 0.25 * (gi[c] * conv[b] + agi * gradA[b][c]) + 0.25 * tau * V * gi[c] * (gradP[b] - f[b]) +
                          0.25 * tau * alpha * V * gi[c] * abar[b] + 0.25 * tau * rho * dtInv * (V / 4.0) * gi[c] * sumDiff[b];
        for (int c = 0; c < 3; ++c)
        {
            double sum = 0;
            for (int b = 0; b < 3; ++b) sum += gi[b] * gradA[b][c];
            Jc[c] = 0.25 * tau * V * sum;
        }
    }
    double ag[4];               // a_k . g_j summed with the mass weights: sum_k m_ik (a_k . g_j)
    for (int lj = 0; lj < 4; ++lj)
    {
        double s = 0;
        if (convect)
            for (int k = 0; k < 4; ++k) s += (V * (1.0 + (k == li ? 1.0 : 0.0)) / 20.0) * a[k].dot(g[lj]);
        ag[lj] = s;
    }
    for (int lj = 0; lj < 4; ++lj)
    {
        const Vec3& gj = g[lj];
        const double mij = V * (1.0 + (lj == li ? 1.0 : 0.0)) / 20.0;
        const double gg = gi.dot(gj);
        const double agj = abar.dot(gj);
        for (int ra = 0; ra < 3; ++ra)
        {
            for (int cb = 0; cb < 3; ++cb)
            {
                double v = 0;
                if (ra == cb)
                {
                    v += mu * V * gg + alpha * mij + rho * dtInv * mij;
                    if (convect) v += rho * ag[lj] + tau * rho * V * agi * agj + tau * alpha * agi * V / 4.0 + tau * rho * agi * dtInv * V / 4.0;
                }
                v += tauL * rho * V * gi[ra] * gj[cb];
                if (newton) v += rho * mij * gradA[ra][cb] + J[ra][cb];
                out.r[ra][4 * lj + cb] = v;
            }
            out.r[ra][4 * lj + 3] = -(V / 4.0) * gi[ra] + (convect ? tau * V * agi * gj[ra] : 0.0);
        }
        for (int cb = 0; cb < 3; ++cb)
            out.r[3][4 * lj + cb] = (V / 4.0) * gj[cb] + (convect ? tau * V * gi[cb] * agj : 0.0) +
                                    (tau * alpha / rho) * (V / 4.0) * gi[cb] + tau * dtInv * (V / 4.0) * gi[cb] + Jc[cb];
        out.r[3][4 * lj + 3] = (tau / rho) * V * gg;
    }
    for (int ra = 0; ra < 3; ++ra)
    {
        double b = (V / 4.0) * f[ra] + (convect ? tau * V * agi * f[ra] : 0.0);
        if (dtInv > 0)
            for (int lj = 0; lj < 4; ++lj)
            {
                const double mij = V * (1.0 + (lj == li ? 1.0 : 0.0)) / 20.0;
                b += rho * dtInv * mij * uo[lj][ra] + (convect ? tau * rho * agi * dtInv * (V / 4.0) * uo[lj][ra] : 0.0);
            }
        if (newton)
        {
            for (int lj = 0; lj < 4; ++lj)
            {
                const double mij = V * (1.0 + (lj == li ? 1.0 : 0.0)) / 20.0;
                for (int cb = 0; cb < 3; ++cb) b += rho * mij * gradA[ra][cb] * a[lj][cb];
            }
            for (int cb = 0; cb < 3; ++cb) b += 4.0 * abar[cb] * J[ra][cb];
        }
        out.b[ra] = b;
    }
    out.b[3] = (tau / rho) * V * gi.dot(f);
    if (dtInv > 0)
        for (int lj = 0; lj < 4; ++lj) out.b[3] += tau * dtInv * (V / 4.0) * gi.dot(uo[lj]);
    if (newton)
        for (int cb = 0; cb < 3; ++cb) out.b[3] += 4.0 * abar[cb] * Jc[cb];
}

double triangleArea(const Vec3& a, const Vec3& b, const Vec3& c) { return 0.5 * (b - a).cross(c - a).norm(); }

}   // anonymous namespace

TetFlowProblem::TetFlowProblem(const Tree& domain, Eigen::Vector3d lo, Eigen::Vector3d hi, double h, double density,
                               double viscosity)
    : m_shape(domain), m_lo(lo), m_hi(hi), m_h(h), m_rho(density), m_mu(viscosity)
{
}

void TetFlowProblem::addInlet(const Tree& region, Eigen::Vector3d direction, double speed, double flowRate, int profile)
{
    m_inlets.push_back({region, direction, speed, flowRate, profile});
    m_prepared = false;
}

void TetFlowProblem::addOutlet(const Tree& region, double pressure)
{
    m_outlets.push_back({region, pressure});
    m_prepared = false;
}

void TetFlowProblem::addWall(const Tree& region, Eigen::Vector3d velocity)
{
    m_walls.push_back({region, velocity});
    m_prepared = false;
}

void TetFlowProblem::addSlip(const Tree& region)
{
    m_slips.push_back(region);
    m_prepared = false;
}

void TetFlowProblem::setBodyForce(Eigen::Vector3d g)
{
    m_g = g;
    m_prepared = false;
}

////////////////////////////////////////////////////////////////////////////////
// The developed inlet profile: -laplace phi = 1 on the inlet's triangles, phi = 0 on its rim

bool TetFlowProblem::developedProfile(const std::vector<char>& inFace, const std::vector<char>& rim,
                                      std::vector<double>& phi, std::string& error) const
{
    const TetMesh& m = *m_mesh;
    const size_t nv = m.pos.size();
    std::vector<int> local(nv, -1);
    std::vector<int> nodes;
    for (size_t f = 0; f < m.faces.size(); ++f)
    {
        if (!inFace[f]) continue;
        for (int p = 0; p < 3; ++p)
        {
            const int n = m.faces[f][size_t(p)];
            if (local[size_t(n)] < 0 && !rim[size_t(n)])
            {
                local[size_t(n)] = int(nodes.size());
                nodes.push_back(n);
            }
        }
    }
    phi.assign(nv, 0.0);
    if (nodes.empty())
    {
        error = "an inlet has no node inside its rim: it is less than two elements wide; use a smaller element size";
        return false;
    }
    std::vector<Eigen::Triplet<double>> trip;
    Eigen::VectorXd rhs = Eigen::VectorXd::Zero(long(nodes.size()));
    for (size_t f = 0; f < m.faces.size(); ++f)
    {
        if (!inFace[f]) continue;
        const auto& tri = m.faces[f];
        const Vec3 &p0 = m.pos[size_t(tri[0])], &p1 = m.pos[size_t(tri[1])], &p2 = m.pos[size_t(tri[2])];
        const Vec3 nrm = (p1 - p0).cross(p2 - p0);
        const double A = 0.5 * nrm.norm();
        if (!(A > 0)) continue;
        const Vec3 n = nrm / (2.0 * A);
        const Vec3 gr[3] = {n.cross(p2 - p1) / (2.0 * A), n.cross(p0 - p2) / (2.0 * A), n.cross(p1 - p0) / (2.0 * A)};
        for (int i = 0; i < 3; ++i)
        {
            const int li = local[size_t(tri[size_t(i)])];
            if (li < 0) continue;
            rhs[li] += A / 3.0;
            for (int j = 0; j < 3; ++j)
            {
                const int lj = local[size_t(tri[size_t(j)])];
                if (lj < 0) continue;
                trip.emplace_back(li, lj, A * gr[i].dot(gr[j]));
            }
        }
    }
    Eigen::SparseMatrix<double> K(long(nodes.size()), long(nodes.size()));
    K.setFromTriplets(trip.begin(), trip.end());
    Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>> ldlt(K);
    if (ldlt.info() != Eigen::Success)
    {
        error = "the developed inlet profile could not be computed (the inlet's triangles don't make one patch)";
        return false;
    }
    const Eigen::VectorXd x = ldlt.solve(rhs);
    for (size_t k = 0; k < nodes.size(); ++k) phi[size_t(nodes[k])] = std::max(0.0, x[long(k)]);
    return true;
}

////////////////////////////////////////////////////////////////////////////////
// Preparing: the mesh and the boundary conditions on it

bool TetFlowProblem::prepare(std::string& error)
{
    m_ilut.reset();
    m_ilutIter = -1;
    m_warning.clear();
    if (!(m_h > 0) || !(m_rho > 0) || !(m_mu > 0))
    {
        error = "invalid element size or fluid (the density and the viscosity must be positive)";
        return false;
    }
    if (m_inlets.empty() && m_walls.empty())
    {
        error = "nothing drives the flow: add an inlet(...) or a moving wall(...)";
        return false;
    }
    run_progress::Task task("meshing");
    task.set(0.0, "meshing the fluid");
    auto mesh = std::make_shared<TetMesh>();
    if (!meshShape(m_shape, m_lo, m_hi, m_h, *mesh, error)) return false;
    if (mesh->faces.empty())
    {
        error = "the fluid domain has no surface";
        return false;
    }
    task.set(0.6, "resolving the boundary conditions");
    m_mesh = mesh;
    m_locator.reset();
    if (!resolveConditions(error)) return false;
    m_prepared = true;
    return true;
}

bool TetFlowProblem::resolveConditions(std::string& error)
{
    const TetMesh& m = *m_mesh;
    const size_t nv = m.pos.size(), nf = m.faces.size(), nt = m.tets.size();
    const float reach = float(0.05 * m_h);

    // Faces: area, normal (out of the fluid), centre
    m_faceArea.assign(nf, 0.0);
    m_faceNormal.assign(nf, Vec3::Zero());
    std::vector<Eigen::Vector3f> centres(nf);
    for (size_t f = 0; f < nf; ++f)
    {
        const auto& tri = m.faces[f];
        const Vec3 &a = m.pos[size_t(tri[0])], &b = m.pos[size_t(tri[1])], &c = m.pos[size_t(tri[2])];
        const Vec3 nrm = (b - a).cross(c - a);
        m_faceArea[f] = 0.5 * nrm.norm();
        m_faceNormal[f] = m_faceArea[f] > 0 ? Vec3(nrm / (2.0 * m_faceArea[f])) : Vec3::Zero();
        centres[f] = ((a + b + c) / 3.0).cast<float>();
    }
    std::vector<Eigen::Vector3f> pts(nv);
    for (size_t i = 0; i < nv; ++i) pts[i] = m.pos[i].cast<float>();

    // Each node's boundary faces
    m_vfStart.assign(nv + 1, 0);
    for (size_t f = 0; f < nf; ++f)
        for (int p = 0; p < 3; ++p) m_vfStart[size_t(m.faces[f][size_t(p)]) + 1]++;
    for (size_t i = 0; i < nv; ++i) m_vfStart[i + 1] += m_vfStart[i];
    m_vfList.assign(3 * nf, 0);
    {
        std::vector<uint32_t> cursor(m_vfStart.begin(), m_vfStart.end() - 1);
        for (size_t f = 0; f < nf; ++f)
            for (int p = 0; p < 3; ++p) m_vfList[cursor[size_t(m.faces[f][size_t(p)])]++] = uint32_t(f);
    }

    // Every region at every boundary node: a node within a small fraction of an element of a region counts as
    // inside it (a region that misses the surface by a little still reaches it: within half an element)
    struct Item { int kind; int index; const Tree* region; };
    std::vector<Item> items;
    for (size_t k = 0; k < m_slips.size(); ++k) items.push_back({3, int(k), &m_slips[k]});
    for (size_t k = 0; k < m_outlets.size(); ++k) items.push_back({1, int(k), &m_outlets[k].region});
    for (size_t k = 0; k < m_inlets.size(); ++k) items.push_back({0, int(k), &m_inlets[k].region});
    for (size_t k = 0; k < m_walls.size(); ++k) items.push_back({2, int(k), &m_walls[k].region});
    const size_t ni = items.size();
    auto itemName = [&](const Item& it) {
        static const char* const names[] = {"inlet", "outlet", "wall", "slip / symmetry region"};
        return std::string(names[it.kind]) + " " + std::to_string(it.index + 1);
    };
    std::vector<std::vector<char>> inside(ni);
    for (size_t q = 0; q < ni; ++q)
    {
        std::vector<float> vn;
        evalTreePoints(*items[q].region, pts, vn);
        std::vector<char>& in = inside[q];
        in.assign(nv, 0);
        for (int pass = 0; pass < 2; ++pass)
        {
            const float rch = pass == 0 ? reach : 10 * reach;
            size_t n = 0;
            for (size_t i = 0; i < nv; ++i)
            {
                in[i] = m_vfStart[i + 1] > m_vfStart[i] && vn[i] <= rch;
                n += in[i];
            }
            if (n > 0) break;
        }
    }

    // The triangles a region holds: those with all three corners inside it, else those whose centre is;
    // later items win over earlier ones (slip, outlets, inlets, then explicit walls)
    std::vector<int> faceOwner(nf, -1);
    for (size_t q = 0; q < ni; ++q)
    {
        const std::vector<char>& in = inside[q];
        size_t n = 0;
        for (size_t f = 0; f < nf; ++f)
        {
            const auto& tri = m.faces[f];
            if (in[size_t(tri[0])] && in[size_t(tri[1])] && in[size_t(tri[2])])
            {
                faceOwner[f] = int(q);
                ++n;
            }
        }
        if (n == 0)
        {
            std::vector<float> vc;
            evalTreePoints(*items[q].region, centres, vc);
            for (size_t f = 0; f < nf; ++f)
                if (vc[f] <= 10 * reach)
                {
                    faceOwner[f] = int(q);
                    ++n;
                }
        }
        if (n == 0)
        {
            error = itemName(items[q]) + " doesn't touch the fluid's surface";
            return false;
        }
    }
    // The mesh rounds a sharp edge of the domain within an element: a triangle there lies in neither of
    // the surfaces that meet at the edge, so no region holds all of its corners.  One with a corner inside
    // a region and a corner outside it that faces about the way the region's own triangles AT ITS CORNERS
    // face (within 75 degrees) and that leaves their plane (a corner off it by a tenth of the triangle's
    // size) is such an edge triangle, and belongs to the region (it would otherwise be a wall, and hold a
    // whole row of the inlet's nodes at rest).  A triangle lying in the region's own plane with a corner
    // outside is not: it is the flat wall beside a port in the middle of a face, and taking it widened every
    // such port by an element all round -- the inlet's profile then pushed fluid into the solid beside the
    // port (a 4 mm port read 5 mm wide: the flow optimiser's objective was dominated by that friction, and
    // its channels flared at the ports).  (The facing alone cannot tell them apart: the rounded edge's
    // triangles face as little as 10 degrees off the region.)  The comparison is with the region's
    // triangles next to the edge, not with a mean over the whole region: a region of two opposite planes
    // (both faces of a slab, both its sides) has no mean normal, and comparing with it adopted the edge
    // triangles on one side of the slab and not on the other, which was then a wall at rest along its
    // whole length.
    for (size_t f = 0; f < nf; ++f)
    {
        if (faceOwner[f] >= 0) continue;
        const auto& tri = m.faces[f];
        for (size_t q = 0; q < ni; ++q)
        {
            int cin = 0;
            for (int p = 0; p < 3; ++p) cin += inside[q][size_t(tri[size_t(p)])] != 0;
            if (cin == 0 || cin == 3) continue;
            bool adopt = false;
            for (int p = 0; p < 3 && !adopt; ++p)
            {
                const size_t v = size_t(tri[size_t(p)]);
                for (uint32_t e = m_vfStart[v]; e < m_vfStart[v + 1] && !adopt; ++e)
                {
                    const size_t g = m_vfList[e];
                    if (g == f || faceOwner[g] != int(q)) continue;
                    if (!(m_faceNormal[f].dot(m_faceNormal[g]) > 0.25)) continue;
                    const auto& tg = m.faces[g];
                    const Vec3 c = (m.pos[size_t(tg[0])] + m.pos[size_t(tg[1])] + m.pos[size_t(tg[2])]) / 3.0;
                    double off = 0;
                    for (int r = 0; r < 3; ++r)
                        off = std::max(off, std::abs((m.pos[size_t(tri[size_t(r)])] - c).dot(m_faceNormal[g])));
                    if (off > 0.1 * std::sqrt(std::max(m_faceArea[f], 1e-300))) adopt = true;
                }
            }
            if (adopt) faceOwner[f] = int(q);
        }
    }
    m_faceKind.assign(nf, -1);
    m_faceItem.assign(nf, -1);
    for (size_t f = 0; f < nf; ++f)
        if (faceOwner[f] >= 0)
        {
            m_faceKind[f] = items[size_t(faceOwner[f])].kind;
            m_faceItem[f] = items[size_t(faceOwner[f])].index;
        }

    // The velocity at a node, from the triangles at it: an explicit wall first, then a wall at rest (a
    // triangle in no region), then an inlet, then slip (the normal component only); an outlet node, like
    // an inner one, is free.  (A triangle that imposes a velocity imposes it on all its nodes: an edge
    // triangle of an inlet that reaches a slip plane holds that node too, else the inlet would leak back
    // out through it.)
    m_fixed.assign(4 * nv, 0);
    m_fixedValue.assign(4 * nv, 0.0);
    m_slipNormal.assign(nv, Vec3::Zero());
    m_slipNormal2.assign(nv, Vec3::Zero());
    std::vector<int> nodeKind(nv, 9);           // 9 inner / free, 3 slip, 0 inlet, -1 wall at rest, 2 explicit wall
    std::vector<int> nodeItem(nv, -1);
    std::vector<Vec3> slipNormal(nv, Vec3::Zero());
    auto rank = [](int kind) { return kind == 2 ? 0 : kind == -1 ? 1 : kind == 0 ? 2 : kind == 3 ? 3 : 4; };
    for (size_t i = 0; i < nv; ++i)
    {
        if (m_vfStart[i + 1] == m_vfStart[i]) continue;
        int best = 9, item = -1;
        for (uint32_t e = m_vfStart[i]; e < m_vfStart[i + 1]; ++e)
        {
            const size_t f = m_vfList[e];
            const int kind = m_faceKind[f];
            if (kind == 3) slipNormal[i] += m_faceArea[f] * m_faceNormal[f];
            if (kind == 1) continue;
            if (rank(kind) < rank(best) || (kind == best && kind == 2 && m_faceItem[f] > item))
            {
                best = kind;
                item = m_faceItem[f];                       // (the last explicit wall given wins)
            }
        }
        nodeKind[i] = best;
        nodeItem[i] = item;
    }
    m_nodeKind = nodeKind;
    m_nodeItem = nodeItem;
    for (size_t i = 0; i < nv; ++i)
    {
        if (nodeKind[i] == -1 || nodeKind[i] == 2)
        {
            const Vec3 v = nodeKind[i] == 2 ? m_walls[size_t(nodeItem[i])].velocity : Vec3::Zero();
            for (int a = 0; a < 3; ++a)
            {
                m_fixed[4 * i + size_t(a)] = 1;
                m_fixedValue[4 * i + size_t(a)] = v[a];
            }
        }
        else if (nodeKind[i] == 3)
        {
            // The slip planes at the node: nothing flows through any of them.  On one plane its normal (the
            // area-weighted mean of the triangles' normals: a smooth surface); at the rounded edge the mesher
            // makes between two planes, the second plane's normal too, so the node moves along the edge only
            // (one averaged normal let 2.4 % of the flow leak through the edges of a slab two elements thick);
            // at a corner of three the node is held.  A direction counts as new when a third of it is not in
            // the span of those before it.
            Vec3 ns[3];
            int cnt = 0;
            if (slipNormal[i].norm() > 0) ns[cnt++] = slipNormal[i].normalized();
            for (uint32_t e = m_vfStart[i]; e < m_vfStart[i + 1] && cnt < 3; ++e)
            {
                const size_t f = m_vfList[e];
                if (m_faceKind[f] != 3) continue;
                Vec3 r = m_faceNormal[f];
                for (int k = 0; k < cnt; ++k) r -= r.dot(ns[k]) * ns[k];
                if (r.norm() > 0.35) ns[cnt++] = r.normalized();
            }
            if (cnt >= 3)
            {
                for (int a = 0; a < 3; ++a)
                {
                    m_fixed[4 * i + size_t(a)] = 1;
                    m_fixedValue[4 * i + size_t(a)] = 0.0;
                }
                continue;
            }
            for (int k = 0; k < cnt; ++k)
            {
                const Vec3& n = ns[k];
                int axis = -1;
                for (int a = 0; a < 3; ++a) if (std::abs(n[a]) > 0.9998) axis = a;
                if (axis >= 0)
                {
                    m_fixed[4 * i + size_t(axis)] = 1;
                    m_fixedValue[4 * i + size_t(axis)] = 0.0;
                }
                else if (m_slipNormal[i].norm() == 0)
                    m_slipNormal[i] = n;                    // (a penalty on the normal component)
                else
                    m_slipNormal2[i] = n;
            }
        }
    }

    // Inlets: the profile and the velocity scaled to the speed or flow rate on the discrete flux
    m_inletArea = 0;
    m_inletPerimeter = 0;
    m_refSpeed = 0;
    m_inletDir.clear();
    std::vector<char> rim(nv, 0);
    for (size_t i = 0; i < nv; ++i) rim[i] = nodeKind[i] == -1 || nodeKind[i] == 2;
    for (size_t k = 0; k < m_inlets.size(); ++k)
    {
        const Inlet& in = m_inlets[k];
        std::vector<char> inFace(nf, 0);
        double area = 0;
        Vec3 nbar = Vec3::Zero();
        for (size_t f = 0; f < nf; ++f)
            if (m_faceKind[f] == 0 && m_faceItem[f] == int(k))
            {
                inFace[f] = 1;
                area += m_faceArea[f];
                nbar += m_faceArea[f] * m_faceNormal[f];
            }
        if (!(area > 0) || !(nbar.norm() > 0))
        {
            error = "inlet " + std::to_string(k + 1) + " has no area";
            return false;
        }
        Vec3 dir = in.direction;
        if (dir.norm() > 0) dir.normalize();
        else dir = -nbar.normalized();
        // The cross-section the inlet presents to the flow: the patch's area projected along the flow direction.
        // (The patch holds the rounded-edge triangles between the inlet's face and the surfaces around it, at
        // 45 degrees or so: their full area inflated the inlet by 19 % on a slab two elements thick, and a given
        // speed then pushed 19 % too much flow in.  Projected, the rounded edge replaces the sharp corner exactly.)
        double areaProj = 0;
        for (size_t f = 0; f < nf; ++f)
            if (inFace[f]) areaProj += m_faceArea[f] * std::abs(dir.dot(m_faceNormal[f]));
        area = std::max(areaProj, 1e-300);
        // its rim: the edges of its triangles that are not shared with another of its triangles
        {
            std::map<std::pair<int, int>, int> edges;
            for (size_t f = 0; f < nf; ++f)
            {
                if (!inFace[f]) continue;
                for (int p = 0; p < 3; ++p)
                {
                    int a = m.faces[f][size_t(p)], b = m.faces[f][size_t((p + 1) % 3)];
                    if (a > b) std::swap(a, b);
                    edges[{a, b}]++;
                }
            }
            for (const auto& e : edges)
                if (e.second == 1) m_inletPerimeter += (m.pos[size_t(e.first.first)] - m.pos[size_t(e.first.second)]).norm();
        }
        std::vector<double> phi(nv, 0.0);
        if (in.profile == DEVELOPED)
        {
            if (!developedProfile(inFace, rim, phi, error)) return false;
        }
        else
            for (size_t i = 0; i < nv; ++i) phi[i] = (nodeKind[i] == 0 && nodeItem[i] == int(k) && !rim[i]) ? 1.0 : 0.0;
        // the discrete flux of the unit profile into the domain, and of the rim's own velocity (a moving wall)
        double q1 = 0, qRim = 0;
        for (size_t f = 0; f < nf; ++f)
        {
            if (!inFace[f]) continue;
            double s = 0;
            for (int p = 0; p < 3; ++p)
            {
                const size_t n = size_t(m.faces[f][size_t(p)]);
                s += phi[n];
                if (rim[n])
                    qRim -= (m_faceArea[f] / 3.0) * Vec3(m_fixedValue[4 * n], m_fixedValue[4 * n + 1], m_fixedValue[4 * n + 2]).dot(m_faceNormal[f]);
            }
            q1 -= (m_faceArea[f] / 3.0) * s * dir.dot(m_faceNormal[f]);
        }
        if (!(q1 > 0))
        {
            bool any = false;
            for (double v : phi) any = any || v > 0;
            error = !any ? "inlet " + std::to_string(k + 1) + " has no node of its own (it is less than two elements wide): use a smaller element size"
                         : "inlet " + std::to_string(k + 1) + ": the direction points out of the fluid";
            return false;
        }
        const double speed = in.flowRate > 0 ? in.flowRate / area : (in.speed > 0 ? in.speed : in.direction.norm());
        if (!(speed > 0))
        {
            error = "inlet " + std::to_string(k + 1) + " has no speed: give velocity=, speed= or flow_rate=";
            return false;
        }
        const double scale = (speed * area - qRim) / q1;
        if (!(scale > 0))
        {
            error = "inlet " + std::to_string(k + 1) + ": the walls at its rim already carry more than its flow";
            return false;
        }
        for (size_t i = 0; i < nv; ++i)
        {
            if (!(nodeKind[i] == 0 && nodeItem[i] == int(k))) continue;
            for (int a = 0; a < 3; ++a)
            {
                m_fixed[4 * i + size_t(a)] = 1;
                m_fixedValue[4 * i + size_t(a)] = scale * phi[i] * dir[a];
            }
        }
        m_inletArea += area;
        m_inletDir.push_back(dir);
        m_refSpeed = std::max(m_refSpeed, speed);
    }
    for (const auto& w : m_walls) m_refSpeed = std::max(m_refSpeed, w.velocity.norm());

    // A closed domain (no outlet): the pressure is relative, zero at one node
    if (m_outlets.empty())
    {
        m_fixed[3] = 1;
        m_fixedValue[3] = 0.0;
    }

    // The domain's volume and wall area (the hydraulic diameter, and how many elements lie across it)
    m_volume = 0;
    for (size_t t = 0; t < nt; ++t)
    {
        const auto& tv = m.tets[t];
        m_volume += std::abs((m.pos[size_t(tv[1])] - m.pos[size_t(tv[0])]).cross(m.pos[size_t(tv[2])] - m.pos[size_t(tv[0])])
                                 .dot(m.pos[size_t(tv[3])] - m.pos[size_t(tv[0])])) / 6.0;
    }
    m_wallArea = 0;
    for (size_t f = 0; f < nf; ++f) if (m_faceKind[f] != 0 && m_faceKind[f] != 1) m_wallArea += m_faceArea[f];

    uint64_t hsh = 1469598103934665603ull;
    const double hdr[] = {m_h, m_rho, m_mu, m_g.x(), m_g.y(), m_g.z(), double(nv), double(nt),
                          double(m_opt.stokes), double(m_opt.nonlinearIterations), m_opt.relaxation,
                          double(m_opt.directLimit), double(m_opt.linearIterations), m_opt.linearTolerance,
                          double(m_opt.newton)};
    hsh = fnv(hsh, hdr, sizeof(hdr));
    hsh = fnv(hsh, m.pos.data(), m.pos.size() * sizeof(Vec3));
    hsh = fnv(hsh, m.tets.data(), m.tets.size() * sizeof(std::array<int, 4>));
    hsh = fnv(hsh, m_fixed.data(), m_fixed.size());
    hsh = fnv(hsh, m_fixedValue.data(), m_fixedValue.size() * sizeof(double));
    hsh = fnv(hsh, m_faceKind.data(), m_faceKind.size() * sizeof(int));
    hsh = fnv(hsh, m_faceItem.data(), m_faceItem.size() * sizeof(int));
    for (const auto& o : m_outlets) hsh = fnv(hsh, &o.pressure, sizeof(double));
    m_hash = hsh;
    return true;
}

////////////////////////////////////////////////////////////////////////////////
// Solving

bool TetFlowProblem::solve(double tolerance, std::string& error, const std::atomic<bool>* cancel)
{
    if (!m_prepared && !prepare(error)) return false;
    const auto t0 = std::chrono::steady_clock::now();
    const TetMesh& mesh = *m_mesh;
    const size_t nv = mesh.pos.size(), nt = mesh.tets.size(), nf = mesh.faces.size();
    const size_t N = 4 * nv;
    const bool debug = std::getenv("FIELDES_FLOW_DEBUG") != nullptr;
    // (a first estimate of the Reynolds number, for the messages)
    m_stats.reynolds = m_rho * m_refSpeed * (m_inletPerimeter > 0 ? 4.0 * m_inletArea / m_inletPerimeter : (m_hi - m_lo).maxCoeff()) / m_mu;
    std::unique_ptr<run_progress::Task> taskPtr;
    if (!m_quiet) taskPtr.reset(new run_progress::Task("solving the flow"));
    auto taskSet = [&](double f, const char* text) { if (taskPtr) taskPtr->set(f, text); };
    taskSet(0.0, "building the equations");

    // The pattern: 4 x 4 blocks on the vertex adjacency
    Assembly A;
    buildPattern(mesh, A);
    const size_t nb = A.K.col.size();
    std::vector<int> outer(N + 1), inner(16 * nb);
    std::vector<double> val(16 * nb, 0.0), rhs(N, 0.0);
    for (size_t i = 0; i < nv; ++i)
    {
        const size_t rowLen = 4 * size_t(A.K.rowPtr[i + 1] - A.K.rowPtr[i]);
        for (int r = 0; r < 4; ++r)
        {
            const size_t row = 4 * i + size_t(r);
            outer[row] = int(16 * size_t(A.K.rowPtr[i]) + size_t(r) * rowLen);
            size_t pos = size_t(outer[row]);
            for (uint32_t e = A.K.rowPtr[i]; e < A.K.rowPtr[i + 1]; ++e)
                for (int c = 0; c < 4; ++c) inner[pos++] = int(4 * size_t(A.K.col[e]) + size_t(c));
        }
    }
    outer[N] = int(16 * nb);
    std::vector<double> he(nt);
    for (size_t t = 0; t < nt; ++t) he[t] = std::cbrt(6.0 * std::sqrt(2.0) * A.geom[t].vol);
    const Vec3 f = m_rho * m_g;
    const bool friction = m_alpha.size() == nt;
    const bool stepping = m_dtInv > 0 && m_uOld.size() == 3 * nv;
    const double penalty = 1e5 * m_mu * m_h;
    std::vector<double> u(3 * nv, 0.0), p(nv, 0.0);
    const bool warm = m_u.size() == 3 * nv && m_p.size() == nv;
    if (warm)
    {
        // (a warm start from the last solution: the optimiser solves the same mesh again and again)
        u = m_u;
        p = m_p;
    }
    else
        m_residualScale = 0;
    for (size_t i = 0; i < nv; ++i)
        for (int a = 0; a < 3; ++a) if (m_fixed[4 * i + size_t(a)]) u[3 * i + size_t(a)] = m_fixedValue[4 * i + size_t(a)];

    // Assembles the system for the advection velocity `adv` (the current iterate)
    std::atomic<long> backflowFaces(0);
    bool keepHeldRows = false;              // (the reactions: the equations of the held nodes, not the identity)
    auto assemble = [&](const std::vector<double>& adv, bool convect, bool newton) {
        backflowFaces.store(0);
        parallelRange(nv, [&](size_t b0, size_t b1) {
            ElemRows er;
            for (size_t i = b0; i < b1; ++i)
            {
                const uint32_t r0 = A.K.rowPtr[i], r1 = A.K.rowPtr[i + 1];
                const size_t rowLen = 4 * size_t(r1 - r0);
                for (int r = 0; r < 4; ++r)
                {
                    const size_t row = 4 * i + size_t(r);
                    std::fill(val.begin() + outer[row], val.begin() + outer[row] + std::ptrdiff_t(rowLen), 0.0);
                    rhs[row] = 0.0;
                }
                auto slot = [&](uint32_t j) {
                    return size_t(std::lower_bound(A.K.col.begin() + r0, A.K.col.begin() + r1, j) - (A.K.col.begin() + r0));
                };
                for (uint32_t e = A.vtStart[i]; e < A.vtStart[i + 1]; ++e)
                {
                    const size_t t = A.vtList[e];
                    const auto& tv = mesh.tets[t];
                    int li = 0;
                    Vec3 a[4], uo[4];
                    double pk[4];
                    for (int q = 0; q < 4; ++q)
                    {
                        if (size_t(tv[size_t(q)]) == i) li = q;
                        const size_t n = size_t(tv[size_t(q)]);
                        a[q] = Vec3(adv[3 * n], adv[3 * n + 1], adv[3 * n + 2]);
                        uo[q] = stepping ? Vec3(m_uOld[3 * n], m_uOld[3 * n + 1], m_uOld[3 * n + 2]) : Vec3::Zero();
                        pk[q] = p[n];
                    }
                    elementRows(A.geom[t], li, he[t], a, pk, convect, newton, friction ? m_alpha[t] : 0.0, f, m_mu, m_rho,
                                stepping ? m_dtInv : 0.0, uo, er);
                    for (int lj = 0; lj < 4; ++lj)
                    {
                        const size_t s = slot(uint32_t(tv[size_t(lj)]));
                        for (int r = 0; r < 4; ++r)
                        {
                            double* dst = &val[size_t(outer[4 * i + size_t(r)]) + 4 * s];
                            for (int c = 0; c < 4; ++c) dst[c] += er.r[r][4 * lj + c];
                        }
                    }
                    for (int r = 0; r < 4; ++r) rhs[4 * i + size_t(r)] += er.b[r];
                }
                // boundary faces at this node: the outlet pressure, the backflow term
                for (uint32_t e = m_vfStart[i]; e < m_vfStart[i + 1]; ++e)
                {
                    const size_t fc = m_vfList[e];
                    if (m_faceKind[fc] != 1) continue;
                    const double area = m_faceArea[fc];
                    const Vec3& n = m_faceNormal[fc];
                    const double pout = m_outlets[size_t(m_faceItem[fc])].pressure;
                    for (int ra = 0; ra < 3; ++ra) rhs[4 * i + size_t(ra)] += -pout * (area / 3.0) * n[ra];
                    if (convect)
                    {
                        Vec3 ubar = Vec3::Zero();
                        for (int q = 0; q < 3; ++q)
                        {
                            const size_t nn = size_t(mesh.faces[fc][size_t(q)]);
                            ubar += Vec3(adv[3 * nn], adv[3 * nn + 1], adv[3 * nn + 2]);
                        }
                        ubar /= 3.0;
                        const double un = ubar.dot(n);
                        if (un < 0) backflowFaces.fetch_add(1, std::memory_order_relaxed);
                        if (un < 0)
                            for (int q = 0; q < 3; ++q)
                            {
                                const uint32_t j = uint32_t(mesh.faces[fc][size_t(q)]);
                                const size_t s = slot(j);
                                const double w = kBackflow * m_rho * (-un) * area * (j == i ? 2.0 : 1.0) / 12.0;
                                for (int ra = 0; ra < 3; ++ra) val[size_t(outer[4 * i + size_t(ra)]) + 4 * s + size_t(ra)] += w;
                            }
                    }
                }
                // a slip node whose normal is not along an axis: a penalty on the normal component (two at the
                // rounded edge between two slip planes)
                for (const Vec3* np : {&m_slipNormal[i], &m_slipNormal2[i]})
                {
                    if (!(np->norm() > 0)) continue;
                    const Vec3& n = *np;
                    const size_t s = slot(uint32_t(i));
                    for (int ra = 0; ra < 3; ++ra)
                        for (int cb = 0; cb < 3; ++cb) val[size_t(outer[4 * i + size_t(ra)]) + 4 * s + size_t(cb)] += penalty * n[ra] * n[cb];
                }
                // held unknowns: the row is the identity
                for (int r = 0; r < 4 && !keepHeldRows; ++r)
                {
                    const size_t row = 4 * i + size_t(r);
                    if (!m_fixed[row]) continue;
                    std::fill(val.begin() + outer[row], val.begin() + outer[row] + std::ptrdiff_t(rowLen), 0.0);
                    const size_t s = slot(uint32_t(i));
                    val[size_t(outer[row]) + 4 * s + size_t(r)] = 1.0;
                    rhs[row] = m_fixedValue[row];
                }
            }
        }, 16);
    };
    auto matvec = [&](const std::vector<double>& x, std::vector<double>& y) {
        y.resize(N);
        parallelRange(N, [&](size_t b0, size_t b1) {
            for (size_t r = b0; r < b1; ++r)
            {
                double s = 0;
                for (int e = outer[r]; e < outer[r + 1]; ++e) s += val[size_t(e)] * x[size_t(inner[size_t(e)])];
                y[r] = s;
            }
        }, 1024);
    };
    auto pack = [&](std::vector<double>& x) {
        x.resize(N);
        for (size_t i = 0; i < nv; ++i)
        {
            for (int a = 0; a < 3; ++a) x[4 * i + size_t(a)] = u[3 * i + size_t(a)];
            x[4 * i + 3] = p[i];
        }
    };
    auto unpack = [&](const std::vector<double>& x) {
        for (size_t i = 0; i < nv; ++i)
        {
            for (int a = 0; a < 3; ++a) u[3 * i + size_t(a)] = x[4 * i + size_t(a)];
            p[i] = x[4 * i + 3];
        }
    };
    auto freeNorm = [&](const std::vector<double>& x) {
        double s = 0;
        for (size_t r = 0; r < N; ++r) if (!m_fixed[r]) s += x[r] * x[r];
        return std::sqrt(s);
    };

    // The linear solver: direct when the system is small enough, else BiCGSTAB with an incomplete LU
    using SpRow = Eigen::SparseMatrix<double, Eigen::RowMajor, int>;
    using SpCol = Eigen::SparseMatrix<double, Eigen::ColMajor, int>;
    int directLimit = m_opt.directLimit;
    if (const char* e = std::getenv("FIELDES_FLOW_DIRECT_LIMIT")) directLimit = std::atoi(e);      // (for the developer)
    const bool direct = N <= size_t(std::max(1000, directLimit));
    Eigen::SparseLU<SpCol, Eigen::COLAMDOrdering<int>> lu;
    bool analysed = false;
    bool usedDirect = false, usedIterative = false;
    std::vector<double> dsc(N, 1.0), val2(16 * nb), rhs2(N), x2(N);
    // (the relative residual the nonlinear iteration is at, so the linear solve need not go far below it)
    auto linearSolve = [&](std::vector<double>& x, double currentRel, std::string& err) {
        // The entries of the flow equations span many orders of magnitude (mu is 1e-9 MPa s for water, the pressure
        // stabilisation goes like h^3 / mu): the system is scaled symmetrically by its diagonal, D A D y = D b, x = D y,
        // D = 1 / sqrt(|a_ii|), which brings every diagonal to one and the solvers to convergence in far fewer steps
        for (size_t r = 0; r < N; ++r)
        {
            double d = 0;
            for (int e = outer[r]; e < outer[r + 1]; ++e) if (size_t(inner[size_t(e)]) == r) d = std::abs(val[size_t(e)]);
            dsc[r] = d > 1e-300 ? 1.0 / std::sqrt(d) : 1.0;
        }
        parallelRange(N, [&](size_t b0, size_t b1) {
            for (size_t r = b0; r < b1; ++r)
            {
                for (int e = outer[r]; e < outer[r + 1]; ++e) val2[size_t(e)] = dsc[r] * val[size_t(e)] * dsc[size_t(inner[size_t(e)])];
                rhs2[r] = dsc[r] * rhs[r];
                x2[r] = x[r] / dsc[r];
            }
        }, 1024);
        Eigen::Map<const SpRow> Ar(long(N), long(N), long(16 * nb), outer.data(), inner.data(), val2.data());
        Eigen::Map<const Eigen::VectorXd> b(rhs2.data(), long(N));
        Eigen::Map<const Eigen::VectorXd> x0(x2.data(), long(N));
        auto unscale = [&](const Eigen::VectorXd& sol) {
            x.resize(N);
            for (size_t r = 0; r < N; ++r) x[r] = dsc[r] * sol[long(r)];
        };
        const auto tl0 = std::chrono::steady_clock::now();
        bool iterativeFailed = false;
        if (!direct)
        {
            // The incomplete LU of the scaled system (the fill and the drop tolerance balance its cost against the
            // iterations it saves; FIELDES_FLOW_ILUT_FILL / _DROP override them for the developer), and BiCGSTAB on
            // the correction d = x - x0 of the current iterate, whose right side is the residual to reduce.  The
            // reduction asked: a nonlinear step a tenth of its own residual (1e-2 at most), a linear solve down to
            // linearTolerance of the right side's size -- and never below that absolute accuracy, whatever the warm
            // start's residual already is.
            static const int fill = std::getenv("FIELDES_FLOW_ILUT_FILL") ? std::atoi(std::getenv("FIELDES_FLOW_ILUT_FILL")) : 10;
            static const double drop = std::getenv("FIELDES_FLOW_ILUT_DROP") ? std::atof(std::getenv("FIELDES_FLOW_ILUT_DROP")) : 1e-2;
            // The incomplete LU is kept from one solve to the next while it still works: built again when the last
            // solve with it took more than twice the iterations of the first solve after building it (80 at least),
            // or failed
            auto buildIlut = [&]() {
                m_ilut.reset(new FlowIlut());
                const auto tb0 = std::chrono::steady_clock::now();
                m_ilut->build(outer, inner, val2, long(N), mesh.pos, m_h, fill, drop);
                m_ilutBuildSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - tb0).count();
                m_ilutN = N;
                m_ilutBase = 0;
                m_ilutIter = -1;
            };
            auto matvec2 = [&](const Eigen::VectorXd& in, Eigen::VectorXd& out) {
                out.resize(long(N));
                parallelRange(N, [&](size_t b0, size_t b1) {
                    for (size_t r = b0; r < b1; ++r)
                    {
                        double sum = 0;
                        for (int e = outer[r]; e < outer[r + 1]; ++e) sum += val2[size_t(e)] * in[long(inner[size_t(e)])];
                        out[long(r)] = sum;
                    }
                }, 1024);
            };
            Eigen::VectorXd r0;
            matvec2(x0, r0);
            r0 = b - r0;
            const double r0n = r0.norm();
            // (the absolute scale of the residual: the first solve's, from the start that holds only the given velocities)
            if (!(m_residualScale > 0)) m_residualScale = r0n;
            double tol = currentRel > 0 ? std::min(1e-2, 0.1 * currentRel) : m_opt.linearTolerance;
            tol = std::max(tol, m_opt.linearTolerance * m_residualScale / std::max(r0n, 1e-300));
            if (!(r0n > 0) || tol >= 1.0)
            {
                if (debug) fprintf(stderr, "[tetflow]   the warm start is already within the accuracy asked (residual %.3e of %.3e)\n", r0n, m_residualScale);
                unscale(x0);
                usedIterative = true;
                return true;
            }
            // (kept while the iterations it needed last time cost less than two builds: a fresh factorisation of the
            // matrix at hand needs fewer iterations, so past that point building again is the cheaper path)
            bool reused = m_ilut && m_ilutN == N && m_ilutIter >= 0 &&
                          (m_ilutIter <= 40 || double(m_ilutIter) * m_ilutIterSeconds <= 2.0 * m_ilutBuildSeconds);
            if (!reused) buildIlut();
            const auto tl1 = std::chrono::steady_clock::now();
            bool ok = false;
            long iterations = 0;
            double rn = r0n;
            Eigen::VectorXd d;
            const double target = tol * r0n;
            auto run = [&]() {
                ok = false;
                iterations = 0;
                rn = r0n;
                d = Eigen::VectorXd::Zero(long(N));
                if (!m_ilut->ok) return;
                // BiCGSTAB (van der Vorst), right-preconditioned by the incomplete LU
                Eigen::VectorXd r = r0, rhat = r0, v = Eigen::VectorXd::Zero(long(N)), pv = v, y, z, sv, t;
                double rho = 1, alpha = 1, w = 1;
                for (iterations = 0; iterations < m_opt.linearIterations && rn > target; ++iterations)
                {
                    const double rhoNew = rhat.dot(r);
                    if (!(std::abs(rhoNew) > 1e-300 * r0n * r0n))
                    {
                        // (the residual has become orthogonal to the shadow direction: restart from the current correction)
                        matvec2(d, r);
                        r = r0 - r;
                        rhat = r;
                        rho = rhat.dot(r);
                        if (!(rho > 0)) break;
                        pv.setZero();
                        v.setZero();
                        alpha = w = 1;
                        continue;
                    }
                    const double beta = (rhoNew / rho) * (alpha / w);
                    rho = rhoNew;
                    pv = r + beta * (pv - w * v);
                    m_ilut->solve(pv, y);
                    matvec2(y, v);
                    const double rv = rhat.dot(v);
                    if (!(std::abs(rv) > 0)) break;
                    alpha = rho / rv;
                    sv = r - alpha * v;
                    if (sv.norm() <= target)
                    {
                        d += alpha * y;
                        rn = sv.norm();
                        ++iterations;
                        break;
                    }
                    m_ilut->solve(sv, z);
                    matvec2(z, t);
                    const double tt = t.squaredNorm();
                    w = tt > 0 ? t.dot(sv) / tt : 0.0;
                    d += alpha * y + w * z;
                    r = sv - w * t;
                    rn = r.norm();
                    if (!std::isfinite(rn)) break;
                }
                ok = std::isfinite(rn) && rn <= 100 * target;
            };
            run();
            if (!ok && reused)
            {   // the kept factorisation no longer fits this matrix: a fresh one, once
                if (debug) fprintf(stderr, "[tetflow]   the kept ILUT failed after %ld iterations: building it again\n", iterations);
                buildIlut();
                reused = false;
                run();
            }
            m_ilutIterSeconds = iterations > 0 ? std::chrono::duration<double>(std::chrono::steady_clock::now() - tl1).count() / double(iterations) : 0.0;
            if (ok)
            {
                if (!reused) m_ilutBase = std::max(1L, iterations);
                m_ilutIter = iterations;
            }
            else
                m_ilutIter = -1;
            if (debug)
                fprintf(stderr, "[tetflow]   bicgstab: %zu unknowns, ILUT %s, %ld iterations in %.1f s, residual %.3e of %.3e (reduction asked %.1e)%s\n", N,
                        reused ? "kept" : (std::string("in ") + std::to_string(std::chrono::duration<double>(tl1 - tl0).count()).substr(0, 4) + " s").c_str(),
                        iterations, std::chrono::duration<double>(std::chrono::steady_clock::now() - tl1).count(), rn, r0n, tol, ok ? "" : " FAILED");
            if (ok)
            {
                Eigen::VectorXd sol = x0 + d;
                unscale(sol);
                usedIterative = true;
                return true;
            }
            iterativeFailed = true;
            if (debug) fprintf(stderr, "[tetflow]   the iterative solver failed: solving directly\n");
            if (N > 400000)
            {
                err = "the iterative solver did not converge and the problem is too large to solve directly: use a larger element size";
                return false;
            }
        }
        {
            SpCol Ac(Ar);
            if (!analysed)
            {
                lu.analyzePattern(Ac);
                analysed = true;
            }
            lu.factorize(Ac);
            if (lu.info() != Eigen::Success)
            {
                err = "the flow equations are singular: no outlet holds the pressure, or nothing drives the flow";
                return false;
            }
            const auto tl1 = std::chrono::steady_clock::now();
            Eigen::VectorXd sol = lu.solve(b);
            unscale(sol);
            if (debug)
                fprintf(stderr, "[tetflow]   direct: %zu unknowns, factorised in %.1f s, solved in %.2f s%s\n", N,
                        std::chrono::duration<double>(tl1 - tl0).count(),
                        std::chrono::duration<double>(std::chrono::steady_clock::now() - tl1).count(),
                        iterativeFailed ? " (after the iterative solver failed)" : "");
            usedDirect = true;
            return true;
        }
    };

    // Stokes first
    std::vector<double> x, y;
    pack(x);
    assemble(u, false, false);
    double scale;
    {
        // the size of the forcing: the right side, and what the held velocities push into the free rows
        std::vector<double> xd(N, 0.0);
        for (size_t r = 0; r < N; ++r) xd[r] = m_fixed[r] ? m_fixedValue[r] : 0.0;
        matvec(xd, y);
        scale = std::max(freeNorm(rhs), freeNorm(y));
        if (!(scale > 0)) scale = 1.0;
    }
    if (cancel && cancel->load())
    {
        error = "cancelled";
        return false;
    }
    // The iterates of a steady solve asked for by the user (not the optimiser's or a time step's): each becomes a
    // step of the result, the flow developing from the Stokes start to the converged flow
    struct Iterate { std::vector<double> u, p; double residual; };
    std::vector<Iterate> iterates;
    const bool keepIterates = !m_quiet && !stepping;
    if (!stepping)
    {
        taskSet(0.05, direct ? "solving the Stokes equations (direct)" : "solving the Stokes equations (iterative)");
        if (!linearSolve(x, 0.0, error)) return false;
        unpack(x);
        if (keepIterates) iterates.push_back({u, p, std::numeric_limits<double>::quiet_NaN()});
    }
    int it = 0;
    double rel = 0;
    bool converged = true;
    const bool convect = !m_opt.stokes;
    if (convect || stepping)
    {
        // Picard, then Newton once the residual has come down; damped when it climbs
        double rel0 = -1, best = std::numeric_limits<double>::max();
        double omega = m_opt.relaxation;
        bool newton = false;
        int newtonOff = 0;
        std::vector<double> xPrev;
        converged = false;
        for (it = 0; it < m_opt.nonlinearIterations; ++it)
        {
            if (cancel && cancel->load())
            {
                error = "cancelled";
                return false;
            }
            const bool useNewton = m_opt.newton && newton && newtonOff == 0 && convect;
            assemble(u, convect, useNewton);
            pack(x);
            matvec(x, y);
            std::vector<double> r(N, 0.0);
            for (size_t k = 0; k < N; ++k) r[k] = m_fixed[k] ? 0.0 : y[k] - rhs[k];
            rel = freeNorm(r) / scale;
            if (rel0 < 0) rel0 = rel;
            if (keepIterates && !iterates.empty()) iterates.back().residual = rel;
            if (debug) fprintf(stderr, "[tetflow] iteration %d (%s): residual %.3e, %ld outlet triangles with backflow (each counted thrice)\n", it, useNewton ? "Newton" : "Picard", rel, backflowFaces.load());
            {
                const double frac = rel0 > 0 ? std::max(0.0, std::min(1.0, std::log(rel0 / std::max(rel, 1e-300)) /
                                                                            std::log(rel0 / std::max(tolerance, 1e-300)))) : 1.0;
                char buf[96];
                snprintf(buf, sizeof(buf), "%s iteration %d, residual %.2e", useNewton ? "Newton" : "Picard", it + 1, rel);
                taskSet(0.1 + 0.85 * frac, buf);
            }
            if (!std::isfinite(rel))
            {
                error = "the flow solver diverged: the flow may be beyond the laminar steady range (the Reynolds number is " +
                        std::to_string(int(m_stats.reynolds)) + ")";
                return false;
            }
            if (rel < tolerance)
            {
                converged = true;
                break;
            }
            if (rel > 10.0 * best)
            {
                // climbing: back to Picard for a while, and damp it
                if (useNewton) newtonOff = 3;
                else omega = std::max(0.1, 0.5 * omega);
                if (debug) fprintf(stderr, "[tetflow]   residual climbing: omega %.2f, newton off %d\n", omega, newtonOff);
            }
            best = std::min(best, rel);
            if (newtonOff > 0) --newtonOff;
            xPrev = x;
            if (!linearSolve(x, rel, error)) return false;
            const double w = useNewton ? 1.0 : omega;
            for (size_t k = 0; k < N; ++k) x[k] = w * x[k] + (1.0 - w) * xPrev[k];
            unpack(x);
            if (keepIterates) iterates.push_back({u, p, std::numeric_limits<double>::quiet_NaN()});
            if (rel < 1e-2 && (it >= 1 || stepping)) newton = true;
        }
        if (!converged)
        {
            assemble(u, convect, false);
            pack(x);
            matvec(x, y);
            std::vector<double> r(N, 0.0);
            for (size_t k = 0; k < N; ++k) r[k] = m_fixed[k] ? 0.0 : y[k] - rhs[k];
            rel = freeNorm(r) / scale;
            if (!(rel < 100 * tolerance))
            {
                error = "the flow solver did not converge in " + std::to_string(m_opt.nonlinearIterations) + " iterations (residual " +
                        std::to_string(rel) + "): the flow may be unsteady at this Reynolds number (" +
                        std::to_string(int(m_stats.reynolds)) + "), or the mesh too coarse";
                return false;
            }
        }
    }
    else
    {
        pack(x);
        matvec(x, y);
        std::vector<double> r(N, 0.0);
        for (size_t k = 0; k < N; ++k) r[k] = m_fixed[k] ? 0.0 : y[k] - rhs[k];
        rel = freeNorm(r) / scale;
    }
    taskSet(0.96, "the fields");
    m_u = u;
    m_p = p;

    m_adjoint.clear();
    if (m_wantAdjoint && !stepping)
    {
        // The discrete adjoint of the Stokes-Brinkman system for the dissipated power J = sum_e mu V |grad u|^2 +
        // alpha_e int |u|^2: K^T lambda = -dJ/dU.  The stabilised system is not symmetric (the pressure equation's
        // tau alpha grad q . u is not the transpose of -(div w, p), and tau depends on alpha), so lambda is not -2u,
        // and the self-adjoint sensitivity alpha' int |u|^2 was wrong by up to 7x (checked by finite differences):
        // K's transpose is built on the same block pattern, the held rows the identity (lambda is zero there), and
        // solved through the same scaled linear solver (whose kept factorisation is of K: built afresh around this).
        taskSet(0.96, "the adjoint");
        // (the convective flow: the exact Jacobian at the converged state, not the last iteration's)
        if (convect) assemble(u, true, true);
        // dJ/dU of the objective J = F . m_objDir, F = sum_e alpha_e int u dV = sum_e alpha_e (V_e / 4) sum_i u_i
        std::vector<double> rJ(N, 0.0);
        for (size_t t = 0; t < nt; ++t)
        {
            const double al = friction ? m_alpha[t] : 0.0;
            if (!(al > 0)) continue;
            const double w = al * A.geom[t].vol / 4.0;
            const auto& tv = mesh.tets[t];
            for (int q = 0; q < 4; ++q)
            {
                const size_t n = size_t(tv[size_t(q)]);
                for (int a2 = 0; a2 < 3; ++a2) rJ[4 * n + size_t(a2)] += w * m_objDir[a2];
            }
        }
        std::vector<double> valT(val.size(), 0.0), rhsT(N, 0.0);
        for (size_t i = 0; i < nv; ++i)
        {
            const uint32_t r0 = A.K.rowPtr[i], r1 = A.K.rowPtr[i + 1];
            for (uint32_t e = r0; e < r1; ++e)
            {
                const uint32_t j = A.K.col[e];
                const uint32_t j0 = A.K.rowPtr[j], j1 = A.K.rowPtr[j + 1];
                const size_t sj = size_t(std::lower_bound(A.K.col.begin() + j0, A.K.col.begin() + j1, uint32_t(i)) - (A.K.col.begin() + j0));
                const size_t si = size_t(e - r0);
                for (int r = 0; r < 4; ++r)
                    for (int c = 0; c < 4; ++c)
                        valT[size_t(outer[4 * i + size_t(r)]) + 4 * si + size_t(c)] = val[size_t(outer[4 * j + size_t(c)]) + 4 * sj + size_t(r)];
            }
            const size_t rowLen = 4 * size_t(r1 - r0);
            const size_t sii = size_t(std::lower_bound(A.K.col.begin() + r0, A.K.col.begin() + r1, uint32_t(i)) - (A.K.col.begin() + r0));
            for (int r = 0; r < 4; ++r)
            {
                const size_t row = 4 * i + size_t(r);
                if (m_fixed[row])
                {
                    std::fill(valT.begin() + outer[row], valT.begin() + outer[row] + std::ptrdiff_t(rowLen), 0.0);
                    valT[size_t(outer[row]) + 4 * sii + size_t(r)] = 1.0;
                    rhsT[row] = 0.0;
                }
                else
                    rhsT[row] = -rJ[row];
            }
        }
        std::swap(val, valT);
        std::swap(rhs, rhsT);
        const double savedScale = m_residualScale;
        m_residualScale = 0;
        m_ilutIter = -1;
        std::vector<double> lam(N, 0.0);
        std::string errA;
        const bool okA = linearSolve(lam, 0.0, errA);
        m_ilutIter = -1;
        m_residualScale = savedScale;
        std::swap(val, valT);
        std::swap(rhs, rhsT);
        if (okA) m_adjoint = lam;
        else if (debug) fprintf(stderr, "[tetflow] the adjoint solve failed: %s\n", errA.c_str());
    }

    // The fields of an iterate (the converged flow, or an earlier one for the steps): each element's velocity
    // gradient, the derived fields averaged at the nodes by volume, the dissipation and the cell Reynolds number
    auto makeFields = [&](const std::vector<double>& uu, const std::vector<double>& pp, std::vector<Eigen::Matrix3d>& gradOut,
                          double& dissipationOut, double& cellReOut, double& maxSpeedOut) -> std::shared_ptr<MeshResult> {
        gradOut.assign(nt, Eigen::Matrix3d::Zero());
        std::vector<double> shear(nt), vort(nt);
        dissipationOut = 0;
        cellReOut = 0;
        parallelRange(nt, [&](size_t b0, size_t b1) {
            for (size_t t = b0; t < b1; ++t)
            {
                Eigen::Matrix3d G = Eigen::Matrix3d::Zero();
                for (int q = 0; q < 4; ++q)
                {
                    const size_t n = size_t(mesh.tets[t][size_t(q)]);
                    const Vec3 un(uu[3 * n], uu[3 * n + 1], uu[3 * n + 2]);
                    for (int b = 0; b < 3; ++b)
                        for (int c = 0; c < 3; ++c) G(b, c) += un[b] * A.geom[t].g[q][c];
                }
                gradOut[t] = G;
                const Eigen::Matrix3d E = 0.5 * (G + G.transpose());
                shear[t] = std::sqrt(2.0 * (E.array() * E.array()).sum());
                const Vec3 w(G(2, 1) - G(1, 2), G(0, 2) - G(2, 0), G(1, 0) - G(0, 1));
                vort[t] = w.norm();
            }
        }, 256);
        for (size_t t = 0; t < nt; ++t)
        {
            const Eigen::Matrix3d E = 0.5 * (gradOut[t] + gradOut[t].transpose());
            dissipationOut += 2.0 * m_mu * A.geom[t].vol * (E.array() * E.array()).sum();
            Vec3 ubar = Vec3::Zero();
            for (int q = 0; q < 4; ++q)
            {
                const size_t n = size_t(mesh.tets[t][size_t(q)]);
                ubar += 0.25 * Vec3(uu[3 * n], uu[3 * n + 1], uu[3 * n + 2]);
            }
            cellReOut = std::max(cellReOut, ubar.norm() * he[t] * m_rho / m_mu);
        }
        auto r = std::make_shared<MeshResult>();
        r->mesh = m_mesh;
        for (int k = 0; k < FLOW_FIELD_COUNT; ++k) r->fields[k].assign(nv, 0.0f);
        parallelRange(nv, [&](size_t b0, size_t b1) {
            for (size_t i = b0; i < b1; ++i)
            {
                double sh = 0, vo = 0, w = 0;
                for (uint32_t e = A.vtStart[i]; e < A.vtStart[i + 1]; ++e)
                {
                    const size_t t = A.vtList[e];
                    sh += A.geom[t].vol * shear[t];
                    vo += A.geom[t].vol * vort[t];
                    w += A.geom[t].vol;
                }
                if (w > 0)
                {
                    sh /= w;
                    vo /= w;
                }
                const Vec3 ui(uu[3 * i], uu[3 * i + 1], uu[3 * i + 2]);
                r->fields[SPEED][i] = float(ui.norm());
                r->fields[VX][i] = float(ui.x());
                r->fields[VY][i] = float(ui.y());
                r->fields[VZ][i] = float(ui.z());
                r->fields[PRESSURE][i] = float(pp[i]);
                r->fields[TOTAL_PRESSURE][i] = float(pp[i] + 0.5 * m_rho * ui.squaredNorm());
                r->fields[SHEAR_RATE][i] = float(sh);
                r->fields[VORTICITY][i] = float(vo);
            }
        }, 256);
        maxSpeedOut = 0;
        for (size_t i = 0; i < nv; ++i) maxSpeedOut = std::max(maxSpeedOut, double(r->fields[SPEED][i]));
        for (int k = 0; k < Result::FIELD_COUNT; ++k)
        {
            float mn = std::numeric_limits<float>::max(), mx = -std::numeric_limits<float>::max();
            for (float v : r->fields[k])
            {
                mn = std::min(mn, v);
                mx = std::max(mx, v);
            }
            r->minValue[k] = mn;
            r->maxValue[k] = mx;
        }
        return r;
    };
    std::vector<Eigen::Matrix3d> grad;
    double dissipation = 0, cellRe = 0, maxSpeed = 0;
    auto res = makeFields(u, p, grad, dissipation, cellRe, maxSpeed);
    if (!m_locator) m_locator = std::make_shared<TetLocator>(mesh);
    res->locator = m_locator;
    res->elements = int(nt);
    res->nodes = int(nv);
    res->dofs = int(N);
    res->iterations = it;
    res->residual = rel;

    // The force on the walls: the reaction of the discrete momentum equations at the nodes a wall holds (the
    // equations assembled without replacing the held rows: what the wall must push with to hold the velocity
    // there, which is the force on the fluid; the force on the wall is the opposite).  It is the force that the
    // discrete momentum balance accounts for, and it converges much faster than integrating the elements' constant
    // stress over the wall's triangles (which is also computed, as a check: FIELDES_FLOW_DEBUG prints both).
    std::vector<Vec3> reactionForces(m_walls.size(), Vec3::Zero());
    Vec3 reactionTotal = Vec3::Zero();
    {
        keepHeldRows = true;
        assemble(u, convect, false);
        keepHeldRows = false;
        pack(x);
        matvec(x, y);
        for (size_t i = 0; i < nv; ++i)
        {
            const int kind = m_nodeKind[i];
            if (kind != -1 && kind != 2) continue;
            Vec3 r = Vec3::Zero();
            for (int a = 0; a < 3; ++a) r[a] = y[4 * i + size_t(a)] - rhs[4 * i + size_t(a)];
            reactionTotal -= r;
            if (kind == 2) reactionForces[size_t(m_nodeItem[i])] -= r;
        }
    }

    // Flows through the inlets and outlets, their mean pressures, the force on the walls
    m_inletFlows.assign(m_inlets.size(), 0.0);
    m_outletFlows.assign(m_outlets.size(), 0.0);
    m_wallForces.assign(m_walls.size(), Vec3::Zero());
    double pIn = 0, pOut = 0, aIn = 0, aOut = 0, wallFlow = 0;
    Vec3 wallForce = Vec3::Zero();
    for (size_t fc = 0; fc < nf; ++fc)
    {
        const auto& tri = mesh.faces[fc];
        const double area = m_faceArea[fc];
        const Vec3& n = m_faceNormal[fc];
        double flux = 0, pf = 0;
        for (int q = 0; q < 3; ++q)
        {
            const size_t nn = size_t(tri[size_t(q)]);
            flux += (area / 3.0) * n.dot(Vec3(u[3 * nn], u[3 * nn + 1], u[3 * nn + 2]));
            pf += p[nn] / 3.0;
        }
        const int kind = m_faceKind[fc];
        if (kind == 0)
        {
            m_inletFlows[size_t(m_faceItem[fc])] -= flux;
            pIn += area * pf;
            aIn += area;
        }
        else if (kind == 1)
        {
            m_outletFlows[size_t(m_faceItem[fc])] += flux;
            pOut += area * pf;
            aOut += area;
        }
        else if (kind == -1 || kind == 2)
        {
            // (sigma n is the traction ON the fluid at its boundary, n pointing out of the fluid: the force on the wall
            // is the opposite; a wall that moves, or the rounded rim of an inlet, carries flux too)
            const size_t t = size_t(mesh.faceTet[fc]);
            const Eigen::Matrix3d sigma = -pf * Eigen::Matrix3d::Identity() + m_mu * (grad[t] + grad[t].transpose());
            const Vec3 force = -area * (sigma * n);
            wallForce += force;
            if (kind == 2) m_wallForces[size_t(m_faceItem[fc])] += force;
            wallFlow -= flux;
        }
    }
    if (debug)
    {
        fprintf(stderr, "[tetflow] wall force: reactions (%.5g %.5g %.5g) N, stress integral (%.5g %.5g %.5g) N\n",
                reactionTotal.x(), reactionTotal.y(), reactionTotal.z(), wallForce.x(), wallForce.y(), wallForce.z());
        for (size_t k = 0; k < m_walls.size(); ++k)
            fprintf(stderr, "[tetflow]   wall %zu: reactions (%.5g %.5g %.5g), stress integral (%.5g %.5g %.5g)\n", k + 1,
                    reactionForces[k].x(), reactionForces[k].y(), reactionForces[k].z(), m_wallForces[k].x(), m_wallForces[k].y(), m_wallForces[k].z());
    }
    wallForce = reactionTotal;
    m_wallForces = reactionForces;
    Stats st;
    st.inletFlow = std::accumulate(m_inletFlows.begin(), m_inletFlows.end(), 0.0);
    st.outletFlow = std::accumulate(m_outletFlows.begin(), m_outletFlows.end(), 0.0);
    st.wallFlow = wallFlow;
    st.imbalance = st.inletFlow > 0 ? std::abs(st.inletFlow + st.wallFlow - st.outletFlow) / st.inletFlow : 0.0;
    st.pressureDrop = (aIn > 0 ? pIn / aIn : 0.0) - (aOut > 0 ? pOut / aOut : 0.0);
    st.maxSpeed = maxSpeed;
    st.dissipation = dissipation;
    st.hydraulicDiameter = m_wallArea > 0 ? 4.0 * m_volume / m_wallArea : 0.0;
    st.elementsAcross = st.hydraulicDiameter / m_h;
    {
        double dh = 0, uref = 0;
        if (m_inletArea > 0 && m_inletPerimeter > 0)
        {
            dh = 4.0 * m_inletArea / m_inletPerimeter;
            uref = st.inletFlow / m_inletArea;
        }
        else
        {
            // (a closed domain, a lid-driven cavity: its size and the fastest wall)
            dh = (m_hi - m_lo).maxCoeff();
            uref = m_refSpeed;
        }
        st.reynolds = m_rho * uref * dh / m_mu;
    }
    st.cellReynolds = cellRe;
    st.wallForce = wallForce;
    st.nonlinearIterations = it;
    st.residual = rel;
    st.converged = converged;
    st.linearSolver = usedDirect && !usedIterative ? 0 : (usedIterative && !usedDirect ? 1 : 2);
    st.elements = int(nt);
    st.nodes = int(nv);
    st.unknowns = int(N);
    st.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    m_stats = st;
    m_warning.clear();
    if (st.elementsAcross < 4)
    {
        char buf[200];
        snprintf(buf, sizeof(buf), "the mesh is coarse for this flow: %.1f elements across the passages (hydraulic diameter %.3g mm over "
                                   "the element size); four or more are needed for a trustworthy pressure drop", st.elementsAcross, st.hydraulicDiameter);
        m_warning = buf;
    }
    if (!m_opt.stokes && st.reynolds > 2000)
    {
        char buf[200];
        snprintf(buf, sizeof(buf), "Reynolds number %.0f: beyond the laminar range (about 2000 in a pipe); a steady laminar solution may not "
                                   "describe the real flow", st.reynolds);
        m_warning += (m_warning.empty() ? "" : "; ") + std::string(buf);
    }
    m_result = res;
    if (m_snapshot)
    {
        Step step;
        step.time = m_time;
        step.result = res;
        m_steps.push_back(step);
        m_stepStats.push_back(st);
    }
    if (keepIterates)
    {
        // the steps of this steady solve: the flow after each iteration (the Stokes start first), the last the result
        m_steps.clear();
        m_stepStats.clear();
        for (size_t k = 0; k < iterates.size(); ++k)
        {
            const bool last = k + 1 == iterates.size();
            std::vector<Eigen::Matrix3d> g2;
            double d2 = dissipation, c2 = cellRe, ms2 = maxSpeed;
            std::shared_ptr<MeshResult> r2;
            if (!last)
            {
                r2 = makeFields(iterates[k].u, iterates[k].p, g2, d2, c2, ms2);
                r2->locator = m_locator;
            }
            Step step;
            step.time = 0;
            step.iteration = int(k);
            step.result = last ? std::shared_ptr<const MeshResult>(res) : std::shared_ptr<const MeshResult>(r2);
            Stats st2 = st;
            st2.maxSpeed = ms2;
            st2.dissipation = d2;
            st2.cellReynolds = c2;
            st2.nonlinearIterations = int(k);
            st2.residual = std::isfinite(iterates[k].residual) ? iterates[k].residual : st.residual;
            st2.converged = last ? st.converged : false;
            m_steps.push_back(step);
            m_stepStats.push_back(st2);
        }
    }
    if (const char* dump = std::getenv("FIELDES_FLOW_DUMP"))
    {
        // (for the developer: every boundary node -- position, kind of condition, velocity, pressure)
        if (FILE* fp = std::fopen(dump, "w"))
        {
            for (size_t i = 0; i < nv; ++i)
            {
                if (m_vfStart[i + 1] == m_vfStart[i]) continue;
                int kinds = 0;
                for (uint32_t e = m_vfStart[i]; e < m_vfStart[i + 1]; ++e) kinds |= 1 << (m_faceKind[m_vfList[e]] + 1);
                std::fprintf(fp, "%.6g %.6g %.6g %d%d%d%d %d %.6g %.6g %.6g %.6g\n", mesh.pos[i].x(), mesh.pos[i].y(), mesh.pos[i].z(),
                             int(m_fixed[4 * i]), int(m_fixed[4 * i + 1]), int(m_fixed[4 * i + 2]), int(m_fixed[4 * i + 3]), kinds,
                             u[3 * i], u[3 * i + 1], u[3 * i + 2], p[i]);
            }
            std::fclose(fp);
        }
    }
    return true;
}

////////////////////////////////////////////////////////////////////////////////
// The flow in time

bool TetFlowProblem::solveTransient(double dt, int steps, int storeEvery, double tolerance, std::string& error,
                                    const std::atomic<bool>* cancel)
{
    if (!(dt > 0) || steps < 1)
    {
        error = "the time step must be positive and the number of steps at least one";
        return false;
    }
    if (!m_prepared && !prepare(error)) return false;
    storeEvery = std::max(1, storeEvery);
    m_steps.clear();
    m_stepStats.clear();
    m_dtInv = 0;
    m_uOld.clear();
    m_u.clear();
    m_p.clear();
    run_progress::Task task("the flow in time");
    task.set(0.0, "the start: the Stokes flow");
    // t = 0: the Stokes flow (an impulsive start: the fluid is set in motion at once)
    {
        const Options saved = m_opt;
        m_opt.stokes = true;
        m_quiet = true;
        m_snapshot = true;
        m_time = 0.0;
        const bool ok = solve(tolerance, error, cancel);
        m_opt = saved;
        m_quiet = false;
        m_snapshot = false;
        if (!ok) return false;
    }
    const bool debug = std::getenv("FIELDES_FLOW_DEBUG") != nullptr;
    for (int k = 1; k <= steps; ++k)
    {
        if (cancel && cancel->load())
        {
            error = "cancelled";
            m_dtInv = 0;
            m_uOld.clear();
            return false;
        }
        {
            char buf[96];
            snprintf(buf, sizeof(buf), "step %d of %d, t = %.4g s", k, steps, k * dt);
            task.set(double(k - 1) / double(steps), buf);
            task.span(double(k - 1) / double(steps), double(k) / double(steps));
        }
        m_uOld = m_u;
        m_dtInv = 1.0 / dt;
        m_time = k * dt;
        m_quiet = true;
        m_snapshot = (k % storeEvery == 0) || k == steps;
        const bool ok = solve(tolerance, error, cancel);
        m_quiet = false;
        m_snapshot = false;
        if (!ok)
        {
            m_dtInv = 0;
            m_uOld.clear();
            error = "at t = " + std::to_string(k * dt) + " s: " + error;
            return false;
        }
        if (debug)
            fprintf(stderr, "[tetflow time] step %d t %.4g: %d iterations, residual %.2e, max speed %.4g, in %.4g out %.4g, force (%.4g %.4g %.4g)\n",
                    k, k * dt, m_stats.nonlinearIterations, m_stats.residual, m_stats.maxSpeed, m_stats.inletFlow, m_stats.outletFlow,
                    m_stats.wallForce.x(), m_stats.wallForce.y(), m_stats.wallForce.z());
    }
    m_dtInv = 0;
    m_uOld.clear();
    return true;
}

////////////////////////////////////////////////////////////////////////////////
// Streamlines

void TetFlowProblem::streamlines(std::shared_ptr<const MeshResult> res, const std::vector<Vec3>& seeds, double maxTime,
                                 int maxPoints, bool backward, std::vector<std::vector<std::array<double, 5>>>& out) const
{
    out.clear();
    if (!res || !res->locator || !m_mesh) return;
    const TetMesh& mesh = *m_mesh;
    const double uRef = std::max(1e-300, double(res->maxValue[SPEED]));
    const double sign = backward ? -1.0 : 1.0;
    auto velocity = [&](const Vec3& p, Vec3& u) {
        double lam[4];
        const int t = res->locator->locateInside(p, lam);
        if (t < 0) return false;
        u = Vec3::Zero();
        for (int i = 0; i < 4; ++i)
        {
            const size_t n = size_t(mesh.tets[size_t(t)][size_t(i)]);
            u += lam[i] * Vec3(res->fields[VX][n], res->fields[VY][n], res->fields[VZ][n]);
        }
        u *= sign;
        return true;
    };
    const double ds = 0.5 * m_h;
    out.reserve(seeds.size());
    for (const Vec3& seed : seeds)
    {
        std::vector<std::array<double, 5>> line;
        Vec3 p = seed, u;
        if (!velocity(p, u)) continue;
        double t = 0;
        line.push_back({p.x(), p.y(), p.z(), u.norm(), 0.0});
        for (int n = 1; n < maxPoints && t < maxTime; ++n)
        {
            const double speed = u.norm();
            if (!(speed > 1e-6 * uRef)) break;
            double dt = ds / speed;
            Vec3 k1 = u, k2, k3, k4, pn;
            bool ok = false;
            for (int tries = 0; tries < 4 && !ok; ++tries)
            {
                ok = velocity(p + 0.5 * dt * k1, k2) && velocity(p + 0.5 * dt * k2, k3) && velocity(p + dt * k3, k4);
                if (ok)
                {
                    pn = p + (dt / 6.0) * (k1 + 2.0 * k2 + 2.0 * k3 + k4);
                    ok = velocity(pn, u);
                }
                if (!ok) dt *= 0.25;
            }
            if (!ok) break;
            p = pn;
            t += dt;
            line.push_back({p.x(), p.y(), p.z(), u.norm(), t});
        }
        if (line.size() > 1) out.push_back(std::move(line));
    }
}

std::vector<Vec3> TetFlowProblem::inletSeeds(int n) const
{
    std::vector<Vec3> all;
    if (!m_mesh) return all;
    const TetMesh& mesh = *m_mesh;
    for (size_t f = 0; f < mesh.faces.size(); ++f)
    {
        if (m_faceKind[f] != 0) continue;
        const auto& tri = mesh.faces[f];
        const Vec3 c = (mesh.pos[size_t(tri[0])] + mesh.pos[size_t(tri[1])] + mesh.pos[size_t(tri[2])]) / 3.0;
        all.push_back(c - 0.2 * m_h * m_faceNormal[f]);         // (a little inside the fluid)
    }
    if (n <= 0 || all.size() <= size_t(n)) return all;
    std::vector<Vec3> out;
    const double stride = double(all.size()) / double(n);
    for (int k = 0; k < n; ++k) out.push_back(all[size_t(std::min<double>(double(all.size() - 1), (k + 0.5) * stride))]);
    return out;
}

////////////////////////////////////////////////////////////////////////////////
// Flow topology optimisation

bool TetFlowProblem::optimize(const FlowOpt& s, std::string& error, const std::atomic<bool>* cancel)
{
    if (!m_prepared && !prepare(error)) return false;
    if (!s.body.is_valid())
    {
        error = "the body to optimise is missing";
        return false;
    }
    if (m_g.norm() > 0)
    {
        error = "gravity isn't supported in flow shape optimisation";
        return false;
    }
    if (m_inlets.empty() || m_inletDir.empty())
    {
        error = "the flow needs an inlet for the body to be optimised in it";
        return false;
    }
    if (!(s.volumeMin >= 0 && s.volumeMax >= s.volumeMin))
    {
        error = "the volume must be between 0 and 1 times the body's, the least no more than the most";
        return false;
    }
    const auto t0 = std::chrono::steady_clock::now();
    const TetMesh& mesh = *m_mesh;
    const size_t nv = mesh.pos.size(), na = mesh.tets.size(), nf = mesh.faces.size();
    const double h = m_h;
    const bool debug = std::getenv("FIELDES_FLOW_DEBUG") != nullptr;
    std::vector<TetGeom> geom(na);
    std::vector<Vec3> cen(na);
    std::vector<Eigen::Vector3f> centres(na);
    for (size_t a = 0; a < na; ++a)
    {
        const auto& v = mesh.tets[a];
        geom[a] = geometryOf(mesh.pos[size_t(v[0])], mesh.pos[size_t(v[1])], mesh.pos[size_t(v[2])], mesh.pos[size_t(v[3])]);
        Vec3 c = Vec3::Zero();
        for (int p = 0; p < 4; ++p) c += mesh.pos[size_t(v[size_t(p)])];
        cen[a] = c / 4.0;
        centres[a] = cen[a].cast<float>();
    }
    std::vector<double> vol(na);
    for (size_t a = 0; a < na; ++a) vol[a] = geom[a].vol;

    // The directions: the flow's (the inlets' mean), the lift's (perpendicular to it, in the plane of the domain's
    // two long axes -- a slab's plane -- or any perpendicular when the flow runs along the thin axis)
    Vec3 d = s.flowDir;
    if (!(d.norm() > 0))
    {
        d = Vec3::Zero();
        for (const auto& v : m_inletDir) d += v;
    }
    if (!(d.norm() > 0))
    {
        error = "the flow direction cannot be told from the inlets: give flow_direction";
        return false;
    }
    d.normalize();
    Vec3 l = s.liftDir;
    if (!(l.norm() > 0))
    {
        const Vec3 ext = m_hi - m_lo;
        int thin = 0;
        for (int k = 1; k < 3; ++k) if (ext[k] < ext[thin]) thin = k;
        Vec3 t = Vec3::Zero();
        t[thin] = 1.0;
        l = t.cross(d);
        if (!(l.norm() > 1e-6))
        {
            t = Vec3::Zero();
            t[(thin + 1) % 3] = 1.0;
            l = t.cross(d);
        }
    }
    l -= l.dot(d) * d;
    if (!(l.norm() > 0))
    {
        error = "the lift direction must not be along the flow";
        return false;
    }
    l.normalize();
    m_flowDir = d;
    m_liftDir = l;
    const Vec3 objDir = s.wDrag * d - s.wLift * l;
    if (!(objDir.norm() > 0))
    {
        error = "the objective weighs nothing: give a drag or a lift weight";
        return false;
    }

    // Which elements are fixed: fluid (avoid regions, outside the design region, the inlets' and outlets' own
    // elements) or solid (keep regions); the rest are the design.
    std::vector<char> state(na, 0);             // 0 design, 1 fluid, 2 solid
    std::vector<float> vals;
    if (s.region.is_valid())
    {
        evalTreePoints(s.region, centres, vals);
        for (size_t a = 0; a < na; ++a) if (!(vals[a] < 0)) state[a] = 1;
    }
    for (const auto& t : s.avoid)
    {
        evalTreePoints(t, centres, vals);
        for (size_t a = 0; a < na; ++a) if (vals[a] < 0) state[a] = 1;
    }
    for (const auto& t : s.keep)
    {
        evalTreePoints(t, centres, vals);
        for (size_t a = 0; a < na; ++a) if (vals[a] < 0) state[a] = 2;
    }
    for (size_t f = 0; f < nf; ++f)
        if (m_faceKind[f] == 0 || m_faceKind[f] == 1) state[size_t(mesh.faceTet[f])] = 1;

    // The design is a LEVEL SET at the nodes, phi (mm, > 0 inside the body), the body its zero level: a smooth
    // boundary that moves, splits and merges, placed to a fraction of an element.  Each element's share of the
    // body is the exact fraction of it where the linear interpolant of phi is positive, and that fraction sets its
    // friction.  A node of a fluid element is held at -h, of a solid (keep) element at +h, the rest are the
    // variables; phi starts as the given body's own distance.
    std::vector<uint32_t> vtStart(nv + 1, 0), vtList(4 * na);
    for (const auto& t : mesh.tets)
        for (int p = 0; p < 4; ++p) vtStart[size_t(t[size_t(p)]) + 1]++;
    for (size_t i = 0; i < nv; ++i) vtStart[i + 1] += vtStart[i];
    {
        std::vector<uint32_t> cursor(vtStart.begin(), vtStart.end() - 1);
        for (size_t a = 0; a < na; ++a)
            for (int p = 0; p < 4; ++p) vtList[cursor[size_t(mesh.tets[a][size_t(p)])]++] = uint32_t(a);
    }
    std::vector<char> nodeState(nv, 0);         // 0 variable, 1 held fluid, 2 held solid
    for (size_t i = 0; i < nv; ++i)
    {
        bool fluid = false, solid = false, design = false;
        for (uint32_t e = vtStart[i]; e < vtStart[i + 1]; ++e)
        {
            const char st = state[vtList[e]];
            fluid = fluid || st == 1;
            solid = solid || st == 2;
            design = design || st == 0;
        }
        nodeState[i] = fluid ? 1 : solid ? 2 : design ? 0 : 1;
    }
    std::vector<Eigen::Vector3f> nodePts(nv);
    for (size_t i = 0; i < nv; ++i) nodePts[i] = mesh.pos[i].cast<float>();
    evalTreePoints(s.body, nodePts, vals);
    std::vector<double> phi(nv);
    for (size_t i = 0; i < nv; ++i)
        phi[i] = nodeState[i] == 1 ? -h : nodeState[i] == 2 ? h : -double(vals[i]);
    auto hold = [&](std::vector<double>& ph) {
        for (size_t i = 0; i < nv; ++i)
            if (nodeState[i] == 1) ph[i] = -h;
            else if (nodeState[i] == 2) ph[i] = h;
    };

    // Extrusion: the variable nodes along a line share one value
    std::vector<int> column(nv, -1);
    int nCol = 0;
    std::vector<double> colCount;
    if (s.extrude >= 0 && s.extrude <= 2)
    {
        const int u0 = s.extrude == 0 ? 1 : 0, u1 = s.extrude == 2 ? 1 : 2;
        Vec3 lo = mesh.pos[0];
        for (const auto& c : mesh.pos) lo = lo.cwiseMin(c);
        std::unordered_map<int64_t, int> id;
        const double bin = 0.25 * h;
        for (size_t i = 0; i < nv; ++i)
        {
            if (nodeState[i]) continue;
            const int64_t key = int64_t(std::floor((mesh.pos[i][u0] - lo[u0]) / bin + 0.5)) * 1000003 +
                                int64_t(std::floor((mesh.pos[i][u1] - lo[u1]) / bin + 0.5));
            auto it = id.find(key);
            if (it == id.end()) it = id.emplace(key, nCol++).first;
            column[i] = it->second;
        }
        colCount.assign(size_t(nCol), 0.0);
        for (size_t i = 0; i < nv; ++i) if (column[i] >= 0) colCount[size_t(column[i])] += 1.0;
    }
    auto alongColumns = [&](std::vector<double>& v, bool mean) {
        if (!nCol) return;
        std::vector<double> sum(size_t(nCol), 0.0);
        for (size_t i = 0; i < nv; ++i) if (column[i] >= 0) sum[size_t(column[i])] += v[i];
        for (size_t i = 0; i < nv; ++i)
            if (column[i] >= 0) v[i] = mean ? sum[size_t(column[i])] / colCount[size_t(column[i])] : sum[size_t(column[i])];
    };
    alongColumns(phi, true);

    // The smoothing of the boundary's motion (the descent direction, not the shape): weights rmin - distance over
    // the nodes within rmin, times their share of the volume (the filter of the structural optimiser, at the nodes)
    const double rmin = s.filterRadius > 0 ? s.filterRadius : 1.5 * h;
    std::vector<double> vn(nv, 0.0);
    for (size_t a = 0; a < na; ++a)
        for (int p = 0; p < 4; ++p) vn[size_t(mesh.tets[a][size_t(p)])] += 0.25 * vol[a];
    std::vector<size_t> fStart(nv + 1, 0);
    std::vector<uint32_t> fIdx;
    std::vector<float> fW;
    std::vector<double> Hs(nv, 0.0);
    {
        Vec3 lo = mesh.pos[0], hi = mesh.pos[0];
        for (const auto& c : mesh.pos)
        {
            lo = lo.cwiseMin(c);
            hi = hi.cwiseMax(c);
        }
        const double cell = rmin;
        int dims[3];
        for (int k = 0; k < 3; ++k) dims[k] = std::max(1, int((hi[k] - lo[k]) / cell) + 1);
        std::vector<std::vector<uint32_t>> grid(size_t(dims[0]) * dims[1] * dims[2]);
        auto cellOf = [&](const Vec3& c, int* ijk) {
            for (int k = 0; k < 3; ++k) ijk[k] = std::max(0, std::min(dims[k] - 1, int((c[k] - lo[k]) / cell)));
        };
        for (size_t i = 0; i < nv; ++i)
        {
            int ijk[3];
            cellOf(mesh.pos[i], ijk);
            grid[size_t(ijk[0]) + size_t(dims[0]) * (size_t(ijk[1]) + size_t(dims[1]) * size_t(ijk[2]))].push_back(uint32_t(i));
        }
        for (size_t i = 0; i < nv; ++i)
        {
            int ijk[3];
            cellOf(mesh.pos[i], ijk);
            for (int dz = -1; dz <= 1; ++dz)
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx)
                    {
                        const int ii = ijk[0] + dx, jj = ijk[1] + dy, kk = ijk[2] + dz;
                        if (ii < 0 || jj < 0 || kk < 0 || ii >= dims[0] || jj >= dims[1] || kk >= dims[2]) continue;
                        for (uint32_t j : grid[size_t(ii) + size_t(dims[0]) * (size_t(jj) + size_t(dims[1]) * size_t(kk))])
                        {
                            const double dist = (mesh.pos[i] - mesh.pos[j]).norm();
                            if (dist < rmin)
                            {
                                const double w = (rmin - dist) * vn[j];
                                fIdx.push_back(j);
                                fW.push_back(float(w));
                                Hs[i] += w;
                            }
                        }
                    }
            fStart[i + 1] = fIdx.size();
        }
    }
    auto filter = [&](const std::vector<double>& in, std::vector<double>& out) {
        out.resize(nv);
        parallelRange(nv, [&](size_t b0, size_t b1) {
            for (size_t i = b0; i < b1; ++i)
            {
                double sum = 0;
                for (size_t q = fStart[i]; q < fStart[i + 1]; ++q) sum += double(fW[q]) * in[fIdx[q]];
                out[i] = Hs[i] > 0 ? sum / Hs[i] : in[i];
            }
        }, 1024);
    };
    auto filterT = [&](const std::vector<double>& in, std::vector<double>& out) {
        out.assign(nv, 0.0);
        for (size_t i = 0; i < nv; ++i)
        {
            if (!(Hs[i] > 0)) { out[i] += in[i]; continue; }
            const double f = in[i] / Hs[i];
            for (size_t q = fStart[i]; q < fStart[i + 1]; ++q) out[fIdx[q]] += double(fW[q]) * f;
        }
    };

    // The fraction of a tetrahedron where the linear interpolant of its four nodal values is positive -- the
    // volume of the simplex above the plane -- as the third divided difference of t_+^3 over the values (one
    // positive corner: a small similar tetrahedron, (d / (d - a)) (d / (d - b)) (d / (d - c)); the rest follows),
    // with its derivative in each nodal value.  Equal values are nudged apart: the formula divides by their
    // differences.
    auto fraction = [h](const double u[4], double* grad) -> double {
        double v[4] = {u[0], u[1], u[2], u[3]};
        int pos = 0, neg = 0;
        double scale = 0;
        for (int k = 0; k < 4; ++k)
        {
            scale = std::max(scale, std::abs(v[k]));
            if (v[k] > 0) ++pos;
            if (v[k] < 0) ++neg;
        }
        if (grad) for (int k = 0; k < 4; ++k) grad[k] = 0;
        if (neg == 0) return 1.0;
        if (pos == 0) return 0.0;
        // (the nudge: a thousandth of the values' size, and never below a millionth of an element -- values all
        // within a hair of the zero level would otherwise divide by nothing)
        const double eps = std::max(1e-3 * scale, 1e-6 * h);
        for (int pass = 0; pass < 4; ++pass)
            for (int i = 0; i < 4; ++i)
                for (int j = i + 1; j < 4; ++j)
                    if (std::abs(v[i] - v[j]) < eps) v[j] += (v[j] >= v[i]) ? eps : -eps;
        double P[4], g[4], gp[4];
        for (int k = 0; k < 4; ++k)
        {
            P[k] = 1.0;
            for (int j = 0; j < 4; ++j) if (j != k) P[k] *= (v[k] - v[j]);
            g[k] = v[k] > 0 ? v[k] * v[k] * v[k] : 0.0;
            gp[k] = v[k] > 0 ? 3.0 * v[k] * v[k] : 0.0;
        }
        double F = 0;
        for (int k = 0; k < 4; ++k) F += g[k] / P[k];
        if (grad)
        {
            for (int k = 0; k < 4; ++k)
            {
                double sumInv = 0;
                for (int j = 0; j < 4; ++j) if (j != k) sumInv += 1.0 / (v[k] - v[j]);
                double gk = gp[k] / P[k] - g[k] / P[k] * sumInv;
                for (int m = 0; m < 4; ++m) if (m != k) gk += g[m] / (P[m] * (v[m] - v[k]));
                grad[k] = gk;
            }
        }
        if (!std::isfinite(F))
        {
            if (grad) for (int k = 0; k < 4; ++k) grad[k] = 0;
            return pos > neg ? 1.0 : 0.0;
        }
        if (grad) for (int k = 0; k < 4; ++k) if (!std::isfinite(grad[k])) grad[k] = 0;
        return std::max(0.0, std::min(1.0, F));
    };
    // Every element's share of the body from the smoothed level set, held elements aside
    std::vector<double> phiTilde, rho(na, 0.0);
    auto fractions = [&](const std::vector<double>& ph, std::vector<double>& out) {
        out.resize(na);
        parallelRange(na, [&](size_t b0, size_t b1) {
            for (size_t a = b0; a < b1; ++a)
            {
                if (state[a] == 1) { out[a] = 0.0; continue; }
                if (state[a] == 2) { out[a] = 1.0; continue; }
                double u[4];
                for (int p = 0; p < 4; ++p) u[p] = ph[size_t(mesh.tets[a][size_t(p)])];
                out[a] = fraction(u, nullptr);
            }
        }, 1024);
    };
    auto solidVolume = [&](const std::vector<double>& fr) {
        double v = 0;
        for (size_t a = 0; a < na; ++a) v += vol[a] * fr[a];
        return v;
    };
    auto nodal = [&](const std::vector<double>& ph, std::vector<float>& out) {
        out.resize(nv);
        for (size_t i = 0; i < nv; ++i) out[i] = float(ph[i]);
    };
    // The mean slope of the level set across the elements it cuts: a distance function has 1, and the steps
    // are measured in mm of boundary motion, so phi is rescaled to a slope of 1 each iteration (a cheap
    // reinitialisation; the smoothing flattens it a little every time)
    auto slopeOf = [&](const std::vector<double>& ph) {
        double sum = 0, cnt = 0;
        for (size_t a = 0; a < na; ++a)
        {
            if (state[a]) continue;
            const auto& tv = mesh.tets[a];
            double mn = ph[size_t(tv[0])], mx = mn;
            for (int p = 1; p < 4; ++p)
            {
                mn = std::min(mn, ph[size_t(tv[size_t(p)])]);
                mx = std::max(mx, ph[size_t(tv[size_t(p)])]);
            }
            if (!(mn < 0 && mx > 0)) continue;
            Eigen::Matrix<double, 4, 3> g;
            m_locator->gradients(int(a), g);
            Vec3 gr = Vec3::Zero();
            for (int p = 0; p < 4; ++p) gr += ph[size_t(tv[size_t(p)])] * g.row(p).transpose();
            sum += gr.norm();
            cnt += 1.0;
        }
        return cnt > 0 ? sum / cnt : 1.0;
    };

    phiTilde = phi;
    fractions(phiTilde, rho);
    const double bodyVolume = solidVolume(rho);
    if (!(bodyVolume > 0))
    {
        error = "the body lies in no element of the fluid domain";
        return false;
    }
    const double vMin = s.volumeMin * bodyVolume, vMax = s.volumeMax * bodyVolume;

    // The friction of the solid: mu / (darcy h^2), so the flow penetrates it by about sqrt(darcy) elements; an
    // element's friction is its share of the body times that (an element the boundary cuts in two carries half).
    // It is set by the element, not by the body: a solid many times more impermeable than that is a wall already
    // when a sliver of it is in an element, the fraction then changes the flow only when it is nearly nothing, and
    // the sensitivities sit on that edge and mislead (the body zig-zagged; the finite-difference check read 3-5x).
    const double alphaMax = m_mu / (std::max(s.darcy, 1e-6) * h * h);
    // The force on the body from the flow just solved: F = sum_e alpha_e (V_e / 4) sum_i u_i
    auto bodyForce = [&]() {
        Vec3 F = Vec3::Zero();
        for (size_t a = 0; a < na; ++a)
        {
            if (!(m_alpha[a] > 0)) continue;
            Vec3 su = Vec3::Zero();
            for (int qn = 0; qn < 4; ++qn)
            {
                const size_t n = size_t(mesh.tets[a][size_t(qn)]);
                su += Vec3(m_u[3 * n], m_u[3 * n + 1], m_u[3 * n + 2]);
            }
            F += m_alpha[a] * vol[a] / 4.0 * su;
        }
        return F;
    };

    m_history.clear();
    m_dropHistory.clear();
    m_densityHistory.clear();
    m_steps.clear();
    m_stepStats.clear();
    const Options saved = m_opt;
    m_quiet = true;
    m_wantAdjoint = true;
    m_objDir = objDir;
    m_u.clear();
    m_p.clear();
    run_progress::Task task("optimising the body in the flow");
    const double stepLength = std::max(0.01, s.move) * h;
    double step = stepLength;
    int it = 0, rejected = 0;
    bool ok = true;
    std::vector<double> dc(na), gN, gPhi, gPrev, phiPrev, phiNew(nv), phiS(nv), phiT2, rho2;
    double Jprev = std::numeric_limits<double>::infinity();
    double tSolve = 0;                                      // (debug: the seconds of an iteration's flow and adjoint)
    std::chrono::steady_clock::time_point tStep;
    m_optRejected = 0;
    m_optStop = 0;
    for (; it < s.iterations; ++it)
    {
        if (cancel && cancel->load())
        {
            error = "cancelled";
            ok = false;
            break;
        }
        {
            char buf[64];
            if (rejected) snprintf(buf, sizeof(buf), "optimising, iteration %d of %d (%d taken back)", it + 1, s.iterations, rejected);
            else snprintf(buf, sizeof(buf), "optimising, iteration %d of %d", it + 1, s.iterations);
            task.set(double(it) / std::max(1, s.iterations), buf);
            task.span(double(it) / std::max(1, s.iterations), double(it + 1) / std::max(1, s.iterations));
        }
        phiTilde = phi;
        fractions(phiTilde, rho);
        m_alpha.assign(na, 0.0);
        for (size_t a = 0; a < na; ++a) m_alpha[a] = alphaMax * rho[a];
        m_time = 0;
        m_snapshot = true;
        const auto tA = std::chrono::steady_clock::now();
        const bool solved = solve(1e-9, error, cancel);
        m_snapshot = false;
        tSolve = std::chrono::duration<double>(std::chrono::steady_clock::now() - tA).count();
        tStep = std::chrono::steady_clock::now();
        if (!solved)
        {
            ok = false;
            break;
        }
        if (!m_steps.empty()) m_steps.back().iteration = it;
        const Vec3 F = bodyForce();
        const double J = F.dot(objDir);
        // A step that raised the objective was too long: back to the design before it, the step halved, the same
        // gradient (no new adjoint); the record of that design is dropped
        const bool backtrack = it > 0 && J > Jprev * (1.0 + 1e-3) && step > 0.02 * h && !gPrev.empty();
        if (backtrack)
        {
            if (!m_steps.empty()) { m_steps.pop_back(); m_stepStats.pop_back(); }
            phi = phiPrev;
            phiTilde = phi;
            fractions(phiTilde, rho);
            step *= 0.5;
            ++rejected;
            gPhi = gPrev;
            if (debug) fprintf(stderr, "[tetflow opt] iteration %d: J %.6g above %.6g -- step back, step now %.3f mm\n", it, J, Jprev, step);
        }
        else
        {
        m_history.push_back(F.dot(d));
        m_dropHistory.push_back(F.dot(l));
        m_densityHistory.emplace_back();
        nodal(phiTilde, m_densityHistory.back());

        // The sensitivities dJ/dalpha_e: the explicit part (V_e / 4) sum_i u_i . objDir, and the adjoint part
        // lambda . d(K U - F)/d alpha_e (the element's rows at alpha and at alpha + delta, closed-form)
        std::vector<double> adj(na, 0.0), explicitPart(na, 0.0);
        if (m_adjoint.size() == 4 * nv)
        {
            const Vec3 fb = m_rho * m_g;
            const bool convect = !m_opt.stokes;
            parallelRange(na, [&](size_t b0, size_t b1) {
                ElemRows e1, e2;
                for (size_t a = b0; a < b1; ++a)
                {
                    if (state[a]) continue;
                    const auto& tv = mesh.tets[a];
                    const double hea = std::cbrt(6.0 * std::sqrt(2.0) * vol[a]);
                    const double dd = 1e-4 * std::max(m_alpha[a], m_rho * 4.0 * (m_mu / m_rho) / (hea * hea));
                    Vec3 av[4], uo[4];
                    double pk[4], U[16];
                    Vec3 su = Vec3::Zero();
                    for (int qn = 0; qn < 4; ++qn)
                    {
                        const size_t n = size_t(tv[size_t(qn)]);
                        av[qn] = Vec3(m_u[3 * n], m_u[3 * n + 1], m_u[3 * n + 2]);
                        su += av[qn];
                        uo[qn] = Vec3::Zero();
                        pk[qn] = m_p[n];
                        for (int c = 0; c < 3; ++c) U[4 * qn + c] = m_u[3 * n + size_t(c)];
                        U[4 * qn + 3] = m_p[n];
                    }
                    explicitPart[a] = vol[a] / 4.0 * su.dot(objDir);
                    double sum = 0;
                    for (int li = 0; li < 4; ++li)
                    {
                        const size_t n = size_t(tv[size_t(li)]);
                        bool any = false;
                        for (int r = 0; r < 4; ++r) any = any || m_adjoint[4 * n + size_t(r)] != 0;
                        if (!any) continue;
                        elementRows(geom[a], li, hea, av, pk, convect, false, m_alpha[a], fb, m_mu, m_rho, 0.0, uo, e1);
                        elementRows(geom[a], li, hea, av, pk, convect, false, m_alpha[a] + dd, fb, m_mu, m_rho, 0.0, uo, e2);
                        for (int r = 0; r < 4; ++r)
                        {
                            const double lam = m_adjoint[4 * n + size_t(r)];
                            if (lam == 0) continue;
                            double dr = -(e2.b[r] - e1.b[r]);
                            for (int k = 0; k < 16; ++k) dr += (e2.r[r][k] - e1.r[r][k]) * U[k];
                            sum += lam * dr / dd;
                        }
                    }
                    adj[a] = sum;
                }
            }, 256);
        }
        // dJ/drho_e, then at the nodes of the smoothed level set through each cut element's fraction, then back
        // through the smoothing; along an extruded line, one value
        for (size_t a = 0; a < na; ++a) dc[a] = state[a] ? 0.0 : alphaMax * (explicitPart[a] + adj[a]);
        gN.assign(nv, 0.0);
        for (size_t a = 0; a < na; ++a)
        {
            if (state[a] || dc[a] == 0) continue;
            const auto& tv = mesh.tets[a];
            double u[4], gr[4];
            for (int p = 0; p < 4; ++p) u[p] = phiTilde[size_t(tv[size_t(p)])];
            fraction(u, gr);
            for (int p = 0; p < 4; ++p) gN[size_t(tv[size_t(p)])] += dc[a] * gr[p];
        }
        if (std::getenv("FIELDES_FLOW_FDCHECK") && it == (std::getenv("FIELDES_FLOW_FDCHECK_IT") ? std::atoi(std::getenv("FIELDES_FLOW_FDCHECK_IT")) : 0))
        {
            // dJ/dphiTilde_i against a finite difference: a node of the smoothed level set moved, the fractions
            // and the flow solved again -- the six nodes with the largest |g| and six spread over the cut ones
            const double delta = 0.05 * h;
            std::vector<size_t> order, pick;
            for (size_t i = 0; i < nv; ++i) if (gN[i] != 0) order.push_back(i);
            std::sort(order.begin(), order.end(), [&](size_t p, size_t q2) { return std::abs(gN[p]) > std::abs(gN[q2]); });
            for (size_t k = 0; k < order.size() && pick.size() < 6; ++k) pick.push_back(order[k]);
            for (size_t k = 0; k < 6 && !order.empty(); ++k) pick.push_back(order[(order.size() * (2 * k + 1)) / 12]);
            const std::vector<double> u0 = m_u, p0 = m_p;
            m_wantAdjoint = false;
            fprintf(stderr, "[tetflow fdcheck] J %.8g drag %.6g lift %.6g, alphaMax %.4g, %zu cut nodes\n", J, m_history.back(), m_dropHistory.back(), alphaMax, order.size());
            for (size_t i : pick)
            {
                std::vector<double> ph2 = phiTilde;
                ph2[i] += delta;
                fractions(ph2, rho2);
                for (size_t b2 = 0; b2 < na; ++b2) m_alpha[b2] = alphaMax * rho2[b2];
                m_u = u0;
                m_p = p0;
                std::string err2;
                if (!solve(1e-11, err2, cancel)) { fprintf(stderr, "[tetflow fdcheck]   solve failed: %s\n", err2.c_str()); continue; }
                const double J2 = bodyForce().dot(objDir);
                const double fd = (J2 - J) / delta;
                fprintf(stderr, "[tetflow fdcheck]   node %zu phi %.4f: g %.6g, finite difference %.6g, ratio %.3f\n",
                        i, phiTilde[i], gN[i], fd, gN[i] != 0 ? fd / gN[i] : 0.0);
            }
            for (size_t b2 = 0; b2 < na; ++b2) m_alpha[b2] = alphaMax * rho[b2];
            m_u = u0;
            m_p = p0;
            m_wantAdjoint = true;
        }
        // The direction: the nodal gradient smoothed (K^T K, symmetric, so still a descent direction); the shape
        // itself is never smoothed, so a nose or a tail can sharpen as far as the sensitivities ask
        {
            std::vector<double> gS;
            filter(gN, gS);
            filterT(gS, gPhi);
        }
        for (size_t i = 0; i < nv; ++i) if (nodeState[i]) gPhi[i] = 0.0;
        alongColumns(gPhi, false);
        // The volume's own gradient at the nodes, treated the same way, and the direction projected off it when
        // the volume is to be kept (the constrained steepest descent: at the optimum the two are proportional and
        // the direction vanishes; the offset after the step then only corrects what the curvature leaves)
        if (vMax - vMin < 1e-6 * bodyVolume)
        {
            std::vector<double> vN(nv, 0.0), vS, vPhi;
            for (size_t a = 0; a < na; ++a)
            {
                if (state[a]) continue;
                const auto& tv = mesh.tets[a];
                double u[4], gr[4];
                for (int p = 0; p < 4; ++p) u[p] = phiTilde[size_t(tv[size_t(p)])];
                fraction(u, gr);
                for (int p = 0; p < 4; ++p) vN[size_t(tv[size_t(p)])] += vol[a] * gr[p];
            }
            filter(vN, vS);
            filterT(vS, vPhi);
            for (size_t i = 0; i < nv; ++i) if (nodeState[i]) vPhi[i] = 0.0;
            alongColumns(vPhi, false);
            double gv = 0, vv = 0;
            for (size_t i = 0; i < nv; ++i)
            {
                gv += gPhi[i] * vPhi[i];
                vv += vPhi[i] * vPhi[i];
            }
            if (vv > 0) for (size_t i = 0; i < nv; ++i) gPhi[i] -= (gv / vv) * vPhi[i];
        }
        {
            size_t bad = 0;
            for (size_t i = 0; i < nv; ++i) if (!std::isfinite(gPhi[i])) { gPhi[i] = 0.0; ++bad; }
            if (bad && debug) fprintf(stderr, "[tetflow opt] iteration %d: %zu nodes of the direction were not finite (set to zero)\n", it, bad);
        }
        phiPrev = phi;
        gPrev = gPhi;
        Jprev = J;
        if (it > 0) step = std::min(stepLength, step * 1.25);
        }   // (a fresh gradient)
        double gmax = 0;
        for (size_t i = 0; i < nv; ++i) gmax = std::max(gmax, std::abs(gPhi[i]));
        if (!(gmax > 0))
        {
            if (debug) fprintf(stderr, "[tetflow opt] iteration %d: no sensitivity left\n", it);
            m_optStop = 3;
            ++it;
            break;
        }
        // The step: the level set rescaled to a slope of 1, then moved down the gradient by at most `move`
        // elements, then the body's volume brought within its bounds by one offset of the whole level set
        // (found by bisection: an offset of the level set is an offset of the boundary)
        const double slope = slopeOf(phiTilde);
        for (size_t i = 0; i < nv; ++i) phiNew[i] = nodeState[i] ? phi[i] : phi[i] / std::max(slope, 1e-9) - step * gPhi[i] / gmax;
        hold(phiNew);
        alongColumns(phiNew, true);
        auto volumeWithShift = [&](double c) {
            for (size_t i = 0; i < nv; ++i) phiS[i] = nodeState[i] ? phiNew[i] : phiNew[i] + c;
            fractions(phiS, rho2);
            return solidVolume(rho2);
        };
        double v0 = volumeWithShift(0.0);
        if (v0 < vMin || v0 > vMax)
        {
            const double target = v0 < vMin ? vMin : vMax;
            double lo = v0 < vMin ? 0.0 : -h, hi = v0 < vMin ? h : 0.0;
            for (int grow = 0; grow < 8 && ((v0 < vMin && volumeWithShift(hi) < target) || (v0 > vMax && volumeWithShift(lo) > target)); ++grow)
            {
                if (v0 < vMin) hi *= 2.0; else lo *= 2.0;
            }
            for (int bis = 0; bis < 60 && hi - lo > 1e-6 * h; ++bis)
            {
                const double c = 0.5 * (lo + hi);
                if (volumeWithShift(c) < target) lo = c; else hi = c;
            }
            volumeWithShift(0.5 * (lo + hi));
            phiNew = phiS;
        }
        // (how far the boundary moved: the change of the level set near it, the far field's rescale aside)
        double change = 0;
        for (size_t i = 0; i < nv; ++i)
            if (!nodeState[i] && std::abs(phi[i]) < 2.0 * h) change = std::max(change, std::abs(phiNew[i] - phi[i]));
        phi.swap(phiNew);
        if (debug && !backtrack)
            fprintf(stderr, "[tetflow opt] iteration %d: drag %.6g lift %.6g J %.6g, body volume %.4g of %.4g (%.3f), slope %.3f, step %.3f mm, moved %.3f mm; flow+adjoint %.1f s, sensitivities+step %.1f s\n",
                    it, m_history.back(), m_dropHistory.back(), J, solidVolume(rho), bodyVolume, solidVolume(rho) / bodyVolume, slope, step, change,
                    tSolve, std::chrono::duration<double>(std::chrono::steady_clock::now() - tStep).count());
        if (it >= 5 && !backtrack && change < 0.01 * h) { m_optStop = 1; ++it; break; }
        if (step <= 0.02 * h) { m_optStop = 2; ++it; break; }     // (steps this short make no progress)
    }
    m_optRejected = rejected;
    if (debug && rejected) fprintf(stderr, "[tetflow opt] %d steps taken back\n", rejected);
    m_opt = saved;
    m_quiet = false;
    m_wantAdjoint = false;
    if (!ok) return false;
    // the final flow around the final body, for the result's fields and the last step; the last step taken was
    // never checked, so if it raised the objective the design before it (the best accepted) is the result
    auto finalSolve = [&](double& J) {
        phiTilde = phi;
        fractions(phiTilde, rho);
        m_alpha.assign(na, 0.0);
        for (size_t a = 0; a < na; ++a) m_alpha[a] = alphaMax * rho[a];
        m_quiet = true;
        m_time = 0;
        m_snapshot = true;
        const bool fine = solve(1e-9, error, cancel);
        m_snapshot = false;
        m_quiet = false;
        if (!fine) return false;
        if (!m_steps.empty()) m_steps.back().iteration = it;
        J = bodyForce().dot(objDir);
        return true;
    };
    double Jfinal = 0;
    if (!finalSolve(Jfinal)) return false;
    if (!phiPrev.empty() && Jfinal > Jprev * (1.0 + 1e-3))
    {
        if (debug) fprintf(stderr, "[tetflow opt] the last step raised J to %.6g from %.6g: the design before it is the result\n", Jfinal, Jprev);
        if (!m_steps.empty()) { m_steps.pop_back(); m_stepStats.pop_back(); }
        phi = phiPrev;
        if (!finalSolve(Jfinal)) return false;
    }
    {
        const Vec3 F = bodyForce();
        m_history.push_back(F.dot(d));
        m_dropHistory.push_back(F.dot(l));
        m_densityHistory.emplace_back();
        nodal(phiTilde, m_densityHistory.back());
    }

    auto R2 = std::make_shared<MeshResult>();
    R2->mesh = m_mesh;
    R2->locator = m_locator ? m_locator : std::make_shared<TetLocator>(mesh);
    for (auto& fl : R2->fields) fl.assign(nv, 0.0f);
    R2->fields[0] = m_densityHistory.back();
    {
        float mn = 0, mx = 0;
        for (float v : R2->fields[0]) { mn = std::min(mn, v); mx = std::max(mx, v); }
        R2->minValue[0] = mn;
        R2->maxValue[0] = mx;
    }
    R2->iterations = it;
    R2->volume = solidVolume(rho);
    R2->elements = int(na);
    R2->seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    m_densityResult = R2;
    return true;
}

std::shared_ptr<const MeshResult> TetFlowProblem::densityResultAt(size_t k) const
{
    if (k >= m_densityHistory.size() || !m_densityResult) return nullptr;
    auto R = std::make_shared<MeshResult>();
    R->serial = derivedContentSerial(m_densityResult->serial, 7, k + 1);
    R->mesh = m_densityResult->mesh;
    R->locator = m_densityResult->locator;
    for (auto& f : R->fields) f.assign(m_densityHistory[k].size(), 0.0f);
    R->fields[0] = m_densityHistory[k];
    {
        float mn = 0, mx = 0;
        for (float v : R->fields[0]) { mn = std::min(mn, v); mx = std::max(mx, v); }
        R->minValue[0] = mn;
        R->maxValue[0] = mx;
    }
    R->iterations = int(k) + 1;
    R->elements = m_densityResult->elements;
    return R;
}

}   // namespace fea
}   // namespace libfive
