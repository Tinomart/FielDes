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
    if (!meshShape(m_shape, m_lo, m_hi, m_h, *mesh, error)) return false;
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
    std::vector<double> fixedValue, source;
    std::vector<ConvTri> conv;
    double heatIn = 0;
    auto resolve = [&](const TetMesh& m, std::string& err) {
        const size_t nv = m.pos.size(), nt = m.tets.size();
        fixed.assign(nv, 0);
        fixedValue.assign(nv, 0.0);
        source.assign(nv, 0.0);
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
            for (size_t i = 0; i < nv; ++i)
                if (v[i] <= rch)
                {
                    fixed[i] = 1;
                    fixedValue[i] = t.value;
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
                double sel = 0;
                for (size_t t = 0; t < nt; ++t) if (v[t] <= reach) sel += vol[t];
                if (!(sel > 0))
                {
                    err = "a heat generation region doesn't touch the part";
                    return false;
                }
                for (size_t t = 0; t < nt; ++t)
                {
                    if (!(v[t] <= reach)) continue;
                    const double share = hq.watts * vol[t] / sel / 4.0;
                    for (int p = 0; p < 4; ++p) source[size_t(m.tets[t][size_t(p)])] += share;
                }
                heatIn += hq.watts;
                continue;
            }
            std::vector<char> take;
            if (trianglesIn(m, hq.region, pts, centres, take) > 0)
            {
                double sel = 0;
                for (size_t f = 0; f < take.size(); ++f) if (take[f]) sel += faceArea(m, f);
                for (size_t f = 0; f < take.size(); ++f)
                {
                    if (!take[f]) continue;
                    const double share = hq.watts * faceArea(m, f) / sel / 3.0;
                    for (int p = 0; p < 3; ++p) source[size_t(m.faces[f][size_t(p)])] += share;
                }
            }
            else
            {
                // a region holding no surface: over the nodes inside it
                evalTreePoints(hq.region, pts, v);
                size_t count = 0;
                for (float x : v) count += x <= reach;
                if (count == 0)
                {
                    err = "a heat input region doesn't touch the part";
                    return false;
                }
                for (size_t i = 0; i < nv; ++i) if (v[i] <= reach) source[i] += hq.watts / double(count);
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
            for (size_t f = 0; f < take.size(); ++f)
                if (take[f]) conv.push_back({m.faces[f][0], m.faces[f][1], m.faces[f][2], faceArea(m, f), c.coefficient, c.ambient});
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

    uint64_t hsh = 1469598103934665603ull;
    const double hdr[] = {m_h, m_k, double(mesh->pos.size()), double(mesh->tets.size())};
    hsh = fnv(hsh, hdr, sizeof(hdr));
    hsh = fnv(hsh, mesh->pos.data(), mesh->pos.size() * sizeof(Vec3));
    hsh = fnv(hsh, mesh->tets.data(), mesh->tets.size() * sizeof(std::array<int, 4>));
    hsh = fnv(hsh, fixed.data(), fixed.size());
    hsh = fnv(hsh, fixedValue.data(), fixedValue.size() * sizeof(double));
    hsh = fnv(hsh, source.data(), source.size() * sizeof(double));
    hsh = fnv(hsh, conv.data(), conv.size() * sizeof(ConvTri));
    m_hash = hsh;

    m_mesh = mesh;
    m_fixed.swap(fixed);
    m_fixedValue.swap(fixedValue);
    m_source.swap(source);
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
                    val[entry(i, size_t(tv[size_t(lj)]))] += m_k * A.geom[t].vol * d;
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
            flux[t] = -m_k * g;
        }
    }, 256);
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
    res->heatOutFixed = outFixed;
    res->heatOutConvection = outConv;
    res->looseElements = m_looseElements;
    res->seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    m_result = res;
    return true;
}

}   // namespace fea
}   // namespace libfive
