/*
libfive: a CAD kernel for modeling with implicit functions

The design of a density-based topology optimization, whatever physics it designs for; see density_design.hpp.  It was the middle of the
structural optimiser (tetfea.cpp) and is moved here whole, so that the flow (tetflow.cpp) has the same filter, projection, symmetry, columns,
step control and update, and the physics supplies only its measures.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <numeric>
#include <unordered_map>

#include "libfive/fea/density_design.hpp"
#include "libfive/fea/fea.hpp"
#include "libfive/fea/tetfea.hpp"
#include "libfive/run_progress.hpp"
#include "tet_common.hpp"

namespace libfive {
namespace fea {
namespace topopt {

using namespace tet;

bool runDensityDesign(const TetMesh& mesh, const std::vector<double>& vol, double h, const std::vector<uint32_t>& vtStart,
                      const std::vector<uint32_t>& vtList, const DesignSettings& s, Physics& phys, DesignResult& out, std::string& error,
                      const std::atomic<bool>* cancel)
{
    const bool grown = s.origin.is_valid();
    if (!(s.startDensity > 0 && s.startDensity <= 1))
    {
        error = "the design has to start with some material";
        return false;
    }
    out = DesignResult();
    const size_t nv = mesh.pos.size(), na = mesh.tets.size();
    const bool debug = std::getenv("FIELDES_FEA_DEBUG") != nullptr;

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
    // The part the space was grown from: what lies outside it starts nearly empty, and the volume fraction is of what lies inside
    std::vector<char> outside(na, 0);
    std::vector<float> originVals;              // (how far outside the part each element lies, when `origin` is a distance)
    if (grown)
    {
        evalTreePoints(s.origin, centres, vals);
        originVals = vals;
        for (size_t a = 0; a < na; ++a) outside[a] = !(vals[a] < 0);
        if (debug)
        {
            size_t nOut = 0, nSolid = 0, nEmpty = 0;
            for (size_t a = 0; a < na; ++a)
            {
                nOut += outside[a] ? 1 : 0;
                nSolid += state[a] == 1 ? 1 : 0;
                nEmpty += state[a] == 2 ? 1 : 0;
            }
            fprintf(stderr, "[tetopt] grown design space: %zu of %zu elements lie outside the part (%zu held solid, %zu held empty)\n", nOut, na, nSolid, nEmpty);
        }
    }
    // What the physics holds solid (where the supports and loads are), and the elements that hold a support
    std::vector<char> supported;
    phys.fixedElements(state, supported);

    // Mirror symmetry.  For each plane the design is kept symmetric about: every element's partner, the element that holds the mirror
    // image of its centre.  The sensitivities are averaged over the pairs, which keeps a design that starts symmetric symmetric
    struct MirrorMap { int axis; double at; std::vector<int32_t> partner; };
    std::vector<MirrorMap> mirrorMaps;
    if (s.symmetryAuto || !s.mirrors.empty())
    {
        Vec3 vlo = mesh.pos[0], vhi = mesh.pos[0];
        for (const auto& p : mesh.pos)
        {
            vlo = vlo.cwiseMin(p);
            vhi = vhi.cwiseMax(p);
        }
        const TetLocator locator(mesh);
        auto mirrorOf = [](const Vec3& c, int axis, double at) {
            Vec3 q = c;
            q[axis] = 2 * at - c[axis];
            return q;
        };
        auto makeMap = [&](int axis, double at) {
            MirrorMap mm{axis, at, std::vector<int32_t>(na, -1)};
            std::vector<int32_t> raw(na, -1);
            parallelRange(na, [&](size_t b0, size_t b1) {
                for (size_t a = b0; a < b1; ++a)
                {
                    double lam[4];
                    raw[a] = locator.locateInside(mirrorOf(cen[a], axis, at), lam);
                }
            }, 512);
            // The element that holds the mirror image of a centre is not an exact partner on a mesh that is not exactly symmetric: two elements may name the same one, a
            // pair may not name each other, and a design averaged over such a map stays lopsided.  So the pairs are made exact: elements that name each other, then
            // the nearest of the rest (by the distance of one's mirror image to the other's centre), each element in one pair at most; an element with no partner is left alone
            std::vector<int32_t>& p = mm.partner;
            for (size_t a = 0; a < na; ++a)
                if (raw[a] >= 0 && raw[size_t(raw[a])] == int32_t(a)) p[a] = raw[a];
            struct Cand { double cost; int32_t a, b; };
            std::vector<Cand> cands;
            for (size_t a = 0; a < na; ++a)
            {
                if (p[a] >= 0 || raw[a] < 0) continue;
                const int32_t b = raw[a];
                if (size_t(b) == a || p[size_t(b)] >= 0) continue;
                cands.push_back({(mirrorOf(cen[a], axis, at) - cen[size_t(b)]).norm(), int32_t(a), b});
            }
            std::sort(cands.begin(), cands.end(), [](const Cand& x, const Cand& y) {
                if (x.cost != y.cost) return x.cost < y.cost;
                return x.a != y.a ? x.a < y.a : x.b < y.b;
            });
            for (const auto& c : cands)
                if (p[size_t(c.a)] < 0 && p[size_t(c.b)] < 0)
                {
                    p[size_t(c.a)] = c.b;
                    p[size_t(c.b)] = c.a;
                }
            return mm;
        };
        // Is the whole problem symmetric about this plane?  The keep / avoid regions: the same at the mirror image of every element's centre; the
        // rest (the part, the loads, the supports ...) is the physics' to say
        auto problemSymmetric = [&](int axis, double at, std::string& why) -> bool {
            std::vector<Eigen::Vector3f> mc(na);
            for (size_t a = 0; a < na; ++a) mc[a] = mirrorOf(cen[a], axis, at).cast<float>();
            double total = 0, badRegion = 0;
            std::vector<float> v1, v2;
            for (size_t a = 0; a < na; ++a) total += vol[a];
            for (const auto* group : {&s.keep, &s.avoid})
                for (const auto& t : *group)
                {
                    evalTreePoints(t, centres, v1);
                    evalTreePoints(t, mc, v2);
                    for (size_t a = 0; a < na; ++a)
                        if ((v1[a] < 0) != (v2[a] < 0)) badRegion += vol[a];
                }
            return phys.symmetricAbout(axis, at, mc, badRegion / std::max(total, 1e-30), why);
        };
        for (const auto& m : s.mirrors)
        {
            if (m.axis < 0 || m.axis > 2) continue;
            const double at = m.atCentre ? 0.5 * (vlo[m.axis] + vhi[m.axis]) : m.at;
            mirrorMaps.push_back(makeMap(m.axis, at));
            out.mirrorsUsed.push_back({m.axis, at, false});
        }
        if (s.symmetryAuto)
            for (int axis = 0; axis < 3; ++axis)
            {
                bool asked = false;
                for (const auto& mm : mirrorMaps) asked = asked || mm.axis == axis;
                if (asked) continue;
                const double at = 0.5 * (vlo[axis] + vhi[axis]);
                std::string why;
                if (!problemSymmetric(axis, at, why))
                {
                    if (debug) fprintf(stderr, "[tetopt] not symmetric about axis %d: %s\n", axis, why.c_str());
                    continue;
                }
                mirrorMaps.push_back(makeMap(axis, at));
                out.mirrorsUsed.push_back({axis, at, true});
            }
    }
    // The orbits of the elements under the planes: the elements joined by a pair of any plane.  A symmetric design is one value for each orbit, and the average over an orbit is a
    // projection (applied twice it changes nothing, and it is its own transpose, so the sensitivity of a symmetric design is the average of the sensitivities) whatever the pairs of the
    // planes are.  On a mesh that is exactly symmetric an orbit is the images of an element, at most two for each plane; on one that is not, the pairs of two planes need not join up
    // and an orbit can grow past the images there can be -- it is left alone, not averaged.  (Averaging the planes one after the other, as was done, is a projection only when the pairs
    // of the planes agree: the sensitivity of a design symmetric about two planes of a mesh that is not symmetric was off by a sixth.)
    std::vector<int32_t> orbitStart(1, 0), orbitMember;
    if (!mirrorMaps.empty())
    {
        std::vector<int32_t> parent(na);
        for (size_t a = 0; a < na; ++a) parent[a] = int32_t(a);
        auto root = [&](int32_t a) {
            while (parent[size_t(a)] != a)
            {
                parent[size_t(a)] = parent[size_t(parent[size_t(a)])];
                a = parent[size_t(a)];
            }
            return a;
        };
        for (const auto& mm : mirrorMaps)
            for (size_t a = 0; a < na; ++a)
            {
                const int32_t q = mm.partner[a];
                if (q < 0) continue;
                const int32_t ra = root(int32_t(a)), rq = root(q);
                if (ra != rq) parent[size_t(std::max(ra, rq))] = std::min(ra, rq);     // (the lowest element is the root: the same orbits on every run)
            }
        std::vector<int32_t> size(na, 0), orbitOf(na, -1);
        for (size_t a = 0; a < na; ++a) ++size[size_t(root(int32_t(a)))];
        const int32_t most = int32_t(1) << int(mirrorMaps.size());
        int32_t orbits = 0;
        for (size_t a = 0; a < na; ++a)
            if (root(int32_t(a)) == int32_t(a) && size[a] >= 2 && size[a] <= most)
            {
                orbitOf[a] = orbits++;
                orbitStart.push_back(orbitStart.back() + size[a]);
            }
        orbitMember.assign(size_t(orbitStart.back()), -1);
        std::vector<int32_t> fill(orbitStart.begin(), orbitStart.end() - 1);
        for (size_t a = 0; a < na; ++a)
        {
            const int32_t o = orbitOf[size_t(root(int32_t(a)))];
            if (o >= 0) orbitMember[size_t(fill[size_t(o)]++)] = int32_t(a);
        }
    }
    auto symmetrise = [&](std::vector<double>& v) {
        for (size_t o = 0; o + 1 < orbitStart.size(); ++o)
        {
            double sum = 0;
            for (int32_t k = orbitStart[o]; k < orbitStart[o + 1]; ++k) sum += v[size_t(orbitMember[size_t(k)])];
            const double mean = sum / double(orbitStart[o + 1] - orbitStart[o]);
            for (int32_t k = orbitStart[o]; k < orbitStart[o + 1]; ++k) v[size_t(orbitMember[size_t(k)])] = mean;
        }
    };

    // Extrusion: the design elements at each (x, y) (or the like) share one value -- their sensitivities
    // summed -- so the design is a profile extruded through.  The lines are bins of half an element; across a plane of symmetry they are counted from the
    // plane outward on both sides (a bin and its mirror image are then each other's images -- bins counted from the lowest centre are not symmetric about the
    // plane, and the elements that mirror each other fell into lines of another make-up, so that the design came out lopsided)
    std::vector<int> column(na, -1);
    int nCol = 0;
    if (s.extrude >= 0 && s.extrude <= 2)
    {
        const int u0 = s.extrude == 0 ? 1 : 0, u1 = s.extrude == 2 ? 1 : 2;
        Vec3 lo = cen[0];
        for (const auto& c : cen) lo = lo.cwiseMin(c);
        double anchor[3] = {lo[0], lo[1], lo[2]};
        bool mirroredAxis[3] = {false, false, false};
        for (const auto& mm : mirrorMaps)
            if (mm.axis != s.extrude)
            {
                mirroredAxis[mm.axis] = true;
                anchor[mm.axis] = mm.at;
            }
        const double bin = 0.5 * h;
        auto keyOf = [&](const Vec3& c, int u) -> int64_t {
            const double d = c[u] - anchor[u];
            if (!mirroredAxis[u]) return int64_t(std::floor(d / bin + 1e-6));
            const int64_t m = int64_t(std::floor(std::abs(d) / bin + 1e-6));
            return d >= -1e-6 * bin ? m : -m - 1;
        };
        std::unordered_map<int64_t, int> id;
        for (size_t a = 0; a < na; ++a)
        {
            if (state[a]) continue;
            const int64_t key = keyOf(cen[a], u0) * 1000003 + keyOf(cen[a], u1);
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
                            // The filter smooths the DESIGN: an element that is held (solid or empty) is not a variable and does not lend its density to the
                            // design elements round it.  Counted in, a support's solid elements, a kept region, a void put density (or took it) from the
                            // elements within the filter radius whatever the stress there said -- material beside a held region that no stress reached
                            if (state[a] == 0 && state[q] != 0) continue;
                            fIdx.push_back(q);
                            fW.push_back(float(w * vol[q]));
                            Hs[a] += w * vol[q];
                        }
                    }
            fStart[a + 1] = fIdx.size();
        }
    }
    auto filter = [&](const std::vector<double>& in, std::vector<double>& res) {
        res.resize(na);
        parallelRange(na, [&](size_t b0, size_t b1) {
            for (size_t a = b0; a < b1; ++a)
            {
                double sum = 0;
                for (size_t q = fStart[a]; q < fStart[a + 1]; ++q) sum += double(fW[q]) * in[fIdx[q]];
                res[a] = sum / Hs[a];
            }
        }, 1024);
    };
    // the transpose, for the sensitivities: res_b = sum_a w_ab in_a / Hs_a
    auto filterT = [&](const std::vector<double>& in, std::vector<double>& res) {
        res.assign(na, 0.0);
        for (size_t a = 0; a < na; ++a)
        {
            const double f = in[a] / Hs[a];
            for (size_t q = fStart[a]; q < fStart[a + 1]; ++q) res[fIdx[q]] += double(fW[q]) * f;
        }
    };

    double total = 0;
    for (size_t a = 0; a < na; ++a)
        if (!outside[a]) total += vol[a];
    phys.partVolume(total);
    // The density is projected towards 0 and 1 (a smoothed step at 1/2, tanh(beta (x - 1/2)), steeper as the optimisation goes on):
    // without it a third of the design stays grey -- material that is neither there nor not there, whose place in the part is
    // decided by where the volume fraction cuts it -- and the layout keeps shifting to the very end.  beta starts at 1 (the filter's
    // density as it is), doubles every projEvery iterations from projStart, and stops at the sharpness asked for
    const double betaMax = s.sharpness > 1.0 ? s.sharpness : 0.0;
    // The sharpening reaches its full steepness at three quarters of the iterations, however many there are (it used to start at the 10th and
    // step every 5th at least, so a short run never got there: its design stayed grey): beta doubles from 1 up to the sharpness in steps of
    // projEvery iterations, the last of them at 3/4 of the run, and the rest of the run refines the crisp design
    const int doublings = betaMax > 1.0 ? std::max(1, int(std::ceil(std::log2(betaMax)))) : 1;
    const int projEvery = std::max(2, int(std::ceil(0.75 * s.iterations / (2.0 * doublings))));
    const int projStart = std::max(3, int(std::lround(0.75 * s.iterations - double(doublings) * projEvery)));
    double beta = 1.0;
    double betaProgress = 0;                    // (smooth sharpening: the doublings so far, in fractions of one)
    const bool smoothBeta = s.smoothSharpening;
    auto sameBeta = [&](double a, double b) { return smoothBeta ? std::abs(a - b) <= 0.25 * std::max(a, b) : a == b; };
    const double eta = 0.5;
    auto projectOf = [&](double v) {
        if (betaMax <= 0) return v;
        const double den = std::tanh(beta * eta) + std::tanh(beta * (1 - eta));
        return (std::tanh(beta * eta) + std::tanh(beta * (v - eta))) / den;
    };
    auto projectSlope = [&](double v) {
        if (betaMax <= 0) return 1.0;
        const double den = std::tanh(beta * eta) + std::tanh(beta * (1 - eta));
        const double th = std::tanh(beta * (v - eta));
        return beta * (1 - th * th) / den;
    };
    std::vector<double> xTil;

    std::vector<double> x(na, grown ? std::min(1.0, s.startDensity) : s.startDensity), xPhys, dcF, xNew(na);
    // (a design found in a smaller design space: its variable, where the new space has it; not under a little, which nothing could grow from.
    // Beyond that space the field is only the value at its edge carried outwards, which is not a design there: such an element starts empty)
    std::vector<float> startVals;
    if (s.startField.is_valid())
    {
        evalTreePoints(s.startField, centres, startVals);
        if (s.startDomain.is_valid())
        {
            evalTreePoints(s.startDomain, centres, vals);
            for (size_t a = 0; a < na; ++a)
                if (!(vals[a] < 0)) startVals[a] = 0.0f;
        }
    }
    for (size_t a = 0; a < na; ++a)
    {
        if (outside[a]) x[a] = 0.02;            // (a little: material put in nothing grows only where the neighbours' strain says so)
        if (!startVals.empty()) x[a] = std::min(1.0, std::max(0.02, double(startVals[a])));
        if (state[a]) x[a] = state[a] == 1 ? 1.0 : 0.0;
    }
    // A design that starts symmetric stays symmetric: the sensitivities are averaged with their mirror images, so equal elements move equally -- but
    // the start has to be equal.  A start that is sampled from a field (the body a flow design begins as, the design of a smaller space) is not on a mesh
    // that is itself symmetric, and the difference between a pair never went away: the body came out lopsided
    if (!mirrorMaps.empty() && !startVals.empty())
    {
        symmetrise(x);
        for (size_t a = 0; a < na; ++a)
            if (state[a]) x[a] = state[a] == 1 ? 1.0 : 0.0;
    }
    auto physical =[&](const std::vector<double>& xs, std::vector<double>& res) {
        filter(xs, xTil);
        res.resize(na);
        for (size_t a = 0; a < na; ++a)
        {
            if (state[a] == 1) res[a] = 1.0;
            else if (state[a] == 2) res[a] = 0.0;
            else res[a] = projectOf(xTil[a]);
        }
    };
    // The density at the nodes (each tetrahedron's averaged at its nodes by volume): the field of an iteration
    auto nodal = [&](const std::vector<double>& xs, std::vector<float>& res) {
        res.assign(nv, 0.0f);
        parallelRange(nv, [&](size_t b0, size_t b1) {
            for (size_t i = b0; i < b1; ++i)
            {
                double sum = 0, w = 0;
                for (uint32_t e = vtStart[i]; e < vtStart[i + 1]; ++e)
                {
                    const size_t t = vtList[e];
                    sum += vol[t] * xs[t];
                    w += vol[t];
                }
                res[i] = w > 0 ? float(sum / w) : 0.0f;
            }
        }, 256);
    };

    // What the design is made for: the physics' MEASURES (each a value and its sensitivity to the density of every element) and the formulas of them
    // (s.program) that are the objective and the constraints
    const int nb = phys.measureCount();
    const int nOut = s.program.outputs();
    if (nb < 1 || nOut < 1)
    {
        error = "the optimisation was not told what to make best";
        return false;
    }
    run_progress::Task task(phys.taskName());
    // (the picture of the design that the viewport draws as it goes comes down when the optimisation ends, however it ends -- except when the run
    // only stopped at the edge of its design space: the optimisation goes on in a larger one, and the card that shows it must not go and come back
    // while that space is made (it looked like something had broken).  The last picture stays, with a note, until the next run puts its own)
    struct LivePicture
    {
        bool goesOn = false;
        ~LivePicture()
        {
            if (goesOn)
            {
                if (auto last = run_progress::liveView())
                {
                    auto kept = std::make_shared<run_progress::LiveView>(*last);
                    kept->note = "grow";
                    run_progress::publishLive(kept);
                    return;
                }
            }
            run_progress::publishLive(nullptr);
        }
    } livePicture;

    // Which element lies across each face of an element (-1: none): for the loose-material rule
    std::vector<std::array<int32_t, 4>> across(na);
    {
        struct Face { int32_t a, b, c, element, slot; };
        std::vector<Face> faces;
        faces.reserve(4 * na);
        static const int corner[4][3] = {{1, 2, 3}, {0, 2, 3}, {0, 1, 3}, {0, 1, 2}};
        for (size_t e = 0; e < na; ++e)
        {
            across[e] = {-1, -1, -1, -1};
            for (int q = 0; q < 4; ++q)
            {
                int32_t v[3] = {int32_t(mesh.tets[e][size_t(corner[q][0])]), int32_t(mesh.tets[e][size_t(corner[q][1])]),
                                int32_t(mesh.tets[e][size_t(corner[q][2])])};
                std::sort(v, v + 3);
                faces.push_back({v[0], v[1], v[2], int32_t(e), q});
            }
        }
        std::sort(faces.begin(), faces.end(), [](const Face& x, const Face& y) {
            return x.a != y.a ? x.a < y.a : x.b != y.b ? x.b < y.b : x.c < y.c;
        });
        for (size_t i = 0; i + 1 < faces.size(); ++i)
            if (faces[i].a == faces[i + 1].a && faces[i].b == faces[i + 1].b && faces[i].c == faces[i + 1].c)
            {
                across[size_t(faces[i].element)][size_t(faces[i].slot)] = faces[i + 1].element;
                across[size_t(faces[i + 1].element)][size_t(faces[i + 1].slot)] = faces[i].element;
            }
    }
    std::vector<char> floating(na, 0);

    // The edge of the design space, where it lies outside the part: the elements with a face that no element lies across, farther from the part than
    // the mesh's own slack beside its surface.  The highest density in them is how far the design wants to reach (DesignResult::frontier)
    std::vector<uint32_t> edgeElements;
    if (grown)
    {
        std::vector<float> blocked;
        if (s.frontierBlocked.is_valid()) evalTreePoints(s.frontierBlocked, centres, blocked);
        for (size_t a = 0; a < na; ++a)
            if (state[a] == 0 && outside[a] && double(originVals[a]) > s.frontierDistance && (blocked.empty() || !(blocked[a] < 0)) &&
                (across[a][0] < 0 || across[a][1] < 0 || across[a][2] < 0 || across[a][3] < 0))
                edgeElements.push_back(uint32_t(a));
    }
    auto frontierOf = [&](const std::vector<double>& xs) {
        double f = 0;
        for (const uint32_t a : edgeElements) f = std::max(f, xs[a]);
        return f;
    };

    // The design variables: each free element, or each column of an extrusion (its elements share one value)
    std::vector<int> varOf(na, -1);
    std::vector<size_t> varFirst;                       // a representative element of each variable
    {
        std::vector<int> columnVar(size_t(std::max(nCol, 1)), -1);
        for (size_t a = 0; a < na; ++a)
        {
            if (state[a]) continue;
            if (nCol && column[a] >= 0)
            {
                int& v = columnVar[size_t(column[a])];
                if (v < 0)
                {
                    v = int(varFirst.size());
                    varFirst.push_back(a);
                }
                varOf[a] = v;
            }
            else
            {
                varOf[a] = int(varFirst.size());
                varFirst.push_back(a);
            }
        }
    }
    const size_t nVar = varFirst.size();
    if (nVar == 0)
    {
        error = "everything in the design space is kept or avoided: there is nothing to design";
        return false;
    }
    std::vector<std::vector<double>> dB(size_t(nb), std::vector<double>(na, 0.0));          // d measure / d density of each element
    std::vector<std::vector<double>> gB(size_t(nb), std::vector<double>(nVar, 0.0));        // ... through the filter, the projection, the symmetry and the columns, per variable
    std::vector<double> bv(size_t(nb), 0.0);

    // The step: how far a density may move in one iteration.  It starts as large as the iterations asked for allow (a few have to go the
    // whole way; many can be careful), the cap on it shrinks as they go so that the design has settled by the last, and within the cap it
    // grows while the objective falls as the sensitivities predicted and shrinks when it does not -- a trust region: the change the
    // sensitivities predict for the step taken is compared with the change the next iteration's objective shows
    const double moveStart = s.moveStart > 0 ? s.moveStart : std::max(0.2, std::min(0.5, 10.0 / std::max(1, s.iterations))), moveEnd = 0.05;
    // (a run that goes on from one that stopped has its iteration, sharpening stage, step and scale: see DesignSettings::resume)
    const bool resumed = s.resume.size() >= 5;
    double moveNow = resumed ? s.resume[3] : moveStart, predicted = 0, betaPrev = -1;
    bool havePrediction = false;
    // What a design is worth: its objective (scaled) plus a penalty on each constraint it breaks by more than a thousandth (the share of its
    // limit that the result is allowed to miss it by).  The step control compares designs by it, and the design that is given back is the
    // best one by it
    const double penaltyWeight = 50.0, slack = 1e-3;
    struct Snapshot
    {
        std::vector<double> x, outValue, physicsState;
        std::vector<std::vector<double>> gOut;
        std::vector<char> floating;
        double objective = 0, merit = 0, violation = 0, beta = 0;
        bool valid = false;
    };
    Snapshot accepted, best;                            // (the design the last step led from, and the best design evaluated so far)
    int stepsBack = 0;
    topopt::Mma mma(nVar, nOut - 1);
    double objectiveScale = resumed ? s.resume[4] : 0;
    std::vector<double> xv(nVar), xvNew, loV(nVar, 0.0), hiV(nVar, 1.0);
    double lastViolation = 0;
    const char* fdEnv = std::getenv("FIELDES_TOPO_FDCHECK");
    const int fdAt = fdEnv ? std::atoi(fdEnv) : -1;
    // (the sharpening's stage, and how long it has been held up: see the top of the loop.  A run that goes on from one that stopped at the edge of its
    // space has the iteration, the stage and the delay that one had: the schedule of the whole run is the same, however many spaces it takes)
    int it = resumed ? int(s.resume[0]) : 0, betaStage = resumed ? int(s.resume[1]) : 0, betaDelay = resumed ? int(s.resume[2]) : 0;
    const int runStart = it;
    bool stopped = false;                       // (it stopped because the design reached the edge of its space)
    const int betaDelayMax = std::max(4, s.iterations / 10);
    // What a design is worth: the measures with their sensitivities to the density of each element, and the formulas of the objective and
    // the constraints over them (outValue, outPartial).  update: the physics may move its own state on (not when the design is only looked at a
    // little off its place)
    std::vector<double> outValue;
    std::vector<std::vector<double>> outPartial;
    auto evaluate = [&](bool update) -> bool {
        if (!phys.evaluate(xPhys, update, it, bv, dB, error)) return false;
        // The objective and the constraints, and their derivatives over the measures
        if (!s.program.evaluate(bv, outValue, outPartial, error)) return false;
        if (int(outValue.size()) != nOut)
        {
            error = "the formulas of the optimisation are not what they should be";
            return false;
        }
        return true;
    };

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
        if (betaMax > 0)
        {
            // The sharpening goes up a stage (beta doubles) every projEvery iterations from projStart.  A sharper design breaks the constraints
            // that the soft one kept (a stress limit above all, which a grey member under-reports): a stage waits while a constraint is
            // broken by more than a hundredth, for at most a tenth of the iterations in all, so that the last stage is never short of its share
            if (!smoothBeta && betaStage < doublings && it >= projStart + (betaStage + 1) * projEvery + betaDelay)
            {
                if ((accepted.valid ? accepted.violation : 0.0) <= 1e-2 || betaDelay >= betaDelayMax) ++betaStage;
                else ++betaDelay;
            }
            if (smoothBeta)
            {
                // a little each iteration from projStart to three quarters of the run; held up (at most betaDelayMax iterations in all) while a constraint is broken
                const int last = std::max(projStart + 1, int(std::lround(0.75 * s.iterations)));
                if (it >= projStart && betaProgress < double(doublings))
                {
                    if ((accepted.valid ? accepted.violation : 0.0) > 1e-2 && betaDelay < betaDelayMax) ++betaDelay;
                    else betaProgress = std::min(double(doublings), betaProgress + double(doublings) / double(last - projStart));
                }
                beta = std::min(betaMax, std::pow(2.0, betaProgress));
            }
            else
                beta = std::min(betaMax, std::pow(2.0, double(betaStage)));
        }
        physical(x, xPhys);
        // Material that nothing joins to a support or a load carries nothing, and is taken out: left alone, an island grows from the
        // stress at the corner of a support into the empty space beside the part, and stays there -- using the volume of a member that
        // would be of use.  Material is where the density is above 0.3; two elements are joined when they share a face (a piece that
        // touches the part at one corner is not held by it)
        std::fill(floating.begin(), floating.end(), 0);
        if (it >= 10 && !supported.empty())
        {
            std::vector<int32_t> parent(na);
            std::iota(parent.begin(), parent.end(), 0);
            auto root = [&](int32_t a) {
                while (parent[size_t(a)] != a)
                {
                    parent[size_t(a)] = parent[size_t(parent[size_t(a)])];
                    a = parent[size_t(a)];
                }
                return a;
            };
            auto material = [&](size_t a) { return state[a] == 1 || xPhys[a] > 0.3; };
            for (size_t a = 0; a < na; ++a)
            {
                if (!material(a)) continue;
                for (int q = 0; q < 4; ++q)
                {
                    const int32_t b = across[a][size_t(q)];
                    if (b < 0 || !material(size_t(b))) continue;
                    const int32_t r0 = root(int32_t(a)), r1 = root(b);
                    if (r0 != r1) parent[size_t(r1)] = r0;
                }
            }
            // (material is held when it joins an element that holds a support: the slab under a load, or a keep region, holds nothing itself)
            std::vector<char> anchored(na, 0);
            for (size_t a = 0; a < na; ++a)
                if (supported[a]) anchored[size_t(root(int32_t(a)))] = 1;
            for (size_t a = 0; a < na; ++a)
                if (state[a] == 0 && xPhys[a] > 0.3 && !anchored[size_t(root(int32_t(a)))]) floating[a] = 1;
            if (debug)
            {
                size_t cnt = 0;
                double volume = 0;
                for (size_t a = 0; a < na; ++a) if (floating[a]) { ++cnt; volume += vol[a]; }
                fprintf(stderr, "[tetopt] iteration %d: %zu elements of loose material (%.3g mm^3)\n", it, cnt, volume);
            }
        }
        if (!evaluate(true)) return false;

        out.baseHistory.push_back(bv);
        out.outputHistory.push_back(outValue);
        out.history.push_back(s.objectiveSign * outValue[0]);
        out.densityHistory.emplace_back();
        nodal(xPhys, out.densityHistory.back());
        if (objectiveScale <= 0) objectiveScale = std::abs(outValue[0]) > 1e-12 ? 10.0 / std::abs(outValue[0]) : 1.0;

        // Through the projection to the filtered density, the filter, the symmetry and the columns, to the variables
        for (int b = 0; b < nb; ++b)
        {
            std::vector<double>& g = dB[size_t(b)];
            if (betaMax > 0)
                for (size_t a = 0; a < na; ++a) g[a] *= state[a] ? 0.0 : projectSlope(xTil[a]);
            filterT(g, dcF);
            if (!mirrorMaps.empty()) symmetrise(dcF);
            if (nCol)
            {
                // (one value per line: the line's sensitivities summed; x stays equal along each line, starting equal)
                std::vector<double> cs(size_t(nCol), 0.0);
                for (size_t a = 0; a < na; ++a)
                    if (column[a] >= 0) cs[size_t(column[a])] += dcF[a];
                for (size_t a = 0; a < na; ++a)
                    if (column[a] >= 0) dcF[a] = cs[size_t(column[a])];
                if (!mirrorMaps.empty()) symmetrise(dcF);       // (a line and its mirror image share their sums)
            }
            for (size_t v = 0; v < nVar; ++v) gB[size_t(b)][v] = dcF[varFirst[v]];
        }
        if (fdAt >= 0 && it == fdAt)
        {
            // A check of the sensitivities (FIELDES_TOPO_FDCHECK=<iteration>): the change of every measure along a direction of the design, once from
            // the derivatives above and once from two evaluations a little off the design to either side (the solutions to a tight tolerance)
            const std::vector<double> bv0 = bv, outValue0 = outValue;
            const std::vector<std::vector<double>> outPartial0 = outPartial;
            phys.tightSolves(true);
            for (int probe = 0; probe < 2; ++probe)
            {
                std::vector<double> dir(na, 0.0);
                uint32_t seed = 12345u + 977u * uint32_t(probe);
                std::vector<double> dVar(nVar);
                for (size_t v = 0; v < nVar; ++v)
                {
                    seed = seed * 1664525u + 1013904223u;
                    dVar[v] = double((seed >> 8) & 0xFFFFu) / 32768.0 - 1.0;
                }
                // (a direction that dies out at the bounds, so that a step either way keeps the density within them)
                for (size_t a = 0; a < na; ++a)
                    if (varOf[a] >= 0) dir[a] = dVar[size_t(varOf[a])] * 4.0 * x[a] * (1.0 - x[a]);
                if (!mirrorMaps.empty()) symmetrise(dir);
                const double step = 1e-3;
                std::vector<std::vector<double>> at(2);
                for (int side = 0; side < 2; ++side)
                {
                    std::vector<double> xs(x);
                    for (size_t a = 0; a < na; ++a) xs[a] += (side == 0 ? step : -step) * dir[a];
                    physical(xs, xPhys);
                    if (!evaluate(false)) return false;
                    at[size_t(side)] = bv;
                }
                fprintf(stderr, "[tetopt] FD check, iteration %d, direction %d:", it, probe);
                for (int b = 0; b < nb; ++b)
                {
                    double analytic = 0;
                    for (size_t v = 0; v < nVar; ++v) analytic += gB[size_t(b)][v] * dir[varFirst[v]];
                    const double numeric = (at[0][size_t(b)] - at[1][size_t(b)]) / (2.0 * step);
                    fprintf(stderr, " measure %d (value %.6g) derivative %.6g, from two evaluations %.6g;", b, bv0[size_t(b)], analytic, numeric);
                }
                fprintf(stderr, "\n");
            }
            phys.tightSolves(false);
            physical(x, xPhys);
            bv = bv0;
            outValue = outValue0;
            outPartial = outPartial0;
        }
        std::vector<std::vector<double>> gOut(size_t(nOut), std::vector<double>(nVar, 0.0));
        for (int k = 0; k < nOut; ++k)
            for (int b = 0; b < nb; ++b)
            {
                const double w = outPartial[size_t(k)][size_t(b)];
                if (w == 0.0) continue;
                for (size_t v = 0; v < nVar; ++v) gOut[size_t(k)][v] += w * gB[size_t(b)][v];
            }
        double violation = 0;
        for (int k = 1; k < nOut; ++k) violation = std::max(violation, outValue[size_t(k)]);
        lastViolation = violation;

        // the step of this iteration (see above)
        const double moveCap = moveStart + (moveEnd - moveStart) * std::min(1.0, double(it) / std::max(1, s.iterations - 1));
        double objectiveNow = outValue[0] * objectiveScale;
        auto meritOf = [&](const std::vector<double>& res, double objectiveScaled) {
            double w = objectiveScaled;
            for (int k = 1; k < nOut; ++k) w += penaltyWeight * std::max(0.0, res[size_t(k)] - slack);
            return w;
        };
        double meritNow = meritOf(outValue, objectiveNow);
        // A step that led to a worse design -- by more than half of what the sensitivities promised, and by more than a fifth of a percent --
        // went too far (they hold close to the design only): the design it came from is taken up again, and the step is halved
        bool rejected = false;
        if (accepted.valid && havePrediction && sameBeta(beta, accepted.beta) && moveNow > 0.0201 &&
            meritNow - accepted.merit > std::max(2e-3 * (std::abs(accepted.merit) + 1e-9), 0.5 * std::abs(predicted)))
        {
            rejected = true;
            ++stepsBack;
            x = accepted.x;
            floating = accepted.floating;
            phys.restoreState(accepted.physicsState);
            outValue = accepted.outValue;
            gOut = accepted.gOut;
            violation = accepted.violation;
            objectiveNow = accepted.objective;
            meritNow = accepted.merit;
            out.history.pop_back();
            out.baseHistory.pop_back();
            out.outputHistory.pop_back();
            out.densityHistory.pop_back();
            moveNow = std::max(0.02, 0.5 * moveNow);
        }
        else if (accepted.valid && havePrediction && predicted < -1e-12 * (std::abs(accepted.merit) + 1e-9) && sameBeta(beta, betaPrev))
        {
            const double ratio = (meritNow - accepted.merit) / predicted;         // 1: it fell as predicted; below 0: it rose
            if (ratio > 0.75) moveNow *= 1.4;
            else if (ratio < 0.25) moveNow *= 0.6;
        }
        moveNow = std::max(0.02, std::min(moveNow, moveCap));
        const double move = moveNow;
        if (!rejected)
        {
            accepted.x = x;
            accepted.outValue = outValue;
            accepted.gOut = gOut;
            accepted.floating = floating;
            accepted.physicsState = phys.saveState();
            accepted.objective = objectiveNow;
            accepted.merit = meritNow;
            accepted.violation = violation;
            accepted.beta = beta;
            accepted.valid = true;
            if (!best.valid || best.beta != beta || accepted.merit < best.merit) best = accepted;
        }

        // the next design: the method of moving asymptotes, on the objective and the constraints
        for (size_t v = 0; v < nVar; ++v)
        {
            const size_t a = varFirst[v];
            xv[v] = x[a];
            loV[v] = 0.0;
            hiV[v] = floating[a] ? std::max(0.0, x[a] - move) : 1.0;
        }
        std::vector<double> df0(nVar), gVal(size_t(nOut - 1));
        for (size_t v = 0; v < nVar; ++v) df0[v] = gOut[0][v] * objectiveScale;
        std::vector<std::vector<double>> dg(size_t(nOut - 1));
        for (int k = 1; k < nOut; ++k)
        {
            gVal[size_t(k - 1)] = outValue[size_t(k)];
            dg[size_t(k - 1)] = gOut[size_t(k)];
        }
        if (!mma.step(xv, objectiveNow, df0, gVal, dg, loV, hiV, move, xvNew))
        {
            error = "the update of the design could not be made";
            return false;
        }
        double change = 0;
        std::fill(xNew.begin(), xNew.end(), 0.0);
        for (size_t a = 0; a < na; ++a)
        {
            if (state[a])
                xNew[a] = x[a];
            else
                xNew[a] = xvNew[size_t(varOf[a])];
            change = std::max(change, std::abs(xNew[a] - x[a]));
        }
        if (!mirrorMaps.empty())
        {
            // The design is kept on the symmetric side of every plane: a tiny difference between a pair is not allowed to grow (a symmetric design is often a saddle of the
            // asymmetric ones, and rounding alone makes a lopsided body of it)
            symmetrise(xNew);
            change = 0;
            for (size_t a = 0; a < na; ++a)
            {
                if (state[a]) xNew[a] = x[a];
                change = std::max(change, std::abs(xNew[a] - x[a]));
            }
        }
        if (!rejected)
        {
            double kept = 0;
            for (size_t a = 0; a < na; ++a) kept += vol[a] * xPhys[a];
            const IterationView view{it + 1, s.iterations, s.objectiveSign * outValue[0], change, total > 0 ? kept / total : 0.0, out.history, xPhys,
                                     out.densityHistory.back(), rmin};
            phys.iterationKept(view);
        }
        // (what the sensitivities say this step changes the worth of the design by: for the next iteration to compare with what it did)
        havePrediction = true;
        predicted = 0;
        for (size_t v = 0; v < nVar; ++v) predicted += df0[v] * (xvNew[v] - xv[v]);
        for (int k = 1; k < nOut; ++k)
        {
            const double now = gVal[size_t(k - 1)];
            double then = now;
            for (size_t v = 0; v < nVar; ++v) then += dg[size_t(k - 1)][v] * (xvNew[v] - xv[v]);
            predicted += penaltyWeight * (std::max(0.0, then - slack) - std::max(0.0, now - slack));
        }
        betaPrev = beta;
        x.swap(xNew);
        // converged: the design stopped moving, or the objective stopped improving over the last 5 iterations -- with every constraint kept
        const size_t H = out.history.size();
        const bool flat = H > 6 && std::abs(out.history[H - 1] - out.history[H - 6]) < 2e-3 * std::abs(out.history[H - 1]);
        if (debug)
        {
            fprintf(stderr, "[tetopt] iteration %d%s beta %.0f objective %.6g violation %.4g change %.4f move %.3f measures", it, rejected ? " (taken back)" : "", beta, outValue[0], violation, change, move);
            for (double v : bv) fprintf(stderr, " %.6g", v);
            fprintf(stderr, "\n");
        }
        // The design has reached the edge of its space (material in an element of the edge that lies outside the part): the run stops here, to go on
        // in a space that is made again round the design, from the step that was just taken -- the design is never held against a wall while the
        // run goes on.  Not after a step that was taken back (the design it came from was not at the edge), not in the last iteration (there is
        // nothing to go on with) and not at once after a run began (the space was made for the design to move in)
        if (s.stopAtEdge && !edgeElements.empty() && !rejected && it + 1 < s.iterations && it - runStart >= 3 && frontierOf(xPhys) > 0.1)
        {
            if (debug) fprintf(stderr, "[tetopt] the design reached the edge of its space at iteration %d (density %.3f)\n", it, frontierOf(xPhys));
            stopped = true;
            livePicture.goesOn = true;
            ++it;
            break;
        }
        if (!rejected && it >= 15 && (betaMax <= 0 || beta >= betaMax) && (change < 0.01 || flat) && violation < 2e-3) { ++it; break; }
    }
    (void)lastViolation;
    // The best design that was evaluated (the last one, when the steps kept making it better): the design after the last step has not been
    // evaluated, and a step that overshoots in the last iterations would be given back as it is.  (A run that stopped at the edge of its space
    // gives back the design it stopped at: the next run goes on from there)
    if (!stopped)
    {
        if (best.valid) x = best.x;
        physical(x, xPhys);
    }
    if (debug)
    {
        double grey = 0, all = 0;
        for (size_t a = 0; a < na; ++a)
        {
            all += vol[a];
            if (xPhys[a] > 0.1 && xPhys[a] < 0.9) grey += vol[a];
        }
        fprintf(stderr, "[tetopt] %d steps were taken back; the design given back has %.1f %% of the volume grey (density between 0.1 and 0.9), beta %.0f\n",
                stepsBack, all > 0 ? 100.0 * grey / all : 0.0, beta);
    }

    out.xPhys = xPhys;
    out.stepsBack = stepsBack;
    nodal(xPhys, out.nodal);
    double v = 0;
    for (size_t a = 0; a < na; ++a) v += vol[a] * xPhys[a];
    out.designVolume = v;
    out.iterations = it;
    // How far the design wants to reach, when the design space ends: the highest density in an element of the edge of the space that lies
    // outside the part
    out.frontier = frontierOf(xPhys);
    if (stopped)
    {
        out.resume = {double(it), double(betaStage), double(betaDelay), moveNow, objectiveScale};
        nodal(x, out.designNodal);
    }
    return true;
}

}   // namespace topopt
}   // namespace fea
}   // namespace libfive
