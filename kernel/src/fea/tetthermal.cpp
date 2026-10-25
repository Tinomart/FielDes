/*
libfive: a CAD kernel for modeling with implicit functions

Steady-state heat conduction on a body-fitted tetrahedral mesh; see tetthermal.hpp.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <numeric>

#include "libfive/fea/tetthermal.hpp"
#include "libfive/run_progress.hpp"
#include "libfive/tree/content_key.hpp"
#include "tet_common.hpp"

namespace libfive {
namespace fea {

using namespace tet;

TetThermalProblem::TetThermalProblem(const Tree& shape, Eigen::Vector3d lo, Eigen::Vector3d hi, double h,
                                     double conductivity)
    : m_shape(shape), m_lo(lo), m_hi(hi), m_h(h), m_k(conductivity)
{
}

void TetThermalProblem::addTemperature(const Tree& region, double value)
{
    m_temperatures.push_back({region, value});
    m_prepared = false;
}

void TetThermalProblem::addHeat(const Tree& region, double watts)
{
    m_heats.push_back({region, watts, false});
    m_prepared = false;
}

void TetThermalProblem::addGeneration(const Tree& region, double watts)
{
    m_heats.push_back({region, watts, true});
    m_prepared = false;
}

void TetThermalProblem::addConvection(const Tree& region, double coefficient, double ambient)
{
    m_convections.push_back({region, coefficient, ambient});
    m_prepared = false;
}

void TetThermalProblem::setConductivityField(const Tree& k)
{
    m_conductivityField = k;
    m_prepared = false;
}

void TetThermalProblem::addTemperatureField(const Tree& region, double value, const Tree& field)
{
    m_temperatures.push_back({region, value, field});
    m_prepared = false;
}

void TetThermalProblem::addHeatProfile(const Tree& region, double watts, const Tree& profile)
{
    m_heats.push_back({region, watts, false, profile});
    m_prepared = false;
}

void TetThermalProblem::addGenerationProfile(const Tree& region, double watts, const Tree& profile)
{
    m_heats.push_back({region, watts, true, profile});
    m_prepared = false;
}

void TetThermalProblem::addConvectionFields(const Tree& region, double coefficient, const Tree& coefficientField,
                                            double ambient, const Tree& ambientField)
{
    m_convections.push_back({region, coefficient, ambient, coefficientField, ambientField});
    m_prepared = false;
}

bool TetThermalProblem::prepare(std::string& error)
{
    if (!(m_h > 0) || !(m_k > 0))
    {
        error = "invalid element size or conductivity";
        return false;
    }
    if (m_temperatures.empty() && m_convections.empty())
    {
        error = "no boundary condition holds the temperature: add fixed_temperature(...) or convection(...)";
        return false;
    }
    run_progress::Task task("meshing");
    task.set(0.0, "meshing the part");
    auto mesh = std::make_shared<TetMesh>();
    // (a shape that is symmetric about a plane through the middle of its region is meshed on one side of the plane and mirrored: the mesh of a symmetric problem
    // is then exactly symmetric, and so are the temperature and the design on it -- a lattice cut by the shape cannot be, see meshShapeMirrored)
    if (!meshShapeMirrored(m_shape, m_lo, m_hi, m_h, findMeshMirrors(m_shape, m_lo, m_hi, m_h), *mesh, error)) return false;
    task.set(0.5, "resolving the boundary conditions");

    // A node within a small fraction of an element of a region counts as inside it
    const float reach = float(0.05 * m_h);
    auto positions = [](const TetMesh& m) {
        std::vector<Eigen::Vector3f> pts(m.pos.size());
        for (size_t i = 0; i < pts.size(); ++i) pts[i] = m.pos[i].cast<float>();
        return pts;
    };
    // The boundary triangles a region covers: those with all three corners inside it, else
    // those whose centre is inside it
    // (a region that misses the part by a little still reaches it: the second pass takes what lies within
    // half an element)
    auto trianglesIn = [&](const TetMesh& m, const Tree& region, const std::vector<Eigen::Vector3f>& pts,
                           const std::vector<Eigen::Vector3f>& centres, std::vector<char>& take) {
        std::vector<float> vn, vc;
        evalTreePoints(region, pts, vn);
        size_t n = 0;
        for (int pass = 0; pass < 2 && n == 0; ++pass)
        {
            const float rch = pass == 0 ? reach : 10 * reach;
            take.assign(m.faces.size(), 0);
            for (size_t f = 0; f < take.size(); ++f)
            {
                const auto& tri = m.faces[f];
                take[f] = vn[size_t(tri[0])] <= rch && vn[size_t(tri[1])] <= rch && vn[size_t(tri[2])] <= rch;
                n += take[f];
            }
            if (n == 0)
            {
                if (vc.empty()) evalTreePoints(region, centres, vc);
                for (size_t f = 0; f < take.size(); ++f)
                {
                    take[f] = vc[f] <= rch;
                    n += take[f];
                }
            }
        }
        return n;
    };
    auto faceArea = [&](const TetMesh& m, size_t f) {
        const auto& tri = m.faces[f];
        return 0.5 * (m.pos[size_t(tri[1])] - m.pos[size_t(tri[0])]).cross(m.pos[size_t(tri[2])] - m.pos[size_t(tri[0])]).norm();
    };

    // Everything the boundary conditions say, on a mesh
    std::vector<unsigned char> fixed;
    std::vector<double> fixedValue, source, sourceFixed;
    std::vector<ConvTri> conv;
    double heatIn = 0;
    auto resolve = [&](const TetMesh& m, std::string& err) {
        const size_t nv = m.pos.size(), nt = m.tets.size();
        fixed.assign(nv, 0);
        fixedValue.assign(nv, 0.0);
        source.assign(nv, 0.0);
        sourceFixed.assign(nv, 0.0);
        conv.clear();
        heatIn = 0;
        const auto pts = positions(m);
        std::vector<Eigen::Vector3f> centres(m.faces.size());
        for (size_t f = 0; f < m.faces.size(); ++f)
        {
            const auto& tri = m.faces[f];
            centres[f] = ((m.pos[size_t(tri[0])] + m.pos[size_t(tri[1])] + m.pos[size_t(tri[2])]) / 3.0).cast<float>();
        }
        std::vector<float> v;
        for (const auto& t : m_temperatures)
        {
            evalTreePoints(t.region, pts, v);
            float rch = reach;
            bool any = false;
            for (float x : v) any = any || x <= rch;
            if (!any) rch = 10 * reach;
            std::vector<float> atNodes;
            if (t.field.is_valid()) evalTreePoints(t.field, pts, atNodes);
            for (size_t i = 0; i < nv; ++i)
                if (v[i] <= rch)
                {
                    if (!atNodes.empty() && !std::isfinite(atNodes[i]))
                    {
                        err = "a held temperature's field isn't defined everywhere in its region";
                        return false;
                    }
                    fixed[i] = 1;
                    fixedValue[i] = atNodes.empty() ? t.value : double(atNodes[i]);
                }
        }
        for (const auto& hq : m_heats)
        {
            if (hq.volume)
            {
                // the tetrahedra whose centre lies inside the region, by volume
                std::vector<Eigen::Vector3f> tc(nt);
                std::vector<double> vol(nt);
                for (size_t t = 0; t < nt; ++t)
                {
                    const auto& tv = m.tets[t];
                    const Vec3 c = (m.pos[size_t(tv[0])] + m.pos[size_t(tv[1])] + m.pos[size_t(tv[2])] + m.pos[size_t(tv[3])]) / 4.0;
                    tc[t] = c.cast<float>();
                    vol[t] = std::abs((m.pos[size_t(tv[1])] - m.pos[size_t(tv[0])]).cross(m.pos[size_t(tv[2])] - m.pos[size_t(tv[0])])
                                          .dot(m.pos[size_t(tv[3])] - m.pos[size_t(tv[0])])) / 6.0;
                }
                evalTreePoints(hq.region, tc, v);
                std::vector<float> prof;
                if (hq.profile.is_valid()) evalTreePoints(hq.profile, tc, prof);
                auto weight = [&](size_t t) {
                    if (prof.empty()) return vol[t];
                    const double w = double(prof[t]);
                    return std::isfinite(w) && w > 0 ? vol[t] * w : 0.0;
                };
                double sel = 0, selVolume = 0;
                for (size_t t = 0; t < nt; ++t) if (v[t] <= reach) { sel += weight(t); selVolume += vol[t]; }
                if (!(selVolume > 0))
                {
                    err = "a heat generation region doesn't touch the part";
                    return false;
                }
                if (!(sel > 0))
                {
                    err = "a heat generation's profile is zero inside its region";
                    return false;
                }
                for (size_t t = 0; t < nt; ++t)
                {
                    if (!(v[t] <= reach)) continue;
                    const double share = hq.watts * weight(t) / sel / 4.0;
                    for (int p = 0; p < 4; ++p) source[size_t(m.tets[t][size_t(p)])] += share;
                }
                heatIn += hq.watts;
                continue;
            }
            std::vector<char> take;
            if (trianglesIn(m, hq.region, pts, centres, take) > 0)
            {
                std::vector<float> prof;
                if (hq.profile.is_valid()) evalTreePoints(hq.profile, centres, prof);
                auto weight = [&](size_t f) {
                    if (prof.empty()) return faceArea(m, f);
                    const double w = double(prof[f]);
                    return std::isfinite(w) && w > 0 ? faceArea(m, f) * w : 0.0;
                };
                double sel = 0;
                for (size_t f = 0; f < take.size(); ++f) if (take[f]) sel += weight(f);
                if (!(sel > 0))
                {
                    err = "a heat input's profile is zero over its region";
                    return false;
                }
                for (size_t f = 0; f < take.size(); ++f)
                {
                    if (!take[f]) continue;
                    const double share = hq.watts * weight(f) / sel / 3.0;
                    for (int p = 0; p < 3; ++p)
                    {
                        source[size_t(m.faces[f][size_t(p)])] += share;
                        sourceFixed[size_t(m.faces[f][size_t(p)])] += share;
                    }
                }
            }
            else
            {
                // a region holding no surface: over the nodes inside it
                evalTreePoints(hq.region, pts, v);
                std::vector<float> prof;
                if (hq.profile.is_valid()) evalTreePoints(hq.profile, pts, prof);
                auto weight = [&](size_t i) {
                    if (prof.empty()) return 1.0;
                    const double w = double(prof[i]);
                    return std::isfinite(w) && w > 0 ? w : 0.0;
                };
                size_t count = 0;
                double sel = 0;
                for (size_t i = 0; i < nv; ++i) if (v[i] <= reach) { ++count; sel += weight(i); }
                if (count == 0)
                {
                    err = "a heat input region doesn't touch the part";
                    return false;
                }
                if (!(sel > 0))
                {
                    err = "a heat input's profile is zero over its region";
                    return false;
                }
                for (size_t i = 0; i < nv; ++i)
                    if (v[i] <= reach)
                    {
                        source[i] += hq.watts * weight(i) / sel;
                        sourceFixed[i] += hq.watts * weight(i) / sel;
                    }
            }
            heatIn += hq.watts;
        }
        for (const auto& c : m_convections)
        {
            std::vector<char> take;
            if (trianglesIn(m, c.region, pts, centres, take) == 0)
            {
                err = "a convection region doesn't touch the part";
                return false;
            }
            std::vector<float> hf, af;
            if (c.coefficientField.is_valid()) evalTreePoints(c.coefficientField, centres, hf);
            if (c.ambientField.is_valid()) evalTreePoints(c.ambientField, centres, af);
            for (size_t f = 0; f < take.size(); ++f)
            {
                if (!take[f]) continue;
                const double h = hf.empty() ? c.coefficient : double(hf[f]);
                const double amb = af.empty() ? c.ambient : double(af[f]);
                if (!std::isfinite(h) || h < 0 || !std::isfinite(amb))
                {
                    err = "a convection's coefficient must not be negative, and its ambient temperature must be defined, everywhere "
                          "on its surface";
                    return false;
                }
                conv.push_back({m.faces[f][0], m.faces[f][1], m.faces[f][2], faceArea(m, f), h, amb});
            }
        }
        return true;
    };
    if (!resolve(*mesh, error)) return false;

    // Material nothing holds at a temperature -- no fixed node, no convecting surface -- has no
    // temperature of its own: left out, and counted
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
        for (size_t i = 0; i < nv; ++i) if (fixed[i]) held[find(uint32_t(i))] = 1;
        for (const auto& c : conv) held[find(uint32_t(c.a))] = 1;
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
            if (!resolve(*mesh, error)) return false;
        }
    }
    {
        size_t nFixed = 0;
        for (auto f : fixed) nFixed += f != 0;
        if (nFixed == 0 && conv.empty())
        {
            error = "the boundary conditions don't touch the part: the temperature isn't determined";
            return false;
        }
    }

    m_elementK.clear();
    if (m_conductivityField.is_valid())
    {
        std::vector<Eigen::Vector3f> tc(mesh->tets.size());
        for (size_t t = 0; t < tc.size(); ++t)
        {
            const auto& tv = mesh->tets[t];
            tc[t] = ((mesh->pos[size_t(tv[0])] + mesh->pos[size_t(tv[1])] + mesh->pos[size_t(tv[2])] + mesh->pos[size_t(tv[3])]) / 4.0)
                        .cast<float>();
        }
        std::vector<float> kv;
        evalTreePoints(m_conductivityField, tc, kv);
        m_elementK.resize(tc.size());
        for (size_t t = 0; t < tc.size(); ++t)
        {
            if (!std::isfinite(kv[t]) || !(kv[t] > 0))
            {
                char buf[200];
                snprintf(buf, sizeof(buf), "the conductivity field must be positive everywhere in the part (it is %g at (%.4g, %.4g, %.4g))",
                         double(kv[t]), double(tc[t].x()), double(tc[t].y()), double(tc[t].z()));
                error = buf;
                return false;
            }
            m_elementK[t] = double(kv[t]);
        }
    }
    uint64_t hsh = 1469598103934665603ull;
    const double hdr[] = {m_h, m_k, double(mesh->pos.size()), double(mesh->tets.size())};
    hsh = fnv(hsh, hdr, sizeof(hdr));
    hsh = fnv(hsh, mesh->pos.data(), mesh->pos.size() * sizeof(Vec3));
    hsh = fnv(hsh, mesh->tets.data(), mesh->tets.size() * sizeof(std::array<int, 4>));
    hsh = fnv(hsh, fixed.data(), fixed.size());
    hsh = fnv(hsh, fixedValue.data(), fixedValue.size() * sizeof(double));
    hsh = fnv(hsh, source.data(), source.size() * sizeof(double));
    hsh = fnv(hsh, conv.data(), conv.size() * sizeof(ConvTri));
    if (!m_elementK.empty()) hsh = fnv(hsh, m_elementK.data(), m_elementK.size() * sizeof(double));
    m_hash = hsh;

    m_mesh = mesh;
    m_fixed.swap(fixed);
    m_fixedValue.swap(fixedValue);
    m_source.swap(source);
    m_sourceFixed.swap(sourceFixed);
    m_conv.swap(conv);
    m_heatIn = heatIn;
    m_prepared = true;
    return true;
}

bool TetThermalProblem::solve(int maxIterations, double tolerance, std::string& error, const std::atomic<bool>* cancel)
{
    if (!m_prepared && !prepare(error)) return false;
    const auto t0 = std::chrono::steady_clock::now();
    run_progress::Task task("solving");
    task.set(0.0, "assembling the conduction matrix");

    const TetMesh& mesh = *m_mesh;
    const size_t nv = mesh.pos.size(), nt = mesh.tets.size();
    Assembly A;
    buildPattern(mesh, A);
    std::vector<double> val(A.K.col.size(), 0.0);
    auto entry = [&](size_t i, size_t j) {
        return size_t(std::lower_bound(A.K.col.begin() + A.K.rowPtr[i], A.K.col.begin() + A.K.rowPtr[i + 1], uint32_t(j)) -
                      A.K.col.begin());
    };
    // conduction: k V grad N_i . grad N_j, row by row
    parallelRange(nv, [&](size_t b0, size_t b1) {
        for (size_t i = b0; i < b1; ++i)
            for (uint32_t e = A.vtStart[i]; e < A.vtStart[i + 1]; ++e)
            {
                const size_t t = A.vtList[e];
                const auto& tv = mesh.tets[t];
                int li = 0;
                for (int p = 0; p < 4; ++p) if (size_t(tv[size_t(p)]) == i) li = p;
                for (int lj = 0; lj < 4; ++lj)
                {
                    const double d = A.geom[t].g[li][0] * A.geom[t].g[lj][0] + A.geom[t].g[li][1] * A.geom[t].g[lj][1] +
                                     A.geom[t].g[li][2] * A.geom[t].g[lj][2];
                    val[entry(i, size_t(tv[size_t(lj)]))] += (m_elementK.empty() ? m_k : m_elementK[t]) * A.geom[t].vol * d;
                }
            }
    }, 16);
    // convection: h times the triangle's mass matrix (2 on the diagonal, 1 off, over 12) and the ambient's load
    std::vector<double> b(nv, 0.0);
    for (size_t i = 0; i < nv; ++i) b[i] = m_source[i];
    for (const auto& c : m_conv)
    {
        const int v[3] = {c.a, c.b, c.c};
        for (int p = 0; p < 3; ++p)
        {
            for (int q = 0; q < 3; ++q) val[entry(size_t(v[p]), size_t(v[q]))] += c.h * c.area * (p == q ? 2.0 : 1.0) / 12.0;
            b[size_t(v[p])] += c.h * c.ambient * c.area / 3.0;
        }
    }
    task.set(0.05, "solving");

    // Held nodes: their rows become the identity; the free rows take what the held ones contribute
    const std::vector<unsigned char>& fixed = m_fixed;
    std::vector<double> T(nv, 0.0);
    {
        double mean = 0;
        size_t n = 0;
        for (size_t i = 0; i < nv; ++i) if (fixed[i]) { mean += m_fixedValue[i]; ++n; }
        for (const auto& c : m_conv) { mean += c.ambient; ++n; }
        const double t0v = n ? mean / double(n) : 0.0;
        for (size_t i = 0; i < nv; ++i) T[i] = fixed[i] ? m_fixedValue[i] : t0v;
    }
    auto matvecFull = [&](const std::vector<double>& x, std::vector<double>& y) {
        parallelRange(nv, [&](size_t b0, size_t b1) {
            for (size_t i = b0; i < b1; ++i)
            {
                double s = 0;
                for (uint32_t e = A.K.rowPtr[i]; e < A.K.rowPtr[i + 1]; ++e) s += val[e] * x[A.K.col[e]];
                y[i] = s;
            }
        }, 256);
    };
    std::vector<double> KT(nv);
    // the operator on the free nodes: y_i = sum over free j of K_ij x_j (held rows: identity)
    std::vector<double> xm(nv);
    auto apply = [&](const std::vector<double>& x, std::vector<double>& y) {
        for (size_t i = 0; i < nv; ++i) xm[i] = fixed[i] ? 0.0 : x[i];
        matvecFull(xm, y);
        for (size_t i = 0; i < nv; ++i) if (fixed[i]) y[i] = x[i];
    };
    // the right side: b minus what the held temperatures push into the free rows
    std::vector<double> Tc(nv, 0.0);
    for (size_t i = 0; i < nv; ++i) Tc[i] = fixed[i] ? m_fixedValue[i] : 0.0;
    matvecFull(Tc, KT);
    std::vector<double> rhs(nv);
    for (size_t i = 0; i < nv; ++i) rhs[i] = fixed[i] ? 0.0 : b[i] - KT[i];
    std::vector<double> invDiag(nv, 1.0);
    for (size_t i = 0; i < nv; ++i)
        if (!fixed[i])
        {
            const double d = val[entry(i, i)];
            invDiag[i] = d > 0 ? 1.0 / d : 1.0;
        }
    auto dotp = [&](const std::vector<double>& a, const std::vector<double>& c) {
        return parallelTotal(nv, [&](size_t b0, size_t b1) {
            double s = 0;
            for (size_t i = b0; i < b1; ++i) s += a[i] * c[i];
            return s;
        });
    };
    // x = T with the held entries zero (the unknown is T - the held part)
    std::vector<double> x(nv, 0.0), r(nv), z(nv), p(nv), Ap(nv);
    for (size_t i = 0; i < nv; ++i) x[i] = fixed[i] ? 0.0 : T[i];
    apply(x, Ap);
    for (size_t i = 0; i < nv; ++i) r[i] = fixed[i] ? 0.0 : rhs[i] - Ap[i];
    double bnorm = std::sqrt(dotp(rhs, rhs));
    if (!(bnorm > 0)) bnorm = 1.0;
    double rel = std::sqrt(dotp(r, r)) / bnorm;
    int it = 0;
    if (rel >= tolerance)
    {
        for (size_t i = 0; i < nv; ++i) z[i] = r[i] * invDiag[i];
        p = z;
        double rz = dotp(r, z);
        const double rel0 = std::max(rel, 1e-300);
        for (it = 0; it < maxIterations; ++it)
        {
            if (cancel && cancel->load())
            {
                error = "cancelled";
                return false;
            }
            apply(p, Ap);
            const double pAp = dotp(p, Ap);
            if (!(pAp > 0))
            {
                error = "the conduction matrix is singular: nothing holds the temperature";
                return false;
            }
            const double alpha = rz / pAp;
            for (size_t i = 0; i < nv; ++i)
            {
                x[i] += alpha * p[i];
                r[i] -= alpha * Ap[i];
            }
            rel = std::sqrt(dotp(r, r)) / bnorm;
            if (it % 50 == 0)
            {
                const double frac = std::max(0.0, std::min(1.0, std::log(rel0 / std::max(rel, 1e-300)) /
                                                                   std::log(rel0 / std::max(tolerance, 1e-300))));
                task.set(0.05 + 0.9 * frac, "solving: iteration " + std::to_string(it));
            }
            if (!std::isfinite(rel))
            {
                error = "the solver diverged";
                return false;
            }
            if (rel < tolerance)
            {
                ++it;
                break;
            }
            for (size_t i = 0; i < nv; ++i) z[i] = r[i] * invDiag[i];
            const double rzNew = dotp(r, z);
            const double beta = rzNew / rz;
            rz = rzNew;
            for (size_t i = 0; i < nv; ++i) p[i] = z[i] + beta * p[i];
        }
        if (rel > std::max(tolerance * 100, 1e-3))
        {
            error = "the solver did not converge (" + std::to_string(it) + " iterations, residual " +
                    std::to_string(rel) + ")";
            return false;
        }
    }
    for (size_t i = 0; i < nv; ++i) T[i] = fixed[i] ? m_fixedValue[i] : x[i];
    // The temperature the heat comes in at: the free nodes' temperature weighted by the heat that comes in at each
    {
        double q = 0, qt = 0;
        for (size_t i = 0; i < nv; ++i)
            if (!fixed[i] && m_source[i] != 0.0)
            {
                q += m_source[i];
                qt += m_source[i] * T[i];
            }
        m_inputTemperature = q > 0 ? qt / q : 0.0;
    }

    // Heat out through the held nodes (what K T - b leaves there) and by convection; each element's flux -k grad T
    matvecFull(T, KT);
    double outFixed = 0;
    // (the equation at a held node: conduction out = sources + what the sink takes away, so the sink takes b - K T)
    for (size_t i = 0; i < nv; ++i) if (fixed[i]) outFixed += b[i] - KT[i];
    double outConv = 0;
    for (const auto& c : m_conv)
    {
        const double tm = (T[size_t(c.a)] + T[size_t(c.b)] + T[size_t(c.c)]) / 3.0;
        outConv += c.h * c.area * (tm - c.ambient);
    }

    auto res = std::make_shared<MeshResult>();
    res->mesh = m_mesh;
    for (auto& f : res->fields) f.assign(nv, 0.0f);
    std::vector<Vec3> flux(nt);
    parallelRange(nt, [&](size_t b0, size_t b1) {
        for (size_t t = b0; t < b1; ++t)
        {
            Vec3 g = Vec3::Zero();
            for (int p2 = 0; p2 < 4; ++p2)
                for (int a = 0; a < 3; ++a) g[a] += T[size_t(mesh.tets[t][size_t(p2)])] * A.geom[t].g[p2][a];
            flux[t] = -(m_elementK.empty() ? m_k : m_elementK[t]) * g;
        }
    }, 256);
    // (each element's own flux, for the element display: qx qy qz and |q|)
    res->elementStress.resize(nt);
    for (size_t t = 0; t < nt; ++t)
        res->elementStress[t] = {float(flux[t].x()), float(flux[t].y()), float(flux[t].z()), float(flux[t].norm()), 0.0f, 0.0f, 0.0f};
    parallelRange(nv, [&](size_t b0, size_t b1) {
        for (size_t i = b0; i < b1; ++i)
        {
            Vec3 q = Vec3::Zero();
            double w = 0;
            for (uint32_t e = A.vtStart[i]; e < A.vtStart[i + 1]; ++e)
            {
                const size_t t = A.vtList[e];
                q += A.geom[t].vol * flux[t];
                w += A.geom[t].vol;
            }
            if (w > 0) q /= w;
            res->fields[0][i] = float(T[i]);
            res->fields[1][i] = float(q.norm());
            res->fields[2][i] = float(q.x());
            res->fields[3][i] = float(q.y());
            res->fields[4][i] = float(q.z());
        }
    }, 64);
    for (int f = 0; f < Result::FIELD_COUNT; ++f)
    {
        float mn = std::numeric_limits<float>::max(), mx = -std::numeric_limits<float>::max();
        for (float v : res->fields[f])
        {
            mn = std::min(mn, v);
            mx = std::max(mx, v);
        }
        res->minValue[f] = mn;
        res->maxValue[f] = mx;
    }
    res->locator = std::make_shared<TetLocator>(mesh);
    res->elements = int(nt);
    res->nodes = int(nv);
    res->dofs = int(nv);
    res->iterations = it;
    res->residual = rel;
    res->heatIn = m_heatIn;
    {
        double v = 0;
        for (size_t t = 0; t < nt; ++t) v += A.geom[t].vol;
        res->volume = v;
    }
    res->heatOutFixed = outFixed;
    res->heatOutConvection = outConv;
    res->looseElements = m_looseElements;
    res->seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    m_result = res;
    return true;
}

////////////////////////////////////////////////////////////////////////////////
// Topology optimisation: one density to each tetrahedron on the design core every analysis shares (density_design.cpp); the conductivity of an
// element is the material's times  kmin + (1 - kmin) rho^p  (SIMP).  The measures are numbers of the temperature -- its peak and its lowest, the
// peak heat flux, the temperature the heat comes in at, the temperature at a point -- each with the sensitivity to the density of every element
// from one adjoint solve:  dM/drho_e = dM/drho_e (explicit) - lambda^T (dK/drho_e) T,  K_ff lambda = dM/dT (the free nodes).

namespace {

// The share of a tetrahedron's volume where the field that is linear through the values v[0..3] at its corners is above `level`: the corners above
// it and below it are counted, and each case is the volume of what the plane cuts off, written with the distances to the level (all positive) so
// that nothing cancels when two corners are nearly alike
double thermalShareAbove(const double* v, double level)
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

bool TetThermalProblem::optimizeDensity(const DensityOpt& s, std::string& error, const std::atomic<bool>* cancel)
{
    if (!m_prepared && !prepare(error)) return false;
    m_history.clear();
    m_baseHistory.clear();
    m_outputHistory.clear();
    m_densityHistory.clear();
    m_mirrorsUsed.clear();
    m_topDensity.clear();
    m_densityResult.reset();
    m_designVolume = 0;
    m_stepsBack = 0;
    const auto t0 = std::chrono::steady_clock::now();
    const TetMesh& mesh = *m_mesh;
    const size_t nv = mesh.pos.size(), na = mesh.tets.size();
    const double h = m_h;
    Assembly A;
    buildPattern(mesh, A);
    const bool debug = std::getenv("FIELDES_FEA_DEBUG") != nullptr;
    std::vector<double> vol(na);
    for (size_t a = 0; a < na; ++a) vol[a] = A.geom[a].vol;
    double total = 0;                           // (the volume of the part: told by the design)
    const double p = s.penalty, kmin = s.minConductivity;
    const int nb = int(s.measures.size());
    const int nOut = s.program.outputs();
    if (nb < 1 || nOut < 1)
    {
        error = "the optimisation was not told what to make best";
        return false;
    }
    // The heat that goes in (at the free nodes: what a held temperature takes up does not count), for the temperature the heat comes in at
    double heatQ = 0;
    for (size_t i = 0; i < nv; ++i) if (!m_fixed[i]) heatQ += m_source[i];
    bool needPoint = false;
    for (const auto& ms : s.measures)
    {
        if (ms.kind == MEASURE_INPUT_TEMPERATURE && !(heatQ > 0))
        {
            error = "the temperature the heat comes in at is read where heat goes in: the problem has no heat_input or heat_generation";
            return false;
        }
        needPoint = needPoint || ms.kind == MEASURE_TEMPERATURE_AT;
    }
    std::unique_ptr<TetLocator> pointLocator;
    if (needPoint) pointLocator = std::make_unique<TetLocator>(mesh);

    auto baseK = [&](size_t a) { return m_elementK.empty() ? m_k : m_elementK[a]; };
    auto sOf = [&](double x) { return kmin + (1.0 - kmin) * std::pow(std::max(x, 0.0), p); };
    auto dsOf = [&](double x) { return p * (1.0 - kmin) * std::pow(std::max(x, 1e-9), p - 1.0); };
    auto entry = [&](size_t i, size_t j) {
        return size_t(std::lower_bound(A.K.col.begin() + A.K.rowPtr[i], A.K.col.begin() + A.K.rowPtr[i + 1], uint32_t(j)) - A.K.col.begin());
    };
    const std::vector<unsigned char>& fixed = m_fixed;

    // The equations of the design: the conduction matrix of the densities, the convection the part's own surface has and the heat that goes in (all of it a
    // condition of the design space: a generation makes its heat where its region is, material or not)
    std::vector<double> val(A.K.col.size(), 0.0), kElem(na, 0.0), xPhys(na, 1.0), b(nv, 0.0), T(nv, 0.0);
    for (size_t i = 0; i < nv; ++i) b[i] = m_source[i];
    for (const auto& c : m_conv)
    {
        const int v[3] = {c.a, c.b, c.c};
        for (int q = 0; q < 3; ++q) b[size_t(v[q])] += c.h * c.ambient * c.area / 3.0;
    }
    auto assembleK = [&]() {
        std::fill(val.begin(), val.end(), 0.0);
        parallelRange(nv, [&](size_t b0, size_t b1) {
            for (size_t i = b0; i < b1; ++i)
                for (uint32_t e = A.vtStart[i]; e < A.vtStart[i + 1]; ++e)
                {
                    const size_t t = A.vtList[e];
                    const auto& tv = mesh.tets[t];
                    int li = 0;
                    for (int q = 0; q < 4; ++q) if (size_t(tv[size_t(q)]) == i) li = q;
                    for (int lj = 0; lj < 4; ++lj)
                    {
                        const double d = A.geom[t].g[li][0] * A.geom[t].g[lj][0] + A.geom[t].g[li][1] * A.geom[t].g[lj][1] +
                                         A.geom[t].g[li][2] * A.geom[t].g[lj][2];
                        val[entry(i, size_t(tv[size_t(lj)]))] += kElem[t] * A.geom[t].vol * d;
                    }
                }
        }, 16);
        for (const auto& c : m_conv)
        {
            const int v[3] = {c.a, c.b, c.c};
            for (int q = 0; q < 3; ++q)
                for (int r = 0; r < 3; ++r) val[entry(size_t(v[q]), size_t(v[r]))] += c.h * c.area * (q == r ? 2.0 : 1.0) / 12.0;
        }
    };

    // The solver: Jacobi-preconditioned conjugate gradients on the free nodes (the held rows are the identity), as the analysis has it
    std::vector<double> xm(nv), Ap(nv), rr(nv), zz(nv), pp(nv), invDiag(nv), KT(nv);
    auto matvecFull = [&](const std::vector<double>& x, std::vector<double>& y) {
        parallelRange(nv, [&](size_t b0, size_t b1) {
            for (size_t i = b0; i < b1; ++i)
            {
                double sum = 0;
                for (uint32_t e = A.K.rowPtr[i]; e < A.K.rowPtr[i + 1]; ++e) sum += val[e] * x[A.K.col[e]];
                y[i] = sum;
            }
        }, 256);
    };
    auto apply = [&](const std::vector<double>& x, std::vector<double>& y) {
        for (size_t i = 0; i < nv; ++i) xm[i] = fixed[i] ? 0.0 : x[i];
        matvecFull(xm, y);
        for (size_t i = 0; i < nv; ++i) if (fixed[i]) y[i] = x[i];
    };
    auto dotp = [&](const std::vector<double>& u, const std::vector<double>& w) {
        return parallelTotal(nv, [&](size_t b0, size_t b1) {
            double sum = 0;
            for (size_t i = b0; i < b1; ++i) sum += u[i] * w[i];
            return sum;
        });
    };
    double solveTol = std::max(s.tolerance, 1e-9);
    // x: the free nodes' unknown (the held entries are zero); rhs: the free rows (the held entries zero)
    auto cg = [&](const std::vector<double>& rhs, std::vector<double>& x) -> bool {
        for (size_t i = 0; i < nv; ++i)
        {
            if (fixed[i]) { invDiag[i] = 1.0; x[i] = 0.0; continue; }
            const double d = val[entry(i, i)];
            invDiag[i] = d > 0 ? 1.0 / d : 1.0;
        }
        const double bnorm = std::sqrt(dotp(rhs, rhs));
        if (!(bnorm > 0))
        {
            std::fill(x.begin(), x.end(), 0.0);
            return true;
        }
        apply(x, Ap);
        for (size_t i = 0; i < nv; ++i) rr[i] = fixed[i] ? 0.0 : rhs[i] - Ap[i];
        double rel = std::sqrt(dotp(rr, rr)) / bnorm;
        if (rel < solveTol) return true;
        for (size_t i = 0; i < nv; ++i) zz[i] = rr[i] * invDiag[i];
        pp = zz;
        double rz = dotp(rr, zz);
        int it = 0;
        for (it = 0; it < s.solverIterations; ++it)
        {
            if (cancel && cancel->load())
            {
                error = "cancelled";
                return false;
            }
            apply(pp, Ap);
            const double pAp = dotp(pp, Ap);
            if (!(pAp > 0))
            {
                error = "the conduction matrix is singular: nothing holds the temperature";
                return false;
            }
            const double alpha = rz / pAp;
            for (size_t i = 0; i < nv; ++i)
            {
                x[i] += alpha * pp[i];
                rr[i] -= alpha * Ap[i];
            }
            rel = std::sqrt(dotp(rr, rr)) / bnorm;
            if (!std::isfinite(rel))
            {
                error = "the solver diverged";
                return false;
            }
            if (rel < solveTol) break;
            for (size_t i = 0; i < nv; ++i) zz[i] = rr[i] * invDiag[i];
            const double rzNew = dotp(rr, zz);
            const double beta = rzNew / rz;
            rz = rzNew;
            for (size_t i = 0; i < nv; ++i) pp[i] = zz[i] + beta * pp[i];
        }
        if (rel > std::max(solveTol * 100, 1e-3))
        {
            error = "the conduction solver did not converge (" + std::to_string(it) + " iterations, residual " + std::to_string(rel) + ")";
            return false;
        }
        return true;
    };
    // The start of the temperature: the mean of what holds it
    {
        double mean = 0;
        size_t n = 0;
        for (size_t i = 0; i < nv; ++i) if (fixed[i]) { mean += m_fixedValue[i]; ++n; }
        for (const auto& c : m_conv) { mean += c.ambient; ++n; }
        const double t0v = n ? mean / double(n) : 0.0;
        for (size_t i = 0; i < nv; ++i) T[i] = fixed[i] ? m_fixedValue[i] : t0v;
    }
    std::vector<double> xFree(nv, 0.0), rhsT(nv, 0.0);
    auto solveTemperature = [&]() -> bool {
        for (size_t i = 0; i < nv; ++i) xm[i] = fixed[i] ? m_fixedValue[i] : 0.0;
        matvecFull(xm, KT);
        for (size_t i = 0; i < nv; ++i)
        {
            rhsT[i] = fixed[i] ? 0.0 : b[i] - KT[i];
            xFree[i] = fixed[i] ? 0.0 : T[i];
        }
        if (!cg(rhsT, xFree)) return false;
        for (size_t i = 0; i < nv; ++i) T[i] = fixed[i] ? m_fixedValue[i] : xFree[i];
        return true;
    };

    auto gradientOf = [&](const std::vector<double>& f, size_t a, double g[3]) {
        g[0] = g[1] = g[2] = 0.0;
        for (int q = 0; q < 4; ++q)
        {
            const double fv = f[size_t(mesh.tets[a][size_t(q)])];
            for (int d = 0; d < 3; ++d) g[d] += fv * A.geom[a].g[q][d];
        }
    };
    // What the stiffness part of a measure's change is: -lambda^T (dK / drho) T, the element's  k V grad(lambda) . grad(T)  times  d s / d rho
    auto addImplicit = [&](std::vector<double>& out, const std::vector<double>& lam) {
        parallelRange(na, [&](size_t b0, size_t b1) {
            for (size_t a = b0; a < b1; ++a)
            {
                double gl[3], gt[3];
                gradientOf(lam, a, gl);
                gradientOf(T, a, gt);
                out[a] -= baseK(a) * dsOf(xPhys[a]) * vol[a] * (gl[0] * gt[0] + gl[1] * gt[1] + gl[2] * gt[2]);
            }
        }, 512);
    };

    const double norm = 10.0;                                   // the exponent of the p-norm that stands for a peak
    std::vector<double> peakFactor(size_t(nb), 1.0);            // what keeps a p-norm at the peak it stands for
    std::vector<double> reference(size_t(nb), std::numeric_limits<double>::quiet_NaN());   // the temperature a peak is measured from (the first evaluation's coldest / hottest)
    std::vector<std::vector<double>> adjointT(static_cast<size_t>(nb));      // (not `adjointT(size_t(nb))`: a declaration of a function)
    std::vector<double> rhsA(nv, 0.0);
    int it = 0;

    // The measures of a density and their sensitivities to the density of every element.  update: the factors that keep a p-norm at the peak it
    // stands for are moved on (not when the design is only looked at a little off its place)
    auto measuresOf = [&](const std::vector<double>& xPhysIn, bool update, int iteration, std::vector<double>& bv,
                          std::vector<std::vector<double>>& dB) -> bool {
        xPhys = xPhysIn;
        it = iteration;
        for (size_t a = 0; a < na; ++a) kElem[a] = baseK(a) * sOf(xPhys[a]);
        assembleK();
        if (!solveTemperature()) return false;
        for (auto& g : dB) std::fill(g.begin(), g.end(), 0.0);
        // The density at the nodes: what each element and its neighbours have, by volume
        std::vector<double> rhoNode(nv, 0.0), vsum(nv, 0.0);
        for (size_t i = 0; i < nv; ++i)
        {
            for (uint32_t k = A.vtStart[i]; k < A.vtStart[i + 1]; ++k)
            {
                const size_t t = A.vtList[k];
                rhoNode[i] += vol[t] * xPhys[t];
                vsum[i] += vol[t];
            }
            rhoNode[i] = vsum[i] > 0 ? rhoNode[i] / vsum[i] : 0.0;
        }
        auto adjoint = [&](size_t bi, const std::vector<double>& rhs) -> bool {
            std::vector<double>& lam = adjointT[bi];
            lam.resize(nv, 0.0);
            for (size_t i = 0; i < nv; ++i) rhsA[i] = fixed[i] ? 0.0 : rhs[i];
            return cg(rhsA, lam);
        };
        for (int bi = 0; bi < nb; ++bi)
        {
            const DensityMeasureSpec& ms = s.measures[size_t(bi)];
            std::vector<double>& out = dB[size_t(bi)];
            switch (ms.kind)
            {
            case MEASURE_VOLUME:
            {
                // the share of the part's volume that the design uses
                double v = 0;
                for (size_t a = 0; a < na; ++a)
                {
                    v += vol[a] * xPhys[a];
                    out[a] = total > 0 ? vol[a] / total : 0.0;
                }
                bv[size_t(bi)] = total > 0 ? v / total : 0.0;
                break;
            }
            case MEASURE_MAX_TEMPERATURE:
            case MEASURE_MIN_TEMPERATURE:
            {
                // the p-norm of the nodes' temperature above the coldest (below the hottest) one the first evaluation had, each node counting as much as
                // the material round it (its density to the 4th power): an empty part has temperatures that are of no part's
                const double sgn = ms.kind == MEASURE_MAX_TEMPERATURE ? 1.0 : -1.0;
                if (std::isnan(reference[size_t(bi)]))
                {
                    double lo = std::numeric_limits<double>::max(), hi = -std::numeric_limits<double>::max();
                    for (size_t i = 0; i < nv; ++i) { lo = std::min(lo, T[i]); hi = std::max(hi, T[i]); }
                    reference[size_t(bi)] = sgn > 0 ? lo : hi;
                }
                const double ref = reference[size_t(bi)];
                std::vector<double> u(nv), weight(nv);
                double peak = 0;
                for (size_t i = 0; i < nv; ++i)
                {
                    u[i] = std::max(0.0, sgn * (T[i] - ref));
                    weight[i] = std::pow(rhoNode[i], 4.0);
                    if (weight[i] > 1e-8) peak = std::max(peak, u[i]);
                }
                double sum = 0;
                if (peak > 0)
                    for (size_t i = 0; i < nv; ++i) sum += weight[i] * std::pow(u[i] / peak, norm);
                const double pn = peak * std::pow(sum, 1.0 / norm);            // (>= the peak)
                if (update && it > 0 && pn > 0 && std::isfinite(peak / pn))
                    peakFactor[size_t(bi)] = 0.5 * peakFactor[size_t(bi)] + 0.5 * std::max(0.1, std::min(2.0, peak / pn));
                const double factor = peakFactor[size_t(bi)];
                bv[size_t(bi)] = ref + sgn * factor * pn;
                std::vector<double> rhs(nv, 0.0);
                if (pn > 0)
                    for (size_t i = 0; i < nv; ++i)
                        if (u[i] > 0 && weight[i] > 0) rhs[i] = factor * weight[i] * std::pow(u[i] / pn, norm - 1.0);
                if (!adjoint(size_t(bi), rhs)) return false;
                addImplicit(out, adjointT[size_t(bi)]);
                // through the nodes' weights: d weight_i / d x_a = 4 rho_i^3 vol_a / vsum_i
                if (pn > 0)
                    for (size_t a = 0; a < na; ++a)
                    {
                        double acc = 0;
                        for (int q = 0; q < 4; ++q)
                        {
                            const size_t i = size_t(mesh.tets[a][size_t(q)]);
                            if (!(vsum[i] > 0)) continue;
                            acc += factor * (1.0 / norm) * std::pow(pn, 1.0 - norm) * std::pow(u[i], norm) * 4.0 * std::pow(rhoNode[i], 3.0) * vol[a] / vsum[i];
                        }
                        out[a] += sgn * acc;
                    }
                break;
            }
            case MEASURE_MAX_HEAT_FLUX:
            {
                // the p-norm of the elements' heat flux  k s(rho) |grad T|, brought to the peak it stands for
                std::vector<double> q(na, 0.0);
                parallelRange(na, [&](size_t b0, size_t b1) {
                    for (size_t a = b0; a < b1; ++a)
                    {
                        double g[3];
                        gradientOf(T, a, g);
                        q[a] = kElem[a] * std::sqrt(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]);
                    }
                }, 512);
                double peak = 0;
                for (size_t a = 0; a < na; ++a) peak = std::max(peak, q[a]);
                double sum = 0;
                if (peak > 0)
                    for (size_t a = 0; a < na; ++a) sum += std::pow(q[a] / peak, norm);
                const double pn = peak * std::pow(sum, 1.0 / norm);
                if (update && it > 0 && pn > 0 && std::isfinite(peak / pn))
                    peakFactor[size_t(bi)] = 0.5 * peakFactor[size_t(bi)] + 0.5 * std::max(0.1, std::min(2.0, peak / pn));
                const double factor = peakFactor[size_t(bi)];
                bv[size_t(bi)] = factor * pn;
                // d (factor * pn) / d q_a = factor * (q_a / pn)^(norm-1); q_a depends on the temperature at the element's nodes (k g/|g| . grad N) and, explicitly, on its density
                std::vector<double> rhs(nv, 0.0);
                if (pn > 0)
                    for (size_t a = 0; a < na; ++a)
                    {
                        if (!(q[a] > 0)) continue;
                        double g[3];
                        gradientOf(T, a, g);
                        const double gn = std::sqrt(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]);
                        if (!(gn > 0)) continue;
                        const double c = factor * std::pow(q[a] / pn, norm - 1.0);
                        for (int qn = 0; qn < 4; ++qn)
                        {
                            const size_t i = size_t(mesh.tets[a][size_t(qn)]);
                            const double dir = (g[0] * A.geom[a].g[qn][0] + g[1] * A.geom[a].g[qn][1] + g[2] * A.geom[a].g[qn][2]) / gn;
                            rhs[i] += c * kElem[a] * dir;
                        }
                        out[a] += c * baseK(a) * dsOf(xPhys[a]) * gn;
                    }
                if (!adjoint(size_t(bi), rhs)) return false;
                addImplicit(out, adjointT[size_t(bi)]);
                break;
            }
            case MEASURE_INPUT_TEMPERATURE:
            {
                // the mean temperature at the places the heat comes in, weighted by how much comes in at each
                double value = 0;
                std::vector<double> rhs(nv, 0.0);
                for (size_t i = 0; i < nv; ++i)
                {
                    if (fixed[i] || !(m_source[i] != 0.0)) continue;
                    value += m_source[i] * T[i] / heatQ;
                    rhs[i] = m_source[i] / heatQ;
                }
                bv[size_t(bi)] = value;
                if (!adjoint(size_t(bi), rhs)) return false;
                addImplicit(out, adjointT[size_t(bi)]);
                break;
            }
            case MEASURE_TEMPERATURE_AT:
            {
                // the temperature at a point: the barycentric mix of the nodes of the tetrahedron that holds it
                double lam4[4];
                const int tet = pointLocator->locate(Vec3(ms.a, ms.b, ms.c), lam4);
                if (tet < 0)
                {
                    error = "a point where the temperature is read is outside the part";
                    return false;
                }
                double value = 0;
                std::vector<double> rhs(nv, 0.0);
                for (int q = 0; q < 4; ++q)
                {
                    const size_t i = size_t(mesh.tets[size_t(tet)][size_t(q)]);
                    value += lam4[q] * T[i];
                    rhs[i] += lam4[q];
                }
                bv[size_t(bi)] = value;
                if (!adjoint(size_t(bi), rhs)) return false;
                addImplicit(out, adjointT[size_t(bi)]);
                break;
            }
            default:
                error = "a measure that the optimisation does not know";
                return false;
            }
        }
        return true;
    };

    // The thermal physics of the design: what holds the temperature or brings the heat stays solid, whether the problem is symmetric about a plane, and
    // the picture of an iteration
    auto fixedElements = [&](std::vector<char>& state, std::vector<char>& supported) {
        // Elements at a held temperature or a heat input (over a surface, at a point) stay solid; a generation region is a condition of the design space (its heat is made there, material or not).
        // `supported`: the ones that hold a temperature or a convecting face -- what the material has to be joined to to be cooled
        supported.assign(na, 0);
        for (size_t a = 0; a < na; ++a)
            for (int q = 0; q < 4; ++q)
            {
                const size_t i = size_t(mesh.tets[a][size_t(q)]);
                if (fixed[i])
                {
                    supported[a] = 1;
                    state[a] = 1;
                }
                else if (m_sourceFixed[i] != 0.0)
                    state[a] = 1;
            }
        for (const auto& c : m_conv)
        {
            // the element that has the face
            for (uint32_t k = A.vtStart[size_t(c.a)]; k < A.vtStart[size_t(c.a) + 1]; ++k)
            {
                const size_t a = A.vtList[k];
                int found = 0;
                for (int q = 0; q < 4; ++q)
                {
                    const int v = mesh.tets[a][size_t(q)];
                    if (v == c.a || v == c.b || v == c.c) ++found;
                }
                if (found == 3)
                {
                    supported[a] = 1;
                    break;
                }
            }
        }
    };
    auto symmetricAbout = [&](int axis, double at, const std::vector<Eigen::Vector3f>& mc, double badRegionShare, std::string& why) -> bool {
        // The part (the same at the mirror image of every element's centre); what acts on one side of the plane is what acts on the other, mirrored
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
        if (!m_elementK.empty()) { why = "the conductivity is a field"; return false; }
        // what the conditions give at the nodes: the heat in, the held temperatures (that they are held, and their values), the convection and its ambient
        std::vector<double> qHeat(nv, 0.0), qHeld(nv, 0.0), qHeldT(nv, 0.0), qConv(nv, 0.0), qAmb(nv, 0.0);
        for (size_t i = 0; i < nv; ++i)
        {
            qHeat[i] = m_source[i];
            qHeld[i] = fixed[i] ? 1.0 : 0.0;
            qHeldT[i] = fixed[i] ? m_fixedValue[i] : 0.0;
        }
        for (const auto& c : m_conv)
        {
            const int v[3] = {c.a, c.b, c.c};
            for (int q = 0; q < 3; ++q)
            {
                qConv[size_t(v[q])] += c.h * c.area / 3.0;
                qAmb[size_t(v[q])] += c.h * c.area / 3.0 * c.ambient;
            }
        }
        auto sides = [&](const std::vector<double>& w, const char* what) -> bool {
            double sum[2] = {0, 0}, weight[2] = {0, 0};
            Vec3 centre[2] = {Vec3::Zero(), Vec3::Zero()};
            for (size_t i = 0; i < nv; ++i)
            {
                if (w[i] == 0.0) continue;
                const double d = mesh.pos[i][axis] - at;
                if (std::abs(d) < 0.25 * h) continue;                 // (on the plane: its own image)
                const int side = d > 0 ? 0 : 1;
                Vec3 q = mesh.pos[i];
                q[axis] = std::abs(d);                                // (the distance from the plane: the same for a point and its image)
                sum[side] += w[i];
                weight[side] += std::abs(w[i]);
                centre[side] += std::abs(w[i]) * q;
            }
            const double scale = std::max(std::abs(sum[0]), std::abs(sum[1]));
            if (debug) fprintf(stderr, "[tetopt]   %s: above %.4g, below %.4g\n", what, sum[0], sum[1]);
            if (std::abs(sum[0] - sum[1]) > 0.06 * scale + 1e-12 * (weight[0] + weight[1]))
            {
                why = std::string("the ") + what + " differ between the two sides";
                return false;
            }
            if (weight[0] > 0 && weight[1] > 0)
            {
                const Vec3 cA = centre[0] / weight[0], cB = centre[1] / weight[1];
                if ((cA - cB).norm() > 0.5 * h)
                {
                    why = std::string("the ") + what + " are in other places on the two sides";
                    return false;
                }
            }
            return true;
        };
        return sides(qHeat, "heat inputs") && sides(qHeld, "held temperatures") && sides(qHeldT, "held temperatures' values") &&
               sides(qConv, "convecting surfaces") && sides(qAmb, "ambient temperatures");
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
        const char* taskName() const override { return "optimising the heat conduction"; }
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
    double savedTol = 0;
    adapter.tightFn = [&](bool on) {
        if (on)
        {
            savedTol = solveTol;
            solveTol = 1e-12;
        }
        else
            solveTol = savedTol;
    };
    std::shared_ptr<TetLocator> liveLocator;
    adapter.keptFn = [&](const topopt::IterationView& view) {
        // The design as it is, for the viewport: the part where the density is above the level that keeps the material the design has
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
    m_stepsBack = res.stepsBack;
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
    R2->volume = res.designVolume;
    R2->elements = int(na);
    R2->seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    m_densityResult = R2;
    m_designVolume = res.designVolume;
    return true;
}

double TetThermalProblem::cutThreshold() const
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
            sum += vol[t] * thermalShareAbove(v, level);
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

std::shared_ptr<const MeshResult> TetThermalProblem::densityResultAt(size_t k) const
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
    R->elements = m_densityResult->elements;
    return R;
}

int TetThermalProblem::pieces(double threshold, double margin) const
{
    // The part is where the density at the nodes -- the field the optimised part is cut from, linear in each tetrahedron -- is above the level; two
    // nodes of one tetrahedron that are above it are in the same piece.  A node only just above the level makes a hair-thin link that no picture
    // shows: it counts when it is above by `margin`
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
        for (int q = 0; q < 4; ++q)
        {
            const int i = t[size_t(q)];
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

}   // namespace fea
}   // namespace libfive
