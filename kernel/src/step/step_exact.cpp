/*
libfive: a CAD kernel for modeling with implicit functions

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#include "libfive/step/step_exact.hpp"
#include "libfive/step/step_model.hpp"
#include "libfive/step/step_parts.hpp"
#include "libfive/step/step_progress.hpp"
#include "libfive/step/step_tessellate.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>
#include <map>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace libfive {
namespace step {

namespace {

// Parsed files and their tessellated solids, kept while the file is
// unchanged: the script asks again on every run.
struct CachedFile
{
    std::filesystem::file_time_type mtime;
    std::uintmax_t size = 0;
    std::shared_ptr<Model> model;
    std::map<std::pair<int, int>, std::shared_ptr<const TessMesh>> tess;   // (solid, turnSamples)
};
std::mutex g_cacheMutex;
std::map<std::string, CachedFile> g_cache;

// Finds the solid (and its instance) in the parsed file, tessellating it on request; the parsed file and
// the tessellations are kept while the file is unchanged
bool locate(const ExactSpec& spec, bool wantTess, std::shared_ptr<const TessMesh>& tess,
            SolidInstance& inst, Vec3& bmin, Vec3& bmax, std::string& error)
{
    std::lock_guard<std::mutex> lock(g_cacheMutex);
    std::error_code ec;
    const auto mtime = std::filesystem::last_write_time(spec.path, ec);
    const auto size = ec ? 0 : std::filesystem::file_size(spec.path, ec);
    if (ec) {
        error = "cannot read " + spec.path;
        return false;
    }
    CachedFile& file = g_cache[spec.path];
    if (!file.model || file.mtime != mtime || file.size != size) {
        ImportResult r = importStepFile(spec.path);
        if (!r.ok || !r.model) {
            g_cache.erase(spec.path);
            error = r.error.empty() ? "cannot import " + spec.path : r.error;
            return false;
        }
        file = CachedFile();
        file.mtime = mtime;
        file.size = size;
        file.model = r.model;
    }
    const auto& solids = file.model->solids;
    if (spec.solid < 0 || size_t(spec.solid) >= solids.size()) {
        error = "no solid " + std::to_string(spec.solid) + " in " + spec.path;
        return false;
    }
    const Solid& solid = solids[size_t(spec.solid)];
    if (!solid.instances.empty()) {
        if (spec.instance < 0 || size_t(spec.instance) >= solid.instances.size()) {
            error = "no instance " + std::to_string(spec.instance) + " of solid " + std::to_string(spec.solid);
            return false;
        }
        inst = solid.instances[size_t(spec.instance)];
    }
    bmin = solid.boundMin;
    bmax = solid.boundMax;
    if (wantTess) {
        auto& t = file.tess[{spec.solid, spec.turnSamples}];
        if (!t) t = std::make_shared<const TessMesh>(tessellateSolid(solid, spec.turnSamples));
        tess = t;
    }
    return true;
}

}   // namespace

ExactPiece exactSurface(const ExactSpec& spec)
{
    ExactPiece out;
    std::shared_ptr<const TessMesh> tess;
    SolidInstance inst;
    Vec3 bmin, bmax;
    if (!locate(spec, true, tess, inst, bmin, bmax, out.error)) return out;

    // Placed: the instance, then the user's transform
    const Eigen::Matrix3d L = spec.transform.topLeftCorner<3, 3>() * inst.linear;
    const Eigen::Vector3d o = spec.transform.topLeftCorner<3, 3>() * inst.offset +
                              spec.transform.topRightCorner<3, 1>();
    const bool mirrored = L.determinant() < 0;
    out.verts.reserve(tess->verts.size());
    for (const Vec3& v : tess->verts) {
        out.verts.push_back((L * v + o).cast<float>());
    }
    out.tris.reserve(tess->tris.size());
    for (const auto& t : tess->tris) {
        out.tris.emplace_back(t[0], mirrored ? t[2] : t[1], mirrored ? t[1] : t[2]);
    }
    return out;
}

BrepParts brepParts(const std::string& path, int turnSamples)
{
    BrepParts out;
    progress::begin(path);
    struct End { ~End() { progress::end(); } } end;
    std::shared_ptr<Model> model;
    {
        std::lock_guard<std::mutex> lock(g_cacheMutex);
        std::error_code ec;
        const auto mtime = std::filesystem::last_write_time(path, ec);
        const auto size = ec ? 0 : std::filesystem::file_size(path, ec);
        if (ec) {
            out.error = "cannot read " + path;
            return out;
        }
        CachedFile& file = g_cache[path];
        if (!file.model || file.mtime != mtime || file.size != size) {
            ImportResult r = importStepFile(path);
            if (!r.ok || !r.model) {
                g_cache.erase(path);
                out.error = r.error.empty() ? "cannot import " + path : r.error;
                return out;
            }
            file = CachedFile();
            file.mtime = mtime;
            file.size = size;
            file.model = r.model;
        }
        model = file.model;
    }
    const auto& solids = model->solids;
    const size_t N = solids.size();
    out.solids.resize(N);

    // Every solid tessellated: the biggest first, a few at a time (each also refines its free-form faces on
    // all the threads there are)
    const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
    std::vector<std::shared_ptr<const TessMesh>> tess(N);
    std::vector<size_t> order(N);
    for (size_t i = 0; i < N; i++) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return solids[a].faces.size() > solids[b].faces.size();
    });
    progress::setSolids(N);
    std::atomic<size_t> next{0};
    auto work = [&]() {
        for (size_t q; (q = next.fetch_add(1)) < N;) {
            const size_t si = order[q];
            progress::Scope scope{long(si)};
            progress::solidStarted(si);
            struct Done { size_t si; ~Done() { progress::solidDone(si); } } done{si};
            BrepSolid& b = out.solids[si];
            b.faces = int(solids[si].faces.size());
            for (const Face& f : solids[si].faces) b.bsplineFaces += f.surface.kind == SurfaceKind::BSpline;
            try {
                tess[si] = std::make_shared<const TessMesh>(tessellateSolid(solids[si], turnSamples, 0));
            } catch (const std::exception& e) {
                b.mesh.error = "solid " + std::to_string(si) + ": tessellation failed: " + e.what();
                continue;
            }
            if (tess[si]->tris.empty()) {
                b.mesh.error = "solid " + std::to_string(si) + ": it has no surface to tessellate";
                tess[si].reset();
                continue;
            }
            b.mesh.verts.reserve(tess[si]->verts.size());
            for (const Vec3& v : tess[si]->verts) b.mesh.verts.push_back(v.cast<float>());
            b.mesh.tris.reserve(tess[si]->tris.size());
            for (const auto& t : tess[si]->tris) b.mesh.tris.emplace_back(t[0], t[1], t[2]);
        }
    };
    {
        const size_t nThreads = std::max<size_t>(1, std::min<size_t>(N, std::max(2u, hw / 2)));
        std::vector<std::thread> pool;
        for (size_t t = 1; t < nThreads; t++) pool.emplace_back(work);
        work();
        for (auto& th : pool) th.join();
    }

    // The placed occurrences: the first of each solid in the solid's slot, the rest after the last solid
    std::vector<BrepInstance> extra;
    for (size_t si = 0; si < N; si++) {
        const Solid& solid = solids[si];
        SolidMetrics metrics;
        if (tess[si]) metrics = solidMetricsFromMesh(solid, *tess[si]);
        std::vector<SolidInstance> list = solid.instances;
        if (list.empty()) list.emplace_back();
        for (size_t k = 0; k < list.size(); k++) {
            const SolidInstance& inst = list[k];
            BrepInstance p;
            p.solid = int(si);
            p.instance = int(k);
            p.linear = inst.linear;
            p.offset = inst.offset;
            p.name = inst.name.empty() ? solid.name : inst.name;
            Eigen::Vector3d lo = Eigen::Vector3d::Constant(std::numeric_limits<double>::infinity()), hi = -lo;
            if (tess[si]) {
                for (const Vec3& v : tess[si]->verts) {
                    const Eigen::Vector3d q = inst.linear * v + inst.offset;
                    lo = lo.cwiseMin(q);
                    hi = hi.cwiseMax(q);
                }
            }
            if (!lo.allFinite() || !hi.allFinite()) {
                lo = hi = Eigen::Vector3d::Zero();
                placeBox(solid.boundMin, solid.boundMax, inst, lo, hi);
            }
            p.boundMin = lo;
            p.boundMax = hi;
            const double unit = std::cbrt(std::abs(inst.linear.determinant()));
            p.detail = metrics.detail * unit;
            p.areaFlat = metrics.areaFlat * unit * unit;
            p.areaCurved = metrics.areaCurved * unit * unit;
            (k == 0 ? out.instances : extra).push_back(p);
        }
    }
    out.instances.insert(out.instances.end(), extra.begin(), extra.end());
    return out;
}

}   // namespace step
}   // namespace libfive
