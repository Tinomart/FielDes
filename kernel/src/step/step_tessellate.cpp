/*
libfive: a CAD kernel for modeling with implicit functions

Direct B-rep tessellation, replacing dual-contouring for STEP-imported
solids. Confirmed this session (2026-09-23): wrapping the raw trimmed
B-rep as a live Oracle and letting libfive's octree/dual-contouring
rediscover its topology through blind spatial sampling produces severe,
resolution-INDEPENDENT fragmentation (going from resolution 80 to 5000
made a real hinge part's fragment count go from 7 to 151, not better --
confirming this isn't an under-sampling artifact but a structural
mismatch between "voxel rediscovery" and a format that already hands us
exact topology). A from-scratch two-stage alternative (also confirmed
this session, via the old pre-rewrite build, which used plain nearest-
face evaluation with zero winding-number machinery and still meshed a
602-face table top as one single connected piece where the newer
winding-number-based Oracle fragmented it into a dozen pieces): STEP
already tells us exactly which faces share which edges, in exact
analytic form -- throwing that away and trying to rediscover it through
octree sampling is fighting the format, not using it.

This file is the first of that two stages: tessellate each trimmed
face's own UV domain directly (ear-clipping via triangulatePolygon,
holes bridged in), stitching adjacent faces via a shared per-EDGE_CURVE
3D sampling cache (see EdgeSpan::edgeCurveId) so two faces meeting along
a manifold edge get bit-identical boundary vertices -- watertight by
construction, not by chance. No octree, no resolution parameter, no
implicit-function sampling anywhere in this path; cost scales with the
model's actual geometric complexity, not a voxel budget. The second
stage (mesh-based generalized winding number, turning this triangle
soup into a fast SDF oracle usable for CSG/field math) is
step_mesh_oracle.cpp.

Boundary-only triangulation, then interior refinement: for the narrow curved
bands typical of fillets, the two boundary curves (already finely sampled --
see the sample-count comment in step_face.cpp) are close together, so the
outline triangulation is already a reasonable tessellation.  A wide,
gently-curved face's interior would look faceted, so the edges inside a
curved face (cylinder, cone, sphere -- and a free-form B-spline face, which
is what a designed panel is) are split at their (u, v) middle, on the
surface, until no edge turns the surface by more than a turn's share (2 pi
over `turnSamples`), see the end of tessellateSolid.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#include "libfive/step/step_tessellate.hpp"
#include "libfive/step/step_bspline.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <cstring>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include <manifold/polygon.h>

namespace libfive {
namespace step {

namespace {

// Canonical 3D tessellation of one EDGE_CURVE, built once (in whichever
// EdgeSpan happens to reach it first) and reused by every other
// EdgeSpan referencing the same edgeCurveId -- this identity is what
// makes adjacent faces' boundaries bit-identical.
struct EdgeCacheEntry
{
    std::vector<Vec3> points;  // in this entry's own t0 -> t1 direction
    double t0 = 0, t1 = 0;
};

// Same angular-resolution policy as sampleEdgeIntoParamSpace
// (step_face.cpp) for circle/ellipse/B-spline (so sagitta scales with
// the curve's own radius, not a global setting) -- but NOT the same
// numeric default for a line. The loop below is `i <= samples` (i.e.
// samples+1 points); sampleEdgeIntoParamSpace's unchanged default of 2
// there is harmless for winding-number purposes (an extra exact-
// midpoint sample on an otherwise-straight boundary doesn't change a
// solid-angle integral or a point-in-polygon test), but it's a real bug
// for tessellation: that midpoint becomes a genuine extra boundary
// vertex, and two faces sharing this physical edge can each
// independently ear-clip around it differently (one treating it as a
// real corner, another's clipping order collapsing straight past it),
// producing mismatched, non-watertight boundaries between them.
// CONFIRMED against box.step: this produced 9 unmatched mesh edges, all
// traced to exactly this -- an edge like (0,0,0)->(10,0,0) sampled as
// [(0,0,0),(5,0,0),(10,0,0)] by evalAt(0), evalAt(5), evalAt(10) with
// the old samples=2. A line needs exactly its two true endpoints.
// Circles and ellipses get 64 samples per full turn, in proportion to
// the arc's angle: a fixed 64 put HingedTable.step's 0.027 rad (54 um)
// arc at 0.85 um spacing, its triangles were zero-area slivers and the
// cylinder beside it came out open.
thread_local int g_turnSamples = 64;   // set by tessellateSolid
// A chord cutting a surface by less than this is no error whatever the surface does (5e-5 of the solid's
// size: a twentieth of a pixel across a 400 pixel view): the smooth look of a curved face comes from its
// normals turning by no more than a turn's share between neighbours, but a fillet two millimetres wide on
// a hundred millimetre part needs no 25 micrometre triangles for it.  Set by tessellateSolid.
thread_local double g_tolAbs = 0;

int sampleCountFor(const Curve& curve, double t0, double t1)
{
    if (curve.kind == CurveKind::Circle || curve.kind == CurveKind::Ellipse) {
        return std::max(1, int(std::ceil(g_turnSamples * std::abs(t1 - t0) / (2 * M_PI) - 1e-9)));
    }
    if (curve.kind == CurveKind::BSpline) return std::max(4, g_turnSamples * 3 / 4);
    return 1;
}

// The foot of a point on a free-form patch: its (u, v), of the feet that are
// nearest the point the one nearest `prev` (a point on the seam of a patch that
// closes on itself is at both ends of the domain: continuity decides which,
// unwrapped by whole periods); without `prev`, the nearest.  `dist` is how far
// the point is from the patch.
Eigen::Vector2d footNear(const BSplinePatch& patch, const Vec3& p, const Eigen::Vector2d* prev, double& dist)
{
    static thread_local std::vector<BSplinePatch::Foot> feet;
    const double du = patch.u1 - patch.u0, dv = patch.v1 - patch.v0;
    const double tol = 1e-5 * std::max((patch.hi - patch.lo).norm(), 1e-12);
    patch.feet(p, prev, feet);
    if (feet.empty()) {
        dist = 1e300;
        return prev ? *prev : Eigen::Vector2d(patch.u0, patch.v0);
    }
    const double best = feet.front().dist;
    dist = best;
    if (!prev) return Eigen::Vector2d(feet.front().u, feet.front().v);
    Eigen::Vector2d pick(feet.front().u, feet.front().v);
    double bestCost = 1e300;
    for (const auto& f : feet) {
        if (f.dist > best + tol) break;     // (nearest first)
        double u = f.u, v = f.v;
        if (patch.closedU) u += du * std::round((prev->x() - u) / du);
        if (patch.closedV) v += dv * std::round((prev->y() - v) / dv);
        const double cost = std::hypot((u - prev->x()) / du, (v - prev->y()) / dv);
        if (cost < bestCost) { bestCost = cost; pick = Eigen::Vector2d(u, v); }
    }
    return pick;
}

// The points of a B-spline (or ellipse) edge curve from t0 to t1, as many as it takes: a
// segment is split at its parameter middle where the curve is off the
// segment (at the middle or a quarter of the way along) by more than an arc
// that turns by one turn's share (2 pi over `turnSamples`) would be -- its
// sagitta -- so a long edge whose curvature is in one corner gets its points
// there.  (A fixed number of points a curve, what this was, put a chord of
// 15 mm round a bend of radius 7 mm at the end of a designed panel's 250 mm
// edge: the outline of the face was off the face by millimetres, and the
// interior of the face had to be much finer than its own outline.)
void sampleBSplineEdge(const Curve& curve, double t0, double t1, std::vector<double>& ts, std::vector<Vec3>& out)
{
    const double angle = 2 * M_PI / double(g_turnSamples);
    constexpr int kFirst = 8;                   // (so an S does not pass for a straight line)
    std::vector<Vec3> first;
    double total = 0;
    for (int i = 0; i <= kFirst; i++) {
        first.push_back(curve.evalAt(t0 + (t1 - t0) * double(i) / kFirst));
        if (i) total += (first[size_t(i)] - first[size_t(i - 1)]).norm();
    }
    const double smallest = 1e-4 * total;       // (a corner of the curve splits forever: not shorter than this)
    ts.assign(1, t0);
    out.assign(1, first[0]);
    std::function<void(double, const Vec3&, double, const Vec3&, int)> cut =
        [&](double ta, const Vec3& pa, double tb, const Vec3& pb, int depth) {
            const Vec3 ab = pb - pa;
            const double len = ab.norm();
            bool split = false;
            if (depth < 16 && len > smallest && out.size() < 20000) {
                const double allowed = std::max(angle / 8.0 * len, g_tolAbs);
                auto gap = [&](const Vec3& q) {
                    const double along = std::max(0.0, std::min(1.0, (q - pa).dot(ab) / (len * len)));
                    return (q - (pa + along * ab)).norm();
                };
                const Vec3 pm = curve.evalAt(0.5 * (ta + tb));
                split = gap(pm) > allowed;
                for (double f : {0.25, 0.75}) {
                    if (!split) split = gap(curve.evalAt(ta + f * (tb - ta))) > allowed;
                }
                if (split) {
                    cut(ta, pa, 0.5 * (ta + tb), pm, depth + 1);
                    cut(0.5 * (ta + tb), pm, tb, pb, depth + 1);
                    return;
                }
            }
            ts.push_back(tb);
            out.push_back(pb);
        };
    for (int i = 0; i < kFirst; i++) {
        cut(t0 + (t1 - t0) * double(i) / kFirst, first[size_t(i)], t0 + (t1 - t0) * double(i + 1) / kFirst,
            first[size_t(i + 1)], 0);
    }
}

// The edges of one solid -> the free-form patches of the faces that share each: what those faces need of
// the edge's points, see refineEdgeForFaces.
using EdgePatches = std::map<int, std::vector<const BSplinePatch*>>;

// How wide the triangles of a free-form face come out where its surface turns fastest: the shorter of the
// two lengths over which it turns by one turn's share (the spacing of the mesh across a tube, along a
// fold).  An outline edge longer than this has a chord that cuts the surface by more than the width of
// the triangles next to it, and those then roll across the surface -- see refineEdgeForFaces.
thread_local std::map<const BSplinePatch*, double> g_patchWidth;

double patchWidth(const BSplinePatch& patch)
{
    auto it = g_patchWidth.find(&patch);
    if (it != g_patchWidth.end()) return it->second;
    double ru = 1, rv = 1;
    patch.turnRates(ru, rv);
    const double angle = 2 * M_PI / double(g_turnSamples);
    const double wu = angle / std::max(ru, 1e-9) * patch.speedU, wv = angle / std::max(rv, 1e-9) * patch.speedV;
    return g_patchWidth[&patch] = std::min(wu, wv);
}

// More points along an edge for the free-form faces that share it.  The face's mesh is
// triangulated between its outline points, in (u, v), and then refined inside: for that to
// work the outline has to follow the face, not only the edge curve -- between two neighbouring
// points the face must not turn by more than one turn's share (the normals at the two points),
// and the face's own (u, v) middle between them (what the triangulation sees as the straight
// line between the points) must land on the chord in space: where the edge runs across a
// panel that bends away from it (the 250 x 300 mm panel of Keukencombinatie: a cut edge sampled
// at 37 mm, the surface 0.25 mm off its chord), the triangles along it are slivers of every
// height and the refinement runs away.  A segment is split at its middle until both hold (or it
// is short: a crease of the surface turns however short the segment).
void refineEdgeForFaces(const Curve& curve, std::vector<double>& ts, std::vector<Vec3>& pts,
                        const std::vector<const BSplinePatch*>& patches)
{
    if (patches.empty() || pts.size() < 2) return;
    const double angle = 2 * M_PI / double(g_turnSamples);
    double total = 0;
    for (size_t i = 1; i < pts.size(); i++) total += (pts[i] - pts[i - 1]).norm();
    if (!(total > 0)) return;
    const double smallest = 1e-3 * total;
    struct End { Eigen::Vector2d uv; Vec3 n; double miss; bool ok; };
    auto probe = [&](const BSplinePatch& patch, const Vec3& p, const Eigen::Vector2d* near) {
        End e;
        double dist = 0;
        e.uv = footNear(patch, p, near, dist);
        e.ok = dist <= 1e-3 * std::max(total, (patch.hi - patch.lo).norm());
        e.miss = e.ok ? dist : 0.0;
        e.n = patch.normalWrapped(e.uv.x(), e.uv.y());
        return e;
    };
    std::vector<double> outT{ts[0]};
    std::vector<Vec3> outP{pts[0]};
    std::vector<End> first(patches.size());
    for (size_t k = 0; k < patches.size(); k++) first[k] = probe(*patches[k], pts[0], nullptr);
    std::function<void(double, const Vec3&, const std::vector<End>&, double, const Vec3&, const std::vector<End>&, int)> cut =
        [&](double ta, const Vec3& pa, const std::vector<End>& ea, double tb, const Vec3& pb, const std::vector<End>& eb, int depth) {
            const Vec3 ab = pb - pa;
            const double len = ab.norm();
            if (depth < 14 && len > smallest && outP.size() < 20000) {
                const double tm = 0.5 * (ta + tb);
                const Vec3 pm = curve.evalAt(tm);
                std::vector<End> em(patches.size());
                bool split = false;
                for (size_t k = 0; k < patches.size() && !split; k++) {
                    if (!ea[k].ok || !eb[k].ok) continue;
                    const BSplinePatch& patch = *patches[k];
                    const double allowed = std::max(std::min(angle / 8.0 * len, 0.25 * patchWidth(patch)), g_tolAbs) +
                                           2.0 * std::max(ea[k].miss, eb[k].miss) + 1e-9;
                    // the face's (u, v) middle against the chord
                    Vec3 on;
                    const Eigen::Vector2d mid = 0.5 * (ea[k].uv + eb[k].uv);
                    patch.evalWrapped(mid.x(), mid.y(), on);
                    const double along = std::max(0.0, std::min(1.0, (on - pa).dot(ab) / (len * len)));
                    const double gapImage = (on - (pa + along * ab)).norm();
                    if (gapImage > allowed) split = true;
                    // the turn of the face between the ends
                    if (!split && gapImage > g_tolAbs &&
                        std::acos(std::max(-1.0, std::min(1.0, ea[k].n.dot(eb[k].n)))) > angle) split = true;
                }
                if (split) {
                    for (size_t k = 0; k < patches.size(); k++) em[k] = probe(*patches[k], pm, ea[k].ok ? &ea[k].uv : nullptr);
                    cut(ta, pa, ea, tm, pm, em, depth + 1);
                    cut(tm, pm, em, tb, pb, eb, depth + 1);
                    return;
                }
            }
            outT.push_back(tb);
            outP.push_back(pb);
        };
    std::vector<End> prev = first;
    for (size_t i = 1; i < pts.size(); i++) {
        std::vector<End> now(patches.size());
        for (size_t k = 0; k < patches.size(); k++) now[k] = probe(*patches[k], pts[i], prev[k].ok ? &prev[k].uv : nullptr);
        cut(ts[i - 1], pts[i - 1], prev, ts[i], pts[i], now, 0);
        prev = now;
    }
    ts = std::move(outT);
    pts = std::move(outP);
}

// Puts the ends of an edge's points on the edge's VERTEX_POINTs (shared bit-identically by the faces round
// it).  Where the curve runs a little PAST a vertex -- a spline whose parameter range is not exactly the
// edge's: 1.3 mm of the top circle of Keukencombinatie's tube 1811108, in its mirror image only, lay beyond
// the end vertex -- replacing the last sample by the vertex left a spur, the outline going out along the
// circle and back, which the face's triangulation could not mesh (its refinement was thrown away, and the
// tube came out as a fan of slivers).  The samples beyond the vertex are dropped instead.
void pinEnds(std::vector<Vec3>& pts, const Vec3& pStart, const Vec3& pEnd)
{
    auto fixBack = [](std::vector<Vec3>& p, const Vec3& v) {
        const int m = int(p.size());
        if (m < 3) { p.back() = v; return; }
        int bestJ = -1;
        double bestT = 0, bestD = 1e300, bestLen = 0;
        for (int j = m - 2; j >= std::max(0, m - 4); j--) {        // (the last three segments)
            const Vec3 a = p[size_t(j)], d = p[size_t(j + 1)] - a;
            const double len2 = d.squaredNorm();
            if (!(len2 > 0)) continue;
            const double t = std::max(0.0, std::min(1.0, (v - a).dot(d) / len2));
            const double dist = (v - (a + t * d)).norm();
            if (dist < bestD) { bestD = dist; bestJ = j; bestT = t; bestLen = std::sqrt(len2); }
        }
        if (bestJ >= 0 && bestJ < m - 2 && bestD <= 0.05 * bestLen) {
            p.resize(size_t(bestJ + 1));
            if (bestT > 0.02) p.push_back(v); else p.back() = v;
        } else {
            p.back() = v;
        }
    };
    if (pts.empty()) return;
    std::reverse(pts.begin(), pts.end());
    fixBack(pts, pStart);
    std::reverse(pts.begin(), pts.end());
    fixBack(pts, pEnd);
}

// Returns this EdgeSpan's boundary points in ITS OWN t0->t1 direction,
// building and caching the canonical tessellation on first use.
std::atomic<int> g_tessCacheHits{0};
std::atomic<int> g_tessCacheMisses{0};
std::atomic<int> g_tessCacheIdNeg1{0};
std::atomic<int> g_tessFailedFaces{0};   // faces Manifold could not triangulate

const std::vector<Vec3>& cachedEdgePoints(const EdgeSpan& span,
                                           std::map<int, EdgeCacheEntry>& cache,
                                           std::vector<Vec3>& scratchReversed,
                                           const EdgePatches& edgePatches)
{
    if (span.edgeCurveId < 0) g_tessCacheIdNeg1++;
    auto it = cache.find(span.edgeCurveId);
    if (it == cache.end()) g_tessCacheMisses++; else g_tessCacheHits++;
    if (it == cache.end()) {
        EdgeCacheEntry entry;
        entry.t0 = span.t0;
        entry.t1 = span.t1;
        std::vector<double> ts;
        // (an ellipse is as free-form as a spline to a uniform step: a quarter of a 250 x 36 mm one, sampled
        // evenly in its parameter, had chords five times too long at the end where it bends)
        if (span.curve.kind == CurveKind::BSpline || span.curve.kind == CurveKind::Ellipse) {
            sampleBSplineEdge(span.curve, span.t0, span.t1, ts, entry.points);
        } else {
            int samples = sampleCountFor(span.curve, span.t0, span.t1);
            entry.points.reserve(samples + 1);
            for (int i = 0; i <= samples; i++) {
                double t = span.t0 + (span.t1 - span.t0) * i / samples;
                ts.push_back(t);
                entry.points.push_back(span.curve.evalAt(t));
            }
        }
        // A B-spline edge that is really a straight line (Bracket.step
        // stores every edge as a B-spline) needs just its two ends
        if (span.curve.kind == CurveKind::BSpline && entry.points.size() > 2) {
            const Vec3 p0 = entry.points.front(), d = entry.points.back() - p0;
            const double len = d.norm();
            bool straight = len > 0;
            for (const Vec3& q : entry.points) {
                straight = straight && (q - p0).cross(d).norm() <= 1e-7 * len * len;
            }
            if (straight) {
                entry.points = {entry.points.front(), entry.points.back()};
                ts = {ts.front(), ts.back()};
            }
        }
        // the free-form faces on this edge want it finer where they bend
        auto shared = span.edgeCurveId >= 0 ? edgePatches.find(span.edgeCurveId) : edgePatches.end();
        if (shared != edgePatches.end()) refineEdgeForFaces(span.curve, ts, entry.points, shared->second);
        if (span.hasEnds) {   // the shared VERTEX_POINTs, bit-identical everywhere
            pinEnds(entry.points, span.pStart, span.pEnd);
        }
        it = cache.emplace(span.edgeCurveId, std::move(entry)).first;
    }
    const EdgeCacheEntry& entry = it->second;
    // The two faces sharing this edge each independently resolve their
    // own (t0,t1) via resolveOrientedEdge (step_face.cpp) -- for both
    // the explicit-trim and heuristic paths there, that resolution is a
    // pure function of the edge's own fixed curve/vertices, swapped
    // only by this particular use's orientation flag. So a second use
    // of the same edge always lands on either the SAME (t0,t1) pair the
    // cache entry was built from, or that pair with t0/t1 swapped --
    // never an independently-drifted value -- which is what makes this
    // direct comparison reliable instead of needing a tolerance-based
    // periodic-wrap-aware match.
    bool forward = std::abs(span.t0 - entry.t0) <= std::abs(span.t0 - entry.t1);
    if (forward) return entry.points;
    scratchReversed.assign(entry.points.rbegin(), entry.points.rend());
    return scratchReversed;
}

// Appends one loop's ordered 3D boundary points (deduplicating the
// point shared between consecutive edges) into `out3D`.
void collectLoop3D(const Loop& loop, std::map<int, EdgeCacheEntry>& cache,
                    std::vector<Vec3>& out3D, const EdgePatches& edgePatches)
{
    for (const EdgeSpan& span : loop.edges) {
        std::vector<Vec3> scratch;
        const std::vector<Vec3>& pts = cachedEdgePoints(span, cache, scratch, edgePatches);
        // Skip the first point of every edge after the first -- it's
        // the same 3D vertex as the previous edge's last point (shared
        // endpoint), and triangulatePolygon already has to defend
        // against near-duplicate points from this same source, so
        // avoiding them here keeps the polygon smaller and cleaner.
        size_t start = out3D.empty() ? 0 : 1;
        for (size_t i = start; i < pts.size(); i++) out3D.push_back(pts[i]);
    }
}

// Projects a sequence of 3D points into `surf`'s own (u,v) space,
// seeding each projection from the previous point for cheap B-spline
// continuity and unwrapping periodic surfaces' seam the same way
// sampleEdgeIntoParamSpace does (step_face.cpp) -- a loop's boundary
// must stay a single connected polygon in parameter space, not jump a
// full period at the seam.
void projectLoopToUV(const std::vector<Vec3>& pts3D, const Surface& surf,
                      std::vector<Eigen::Vector2d>& outUV)
{
    const bool wrapsU = surf.kind == SurfaceKind::Cylinder || surf.kind == SurfaceKind::Cone ||
                         surf.kind == SurfaceKind::Sphere || surf.kind == SurfaceKind::Torus;
    const bool wrapsV = surf.kind == SurfaceKind::Torus;
    std::optional<Eigen::Vector2d> seed;
    outUV.reserve(pts3D.size());
    for (const Vec3& p3 : pts3D) {
        Surface::Closest proj = seed ? surf.closestPointNear(p3, seed->x(), seed->y())
                                      : surf.closestPoint(p3);
        double u = proj.u, v = proj.v;
        if (seed) {
            if (wrapsU) while (u - seed->x() > M_PI) u -= 2 * M_PI;
            if (wrapsU) while (u - seed->x() < -M_PI) u += 2 * M_PI;
            if (wrapsV) while (v - seed->y() > M_PI) v -= 2 * M_PI;
            if (wrapsV) while (v - seed->y() < -M_PI) v += 2 * M_PI;
        }
        seed = Eigen::Vector2d(u, v);
        outUV.push_back(*seed);
    }
}

// The outline of a free-form (B-spline) face in its own (u, v).  Every point is
// projected onto the patch by BSplinePatch::feet -- damped Gauss-Newton from
// the previous point's (u, v) and from every place of the patch that is near
// the point -- and of the feet that are on the point (an outline lies on the
// surface), the one nearest the previous point's (u, v) is taken: a point on
// the seam of a patch that closes on itself is at both ends of the domain,
// and continuity decides which, unwrapped by whole periods so the outline is
// one connected polygon.  (A plain Newton from the previous point, what this
// was, threw the iteration back and forth across a patch whose parameter runs
// a thousand times faster at one end than at the other and left whole edges
// of a face at one corner of its domain: the polygon folded, and with it the
// face's triangulation.)
void projectLoopToUVPatch(const std::vector<Vec3>& pts3D, const BSplinePatch& patch,
                          std::vector<Eigen::Vector2d>& outUV)
{
    std::optional<Eigen::Vector2d> prev;
    outUV.reserve(pts3D.size());
    for (const Vec3& p3 : pts3D) {
        double dist = 0;
        const Eigen::Vector2d pick = footNear(patch, p3, prev ? &*prev : nullptr, dist);
        outUV.push_back(pick);
        prev = pick;
    }
}

// Global vertex dedup keyed by a quantized 3D position, so boundary
// points shared between adjacent faces (bit-identical, from the edge
// cache) collapse to one vertex index -- this is what makes the output
// mesh actually connected across face seams, not just visually
// coincident.
struct VertexDedup
{
    std::map<std::array<int64_t, 3>, int> index;
    std::vector<Vec3>* verts;

    static std::array<int64_t, 3> key(const Vec3& p)
    {
        // CONFIRMED (2026-09-23): loosening this to 1e5 (~10 micron
        // buckets) to test whether the hinge part's defects were
        // independently-computed "same" points landing a few microns
        // apart made almost no difference (707->704 unmatched edges) --
        // ruled out; the edge cache's hits==misses symmetry separately
        // confirmed shared boundaries really do get bit-identical
        // points. 1e7 (~100nm) keeps a little margin for incidental
        // floating-point noise without approaching the smallest real
        // feature here (0.3mm fillets).
        constexpr double kScale = 1e7;
        return {static_cast<int64_t>(std::llround(p.x() * kScale)),
                static_cast<int64_t>(std::llround(p.y() * kScale)),
                static_cast<int64_t>(std::llround(p.z() * kScale))};
    }

    int get(const Vec3& p)
    {
        auto k = key(p);
        auto it = index.find(k);
        if (it != index.end()) return it->second;
        int idx = static_cast<int>(verts->size());
        verts->push_back(p);
        index.emplace(k, idx);
        return idx;
    }
};

// Makes the faces' orientations agree across shared edges (the two
// triangles on an edge of a closed surface run it in opposite
// directions), then turns every closed piece outward by the sign of its
// volume. The per-face normal vote in tessellateSolid can put a whole face
// on the wrong side (Bracket.step, PT.stp, ArcRevolve.step: hundreds of
// edges running the same way twice); edge agreement can't. Face pairs
// are joined strongest first (union-find with parity), so one stray edge
// can't outvote a whole shared boundary. A piece that isn't closed has no
// meaningful volume and keeps the vote's side (area-weighted majority).
void orientFaces(TessMesh& mesh)
{
    int nFaces = 0;
    for (int f : mesh.triFaceIdx) nFaces = std::max(nFaces, f + 1);
    if (nFaces == 0) return;

    std::map<std::pair<int, int>, std::vector<std::pair<int, int>>> edges;  // -> (triangle, direction)
    for (size_t t = 0; t < mesh.tris.size(); t++) {
        const auto& tr = mesh.tris[t];
        for (int k = 0; k < 3; k++) {
            const int a = tr[k], b = tr[(k + 1) % 3];
            edges[{std::min(a, b), std::max(a, b)}].push_back({int(t), a < b ? 1 : -1});
        }
    }
    std::map<std::pair<int, int>, int> score;   // face pair -> (#opposite - #same) directions
    std::vector<int> open(nFaces, 0);
    for (const auto& e : edges) {
        if (e.second.size() == 1) open[mesh.triFaceIdx[e.second[0].first]]++;
        if (e.second.size() != 2) continue;
        const int fa = mesh.triFaceIdx[e.second[0].first], fb = mesh.triFaceIdx[e.second[1].first];
        if (fa == fb) continue;
        score[{std::min(fa, fb), std::max(fa, fb)}] += e.second[0].second != e.second[1].second ? 1 : -1;
    }
    std::vector<std::pair<std::pair<int, int>, int>> pairs(score.begin(), score.end());
    std::sort(pairs.begin(), pairs.end(),
              [](const auto& x, const auto& y) { return std::abs(x.second) > std::abs(y.second); });

    std::vector<int> parent(nFaces), parity(nFaces, 1);   // parity relative to the parent
    for (int f = 0; f < nFaces; f++) parent[f] = f;
    std::function<std::pair<int, int>(int)> find = [&](int f) -> std::pair<int, int> {
        if (parent[f] == f) return {f, 1};
        auto rp = find(parent[f]);
        parent[f] = rp.first;
        parity[f] *= rp.second;
        return {parent[f], parity[f]};
    };
    for (const auto& pr : pairs) {
        if (pr.second == 0) continue;
        const int rel = pr.second > 0 ? 1 : -1;   // +1: same orientation already
        auto a = find(pr.first.first), b = find(pr.first.second);
        if (a.first != b.first) {
            parent[b.first] = a.first;
            parity[b.first] = a.second * rel * b.second;
        }
    }

    std::vector<int> sign(nFaces);
    std::map<int, double> volume, area;
    std::map<int, int> openCount;
    for (int f = 0; f < nFaces; f++) {
        sign[f] = find(f).second;
        openCount[find(f).first] += open[f];
    }
    for (size_t t = 0; t < mesh.tris.size(); t++) {
        const int f = mesh.triFaceIdx[t];
        const int root = find(f).first;
        const Vec3& a = mesh.verts[mesh.tris[t][0]];
        const Vec3& b = mesh.verts[mesh.tris[t][1]];
        const Vec3& c = mesh.verts[mesh.tris[t][2]];
        volume[root] += sign[f] * a.dot(b.cross(c)) / 6.0;
        area[root] += sign[f] * 0.5 * (b - a).cross(c - a).norm();   // +: agrees with the vote
    }
    for (int f = 0; f < nFaces; f++) {
        const int root = find(f).first;
        const bool closed = openCount[root] == 0;
        if (closed ? volume[root] < 0 : area[root] < 0) sign[f] = -sign[f];
    }
    for (size_t t = 0; t < mesh.tris.size(); t++) {
        if (sign[mesh.triFaceIdx[t]] < 0) std::swap(mesh.tris[t][0], mesh.tris[t][2]);
    }
}

// Whether two triangles of a mesh run one edge the same way round: a face whose triangles overlap
// itself in (u, v) meshes with folds, and then keeps its outline triangulation.
bool runsTwice(const std::vector<Vec3>& pts, const std::vector<std::array<int, 3>>& tris)
{
    std::map<std::array<int64_t, 3>, int> id;
    std::vector<int> canon(pts.size());
    for (size_t i = 0; i < pts.size(); i++)
        canon[i] = id.emplace(VertexDedup::key(pts[i]), int(id.size())).first->second;
    std::unordered_set<uint64_t> directed;
    directed.reserve(tris.size() * 3);
    for (const auto& t : tris) {
        // (a triangle with two corners at one point -- a closed outline repeats its first point at the end --
        // is dropped when the face is emitted: its edges are no fold)
        const int c0 = canon[size_t(t[0])], c1 = canon[size_t(t[1])], c2 = canon[size_t(t[2])];
        if (c0 == c1 || c1 == c2 || c2 == c0) continue;
        for (int e = 0; e < 3; e++) {
            const int a = canon[size_t(t[size_t(e)])], b = canon[size_t(t[size_t((e + 1) % 3)])];
            if (!directed.insert((uint64_t(uint32_t(a)) << 32) | uint32_t(b)).second) return true;
        }
    }
    return false;
}

// Flips interior edges of a face's triangulation -- triangles counter-clockwise
// in (u, v) -- where that gives better triangles: the two triangles of a convex
// quad become the other two when the worse tilt of a triangle from the
// surface (the angle between its plane and the surface's normals at its
// corners) gets smaller, and, where that is about the same, when the smallest
// of the six angles in (u, v) scaled to turns of the surface grows (a Delaunay
// triangulation of the face as it asks to be meshed).  The outline's edges
// have one triangle and stay.  The outline's triangulation has long thin
// triangles across a strip-like face, and splitting their edges gave slivers
// crowded along the outline; flipped to their neighbours first, the triangles
// are the ones an eye would draw.  The tilt is what keeps a long thin
// triangle from rolling about its length across the surface: three corners on
// one line of a bent tube make a triangle whose plane is anywhere, and the
// Delaunay angles do not see it.
void flipToBetterShape(std::vector<std::array<int, 3>>& tris, const std::vector<Vec3>& pts,
                       const std::vector<Eigen::Vector2d>& uv, const std::unordered_set<uint64_t>& outlineEdges,
                       const std::function<Vec3(int)>& normalOf, double size, int maxPasses)
{
    auto dkey = [](int a, int b) { return (uint64_t(uint32_t(a)) << 32) | uint32_t(b); };
    auto tilt = [&](int a, int b, int c) {
        const Vec3 n = (pts[size_t(b)] - pts[size_t(a)]).cross(pts[size_t(c)] - pts[size_t(a)]);
        const double l = n.norm();
        if (!(l > 1e-14 * size * size)) return M_PI;
        double worst = 0;
        for (int v : {a, b, c}) {
            const Vec3 nv = normalOf(v);
            if (nv.norm() < 0.5) continue;
            worst = std::max(worst, std::acos(std::max(-1.0, std::min(1.0, n.dot(nv) / l))));
        }
        return worst;
    };
    // (the angles are those in (u, v) scaled to turns of the surface)
    auto minAngle = [&](int a, int b, int c) {
        const double ab = (uv[size_t(a)] - uv[size_t(b)]).norm();
        const double bc = (uv[size_t(b)] - uv[size_t(c)]).norm();
        const double ca = (uv[size_t(c)] - uv[size_t(a)]).norm();
        if (!(ab > 0 && bc > 0 && ca > 0)) return 0.0;
        auto corner = [](double x, double y, double opposite) {     // angle between sides x and y
            return std::acos(std::max(-1.0, std::min(1.0, (x * x + y * y - opposite * opposite) / (2 * x * y))));
        };
        return std::min({corner(ab, ca, bc), corner(ab, bc, ca), corner(bc, ca, ab)});
    };
    auto area2 = [&](int a, int b, int c) {
        const Eigen::Vector2d p = uv[size_t(b)] - uv[size_t(a)], q = uv[size_t(c)] - uv[size_t(a)];
        return p.x() * q.y() - p.y() * q.x();
    };
    for (int pass = 0; pass < maxPasses; pass++) {
        std::unordered_map<uint64_t, int> dir;          // directed edge -> its triangle
        dir.reserve(tris.size() * 3);
        for (size_t t = 0; t < tris.size(); t++)
            for (int e = 0; e < 3; e++) dir[dkey(tris[t][size_t(e)], tris[t][size_t((e + 1) % 3)])] = int(t);
        std::vector<char> touched(tris.size(), 0);
        int flips = 0;
        for (size_t t = 0; t < tris.size(); t++) {
            if (touched[t]) continue;
            for (int e = 0; e < 3 && !touched[t]; e++) {
                const int a = tris[t][size_t(e)], b = tris[t][size_t((e + 1) % 3)], c = tris[t][size_t((e + 2) % 3)];
                if (a > b) continue;                                   // each edge once
                if (outlineEdges.count(dkey(a, b))) continue;
                auto other = dir.find(dkey(b, a));
                if (other == dir.end()) continue;                      // the outline
                const size_t t2 = size_t(other->second);
                if (touched[t2] || t2 == t) continue;
                int d = -1;
                for (int k = 0; k < 3; k++) {
                    const int v = tris[t2][size_t(k)];
                    if (v != a && v != b) d = v;
                }
                if (d < 0 || d == c) continue;
                if (dir.count(dkey(c, d)) || dir.count(dkey(d, c))) continue;    // that edge is already there
                // both new triangles must be counter-clockwise in (u, v) and not a line: a point that lies on
                // the line of a neighbouring edge (one the refinement has just put in the middle of it) is no
                // corner of a convex quad, and flipping there would bring the long edge back
                auto proper = [&](int p, int q, int r) {
                    const double longest = std::max({(uv[size_t(p)] - uv[size_t(q)]).squaredNorm(),
                                                      (uv[size_t(q)] - uv[size_t(r)]).squaredNorm(),
                                                      (uv[size_t(r)] - uv[size_t(p)]).squaredNorm()});
                    return area2(p, q, r) > 1e-6 * longest;
                };
                if (!proper(a, d, c) || !proper(d, b, c)) continue;              // not a convex quad
                const double tiltBefore = std::max(tilt(a, b, c), tilt(b, a, d));
                const double tiltAfter = std::max(tilt(a, d, c), tilt(d, b, c));
                constexpr double kSame = 0.005;      // (a third of a degree)
                if (tiltAfter > tiltBefore + kSame) continue;
                if (!(tiltAfter < tiltBefore - kSame)) {
                    const double before = std::min(minAngle(a, b, c), minAngle(b, a, d));
                    const double after = std::min(minAngle(a, d, c), minAngle(d, b, c));
                    if (!(after > before + 1e-9)) continue;
                }
                tris[t] = {a, d, c};
                tris[t2] = {d, b, c};
                dir.erase(dkey(a, b));
                dir.erase(dkey(b, a));
                dir[dkey(b, c)] = int(t2);
                dir[dkey(a, d)] = int(t);
                dir[dkey(d, c)] = int(t);
                dir[dkey(c, d)] = int(t2);
                touched[t] = touched[t2] = 1;
                flips++;
            }
        }
        if (!flips) break;
    }
}

// Refines the triangulation of a free-form (B-spline) face inside its outline.
// An edge is split at its (u, v) middle, on the surface, where
//  - a straight edge would cut the surface: the surface's point at the middle (or
//    a quarter of the way along) is off the chord by more than the sagitta of an
//    arc that turns by `angle` (2 pi over turnSamples), or
//  - the surface turns between its ends by more than that (a twisting panel can
//    have a chord lying on it, its two triangles turning against each other all the
//    same);
// and a triangle whose plane is tilted from the surface's normals at its corners by
// more than three times `angle` has its longest edge split: a triangle as
// long as a tube and as wide as a fingernail, its corners on a bend of the tube,
// rolls about its length by as much as the bend's sagitta allows -- its edges are
// all within the sagitta rule and its plane stands across the surface (a fifth of
// the area of Keukencombinatie's tube, 6 % too much area).
// Every edge of the outline is left alone, the face next door shares it.  Triangles
// are flipped to the better shaped pair after every round.
//
// pts: the outline's points first (outlineCount of them), then the new ones;
// raw: each point's (u, v); the flips and the shape of the triangles are judged in
// the same scaled to turns of the surface, `scale`.
void refineFreeform(const BSplinePatch& patch, const Eigen::Vector2d& scale, size_t outlineCount, double noise,
                    const std::unordered_set<uint64_t>& outlineEdges, std::vector<Vec3>& pts, std::vector<Eigen::Vector2d>& raw,
                    std::vector<std::array<int, 3>>& tris, int turnSamples, size_t faceIdx)
{
    const double angle = 2.0 * M_PI / double(turnSamples);
    Vec3 lo = pts[0], hi = pts[0];
    for (size_t i = 0; i < outlineCount; i++) { lo = lo.cwiseMin(pts[i]); hi = hi.cwiseMax(pts[i]); }
    const double size = std::max((hi - lo).norm(), 1e-9);
    // (an edge shorter than this is not split for this reason: the surface may have a crease,
    // its two sides turning at once however short the edge)
    const double minForTurn = size / 1000.0, minForSagitta = size / 20000.0;
    const size_t cap = 120000;
    std::vector<Eigen::Vector2d> suv(raw.size());
    for (size_t i = 0; i < raw.size(); i++) suv[i] = Eigen::Vector2d(raw[i].x() * scale.x(), raw[i].y() * scale.y());

    std::vector<Vec3> nrm;          // the surface normal at each point, found when asked
    std::vector<char> have;
    auto normal = [&](int i) {
        if (size_t(i) >= nrm.size()) { nrm.resize(pts.size()); have.resize(pts.size(), 0); }
        if (!have[size_t(i)]) {
            const Vec3 n = patch.normalWrapped(raw[size_t(i)].x(), raw[size_t(i)].y());
            const double l = n.norm();
            nrm[size_t(i)] = l > 0 && std::isfinite(l) ? Vec3(n / l) : Vec3::Zero();
            have[size_t(i)] = 1;
        }
        return nrm[size_t(i)];
    };
    const std::function<Vec3(int)> normalOf = normal;
    flipToBetterShape(tris, pts, suv, outlineEdges, normalOf, size, 30);
    auto ekey = [](int a, int b) { return (uint64_t(uint32_t(std::min(a, b))) << 32) | uint32_t(std::max(a, b)); };
    int rounds = 0;
    const bool traceRound = std::getenv("FIELDES_TESS_TRACE") != nullptr;
    for (; rounds < 40; rounds++) {
        std::unordered_map<uint64_t, int> count;
        count.reserve(tris.size() * 2);
        for (const auto& t : tris)
            for (int e = 0; e < 3; e++) count[ekey(t[size_t(e)], t[size_t((e + 1) % 3)])]++;
        std::unordered_map<uint64_t, int> mid;      // split edge -> its new point
        bool any = false;
        int byReason[4] = {0, 0, 0, 0};

        // Splits the edge (a, b): by the edge's own rules, or (`forced`) whatever they say.
        auto trySplit = [&](int a, int b, bool forced) {
            if (a > b) std::swap(a, b);
            if (count[ekey(a, b)] != 2 || mid.count(ekey(a, b)) || pts.size() >= cap) return false;
            if (outlineEdges.count((uint64_t(uint32_t(a)) << 32) | uint32_t(b))) return false;      // (the outline: never)
            const double len = (pts[size_t(a)] - pts[size_t(b)]).norm();
            if (len <= (forced ? minForTurn : minForSagitta)) return false;
            // (the outline's points come from the edge curves, which lie on the surface only to the
            // file's tolerance -- `noise`: an edge between such a point and one on the surface is off
            // the surface by that however short, and below that there is nothing to see)
            const double allowed = std::max(angle / 8.0 * len, g_tolAbs) + 2.0 * noise + 1e-9;
            const Eigen::Vector2d m = 0.5 * (raw[size_t(a)] + raw[size_t(b)]);
            Vec3 on;
            patch.evalWrapped(m.x(), m.y(), on);
            // how far the surface is from the chord: the distance to the segment, not to its middle --
            // where the parameter runs faster on one side of the edge than the other (a join of two
            // pieces of the spline) the (u, v) middle is not the chord's middle, and a flat panel would
            // look curved
            const Vec3 ab = pts[size_t(b)] - pts[size_t(a)];
            auto gap = [&](const Vec3& q) {
                const double along = std::max(0.0, std::min(1.0, (q - pts[size_t(a)]).dot(ab) / (len * len)));
                return (q - (pts[size_t(a)] + along * ab)).norm();
            };
            const double off = gap(on);
            bool split = forced || off > allowed;
            int reason = forced ? 3 : (off > allowed ? 0 : -1);
            if (!split) {
                for (double f : {0.25, 0.75}) {
                    const Eigen::Vector2d q = raw[size_t(a)] + f * (raw[size_t(b)] - raw[size_t(a)]);
                    Vec3 at;
                    patch.evalWrapped(q.x(), q.y(), at);
                    if (gap(at) > allowed) { split = true; reason = 1; break; }
                }
            }
            if (!split && len > minForTurn) {
                const Vec3 na = normal(a), nb = normal(b);
                if (na.norm() > 0.5 && nb.norm() > 0.5)
                    split = off > g_tolAbs && std::acos(std::max(-1.0, std::min(1.0, na.dot(nb)))) > angle;
                if (split) reason = 2;
            }
            if (!split) return false;
            if (traceRound && rounds >= 14) {
                static thread_local int shown = 0;
                if (shown < 40) {
                    shown++;
                    std::fprintf(stderr, "[trace] round %d reason %d (0 sagitta, 1 quarter, 2 turn, 3 tilted) edge (%d, %d) uv (%.5f %.5f)-(%.5f %.5f) len %.4g off %.4g allowed %.4g\n",
                                 rounds, reason, a, b, raw[size_t(a)].x(), raw[size_t(a)].y(), raw[size_t(b)].x(), raw[size_t(b)].y(), len, off, allowed);
                }
            }
            // (where the panel folds over itself the (u, v) middle can be far from the chord -- that is the
            // surface, not an error; a middle further off than several edge lengths is another part of
            // it, and the edge is left)
            if (off > 4.0 * len + 1e-9) return false;
            if (traceRound) {
                static thread_local int shownEdge = 0;
                double uMin = 1e300, uMax = -1e300;
                for (size_t i = 0; i < outlineCount; i++) { uMin = std::min(uMin, raw[i].x()); uMax = std::max(uMax, raw[i].x()); }
                if ((std::abs(m.x() - uMin) < 1e-7 || std::abs(m.x() - uMax) < 1e-7) && shownEdge < 12) {
                    shownEdge++;
                    std::fprintf(stderr, "[trace-seam] round %d reason %d splits (%d, %d) uv (%.9g %.9g)-(%.9g %.9g), a outline %d, b outline %d, len %.4g; new point on the seam line\n",
                                 rounds, reason, a, b, raw[size_t(a)].x(), raw[size_t(a)].y(), raw[size_t(b)].x(), raw[size_t(b)].y(),
                                 int(size_t(a) < outlineCount), int(size_t(b) < outlineCount), len);
                }
            }
            mid[ekey(a, b)] = int(pts.size());
            pts.push_back(on);
            raw.push_back(m);
            suv.push_back(Eigen::Vector2d(m.x() * scale.x(), m.y() * scale.y()));
            any = true;
            if (reason >= 0 && reason < 4) byReason[reason]++;
            return true;
        };

        for (const auto& t : tris)
            for (int e = 0; e < 3; e++) trySplit(t[size_t(e)], t[size_t((e + 1) % 3)], false);

        // the triangles whose planes stand across the surface
        for (const auto& t : tris) {
            const Vec3 n = (pts[size_t(t[1])] - pts[size_t(t[0])]).cross(pts[size_t(t[2])] - pts[size_t(t[0])]);
            const double l = n.norm();
            if (!(l > 1e-12 * size * size)) continue;
            // (a triangle thinner than the outline's noise -- a layer halved towards the outline edge again and
            // again -- has its plane set by the noise, and splitting it makes thinner ones)
            const double longest = std::max({(pts[size_t(t[0])] - pts[size_t(t[1])]).norm(),
                                              (pts[size_t(t[1])] - pts[size_t(t[2])]).norm(),
                                              (pts[size_t(t[2])] - pts[size_t(t[0])]).norm()});
            if (l / longest < std::max(40.0 * noise, 1e-4 * size)) continue;
            double worst = 0;
            for (int k = 0; k < 3; k++) {
                const Vec3 nv = normal(t[size_t(k)]);
                if (nv.norm() < 0.5) continue;
                worst = std::max(worst, std::acos(std::max(-1.0, std::min(1.0, n.dot(nv) / l))));
            }
            if (worst <= 3.0 * angle) continue;
            if (l / longest * std::sin(worst) <= g_tolAbs) continue;      // (off the surface by less than nothing to see)
            // its longest edge whose split makes the two halves sit better on the surface (a triangle along a
            // long outline edge, its third corner a hair away from the edge, stands across the surface however
            // it is split: splitting it only makes thinner ones)
            int order[3] = {0, 1, 2};
            auto edgeLen = [&](int e) { return (pts[size_t(t[size_t(e)])] - pts[size_t(t[size_t((e + 1) % 3)])]).norm(); };
            std::sort(order, order + 3, [&](int x, int y) { return edgeLen(x) > edgeLen(y); });
            for (int e : order) {
                const int a = t[size_t(e)], b = t[size_t((e + 1) % 3)], c = t[size_t((e + 2) % 3)];
                if (mid.count(ekey(a, b)) || outlineEdges.count(ekey(a, b)) || count[ekey(a, b)] != 2) continue;
                const Eigen::Vector2d m = 0.5 * (raw[size_t(a)] + raw[size_t(b)]);
                Vec3 on;
                patch.evalWrapped(m.x(), m.y(), on);
                const Vec3 nm = patch.normalWrapped(m.x(), m.y());
                auto tiltOf = [&](const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& n0, const Vec3& n1, const Vec3& n2) {
                    const Vec3 nn = (p1 - p0).cross(p2 - p0);
                    const double ll = nn.norm();
                    if (!(ll > 0)) return M_PI;
                    double w = 0;
                    for (const Vec3* q : {&n0, &n1, &n2})
                        if (q->norm() > 0.5) w = std::max(w, std::acos(std::max(-1.0, std::min(1.0, nn.dot(*q) / ll))));
                    return w;
                };
                const double halves = std::max(tiltOf(pts[size_t(a)], on, pts[size_t(c)], normal(a), nm, normal(c)),
                                               tiltOf(on, pts[size_t(b)], pts[size_t(c)], nm, normal(b), normal(c)));
                if (halves < worst - 0.02 && trySplit(a, b, true)) break;
            }
        }

        if (traceRound)
            std::fprintf(stderr, "[round %d] splits: sagitta %d, quarter %d, turn %d, tilt %d; %zu points, %zu triangles; scale (%.4g %.4g), noise %.3g, size %.4g\n",
                         rounds, byReason[0], byReason[1], byReason[2], byReason[3], pts.size(), tris.size(), scale.x(), scale.y(), noise, size);
        if (!any) break;
        std::vector<std::array<int, 3>> next;
        next.reserve(tris.size() * 2);
        for (const auto& t : tris) {
            int m[3];
            int splits = 0;
            for (int e = 0; e < 3; e++) {
                const int a = t[size_t(e)], b = t[size_t((e + 1) % 3)];
                auto it = mid.find(ekey(a, b));
                m[e] = it == mid.end() ? -1 : it->second;
                splits += m[e] >= 0;
            }
            if (splits == 0) { next.push_back(t); continue; }
            // rotate so edge 0 (t0 -> t1) is split, and with two splits, edge 1 as well
            int r = 0;
            if (splits == 1) { while (m[r] < 0) r++; }
            else if (splits == 2) { while (m[r] < 0 || m[(r + 1) % 3] < 0) r++; }
            const int a = t[size_t(r)], b = t[size_t((r + 1) % 3)], c = t[size_t((r + 2) % 3)];
            const int mab = m[r], mbc = m[(r + 1) % 3], mca = m[(r + 2) % 3];
            if (splits == 1) {
                next.push_back({a, mab, c});
                next.push_back({mab, b, c});
            } else if (splits == 2) {
                next.push_back({mab, b, mbc});
                next.push_back({a, mab, mbc});
                next.push_back({a, mbc, c});
            } else {
                next.push_back({a, mab, mca});
                next.push_back({mab, b, mbc});
                next.push_back({mca, mbc, c});
                next.push_back({mab, mbc, mca});
            }
        }
        tris = std::move(next);
        flipToBetterShape(tris, pts, suv, outlineEdges, normalOf, size, 30);
    }
    if (std::getenv("FIELDES_TESS_DEBUG"))
        std::fprintf(stderr, "[tess] free-form face %zu: %zu outline points, refined in %d rounds to %zu points, %zu triangles\n",
                     faceIdx, outlineCount, rounds, pts.size(), tris.size());
}

}  // namespace

TessMesh tessellateSolid(const Solid& solid, int turnSamples, int threads)
{
    g_turnSamples = std::max(8, turnSamples);
    g_patchWidth.clear();
    {
        Vec3 lo = Vec3::Constant(1e300), hi = Vec3::Constant(-1e300);
        for (const Face& f : solid.faces) {
            if (f.loops.empty()) continue;
            lo = lo.cwiseMin(f.boundMin);
            hi = hi.cwiseMax(f.boundMax);
        }
        g_tolAbs = (hi - lo).allFinite() && hi.x() > lo.x() ? 5e-5 * (hi - lo).norm() : 0.0;
    }
    TessMesh mesh;
    VertexDedup dedup;
    dedup.verts = &mesh.verts;
    std::map<int, EdgeCacheEntry> edgeCache;
    // which free-form faces share each edge: the edge's points are chosen for them too
    EdgePatches edgePatches;
    for (const Face& face : solid.faces) {
        if (face.surface.kind != SurfaceKind::BSpline || !face.patch || !face.patch->ok()) continue;
        for (const Loop& loop : face.loops)
            for (const EdgeSpan& span : loop.edges) {
                if (span.edgeCurveId < 0) continue;
                auto& list = edgePatches[span.edgeCurveId];
                if (std::find(list.begin(), list.end(), face.patch.get()) == list.end()) list.push_back(face.patch.get());
            }
    }

    // Emits triangles (indices into pts), the whole face oriented once
    // by an area-weighted vote -- see below for why not per triangle
    auto emitFace = [&](const Face& face, size_t faceIdx, const BSplinePatch* patch,
                    const std::vector<Vec3>& pts, const std::vector<std::array<int, 3>>& tris) {
        double vote = 0.0;
        // (a refined free-form face has thousands of triangles and finding the surface point
        // nearest one costs a search: the vote is taken on an even sample of at most 96)
        const size_t every = std::max<size_t>(1, tris.size() / 96);
        for (size_t q = 0; q < tris.size(); q += every) {
            const auto& tri = tris[q];
            Vec3 a = pts[tri[0]], b = pts[tri[1]], c = pts[tri[2]];
            Vec3 geomNormal = (b - a).cross(c - a);
            if (patch) {
                const BSplinePatch::Foot foot = patch->closest((a + b + c) / 3.0);
                vote += geomNormal.dot(patch->normal(foot.u, foot.v));
            } else {
                Surface::Closest c3 = face.surface.closestPoint((a + b + c) / 3.0);
                vote += geomNormal.dot(face.surface.normalAt(c3.u, c3.v));
            }
        }
        const bool flip = face.sameSense ? (vote < 0) : (vote > 0);
        for (auto& tri : tris) {
            // (zero-area triangles are kept: dropping them opens the
            // mesh -- only ones collapsed onto a repeated vertex go)
            Vec3 a = pts[tri[0]], b = pts[tri[1]], c = pts[tri[2]];
            if (flip) std::swap(a, c);
            int ia = dedup.get(a), ib = dedup.get(b), ic = dedup.get(c);
            if (ia == ib || ib == ic || ia == ic) continue;
            mesh.tris.push_back({ia, ib, ic});
            mesh.triFaceIdx.push_back(int(faceIdx));
            mesh.triIsBSpline.push_back(face.surface.kind == SurfaceKind::BSpline);
        }
    };

    // A free-form face is triangulated between its outline's points here and refined inside afterwards,
    // all of them together and in parallel: each depends on nothing but its own outline, and refining
    // takes a second or more a face (a gear's flank).  What is kept of one until then:
    struct Deferred
    {
        size_t faceIdx = 0;
        const BSplinePatch* patch = nullptr;
        Eigen::Vector2d scale = Eigen::Vector2d::Ones();
        size_t pts0 = 0;                    // the outline's points
        double noise = 0;
        std::unordered_set<uint64_t> outlineEdges;
        std::vector<Vec3> pts;
        std::vector<Eigen::Vector2d> raw;
        std::vector<std::array<int, 3>> tris, tris0;
        bool folds = false;
    };
    std::vector<Deferred> deferred;

    for (size_t faceIdx = 0; faceIdx < solid.faces.size(); faceIdx++) {
        const Face& face = solid.faces[faceIdx];
        if (face.loops.empty()) continue;
        // (a free-form face's own patch: fast evaluation with derivatives, and a robust projection)
        const BSplinePatch* patch = (face.surface.kind == SurfaceKind::BSpline && face.patch && face.patch->ok())
                                        ? face.patch.get() : nullptr;

        // Separate outer from holes; collect each loop's 3D boundary
        // (shared-edge-cached) and its projection into this face's own
        // UV space.
        std::vector<Vec3> outer3D;
        std::vector<Eigen::Vector2d> outerUV;
        std::vector<std::pair<std::vector<Vec3>, std::vector<Eigen::Vector2d>>> holes;
        bool haveOuter = false;
        for (const Loop& loop : face.loops) {
            std::vector<Vec3> pts3D;
            collectLoop3D(loop, edgeCache, pts3D, edgePatches);
            if (pts3D.size() < 3) continue;
            std::vector<Eigen::Vector2d> ptsUV;
            if (patch) projectLoopToUVPatch(pts3D, *patch, ptsUV);
            else projectLoopToUV(pts3D, face.surface, ptsUV);
            if (std::getenv("FIELDES_TESS_DEBUG") && face.surface.kind == SurfaceKind::BSpline) {
                // how well the outline maps into (u, v): the surface at each (u, v) against the 3D point,
                // the largest jump between neighbours (in shares of the domain), and whether the polygon crosses itself
                const auto& s = face.surface;
                const double du = std::max(1e-12, s.knotsU[s.knotsU.size() - 1 - size_t(s.degreeU)] - s.knotsU[size_t(s.degreeU)]);
                const double dv = std::max(1e-12, s.knotsV[s.knotsV.size() - 1 - size_t(s.degreeV)] - s.knotsV[size_t(s.degreeV)]);
                double worst = 0, jump = 0;
                for (size_t i = 0; i < ptsUV.size(); i++) {
                    Vec3 at;
                    if (patch) patch->evalWrapped(ptsUV[i].x(), ptsUV[i].y(), at); else at = s.evalParam(ptsUV[i].x(), ptsUV[i].y());
                    worst = std::max(worst, (at - pts3D[i]).norm());
                    if (i) jump = std::max(jump, std::max(std::abs(ptsUV[i].x() - ptsUV[i - 1].x()) / du,
                                                          std::abs(ptsUV[i].y() - ptsUV[i - 1].y()) / dv));
                }
                int crossings = 0;
                const size_t n = ptsUV.size();
                auto cross2 = [](const Eigen::Vector2d& a, const Eigen::Vector2d& b, const Eigen::Vector2d& c) {
                    return (b.x() - a.x()) * (c.y() - a.y()) - (b.y() - a.y()) * (c.x() - a.x());
                };
                if (n < 700) {
                    for (size_t i = 0; i + 1 < n; i++)
                        for (size_t j = i + 2; j + 1 < n; j++) {
                            const auto &a = ptsUV[i], &b = ptsUV[i + 1], &c = ptsUV[j], &d = ptsUV[j + 1];
                            if (cross2(a, b, c) * cross2(a, b, d) < 0 && cross2(c, d, a) * cross2(c, d, b) < 0) crossings++;
                        }
                }
                std::fprintf(stderr, "[tess] face %zu %s loop of %zu points: (u, v) map misses the 3D point by up to %.3g, "
                             "largest jump %.3g of the domain, %d self-crossings\n", faceIdx, loop.isOuter ? "outer" : "inner",
                             pts3D.size(), worst, jump, crossings);
            }
            if (const char* dumpDir = std::getenv("FIELDES_TESS_DUMP")) {
                if (face.surface.kind == SurfaceKind::BSpline) {
                    const auto& s = face.surface;
                    std::FILE* f = std::fopen((std::string(dumpDir) + "/face_" + std::to_string(faceIdx) +
                                               (loop.isOuter ? "_outer_" : "_inner_") + std::to_string(int(&loop - &face.loops[0])) + ".txt").c_str(), "w");
                    if (f) {
                        std::fprintf(f, "degree %d %d\nctrl %d %d\nrational %d\nsameSense %d\n", s.degreeU, s.degreeV, s.nCtrlU, s.nCtrlV,
                                     s.weights.empty() ? 0 : 1, face.sameSense ? 1 : 0);
                        std::fprintf(f, "knotsU"); for (double k : s.knotsU) std::fprintf(f, " %.17g", k); std::fprintf(f, "\n");
                        std::fprintf(f, "knotsV"); for (double k : s.knotsV) std::fprintf(f, " %.17g", k); std::fprintf(f, "\n");
                        std::fprintf(f, "weights"); for (double k : s.weights) std::fprintf(f, " %.17g", k); std::fprintf(f, "\n");
                        for (const Vec3& c : s.ctrl) std::fprintf(f, "c %.17g %.17g %.17g\n", c.x(), c.y(), c.z());
                        for (size_t i = 0; i < pts3D.size(); i++)
                            std::fprintf(f, "p %.17g %.17g %.17g %.17g %.17g\n", pts3D[i].x(), pts3D[i].y(), pts3D[i].z(), ptsUV[i].x(), ptsUV[i].y());
                        std::fclose(f);
                    }
                }
            }
            if (loop.isOuter && !haveOuter) {
                outer3D = std::move(pts3D);
                outerUV = std::move(ptsUV);
                haveOuter = true;
            } else if (loop.isOuter) {
                // Multiple outer loops on one face isn't expected from
                // a valid ADVANCED_FACE, but if it happens, treat any
                // further ones as holes rather than silently dropping
                // them -- still better coverage than nothing.
                holes.emplace_back(std::move(pts3D), std::move(ptsUV));
            } else {
                holes.emplace_back(std::move(pts3D), std::move(ptsUV));
            }
        }
        if (!haveOuter) continue;

        auto emit = [&](const std::vector<Vec3>& pts, const std::vector<std::array<int, 3>>& tris) {
            emitFace(face, faceIdx, patch, pts, tris);
        };

        // The holes of a face round a periodic surface: each loop is unwrapped on its own, so a window cut in
        // the half of a cylinder (the outline runs u = pi to 2 pi) can come out a turn away at u = -2.4 to
        // -0.8, outside the outline, and the face was triangulated as if it had no hole (a cross hole of an
        // engine block came out 76 % too big in area).  The holes are moved by whole turns to where the
        // outline is.
        if (!holes.empty()) {
            const SurfaceKind k = face.surface.kind;
            // (the period of each parameter: a turn for the analytic surfaces, the knot span of a free-form patch that
            // closes on itself -- a big panel of Keukencombinatie whose two round holes were a period away from its
            // outline and so were not cut out: 5 % too much area and volume)
            double periodU = 0, periodV = 0;
            if (k == SurfaceKind::Cylinder || k == SurfaceKind::Cone || k == SurfaceKind::Sphere || k == SurfaceKind::Torus)
                periodU = 2 * M_PI;
            if (k == SurfaceKind::Torus) periodV = 2 * M_PI;
            if (patch) {
                if (patch->closedU) periodU = patch->u1 - patch->u0;
                if (patch->closedV) periodV = patch->v1 - patch->v0;
            }
            auto turned = [](const std::vector<Eigen::Vector2d>& uv, int axis, double period) {
                return uv.size() >= 4 && std::abs(uv.back()[axis] - uv.front()[axis]) > 0.75 * period;
            };
            auto centre = [](const std::vector<Eigen::Vector2d>& uv, int axis) {
                double lo = 1e300, hi = -1e300;
                for (const auto& q : uv) { lo = std::min(lo, q[axis]); hi = std::max(hi, q[axis]); }
                return 0.5 * (lo + hi);
            };
            for (int axis = 0; axis < 2; axis++) {
                const double period = axis == 0 ? periodU : periodV;
                if (!(period > 0)) continue;
                if (turned(outerUV, axis, period)) continue;        // (a loop that goes all the way round has no side)
                const double oc = centre(outerUV, axis);
                for (auto& h : holes) {
                    if (turned(h.second, axis, period)) continue;
                    const double shift = period * std::round((oc - centre(h.second, axis)) / period);
                    if (shift != 0.0) for (auto& q : h.second) q[axis] += shift;
                }
            }
        }

        // A band with holes: a face of a surface of revolution between two
        // complete circles, with more holes in it -- a bore with a cross
        // hole.  Neither the rings below (circles only) nor the polygon
        // triangulation (a complete circle is no closed outline in (u, v))
        // handle it: Bandextruder's heater's 6 mm bore came out as two flat
        // discs at its ends and a patch by the cross hole, its wall missing
        // (and 103 edges running the wrong way round it).  Cut open along a
        // line from one circle to the other that misses the holes, it is an
        // ordinary outline with holes in (u, v).
        {
            const SurfaceKind k = face.surface.kind;
            const bool periodic = k == SurfaceKind::Cylinder || k == SurfaceKind::Cone ||
                                  k == SurfaceKind::Sphere || k == SurfaceKind::Torus;
            auto fullTurn = [](const std::vector<Eigen::Vector2d>& uv) {
                return uv.size() >= 4 && std::abs(uv.back().x() - uv.front().x()) > 1.5 * M_PI;
            };
            using Ring = std::pair<std::vector<Vec3>, std::vector<Eigen::Vector2d>>;
            if (periodic && !holes.empty()) {
                std::vector<Ring> full, rest;
                (fullTurn(outerUV) ? full : rest).emplace_back(outer3D, outerUV);
                for (auto& h : holes) (fullTurn(h.second) ? full : rest).push_back(h);
                if (full.size() == 2 && !rest.empty()) {
                    for (auto& r : full) {
                        if (r.second.back().x() < r.second.front().x()) {   // u increasing
                            std::reverse(r.first.begin(), r.first.end());
                            std::reverse(r.second.begin(), r.second.end());
                        }
                        // (the closing point repeats the first, a turn later)
                        if (r.first.size() > 2 && (r.first.front() - r.first.back()).norm() < 1e-9) {
                            r.first.pop_back();
                            r.second.pop_back();
                        }
                    }
                    const double twoPi = 2 * M_PI;
                    auto wrap = [&](double x) { x = std::fmod(x, twoPi); return x < 0 ? x + twoPi : x; };
                    // The cut: a line from one circle to the other that misses the holes.  Where the holes
                    // are windows that wind round the face (a worm's grooves: each runs a third of a turn
                    // while it climbs, and nine of them cover every u) no line along the axis misses them,
                    // but a line that winds the same way does: u' = u - slope (v - vRef) makes them
                    // straight again, and everything below is done in (u', v), then moved back.
                    const double vRef = full[0].second[0].y();
                    auto sheared = [&](const Eigen::Vector2d& q, double slope) { return q.x() - slope * (q.y() - vRef); };
                    struct Cut { double slope = 0, cut = -1, widest = 0; };
                    // the holes' spans round the circle, from the first
                    // circle's start: the cut goes through the widest gap
                    auto findCut = [&](double slope) {
                        Cut c;
                        c.slope = slope;
                        const double u0 = sheared(full[0].second[0], slope);
                        std::vector<std::pair<double, double>> spans;
                        for (const auto& r : rest) {
                            double lo = 1e300, hi = -1e300;
                            for (const auto& q : r.second) { const double u = sheared(q, slope); lo = std::min(lo, u); hi = std::max(hi, u); }
                            const double a = wrap(lo - u0);
                            spans.push_back({a, a + (hi - lo)});
                        }
                        std::sort(spans.begin(), spans.end());
                        double end = spans[0].second;
                        for (size_t i = 1; i < spans.size(); i++) {
                            if (spans[i].first - end > c.widest) {
                                c.widest = spans[i].first - end;
                                c.cut = 0.5 * (spans[i].first + end);
                            }
                            end = std::max(end, spans[i].second);
                        }
                        if (spans[0].first + twoPi - end > c.widest) {
                            c.widest = spans[0].first + twoPi - end;
                            c.cut = 0.5 * (end + spans[0].first + twoPi);
                        }
                        return c;
                    };
                    Cut chosen = findCut(0.0);
                    if (!(chosen.cut >= 0 && chosen.widest > 1e-6)) {
                        // how much u each window advances per unit of v: the median over the steps of its
                        // outline that climb
                        std::vector<double> slopes;
                        for (const auto& r : rest) {
                            double vLo = 1e300, vHi = -1e300;
                            for (const auto& q : r.second) { vLo = std::min(vLo, q.y()); vHi = std::max(vHi, q.y()); }
                            std::vector<double> mine;
                            for (size_t i = 0; i + 1 < r.second.size(); i++) {
                                const double dv = r.second[i + 1].y() - r.second[i].y();
                                const double du = r.second[i + 1].x() - r.second[i].x();
                                if (std::abs(dv) > 0.01 * (vHi - vLo) && std::abs(du) < M_PI) mine.push_back(du / dv);
                            }
                            if (mine.empty()) continue;
                            std::nth_element(mine.begin(), mine.begin() + mine.size() / 2, mine.end());
                            slopes.push_back(mine[mine.size() / 2]);
                        }
                        if (!slopes.empty()) {
                            std::nth_element(slopes.begin(), slopes.begin() + slopes.size() / 2, slopes.end());
                            const double typical = slopes[slopes.size() / 2];
                            // (a little either side of it: the windows' sides are curves, not lines)
                            for (double f : {1.0, 0.9, 1.1, 0.8, 1.2, 0.7, 1.3}) {
                                const Cut c = findCut(typical * f);
                                if (c.cut >= 0 && c.widest > chosen.widest) chosen = c;
                            }
                        }
                    }
                    if (std::getenv("FIELDES_TESS_DEBUG"))
                        std::fprintf(stderr, "[tess] face %zu: %zu windows round a periodic face, cut at slope %.4g, widest gap %.4g rad (%s)\n",
                                     faceIdx, rest.size(), chosen.slope, chosen.widest, chosen.cut >= 0 && chosen.widest > 1e-6 ? "cut" : "no cut found");
                    if (chosen.cut >= 0 && chosen.widest > 1e-6) {
                        const double slope = chosen.slope;
                        for (auto& r : full) for (auto& q : r.second) q.x() = sheared(q, slope);
                        for (auto& r : rest) for (auto& q : r.second) q.x() = sheared(q, slope);
                        const double u0 = full[0].second[0].x();
                        const double cut = chosen.cut;
                        // each circle's sample nearest the cut
                        auto nearest = [&](const Ring& r, double u) {
                            size_t best = 0;
                            double bestD = 1e300;
                            for (size_t i = 0; i < r.second.size(); i++) {
                                const double d = std::abs(std::remainder(r.second[i].x() - u, twoPi));
                                if (d < bestD) { bestD = d; best = i; }
                            }
                            return best;
                        };
                        const Ring& A = full[0];
                        const Ring& B = full[1];
                        const size_t iA = nearest(A, u0 + cut);
                        const double uS = A.second[iA].x();
                        const size_t iB = nearest(B, uS);
                        std::vector<Vec3> band3D;
                        std::vector<Eigen::Vector2d> bandUV;
                        // along A, a whole turn up in u from the cut ...
                        const size_t nA = A.first.size(), nB = B.first.size();
                        for (size_t q = 0; q <= nA; q++) {
                            const size_t i = (iA + q) % nA;
                            const double u = uS + wrap(A.second[i].x() - uS) + (q == nA ? twoPi : 0.0);
                            band3D.push_back(A.first[i]);
                            bandUV.push_back(Eigen::Vector2d(q == 0 ? uS : u, A.second[i].y()));
                        }
                        // ... across the cut to B, and back down along it
                        // (a cut that winds round the face is a helix, not a line: its points go in too, one
                        // turn's share apart, or the triangles on it are chords through the cylinder)
                        auto addSide = [&](const Eigen::Vector2d& a, const Eigen::Vector2d& b) {
                            if (slope == 0.0) return;
                            const double turn = (b.x() - a.x()) + slope * (b.y() - a.y());
                            const int m = int(std::ceil(std::abs(turn) * double(g_turnSamples) / twoPi));
                            for (int j = 1; j < m; j++) {
                                const Eigen::Vector2d q = a + (double(j) / double(m)) * (b - a);
                                band3D.push_back(face.surface.evalParam(q.x() + slope * (q.y() - vRef), q.y()));
                                bandUV.push_back(q);
                            }
                        };
                        const double uB = uS + twoPi + std::remainder(B.second[iB].x() - uS, twoPi);
                        addSide(bandUV.back(), Eigen::Vector2d(uB, B.second[iB].y()));
                        for (size_t q = 0; q <= nB; q++) {
                            const size_t i = (iB + nB - q % nB) % nB;
                            double u = uB - wrap(B.second[iB].x() - B.second[i].x());
                            if (q == nB) u = uB - twoPi;
                            band3D.push_back(B.first[i]);
                            bandUV.push_back(Eigen::Vector2d(u, B.second[i].y()));
                        }
                        addSide(bandUV.back(), bandUV.front());
                        // the holes moved by whole turns to lie inside the cut-open band
                        for (auto& r : rest) {
                            double lo = 1e300;
                            for (const auto& q : r.second) lo = std::min(lo, q.x());
                            const double shift = twoPi * std::floor((lo - uS) / twoPi);
                            for (auto& q : r.second) q.x() -= shift;
                        }
                        // (back from u' to u)
                        for (auto& q : bandUV) q.x() += slope * (q.y() - vRef);
                        for (auto& r : rest) for (auto& q : r.second) q.x() += slope * (q.y() - vRef);
                        outer3D = std::move(band3D);
                        outerUV = std::move(bandUV);
                        holes = std::move(rest);
                    }
                }
            }
        }

        // Rings: a face of a surface of revolution bounded only by complete
        // circles (a hole's wall, a turned band, a torus fillet round a
        // circle) has no seam edge -- each loop is a closed line across the
        // whole period, and the outline has no area for the ear clipping
        // (HingedTable.step's countersunk holes came out without walls).
        // Two such loops are zipped together along u; a single one is
        // fanned to the cone's apex / the sphere's pole inside the face.
        {
            const SurfaceKind k = face.surface.kind;
            const bool periodic = k == SurfaceKind::Cylinder || k == SurfaceKind::Cone ||
                                  k == SurfaceKind::Sphere || k == SurfaceKind::Torus;
            std::vector<std::pair<std::vector<Vec3>, std::vector<Eigen::Vector2d>>> rings;
            rings.emplace_back(outer3D, outerUV);
            for (auto& h : holes) rings.push_back(h);
            auto fullTurn = [](const std::vector<Eigen::Vector2d>& uv) {
                return uv.size() >= 4 && std::abs(uv.back().x() - uv.front().x()) > 1.5 * M_PI;
            };
            bool allFull = periodic && rings.size() <= 2;
            for (auto& r : rings) allFull = allFull && fullTurn(r.second);
            if (allFull) {
                for (auto& r : rings) {   // u increasing along each ring
                    if (r.second.back().x() < r.second.front().x()) {
                        std::reverse(r.first.begin(), r.first.end());
                        std::reverse(r.second.begin(), r.second.end());
                    }
                }
                std::vector<Vec3> pts;
                std::vector<std::array<int, 3>> tris;
                if (rings.size() == 2) {
                    auto& A = rings[0];
                    auto& B = rings[1];
                    // B's u moved by whole periods to start where A starts -- and B started there: a ring
                    // begins wherever its first edge does, and zipped with A half a turn out of step a
                    // sphere's zone came out as a twisted strip, 13 % too big
                    {
                        const double twoPi = 2 * M_PI;
                        const size_t m = B.first.size();
                        if (m >= 4 && (B.first.front() - B.first.back()).norm() < 1e-6 * (1.0 + B.first.front().norm())) {
                            size_t best = 0;
                            double bestD = 1e300;
                            for (size_t i = 0; i + 1 < m; i++) {
                                const double d = std::abs(std::remainder(B.second[i].x() - A.second[0].x(), twoPi));
                                if (d < bestD) { bestD = d; best = i; }
                            }
                            if (best != 0) {
                                std::vector<Vec3> p3;
                                std::vector<Eigen::Vector2d> uv;
                                for (size_t i = 0; i + 1 < m; i++) {
                                    const size_t at = (best + i) % (m - 1);
                                    p3.push_back(B.first[at]);
                                    uv.push_back(Eigen::Vector2d(B.second[at].x() + (at < best ? twoPi : 0.0), B.second[at].y()));
                                }
                                p3.push_back(B.first[best]);
                                uv.push_back(Eigen::Vector2d(B.second[best].x() + twoPi, B.second[best].y()));
                                B.first = std::move(p3);
                                B.second = std::move(uv);
                            }
                        }
                    }
                    const double shift = 2 * M_PI * std::round((A.second[0].x() - B.second[0].x()) / (2 * M_PI));
                    for (auto& q : B.second) q.x() += shift;
                    // On a sphere or torus the meridian between the two
                    // circles is curved: zipped straight from one to the
                    // other the face came out flat (a button-head screw's
                    // domed head: 8 % of its volume missing).  Circles of
                    // latitude in between, on the surface, zipped in turn.
                    // (Only when both loops are such circles: constant v.)
                    using Ring = std::pair<std::vector<Vec3>, std::vector<Eigen::Vector2d>>;
                    std::vector<Ring> chain{A};
                    auto vSpan = [](const Ring& r, double& mean) {
                        double lo = 1e300, hi = -1e300, sum = 0;
                        for (const auto& q : r.second) { lo = std::min(lo, q.y()); hi = std::max(hi, q.y()); sum += q.y(); }
                        mean = sum / double(r.second.size());
                        return hi - lo;
                    };
                    double vA = 0, vB = 0;
                    // (spheres only: on tori it made volumes worse -- two
                    // Bandextruder parts 0 -> +0.7 % -- the tube-angle
                    // convention is not what this assumed; left zipped)
                    if (k == SurfaceKind::Sphere && vSpan(A, vA) < 1e-6 && vSpan(B, vB) < 1e-6) {
                        if (k == SurfaceKind::Torus) {
                            // which way round the tube the face runs: the
                            // way whose middle is inside it
                            while (vB - vA > M_PI) vB -= 2 * M_PI;
                            while (vA - vB > M_PI) vB += 2 * M_PI;
                            const double u0 = A.second[0].x();
                            if (!face.inTrim(face.surface.evalParam(u0, 0.5 * (vA + vB)))) {
                                vB += (vB > vA ? -2 : 2) * M_PI;
                            }
                        }
                        const int n = int(std::ceil(double(g_turnSamples) * std::abs(vB - vA) / (2 * M_PI))) - 1;
                        for (int q = 1; q <= n; q++) {
                            const double v = vA + (vB - vA) * double(q) / double(n + 1);
                            Ring R;
                            for (const auto& uvA : A.second) {
                                R.first.push_back(face.surface.evalParam(uvA.x(), v));
                                R.second.push_back(Eigen::Vector2d(uvA.x(), v));
                            }
                            chain.push_back(std::move(R));
                        }
                    }
                    chain.push_back(B);
                    for (size_t r = 0; r + 1 < chain.size(); r++) {
                        const Ring& P = chain[r];
                        const Ring& Q = chain[r + 1];
                        const int base = int(pts.size());
                        const int nA = int(P.first.size()), nB = int(Q.first.size());
                        pts.insert(pts.end(), P.first.begin(), P.first.end());
                        pts.insert(pts.end(), Q.first.begin(), Q.first.end());
                        int i = 0, j = 0;
                        while (i < nA - 1 || j < nB - 1) {
                            if (j == nB - 1 || (i < nA - 1 && P.second[size_t(i + 1)].x() <= Q.second[size_t(j + 1)].x())) {
                                tris.push_back({base + i, base + i + 1, base + nA + j});
                                i++;
                            } else {
                                tris.push_back({base + i, base + nA + j + 1, base + nA + j});
                                j++;
                            }
                        }
                    }
                } else {
                    const auto& pl = face.surface.placement;
                    std::vector<Vec3> tips;
                    if (k == SurfaceKind::Cone && std::abs(std::tan(face.surface.semiAngle)) > 1e-12) {
                        tips.push_back(pl.origin - (face.surface.radius / std::tan(face.surface.semiAngle)) * pl.zAxis);
                    } else if (k == SurfaceKind::Sphere) {
                        tips.push_back(pl.origin + face.surface.radius * pl.zAxis);
                        tips.push_back(pl.origin - face.surface.radius * pl.zAxis);
                    }
                    int tip = -1;
                    for (size_t t = 0; t < tips.size() && tip < 0; t++) {
                        if (tips.size() == 1 || face.inTrim(tips[t])) tip = int(t);
                    }
                    if (tip >= 0) {
                        pts = rings[0].first;
                        const int n = int(pts.size());
                        // A sphere's cap, fanned from its pole to its outline, is a cone (a ball of two
                        // hemispheres came out as two cones, half its volume): curves between, on the surface,
                        // each point of the outline moved a share of the way to the pole along its meridian, one
                        // turn's share of the sphere apart, and zipped in turn.  (Only where every meridian
                        // crosses the outline once: where u runs round it without turning back.)
                        const double vPole = tip == 0 ? 0.5 * M_PI : -0.5 * M_PI;      // (tips[0] is the pole on +z)
                        int lat = 0;
                        if (k == SurfaceKind::Sphere) {
                            bool monotone = true;
                            double gap = 0;
                            const auto& uv = rings[0].second;
                            for (size_t i = 0; i < uv.size(); i++) {
                                gap = std::max(gap, std::abs(vPole - uv[i].y()));
                                if (i && uv[i].x() < uv[i - 1].x() - 1e-9) monotone = false;
                            }
                            if (monotone) lat = std::max(0, int(std::ceil(double(g_turnSamples) * gap / (2 * M_PI))) - 1);
                        }
                        int last = 0;                                                  // (the start of the last curve in pts)
                        for (int q = 1; q <= lat; q++) {
                            const double share = double(q) / double(lat + 1);
                            const int base = int(pts.size());
                            for (int i = 0; i < n; i++) {
                                const auto& uvi = rings[0].second[size_t(i)];
                                pts.push_back(face.surface.evalParam(uvi.x(), uvi.y() + (vPole - uvi.y()) * share));
                            }
                            for (int i = 0; i + 1 < n; i++) {
                                tris.push_back({last + i, last + i + 1, base + i});
                                tris.push_back({last + i + 1, base + i + 1, base + i});
                            }
                            last = base;
                        }
                        const int pole = int(pts.size());
                        pts.push_back(tips[size_t(tip)]);
                        for (int i = 0; i + 1 < n; i++) tris.push_back({last + i, last + i + 1, pole});
                    }
                }
                if (!tris.empty()) {
                    emit(pts, tris);
                    continue;
                }
            }
        }

        // The face as a polygon with holes in its own (u, v), triangulated
        // by Manifold: holes are contours of the opposite winding, no
        // bridges. (The old bridge + ear clipping lost every hole's bridge
        // to triangulatePolygon's duplicate-point removal -- a bridge
        // visits two vertices twice -- and left faces with holes partly
        // untriangulated.) Angles are scaled to lengths so the polygon
        // isn't squashed along u.
        Eigen::Vector2d scale(1.0, 1.0);
        {
            const auto& pl = face.surface.placement;
            double rsum = 0.0;
            for (const Vec3& p : outer3D) {
                const Vec3 d = p - pl.origin;
                rsum += (d - d.dot(pl.zAxis) * pl.zAxis).norm();
            }
            const double rmean = std::max(rsum / double(outer3D.size()), 1e-12);
            switch (face.surface.kind) {
                case SurfaceKind::Cylinder:
                case SurfaceKind::Cone: scale = {rmean, 1.0}; break;
                case SurfaceKind::Sphere: scale = {rmean, face.surface.radius}; break;
                case SurfaceKind::Torus: scale = {rmean, face.surface.radius2}; break;
                case SurfaceKind::BSpline:
                    if (patch) {
                        // (u, v) in turns of the surface: a mesh edge of length 1 turns the face by about the same
                        // whichever way it runs.  Triangles come out as long along a tube as its bends allow and
                        // as short round it as its curve does, not the square ones a uniform (u, v) would make
                        // -- an isotropic mesh of a 580 mm tube of 28 mm width is 60 000 triangles, this is 2 000.
                        double ru = 1, rv = 1;
                        patch->turnRates(ru, rv);
                        ru = std::max(ru, 1e-6);
                        scale = {ru, std::max(rv, 0.03 * ru)};
                    }
                    break;
                default: break;
            }
        }
        std::vector<Vec3> pts;
        std::vector<Eigen::Vector2d> ptsUV;          // scaled (u, v) of each point
        std::vector<int> contourStart, contourOf;   // for walking along a contour
        manifold::PolygonsIdx polys;
        auto addContour = [&](std::vector<Vec3> p3, std::vector<Eigen::Vector2d> uv, bool outer) {
            // (the closing vertex repeats the first -- as the same point of space to a ten millionth of a
            // millimetre, the loop's last edge having its own copy of the vertex; it was dropped only when
            // bit-identical, and the copy left a triangle with two corners at one place)
            if (p3.size() > 1 && (p3.front() - p3.back()).norm() < 1e-7 &&
                (uv.front() - uv.back()).norm() < 1e-5 * (1.0 + uv.front().norm())) {
                p3.pop_back();   // the closing vertex repeats the first
                uv.pop_back();
            }
            if (p3.size() < 3) return;
            double area = 0.0;
            for (size_t i = 0, j = uv.size() - 1; i < uv.size(); j = i++) {
                area += (uv[j].x() * uv[i].y() - uv[i].x() * uv[j].y()) * scale.x() * scale.y();
            }
            if ((area > 0) != outer) {   // outer counter-clockwise, holes clockwise
                std::reverse(p3.begin(), p3.end());
                std::reverse(uv.begin(), uv.end());
            }
            manifold::SimplePolygonIdx c;
            contourStart.push_back(int(pts.size()));
            for (size_t i = 0; i < p3.size(); i++) {
                const Eigen::Vector2d q(uv[i].x() * scale.x(), uv[i].y() * scale.y());
                c.push_back({manifold::vec2(q.x(), q.y()), int(pts.size())});
                pts.push_back(p3[i]);
                ptsUV.push_back(q);
                contourOf.push_back(int(polys.size()));
            }
            polys.push_back(std::move(c));
        };
        addContour(outer3D, outerUV, true);
        for (auto& hole : holes) addContour(hole.first, hole.second, false);
        if (polys.empty()) continue;

        std::vector<std::array<int, 3>> tris;
        try {
            for (const auto& t : manifold::TriangulateIdx(polys)) tris.push_back({t.x, t.y, t.z});
        } catch (const std::exception&) {
            tris.clear();
        }
        if (tris.empty()) {
            g_tessFailedFaces++;
            continue;
        }

        // Manifold leaves out zero-area triangles, and with them a boundary
        // point lying on the straight line between its neighbours: it
        // triangulates across it with a chord, and the face next door,
        // which does use the point, meets a T-junction (Bracket.step: 72
        // open edges along its straight edges). Such chords are split back
        // through the skipped points.
        {
            std::map<std::pair<int, int>, int> use;
            for (const auto& t : tris) {
                for (int k = 0; k < 3; k++) {
                    const int a = t[k], b = t[(k + 1) % 3];
                    use[{std::min(a, b), std::max(a, b)}]++;
                }
            }
            auto step = [&](int i, int dir) {
                const int c = contourOf[i], s0 = contourStart[c];
                const int n = int(polys[size_t(c)].size());
                return s0 + ((i - s0 + dir + n) % n);
            };
            std::vector<std::array<int, 3>> fixedTris;
            for (const auto& t : tris) {
                bool split = false;
                for (int k = 0; k < 3 && !split; k++) {
                    const int a = t[k], b = t[(k + 1) % 3], c = t[(k + 2) % 3];
                    if (use[{std::min(a, b), std::max(a, b)}] != 1) continue;    // interior edge
                    if (contourOf[a] != contourOf[b]) continue;
                    if (step(a, 1) == b || step(a, -1) == b) continue;           // a real boundary segment
                    const Eigen::Vector2d ab = ptsUV[b] - ptsUV[a];
                    const double len = ab.norm();
                    for (int dir : {1, -1}) {
                        std::vector<int> chain;
                        bool onChord = true;
                        for (int i = step(a, dir); i != b && onChord; i = step(i, dir)) {
                            const Eigen::Vector2d ap = ptsUV[i] - ptsUV[a];
                            const double along = ap.dot(ab) / (len * len);
                            onChord = chain.size() < 4096 && along > 0 && along < 1 &&
                                      std::abs(ab.x() * ap.y() - ab.y() * ap.x()) <= 1e-6 * len * len;
                            chain.push_back(i);
                        }
                        if (!onChord || chain.empty()) continue;
                        int prev = a;
                        for (int q : chain) {
                            fixedTris.push_back({prev, q, c});
                            prev = q;
                        }
                        fixedTris.push_back({prev, b, c});
                        split = true;
                        break;
                    }
                }
                if (!split) fixedTris.push_back(t);
            }
            tris = std::move(fixedTris);
        }

        // Inside a curved analytic face the triangles spanning its outline
        // cut chords through the surface (ArcRevolve.step's quarter-turn
        // sphere and torus patches: 8 % of its volume missing).  Interior
        // edges whose middle is off the surface by more than the outline's
        // own sampling allows (a circle's sagitta at g_turnSamples points a
        // turn) are split at their (u, v) middle, on the surface -- in both
        // triangles that share them (a triangle with 1 / 2 / 3 split edges
        // becomes 2 / 3 / 4), so no cracks; the outline's edges are left
        // alone (the face next door shares them).  Tori are refined the same
        // way, but kept only if the area of the mesh comes closer to the
        // torus patch's own (an integral over its outline in (u, v)):
        // ArcRevolve.step's large tori (R 28.9, r 27 -- the tube nearly
        // touching the axis) came out 21 % too big refined, for a reason not
        // found (each new point checked out on the surface, between its
        // edge's ends); and an unrefined fillet torus, its triangles spanning
        // the outline, is 8 to 11 % too small.  A free-form B-spline
        // face is refined by refineFreeform, below.
        {
            const SurfaceKind k = face.surface.kind;
            const bool curved = k == SurfaceKind::Cylinder || k == SurfaceKind::Cone ||
                                k == SurfaceKind::Sphere || k == SurfaceKind::Torus;
            if (curved && !tris.empty() && scale.x() > 0 && scale.y() > 0) {
                const double rad = (k == SurfaceKind::Cylinder || k == SurfaceKind::Cone)
                                       ? scale.x() : std::min(scale.x(), scale.y());
                const double tol = std::max(1e-9, rad * (1 - std::cos(M_PI / double(g_turnSamples))));
                // (a worm's helical grooves leave triangles that span the cylinder, 200 degrees of it: splitting
                // them to a sagitta takes a dozen rounds and a good many points)
                const size_t cap = std::max<size_t>(4 * pts.size() + 64, 120000);
                const size_t pts0 = pts.size();
                const std::vector<std::array<int, 3>> tris0 = tris;
                std::vector<Eigen::Vector2d> raw(ptsUV.size());
                for (size_t i = 0; i < ptsUV.size(); i++)
                    raw[i] = Eigen::Vector2d(ptsUV[i].x() / scale.x(), ptsUV[i].y() / scale.y());
                // (a point on the surface at an edge's (u, v) middle is never
                // further from the edge's middle than half its length -- a
                // semicircle's is exactly that; one that is lies on another
                // part of the surface than the face: the face's (u, v) doesn't
                // describe it well, and it keeps its outline triangulation)
                bool astray = false;
                for (int round = 0; round < 16 && !astray; round++) {
                    std::map<std::pair<int, int>, int> use;
                    for (const auto& t : tris)
                        for (int e = 0; e < 3; e++) {
                            const int a = t[size_t(e)], b = t[size_t((e + 1) % 3)];
                            use[{std::min(a, b), std::max(a, b)}]++;
                        }
                    std::map<std::pair<int, int>, int> mid;     // split edge -> its new point
                    for (const auto& e : use) {
                        if (e.second != 2 || pts.size() >= cap) continue;   // the outline: never
                        const int a = e.first.first, b = e.first.second;
                        Eigen::Vector2d m = 0.5 * (raw[size_t(a)] + raw[size_t(b)]);
                        Vec3 on = face.surface.evalParam(m.x(), m.y());
                        if (k == SurfaceKind::Torus) {
                            // (the (u, v) middle of a long edge across a dome of a torus whose tube is much wider
                            // than its axis -- a spindle -- is not near the edge: u is all but undefined at the
                            // apex.  The point of the surface nearest the middle of the edge is.)
                            const Surface::Closest c = face.surface.closestPoint(0.5 * (pts[size_t(a)] + pts[size_t(b)]));
                            const double twoPi = 2 * M_PI;
                            m = Eigen::Vector2d(c.u + twoPi * std::round((m.x() - c.u) / twoPi),
                                                c.v + twoPi * std::round((m.y() - c.v) / twoPi));
                            on = face.surface.evalParam(m.x(), m.y());
                        }
                        const double off = (on - 0.5 * (pts[size_t(a)] + pts[size_t(b)])).norm();
                        if (k == SurfaceKind::Torus) {
                            // (a torus curves round its axis and round its tube at different radii: an edge is
                            // short enough when the surface turns by no more than a turn's share between its
                            // ends, whichever way it runs, not when its sagitta is a share of the smaller radius)
                            const Vec3 na = face.surface.normalAt(raw[size_t(a)].x(), raw[size_t(a)].y());
                            const Vec3 nb = face.surface.normalAt(raw[size_t(b)].x(), raw[size_t(b)].y());
                            const double nn = na.norm() * nb.norm();
                            if (nn > 0 && std::acos(std::max(-1.0, std::min(1.0, na.dot(nb) / nn))) <= 2 * M_PI / double(g_turnSamples)) continue;
                        } else if (off <= tol) continue;
                        const double len = (pts[size_t(a)] - pts[size_t(b)]).norm();
                        if (off > 0.5 * len * (1 + 1e-6) + 1e-12) {
                            astray = true;
                            break;
                        }
                        mid[e.first] = int(pts.size());
                        pts.push_back(on);
                        raw.push_back(m);
                    }
                    if (mid.empty() || astray) break;
                    std::vector<std::array<int, 3>> next;
                    for (const auto& t : tris) {
                        int m[3];
                        int count = 0;
                        for (int e = 0; e < 3; e++) {
                            const int a = t[size_t(e)], b = t[size_t((e + 1) % 3)];
                            auto it = mid.find({std::min(a, b), std::max(a, b)});
                            m[e] = it == mid.end() ? -1 : it->second;
                            count += m[e] >= 0;
                        }
                        if (count == 0) { next.push_back(t); continue; }
                        // rotate so edge 0 (t0 -> t1) is split, and with two
                        // splits, edge 1 as well
                        int r = 0;
                        if (count == 1) { while (m[r] < 0) r++; }
                        else if (count == 2) { while (m[r] < 0 || m[(r + 1) % 3] < 0) r++; }
                        const int a = t[size_t(r)], b = t[size_t((r + 1) % 3)], c = t[size_t((r + 2) % 3)];
                        const int mab = m[r], mbc = m[(r + 1) % 3], mca = m[(r + 2) % 3];
                        if (count == 1) {
                            next.push_back({a, mab, c});
                            next.push_back({mab, b, c});
                        } else if (count == 2) {
                            next.push_back({mab, b, mbc});
                            next.push_back({a, mab, mbc});
                            next.push_back({a, mbc, c});
                        } else {
                            next.push_back({a, mab, mca});
                            next.push_back({mab, b, mbc});
                            next.push_back({mca, mbc, c});
                            next.push_back({mab, mbc, mca});
                        }
                    }
                    tris = std::move(next);
                }
                // (kept only if the face stays a clean patch where the mesh
                // joins its points: no edge run the same way twice -- a
                // face whose outline triangulation overlaps itself in
                // (u, v) came out with folds; it then keeps its outline
                // triangulation)
                if (std::getenv("FIELDES_TESS_DEBUG") && k != SurfaceKind::BSpline)
                    std::fprintf(stderr, "[tess] face %zu (kind %d): refined %zu -> %zu points, %zu triangles%s%s\n", faceIdx, int(k),
                                 pts0, pts.size(), tris.size(), astray ? ", astray" : "",
                                 pts.size() > pts0 && runsTwice(pts, tris) ? ", runs twice" : "");
                if (astray) {
                    tris = tris0;
                    pts.resize(pts0);
                } else if (pts.size() > pts0 && !runsTwice(pts, tris)) {
                    // (clean)
                    if (k == SurfaceKind::Torus) {
                        // the torus patch's area from its outline: A = r |closed integral of (R v + r sin v) du|
                        const double R = face.surface.radius, r = face.surface.radius2;
                        double integral = 0;
                        for (size_t c = 0; c < polys.size(); c++) {
                            const size_t n = polys[c].size(), s0 = size_t(contourStart[c]);
                            for (size_t i = 0; i < n; i++) {
                                const Eigen::Vector2d a = ptsUV[s0 + i], b = ptsUV[s0 + (i + 1) % n];
                                const double ua = a.x() / scale.x(), va = a.y() / scale.y();
                                const double ub = b.x() / scale.x(), vb = b.y() / scale.y();
                                integral += 0.5 * ((R * va + r * std::sin(va)) + (R * vb + r * std::sin(vb))) * (ub - ua);
                            }
                        }
                        const double exact = r * std::abs(integral);
                        // (a patch that reaches the axis of a torus whose tube is wider than its axis -- the apex
                        // of a dome -- has no (u, v) outline to take the area of: it is judged by the refinement
                        // itself, which is checked not to fold)
                        double radialMin = 1e300;
                        for (size_t i = 0; i < ptsUV.size(); i++)
                            radialMin = std::min(radialMin, R + r * std::cos(ptsUV[i].y() / scale.y()));
                        const bool apex = radialMin < 0.02 * r;
                        auto meshArea = [&](const std::vector<std::array<int, 3>>& T) {
                            double area = 0;
                            for (const auto& t : T)
                                area += 0.5 * (pts[size_t(t[1])] - pts[size_t(t[0])]).cross(pts[size_t(t[2])] - pts[size_t(t[0])]).norm();
                            return area;
                        };
                        if (!apex && !(std::abs(meshArea(tris) - exact) < std::abs(meshArea(tris0) - exact))) {
                            tris = tris0;
                            pts.resize(pts0);
                        }
                    }
                } else if (pts.size() > pts0) {
                    tris = tris0;
                    pts.resize(pts0);
                }
            }
        }

        // A free-form face: the same inside its outline, see refineFreeform -- after the other faces
        if (patch && !tris.empty()) {
            Deferred d;
            d.faceIdx = faceIdx;
            d.patch = patch;
            d.scale = scale;
            d.pts0 = pts.size();
            d.tris0 = tris;
            d.raw.resize(ptsUV.size());
            for (size_t i = 0; i < ptsUV.size(); i++)
                d.raw[i] = Eigen::Vector2d(ptsUV[i].x() / scale.x(), ptsUV[i].y() / scale.y());
            // the outline's edges: consecutive points of a contour, which the face next door shares
            for (size_t c = 0; c < polys.size(); c++) {
                const size_t n = polys[c].size(), s0 = size_t(contourStart[c]);
                for (size_t i = 0; i < n; i++) {
                    const int a = int(s0 + i), b = int(s0 + (i + 1) % n);
                    d.outlineEdges.insert((uint64_t(uint32_t(std::min(a, b))) << 32) | uint32_t(std::max(a, b)));
                }
            }
            // how far the outline's points are from the surface
            for (size_t i = 0; i < d.pts0; i++) {
                Vec3 at;
                patch->evalWrapped(d.raw[i].x(), d.raw[i].y(), at);
                d.noise = std::max(d.noise, (at - pts[i]).norm());
            }
            d.pts = std::move(pts);
            d.tris = std::move(tris);
            deferred.push_back(std::move(d));
            continue;
        }

        emit(pts, tris);
    }

    // The free-form faces, refined inside their outlines: a few at a time on as many threads as there are
    // (`threads`: 0 for all of them, 1 for none besides this one), then put into the mesh in the order of the faces
    if (!deferred.empty()) {
        const int turn = g_turnSamples;
        const double tolAbs = g_tolAbs;
        std::atomic<size_t> next{0};
        auto work = [&]() {
            g_turnSamples = turn;           // (thread_local: each thread has its own)
            g_tolAbs = tolAbs;
            for (size_t q; (q = next.fetch_add(1)) < deferred.size();) {
                Deferred& d = deferred[q];
                refineFreeform(*d.patch, d.scale, d.pts0, d.noise, d.outlineEdges, d.pts, d.raw, d.tris, turn, d.faceIdx);
                d.folds = d.pts.size() > d.pts0 && runsTwice(d.pts, d.tris);
            }
        };
        const size_t hw = std::max(1u, std::thread::hardware_concurrency());
        const size_t nThreads = std::max<size_t>(1, std::min<size_t>(deferred.size(), threads > 0 ? size_t(threads) : hw));
        std::vector<std::thread> pool;
        for (size_t t = 1; t < nThreads; t++) pool.emplace_back(work);
        work();
        for (auto& th : pool) th.join();

        for (Deferred& d : deferred) {
            if (std::getenv("FIELDES_TESS_QUALITY")) {
                // how the triangles lie on the surface: the angle between each triangle's normal and the surface's at its middle,
                // and how far the middle is from the surface -- by area
                double areaAll = 0, areaTilted = 0, worstTilt = 0, worstOff = 0;
                const size_t every = std::max<size_t>(1, d.tris.size() / 3000);
                for (size_t q = 0; q < d.tris.size(); q += every) {
                    const Vec3 a = d.pts[size_t(d.tris[q][0])], b = d.pts[size_t(d.tris[q][1])], c = d.pts[size_t(d.tris[q][2])];
                    const Vec3 n = (b - a).cross(c - a);
                    const double ar = 0.5 * n.norm();
                    if (!(ar > 0)) continue;
                    const Vec3 mid = (a + b + c) / 3.0;
                    const BSplinePatch::Foot ft = d.patch->closest(mid);
                    const Vec3 sn = d.patch->normalWrapped(ft.u, ft.v);
                    const double tilt = std::acos(std::min(1.0, std::abs(n.normalized().dot(sn)))) * 180.0 / M_PI;
                    areaAll += ar;
                    if (tilt > 20) areaTilted += ar;
                    worstTilt = std::max(worstTilt, tilt);
                    worstOff = std::max(worstOff, ft.dist);
                }
                std::fprintf(stderr, "[quality] face %zu: %zu triangles; %.1f %% of the area tilted more than 20 deg from the surface (worst %.1f deg), mid-points up to %.4g mm off the surface\n",
                             d.faceIdx, d.tris.size(), areaAll > 0 ? 100.0 * areaTilted / areaAll : 0.0, worstTilt, worstOff);
            }
            if (d.folds) {
                if (std::getenv("FIELDES_TESS_DEBUG"))
                    std::fprintf(stderr, "[tess] face %zu: refined to %zu points but the patch folds: left as it is\n", d.faceIdx, d.pts.size());
                d.tris = d.tris0;
                d.pts.resize(d.pts0);
            }
            emitFace(solid.faces[d.faceIdx], d.faceIdx, d.patch, d.pts, d.tris);
        }
    }
    orientFaces(mesh);
    return mesh;
}

}  // namespace step
}  // namespace libfive

// TEMP DEBUG (2026-09-23): direct entry point for validating
// tessellateSolid() from Python before the mesh-based winding-number
// oracle (stage 2) exists to wrap it properly. Writes solid `solidIdx`
// of the STEP file at `path` to an OBJ file at `outPath`. Returns 1 on
// success, 0 on failure (bad path/index). Remove once step_oracle.cpp
// wraps this mesh directly and nothing needs to reach it from outside
// the library anymore.
extern "C" int libfive_step_debug_tessellate(const char* path, int solidIdx, const char* outPath)
{
    using namespace libfive::step;
    ImportResult result = importStepFile(path);
    if (!result.ok || !result.model || solidIdx < 0 ||
        static_cast<size_t>(solidIdx) >= result.model->solids.size()) {
        return 0;
    }
    g_tessCacheHits = 0;
    g_tessCacheMisses = 0;
    g_tessCacheIdNeg1 = 0;
    g_tessFailedFaces = 0;
    TessMesh mesh = tessellateSolid(result.model->solids[solidIdx]);
    if (g_tessFailedFaces.load()) fprintf(stderr, "[tess] %d face(s) not triangulated\n", g_tessFailedFaces.load());
    if (std::getenv("FIELDES_STEP_DEBUG_TESS_CACHE")) {
        fprintf(stderr, "[tess cache] hits=%d misses=%d negId=%d\n",
                g_tessCacheHits.load(), g_tessCacheMisses.load(), g_tessCacheIdNeg1.load());
    }
    FILE* f = fopen(outPath, "w");
    if (!f) return 0;
    for (auto& v : mesh.verts) fprintf(f, "v %.17g %.17g %.17g\n", v.x(), v.y(), v.z());
    for (auto& t : mesh.tris) fprintf(f, "f %d %d %d\n", t[0] + 1, t[1] + 1, t[2] + 1);
    fclose(f);

    // Sidecar file: one line per triangle (same order as the OBJ's `f`
    // lines), "faceIdx isBSpline" -- lets a caller mark exactly which
    // triangles/faces came from a B-spline surface without re-deriving
    // face membership from the mesh geometry.
    std::string tagsPath = std::string(outPath) + ".tags";
    FILE* tf = fopen(tagsPath.c_str(), "w");
    if (tf) {
        for (size_t i = 0; i < mesh.tris.size(); i++) {
            fprintf(tf, "%d %d\n", mesh.triFaceIdx[i], mesh.triIsBSpline[i] ? 1 : 0);
        }
        fclose(tf);
    }
    return 1;
}

// TEMP DEBUG (2026-09-24): lists every B-spline face of one solid with
// its REAL trim-boundary bbox (Face::boundMin/boundMax, built directly
// from the loop's actual 3D boundary points at import time -- see
// resolveFace/resolveLoop in step_face.cpp) -- completely independent of
// tessellateSolid()'s own triangulation, so this can be used to check
// whether a tessellated/colored B-spline patch's on-screen location
// actually matches the real face's own geometry, or whether the
// tessellation pipeline itself is misattributing/misplacing it.
extern "C" int libfive_step_debug_bspline_faces(const char* path, int solidIdx, char* outBuf, int outBufLen)
{
    using namespace libfive::step;
    ImportResult result = importStepFile(path);
    if (!result.ok || !result.model || solidIdx < 0 ||
        static_cast<size_t>(solidIdx) >= result.model->solids.size()) {
        return 0;
    }
    const Solid& solid = result.model->solids[solidIdx];
    std::string out;
    char line[512];
    int count = 0;
    for (size_t fi = 0; fi < solid.faces.size(); fi++) {
        const Face& f = solid.faces[fi];
        if (f.surface.kind != SurfaceKind::BSpline) continue;
        count++;
        int nLoops = int(f.loops.size());
        int nCtrlU = f.surface.nCtrlU, nCtrlV = f.surface.nCtrlV;
        snprintf(line, sizeof(line),
            "face %zu: loops=%d ctrl=%dx%d boundMin=(%.6f,%.6f,%.6f) boundMax=(%.6f,%.6f,%.6f)\n",
            fi, nLoops, nCtrlU, nCtrlV,
            f.boundMin.x(), f.boundMin.y(), f.boundMin.z(),
            f.boundMax.x(), f.boundMax.y(), f.boundMax.z());
        out += line;
    }
    snprintf(line, sizeof(line), "total B-spline faces: %d / %zu\n", count, solid.faces.size());
    out += line;
    strncpy(outBuf, out.c_str(), outBufLen - 1);
    outBuf[outBufLen - 1] = '\0';
    return int(out.size());
}
