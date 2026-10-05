/*
libfive: a CAD kernel for modeling with implicit functions

Static linear-elastic FEA on a body-fitted tetrahedral mesh; see tetfea.hpp.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <numeric>
#include <unordered_map>

#include <boost/container/small_vector.hpp>

#include "libfive/fea/tetfea.hpp"
#include "libfive/tree/content_key.hpp"
#include "tet_common.hpp"
#include "libfive/eval/eval_array.hpp"
#include "libfive/eval/feature.hpp"
#include "libfive/oracle/oracle_clause.hpp"
#include "libfive/oracle/oracle_storage.hpp"
#include "libfive/run_progress.hpp"

namespace libfive {
namespace fea {

using namespace tet;

namespace {

const char* const kAxis[3] = {"x", "y", "z"};

}   // anonymous namespace

////////////////////////////////////////////////////////////////////////////////
// Locating points in the mesh

TetLocator::TetLocator(const TetMesh& mesh) : m_mesh(&mesh)
{
    const size_t nt = mesh.tets.size();
    if (mesh.pos.empty() || nt == 0) return;
    m_lo = m_hi = mesh.pos[0];
    for (const auto& p : mesh.pos)
    {
        m_lo = m_lo.cwiseMin(p);
        m_hi = m_hi.cwiseMax(p);
    }
    const Vec3 size = (m_hi - m_lo).cwiseMax(Vec3::Constant(1e-9));
    m_cell = std::max(mesh.h > 0 ? mesh.h : 1.0, std::cbrt(size.x() * size.y() * size.z() / 4e6));
    for (int a = 0; a < 3; ++a) m_n[a] = std::max(1, int(std::ceil(size[a] / m_cell)));

    m_inv.resize(nt);
    std::vector<int> lo(3 * nt), hi(3 * nt);
    const size_t nCells = size_t(m_n[0]) * m_n[1] * m_n[2];
    m_start.assign(nCells + 1, 0);
    for (size_t t = 0; t < nt; ++t)
    {
        const auto& v = mesh.tets[t];
        const Vec3& p0 = mesh.pos[size_t(v[0])];
        Eigen::Matrix3d T;
        T.col(0) = mesh.pos[size_t(v[1])] - p0;
        T.col(1) = mesh.pos[size_t(v[2])] - p0;
        T.col(2) = mesh.pos[size_t(v[3])] - p0;
        const Eigen::Matrix3d inv = T.inverse();
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c) m_inv[t].m[3 * r + c] = inv(r, c);
        for (int a = 0; a < 3; ++a) m_inv[t].p0[a] = p0[a];
        Vec3 bmin = p0, bmax = p0;
        for (int p = 1; p < 4; ++p)
        {
            bmin = bmin.cwiseMin(mesh.pos[size_t(v[size_t(p)])]);
            bmax = bmax.cwiseMax(mesh.pos[size_t(v[size_t(p)])]);
        }
        int l[3], h[3];
        cellRange(bmin, bmax, l, h);
        for (int a = 0; a < 3; ++a)
        {
            lo[3 * t + size_t(a)] = l[a];
            hi[3 * t + size_t(a)] = h[a];
        }
        for (int k = l[2]; k <= h[2]; ++k)
            for (int j = l[1]; j <= h[1]; ++j)
                for (int i = l[0]; i <= h[0]; ++i) m_start[cellIndex(i, j, k) + 1]++;
    }
    for (size_t c = 0; c < nCells; ++c) m_start[c + 1] += m_start[c];
    m_list.resize(m_start[nCells]);
    std::vector<uint32_t> cursor(m_start.begin(), m_start.end() - 1);
    for (size_t t = 0; t < nt; ++t)
        for (int k = lo[3 * t + 2]; k <= hi[3 * t + 2]; ++k)
            for (int j = lo[3 * t + 1]; j <= hi[3 * t + 1]; ++j)
                for (int i = lo[3 * t]; i <= hi[3 * t]; ++i) m_list[cursor[cellIndex(i, j, k)]++] = uint32_t(t);
}

void TetLocator::cellRange(const Vec3& lower, const Vec3& upper, int lo[3], int hi[3]) const
{
    for (int a = 0; a < 3; ++a)
    {
        lo[a] = std::max(0, std::min(m_n[a] - 1, int(std::floor((lower[a] - m_lo[a]) / m_cell))));
        hi[a] = std::max(0, std::min(m_n[a] - 1, int(std::floor((upper[a] - m_lo[a]) / m_cell))));
    }
}

template <class F>
bool TetLocator::forTetsIn(const Vec3& lower, const Vec3& upper, F&& fn) const
{
    const bool inside = (lower.array() >= m_lo.array() - 1e-9).all() && (upper.array() <= m_hi.array() + 1e-9).all();
    int lo[3], hi[3];
    cellRange(lower, upper, lo, hi);
    for (int k = lo[2]; k <= hi[2]; ++k)
        for (int j = lo[1]; j <= hi[1]; ++j)
            for (int i = lo[0]; i <= hi[0]; ++i)
            {
                const size_t c = cellIndex(i, j, k);
                for (uint32_t e = m_start[c]; e < m_start[c + 1]; ++e) fn(int(m_list[e]));
            }
    return inside;
}

int TetLocator::locate(const Vec3& p, double lambda[4]) const
{
    if (m_list.empty()) return -1;
    auto barycentric = [&](size_t t, double l[4]) {
        const Inv& iv = m_inv[t];
        const double d0 = p.x() - iv.p0[0], d1 = p.y() - iv.p0[1], d2 = p.z() - iv.p0[2];
        l[1] = iv.m[0] * d0 + iv.m[1] * d1 + iv.m[2] * d2;
        l[2] = iv.m[3] * d0 + iv.m[4] * d1 + iv.m[5] * d2;
        l[3] = iv.m[6] * d0 + iv.m[7] * d1 + iv.m[8] * d2;
        l[0] = 1.0 - l[1] - l[2] - l[3];
        return std::min(std::min(l[0], l[1]), std::min(l[2], l[3]));
    };
    int ci[3];
    for (int a = 0; a < 3; ++a)
        ci[a] = std::max(0, std::min(m_n[a] - 1, int(std::floor((p[a] - m_lo[a]) / m_cell))));
    // The cell's own tetrahedra
    double best[4] = {0, 0, 0, 0}, l[4];
    int bestTet = -1;
    double bestMin = -std::numeric_limits<double>::max();
    {
        const size_t c = cellIndex(ci[0], ci[1], ci[2]);
        for (uint32_t e = m_start[c]; e < m_start[c + 1]; ++e)
        {
            const double mn = barycentric(m_list[e], l);
            if (mn >= -1e-9)
            {
                std::copy(l, l + 4, lambda);
                return int(m_list[e]);
            }
            if (mn > bestMin)
            {
                bestMin = mn;
                bestTet = int(m_list[e]);
                std::copy(l, l + 4, best);
            }
        }
    }
    // Not inside it: the point is outside the mesh (or on a face the cell's
    // tetrahedra miss by rounding).  The nearest tetrahedron around, coordinates clamped.
    for (int r = 1; r <= 3 && (bestTet < 0 || bestMin < -0.5); ++r)
    {
        for (int k = std::max(0, ci[2] - r); k <= std::min(m_n[2] - 1, ci[2] + r); ++k)
            for (int j = std::max(0, ci[1] - r); j <= std::min(m_n[1] - 1, ci[1] + r); ++j)
                for (int i = std::max(0, ci[0] - r); i <= std::min(m_n[0] - 1, ci[0] + r); ++i)
                {
                    if (std::max(std::abs(i - ci[0]), std::max(std::abs(j - ci[1]), std::abs(k - ci[2]))) != r) continue;
                    const size_t c = cellIndex(i, j, k);
                    for (uint32_t e = m_start[c]; e < m_start[c + 1]; ++e)
                    {
                        const double mn = barycentric(m_list[e], l);
                        if (mn >= -1e-9)
                        {
                            std::copy(l, l + 4, lambda);
                            return int(m_list[e]);
                        }
                        if (mn > bestMin)
                        {
                            bestMin = mn;
                            bestTet = int(m_list[e]);
                            std::copy(l, l + 4, best);
                        }
                    }
                }
    }
    if (bestTet < 0) return -1;
    double sum = 0;
    for (int q = 0; q < 4; ++q)
    {
        lambda[q] = std::max(0.0, best[q]);
        sum += lambda[q];
    }
    if (!(sum > 0)) return -1;
    for (int q = 0; q < 4; ++q) lambda[q] /= sum;
    return bestTet;
}

int TetLocator::locateInside(const Vec3& p, double lambda[4]) const
{
    if (m_list.empty()) return -1;
    int ci[3];
    for (int a = 0; a < 3; ++a)
    {
        const double c = (p[a] - m_lo[a]) / m_cell;
        if (c < -1e-9 || c > m_n[a] + 1e-9) return -1;
        ci[a] = std::max(0, std::min(m_n[a] - 1, int(std::floor(c))));
    }
    // (the cell, then its neighbours: a point on a cell's face may belong to a tetrahedron listed next door)
    for (int r = 0; r <= 1; ++r)
        for (int k = std::max(0, ci[2] - r); k <= std::min(m_n[2] - 1, ci[2] + r); ++k)
            for (int j = std::max(0, ci[1] - r); j <= std::min(m_n[1] - 1, ci[1] + r); ++j)
                for (int i = std::max(0, ci[0] - r); i <= std::min(m_n[0] - 1, ci[0] + r); ++i)
                {
                    if (std::max(std::abs(i - ci[0]), std::max(std::abs(j - ci[1]), std::abs(k - ci[2]))) != r) continue;
                    const size_t c = cellIndex(i, j, k);
                    for (uint32_t e = m_start[c]; e < m_start[c + 1]; ++e)
                    {
                        const Inv& iv = m_inv[m_list[e]];
                        const double d0 = p.x() - iv.p0[0], d1 = p.y() - iv.p0[1], d2 = p.z() - iv.p0[2];
                        double l[4];
                        l[1] = iv.m[0] * d0 + iv.m[1] * d1 + iv.m[2] * d2;
                        l[2] = iv.m[3] * d0 + iv.m[4] * d1 + iv.m[5] * d2;
                        l[3] = iv.m[6] * d0 + iv.m[7] * d1 + iv.m[8] * d2;
                        l[0] = 1.0 - l[1] - l[2] - l[3];
                        if (std::min(std::min(l[0], l[1]), std::min(l[2], l[3])) >= -1e-7)
                        {
                            std::copy(l, l + 4, lambda);
                            return int(m_list[e]);
                        }
                    }
                }
    return -1;
}

void TetLocator::gradients(int t, Eigen::Matrix<double, 4, 3>& g) const
{
    // lambda_1..3 = inv * (p - p0): the rows of inv are their gradients
    const Inv& iv = m_inv[size_t(t)];
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) g(r + 1, c) = iv.m[3 * r + c];
    g.row(0) = -(g.row(1) + g.row(2) + g.row(3));
}

////////////////////////////////////////////////////////////////////////////////
// Result fields as Trees

namespace {

class MeshFieldOracle : public OracleStorage<>
{
public:
    MeshFieldOracle(std::shared_ptr<const MeshResult> r, int f) : res(std::move(r)), field(f) {}

    float sample(const Eigen::Vector3f& q, Eigen::Vector3f* grad = nullptr) const
    {
        const Vec3 p = q.cast<double>();
        double lam[4];
        const int t = res->locator->locate(p, lam);
        if (t < 0)
        {
            if (grad) grad->setZero();
            return 0.0f;
        }
        const auto& v = res->mesh->tets[size_t(t)];
        const auto& f = res->fields[field];
        double value = 0;
        for (int i = 0; i < 4; ++i) value += lam[i] * double(f[size_t(v[size_t(i)])]);
        if (grad)
        {
            Eigen::Matrix<double, 4, 3> g;
            res->locator->gradients(t, g);
            Vec3 gr = Vec3::Zero();
            for (int i = 0; i < 4; ++i) gr += double(f[size_t(v[size_t(i)])]) * g.row(i).transpose();
            *grad = gr.cast<float>();
        }
        return float(value);
    }

    void evalInterval(Interval& out) override
    {
        const auto& f = res->fields[field];
        float mn = std::numeric_limits<float>::max(), mx = -std::numeric_limits<float>::max();
        bool ok = false;
        size_t count = 0;
        const Vec3 lo = lower.cast<double>(), hi = upper.cast<double>();
        if (res->locator->forTetsIn(lo, hi, [&](int) { ++count; }) && count > 0 && count < 60000)
        {
            res->locator->forTetsIn(lo, hi, [&](int t) {
                for (int i = 0; i < 4; ++i)
                {
                    const float x = f[size_t(res->mesh->tets[size_t(t)][size_t(i)])];
                    mn = std::min(mn, x);
                    mx = std::max(mx, x);
                }
            });
            ok = true;
        }
        if (!ok)
        {
            mn = res->minValue[field];
            mx = res->maxValue[field];
            for (float x : f)
            {
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
    std::shared_ptr<const MeshResult> res;
    int field;
};

class MeshFieldClause : public OracleClause
{
public:
    MeshFieldClause(std::shared_ptr<const MeshResult> r, int f) : res(std::move(r)), field(f) {}
    std::unique_ptr<Oracle> getOracle() const override
    {
        return std::make_unique<MeshFieldOracle>(res, field);
    }
    std::string name() const override { return "tetfea_field_" + std::to_string(field); }
    std::string contentKey() const override
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "tetfea#%llu#%d", static_cast<unsigned long long>(res->serial), field);
        return buf;
    }

private:
    std::shared_ptr<const MeshResult> res;
    int field;
};

}   // anonymous namespace

Tree meshFieldTree(std::shared_ptr<const MeshResult> result, int field)
{
    if (!result || !result->locator || field < 0 || field >= Result::FIELD_COUNT) return Tree::invalid();
    return Tree(std::make_unique<MeshFieldClause>(std::move(result), field));
}

void MeshResult::elementValues(int field, std::vector<float>& out) const
{
    const size_t nt = mesh->tets.size();
    out.assign(nt, 0.0f);
    if (field < 0 || field >= Result::FIELD_COUNT) return;
    const bool displacement = field == Result::DISPLACEMENT || field == Result::UX ||
                              field == Result::UY || field == Result::UZ;
    parallelRange(nt, [&](size_t b0, size_t b1) {
        for (size_t t = b0; t < b1; ++t)
        {
            float v = 0;
            if (displacement)
            {
                Vec3 u = Vec3::Zero();
                for (int i = 0; i < 4; ++i)
                {
                    const size_t n = size_t(mesh->tets[t][size_t(i)]);
                    u += Vec3(fields[Result::UX][n], fields[Result::UY][n], fields[Result::UZ][n]);
                }
                u /= 4.0;
                v = float(field == Result::UX ? u.x() : field == Result::UY ? u.y()
                          : field == Result::UZ ? u.z() : u.norm());
            }
            else
            {
                const auto& s = elementStress[t];
                const double sxx = s[0], syy = s[1], szz = s[2], sxy = s[3], syz = s[4], szx = s[5];
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
                        const Vec3 pr = Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d>(S, Eigen::EigenvaluesOnly).eigenvalues();
                        v = float(field == Result::MAX_PRINCIPAL ? pr[2] : pr[0]);
                        break;
                    }
                    case Result::STRAIN_ENERGY: v = s[6]; break;
                    default: break;
                }
            }
            out[t] = v;
        }
    }, 256);
}

////////////////////////////////////////////////////////////////////////////////
// The problem

TetProblem::TetProblem(const Tree& shape, Eigen::Vector3d lo, Eigen::Vector3d hi, double h, double E, double nu)
    : m_shape(shape), m_lo(lo), m_hi(hi), m_h(h), m_E(E), m_nu(nu)
{
}

void TetProblem::addSupport(const Tree& region, bool x, bool y, bool z)
{
    m_supports.push_back(Support{region, {x, y, z}});
    m_prepared = false;
}

void TetProblem::addForce(const Tree& region, Eigen::Vector3d total, int loadCase)
{
    m_forces.push_back(Force{region, total, std::max(0, loadCase)});
    m_prepared = false;
}

void TetProblem::setGravity(Eigen::Vector3d g, double density)
{
    m_gravity = g;
    m_density = density;
    m_prepared = false;
}

void TetProblem::setThermal(const Tree& temperature, double alpha, double reference)
{
    m_temperature = temperature;
    m_alpha = alpha;
    m_reference = reference;
    m_prepared = false;
}

bool TetProblem::prepare(std::string& error)
{
    if (!(m_h > 0) || !(m_E > 0) || !(m_nu > -1 && m_nu < 0.5))
    {
        error = "invalid element size or material (need E > 0 and -1 < nu < 0.5)";
        return false;
    }
    run_progress::Task task("meshing");
    task.set(0.0, "meshing the part");
    auto mesh = std::make_shared<TetMesh>();
    if (!meshShape(m_shape, m_lo, m_hi, m_h, *mesh, error)) return false;
    task.set(0.5, "resolving supports and loads");

    // A node within a small fraction of an element of a region counts as inside it
    const float reach = float(0.05 * m_h);
    auto positions = [](const TetMesh& m) {
        std::vector<Eigen::Vector3f> pts(m.pos.size());
        for (size_t i = 0; i < pts.size(); ++i) pts[i] = m.pos[i].cast<float>();
        return pts;
    };
    // Per vertex: bit a set = held in direction a
    auto fixedMask = [&](const TetMesh& m, std::vector<unsigned char>& mask) {
        mask.assign(m.pos.size(), 0);
        const auto pts = positions(m);
        std::vector<float> v;
        for (const auto& s : m_supports)
        {
            evalTreePoints(s.region, pts, v);
            // (a region that misses the part by a little -- the surface of an optimised design, a
            // region placed by hand -- still holds it: the nodes within half an element)
            float rch = reach;
            bool any = false;
            for (float x : v) any = any || x <= rch;
            if (!any) rch = 10 * reach;
            for (size_t i = 0; i < pts.size(); ++i)
            {
                if (!(v[i] <= rch)) continue;
                for (int a = 0; a < 3; ++a) if (s.fix[a]) mask[i] |= (unsigned char)(1 << a);
            }
        }
        int count = 0;
        for (auto b : mask) count += b != 0;
        return count;
    };
    std::vector<unsigned char> mask;
    if (fixedMask(*mesh, mask) == 0)
    {
        error = m_supports.empty() ? "no supports: add fixed(region) touching the part"
                                   : "the support regions don't touch the part";
        return false;
    }

    // Material no support holds -- a loose piece, not connected to the part that
    // is held -- carries nothing and makes the system singular: left out, and counted
    m_looseElements = 0;
    {
        const size_t nv = mesh->pos.size();
        std::vector<uint32_t> parent(nv);
        std::iota(parent.begin(), parent.end(), 0u);
        auto find = [&](uint32_t x) {
            while (parent[x] != x)
            {
                parent[x] = parent[parent[x]];
                x = parent[x];
            }
            return x;
        };
        for (const auto& t : mesh->tets)
            for (int p = 1; p < 4; ++p)
            {
                const uint32_t a = find(uint32_t(t[0])), b = find(uint32_t(t[size_t(p)]));
                if (a != b) parent[a] = b;
            }
        std::vector<char> held(nv, 0);
        for (size_t i = 0; i < nv; ++i) if (mask[i]) held[find(uint32_t(i))] = 1;
        std::vector<std::array<int, 4>> kept;
        kept.reserve(mesh->tets.size());
        for (const auto& t : mesh->tets)
        {
            if (held[find(uint32_t(t[0]))]) kept.push_back(t);
            else m_looseElements++;
        }
        if (m_looseElements > 0)
        {
            mesh->tets.swap(kept);
            finalizeMesh(*mesh);
            if (fixedMask(*mesh, mask) == 0)
            {
                error = "the support regions don't touch the part";
                return false;
            }
        }
    }
    const size_t nv = mesh->pos.size(), nt = mesh->tets.size();
    const size_t nDof = 3 * nv;
    std::vector<unsigned char> fixedDof(nDof, 0);
    m_fixedNodes = 0;
    for (size_t i = 0; i < nv; ++i)
    {
        if (mask[i]) m_fixedNodes++;
        for (int a = 0; a < 3; ++a) if (mask[i] & (1 << a)) fixedDof[3 * i + size_t(a)] = 1;
    }

    // Loads
    const std::vector<Force> noForces;
    const std::vector<Force>& forces = m_noLoads ? noForces : m_forces;
    std::vector<double> force(nDof, 0.0);
    int nCases = 1;
    for (const auto& fo : forces) nCases = std::max(nCases, fo.loadCase + 1);
    // (with several load cases, each has its own forces; the static analysis applies all of them)
    std::vector<std::vector<double>> caseForce;
    if (nCases > 1) caseForce.assign(size_t(nCases), std::vector<double>(nDof, 0.0));
    auto addNodal = [&](size_t n, const Vec3& share, int lc) {
        for (int a = 0; a < 3; ++a)
        {
            force[3 * n + size_t(a)] += share[a];
            if (nCases > 1) caseForce[size_t(lc)][3 * n + size_t(a)] += share[a];
        }
    };
    m_totalLoad = Vec3::Zero();
    std::vector<char> loaded(nv, 0);
    {
        // boundary triangles: centres and areas
        std::vector<Eigen::Vector3f> centres(mesh->faces.size());
        std::vector<double> area(mesh->faces.size());
        for (size_t f = 0; f < mesh->faces.size(); ++f)
        {
            const auto& tri = mesh->faces[f];
            const Vec3 &a = mesh->pos[size_t(tri[0])], &b = mesh->pos[size_t(tri[1])], &c = mesh->pos[size_t(tri[2])];
            centres[f] = ((a + b + c) / 3.0).cast<float>();
            area[f] = 0.5 * (b - a).cross(c - a).norm();
        }
        const auto pts = positions(*mesh);
        for (const auto& fo : forces)
        {
            std::vector<float> v, vn;
            evalTreePoints(fo.region, pts, vn);
            bool placed = false;
            // (the region must reach the part; one that misses it by a little -- the surface of an optimised
            // design -- still loads it: the second pass takes what lies within half an element)
            for (int pass = 0; pass < 2 && !placed; ++pass)
            {
                const float rch = pass == 0 ? reach : 10 * reach;
                // The boundary triangles the region covers: those with all three corners inside it (a
                // triangle on the side of a bar, next to a loaded end, has one corner inside and is
                // not the end's); if there are none, those whose centre is inside
                std::vector<char> take(mesh->faces.size(), 0);
                double sel = 0;
                for (size_t f = 0; f < take.size(); ++f)
                {
                    const auto& tri = mesh->faces[f];
                    take[f] = vn[size_t(tri[0])] <= rch && vn[size_t(tri[1])] <= rch && vn[size_t(tri[2])] <= rch;
                    if (take[f]) sel += area[f];
                }
                if (!(sel > 0))
                {
                    evalTreePoints(fo.region, centres, v);
                    for (size_t f = 0; f < take.size(); ++f)
                    {
                        take[f] = v[f] <= rch;
                        if (take[f]) sel += area[f];
                    }
                }
                if (sel > 0)
                {
                    for (size_t f = 0; f < take.size(); ++f)
                    {
                        if (!take[f]) continue;
                        const Vec3 share = fo.total * (area[f] / sel) / 3.0;
                        for (int p = 0; p < 3; ++p)
                        {
                            const size_t n = size_t(mesh->faces[f][size_t(p)]);
                            addNodal(n, share, fo.loadCase);
                            loaded[n] = 1;
                        }
                    }
                    placed = true;
                    continue;
                }
                // a region holding no surface: over the nodes inside it
                size_t count = 0;
                for (float x : vn) count += x <= rch;
                if (count == 0) continue;
                const Vec3 share = fo.total / double(count);
                for (size_t n = 0; n < nv; ++n)
                {
                    if (!(vn[n] <= rch)) continue;
                    addNodal(n, share, fo.loadCase);
                    loaded[n] = 1;
                }
                placed = true;
            }
            if (!placed)
            {
                error = "a load region doesn't touch the part";
                if (m_looseElements > 0)
                    error += " (" + std::to_string(m_looseElements) + " tetrahedra of material that no support holds were "
                             "left out: the region may lie on them -- the part may be in more than one piece)";
                return false;
            }
        }
    }
    std::vector<TetGeom> geom(nt);
    parallelRange(nt, [&](size_t b0, size_t b1) {
        for (size_t t = b0; t < b1; ++t)
        {
            const auto& v = mesh->tets[t];
            geom[t] = geometryOf(mesh->pos[size_t(v[0])], mesh->pos[size_t(v[1])], mesh->pos[size_t(v[2])],
                                 mesh->pos[size_t(v[3])]);
        }
    }, 256);
    // Gravity: each element's weight, a quarter to each of its nodes
    if (!m_noLoads && m_density > 0 && m_gravity.norm() > 0)
    {
        for (size_t t = 0; t < nt; ++t)
        {
            const Vec3 share = m_gravity * (m_density * geom[t].vol / 4.0);
            for (int p = 0; p < 4; ++p)
            {
                const size_t n = size_t(mesh->tets[t][size_t(p)]);
                for (int a = 0; a < 3; ++a) force[3 * n + size_t(a)] += share[a];
                if (nCases > 1)
                    for (auto& cf : caseForce)              // (gravity acts in every case)
                        for (int a = 0; a < 3; ++a) cf[3 * n + size_t(a)] += share[a];
            }
        }
    }
    // Thermal expansion: the strain alpha (T - reference), at each element's centre
    m_thermalStrain.clear();
    if (!m_noLoads && m_temperature.is_valid())
    {
        std::vector<Eigen::Vector3f> centres(nt);
        for (size_t t = 0; t < nt; ++t)
        {
            Vec3 c = Vec3::Zero();
            for (int p = 0; p < 4; ++p) c += mesh->pos[size_t(mesh->tets[t][size_t(p)])];
            centres[t] = (c / 4.0).cast<Eigen::Vector3f::Scalar>();
        }
        std::vector<float> T;
        evalTreePoints(m_temperature, centres, T);
        m_thermalStrain.resize(nt);
        const Mat6 D = elasticity(m_E, m_nu);
        const double bulk = D(0, 0) + 2 * D(0, 1);      // (3 lambda + 2 mu)
        for (size_t t = 0; t < nt; ++t)
        {
            if (!std::isfinite(T[t]))
            {
                error = "the temperature field isn't defined everywhere in the part";
                return false;
            }
            const double e = m_alpha * (double(T[t]) - m_reference);
            m_thermalStrain[t] = e;
            for (int p = 0; p < 4; ++p)
            {
                const size_t n = size_t(mesh->tets[t][size_t(p)]);
                for (int a = 0; a < 3; ++a) force[3 * n + size_t(a)] += geom[t].vol * bulk * e * geom[t].g[p][a];
            }
        }
    }
    m_loadedNodes = 0;
    for (char l : loaded) m_loadedNodes += l != 0;
    for (const auto& fo : forces) m_totalLoad += fo.total;

    // What the loads and the supports leave: something must push, and every
    // direction that is pushed must be held somewhere
    if (!m_noLoads)
    {
        double freeLoad = 0, anyLoad = 0;
        for (size_t d = 0; d < nDof; ++d)
        {
            anyLoad += std::abs(force[d]);
            if (!fixedDof[d]) freeLoad += std::abs(force[d]);
        }
        if (!(anyLoad > 0))
        {
            error = (m_forces.empty() && !(m_density > 0) && m_thermalStrain.empty())
                        ? "no loads: add force(region, ...), gravity() or thermal_expansion(...)"
                        : "the loads have no effect: a load region may only touch the supports";
            return false;
        }
        if (!(freeLoad > 0))
        {
            error = "the loads act only on fixed nodes: move the load region off the supports";
            return false;
        }
        for (int a = 0; a < 3; ++a)
        {
            bool held = false;
            double net = 0, scale = 0;
            for (size_t i = size_t(a); i < nDof; i += 3)
            {
                held = held || fixedDof[i];
                net += force[i];
                scale += std::abs(force[i]);
            }
            if (!held && std::abs(net) > 1e-9 * std::max(scale, 1e-30))
            {
                error = std::string("nothing holds the part in the ") + kAxis[a] +
                        " direction, but the loads push it that way: fix it in " +
                        kAxis[a] + " somewhere (fixed(region) holds all directions)";
                return false;
            }
        }
    }

    uint64_t hsh = 1469598103934665603ull;
    const double hdr[] = {m_h, m_E, m_nu, double(nv), double(nt), m_alpha, m_reference};
    hsh = fnv(hsh, hdr, sizeof(hdr));
    hsh = fnv(hsh, mesh->pos.data(), mesh->pos.size() * sizeof(Vec3));
    hsh = fnv(hsh, mesh->tets.data(), mesh->tets.size() * sizeof(std::array<int, 4>));
    hsh = fnv(hsh, fixedDof.data(), fixedDof.size());
    hsh = fnv(hsh, force.data(), force.size() * sizeof(double));
    for (const auto& cf : caseForce) hsh = fnv(hsh, cf.data(), cf.size() * sizeof(double));
    if (!m_thermalStrain.empty()) hsh = fnv(hsh, m_thermalStrain.data(), m_thermalStrain.size() * sizeof(double));
    m_hash = hsh;

    m_mesh = mesh;
    m_fixedDof.swap(fixedDof);
    m_force.swap(force);
    m_caseForce.swap(caseForce);
    m_result = std::make_shared<MeshResult>();
    m_result->mesh = m_mesh;
    m_result->looseElements = m_looseElements;
    m_result->totalLoad = m_totalLoad;
    m_prepared = true;
    return true;
}

bool TetProblem::solve(int maxIterations, double tolerance, std::string& error, const std::atomic<bool>* cancel)
{
    if (!m_prepared && !prepare(error)) return false;
    const auto t0 = std::chrono::steady_clock::now();
    run_progress::Task task("solving");
    task.set(0.0, "assembling the stiffness matrix");

    const TetMesh& mesh = *m_mesh;
    const size_t nv = mesh.pos.size(), nt = mesh.tets.size(), n = 3 * nv;
    const Mat6 D = elasticity(m_E, m_nu);
    Assembly A;
    buildPattern(mesh, A);
    assemble(mesh, D, nullptr, A);
    buildPreconditioner(m_fixedDof, A);
    task.set(0.05, "preparing the solver");

    const std::vector<unsigned char>& fixed = m_fixedDof;
    std::vector<double> u(n, 0.0);
    CgResult cg;
    if (!cgSolve(A, fixed, m_force, u, tolerance, maxIterations, cancel,
                 [&](double frac, const std::string& text) { task.set(0.05 + 0.9 * frac, text); }, cg, error))
        return false;
    const int it = cg.iterations;
    const double rel = cg.residual;
    if (rel > std::max(tolerance * 100, 1e-3))
    {
        error = "the solver did not converge (" + std::to_string(it) + " iterations, residual " +
                std::to_string(rel) + "): the supports may not hold the part in place";
        return false;
    }
    if (std::getenv("FIELDES_FEA_DEBUG"))
        fprintf(stderr, "[tetfea] %zu vertices, %zu tetrahedra, %d iterations, residual %.2e\n", nv, nt, it, rel);

    // Held in every direction but still free to turn: the solution drifts into a
    // huge rigid rotation.  Real deflections are far smaller than the part.
    {
        Vec3 lo = mesh.pos[0], hi = mesh.pos[0];
        for (const auto& q : mesh.pos)
        {
            lo = lo.cwiseMin(q);
            hi = hi.cwiseMax(q);
        }
        double umax = 0;
        for (size_t i = 0; i < n; ++i) umax = std::max(umax, std::abs(u[i]));
        if (!(umax < 10 * (hi - lo).maxCoeff()))
        {
            error = "the supports don't hold the part: fix more of it, or in more directions";
            return false;
        }
    }

    // Reactions at the supports, compliance
    std::vector<double> Ku(n);
    A.K.matvec(u.data(), Ku.data());
    Vec3 reaction = Vec3::Zero();
    double compliance = 0;
    for (size_t i = 0; i < n; ++i)
    {
        if (fixed[i]) reaction[int(i % 3)] += Ku[i] - m_force[i];
        compliance += m_force[i] * u[i];
    }

    // Each element's stress from its own strain, then the nodal averages
    auto res = std::make_shared<MeshResult>();
    res->mesh = m_mesh;
    res->elementStress.resize(nt);
    parallelRange(nt, [&](size_t b0, size_t b1) {
        for (size_t t = b0; t < b1; ++t)
        {
            double ue[12];
            for (int p = 0; p < 4; ++p)
                for (int a = 0; a < 3; ++a) ue[3 * p + a] = u[3 * size_t(mesh.tets[t][size_t(p)]) + size_t(a)];
            Vec6 eps = strainOf(A.geom[t], ue);
            if (!m_thermalStrain.empty())
                for (int q = 0; q < 3; ++q) eps[q] -= m_thermalStrain[t];       // (the stress-free part)
            const Vec6 sig = D * eps;
            for (int q = 0; q < 6; ++q) res->elementStress[t][size_t(q)] = float(sig[q]);
            res->elementStress[t][6] = float(0.5 * sig.dot(eps));
        }
    }, 256);

    for (auto& f : res->fields) f.assign(nv, 0.0f);
    parallelRange(nv, [&](size_t b0, size_t b1) {
        for (size_t i = b0; i < b1; ++i)
        {
            double w = 0, acc[7] = {0, 0, 0, 0, 0, 0, 0};
            for (uint32_t e = A.vtStart[i]; e < A.vtStart[i + 1]; ++e)
            {
                const size_t t = A.vtList[e];
                const double vol = A.geom[t].vol;
                w += vol;
                for (int q = 0; q < 7; ++q) acc[q] += vol * double(res->elementStress[t][size_t(q)]);
            }
            if (!(w > 0)) continue;
            const double sxx = acc[0] / w, syy = acc[1] / w, szz = acc[2] / w, sxy = acc[3] / w, syz = acc[4] / w,
                         szx = acc[5] / w;
            const double vm = std::sqrt(0.5 * ((sxx - syy) * (sxx - syy) + (syy - szz) * (syy - szz) +
                                               (szz - sxx) * (szz - sxx)) +
                                        3 * (sxy * sxy + syz * syz + szx * szx));
            Eigen::Matrix3d S;
            S << sxx, sxy, szx, sxy, syy, syz, szx, syz, szz;
            const Vec3 pr = Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d>(S, Eigen::EigenvaluesOnly).eigenvalues();
            res->fields[Result::VON_MISES][i] = float(vm);
            res->fields[Result::SXX][i] = float(sxx);
            res->fields[Result::SYY][i] = float(syy);
            res->fields[Result::SZZ][i] = float(szz);
            res->fields[Result::SXY][i] = float(sxy);
            res->fields[Result::SYZ][i] = float(syz);
            res->fields[Result::SZX][i] = float(szx);
            res->fields[Result::MAX_PRINCIPAL][i] = float(pr[2]);
            res->fields[Result::MIN_PRINCIPAL][i] = float(pr[0]);
            res->fields[Result::STRAIN_ENERGY][i] = float(acc[6] / w);
            const double ux = u[3 * i], uy = u[3 * i + 1], uz = u[3 * i + 2];
            res->fields[Result::UX][i] = float(ux);
            res->fields[Result::UY][i] = float(uy);
            res->fields[Result::UZ][i] = float(uz);
            res->fields[Result::DISPLACEMENT][i] = float(std::sqrt(ux * ux + uy * uy + uz * uz));
        }
    }, 64);
    for (int f = 0; f < Result::FIELD_COUNT; ++f)
    {
        float mn = std::numeric_limits<float>::max(), mx = -std::numeric_limits<float>::max();
        for (float x : res->fields[f])
        {
            mn = std::min(mn, x);
            mx = std::max(mx, x);
        }
        res->minValue[f] = mn;
        res->maxValue[f] = mx;
    }
    res->locator = std::make_shared<TetLocator>(mesh);
    res->elements = int(nt);
    res->nodes = int(nv);
    res->dofs = int(n);
    res->fixedNodes = m_fixedNodes;
    res->loadedNodes = m_loadedNodes;
    res->looseElements = m_looseElements;
    res->iterations = it;
    res->residual = rel;
    res->compliance = compliance;
    res->reaction = reaction;
    res->totalLoad = m_totalLoad;
    double vol = 0;
    for (size_t t = 0; t < nt; ++t) vol += A.geom[t].vol;
    res->volume = vol;
    res->seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    m_result = res;
    return true;
}

bool TetProblem::optimize(const TopOpt& s, std::string& error, const std::atomic<bool>* cancel)
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
    const TetMesh& mesh = *m_mesh;
    const size_t nv = mesh.pos.size(), na = mesh.tets.size(), n = 3 * nv;
    const double h = m_h;
    const Mat6 D = elasticity(m_E, m_nu);
    Assembly A;
    buildPattern(mesh, A);

    // Element centres, and which elements are fixed solid / empty
    std::vector<Eigen::Vector3f> centres(na);
    std::vector<Vec3> cen(na);
    for (size_t a = 0; a < na; ++a)
    {
        Vec3 c = Vec3::Zero();
        for (int p = 0; p < 4; ++p) c += mesh.pos[size_t(mesh.tets[a][size_t(p)])];
        cen[a] = c / 4.0;
        centres[a] = cen[a].cast<float>();
    }
    std::vector<char> state(na, 0);             // 0 design, 1 solid, 2 empty
    std::vector<float> vals;
    for (const auto& t : s.avoid)
    {
        evalTreePoints(t, centres, vals);
        for (size_t a = 0; a < na; ++a) if (vals[a] < 0) state[a] = 2;
    }
    for (const auto& t : s.keep)
    {
        evalTreePoints(t, centres, vals);
        for (size_t a = 0; a < na; ++a) if (vals[a] < 0) state[a] = 1;
    }
    // Elements that hold the supports and loads stay solid
    for (size_t a = 0; a < na; ++a)
        for (int p = 0; p < 4 && state[a] != 1; ++p)
        {
            const size_t d = 3 * size_t(mesh.tets[a][size_t(p)]);
            for (int q = 0; q < 3; ++q)
                if (m_fixedDof[d + size_t(q)] || m_force[d + size_t(q)] != 0) state[a] = 1;
        }

    // Extrusion: the design elements at each (x, y) (or the like) share one value -- their sensitivities
    // summed -- so the design is a profile extruded through
    std::vector<int> column(na, -1);
    int nCol = 0;
    if (s.extrude >= 0 && s.extrude <= 2)
    {
        const int u0 = s.extrude == 0 ? 1 : 0, u1 = s.extrude == 2 ? 1 : 2;
        Vec3 lo = cen[0];
        for (const auto& c : cen) lo = lo.cwiseMin(c);
        std::unordered_map<int64_t, int> id;
        const double bin = 0.5 * h;
        for (size_t a = 0; a < na; ++a)
        {
            if (state[a]) continue;
            const int64_t key = int64_t(std::floor((cen[a][u0] - lo[u0]) / bin + 1e-6)) * 1000003 +
                                int64_t(std::floor((cen[a][u1] - lo[u1]) / bin + 1e-6));
            auto it = id.find(key);
            if (it == id.end()) it = id.emplace(key, nCol++).first;
            column[a] = it->second;
        }
    }

    // Filter: weights rmin - distance over the elements within rmin, times their volume
    const double rmin = s.filterRadius > 0 ? s.filterRadius : 1.5 * h;
    std::vector<size_t> fStart(na + 1, 0);
    std::vector<uint32_t> fIdx;
    std::vector<float> fW;
    std::vector<double> Hs(na, 0.0);
    {
        Vec3 lo = cen[0], hi = cen[0];
        for (const auto& c : cen)
        {
            lo = lo.cwiseMin(c);
            hi = hi.cwiseMax(c);
        }
        int dims[3];
        double cellSize = rmin;
        for (int a = 0; a < 3; ++a) dims[a] = std::max(1, int(std::floor((hi[a] - lo[a]) / cellSize)) + 1);
        auto cellOf = [&](const Vec3& c, int ijk[3]) {
            for (int a = 0; a < 3; ++a) ijk[a] = std::max(0, std::min(dims[a] - 1, int(std::floor((c[a] - lo[a]) / cellSize))));
        };
        const size_t nCells = size_t(dims[0]) * dims[1] * dims[2];
        std::vector<uint32_t> cStart(nCells + 1, 0), cList(na);
        for (size_t a = 0; a < na; ++a)
        {
            int c[3];
            cellOf(cen[a], c);
            cStart[(size_t(c[2]) * dims[1] + c[1]) * dims[0] + c[0] + 1]++;
        }
        for (size_t c = 0; c < nCells; ++c) cStart[c + 1] += cStart[c];
        {
            std::vector<uint32_t> cursor(cStart.begin(), cStart.end() - 1);
            for (size_t a = 0; a < na; ++a)
            {
                int c[3];
                cellOf(cen[a], c);
                cList[cursor[(size_t(c[2]) * dims[1] + c[1]) * dims[0] + c[0]]++] = uint32_t(a);
            }
        }
        std::vector<double> vol(na);
        for (size_t a = 0; a < na; ++a) vol[a] = A.geom[a].vol;
        for (size_t a = 0; a < na; ++a)
        {
            int c[3];
            cellOf(cen[a], c);
            for (int dk = -1; dk <= 1; ++dk)
                for (int dj = -1; dj <= 1; ++dj)
                    for (int di = -1; di <= 1; ++di)
                    {
                        const int i2 = c[0] + di, j2 = c[1] + dj, k2 = c[2] + dk;
                        if (i2 < 0 || j2 < 0 || k2 < 0 || i2 >= dims[0] || j2 >= dims[1] || k2 >= dims[2]) continue;
                        const size_t cc = (size_t(k2) * dims[1] + j2) * dims[0] + i2;
                        for (uint32_t e = cStart[cc]; e < cStart[cc + 1]; ++e)
                        {
                            const uint32_t q = cList[e];
                            const double w = rmin - (cen[q] - cen[a]).norm();
                            if (w <= 0) continue;
                            fIdx.push_back(q);
                            fW.push_back(float(w * vol[q]));
                            Hs[a] += w * vol[q];
                        }
                    }
            fStart[a + 1] = fIdx.size();
        }
    }
    auto filter = [&](const std::vector<double>& in, std::vector<double>& out) {
        out.resize(na);
        parallelRange(na, [&](size_t b0, size_t b1) {
            for (size_t a = b0; a < b1; ++a)
            {
                double sum = 0;
                for (size_t q = fStart[a]; q < fStart[a + 1]; ++q) sum += double(fW[q]) * in[fIdx[q]];
                out[a] = sum / Hs[a];
            }
        }, 1024);
    };
    // the transpose, for the sensitivities: out_b = sum_a w_ab in_a / Hs_a
    auto filterT = [&](const std::vector<double>& in, std::vector<double>& out) {
        out.assign(na, 0.0);
        for (size_t a = 0; a < na; ++a)
        {
            const double f = in[a] / Hs[a];
            for (size_t q = fStart[a]; q < fStart[a + 1]; ++q) out[fIdx[q]] += double(fW[q]) * f;
        }
    };

    std::vector<double> vol(na);
    double total = 0;
    for (size_t a = 0; a < na; ++a)
    {
        vol[a] = A.geom[a].vol;
        total += vol[a];
    }
    const double target = s.volumeFraction * total;
    const double p = s.penalty, emin = s.minStiffness;

    std::vector<double> x(na, s.volumeFraction), xPhys, dc(na), dv(na), dcF, dvF, xNew(na);
    for (size_t a = 0; a < na; ++a) if (state[a]) x[a] = state[a] == 1 ? 1.0 : 0.0;
    auto physical = [&](const std::vector<double>& xs, std::vector<double>& out) {
        filter(xs, out);
        for (size_t a = 0; a < na; ++a)
        {
            if (state[a] == 1) out[a] = 1.0;
            else if (state[a] == 2) out[a] = 0.0;
        }
    };
    m_history.clear();
    m_densityHistory.clear();
    // The density at the nodes (each tetrahedron's averaged at its nodes by volume): the field of an iteration
    auto nodal = [&](const std::vector<double>& xs, std::vector<float>& out) {
        out.assign(nv, 0.0f);
        parallelRange(nv, [&](size_t b0, size_t b1) {
            for (size_t i = b0; i < b1; ++i)
            {
                double sum = 0, w = 0;
                for (uint32_t e = A.vtStart[i]; e < A.vtStart[i + 1]; ++e)
                {
                    const size_t t = A.vtList[e];
                    sum += vol[t] * xs[t];
                    w += vol[t];
                }
                out[i] = w > 0 ? float(sum / w) : 0.0f;
            }
        }, 256);
    };
    // Load cases: each solved on its own (its own warm start), the sensitivities summed -- the part
    // stiff for all of them
    const int nc = std::max<int>(1, int(m_caseForce.size()));
    std::vector<std::vector<double>> caseU(static_cast<size_t>(nc));
    std::vector<double> scale(na);
    run_progress::Task task("optimising");
    buildPreconditioner(m_fixedDof, A);        // (sized; refreshed each iteration)

    int it = 0;
    for (; it < s.iterations; ++it)
    {
        if (cancel && cancel->load())
        {
            error = "cancelled";
            return false;
        }
        {
            char buf[64];
            snprintf(buf, sizeof(buf), "optimising, iteration %d of %d", it + 1, s.iterations);
            task.set(double(it) / std::max(1, s.iterations), buf);
        }
        physical(x, xPhys);
        for (size_t a = 0; a < na; ++a) scale[a] = emin + (1 - emin) * std::pow(xPhys[a], p);
        assemble(mesh, D, &scale, A);
        buildPreconditioner(m_fixedDof, A);
        std::fill(dc.begin(), dc.end(), 0.0);
        double compliance = 0.0;
        for (int lc = 0; lc < nc; ++lc)
        {
            const std::vector<double>& f = nc > 1 ? m_caseForce[size_t(lc)] : m_force;
            std::vector<double>& u = caseU[size_t(lc)];
            CgResult cg;
            // (the sensitivities don't need a tight solve; warm starts keep each one short)
            if (!cgSolve(A, m_fixedDof, f, u, std::max(s.tolerance, 1e-4), s.solverIterations, cancel, nullptr, cg, error))
                return false;
            double c = 0;
            for (size_t i = 0; i < n; ++i) c += f[i] * u[i];
            compliance += c;
            // compliance sensitivities from each element's strain energy
            parallelRange(na, [&](size_t b0, size_t b1) {
                for (size_t a = b0; a < b1; ++a)
                {
                    double ue[12];
                    for (int q = 0; q < 4; ++q)
                        for (int d = 0; d < 3; ++d) ue[3 * q + d] = u[3 * size_t(mesh.tets[a][size_t(q)]) + size_t(d)];
                    const Vec6 eps = strainOf(A.geom[a], ue);
                    const double ce = A.geom[a].vol * eps.dot(D * eps);
                    dc[a] += -p * std::pow(std::max(xPhys[a], 1e-9), p - 1) * (1 - emin) * ce;
                    dv[a] = vol[a];
                }
            }, 512);
        }
        m_history.push_back(compliance);
        m_densityHistory.emplace_back();
        nodal(xPhys, m_densityHistory.back());
        filterT(dc, dcF);
        filterT(dv, dvF);
        if (nCol)
        {
            // (one value per line: the line's sensitivities summed; x stays equal along each line, starting equal)
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
                xNew[a] = std::max(0.0, std::max(x[a] - s.move, std::min(1.0, std::min(x[a] + s.move, x[a] * B))));
            }
            physical(xNew, xp);
            double v = 0;
            for (size_t a = 0; a < na; ++a) v += vol[a] * xp[a];
            if (v > target) l1 = lm; else l2 = lm;
        }
        double change = 0;
        for (size_t a = 0; a < na; ++a) change = std::max(change, std::abs(xNew[a] - x[a]));
        x.swap(xNew);
        // converged: the design stopped moving, or the compliance stopped improving over the last 5 iterations
        const size_t H = m_history.size();
        const bool flat = H > 6 && std::abs(m_history[H - 1] - m_history[H - 6]) < 2e-3 * std::abs(m_history[H - 1]);
        if (std::getenv("FIELDES_FEA_DEBUG"))
            fprintf(stderr, "[tetopt] iteration %d compliance %.6g change %.4f\n", it, compliance, change);
        if (it >= 15 && (change < 0.01 || flat)) { ++it; break; }
    }
    physical(x, xPhys);

    // The density per tetrahedron, and as a field on the mesh (averaged at the nodes by volume)
    m_topDensity.assign(na, 0.0f);
    for (size_t a = 0; a < na; ++a) m_topDensity[a] = float(xPhys[a]);
    auto R2 = std::make_shared<MeshResult>();
    R2->mesh = m_mesh;
    R2->locator = std::make_shared<TetLocator>(mesh);
    for (auto& f : R2->fields) f.assign(nv, 0.0f);
    nodal(xPhys, R2->fields[0]);
    R2->minValue[0] = 0;
    R2->maxValue[0] = 1;
    R2->iterations = it;
    R2->compliance = m_history.empty() ? 0 : m_history.back();
    double v = 0;
    for (size_t a = 0; a < na; ++a) v += vol[a] * xPhys[a];
    R2->volume = v;
    R2->elements = int(na);
    R2->seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    m_densityResult = R2;
    return true;
}

std::shared_ptr<const MeshResult> TetProblem::densityResultAt(size_t k) const
{
    if (k >= m_densityHistory.size() || !m_densityResult) return nullptr;
    auto R = std::make_shared<MeshResult>();
    R->serial = derivedContentSerial(m_densityResult->serial, 7, k + 1);
    R->mesh = m_densityResult->mesh;
    R->locator = m_densityResult->locator;
    for (auto& f : R->fields) f.assign(m_densityHistory[k].size(), 0.0f);
    R->fields[0] = m_densityHistory[k];
    R->minValue[0] = 0;
    R->maxValue[0] = 1;
    R->iterations = int(k) + 1;
    R->compliance = k < m_history.size() ? m_history[k] : 0;
    R->elements = m_densityResult->elements;
    return R;
}

int TetProblem::pieces(double threshold, double margin) const
{
    // The part is where the density at the nodes -- the field the optimised part is cut from, linear in each
    // tetrahedron -- is above the level.  In a tetrahedron that is a convex piece holding its nodes above the level, so
    // two such nodes of one tetrahedron are in the same piece, and the pieces are the groups these links make.  A node
    // only just above the level makes a hair-thin link that no picture shows: it counts when it is above by `margin`
    if (!m_mesh || !m_densityResult || m_densityResult->fields[0].size() != m_mesh->pos.size()) return 0;
    const TetMesh& mesh = *m_mesh;
    const std::vector<float>& dens = m_densityResult->fields[0];
    const size_t nv = mesh.pos.size();
    std::vector<int> parent(nv);
    std::iota(parent.begin(), parent.end(), 0);
    auto find = [&](int a) {
        while (parent[size_t(a)] != a)
        {
            parent[size_t(a)] = parent[size_t(parent[size_t(a)])];
            a = parent[size_t(a)];
        }
        return a;
    };
    std::vector<double> weight(nv, 0.0);       // each node's share of the volume around it
    for (const auto& t : mesh.tets)
    {
        const Vec3 p0 = mesh.pos[size_t(t[0])];
        const double v = std::abs((mesh.pos[size_t(t[1])] - p0).dot((mesh.pos[size_t(t[2])] - p0).cross(mesh.pos[size_t(t[3])] - p0))) / 24.0;
        int first = -1;
        for (int p = 0; p < 4; ++p)
        {
            const int i = t[size_t(p)];
            if (!(double(dens[size_t(i)]) > threshold + margin)) continue;
            weight[size_t(i)] += v;
            if (first < 0) first = i;
            else parent[size_t(find(i))] = find(first);
        }
    }
    std::vector<double> size(nv, 0.0);
    double all = 0;
    for (size_t i = 0; i < nv; ++i)
        if (weight[i] > 0)
        {
            size[size_t(find(int(i)))] += weight[i];
            all += weight[i];
        }
    int count = 0;
    for (size_t i = 0; i < nv; ++i) if (size[i] >= 0.02 * all && size[i] > 0) ++count;
    return count;
}

bool TetProblem::modal(int count, double density, int maxIterations, double tolerance, std::string& error)
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
    const bool ok = prepare(error);
    m_noLoads = false;
    m_prepared = false;         // (a static solve prepares again, with its loads)
    if (!ok) return false;
    const auto t0 = std::chrono::steady_clock::now();
    run_progress::Task task("modal analysis");
    task.set(0.0, "assembling the matrices");

    const TetMesh& mesh = *m_mesh;
    const size_t nv = mesh.pos.size(), n = 3 * nv;
    const Mat6 D = elasticity(m_E, m_nu);
    Assembly A;
    buildPattern(mesh, A);
    assemble(mesh, D, nullptr, A);
    buildPreconditioner(m_fixedDof, A);
    // The lumped mass: a quarter of each element's mass at each of its nodes
    std::vector<double> mass(n, 0.0);
    for (size_t t = 0; t < mesh.tets.size(); ++t)
        for (int p = 0; p < 4; ++p)
            for (int a = 0; a < 3; ++a) mass[3 * size_t(mesh.tets[t][size_t(p)]) + size_t(a)] += density * A.geom[t].vol / 4.0;
    for (size_t i = 0; i < n; ++i) if (m_fixedDof[i]) mass[i] = 0.0;

    task.set(0.1, "finding the modes");
    std::vector<double> lambda;
    std::vector<std::vector<double>> vectors;
    int iterations = 0;
    // (the block eigensolver converges in tens of iterations; the number asked for is a floor)
    if (!lobpcg(A, m_fixedDof, mass, count, std::max(maxIterations, 300), tolerance, nullptr, lambda, vectors, iterations, error))
        return false;
    if (std::getenv("FIELDES_FEA_DEBUG")) fprintf(stderr, "[tetfea] modal: %d iterations\n", iterations);

    m_frequencies.clear();
    m_modes.clear();
    auto locator = std::make_shared<TetLocator>(mesh);
    for (int j = 0; j < count; ++j)
    {
        const double lam = lambda[size_t(j)];
        m_frequencies.push_back(std::sqrt(std::max(0.0, lam)) / 6.283185307179586);
        const auto& x = vectors[size_t(j)];
        double umax = 0;
        for (size_t i = 0; i < nv; ++i)
            umax = std::max(umax, std::sqrt(x[3 * i] * x[3 * i] + x[3 * i + 1] * x[3 * i + 1] + x[3 * i + 2] * x[3 * i + 2]));
        const double s = umax > 0 ? 1.0 / umax : 1.0;
        auto R = std::make_shared<MeshResult>();
        R->mesh = m_mesh;
        R->locator = locator;
        for (auto& f : R->fields) f.assign(nv, 0.0f);
        for (size_t i = 0; i < nv; ++i)
        {
            const double ux = s * x[3 * i], uy = s * x[3 * i + 1], uz = s * x[3 * i + 2];
            R->fields[Result::UX][i] = float(ux);
            R->fields[Result::UY][i] = float(uy);
            R->fields[Result::UZ][i] = float(uz);
            R->fields[Result::DISPLACEMENT][i] = float(std::sqrt(ux * ux + uy * uy + uz * uz));
        }
        for (int f = 0; f < Result::FIELD_COUNT; ++f)
        {
            float mn = std::numeric_limits<float>::max(), mx = -std::numeric_limits<float>::max();
            for (float v : R->fields[f])
            {
                mn = std::min(mn, v);
                mx = std::max(mx, v);
            }
            R->minValue[f] = mn;
            R->maxValue[f] = mx;
        }
        R->elements = int(mesh.tets.size());
        R->nodes = int(nv);
        R->seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        m_modes.push_back(R);
    }
    return true;
}

}   // namespace fea
}   // namespace libfive
