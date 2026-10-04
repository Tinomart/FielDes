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
#include "libfive/eval/deck.hpp"
#include "libfive/eval/eval_array.hpp"
#include "libfive/eval/eval_interval.hpp"
#include "libfive/eval/tape.hpp"

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
// unchanged: Studio asks again on every re-render of the script.
struct CachedFile
{
    std::filesystem::file_time_type mtime;
    std::uintmax_t size = 0;
    std::shared_ptr<Model> model;
    std::map<std::pair<int, int>, std::shared_ptr<const TessMesh>> tess;   // (solid, turnSamples)
};
std::mutex g_cacheMutex;
std::map<std::string, CachedFile> g_cache;

// A vertex position, for sharing the vertices of the triangles a split makes
struct PointKey
{
    std::array<uint32_t, 3> bits;
    explicit PointKey(const Eigen::Vector3f& p)
    {
        for (int a = 0; a < 3; a++) std::memcpy(&bits[size_t(a)], &p[a], sizeof(uint32_t));
    }
    bool operator==(const PointKey& o) const { return bits == o.bits; }
};
struct PointHash
{
    size_t operator()(const PointKey& k) const
    {
        size_t h = 1469598103934665603ull;
        for (uint32_t b : k.bits) h = (h ^ b) * 1099511628211ull;
        return h;
    }
};

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

bool exactReaches(const ExactSpec& spec, const Tree& field, const std::map<Tree::Id, float>& vars)
{
    std::shared_ptr<const TessMesh> none;
    SolidInstance inst;
    Vec3 bmin, bmax;
    std::string error;
    if (!locate(spec, false, none, inst, bmin, bmax, error)) return true;      // (the real call says why)
    // the part's box where it is placed: its corners, moved
    const Eigen::Matrix3d L = spec.transform.topLeftCorner<3, 3>() * inst.linear;
    const Eigen::Vector3d o = spec.transform.topLeftCorner<3, 3>() * inst.offset +
                              spec.transform.topRightCorner<3, 1>();
    const double pad = 1e-3 * std::max((bmax - bmin).norm(), 1.0);
    Eigen::Vector3f lo(1e30f, 1e30f, 1e30f), hi(-1e30f, -1e30f, -1e30f);
    for (int c = 0; c < 8; c++) {
        const Vec3 corner((c & 1) ? bmax.x() + pad : bmin.x() - pad,
                          (c & 2) ? bmax.y() + pad : bmin.y() - pad,
                          (c & 4) ? bmax.z() + pad : bmin.z() - pad);
        const Eigen::Vector3f p = (L * corner + o).cast<float>();
        lo = lo.cwiseMin(p);
        hi = hi.cwiseMax(p);
    }
    IntervalEvaluator e(field, vars);
    return !(e.eval(lo, hi).lower() > 0);
}

ExactPiece clipToRegion(const ExactPiece& surface, const Tree& field,
                        const std::map<Tree::Id, float>& vars, double cell)
{
    ExactPiece out;
    out.error = surface.error;
    if (!surface.error.empty()) return out;
    using V = Eigen::Vector3f;
    IntervalEvaluator e(field, vars);
    const float leaf = float(std::max(cell, 1e-6));

    std::unordered_map<PointKey, int, PointHash> index;
    auto vertex = [&](const V& p) {
        const auto it = index.emplace(PointKey(p), int(out.verts.size()));
        if (it.second) out.verts.push_back(p);
        return it.first->second;
    };

    // (each triangle carries the tape its parent's box shortened: inside a smaller box most of a big
    // field, a union of many parts' regions, is out of play)
    struct Tri { V a, b, c; int depth; std::shared_ptr<Tape> tape; };
    std::vector<Tri> stack;
    stack.reserve(1024);
    if (surface.verts.empty()) return out;
    V lo0 = surface.verts[0], hi0 = surface.verts[0];
    for (const auto& v : surface.verts) {
        lo0 = lo0.cwiseMin(v);
        hi0 = hi0.cwiseMax(v);
    }
    const auto root = e.intervalAndPush(lo0, hi0);          // (the field as it is over this part's box)
    if (root.first.lower() > 0) return out;                 // the region does not reach the part
    for (size_t i = surface.tris.size(); i-- > 0;) {
        const auto& t = surface.tris[i];
        stack.push_back({surface.verts[size_t(t[0])], surface.verts[size_t(t[1])],
                         surface.verts[size_t(t[2])], 0, root.second});
    }
    while (!stack.empty()) {
        const Tri t = stack.back();
        stack.pop_back();
        const V lo = t.a.cwiseMin(t.b).cwiseMin(t.c);
        const V hi = t.a.cwiseMax(t.b).cwiseMax(t.c);
        const Interval r = e.eval(lo, hi, t.tape);
        if (r.lower() > 0) continue;                                 // outside
        if (r.upper() < 0) {                                         // inside, all of it
            const int a = vertex(t.a), b = vertex(t.b), c = vertex(t.c);
            if (a != b && b != c && c != a) out.tris.emplace_back(a, b, c);
            continue;
        }
        // the region's surface crosses it: split the longest edge until it is a cell long
        const float l01 = (t.b - t.a).norm(), l12 = (t.c - t.b).norm(), l20 = (t.a - t.c).norm();
        const float longest = std::max({l01, l12, l20});
        if (longest > leaf && t.depth < 40) {
            V a = t.a, b = t.b, c = t.c;
            if (l12 >= l01 && l12 >= l20) { a = t.b; b = t.c; c = t.a; }        // (rotated, same winding)
            else if (l20 >= l01 && l20 >= l12) { a = t.c; b = t.a; c = t.b; }
            const V m = 0.5f * (a + b);
            const auto shorter = e.push(t.tape);                     // (for what is left of this box)
            stack.push_back({m, b, c, t.depth + 1, shorter});
            stack.push_back({a, m, c, t.depth + 1, shorter});
            continue;
        }
        const V mid = (t.a + t.b + t.c) / 3.0f;
        if (e.eval(mid, mid, t.tape).upper() <= 0) {                         // small: in if its middle is in
            const int a = vertex(t.a), b = vertex(t.b), c = vertex(t.c);
            if (a != b && b != c && c != a) out.tris.emplace_back(a, b, c);
        }
    }
    return out;
}

