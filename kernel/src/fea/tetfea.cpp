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
#include <functional>
#include <limits>
#include <numeric>
#include <unordered_map>

#include <boost/container/small_vector.hpp>

#include "libfive/fea/tetfea.hpp"
#include "libfive/tree/content_key.hpp"
#include "tet_common.hpp"
#include "tet_quad.hpp"
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

void TetProblem::addSupport(const Tree& region, bool x, bool y, bool z, int loadCase, Eigen::Vector3d displacement)
{
    m_supports.push_back(Support{region, {x, y, z}, loadCase, displacement});
    m_prepared = false;
}

void TetProblem::addForce(const Tree& region, Eigen::Vector3d total, int loadCase)
{
    m_forces.push_back(Force{region, total, std::max(0, loadCase)});
    m_prepared = false;
}

void TetProblem::addForceProfile(const Tree& region, Eigen::Vector3d total, int loadCase, const Tree& profile)
{
    m_forces.push_back(Force{region, total, std::max(0, loadCase), profile});
    m_prepared = false;
}

void TetProblem::markModalCase(int loadCase)
{
    if (loadCase < 0) return;
    if (m_modalCase.size() <= size_t(loadCase)) m_modalCase.resize(size_t(loadCase) + 1, 0);
    m_modalCase[size_t(loadCase)] = 1;
    m_prepared = false;
}

void TetProblem::setConditionBody(const Tree& body, double slack)
{
    m_conditionBody = body;
    m_conditionSlack = slack;
    m_prepared = false;
}

void TetProblem::setStiffnessField(const Tree& E)
{
    m_stiffnessField = E;
    m_prepared = false;
}

void TetProblem::setDensityField(const Tree& density)
{
    m_densityField = density;
    m_prepared = false;
}

