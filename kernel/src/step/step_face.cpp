/*
libfive: a CAD kernel for modeling with implicit functions
This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#include "libfive/step/step_face.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <optional>

namespace libfive {
namespace step {

// Ray-casting point-in-polygon test (even-odd rule) in 2D.
bool pointInPolygon(const std::vector<Eigen::Vector2d>& poly, double u, double v)
{
    bool inside = false;
    size_t n = poly.size();
    if (n < 3) return false;
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        double ui = poly[i].x(), vi = poly[i].y();
        double uj = poly[j].x(), vj = poly[j].y();
        bool crosses = ((vi > v) != (vj > v));
        if (crosses) {
            double uCross = ui + (v - vi) / (vj - vi) * (uj - ui);
            if (u < uCross) inside = !inside;
        }
    }
    return inside;
}

void Loop::makeBox()
{
    boxPoints = 0;
    if (uv.size() < 3) return;
    double u0 = 1e300, u1 = -1e300, v0 = 1e300, v1 = -1e300;
    for (const auto& p : uv) {
        if (!std::isfinite(p.x()) || !std::isfinite(p.y())) return;
        u0 = std::min(u0, p.x());
        u1 = std::max(u1, p.x());
        v0 = std::min(v0, p.y());
        v1 = std::max(v1, p.y());
    }
    uMin = u0;
    uMax = u1;
    vMin = v0;
    vMax = v1;
    boxPoints = uv.size();
}

// Ear-clipping triangulation of a simple (non-self-intersecting,
// hole-free) 2D polygon, appending each ear's three POLYGON-INDEX
// triples to `out`. Returns false (and may leave `out` partially
// filled) if the polygon is degenerate or clipping stalls -- callers
// must treat that as "no usable triangulation" rather than trusting a
// partial result.
//
// O(n^2) worst case (each of n-2 ears does an O(n) scan for
// containment), which is fine here: this runs once per face at import
// time (see resolveFace), not per query point -- the whole reason it
// exists is to let Solid::windingSign (step_model.cpp) do a robust
// solid-angle-based inside/outside test at query time without
// retriangulating anything.
bool triangulatePolygon(const std::vector<Eigen::Vector2d>& fullPoly,
                         std::vector<std::array<int, 3>>& out)
{
    // CONFIRMED BUG this fixes: a loop built by concatenating many
    // edges (sampleEdgeIntoParamSpace) routinely has consecutive
    // points that are the *same* 3D vertex projected twice -- once as
    // one edge's endpoint, once as the next edge's start -- landing at
    // very slightly different (u,v) due to independent floating-point
    // projections (confirmed directly: differences around 1e-14 to
    // 1e-17 on coordinates of order 1e-2, i.e. genuinely the same
    // point). A near-zero-length edge like that degenerates the ear
    // convexity/containment math (cross products of a near-zero vector
    // are themselves near-zero noise, not a meaningful sign), and was
    // blocking triangulation outright on most real loops -- not a rare
    // edge case, close to the common one for a loop assembled from
    // several edges. Fixed by deduplicating consecutive near-identical
    // points before triangulating at all, with indices mapped back to
    // the original array so callers keep indexing `fullPoly` as before.
    // CONFIRMED as still not enough on its own for some real loops:
    // a curve that passes very close to a cylinder/cone's own axis can
    // have several of its samples independently hit Surface::
    // closestPoint's near-axis fallback (a fixed placement.xAxis
    // direction, used whenever the true radial direction would be
    // numerically unstable that close in) and land on the exact same
    // (u,v) -- not just consecutively, but at two genuinely different
    // points in the traversal if the curve approaches the axis, moves
    // away, and comes back. A dedup against only the immediately
    // previous point misses that. Checking against every point kept so
    // far (not just the last one) catches it too, at the same O(n^2)
    // cost this function already pays for ear validity, so it isn't a
    // new asymptotic cost.
    std::vector<int> keep;
    keep.reserve(fullPoly.size());
    for (size_t i = 0; i < fullPoly.size(); i++) {
        const auto& cur = fullPoly[i];
        bool dup = false;
        for (int j : keep) {
            if ((cur - fullPoly[j]).squaredNorm() <= 1e-20) { dup = true; break; }
        }
        if (!dup) keep.push_back(static_cast<int>(i));
    }
    std::vector<Eigen::Vector2d> poly;
    poly.reserve(keep.size());
    for (int i : keep) poly.push_back(fullPoly[i]);

    // A loop that collapses to fewer than 3 distinct points after
    // dedup is genuinely zero-area (confirmed against a real case: 49
    // samples that were all, to full double precision, the exact same
    // point -- a curve running essentially along a cylinder's own axis,
    // where Surface::closestPoint's near-axis fallback returns the same
    // direction regardless of exact position). A zero-area region
    // contributes zero solid angle no matter what, so this is safe to
    // report as "succeeded with nothing to add", not a failure -- unlike
    // a real ear-clipping failure below, which must stay a failure
    // (callers can't tell "definitely zero" from "unknown" otherwise,
    // and treating an unknown case as zero is exactly the partial-sum
    // bug already fixed once).
    size_t n = poly.size();
    if (n < 3) { out.clear(); return true; }

    // Ear-clipping assumes a CCW polygon; work on an index list that we
    // can reverse without touching the caller's actual points.
    double signedArea = 0;
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        signedArea += (poly[j].x() * poly[i].y() - poly[i].x() * poly[j].y());
    }
    std::vector<int> idx(n);
    for (size_t i = 0; i < n; i++) idx[i] = static_cast<int>(i);
    if (signedArea < 0) std::reverse(idx.begin(), idx.end());

    auto cross2 = [](const Eigen::Vector2d& a, const Eigen::Vector2d& b) {
        return a.x() * b.y() - a.y() * b.x();
    };
    // CONFIRMED BUG this fixes: the previous "not (mixed signs)" test
    // counts a point exactly ON the ear triangle's boundary (d1/d2/d3
    // == 0) as inside it, which blocks that ear. That's fatal in
    // practice, not just an edge case: a real STEP boundary loop is
    // built by concatenating many edges (see sampleEdgeIntoParamSpace),
    // and consecutive edges naturally share an endpoint -- so most
    // loops here have several vertices sitting exactly on another part
    // of the same boundary. Measured directly: this was blocking nearly
    // every ear on real loops, and triangulatePolygon failing outright
    // is what left most real faces without winding-number coverage.
    // Fixed with a genuine strict-interior test: require the point on
    // the *interior* side of all three edges by more than a tolerance
    // scaled to the triangle's own area (so it doesn't depend on the
    // absolute coordinate scale, which varies a lot between an angular
    // u on a cylinder and a linear one on a plane), and treat a point
    // that coincides with one of the triangle's own vertices --
    // guaranteed for a shared-endpoint loop -- as never blocking.
    auto pointInTri = [&](const Eigen::Vector2d& p, const Eigen::Vector2d& a,
                          const Eigen::Vector2d& b, const Eigen::Vector2d& c) {
        constexpr double kCoincideEps = 1e-14;  // squared distance
        if ((p - a).squaredNorm() < kCoincideEps || (p - b).squaredNorm() < kCoincideEps ||
            (p - c).squaredNorm() < kCoincideEps) {
            return false;
        }
        double d1 = cross2(b - a, p - a);
        double d2 = cross2(c - b, p - b);
        double d3 = cross2(a - c, p - c);
        double area2 = std::abs(cross2(b - a, c - a));
        double eps = std::max(1e-15, area2 * 1e-9);
        return (d1 > eps && d2 > eps && d3 > eps) || (d1 < -eps && d2 < -eps && d3 < -eps);
    };

    int guard = 0;
    const int guardMax = static_cast<int>(n) * static_cast<int>(n) + 16;
    while (idx.size() > 3 && guard++ < guardMax) {
        bool clipped = false;
        for (size_t k = 0; k < idx.size(); k++) {
            size_t iPrev = (k + idx.size() - 1) % idx.size();
            size_t iNext = (k + 1) % idx.size();
            const Eigen::Vector2d& a = poly[idx[iPrev]];
            const Eigen::Vector2d& b = poly[idx[k]];
            const Eigen::Vector2d& c = poly[idx[iNext]];
            // Convex (CCW) vertex only -- a reflex one can't be an ear.
            if (cross2(b - a, c - b) <= 0) continue;
            bool anyInside = false;
            for (size_t m = 0; m < idx.size() && !anyInside; m++) {
                if (m == iPrev || m == k || m == iNext) continue;
                if (pointInTri(poly[idx[m]], a, b, c)) anyInside = true;
            }
            if (anyInside) continue;

            // idx indexes the deduplicated `poly`; map back to fullPoly
            // indices (via `keep`) before storing, since that's the
            // array callers actually index with the result.
            out.push_back({keep[idx[iPrev]], keep[idx[k]], keep[idx[iNext]]});
            idx.erase(idx.begin() + k);
            clipped = true;
            break;
        }
        if (!clipped) {
            // CONFIRMED BUG this fixes: ear-clipping can legitimately
            // run out of clippable ears when every remaining point is
            // collinear -- a real case on a real loop, not just
            // numerical noise (confirmed: a boundary segment held one
            // parameter constant, e.g. a straight edge in (u,v) at
            // fixed v, for dozens of samples in a row). No triangle
            // among purely collinear points can have nonzero area, so
            // none can pass the convexity/containment tests above -- but
            // that also means this remainder has zero area to
            // contribute in the first place, so the triangles already
            // found are the complete, correct answer for the polygon's
            // real area. Only a remainder that *isn't* just a
            // degenerate zero-area sliver is a genuine unresolvable
            // failure (self-intersecting input).
            bool remainderDegenerate = idx.size() < 3;
            if (idx.size() >= 3) {
                remainderDegenerate = true;
                const Eigen::Vector2d& p0 = poly[idx[0]];
                Eigen::Vector2d dir = poly[idx[1]] - p0;
                double dirLen = dir.norm();
                if (dirLen > 1e-12) {
                    for (size_t m = 2; m < idx.size(); m++) {
                        double perp = std::abs(cross2(dir, poly[idx[m]] - p0)) / dirLen;
                        if (perp > dirLen * 1e-6 + 1e-9) { remainderDegenerate = false; break; }
                    }
                }
            }
            if (!remainderDegenerate) return false;
            return true;  // keep the triangles already found; zero-area remainder discarded
        }
    }
    if (idx.size() == 3) {
        out.push_back({keep[idx[0]], keep[idx[1]], keep[idx[2]]});
        return true;
    }
    return false;  // hit the iteration guard without resolving -- stay conservative
}

// Squared distance from (u,v) to the nearest point on a polyline (used
// as a fallback when the surface projection falls outside every loop --
// i.e. the query point is near an edge/corner rather than the face's
// interior). Optionally hands back that nearest point itself, so the
// caller can convert it to a real 3D distance via the surface (a
// parameter-space distance alone isn't dimensionally meaningful --
// e.g. a cylinder's u is an angle -- and mixing it with a real 3D
// length was a confirmed source of wrong-magnitude fallback results;
// see Face::evaluate's out-of-trim branch).
static double distToPolylineSq(const std::vector<Eigen::Vector2d>& poly, double u, double v,
                                Eigen::Vector2d* closestOut = nullptr)
{
    double best = 1e18;
    Eigen::Vector2d p(u, v);
    size_t n = poly.size();
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        Eigen::Vector2d a = poly[j], b = poly[i];
        Eigen::Vector2d ab = b - a;
        double t = ab.squaredNorm() > 1e-12 ? (p - a).dot(ab) / ab.squaredNorm() : 0.0;
        t = std::clamp(t, 0.0, 1.0);
        Eigen::Vector2d closest = a + t * ab;
        double d = (p - closest).squaredNorm();
        if (d < best) {
            best = d;
            if (closestOut) *closestOut = closest;
        }
    }
    return best;
}

#ifdef FIELDES_STEP_DEBUG_COUNTERS
#include <atomic>
std::atomic<int> g_vertexResolveFailures{0};
std::atomic<int> g_curveResolveFailures{0};
std::atomic<int> g_degenerateLoops{0};
#endif

static Vec3 resolveVertexPoint(const Document& doc, int id)
{
    const Entity* e = doc.get(id);
    if (e) {
        if (auto* args = e->find("VERTEX_POINT")) {
            if (args->size() >= 2) return resolvePoint(doc, (*args)[1].asRef());
        }
    }
#ifdef FIELDES_STEP_DEBUG_COUNTERS
    g_vertexResolveFailures++;
#endif
    return Vec3::Zero();
}

// Samples one ORIENTED_EDGE's curve (in the traversal direction implied
// by its orientation flag) and appends the samples' surface-parameter
// projections to `out`. `seed` is an in/out (u,v) hint: on entry, a
// nearby parameter-space point to start the surface projection search
// from (nullopt for the very first sample of a loop); on exit, updated
// to the last sample's (u,v) so the *next* edge can seed from it too.
// This is also what keeps resolving a large assembly's trimmed B-spline
// faces from being impractically slow -- see closestPointNear's doc
// comment.
//
// Cylinder/cone/sphere have an angular u (and torus both u and v) that
// wraps at +-pi: two boundary points a hair's-breadth apart on either
// side of that seam project to values almost 2*pi apart, which turns
// into one bogus edge spanning nearly the whole parameter space in the
// sampled loop polygon -- and *that* can make the point-in-polygon trim
// test misclassify a query point as inside a face it's nowhere near.
// CONFIRMED as a real bug, not just a theoretical one: a stray torus
// face's trim falsely matched a point 0.84 units away, producing a
// wrong-signed distance that showed up as visible spikes in a real
// mesh. Fixed here by unwrapping each new sample toward the running
// seed instead of always using the closed-form principal range.
// See declaration in step_face.hpp: factored out of
// sampleEdgeIntoParamSpace so both the winding-number boundary sampler
// and the tessellator (step_tessellate.cpp) resolve exactly the same
// arc/segment for exactly the same (hard-won, bug-fixed) reasons,
// instead of two independently-maintained copies drifting apart.
ResolvedEdgeUse resolveOrientedEdge(const Document& doc, const Entity& orientedEdge)
{
    ResolvedEdgeUse result;
    auto* oeArgs = orientedEdge.find("ORIENTED_EDGE");
    if (!oeArgs || oeArgs->size() < 5) return result;

    bool orientation = false;
    // orientation is stored as an enum '.T.'/'.F.' -> asString() "T"/"F"
    {
        const auto& val = (*oeArgs)[4];
        orientation = val.isString() && val.asString() == "T";
    }
    result.orientation = orientation;

    int edgeCurveId = (*oeArgs)[3].asRef();
    result.edgeCurveId = edgeCurveId;
    const Entity* ec = doc.get(edgeCurveId);
    if (!ec) return result;
    auto* ecArgs = ec->find("EDGE_CURVE");
    if (!ecArgs || ecArgs->size() < 4) return result;

    // rawV1/rawV2 are EDGE_CURVE's own declared endpoints, in ITS
    // intrinsic order -- independent of how this particular use
    // (ORIENTED_EDGE) traverses it. startP/endP are the actual
    // traversal order for *this* use (rawV1/rawV2 swapped when this
    // use is reversed), for sampling direction and the unsupported-
    // curve fallback below.
    Vec3 rawV1 = resolveVertexPoint(doc, (*ecArgs)[1].asRef());
    Vec3 rawV2 = resolveVertexPoint(doc, (*ecArgs)[2].asRef());
    Vec3 startP = rawV1, endP = rawV2;
    if (!orientation) std::swap(startP, endP);
    result.startP = startP;
    result.endP = endP;

    Curve curve = resolveCurve(doc, (*ecArgs)[3].asRef());
    if (!curve.valid()) {
        // Unsupported curve type (e.g. a raw polyline) -- fall back to a
        // straight segment between the known endpoints so the loop stays
        // closed, even if not perfectly accurate.
        Curve line;
        line.kind = CurveKind::Line;
        line.placement.origin = startP;
        line.placement.zAxis = (endP - startP).normalized();
        result.curve = line;
        result.t0 = 0.0;
        result.t1 = (endP - startP).norm();
        result.ok = true;
        return result;
    }

    double t0, t1;
    if (curve.hasExplicitTrim) {
        // The exporter told us exactly which basis-curve parameter
        // range this edge covers -- use it directly instead of
        // re-deriving it from the endpoints. That re-derivation is
        // fundamentally ambiguous for an arc of >=180 degrees (and
        // unreliable in general for any periodic curve): given just two
        // points on a circle, "which way around" isn't recoverable
        // without more information. CONFIRMED as a real, not just
        // theoretical, bug: a reversed-orientation half-circle edge
        // (sense_agreement effectively flips start/end) picked the far
        // (unintended) semicircle, *and* dragged every subsequent edge's
        // seam-unwrapped (u,v) samples in the same loop a full 2*pi off
        // with it (see project()'s seed-relative unwrapping above) --
        // corrupting the whole face's trim loop, not just this one edge.
        // Match trimA/trimB to start/end by proximity rather than
        // assuming an order, since sense_agreement's start/end
        // convention isn't being interpreted here -- just which literal
        // parameter value the real 3D endpoint actually is.
        double dAtoStart = (curve.evalAt(curve.trimA) - startP).squaredNorm();
        double dBtoStart = (curve.evalAt(curve.trimB) - startP).squaredNorm();
        if (dAtoStart <= dBtoStart) {
            t0 = curve.trimA;
            t1 = curve.trimB;
        } else {
            t0 = curve.trimB;
            t1 = curve.trimA;
        }
    } else {
        // CONFIRMED BUG this fixes: the old version computed t0/t1 (and
        // applied the "assume short way" wrap below) from startP/endP --
        // which is the *traversal* order for this particular use of the
        // edge, already reversed when orientation is F. That mixes two
        // genuinely different things: which arc the edge's curve
        // actually spans (an intrinsic property of the edge, tied to
        // EDGE_CURVE's own vertex1/vertex2 declaration) vs. which
        // direction *this loop* happens to traverse it. For a reversed
        // (orientation=F) edge, applying the wrap heuristic to the
        // already-swapped pair picks the arc on the *other* side of the
        // ambiguity from what the intrinsic vertex1->vertex2 pair would
        // -- for a >=180 degree arc, a genuinely different (wrong) arc
        // entirely. CONFIRMED against a real file (HingedTable.step, no
        // TRIMMED_CURVE entities at all, so every circular edge goes
        // through this exact path): reversed arc edges here previously
        // resolved to the wrong side of a CYLINDRICAL_SURFACE/CONICAL_
        // SURFACE/SPHERICAL_SURFACE's seam, which (combined with the
        // loops.empty()-means-untrimmed fallback and/or a badly wrong
        // trim loop) let a small fillet/hinge surface's full underlying
        // analytic surface leak in as if untrimmed, ballooning a local
        // feature into a dome that swallowed the real geometry.
        // Fixed by resolving the intrinsic arc from the edge's own
        // (unswapped) rawV1/rawV2 first, then choosing which end to
        // start sampling from based on orientation -- so the *set* of
        // parameters covered never depends on traversal direction, only
        // the order they're visited in.
        double tRaw0 = curve.closestPoint(rawV1).t;
        double tRaw1 = curve.closestPoint(rawV2).t;
        // EDGE_CURVE(name, start, end, geometry, same_sense): with
        // same_sense .F. the edge runs from start to end AGAINST the
        // curve's own parameter direction, so on a circle the arc is the
        // one reached by DECREASING t. Ignoring the flag took the
        // complementary arc for every such edge -- a 90-degree fillet edge
        // became a 270-degree one, so the fillet's trimmed patch was the
        // concave three quarters of its cylinder and the rounded edges of
        // a part read as carved grooves (confirmed on Bandextruder.stp,
        // the first test file with any .F. edges: 158 of them).
        bool sameSense = true;
        if (ecArgs->size() >= 5) {
            const auto& sv = (*ecArgs)[4];
            sameSense = !(sv.isString() && sv.asString() == "F");
        }
        if (curve.kind == CurveKind::Circle || curve.kind == CurveKind::Ellipse) {
            if (sameSense) {
                if (tRaw1 <= tRaw0 + 1e-9) tRaw1 += 2 * M_PI;
            } else {
                if (tRaw1 >= tRaw0 - 1e-9) tRaw1 -= 2 * M_PI;
            }
        }
        // A closed B-spline edge (one vertex at both ends, e.g. where two
        // cylinders meet all the way round) is the whole curve: projecting
        // its vertex gave the same parameter twice, an empty range, and
        // the loop -- the hole one cylinder cuts in the other -- collapsed
        // to a single (u, v) point, so the face read as uncut there.
        if (curve.kind == CurveKind::BSpline && curve.tMax > curve.tMin) {
            const double size = std::max(1e-12, (curve.evalAt(curve.tMin) -
                                                 curve.evalAt(0.5 * (curve.tMin + curve.tMax))).norm());
            const bool closedEdge = (rawV1 - rawV2).norm() < 1e-7 * size;
            const bool closedCurve = (curve.evalAt(curve.tMin) - curve.evalAt(curve.tMax)).norm() < 1e-5 * size;
            if (closedEdge && closedCurve && std::abs(tRaw1 - tRaw0) < 1e-9 * (curve.tMax - curve.tMin)) {
                tRaw0 = sameSense ? curve.tMin : curve.tMax;
                tRaw1 = sameSense ? curve.tMax : curve.tMin;
            }
        }
        t0 = orientation ? tRaw0 : tRaw1;
        t1 = orientation ? tRaw1 : tRaw0;
    }
    result.curve = curve;
    result.t0 = t0;
    result.t1 = t1;
    result.ok = true;
    return result;
}

static void sampleEdgeIntoParamSpace(const Document& doc, const Entity& orientedEdge,
                                      const Surface& surf, std::vector<Eigen::Vector2d>& out,
                                      std::optional<Eigen::Vector2d>& seed,
                                      Vec3& boundMin, Vec3& boundMax,
                                      std::vector<EdgeSpan>* outEdges)
{
    const bool wrapsU = surf.kind == SurfaceKind::Cylinder || surf.kind == SurfaceKind::Cone ||
                         surf.kind == SurfaceKind::Sphere || surf.kind == SurfaceKind::Torus;
    const bool wrapsV = surf.kind == SurfaceKind::Torus;

    auto project = [&](const Vec3& p3) -> Eigen::Vector2d {
        boundMin = boundMin.cwiseMin(p3);
        boundMax = boundMax.cwiseMax(p3);
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
        return *seed;
    };

    ResolvedEdgeUse use = resolveOrientedEdge(doc, orientedEdge);
    if (!use.ok) {
#ifdef FIELDES_STEP_DEBUG_COUNTERS
        g_curveResolveFailures++;
#endif
        return;
    }
    Curve& curve = use.curve;
    double t0 = use.t0, t1 = use.t1;
    int samples = 2;
    // CONFIRMED BUG this section fixes: Face::boundMin/boundMax (used by
    // boundingDistance(), which the grid and the sorted early-skip both
    // rely on as a genuine lower bound) were built only from these
    // discrete curve samples. Between two samples, a circle/ellipse/
    // B-spline arc can bulge outward past the straight chord connecting
    // them -- so the box could be too tight, making boundingDistance()
    // overestimate the true minimum distance and let the skip logic
    // throw away the actual closest face. Verified with a dedicated
    // brute-force-vs-"sorted skip over ALL faces, no grid at all" A/B
    // test: they still disagreed by the same amount as grid-vs-brute,
    // proving the bug lived here, not in the grid or the skip logic
    // (which is mathematically sound *given* a genuine lower bound).
    // Fixed with denser sampling plus, for circle/ellipse, an exact
    // closed-form sagitta margin for whatever gap remains between
    // samples.
    double sagitta = 0;
    if (curve.kind == CurveKind::Circle || curve.kind == CurveKind::Ellipse) {
        // t0/t1's direction (which may legitimately have t1 < t0, for a
        // reversed/orientation=F edge -- see above) is already fully
        // resolved by this point, both for the explicit-trim and the
        // heuristic path; nothing left to guess here.
        samples = 64;
        double halfStep = (t1 - t0) / samples / 2;
        double maxRadius = std::max(curve.radius, curve.radius2);
        sagitta = maxRadius * (1 - std::cos(halfStep));
    } else if (curve.kind == CurveKind::BSpline) {
        samples = 48;
    }
    for (int i = 0; i <= samples; i++) {
        double t = t0 + (t1 - t0) * i / samples;
        out.push_back(project(curve.evalAt(t)));
    }
    if (sagitta > 0) {
        boundMin -= Vec3::Constant(sagitta);
        boundMax += Vec3::Constant(sagitta);
    }
    if (outEdges) {
        EdgeSpan span;
        span.curve = curve;
        span.t0 = t0;
        span.t1 = t1;
        span.edgeCurveId = use.edgeCurveId;
        span.pStart = use.startP;
        span.pEnd = use.endP;
        span.hasEnds = true;
        outEdges->push_back(std::move(span));
    }
}

static Loop resolveLoop(const Document& doc, int boundId, const Surface& surf, bool& isOuterOut)
{
    Loop loop;
    const Entity* be = doc.get(boundId);
    if (!be) return loop;

    const ValueList* args = be->find("FACE_OUTER_BOUND");
    isOuterOut = true;
    if (!args) {
        args = be->find("FACE_BOUND");
        isOuterOut = false;
    }
    if (!args || args->size() < 3) return loop;

    // orientation flag on the bound itself (rare to be false, but honor it)
    bool boundOrientation = (*args)[2].isString() && (*args)[2].asString() == "T";

    const Entity* loopE = doc.get((*args)[1].asRef());
    if (!loopE) return loop;
    auto* leArgs = loopE->find("EDGE_LOOP");
    // EDGE_LOOP(name, edge_list) -- same name-then-data layout as
    // CLOSED_SHELL above.
    if (!leArgs || leArgs->size() < 2) return loop;

    std::vector<Eigen::Vector2d> pts;
    std::optional<Eigen::Vector2d> seed;  // carried from edge to edge; see its use above
    for (auto& edgeRef : (*leArgs)[1].asList()) {
        const Entity* oe = doc.get(edgeRef.asRef());
        if (oe) sampleEdgeIntoParamSpace(doc, *oe, surf, pts, seed, loop.boundMin, loop.boundMax, &loop.edges);
    }
    if (!boundOrientation) {
        std::reverse(pts.begin(), pts.end());
        // Reversing the loop's overall traversal direction means both
        // the edge *order* and each individual edge's own t0/t1 (its
        // internal direction) must flip, or windingContribution would
        // integrate this loop backwards relative to the (correctly
        // reversed) trim polygon `pts` uses for the nearest-face test --
        // two different notions of "which way this loop winds" that must
        // agree, since both ultimately have to describe the same
        // physical boundary curve.
        std::reverse(loop.edges.begin(), loop.edges.end());
        for (auto& e : loop.edges) std::swap(e.t0, e.t1);
    }
    if (!loop.edges.empty()) {
        Vec3 sum = Vec3::Zero();
        for (auto& e : loop.edges) sum += e.curve.evalAt(e.t0);
        loop.centroid = sum / static_cast<double>(loop.edges.size());

        // Whole-loop orientation, applied uniformly to every edge: does
        // this loop's fan-from-centroid, taken as a whole, agree with the
        // surface's own natural normal, or does it need reversing?
        //
        // CONFIRMED BUG this fixes: an earlier version tested each edge
        // independently (triangle [centroid, evalAt(t0), evalAt(t1)]
        // against the surface normal at that edge's own midpoint) and
        // flipped only the disagreeing ones -- reasoning by analogy to a
        // *different*, already-fixed bug where per-triangle orientation
        // needed checking individually for an EAR-CLIPPED triangulation's
        // interior diagonal edges. That reasoning doesn't transfer to a
        // simple boundary fan: for a concave (L-shaped, e.g. a box
        // corner notch) loop, the centroid can fall *outside* the true
        // material footprint, in the concave notch itself -- and from an
        // exterior apex, individual per-edge orientation tests are not
        // guaranteed consistent even though the loop's overall winding
        // direction is perfectly well-defined. Confirmed directly: a
        // notched test box's L-shaped face got some edges flipped and
        // others not, corrupting that face's total contribution and
        // making a point *inside the cut-away notch* read as confidently
        // inside the solid. Fixed by aggregating all edges' contribution
        // to a single test vector first, then flipping the whole loop
        // uniformly -- correct for both convex and concave loops, since
        // it no longer depends on where any individual edge's fan
        // triangle happens to point relative to an apex that might not
        // even be inside the loop.
        Vec3 aggregate = Vec3::Zero();
        for (auto& e : loop.edges) {
            Vec3 a = e.curve.evalAt(e.t0);
            Vec3 b = e.curve.evalAt(e.t1);
            aggregate += (a - loop.centroid).cross(b - loop.centroid);
        }
        Surface::Closest c = surf.closestPoint(loop.centroid);
        Vec3 natural = surf.normalAt(c.u, c.v);
        bool reversed = aggregate.dot(natural) < 0;
        for (auto& e : loop.edges) e.reversed = reversed;
    }

#ifdef FIELDES_STEP_DEBUG_COUNTERS
    {
        bool allSame = true;
        for (size_t i = 1; i < pts.size(); i++) {
            if ((pts[i] - pts[0]).norm() > 1e-9) { allSame = false; break; }
        }
        if (pts.size() < 3 || allSame) g_degenerateLoops++;
    }
#endif

    loop.isOuter = isOuterOut;
    loop.uv = std::move(pts);
    return loop;
}

Face resolveFace(const Document& doc, int advancedFaceId)
{
    Face f;
    const Entity* e = doc.get(advancedFaceId);
    if (!e) return f;
    auto* args = e->find("ADVANCED_FACE");
    if (!args || args->size() < 4) return f;

    f.surface = resolveSurface(doc, (*args)[2].asRef());
    if (!f.surface.valid()) return f;

    f.sameSense = (*args)[3].isString() && (*args)[3].asString() == "T";

    for (auto& boundRef : (*args)[1].asList()) {
        bool isOuter = true;
        Loop loop = resolveLoop(doc, boundRef.asRef(), f.surface, isOuter);
        if (!loop.uv.empty()) {
            f.boundMin = f.boundMin.cwiseMin(loop.boundMin);
            f.boundMax = f.boundMax.cwiseMax(loop.boundMax);
            f.loops.push_back(std::move(loop));
        }
    }

    // Full-revolution cylinder/cone band (a drilled hole or countersink
    // wall): two circular loops, each at constant v spanning a full turn.
    // resolveLoop normalises each loop's fan orientation against the
    // surface normal at the LOOP CENTROID -- but a circle's centroid lies
    // on the axis, where the closest surface point (and so the normal) is
    // arbitrary, so the two circles got arbitrary relative orientations and
    // the face's winding-number contribution came out wrong (the oracle
    // read the hole as solid). STEP already defines each loop's direction
    // relative to the face's outward normal, consistently for both
    // circles, so use it directly: relative to the surface normal that is
    // the stored direction when sameSense, reversed otherwise. Both
    // circles then add with the same sign (their opposite traversal is
    // what makes the band's solid angle the difference of the two discs).
    if ((f.surface.kind == SurfaceKind::Cylinder || f.surface.kind == SurfaceKind::Cone) && f.loops.size() == 2) {
        bool band = true;
        for (const Loop& L : f.loops) {
            if (L.uv.size() < 8) { band = false; break; }
            double umin = 1e18, umax = -1e18, vmin = 1e18, vmax = -1e18;
            for (const auto& q : L.uv) {
                umin = std::min(umin, q.x()); umax = std::max(umax, q.x());
                vmin = std::min(vmin, q.y()); vmax = std::max(vmax, q.y());
            }
            double vtol = 1e-6 * (std::abs(vmin) + std::abs(vmax) + std::abs(f.surface.radius));
            if (vmax - vmin > vtol || umax - umin < 2.0 * M_PI - 0.05) { band = false; break; }
        }
        if (band) {
            for (Loop& L : f.loops) {
                L.isOuter = true;
                for (auto& ed : L.edges) ed.reversed = f.sameSense;
            }
        }
    }

    // The boundary curve's own 3D extent isn't always a safe bound for
    // the *surface* between those boundary points: a curved face can
    // bulge outward past the chord its boundary cuts (most visibly, a
    // cylindrical/conical/spherical/toroidal face spanning more than
    // ~180 degrees, or a B-spline patch whose control points overshoot
    // the curve). An earlier version tried per-surface-type analytic
    // margins here (surface radius for analytic types, a fixed percent
    // for B-spline); that was real but insufficient -- confirmed via a
    // brute-force A/B test that kept disagreeing by the same amount
    // through several rounds of tuning those margins. Sampling the
    // surface directly over the trim's own (u,v) footprint, uniformly
    // for every surface kind, is what actually converged: it directly
    // captures each surface's true shape (including a cone's radius
    // growing with height, a torus's compound curvature, etc.) instead
    // of approximating it, so there's no per-type formula to get wrong.
    // Not a mathematically airtight bound on its own -- hence the
    // generous margin on top -- but reliably safe in practice.
    {
        double uLo = 1e18, uHi = -1e18, vLo = 1e18, vHi = -1e18;
        for (auto& loop : f.loops) {
            for (auto& p : loop.uv) {
                uLo = std::min(uLo, p.x()); uHi = std::max(uHi, p.x());
                vLo = std::min(vLo, p.y()); vHi = std::max(vHi, p.y());
            }
        }
        const int N = 20;
        if (uLo <= uHi && vLo <= vHi) {
            for (int i = 0; i <= N; i++) {
                for (int j = 0; j <= N; j++) {
                    Vec3 s = f.surface.evalParam(uLo + (uHi - uLo) * i / N,
                                                  vLo + (vHi - vLo) * j / N);
                    f.boundMin = f.boundMin.cwiseMin(s);
                    f.boundMax = f.boundMax.cwiseMax(s);
                }
            }
        }

        // A loop winding once round the axis of a cone, sphere or torus
        // (the rim of a drill point, a dome, a ball end, a fillet ring)
        // doesn't enclose the face in (u, v): the face runs from that loop
        // to the apex / pole / the other loop, which the loop's own (u, v)
        // footprint above never reaches -- a drill point's box was just its
        // rim circle, and every ray crossing lower down the cone was thrown
        // away by the box test, so the tip read as solid. Sample the whole
        // surface over its natural range and keep what the trim test
        // (evaluate(), which knows winding loops) accepts.
        const SurfaceKind k = f.surface.kind;
        bool winds = false;
        for (const auto& loop : f.loops) {
            if (loop.uv.size() >= 3 &&
                std::abs(std::abs(loop.uv.back().x() - loop.uv.front().x()) - 2.0 * M_PI) < 0.5) {
                winds = true;
            }
        }
        if (winds && (k == SurfaceKind::Cone || k == SurfaceKind::Sphere || k == SurfaceKind::Torus) &&
            vLo <= vHi) {
            double va = vLo, vb = vHi;
            if (k == SurfaceKind::Cone) {
                const double ta = std::tan(f.surface.semiAngle);
                if (std::abs(ta) > 1e-12) {
                    const double apex = -f.surface.radius / ta;
                    va = std::min(va, apex);
                    vb = std::max(vb, apex);
                }
            } else if (k == SurfaceKind::Sphere) {
                va = -M_PI / 2;
                vb = M_PI / 2;
            } else {
                va = -M_PI;
                vb = M_PI;
            }
            const int M = 48;
            for (int i = 0; i < M; i++) {
                for (int j = 0; j <= M; j++) {
                    const Vec3 s = f.surface.evalParam(-M_PI + 2.0 * M_PI * i / M,
                                                        va + (vb - va) * j / M);
                    if (f.inTrim(s)) {
                        f.boundMin = f.boundMin.cwiseMin(s);
                        f.boundMax = f.boundMax.cwiseMax(s);
                    }
                }
            }
            // what the samples may miss between them (the sagitta of a step)
            const double r = 0.5 * (f.boundMax - f.boundMin).norm();
            const double gap = r * (1.0 - std::cos(M_PI / M));
            f.boundMin -= Vec3::Constant(gap);
            f.boundMax += Vec3::Constant(gap);
        }
        // Margin for whatever the NxN grid above still misses *between*
        // samples. Skipped entirely for planes: evalParam() is exactly
        // linear in (u,v), so the grid has zero interpolation error and
        // a plane's boundary edges (straight lines) are sampled exactly
        // too -- no bulge is possible for a flat surface. Confirmed via
        // a hand-built test cube: a flat 20%-of-diagonal margin here was
        // inflating its bbox from an exact [0,0,0]..[10,10,10] out to
        // roughly [-2.8,-2.8,-2.8]..[12.8,12.8,12.8].
        //
        // For curved surfaces, an earlier version used `radius +
        // radius2` as a floor on top of a 20%-of-diagonal term -- real
        // (it fixed a genuine brute-force-vs-grid disagreement when this
        // whole block was first added), but far more than needed: with N
        // samples across an angular span, the true worst-case gap
        // between adjacent samples is the *sagitta* of one step, i.e.
        // radius*(1-cos(halfstep)) -- the same exact quantity already
        // used for CIRCLE/ELLIPSE edge sampling below. For N=20 that's
        // on the order of 1% of the radius, not 100% of it. Confirmed
        // the old floor was the actual bottleneck: instrumenting
        // libfive's own octree mesher (dc_tree.inl, not this file)
        // showed a single cylindrical face's inflated bbox was making
        // the "is this cell provably far from any surface" check
        // (Model::boxLowerBound, step_model.cpp) fail for a much larger
        // region around the surface than geometrically necessary, which
        // is what forced excess octree subdivision for specific domain/
        // resolution combinations against a vaulted test shape.
        double angMargin = 0;
        bool haveAngMargin = false;
        if (uHi > uLo || vHi > vLo) {
            const double halfU = (uHi - uLo) / (2.0 * N);
            const double halfV = (vHi - vLo) / (2.0 * N);
            switch (f.surface.kind) {
                case SurfaceKind::Cylinder:
                case SurfaceKind::Cone:
                    // u is angular (sweep radius up to `radius` plus the
                    // cone's growth with v); v is linear, no bulge.
                    angMargin = (f.surface.radius +
                                 std::max(0.0, f.surface.radius2)) *
                                (1 - std::cos(halfU));
                    haveAngMargin = true;
                    break;
                case SurfaceKind::Sphere:
                    // u sweeps at up to `radius` (widest at the equator);
                    // v sweeps the meridian, also radius `radius`.
                    angMargin = std::max(
                        f.surface.radius * (1 - std::cos(halfU)),
                        f.surface.radius * (1 - std::cos(halfV)));
                    haveAngMargin = true;
                    break;
                case SurfaceKind::Torus:
                    // u sweeps the main radius (up to radius+radius2);
                    // v sweeps the tube (radius2).
                    angMargin = std::max(
                        (f.surface.radius + f.surface.radius2) * (1 - std::cos(halfU)),
                        f.surface.radius2 * (1 - std::cos(halfV)));
                    haveAngMargin = true;
                    break;
                default:
                    // B-spline: no closed-form sagitta available (the
                    // patch isn't a simple analytic sweep), so keep the
                    // original empirical 20%-of-diagonal margin below --
                    // unchanged from before, still the proven-safe value
                    // for this case specifically.
                    break;
            }
        }
        // For the analytic sweep kinds, angMargin is a rigorous (not
        // empirical) upper bound on the inter-sample gap, and is far
        // tighter than the old `radius + radius2` floor -- it fully
        // replaces both the floor and the general 20%-of-diagonal term
        // for these kinds. B-spline (and the degenerate case of a
        // pointlike trim, uHi==uLo && vHi==vLo) still fall back to the
        // original, unmodified 20%-of-diagonal empirical margin.
        double margin = (f.surface.kind == SurfaceKind::Plane) ? 1e-6
            : haveAngMargin ? angMargin + 1e-6
            : (f.boundMax - f.boundMin).norm() * 0.20 + 1e-6;
        f.boundMin -= Vec3::Constant(margin);
        f.boundMax += Vec3::Constant(margin);
    }

    if (f.surface.kind == SurfaceKind::BSpline) {
        auto patch = std::make_shared<BSplinePatch>(f.surface);
        if (patch->ok()) {
            f.patch = patch;
            const double domain = (patch->u1 - patch->u0) * (patch->v1 - patch->v0);
            double area = 0;
            for (auto& loop : f.loops) {
                double a = 0;
                for (size_t i = 0; i < loop.uv.size(); i++) {
                    const auto& p = loop.uv[i];
                    const auto& q = loop.uv[(i + 1) % loop.uv.size()];
                    a += p.x() * q.y() - q.x() * p.y();
                }
                area = std::max(area, std::abs(a) / 2);
            }
            f.fullPatch = !(area > 1e-6 * domain);
        }
    }
    return f;
}

// Whether c (the projection of a point onto this face's surface) lies in
// the face's trimmed region; with withDist, also the nearest point of the
// trim loops (in (u, v)).  Shared by evaluate() and inTrim().
void Face::trimState(const Surface::Closest& c, bool withDist, bool& inOuter, bool& inHole,
                     double& bestBoundaryDistSq, Eigen::Vector2d& bestBoundaryUV) const
{
    const bool wrapsU = surface.kind == SurfaceKind::Cylinder || surface.kind == SurfaceKind::Cone ||
                         surface.kind == SurfaceKind::Sphere || surface.kind == SurfaceKind::Torus;
    const bool wrapsV = surface.kind == SurfaceKind::Torus;

    inOuter = loops.empty();  // no loops at all => untrimmed (treat as always in)
    inHole = false;
    bestBoundaryDistSq = 1e18;
    bestBoundaryUV = Eigen::Vector2d::Zero();

    for (auto& loop : loops) {
        bool hit = false;
        double localBestDistSq = 1e18;
        for (double du : (wrapsU ? std::initializer_list<double>{-2 * M_PI, 0, 2 * M_PI}
                                  : std::initializer_list<double>{0.0})) {
            for (double dv : (wrapsV ? std::initializer_list<double>{-2 * M_PI, 0, 2 * M_PI}
                                      : std::initializer_list<double>{0.0})) {
                double u = c.u + du, v = c.v + dv;
                hit = hit || (!loop.outsideBox(u, v) && pointInPolygon(loop.uv, u, v));
                if (!withDist) continue;
                Eigen::Vector2d closest;
                double d = distToPolylineSq(loop.uv, u, v, &closest);
                localBestDistSq = std::min(localBestDistSq, d);
                if (d < bestBoundaryDistSq) {
                    bestBoundaryDistSq = d;
                    bestBoundaryUV = closest;
                }
            }
        }
        if (loop.isOuter) inOuter = inOuter || hit;
        else inHole = inHole || hit;
    }

    // Loops that wind once around the axis of a surface of revolution (a
    // circle around a drilled hole, the rim of a dome, a hexagonal socket
    // mouth cut into a cone, a corner ball's boundary around its pole) are
    // not closed in (u,v): u advances by a full turn, so point-in-polygon
    // against them is meaningless (a constant-v circle is a zero-area
    // "polygon" that is never inside). Such a face's region is instead:
    //   - two winding loops, v not periodic (cylinder, cone, sphere): the
    //     band between the two curves;
    //   - one winding loop (a sphere cap around a pole, a cone around its
    //     apex): the side of the curve the face lies on, which STEP fixes by
    //     the loop's direction -- the face is on the LEFT of each loop seen
    //     along the face normal, i.e. above the curve (larger v) when the
    //     loop runs toward +u and the face normal agrees with the (u,v)
    //     normal of the analytic surface;
    //   - two winding loops on a torus (v periodic too): the arc of the
    //     tube from the first loop, in its interior direction, to the other.
    // Any non-winding loops of such a face are holes in it.
    // The earlier special case covered only cylinders/cones bounded by two
    // constant-v circles; a button head screw's dome, under-head fillet and
    // socket cones all fell through it and were never in trim.
    if (wrapsU) {
        std::vector<int> windLoops;
        std::vector<int> windDir;
        for (int li = 0; li < int(loops.size()); li++) {
            const auto& L = loops[li];
            if (L.uv.size() < 3) continue;
            double du = L.uv.back().x() - L.uv.front().x();
            if (std::abs(std::abs(du) - 2.0 * M_PI) < 0.5) {
                windLoops.push_back(li);
                windDir.push_back(du > 0 ? 1 : -1);
            }
        }
        // All crossings of the vertical line through (c.u + k 2pi) with a
        // winding loop's polyline (no closing segment: its ends are one
        // turn apart). Returns the v values.
        auto crossingsAt = [&](const Loop& L) {
            std::vector<double> vs;
            for (size_t i = 0; i + 1 < L.uv.size(); i++) {
                const auto& a = L.uv[i];
                const auto& b = L.uv[i + 1];
                double lo = std::min(a.x(), b.x()), hi = std::max(a.x(), b.x());
                if (hi - lo < 1e-15) continue;
                for (int k = -3; k <= 3; k++) {
                    double uq = c.u + 2.0 * M_PI * k;
                    if (uq < lo || uq >= hi) continue;
                    vs.push_back(a.y() + (uq - a.x()) * (b.y() - a.y()) / (b.x() - a.x()));
                }
            }
            return vs;
        };
        auto isBelow = [&](const Loop& L) {  // odd number of curve crossings above the point
            int above = 0;
            for (double v : crossingsAt(L)) if (v > c.v) above++;
            return (above % 2) == 1;
        };
        const double faceSign = (sameSense != surface.orientationUncertain) ? 1.0 : -1.0;
        bool handled = false;
        bool inside = false;
        if (!wrapsV && windLoops.size() == 2) {
            inside = isBelow(loops[windLoops[0]]) != isBelow(loops[windLoops[1]]);
            handled = true;
        } else if (!wrapsV && windLoops.size() == 1) {
            const bool interiorUp = windDir[0] * faceSign > 0;
            inside = interiorUp ? !isBelow(loops[windLoops[0]]) : isBelow(loops[windLoops[0]]);
            handled = true;
        } else if (wrapsV && windLoops.size() == 2) {
            std::vector<double> va = crossingsAt(loops[windLoops[0]]);
            std::vector<double> vb = crossingsAt(loops[windLoops[1]]);
            if (!va.empty() && !vb.empty()) {
                const double dA = windDir[0] * faceSign > 0 ? 1.0 : -1.0;
                auto dist = [&](double from, double to) {
                    double d = std::fmod((to - from) * dA, 2.0 * M_PI);
                    return d < 0 ? d + 2.0 * M_PI : d;
                };
                inside = dist(va[0], c.v) <= dist(va[0], vb[0]);
                handled = true;
            }
        }
        if (handled) {
            // Non-winding loops of a face bounded by winding loops are holes.
            bool inAnyHole = false;
            for (int li = 0; li < int(loops.size()); li++) {
                if (std::find(windLoops.begin(), windLoops.end(), li) != windLoops.end()) continue;
                for (double du : {-2 * M_PI, 0.0, 2 * M_PI}) {
                    if (!loops[li].outsideBox(c.u + du, c.v) &&
                        pointInPolygon(loops[li].uv, c.u + du, c.v)) { inAnyHole = true; break; }
                }
            }
            inOuter = inside;
            inHole = inAnyHole;
        }
    }

}

bool Face::inTrim(const Vec3& p) const
{
    if (!valid()) return false;
    const Surface::Closest c = surface.closestPoint(p);
    bool inOuter = false, inHole = false;
    double d2 = 0;
    Eigen::Vector2d uv;
    trimState(c, false, inOuter, inHole, d2, uv);
    return inOuter && !inHole;
}

}  // namespace step
}  // namespace libfive