std::unique_ptr<Mesh> removeInside(const Mesh& m, const Tree& field,
                                   const std::map<Tree::Id, float>& vars)
{
    using V = Eigen::Vector3f;
    const size_t n = m.verts.size();
    if (n < 2 || m.branes.empty()) return nullptr;

    // Which vertices are inside the region: a grid of blocks, each tested as a whole by interval
    // arithmetic (outside, inside, or -- near the region's surface -- vertex by vertex)
    V lo = m.verts[1], hi = m.verts[1];
    for (size_t i = 1; i < n; i++) {
        lo = lo.cwiseMin(m.verts[i]);
        hi = hi.cwiseMax(m.verts[i]);
    }
    const auto deck = std::make_shared<Deck>(field);
    IntervalEvaluator ie(deck, vars);
    const auto root = ie.intervalAndPush(lo, hi);          // (the field as it is over this mesh's box)
    if (root.first.lower() > 0) return nullptr;            // the region is nowhere near this mesh
    const int G = 16;
    const V span = (hi - lo).cwiseMax(1e-6f);
    auto cellOf = [&](const V& p) {
        std::array<int, 3> c;
        for (int a = 0; a < 3; a++) c[size_t(a)] = std::min(G - 1, std::max(0, int((p[a] - lo[a]) / span[a] * G)));
        return (c[0] * G + c[1]) * G + c[2];
    };
    std::vector<signed char> block(size_t(G * G * G), 0);      // 1 outside, -1 inside, 0 vertex by vertex
    std::vector<std::shared_ptr<Tape>> blockTape(size_t(G * G * G));
    for (int i = 0; i < G; i++)
        for (int j = 0; j < G; j++)
            for (int k = 0; k < G; k++) {
                const V a = lo + V(span.x() * i / G, span.y() * j / G, span.z() * k / G);
                const V b = lo + V(span.x() * (i + 1) / G, span.y() * (j + 1) / G, span.z() * (k + 1) / G);
                const Interval r = ie.eval(a, b, root.second);
                const size_t at = size_t((i * G + j) * G + k);
                block[at] = r.lower() > 0 ? 1 : (r.upper() < 0 ? -1 : 0);
                if (block[at] == 0) blockTape[at] = ie.push(root.second);
            }
    std::vector<char> inside(n, 0);
    std::vector<std::vector<size_t>> todo(size_t(G * G * G));
    for (size_t i = 1; i < n; i++) {
        const size_t at = size_t(cellOf(m.verts[i]));
        const signed char b = block[at];
        if (b < 0) inside[i] = 1;
        else if (b == 0) todo[at].push_back(i);
    }
    {
        ArrayEvaluator ae(deck, vars);
        const size_t N = LIBFIVE_EVAL_ARRAY_SIZE;
        for (size_t at = 0; at < todo.size(); at++) {
            const auto& list = todo[at];
            for (size_t s = 0; s < list.size(); s += N) {
                const size_t k = std::min(N, list.size() - s);
                for (size_t j = 0; j < k; j++) ae.set(m.verts[list[s + j]], j);
                const auto vals = blockTape[at] ? ae.values(k, *blockTape[at]) : ae.values(k);
                for (size_t j = 0; j < k; j++) inside[list[s + j]] = vals[j] < 0 ? 1 : 0;
            }
        }
    }

    // The triangles with most of their corners inside go
    std::vector<char> keep(m.branes.size(), 1);
    size_t removed = 0;
    for (size_t t = 0; t < m.branes.size(); t++) {
        const auto& b = m.branes[t];
        const int in = inside[b[0]] + inside[b[1]] + inside[b[2]];
        if (in >= 2) {
            keep[t] = 0;
            removed++;
        }
    }
    if (!removed) return nullptr;

    std::unique_ptr<Mesh> out(new Mesh());
    out->verts.push_back(m.verts[0]);                           // (vertex 0 is only a marker)
    std::vector<uint32_t> remap(n, 0);
    for (size_t t = 0; t < m.branes.size(); t++) {
        if (!keep[t]) continue;
        std::array<uint32_t, 3> tri;
        for (int k = 0; k < 3; k++) {
            const uint32_t v = m.branes[t][k];
            if (!remap[v]) {
                remap[v] = uint32_t(out->verts.size());
                out->verts.push_back(m.verts[v]);
            }
            tri[size_t(k)] = remap[v];
        }
        out->branes.push_back({tri[0], tri[1], tri[2]});
    }
    return out;
}

std::unique_ptr<Mesh> joinExact(const Mesh& field, const std::vector<ExactPiece>& pieces)
{
    std::unique_ptr<Mesh> out(new Mesh());
    out->verts = field.verts;
    out->branes = field.branes;
    if (out->verts.empty()) out->verts.push_back(Eigen::Vector3f::Zero());   // (the marker)
    for (const auto& p : pieces) {
        if (!p.error.empty()) continue;
        const uint32_t base = uint32_t(out->verts.size());
        for (const auto& v : p.verts) out->verts.push_back(v);
        for (const auto& t : p.tris) {
            out->branes.push_back({base + uint32_t(t[0]), base + uint32_t(t[1]), base + uint32_t(t[2])});
        }
    }
    return out;
}

}   // namespace step
}   // namespace libfive