void TetProblem::setExpansionField(const Tree& alpha)
{
    m_expansionField = alpha;
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

void TetProblem::setOrder(int order)
{
    m_order = order == 2 ? 2 : 1;
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
    // (a shape that is symmetric about a plane through the middle of its region is meshed on one side of the plane and mirrored: the mesh of a symmetric problem
    // is then exactly symmetric, and so are the displacement and the design on it -- a lattice cut by the shape cannot be, see meshShapeMirrored.  On a mesh whose
    // mirror pairs of elements differ the design that is kept symmetric has a gradient that is not the design's own: the volume's derivative was 17 % off)
    if (!meshShapeMirrored(m_shape, m_lo, m_hi, m_h, findMeshMirrors(m_shape, m_lo, m_hi, m_h), *mesh, error)) return false;
    task.set(0.5, "resolving supports and loads");

    // A node within a small fraction of an element of a region counts as inside it
    const float reach = float(0.05 * m_h);
    auto positions = [](const TetMesh& m) {
        std::vector<Eigen::Vector3f> pts(m.pos.size());
        for (size_t i = 0; i < pts.size(); ++i) pts[i] = m.pos[i].cast<float>();
        return pts;
    };
    // Which nodes lie on the body the conditions act on (see setConditionBody): everything, without one.  A node outside the body by more than the
    // slack is not a place a support holds or a load acts
    auto offBody = [&](const std::vector<Eigen::Vector3f>& pts) {
        std::vector<char> off;
        if (!m_conditionBody.is_valid()) return off;
        std::vector<float> bv;
        evalTreePoints(m_conditionBody, pts, bv);
        off.resize(pts.size());
        for (size_t i = 0; i < pts.size(); ++i) off[i] = !(double(bv[i]) <= m_conditionSlack);
        return off;
    };
    // Per vertex: bit a set = held in direction a
    // (onlyCase >= 0: the supports of that load case and those of every case; -2: all of them)
    // (`values`: per DOF, the displacement the support that holds it imposes -- see addSupport)
    auto fixedNodes = [&](const TetMesh& m, std::vector<unsigned char>& mask, int onlyCase, bool surfaceOnly, std::vector<double>* values = nullptr) {
        mask.assign(m.pos.size(), 0);
        if (values) values->assign(3 * m.pos.size(), 0.0);
        const auto pts = positions(m);
        const std::vector<char> off = offBody(pts);
        // (the nodes on the surface of the mesh: a corner of a boundary face -- see setConditionSurface)
        std::vector<char> inside;
        if (surfaceOnly && !m.faces.empty())
        {
            inside.assign(m.pos.size(), 0);
            for (const auto& tri : m.faces)
                for (int k = 0; k < 3; ++k) inside[size_t(tri[size_t(k)])] = 1;
        }
        std::vector<float> v;
        for (const auto& s : m_supports)
        {
            if (onlyCase >= 0 && s.loadCase >= 0 && s.loadCase != onlyCase) continue;
            evalTreePoints(s.region, pts, v);
            if (!off.empty())
                for (size_t i = 0; i < v.size(); ++i)
                    if (off[i]) v[i] = 1e30f;
            if (!inside.empty())
                for (size_t i = 0; i < v.size(); ++i)
                    if (!inside[i]) v[i] = 1e30f;
            // (a region that misses the part by a little -- the surface of an optimised design, a
            // region placed by hand -- still holds it: the nodes within half an element)
            float rch = reach;
            bool any = false;
            for (float x : v) any = any || x <= rch;
            if (!any) rch = 10 * reach;
            for (size_t i = 0; i < pts.size(); ++i)
            {
                if (!(v[i] <= rch)) continue;
                for (int a = 0; a < 3; ++a)
                    if (s.fix[a])
                    {
                        mask[i] |= (unsigned char)(1 << a);
                        if (values) (*values)[3 * i + size_t(a)] = s.displacement[a];
                    }
            }
        }
        int count = 0;
        for (auto b : mask) count += b != 0;
        return count;
    };
    auto fixedMask = [&](const TetMesh& m, std::vector<unsigned char>& mask, int onlyCase = -2, std::vector<double>* values = nullptr) {
        int count = fixedNodes(m, mask, onlyCase, m_conditionSurface, values);
        if (count == 0 && m_conditionSurface) count = fixedNodes(m, mask, onlyCase, false, values);      // (no node of the surface: the nodes inside it)
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
    // The 10-node elements: a node in the middle of every edge.  What the supports, the loads and the matrix below are made on is then every
    // node, the mesh is the nodes cut into linear tetrahedra (what the results are on), and `corners` is the linear mesh the elements were made on
    std::shared_ptr<TetMesh> corners;
    std::shared_ptr<QuadMesh> quad;
    if (m_order == 2)
    {
        // (the stiffness matrix of this many nodes, about 16 kB to a corner of the linear mesh: a mesh that this machine cannot hold is refused
        // here, with a way out, rather than by running out of memory in the middle of the solve)
        const double needed = 16.0e3 * double(mesh->pos.size());
        const double have = availableMemory();
        const double limit = have > 0 ? 0.7 * have : 12.0e9;
        if (needed > limit)
        {
            char buf[300];
            snprintf(buf, sizeof(buf), "the 10-node mesh is too fine for this computer's memory (its stiffness matrix would want about %.1f GB, "
                     "and about %.1f GB are free): use a larger element_size -- 10-node elements need about three times the size of linear ones "
                     "for the same accuracy", needed / 1e9, have / 1e9);
            error = buf;
            return false;
        }
        quad = std::make_shared<QuadMesh>();
        if (!buildQuadMesh(*mesh, m_shape, *quad, error)) return false;
        corners = mesh;
        mesh = refinedMesh(*corners, *quad);
        if (fixedMask(*mesh, mask) == 0)
        {
            error = "the support regions don't touch the part";
            return false;
        }
    }
    const size_t nv = mesh->pos.size(), nt = quad ? quad->elem.size() : mesh->tets.size();
    const size_t nDof = 3 * nv;
    std::vector<unsigned char> fixedDof(nDof, 0);
    m_fixedNodes = 0;
    for (size_t i = 0; i < nv; ++i)
    {
        if (mask[i]) m_fixedNodes++;
        for (int a = 0; a < 3; ++a) if (mask[i] & (1 << a)) fixedDof[3 * i + size_t(a)] = 1;
    }
    // The displacement a support imposes (see addSupport): what every held direction is held at, when any support holds it at more than 0
    std::vector<double> prescribed;
    {
        bool imposed = false;
        for (const auto& sp : m_supports)
            for (int a = 0; a < 3; ++a) imposed = imposed || (sp.fix[a] && sp.displacement[a] != 0.0);
        if (imposed)
        {
            std::vector<unsigned char> again;
            fixedMask(*mesh, again, -2, &prescribed);
        }
    }

    // Loads
    const std::vector<Force> noForces;
    const std::vector<Force>& forces = m_noLoads ? noForces : m_forces;
    std::vector<double> force(nDof, 0.0);
    int nCases = 1;
    for (const auto& fo : forces) nCases = std::max(nCases, fo.loadCase + 1);
    // Supports of one load case's own (several boundary conditions to optimise against, each with its supports): the nodes each
    // case holds, on top of the supports every case has
    int supportCases = 0;
    for (const auto& sp : m_supports) supportCases = std::max(supportCases, sp.loadCase + 1);
    if (!m_noLoads) nCases = std::max(nCases, supportCases);
    if (!m_noLoads) nCases = std::max(nCases, int(m_modalCase.size()));
    // (a case that is a modal analysis has supports and no loads: nothing in it is checked for loads; a problem of modal cases only needs none)
    auto modalCase = [&](int c) { return size_t(c) < m_modalCase.size() && m_modalCase[size_t(c)]; };
    bool allModal = !m_noLoads && !m_modalCase.empty();
    for (int c = 0; c < nCases && allModal; ++c) allModal = modalCase(c);
    std::vector<std::vector<unsigned char>> caseFixed;
    if (supportCases > 0 && !m_noLoads)
    {
        caseFixed.resize(size_t(nCases));
        for (int c = 0; c < nCases; ++c)
        {
            std::vector<unsigned char> mc;
            if (fixedMask(*mesh, mc, c) == 0)
            {
                error = "the boundary conditions " + std::to_string(c + 1) + " hold the part nowhere: each set of boundary conditions "
                        "needs a fixed(...) support that touches the part";
                return false;
            }
            caseFixed[size_t(c)].assign(nDof, 0);
            for (size_t i = 0; i < nv; ++i)
                for (int a = 0; a < 3; ++a)
                    if (mc[i] & (1 << a)) caseFixed[size_t(c)][3 * i + size_t(a)] = 1;
        }
    }
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
        // boundary triangles: centres and areas (of the linear mesh the 10-node elements were made on)
        const auto& lf = quad ? corners->faces : mesh->faces;
        std::vector<Eigen::Vector3f> centres(lf.size());
        std::vector<double> area(lf.size());
        for (size_t f = 0; f < lf.size(); ++f)
        {
            const auto& tri = lf[f];
            const Vec3 &a = mesh->pos[size_t(tri[0])], &b = mesh->pos[size_t(tri[1])], &c = mesh->pos[size_t(tri[2])];
            centres[f] = ((a + b + c) / 3.0).cast<float>();
            area[f] = 0.5 * (b - a).cross(c - a).norm();
        }
        const auto pts = positions(*mesh);
        const std::vector<char> off = offBody(pts);
        for (const auto& fo : forces)
        {
            std::vector<float> v, vn;
            evalTreePoints(fo.region, pts, vn);
            if (!off.empty())
                for (size_t n = 0; n < vn.size(); ++n)
                    if (off[n]) vn[n] = 1e30f;
            // (a load with a profile: its share of a triangle is the area times the profile there, not the area alone)
            std::vector<float> profileAtFaces, profileAtNodes;
            if (fo.profile.is_valid())
            {
                evalTreePoints(fo.profile, centres, profileAtFaces);
                evalTreePoints(fo.profile, pts, profileAtNodes);
            }
            auto weightOfFace = [&](size_t f) {
                if (profileAtFaces.empty()) return area[f];
                const double w = double(profileAtFaces[f]);
                return std::isfinite(w) && w > 0 ? area[f] * w : 0.0;
            };
            auto weightOfNode = [&](size_t n) {
                if (profileAtNodes.empty()) return 1.0;
                const double w = double(profileAtNodes[n]);
                return std::isfinite(w) && w > 0 ? w : 0.0;
            };
            bool placed = false;
            // (the region must reach the part; one that misses it by a little -- the surface of an optimised
            // design -- still loads it: the second pass takes what lies within half an element)
            for (int pass = 0; pass < 2 && !placed; ++pass)
            {
                const float rch = pass == 0 ? reach : 10 * reach;
                // The boundary triangles the region covers: those with all three corners inside it (a
                // triangle on the side of a bar, next to a loaded end, has one corner inside and is
                // not the end's); if there are none, those whose centre is inside
                std::vector<char> take(lf.size(), 0);
                double sel = 0, selArea = 0;
                for (size_t f = 0; f < take.size(); ++f)
                {
                    const auto& tri = lf[f];
                    take[f] = vn[size_t(tri[0])] <= rch && vn[size_t(tri[1])] <= rch && vn[size_t(tri[2])] <= rch;
                    if (take[f]) { sel += weightOfFace(f); selArea += area[f]; }
                }
                if (!(selArea > 0))
                {
                    evalTreePoints(fo.region, centres, v);
                    for (size_t f = 0; f < take.size(); ++f)
                    {
                        const auto& tri = lf[f];
                        take[f] = v[f] <= rch && (off.empty() || !(off[size_t(tri[0])] || off[size_t(tri[1])] || off[size_t(tri[2])]));
                        if (take[f]) { sel += weightOfFace(f); selArea += area[f]; }
                    }
                }
                if (selArea > 0 && !(sel > 0))
                {
                    error = "a load's profile is zero over the part's surface inside its region";
                    return false;
                }
                if (sel > 0)
                {
                    for (size_t f = 0; f < take.size(); ++f)
                    {
                        if (!take[f]) continue;
                        const Vec3 share = fo.total * (weightOfFace(f) / sel) / 3.0;
                        for (int p = 0; p < 3; ++p)
                        {
                            // (a 10-node face takes its load on the nodes in the middle of its edges)
                            const size_t n = quad ? size_t(quad->midNode(size_t(corners->faceTet[f]), lf[f][size_t(p)], lf[f][size_t((p + 1) % 3)]))
                                                  : size_t(lf[f][size_t(p)]);
                            addNodal(n, share, fo.loadCase);
                            loaded[n] = 1;
                        }
                    }
                    placed = true;
                    continue;
                }
                // a region holding no surface: over the nodes inside it
                size_t count = 0;
                double nodeWeight = 0;
                for (size_t n = 0; n < nv; ++n)
                    if (vn[n] <= rch) { ++count; nodeWeight += weightOfNode(n); }
                if (count == 0) continue;
                if (!(nodeWeight > 0))
                {
                    error = "a load's profile is zero at the nodes inside its region";
                    return false;
                }
                for (size_t n = 0; n < nv; ++n)
                {
                    if (!(vn[n] <= rch)) continue;
                    addNodal(n, fo.total * (weightOfNode(n) / nodeWeight), fo.loadCase);
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
    std::vector<TetGeom> geom(quad ? 0 : nt);
    parallelRange(geom.size(), [&](size_t b0, size_t b1) {
        for (size_t t = b0; t < b1; ++t)
        {
            const auto& v = mesh->tets[t];
            geom[t] = geometryOf(mesh->pos[size_t(v[0])], mesh->pos[size_t(v[1])], mesh->pos[size_t(v[2])],
                                 mesh->pos[size_t(v[3])]);
        }
    }, 256);
    // Material fields: each element's value at its centre
    m_scale.clear();
    m_elementDensity.clear();
    std::vector<double> elementAlpha;
    if (m_stiffnessField.is_valid() || m_densityField.is_valid() || m_expansionField.is_valid())
    {
        std::vector<Eigen::Vector3f> centres(nt);
        for (size_t t = 0; t < nt; ++t)
        {
            Vec3 c = Vec3::Zero();
            for (int p = 0; p < 4; ++p) c += mesh->pos[size_t(quad ? quad->elem[t][size_t(p)] : mesh->tets[t][size_t(p)])];
            centres[t] = (c / 4.0).cast<Eigen::Vector3f::Scalar>();
        }
        auto sample = [&](const Tree& field, const char* what, bool strictlyPositive, std::vector<double>& out) {
            std::vector<float> vals;
            evalTreePoints(field, centres, vals);
            out.resize(nt);
            for (size_t t = 0; t < nt; ++t)
            {
                const double x = double(vals[t]);
                if (!std::isfinite(x) || (strictlyPositive ? !(x > 0) : x < 0))
                {
                    char buf[200];
                    snprintf(buf, sizeof(buf), "%s must be %s everywhere in the part (it is %g at (%.4g, %.4g, %.4g))", what,
                             strictlyPositive ? "positive" : "not negative", x, double(centres[t].x()), double(centres[t].y()),
                             double(centres[t].z()));
                    error = buf;
                    return false;
                }
                out[t] = x;
            }
            return true;
        };
        if (m_stiffnessField.is_valid())
        {
            if (!sample(m_stiffnessField, "the Young's modulus field", true, m_scale)) return false;
            for (auto& x : m_scale) x /= m_E;
        }
        if (m_densityField.is_valid() && !sample(m_densityField, "the density field", false, m_elementDensity)) return false;
        if (m_expansionField.is_valid())
        {
            std::vector<float> vals;
            evalTreePoints(m_expansionField, centres, vals);
            elementAlpha.resize(nt);
            for (size_t t = 0; t < nt; ++t)
            {
                if (!std::isfinite(vals[t]))
                {
                    error = "the thermal expansion field isn't defined everywhere in the part";
                    return false;
                }
                elementAlpha[t] = double(vals[t]);
            }
        }
    }
    // Gravity: each element's weight, a quarter to each of its nodes
    if (!m_noLoads && (m_density > 0 || !m_elementDensity.empty()) && m_gravity.norm() > 0)
    {
        for (size_t t = 0; t < nt; ++t)
        {
            const double rho = m_elementDensity.empty() ? m_density : m_elementDensity[t];
            double qvol = 0, qN[10], qGrad[10][3];
            if (quad) quadIntegrals(*quad, t, &qvol, qN, qGrad);
            for (int p = 0; p < (quad ? 10 : 4); ++p)
            {
                // (a linear element's weight is a quarter to each node; a 10-node one's is the integral of each node's shape function)
                const Vec3 share = m_gravity * (rho * (quad ? qN[p] : geom[t].vol / 4.0));
                const size_t n = size_t(quad ? quad->elem[t][size_t(p)] : mesh->tets[t][size_t(p)]);
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
            for (int p = 0; p < 4; ++p) c += mesh->pos[size_t(quad ? quad->elem[t][size_t(p)] : mesh->tets[t][size_t(p)])];
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
            const double alpha = elementAlpha.empty() ? m_alpha : elementAlpha[t];
            const double e = alpha * (double(T[t]) - m_reference);
            m_thermalStrain[t] = e;
            const double stiffness = m_scale.empty() ? 1.0 : m_scale[t];
            double qvol = 0, qN[10], qGrad[10][3];
            if (quad) quadIntegrals(*quad, t, &qvol, qN, qGrad);
            for (int p = 0; p < (quad ? 10 : 4); ++p)
            {
                const size_t n = size_t(quad ? quad->elem[t][size_t(p)] : mesh->tets[t][size_t(p)]);
                for (int a = 0; a < 3; ++a)
                    force[3 * n + size_t(a)] += quad ? bulk * stiffness * e * qGrad[p][a] : geom[t].vol * bulk * stiffness * e * geom[t].g[p][a];
            }
        }
    }
    m_loadedNodes = 0;
    for (char l : loaded) m_loadedNodes += l != 0;
    for (const auto& fo : forces) m_totalLoad += fo.total;

    // What the loads and the supports leave: something must push, and every
    // direction that is pushed must be held somewhere
    if (!m_noLoads && !allModal && caseFixed.empty())            // (cases with supports of their own are checked one by one below)
    {
        double freeLoad = 0, anyLoad = 0;
        for (size_t d = 0; d < nDof; ++d)
        {
            anyLoad += std::abs(force[d]);
            if (!fixedDof[d]) freeLoad += std::abs(force[d]);
        }
        // (a displacement that a support imposes is a load too: a part that is only pushed by its supports needs no force)
        if (!(anyLoad > 0) && prescribed.empty())
        {
            error = (m_forces.empty() && !(m_density > 0) && m_elementDensity.empty() && m_thermalStrain.empty())
                        ? "no loads: add force(region, ...), gravity() or thermal_expansion(...), or a support that imposes a displacement (fixed(region, displacement=(...)))"
                        : "the loads have no effect: a load region may only touch the supports";
            return false;
        }
        if (!(freeLoad > 0) && prescribed.empty())
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

    if (!m_noLoads && !caseFixed.empty())
    {
        for (int c = 0; c < nCases; ++c)
        {
            if (modalCase(c)) continue;
            const std::vector<double>& cf = nCases > 1 ? caseForce[size_t(c)] : force;
            const std::vector<unsigned char>& cx = caseFixed[size_t(c)];
            double free = 0, any = 0;
            for (size_t d = 0; d < nDof; ++d)
            {
                any += std::abs(cf[d]);
                if (!cx[d]) free += std::abs(cf[d]);
            }
            if (!(any > 0) && prescribed.empty())
            {
                error = "the boundary conditions " + std::to_string(c + 1) + " have no loads that act on the part";
                return false;
            }
            if (!(free > 0) && prescribed.empty())
            {
                error = "the loads of the boundary conditions " + std::to_string(c + 1) + " act only on fixed nodes: move the load region off the supports";
                return false;
            }
            for (int a = 0; a < 3; ++a)
            {
                bool held = false;
                double net = 0, scale = 0;
                for (size_t i = size_t(a); i < nDof; i += 3)
                {
                    held = held || cx[i];
                    net += cf[i];
                    scale += std::abs(cf[i]);
                }
                if (!held && std::abs(net) > 1e-9 * std::max(scale, 1e-30))
                {
                    error = std::string("nothing holds the part in the ") + kAxis[a] + " direction in the boundary conditions " +
                            std::to_string(c + 1) + ", but its loads push it that way: fix it in " + kAxis[a] + " somewhere";
                    return false;
                }
            }
        }
    }

    uint64_t hsh = 1469598103934665603ull;
    const double hdr[] = {m_h, m_E, m_nu, double(nv), double(nt), m_alpha, m_reference};
    hsh = fnv(hsh, hdr, sizeof(hdr));
    if (m_order != 1) hsh = fnv(hsh, &m_order, sizeof(m_order));       // (the same mesh with another element is another problem)
    hsh = fnv(hsh, mesh->pos.data(), mesh->pos.size() * sizeof(Vec3));
    hsh = fnv(hsh, mesh->tets.data(), mesh->tets.size() * sizeof(std::array<int, 4>));
    hsh = fnv(hsh, fixedDof.data(), fixedDof.size());
    if (!prescribed.empty()) hsh = fnv(hsh, prescribed.data(), prescribed.size() * sizeof(double));
    for (const auto& cx : caseFixed) hsh = fnv(hsh, cx.data(), cx.size());
    hsh = fnv(hsh, force.data(), force.size() * sizeof(double));
    for (const auto& cf : caseForce) hsh = fnv(hsh, cf.data(), cf.size() * sizeof(double));
    if (!m_thermalStrain.empty()) hsh = fnv(hsh, m_thermalStrain.data(), m_thermalStrain.size() * sizeof(double));
    // (what the material fields made of the elements, and of the weight and the mass: another field is another problem)
    if (!m_scale.empty()) hsh = fnv(hsh, m_scale.data(), m_scale.size() * sizeof(double));
    if (!m_elementDensity.empty()) hsh = fnv(hsh, m_elementDensity.data(), m_elementDensity.size() * sizeof(double));
    m_hash = hsh;

    m_mesh = mesh;
    m_quad = quad;
    m_fixedDof.swap(fixedDof);
    m_prescribed.swap(prescribed);
    m_force.swap(force);
    m_caseForce.swap(caseForce);
    m_caseFixed.swap(caseFixed);
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
    if (m_quad) return solveQuad(maxIterations, tolerance, error, cancel);
    const auto t0 = std::chrono::steady_clock::now();
    run_progress::Task task("solving");
    task.set(0.0, "assembling the stiffness matrix");

    const TetMesh& mesh = *m_mesh;
    const size_t nv = mesh.pos.size(), nt = mesh.tets.size(), n = 3 * nv;
    const Mat6 D = elasticity(m_E, m_nu);
    Assembly A;
    buildPattern(mesh, A);
    assemble(mesh, D, m_scale.empty() ? nullptr : &m_scale, A);
    buildPreconditioner(m_fixedDof, A);
    task.set(0.05, "preparing the solver");

    const std::vector<unsigned char>& fixed = m_fixedDof;
    std::vector<double> u(n, 0.0);
    // The displacement a support imposes (see addSupport): the free directions are solved against what the load leaves after the matrix has taken
    // its share of the imposed movement, and the held ones are given the movement back
    std::vector<double> rhsImposed;
    const std::vector<double>* rhs = &m_force;
    if (!m_prescribed.empty())
    {
        std::vector<double> Kp(n);
        A.K.matvec(m_prescribed.data(), Kp.data());
        rhsImposed = m_force;
        for (size_t i = 0; i < n; ++i) if (!fixed[i]) rhsImposed[i] -= Kp[i];
        rhs = &rhsImposed;
    }
    CgResult cg;
    if (!cgSolve(A, fixed, *rhs, u, tolerance, maxIterations, cancel,
                 [&](double frac, const std::string& text) { task.set(0.05 + 0.9 * frac, text); }, cg, error))
        return false;
    if (!m_prescribed.empty())
        for (size_t i = 0; i < n; ++i) if (fixed[i]) u[i] = m_prescribed[i];
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

    // Reactions at the supports, compliance.  With a displacement imposed by a support, the supports hold each other in balance (the net of all of them is the load, which is nothing
    // when only displacements load the part), so the reaction is what the displaced supports push with -- their force in the directions they move the part -- and the compliance is the
    // work that force does, u.Ku (the same number as f.u for a part that is loaded by forces)
    std::vector<double> Ku(n);
    A.K.matvec(u.data(), Ku.data());
    Vec3 reaction = Vec3::Zero();
    double compliance = 0;
    const bool displaced = !m_prescribed.empty();
    for (size_t i = 0; i < n; ++i)
    {
        if (fixed[i] && (!displaced || m_prescribed[i] != 0.0)) reaction[int(i % 3)] += Ku[i] - m_force[i];
        compliance += displaced ? u[i] * Ku[i] : m_force[i] * u[i];
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
            const Vec6 sig = (m_scale.empty() ? 1.0 : m_scale[t]) * (D * eps);
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
    if (m_order == 2)
    {
        error = "topology optimization designs one density to each linear tetrahedron: it has no 10-node element (use element='tet')";
        return false;
    }
    if (!m_prepared && !prepare(error)) return false;
    if (!m_prescribed.empty())
    {
        // (the sensitivities of the compliance and of the stress assume that nothing moves the part but its loads)
        error = "a support that imposes a displacement isn't supported in topology optimization: hold the part (displacement=(0, 0, 0)) and load it with a force instead";
        return false;
    }
    if (!m_thermalStrain.empty())
    {
        // (its load depends on the design: the sensitivities would be wrong)
        error = "thermal expansion isn't supported in topology optimization";
        return false;
    }
    m_history.clear();
    m_baseHistory.clear();
    m_outputHistory.clear();
    m_densityHistory.clear();
    m_mirrorsUsed.clear();
    m_resume.clear();
    m_designResult.reset();
    const auto t0 = std::chrono::steady_clock::now();
    const TetMesh& mesh = *m_mesh;
    const size_t nv = mesh.pos.size(), na = mesh.tets.size(), n = 3 * nv;
    const double h = m_h;
    const Mat6 D = elasticity(m_E, m_nu);
    Assembly A;
    buildPattern(mesh, A);
    const bool debug = std::getenv("FIELDES_FEA_DEBUG") != nullptr;
    std::vector<double> vol(na);
    for (size_t a = 0; a < na; ++a) vol[a] = A.geom[a].vol;
    double total = 0;                           // (the volume of the part, which the design space may be bigger than: told by the design)
    std::vector<double> xPhys;                  // (the density that is being evaluated: the design's, copied in at each evaluation)
    int it = 0;                                 // (the iteration of it)
    const double p = s.penalty, emin = s.minStiffness;

    // The structural physics of the design (density_design.cpp has the rest of it): what a load or a support holds solid, whether the problem is
    // symmetric about a plane, the measures of a density with their sensitivities, and the picture of an iteration
    auto fixedElements = [&](std::vector<char>& state, std::vector<char>& supported) {
        // Elements that hold the supports and loads stay solid.  `supported`: the ones that hold a support (of any load case) -- what the material
        // has to be joined to to be held: the elements under a load or in a keep region are solid too, but hold nothing
        supported.assign(na, 0);
    for (size_t a = 0; a < na; ++a)
        for (int p = 0; p < 4; ++p)
        {
            const size_t d = 3 * size_t(mesh.tets[a][size_t(p)]);
            for (int q = 0; q < 3; ++q)
            {
                bool held = m_fixedDof[d + size_t(q)] != 0;
                for (const auto& fx : m_caseFixed) held = held || fx[d + size_t(q)] != 0;
                if (held) supported[a] = 1;
                if (held || m_force[d + size_t(q)] != 0) state[a] = 1;
            }
        }
    };
    auto symmetricAbout = [&](int axis, double at, const std::vector<Eigen::Vector3f>& mc, double badRegionShare, std::string& why) -> bool {
        // The part (the same at the mirror image of every element's centre); the loads and the supports: what acts on one side is what acts on the
        // other, mirrored
        double sumVol = 0, badShape = 0;
        std::vector<float> v1;
        evalTreePoints(m_shape, mc, v1);
        for (size_t a = 0; a < na; ++a)
        {
            sumVol += A.geom[a].vol;
            if (!(v1[a] <= 0.25 * h)) badShape += A.geom[a].vol;
        }
        if (debug)
            fprintf(stderr, "[tetopt] symmetry about axis %d at %.4g: part mismatch %.3f, keep/avoid mismatch %.3f\n", axis, at,
                    badShape / std::max(sumVol, 1e-30), badRegionShare);
        if (badShape > 0.04 * sumVol) { why = "the part is not symmetric"; return false; }
        if (badRegionShare > 0.04) { why = "the keep / avoid regions are not symmetric"; return false; }
        if (!m_scale.empty()) { why = "the material is a field"; return false; }

            auto sides = [&](const std::vector<double>& f, const std::vector<unsigned char>& fx, std::string& reason) -> bool {
                struct Side { Vec3 sum = Vec3::Zero(); double w = 0; Vec3 c = Vec3::Zero(); };
                Side sd[2];                                 // (0 above the plane, 1 below it)
                double planeK = 0, planeW = 0, nFix[2][3] = {{0, 0, 0}, {0, 0, 0}}, fw[2] = {0, 0};
                Vec3 fc[2] = {Vec3::Zero(), Vec3::Zero()};
                for (size_t i = 0; i < nv; ++i)
                {
                    const Vec3& p = mesh.pos[i];
                    const double d = p[axis] - at;
                    const bool onPlane = std::abs(d) < 0.25 * h;
                    const int side = d > 0 ? 0 : 1;
                    Vec3 q = p;
                    q[axis] = std::abs(d);                  // (the distance from the plane: the same for a point and its image)
                    const Vec3 F(f[3 * i], f[3 * i + 1], f[3 * i + 2]);
                    const double w = F.norm();
                    if (w > 0)
                    {
                        if (onPlane)
                        {
                            planeK += F[axis];
                            planeW += w;
                        }
                        else
                        {
                            sd[side].sum += F;
                            sd[side].w += w;
                            sd[side].c += w * q;
                        }
                    }
                    bool held = false;
                    for (int a = 0; a < 3; ++a)
                        if (fx[3 * i + size_t(a)] && !onPlane)
                        {
                            nFix[side][a] += 1;
                            held = true;
                        }
                    if (held)
                    {
                        fc[side] += q;
                        fw[side] += 1;
                    }
                }
                const double totalW = sd[0].w + sd[1].w + planeW;
                if (totalW > 0)
                {
                    Vec3 mB = sd[1].sum;
                    mB[axis] = -mB[axis];
                    const double scale = std::max(sd[0].w, sd[1].w);
                    if (debug)
                        fprintf(stderr, "[tetopt]   loads: above (%.4g, %.4g, %.4g) below mirrored (%.4g, %.4g, %.4g), on the plane %.4g\n",
                                sd[0].sum[0], sd[0].sum[1], sd[0].sum[2], mB[0], mB[1], mB[2], planeK);
                    if ((sd[0].sum - mB).norm() > 0.06 * scale) { reason = "the loads differ between the two sides"; return false; }
                    if (std::abs(planeK) > 0.06 * totalW) { reason = "a load on the plane pushes through it"; return false; }
                    if (sd[0].w > 0.03 * totalW && sd[1].w > 0.03 * totalW)
                    {
                        const Vec3 cA = sd[0].c / sd[0].w, cB = sd[1].c / sd[1].w;
                        if (debug) fprintf(stderr, "[tetopt]   load centres differ by %.4g (element size %.4g)\n", (cA - cB).norm(), h);
                        if ((cA - cB).norm() > 0.5 * h) { reason = "the loads are in other places on the two sides"; return false; }
                    }
                }
                for (int a = 0; a < 3; ++a)
                    if (std::abs(nFix[0][a] - nFix[1][a]) > 0.1 * (nFix[0][a] + nFix[1][a]) + 3)
                    {
                        reason = "the supports differ between the two sides";
                        return false;
                    }
                if (fw[0] >= 5 && fw[1] >= 5)
                {
                    const Vec3 cA = fc[0] / fw[0], cB = fc[1] / fw[1];
                    if (debug) fprintf(stderr, "[tetopt]   support centres differ by %.4g\n", (cA - cB).norm());
                    if ((cA - cB).norm() > 0.5 * h) { reason = "the supports are in other places on the two sides"; return false; }
                }
                return true;
            };
            if (m_caseForce.empty())
            {
                if (!sides(m_force, m_caseFixed.empty() ? m_fixedDof : m_caseFixed[0], why)) return false;
            }
            else
                for (size_t c = 0; c < m_caseForce.size(); ++c)
                    if (!sides(m_caseForce[c], m_caseFixed.empty() ? m_fixedDof : m_caseFixed[c], why)) return false;
            return true;
    };

    const int nc = std::max<int>(1, int(m_caseForce.size()));
    const int nb = int(s.measures.size());
    const int nOut = s.program.outputs();
    if (nb < 1 || nOut < 1)
    {
        error = "the optimisation was not told what to make best";
        return false;
    }
    auto isModal = [&](int c) { return size_t(c) < m_modalCase.size() && m_modalCase[size_t(c)]; };
    auto fixedOf = [&](int c) -> const std::vector<unsigned char>& { return m_caseFixed.empty() ? m_fixedDof : m_caseFixed[size_t(c)]; };
    auto forceOf = [&](int c) -> const std::vector<double>& { return nc > 1 ? m_caseForce[size_t(c)] : m_force; };
    std::vector<char> needStatic(size_t(nc), 0);
    std::vector<int> modesWanted(size_t(nc), 0);
    for (const auto& ms : s.measures)
    {
        if (ms.loadCase < 0 || ms.loadCase >= nc)
        {
            error = "a measure is asked for in an analysis that the problem does not have";
            return false;
        }
        if (ms.kind == TopOpt::FREQUENCY)
        {
            if (!isModal(ms.loadCase))
            {
                error = "a frequency can be read from a modal analysis only";
                return false;
            }
            modesWanted[size_t(ms.loadCase)] = std::max(modesWanted[size_t(ms.loadCase)], ms.index + 1);
        }
        else if (ms.kind != TopOpt::VOLUME)
        {
            if (isModal(ms.loadCase))
            {
                error = "a modal analysis has frequencies and a volume, nothing that needs loads";
                return false;
            }
            needStatic[size_t(ms.loadCase)] = 1;
        }
    }
    for (int lc = 0; lc < nc; ++lc)
        if (isModal(lc) && !(s.density > 0) && m_elementDensity.empty())
        {
            error = "a frequency needs the material's density";
            return false;
        }

    std::vector<std::vector<double>> caseU(static_cast<size_t>(nc));
    std::vector<double> scale(na);
    std::shared_ptr<TetLocator> liveLocator;
    buildPreconditioner(m_fixedDof, A);        // (sized; refreshed each iteration)

    // The relaxation of the stress at low density (the qp approach): the stress an element reports is the material's for its strain, times
    // density^q, so that a nearly empty element cannot carry a stress that holds the design back.  Why q = p - 2.5 (not less than 1/4):
    // a design that is grey all over (every element at the density x) has the stiffness x^p, so its strain is x^-p times the solid's, and
    // the stress it reports is x^(q-p) times the solid's -- it meets a limit at x = (solid stress / limit)^(1/(p-q)).  That share of the
    // material is what a foam costs; it has to be more than a crisp layout needs, or the optimiser fills the part with a foam of the size
    // of the elements (it did, with q = p - 1: 40 % of a beam, in two pieces).  With p - q = 2.5 the foam costs the solid's stress ratio
    // to the power 0.4 -- most of the part -- while a member of full density is not changed by the exponent at all
    const double stressExp = std::max(0.25, p - 2.5);
    // ... and no element reports less than the one whose stiffness is the void's (below that density the stiffness stays the void's whatever the
    // density is, so the force that still goes through such an element is no smaller): without this floor a connection taken down to nothing
    // reported no stress at all, and the design fell apart into pieces that the constraint did not see
    const double stressFloor = emin > 0 ? std::min(0.5, std::pow(emin, 1.0 / p)) : 1e-9;
    const double norm = 10.0;                           // the exponent of the p-norm that stands for a peak
    const Mat6 vmForm = [] {
        Mat6 V = Mat6::Zero();
        V(0, 0) = V(1, 1) = V(2, 2) = 1.0;
        V(0, 1) = V(1, 0) = V(0, 2) = V(2, 0) = V(1, 2) = V(2, 1) = -0.5;
        V(3, 3) = V(4, 4) = V(5, 5) = 3.0;
        return V;
    }();
    // (the lowest frequencies of a design with a nearly empty part: a mass that falls off faster than the stiffness below a tenth of
    // the density, so that the empty part has no low modes of its own)
    auto massOf = [](double x) { return x >= 0.1 ? x : 6e5 * std::pow(x, 6) - 5e6 * std::pow(x, 7); };
    auto massSlope = [](double x) { return x >= 0.1 ? 1.0 : 36e5 * std::pow(x, 5) - 35e6 * std::pow(x, 6); };
    std::vector<double> peakFactor(size_t(nb), 1.0);    // what keeps a p-norm at the peak it stands for
    std::vector<std::vector<double>> adjointU(static_cast<size_t>(nb));
    std::unique_ptr<TetLocator> pointLocator;
    for (const auto& ms : s.measures)
        if (ms.kind == TopOpt::DISPLACEMENT_AT && !pointLocator) pointLocator = std::make_unique<TetLocator>(mesh);

    auto gather = [&](const std::vector<double>& u, size_t a, double ue[12]) {
        for (int q = 0; q < 4; ++q)
            for (int d = 0; d < 3; ++d) ue[3 * q + d] = u[3 * size_t(mesh.tets[a][size_t(q)]) + size_t(d)];
    };
    // The energy of two displacements in an element: w^T K0 u with K0 the element's stiffness of the full material
    auto energy = [&](size_t a, const double* w, const double* u) {
        const Vec6 e1 = strainOf(A.geom[a], w), e2 = strainOf(A.geom[a], u);
        return A.geom[a].vol * e1.dot(D * e2) * (m_scale.empty() ? 1.0 : m_scale[a]);
    };
    // The sensitivity of a measure with the solution u and its adjoint lam to the density of each element (what the stiffness part of the
    // measure's change is): -lam^T dK/dx u
    auto addImplicit = [&](std::vector<double>& out, const std::vector<double>& lam, const std::vector<double>& u) {
        parallelRange(na, [&](size_t b0, size_t b1) {
            for (size_t a = b0; a < b1; ++a)
            {
                double ul[12], ue[12];
                gather(lam, a, ul);
                gather(u, a, ue);
                out[a] -= p * std::pow(std::max(xPhys[a], 1e-9), p - 1) * (1 - emin) * energy(a, ul, ue);
            }
        }, 512);
    };
    // The vector that is the sum of per-element contributions to the nodes' displacement DOFs
    auto scatter = [&](const std::vector<std::array<double, 12>>& re, std::vector<double>& r) {
        r.assign(n, 0.0);
        parallelRange(nv, [&](size_t b0, size_t b1) {
            for (size_t i = b0; i < b1; ++i)
                for (uint32_t k = A.vtStart[i]; k < A.vtStart[i + 1]; ++k)
                {
                    const size_t t = A.vtList[k];
                    for (int q = 0; q < 4; ++q)
                        if (size_t(mesh.tets[t][size_t(q)]) == i)
                            for (int d = 0; d < 3; ++d) r[3 * i + size_t(d)] += re[t][size_t(3 * q + d)];
                }
        }, 256);
    };
    double solveTol = std::max(s.tolerance, 1e-4), modeTol = std::max(s.tolerance, 1e-6);
    int cgIterations = s.solverIterations;

    // The measures of a density and their sensitivities to the density of every element.  update: the factors that keep a p-norm at the peak it
    // stands for are moved on (not when the design is only looked at a little off its place)
    auto measuresOf = [&](const std::vector<double>& xPhysIn, bool update, int iteration, std::vector<double>& bv,
                          std::vector<std::vector<double>>& dB) -> bool {
        xPhys = xPhysIn;
        it = iteration;
        for (size_t a = 0; a < na; ++a)
            scale[a] = (emin + (1 - emin) * std::pow(xPhys[a], p)) * (m_scale.empty() ? 1.0 : m_scale[a]);
        assemble(mesh, D, &scale, A);

        // The static analyses that a measure reads
        for (int lc = 0; lc < nc; ++lc)
        {
            if (!needStatic[size_t(lc)]) continue;
            CgResult cg;
            buildPreconditioner(fixedOf(lc), A);
            // (the sensitivities don't need a tight solve; warm starts keep each one short)
            if (!cgSolve(A, fixedOf(lc), forceOf(lc), caseU[size_t(lc)], solveTol, cgIterations, cancel, nullptr, cg, error)) return false;
        }
        // The modes that a measure reads
        std::vector<std::vector<double>> modeLambda(static_cast<size_t>(nc));
        std::vector<std::vector<std::vector<double>>> modeVector(static_cast<size_t>(nc));
        for (int lc = 0; lc < nc; ++lc)
        {
            if (modesWanted[size_t(lc)] < 1) continue;
            std::vector<double> mass(n, 0.0);
            for (size_t t = 0; t < na; ++t)
            {
                const double rho = m_elementDensity.empty() ? s.density : m_elementDensity[t];
                const double mt = rho * A.geom[t].vol / 4.0 * massOf(xPhys[t]);
                for (int q = 0; q < 4; ++q)
                    for (int d = 0; d < 3; ++d) mass[3 * size_t(mesh.tets[t][size_t(q)]) + size_t(d)] += mt;
            }
            const std::vector<unsigned char>& fx = fixedOf(lc);
            for (size_t i = 0; i < n; ++i) if (fx[i]) mass[i] = 0.0;
            buildPreconditioner(fx, A);
            int iterationsUsed = 0;
            // (a few more modes than are read: to tell whether the last one read has a twin)
            const int count = modesWanted[size_t(lc)] + 2;
            if (!lobpcg(A, fx, mass, count, 300, modeTol, cancel, modeLambda[size_t(lc)], modeVector[size_t(lc)], iterationsUsed, error))
                return false;
            for (auto& phi : modeVector[size_t(lc)])
            {
                double mm = 0;
                for (size_t i = 0; i < n; ++i) mm += mass[i] * phi[i] * phi[i];
                if (mm > 0)
                    for (double& v : phi) v /= std::sqrt(mm);
            }
        }

        // The measures' values and sensitivities
        for (auto& g : dB) std::fill(g.begin(), g.end(), 0.0);
        for (int b = 0; b < nb; ++b)
        {
            const TopOpt::Measure& ms = s.measures[size_t(b)];
            std::vector<double>& out = dB[size_t(b)];
            switch (ms.kind)
            {
            case TopOpt::VOLUME:
            {
                // the share of the part's volume that the design uses
                double v = 0;
                for (size_t a = 0; a < na; ++a)
                {
                    v += vol[a] * xPhys[a];
                    out[a] = total > 0 ? vol[a] / total : 0.0;
                }
                bv[size_t(b)] = total > 0 ? v / total : 0.0;
                break;
            }
            case TopOpt::COMPLIANCE:
            {
                const std::vector<double>& f = forceOf(ms.loadCase);
                const std::vector<double>& u = caseU[size_t(ms.loadCase)];
                double c = 0;
                for (size_t i = 0; i < n; ++i) c += f[i] * u[i];
                bv[size_t(b)] = c;
                // compliance sensitivities from each element's strain energy (the adjoint is the solution itself)
                addImplicit(out, u, u);
                break;
            }
            case TopOpt::MAX_VON_MISES:
            {
                // the p-norm of the elements' relaxed von Mises stress, brought to the peak it stands for
                const std::vector<double>& u = caseU[size_t(ms.loadCase)];
                std::vector<double> sigma(na), xq(na);
                std::vector<std::array<double, 12>> re(na);
                double peak = 0;
                parallelRange(na, [&](size_t b0, size_t b1) {
                    for (size_t a = b0; a < b1; ++a)
                    {
                        double ue[12];
                        gather(u, a, ue);
                        const Vec6 eps = strainOf(A.geom[a], ue);
                        const Vec6 st = D * eps * (m_scale.empty() ? 1.0 : m_scale[a]);
                        const double vm = std::sqrt(std::max(0.0, st.dot(vmForm * st)));
                        xq[a] = std::pow(std::max(xPhys[a], stressFloor), stressExp);
                        sigma[a] = xq[a] * vm;
                    }
                }, 512);
                for (size_t a = 0; a < na; ++a) peak = std::max(peak, sigma[a]);
                double sum = 0;
                if (peak > 0)
                    for (size_t a = 0; a < na; ++a) sum += std::pow(sigma[a] / peak, norm);
                const double pn = peak * std::pow(sum, 1.0 / norm);          // (>= the peak)
                if (update && it > 0 && pn > 0 && std::isfinite(peak / pn)) peakFactor[size_t(b)] = 0.5 * peakFactor[size_t(b)] + 0.5 * std::max(0.1, std::min(2.0, peak / pn));
                const double factor = peakFactor[size_t(b)];
                bv[size_t(b)] = factor * pn;
                // d (factor * pn) / d sigma_a = factor * (sigma_a / pn)^(norm-1)
                parallelRange(na, [&](size_t b0, size_t b1) {
                    for (size_t a = b0; a < b1; ++a)
                    {
                        std::array<double, 12>& r = re[a];
                        r.fill(0.0);
                        if (!(pn > 0) || !(sigma[a] > 0)) continue;
                        double ue[12];
                        gather(u, a, ue);
                        const Vec6 eps = strainOf(A.geom[a], ue);
                        const Vec6 st = D * eps * (m_scale.empty() ? 1.0 : m_scale[a]);
                        const double vm = std::sqrt(std::max(1e-30, st.dot(vmForm * st)));
                        const double w = factor * std::pow(sigma[a] / pn, norm - 1.0) * xq[a] / vm;
                        // d vm / d u = (D B)^T V st / vm
                        const Mat6x12 Bm = strainMatrix(A.geom[a]);
                        const Eigen::Matrix<double, 12, 1> dvm = Bm.transpose() * (D * (vmForm * st)) * (m_scale.empty() ? 1.0 : m_scale[a]);
                        for (int k = 0; k < 12; ++k) r[size_t(k)] = w * dvm(k);
                    }
                }, 512);
                std::vector<double> rhs;
                scatter(re, rhs);
                std::vector<double>& lam = adjointU[size_t(b)];
                CgResult cg;
                buildPreconditioner(fixedOf(ms.loadCase), A);
                if (!cgSolve(A, fixedOf(ms.loadCase), rhs, lam, solveTol, cgIterations, cancel, nullptr, cg, error)) return false;
                addImplicit(out, lam, u);
                // the part that does not go through the displacement: the density's own power in the relaxed stress
                parallelRange(na, [&](size_t b0, size_t b1) {
                    for (size_t a = b0; a < b1; ++a)
                    {
                        if (!(pn > 0) || !(sigma[a] > 0) || xPhys[a] <= stressFloor) continue;
                        out[a] += factor * std::pow(sigma[a] / pn, norm - 1.0) * stressExp * sigma[a] / xPhys[a];
                    }
                }, 512);
                break;
            }
            case TopOpt::MAX_DISPLACEMENT:
            {
                // the p-norm of the nodes' displacement, each node counting as much as the material round it (the density of the nodes
                // to the 4th power): an empty part has displacements that are of no part's
                const std::vector<double>& u = caseU[size_t(ms.loadCase)];
                std::vector<double> rho(nv, 0.0), vsum(nv, 0.0), mag(nv, 0.0), weight(nv, 0.0);
                for (size_t i = 0; i < nv; ++i)
                {
                    for (uint32_t k = A.vtStart[i]; k < A.vtStart[i + 1]; ++k)
                    {
                        const size_t t = A.vtList[k];
                        rho[i] += vol[t] * xPhys[t];
                        vsum[i] += vol[t];
                    }
                    rho[i] = vsum[i] > 0 ? rho[i] / vsum[i] : 0.0;
                    weight[i] = std::pow(rho[i], 4.0);
                    mag[i] = std::sqrt(u[3 * i] * u[3 * i] + u[3 * i + 1] * u[3 * i + 1] + u[3 * i + 2] * u[3 * i + 2]);
                }
                double peak = 0;
                for (size_t i = 0; i < nv; ++i) if (weight[i] > 1e-8) peak = std::max(peak, mag[i]);
                double sum = 0;
                if (peak > 0)
                    for (size_t i = 0; i < nv; ++i) sum += weight[i] * std::pow(mag[i] / peak, norm);
                const double pn = peak * std::pow(sum, 1.0 / norm);
                if (update && it > 0 && pn > 0 && std::isfinite(peak / pn)) peakFactor[size_t(b)] = 0.5 * peakFactor[size_t(b)] + 0.5 * std::max(0.1, std::min(2.0, peak / pn));
                const double factor = peakFactor[size_t(b)];
                bv[size_t(b)] = factor * pn;
                std::vector<double> rhs(n, 0.0);
                if (pn > 0)
                    for (size_t i = 0; i < nv; ++i)
                    {
                        if (!(mag[i] > 0) || weight[i] <= 0) continue;
                        // d (factor * pn) / d u_i = factor * weight_i * (|u_i| / pn)^(norm-1) * u_i / |u_i|
                        const double w = factor * weight[i] * std::pow(mag[i] / pn, norm - 1.0) / mag[i];
                        for (int d = 0; d < 3; ++d) rhs[3 * i + size_t(d)] = w * u[3 * i + size_t(d)];
                    }
                std::vector<double>& lam = adjointU[size_t(b)];
                CgResult cg;
                buildPreconditioner(fixedOf(ms.loadCase), A);
                if (!cgSolve(A, fixedOf(ms.loadCase), rhs, lam, solveTol, cgIterations, cancel, nullptr, cg, error)) return false;
                addImplicit(out, lam, u);
                // through the nodes' weights: d weight_i / d x_a = 4 rho_i^3 vol_a / vsum_i
                if (pn > 0)
                    for (size_t a = 0; a < na; ++a)
                    {
                        double acc = 0;
                        for (int q = 0; q < 4; ++q)
                        {
                            const size_t i = size_t(mesh.tets[a][size_t(q)]);
                            if (!(vsum[i] > 0)) continue;
                            acc += factor * (1.0 / norm) * std::pow(pn, 1.0 - norm) * std::pow(mag[i], norm) * 4.0 * std::pow(rho[i], 3.0) * vol[a] / vsum[i];
                        }
                        out[a] += acc;
                    }
                break;
            }
            case TopOpt::DISPLACEMENT_AT:
            {
                // a component (ms.index 0 / 1 / 2) of the displacement at a point, or its magnitude (3): the barycentric mix of the nodes of the tetrahedron that holds it
                const std::vector<double>& u = caseU[size_t(ms.loadCase)];
                double lam4[4];
                const int tet = pointLocator->locate(Vec3(ms.a, ms.b, ms.c), lam4);
                if (tet < 0)
                {
                    error = "a point where the displacement is read is outside the part";
                    return false;
                }
                double ux[3] = {0, 0, 0};
                for (int q = 0; q < 4; ++q)
                    for (int d = 0; d < 3; ++d) ux[d] += lam4[q] * u[3 * size_t(mesh.tets[size_t(tet)][size_t(q)]) + size_t(d)];
                double direction[3] = {0, 0, 0};
                double value;
                if (ms.index >= 0 && ms.index < 3)
                {
                    value = ux[ms.index];
                    direction[ms.index] = 1.0;
                }
                else
                {
                    value = std::sqrt(ux[0] * ux[0] + ux[1] * ux[1] + ux[2] * ux[2]);
                    for (int d = 0; d < 3; ++d) direction[d] = value > 0 ? ux[d] / value : 0.0;
                }
                bv[size_t(b)] = value;
                std::vector<double> rhs(n, 0.0);
                for (int q = 0; q < 4; ++q)
                    for (int d = 0; d < 3; ++d) rhs[3 * size_t(mesh.tets[size_t(tet)][size_t(q)]) + size_t(d)] += lam4[q] * direction[d];
                std::vector<double>& lam = adjointU[size_t(b)];
                CgResult cg;
                buildPreconditioner(fixedOf(ms.loadCase), A);
                if (!cgSolve(A, fixedOf(ms.loadCase), rhs, lam, solveTol, cgIterations, cancel, nullptr, cg, error)) return false;
                addImplicit(out, lam, u);
                break;
            }
            case TopOpt::FREQUENCY:
            {
                // the natural frequency (Hz) of mode ms.index; modes of the same frequency (a symmetric design has them) share their sensitivity
                const std::vector<double>& lamda = modeLambda[size_t(ms.loadCase)];
                const auto& phis = modeVector[size_t(ms.loadCase)];
                const double l0 = lamda[size_t(ms.index)];
                if (!(l0 > 0))
                {
                    error = "the design has a mode that is not positive: a frequency cannot be read";
                    return false;
                }
                std::vector<size_t> cluster;
                for (size_t j = 0; j < lamda.size(); ++j)
                    if (std::abs(lamda[j] - l0) <= 1e-4 * l0) cluster.push_back(j);
                bv[size_t(b)] = std::sqrt(l0) / 6.283185307179586;
                const double dFreq = 1.0 / (2.0 * 6.283185307179586 * std::sqrt(l0));                       // d f / d lambda
                for (size_t j : cluster)
                {
                    const std::vector<double>& phi = phis[j];
                    parallelRange(na, [&](size_t b0, size_t b1) {
                        for (size_t a = b0; a < b1; ++a)
                        {
                            double pe[12];
                            gather(phi, a, pe);
                            const double stiff = p * std::pow(std::max(xPhys[a], 1e-9), p - 1) * (1 - emin) * energy(a, pe, pe);
                            double square = 0;
                            for (int k = 0; k < 12; ++k) square += pe[k] * pe[k];
                            const double rho = m_elementDensity.empty() ? s.density : m_elementDensity[a];
                            const double massPart = lamda[j] * massSlope(xPhys[a]) * rho * A.geom[a].vol / 4.0 * square;
                            out[a] += dFreq * (stiff - massPart) / double(cluster.size());
                        }
                    }, 512);
                }
                break;
            }
            default:
                error = "a measure that the optimisation does not know";
                return false;
            }
        }
        return true;
    };

    struct Adapter : topopt::Physics
    {
        int count = 0;
        double* partTotal = nullptr;
        std::function<void(std::vector<char>&, std::vector<char>&)> fixedFn;
        std::function<bool(int, double, const std::vector<Eigen::Vector3f>&, double, std::string&)> symmetricFn;
        std::function<bool(const std::vector<double>&, bool, int, std::vector<double>&, std::vector<std::vector<double>>&)> measuresFn;
        std::function<std::vector<double>()> saveFn;
        std::function<void(const std::vector<double>&)> restoreFn;
        std::function<void(bool)> tightFn;
        std::function<void(const topopt::IterationView&)> keptFn;
        int measureCount() const override { return count; }
        void partVolume(double volume) override { *partTotal = volume; }
        void fixedElements(std::vector<char>& state, std::vector<char>& supported) override { fixedFn(state, supported); }
        bool symmetricAbout(int axis, double at, const std::vector<Eigen::Vector3f>& mirrored, double regionMismatch, std::string& why) override
        {
            return symmetricFn(axis, at, mirrored, regionMismatch, why);
        }
        bool evaluate(const std::vector<double>& x, bool update, int iteration, std::vector<double>& values, std::vector<std::vector<double>>& sens,
                      std::string&) override
        {
            return measuresFn(x, update, iteration, values, sens);
        }
        std::vector<double> saveState() const override { return saveFn(); }
        void restoreState(const std::vector<double>& state) override { restoreFn(state); }
        void tightSolves(bool on) override { tightFn(on); }
        void iterationKept(const topopt::IterationView& view) override { keptFn(view); }
    } adapter;
    adapter.count = nb;
    adapter.partTotal = &total;
    adapter.fixedFn = fixedElements;
    adapter.symmetricFn = symmetricAbout;
    adapter.measuresFn = measuresOf;
    adapter.saveFn = [&]() { return peakFactor; };
    adapter.restoreFn = [&](const std::vector<double>& state) { peakFactor = state; };
    double savedSolveTol = 0, savedModeTol = 0;
    int savedIterations = 0;
    adapter.tightFn = [&](bool on) {
        if (on)
        {
            savedSolveTol = solveTol;
            savedModeTol = modeTol;
            savedIterations = cgIterations;
            solveTol = 1e-10;
            modeTol = 1e-10;
            cgIterations = std::max(cgIterations, 5000);
        }
        else
        {
            solveTol = savedSolveTol;
            modeTol = savedModeTol;
            cgIterations = savedIterations;
        }
    };
    adapter.keptFn = [&](const topopt::IterationView& view) {
        // The design as it is, for the viewport: the part where the density is above the level that keeps the material the design has
        // (so the first iterations, whose density is all the same, show the whole part)
        auto live = std::make_shared<run_progress::LiveView>();
        live->kind = "topology";
        live->quantity = s.objectiveLabel.empty() ? "objective" : s.objectiveLabel;
        live->iteration = view.iteration;
        live->iterations = view.iterations;
        live->objective = view.objective;
        live->change = view.change;
        double kept = 0;
        for (size_t a = 0; a < na; ++a) kept += vol[a] * view.xPhys[a];
        live->volume = view.share;
        live->volumeLow = live->volumeHigh = live->volume;
        live->history.assign(view.history.begin(), view.history.end());
        double lo = 0, hi = 1;
        for (int k = 0; k < 24; ++k)
        {
            const double mid = 0.5 * (lo + hi);
            double keptAbove = 0;
            for (size_t a = 0; a < na; ++a)
                if (view.xPhys[a] > mid) keptAbove += vol[a];
            if (keptAbove > kept) lo = mid; else hi = mid;
        }
        // The part as the result shows it: where the density (a field on the mesh) is above that level, within the design space,
        // coloured by the density
        auto density = std::make_shared<MeshResult>();
        density->mesh = m_mesh;
        if (!liveLocator) liveLocator = std::make_shared<TetLocator>(mesh);
        density->locator = liveLocator;
        for (auto& f : density->fields) f.assign(nv, 0.0f);
        density->fields[0] = view.nodal;
        density->minValue[0] = 0;
        density->maxValue[0] = 1;
        const Tree field = meshFieldTree(density, 0);
        run_progress::LiveShape part;
        part.tree = max((Tree(float(lo)) - field) * Tree(float(2.0 * view.filterRadius)), m_shape);
        part.channels.push_back({"density", "density", field, 0.0f, 1.0f});
        part.detail = float(h);
        liveSurfaceIndexed(*m_mesh, view.nodal, lo, true, part.surfaceVerts, part.surfaceTris);
        live->shapes.push_back(std::move(part));
        run_progress::publishLive(live);
    };

    topopt::DesignResult res;
    if (!topopt::runDensityDesign(mesh, vol, h, A.vtStart, A.vtList, s, adapter, res, error, cancel)) return false;

    // The results, as the problem keeps them: the objective, every measure and every output at each iteration; the density per tetrahedron, and as a
    // field on the mesh (each tetrahedron's averaged at the nodes by volume)
    m_history = res.history;
    m_baseHistory = res.baseHistory;
    m_outputHistory = res.outputHistory;
    m_densityHistory = res.densityHistory;
    m_mirrorsUsed = res.mirrorsUsed;
    m_topDensity.assign(na, 0.0f);
    for (size_t a = 0; a < na; ++a) m_topDensity[a] = float(res.xPhys[a]);
    auto R2 = std::make_shared<MeshResult>();
    R2->mesh = m_mesh;
    R2->locator = std::make_shared<TetLocator>(mesh);
    for (auto& f : R2->fields) f.assign(nv, 0.0f);
    R2->fields[0] = res.nodal;
    R2->minValue[0] = 0;
    R2->maxValue[0] = 1;
    R2->iterations = res.iterations;
    R2->compliance = m_history.empty() ? 0 : m_history.back();
    R2->volume = res.designVolume;
    m_designVolume = res.designVolume;
    m_frontier = res.frontier;
    R2->elements = int(na);
    R2->seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    m_densityResult = R2;
    // (the run stopped at the edge of its space: the design it stopped at, for the run in a larger space)
    m_resume = res.resume;
    if (!m_resume.empty())
    {
        auto R3 = std::make_shared<MeshResult>();
        R3->mesh = m_mesh;
        R3->locator = R2->locator;
        for (auto& f : R3->fields) f.assign(nv, 0.0f);
        R3->fields[0] = res.designNodal;
        R3->minValue[0] = 0;
        R3->maxValue[0] = 1;
        R3->elements = int(na);
        m_designResult = R3;
    }
    return true;
}

namespace {

// The share of a tetrahedron's volume where the field that is linear through the values v[0..3] at its corners is above `level`.  The
// corners above it and below it are counted, and each case is the volume of what the plane cuts off -- written with the distances to the
// level (all positive), so that nothing cancels when two corners are nearly alike
double shareAbove(const double* v, double level)
{
    double up[4], down[4];
    int nu = 0, nd = 0;
    for (int i = 0; i < 4; ++i)
    {
        if (v[i] > level) up[nu++] = v[i] - level;
        else down[nd++] = level - v[i];
    }
    (void)nd;
    switch (nu)
    {
    case 0: return 0.0;
    case 4: return 1.0;
    case 1: return up[0] * up[0] * up[0] / ((up[0] + down[0]) * (up[0] + down[1]) * (up[0] + down[2]));
    case 3: return 1.0 - down[0] * down[0] * down[0] / ((down[0] + up[0]) * (down[0] + up[1]) * (down[0] + up[2]));
    default:
    {
        const double p = up[0], q = up[1], u = down[0], w = down[1];
        const double below = (p * q * (u * u + u * w + w * w) + (p + q) * u * w * (u + w) + u * u * w * w) /
                             ((p + u) * (q + u) * (p + w) * (q + w));
        return 1.0 - below;
    }
    }
}

}   // anonymous namespace

double TetProblem::cutThreshold() const
{
    if (!m_mesh || !m_densityResult || !(m_designVolume > 0) || m_densityResult->fields[0].size() != m_mesh->pos.size()) return 0.5;
    const TetMesh& mesh = *m_mesh;
    const std::vector<float>& dens = m_densityResult->fields[0];
    const size_t nt = mesh.tets.size();
    std::vector<double> vol(nt);
    for (size_t t = 0; t < nt; ++t)
    {
        const auto& c = mesh.tets[t];
        const Vec3 p0 = mesh.pos[size_t(c[0])];
        vol[t] = std::abs((mesh.pos[size_t(c[1])] - p0).dot((mesh.pos[size_t(c[2])] - p0).cross(mesh.pos[size_t(c[3])] - p0))) / 6.0;
    }
    auto volumeAbove = [&](double level) {
        double sum = 0;
        for (size_t t = 0; t < nt; ++t)
        {
            const auto& c = mesh.tets[t];
            const double v[4] = {double(dens[size_t(c[0])]), double(dens[size_t(c[1])]), double(dens[size_t(c[2])]), double(dens[size_t(c[3])])};
            sum += vol[t] * shareAbove(v, level);
        }
        return sum;
    };
    double a = 0.0, b = 1.0;
    for (int i = 0; i < 40; ++i)
    {
        const double mid = 0.5 * (a + b);
        if (volumeAbove(mid) > m_designVolume) a = mid;
        else b = mid;
    }
    return 0.5 * (a + b);
}

bool TetProblem::finalSurface(double level, std::vector<float>& verts, std::vector<uint32_t>& tris) const
{
    if (!m_mesh || !m_densityResult || m_densityResult->fields[0].size() != m_mesh->pos.size()) return false;
    liveSurfaceIndexed(*m_mesh, m_densityResult->fields[0], level, true, verts, tris);
    return true;
}

bool TetProblem::densitySurface(size_t k, double level, std::vector<float>& verts, std::vector<uint32_t>& tris) const
{
    if (!m_mesh || k >= m_densityHistory.size() || m_densityHistory[k].size() != m_mesh->pos.size()) return false;
    liveSurfaceIndexed(*m_mesh, m_densityHistory[k], level, true, verts, tris);
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

bool TetProblem::modal(int count, double density, int maxIterations, double tolerance, std::string& error,
                       const std::atomic<bool>* cancel)
{
    if (count < 1)
    {
        error = "ask for at least one mode";
        return false;
    }
    if (!(density > 0) && !m_densityField.is_valid())
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
    std::vector<double> mass(n, 0.0);
    if (m_quad)
    {
        // The 10-node elements.  The lumped mass of an element is its mass shared out by the diagonal of its consistent mass matrix (a corner's
        // share is 1/36, a midside node's 4/27 of it: the rule for straight-sided ten-node tetrahedra, the one that keeps every mass positive)
        const QuadMesh& q = *m_quad;
        buildPatternQuad(q, A);
        assembleQuad(q, D, m_scale.empty() ? nullptr : &m_scale, A);
        buildPreconditioner(m_fixedDof, A);
        std::vector<double> volume;
        quadVolumes(q, volume);
        for (size_t t = 0; t < q.elem.size(); ++t)
        {
            const double rho = m_elementDensity.empty() ? density : m_elementDensity[t];
            for (int p = 0; p < 10; ++p)
                for (int a = 0; a < 3; ++a)
                    mass[3 * size_t(q.elem[t][size_t(p)]) + size_t(a)] += rho * volume[t] * (p < 4 ? 1.0 / 36.0 : 4.0 / 27.0);
        }
    }
    else
    {
        buildPattern(mesh, A);
        assemble(mesh, D, m_scale.empty() ? nullptr : &m_scale, A);
        buildPreconditioner(m_fixedDof, A);
        // The lumped mass: a quarter of each element's mass at each of its nodes
        for (size_t t = 0; t < mesh.tets.size(); ++t)
        {
            const double rho = m_elementDensity.empty() ? density : m_elementDensity[t];
            for (int p = 0; p < 4; ++p)
                for (int a = 0; a < 3; ++a) mass[3 * size_t(mesh.tets[t][size_t(p)]) + size_t(a)] += rho * A.geom[t].vol / 4.0;
        }
    }
    for (size_t i = 0; i < n; ++i) if (m_fixedDof[i]) mass[i] = 0.0;

    task.set(0.1, "finding the modes");
    std::vector<double> lambda;
    std::vector<std::vector<double>> vectors;
    int iterations = 0;
    // (the block eigensolver converges in tens of iterations; the number asked for is a floor)
    if (!lobpcg(A, m_fixedDof, mass, count, std::max(maxIterations, 300), tolerance, cancel, lambda, vectors, iterations, error))
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
        R->elements = int(m_quad ? m_quad->elem.size() : mesh.tets.size());
        R->nodes = int(nv);
        R->seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        m_modes.push_back(R);
    }
    return true;
}

////////////////////////////////////////////////////////////////////////////////
// The 10-node elements

namespace {

// A node's fields from the volume-weighted sums of the stresses of the elements around it (acc: xx yy zz xy yz zx, then the strain energy
// density) and its displacement
void setNodalFields(MeshResult& res, size_t i, double w, const double acc[7], double ux, double uy, double uz)
{
    if (!(w > 0)) return;
    const double sxx = acc[0] / w, syy = acc[1] / w, szz = acc[2] / w, sxy = acc[3] / w, syz = acc[4] / w, szx = acc[5] / w;
    const double vm = std::sqrt(0.5 * ((sxx - syy) * (sxx - syy) + (syy - szz) * (syy - szz) + (szz - sxx) * (szz - sxx)) +
                                3 * (sxy * sxy + syz * syz + szx * szx));
    Eigen::Matrix3d S;
    S << sxx, sxy, szx, sxy, syy, syz, szx, syz, szz;
    const Vec3 pr = Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d>(S, Eigen::EigenvaluesOnly).eigenvalues();
    res.fields[Result::VON_MISES][i] = float(vm);
    res.fields[Result::SXX][i] = float(sxx);
    res.fields[Result::SYY][i] = float(syy);
    res.fields[Result::SZZ][i] = float(szz);
    res.fields[Result::SXY][i] = float(sxy);
    res.fields[Result::SYZ][i] = float(syz);
    res.fields[Result::SZX][i] = float(szx);
    res.fields[Result::MAX_PRINCIPAL][i] = float(pr[2]);
    res.fields[Result::MIN_PRINCIPAL][i] = float(pr[0]);
    res.fields[Result::STRAIN_ENERGY][i] = float(acc[6] / w);
    res.fields[Result::UX][i] = float(ux);
    res.fields[Result::UY][i] = float(uy);
    res.fields[Result::UZ][i] = float(uz);
    res.fields[Result::DISPLACEMENT][i] = float(std::sqrt(ux * ux + uy * uy + uz * uz));
}

}   // anonymous namespace

bool TetProblem::solveQuad(int maxIterations, double tolerance, std::string& error, const std::atomic<bool>* cancel)
{
    const auto t0 = std::chrono::steady_clock::now();
    run_progress::Task task("solving");
    task.set(0.0, "assembling the stiffness matrix");

    const QuadMesh& q = *m_quad;
    const TetMesh& mesh = *m_mesh;          // (the nodes cut into linear tetrahedra: what the results are on)
    const size_t nn = q.pos.size(), ne = q.elem.size(), n = 3 * nn;
    const Mat6 D = elasticity(m_E, m_nu);
    Assembly A;
    buildPatternQuad(q, A);
    assembleQuad(q, D, m_scale.empty() ? nullptr : &m_scale, A);
    buildPreconditioner(m_fixedDof, A);
    task.set(0.05, "preparing the solver");

    const std::vector<unsigned char>& fixed = m_fixedDof;
    std::vector<double> u(n, 0.0);
    // (the displacement a support imposes: see solve)
    std::vector<double> rhsImposed;
    const std::vector<double>* rhs = &m_force;
    if (!m_prescribed.empty())
    {
        std::vector<double> Kp(n);
        A.K.matvec(m_prescribed.data(), Kp.data());
        rhsImposed = m_force;
        for (size_t i = 0; i < n; ++i) if (!fixed[i]) rhsImposed[i] -= Kp[i];
        rhs = &rhsImposed;
    }
    CgResult cg;
    if (!cgSolve(A, fixed, *rhs, u, tolerance, maxIterations, cancel,
                 [&](double frac, const std::string& text) { task.set(0.05 + 0.9 * frac, text); }, cg, error))
        return false;
    if (!m_prescribed.empty())
        for (size_t i = 0; i < n; ++i) if (fixed[i]) u[i] = m_prescribed[i];
    const int it = cg.iterations;
    const double rel = cg.residual;
    if (rel > std::max(tolerance * 100, 1e-3))
    {
        error = "the solver did not converge (" + std::to_string(it) + " iterations, residual " +
                std::to_string(rel) + "): the supports may not hold the part in place";
        return false;
    }
    if (std::getenv("FIELDES_FEA_DEBUG"))
        fprintf(stderr, "[tetfea] %zu nodes, %zu 10-node tetrahedra, %d iterations, residual %.2e\n", nn, ne, it, rel);

    // (held in every direction but still free to turn: see solve)
    {
        Vec3 lo = q.pos[0], hi = q.pos[0];
        for (const auto& p : q.pos)
        {
            lo = lo.cwiseMin(p);
            hi = hi.cwiseMax(p);
        }
        double umax = 0;
        for (size_t i = 0; i < n; ++i) umax = std::max(umax, std::abs(u[i]));
        if (!(umax < 10 * (hi - lo).maxCoeff()))
        {
            error = "the supports don't hold the part: fix more of it, or in more directions";
            return false;
        }
    }

    // Reactions at the supports, compliance (see the same lines of the solver of the linear element)
    std::vector<double> Ku(n);
    A.K.matvec(u.data(), Ku.data());
    Vec3 reaction = Vec3::Zero();
    double compliance = 0;
    const bool displaced = !m_prescribed.empty();
    for (size_t i = 0; i < n; ++i)
    {
        if (fixed[i] && (!displaced || m_prescribed[i] != 0.0)) reaction[int(i % 3)] += Ku[i] - m_force[i];
        compliance += displaced ? u[i] * Ku[i] : m_force[i] * u[i];
    }

    std::vector<double> volume;
    quadVolumes(q, volume);
    auto elementU = [&](size_t e, double ue[30]) {
        for (int p = 0; p < 10; ++p)
            for (int a = 0; a < 3; ++a) ue[3 * p + a] = u[3 * size_t(q.elem[e][size_t(p)]) + size_t(a)];
    };
    // The stress (and the strain energy density) of element e at a point of it: linear in the element, from the quadratic displacement
    auto stressAt = [&](size_t e, const double xi[3], const double ue[30], Vec6& sig, double& energy) {
        Vec6 eps = quadStrainAt(q, e, xi, ue);
        if (!m_thermalStrain.empty())
            for (int c = 0; c < 3; ++c) eps[c] -= m_thermalStrain[e];       // (the stress-free part)
        sig = (m_scale.empty() ? 1.0 : m_scale[e]) * (D * eps);
        energy = 0.5 * sig.dot(eps);
    };

    auto res = std::make_shared<MeshResult>();
    res->mesh = m_mesh;
    // The fields at the nodes: the stress at a node is the average, by volume, of what each element round it has there
    for (auto& f : res->fields) f.assign(nn, 0.0f);
    parallelRange(nn, [&](size_t b0, size_t b1) {
        for (size_t i = b0; i < b1; ++i)
        {
            double w = 0, acc[7] = {0, 0, 0, 0, 0, 0, 0};
            for (uint32_t ee = A.vtStart[i]; ee < A.vtStart[i + 1]; ++ee)
            {
                const size_t e = A.vtList[ee];
                int li = 0;
                for (int p = 0; p < 10; ++p) if (size_t(q.elem[e][size_t(p)]) == i) li = p;
                double ue[30];
                elementU(e, ue);
                Vec6 sig;
                double energy = 0;
                stressAt(e, kNodeXi[li], ue, sig, energy);
                w += volume[e];
                for (int c = 0; c < 6; ++c) acc[c] += volume[e] * sig[c];
                acc[6] += volume[e] * energy;
            }
            setNodalFields(*res, i, w, acc, u[3 * i], u[3 * i + 1], u[3 * i + 2]);
        }
    }, 64);
    // The stress of each of the linear tetrahedra the elements are cut into: the element's at their centre
    res->elementStress.resize(8 * ne);
    parallelRange(ne, [&](size_t b0, size_t b1) {
        for (size_t e = b0; e < b1; ++e)
        {
            double ue[30];
            elementU(e, ue);
            const int (*sub)[4] = kSubTets[q.diagonal[e]];
            for (int s = 0; s < 8; ++s)
            {
                double xi[3] = {0, 0, 0};
                for (int p = 0; p < 4; ++p)
                    for (int k = 0; k < 3; ++k) xi[k] += 0.25 * kNodeXi[sub[s][p]][k];
                Vec6 sig;
                double energy = 0;
                stressAt(e, xi, ue, sig, energy);
                for (int c = 0; c < 6; ++c) res->elementStress[8 * e + size_t(s)][size_t(c)] = float(sig[c]);
                res->elementStress[8 * e + size_t(s)][6] = float(energy);
            }
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
    res->elements = int(ne);
    res->nodes = int(nn);
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
    for (double v : volume) vol += v;
    res->volume = vol;
    res->seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    m_result = res;
    return true;
}

}   // namespace fea
}   // namespace libfive
