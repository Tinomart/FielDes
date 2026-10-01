/*
libfive: a CAD kernel for modeling with implicit functions

A trimmed STEP face: a Surface plus one or more boundary loops (an outer
loop, and zero or more inner "hole" loops), built by walking
ADVANCED_FACE -> FACE_(OUTER_)BOUND -> EDGE_LOOP -> ORIENTED_EDGE ->
EDGE_CURVE. Each loop's boundary is sampled into the surface's own (u,v)
parameter space (closed-form for analytic surfaces), so containment is a
plain 2D point-in-polygon test -- no mesh, no trimmed-NURBS-intersection
math.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#pragma once

#include <array>
#include <memory>
#include <vector>

#include "libfive/step/step_geometry.hpp"
#include "libfive/step/step_bspline.hpp"

namespace libfive {
namespace step {

// One edge of a loop's boundary, kept as its real analytic curve (line,
// circle, ellipse, or B-spline) plus the parameter range this specific
// edge covers -- not pre-flattened into points. See Loop::edges: this is
// what lets winding-number contributions be computed adaptively at query
// time (finer sampling only where a query point is actually close enough
// to a curved edge for its curvature to matter), instead of every edge
// paying for a fixed sample density regardless of who's asking.
struct EdgeSpan
{
    Curve curve;
    double t0 = 0, t1 = 0;
    // Whether this edge's (t0 -> t1) traversal direction, fanned from
    // the loop's centroid, needs to be swapped to match the surface's
    // own outward-normal convention -- determined once per edge at
    // import time (see resolveLoop), not re-derived per query-time
    // sub-segment. Per-edge (not per-whole-loop) because a loop mixing
    // straight and curved edges under different surface parametrization
    // quirks isn't guaranteed uniform -- see windingContribution's doc
    // comment for the bug this granularity fixes.
    bool reversed = false;
    // The EDGE_CURVE entity id this span came from (see
    // ResolvedEdgeUse). -1 for the rare case an edge couldn't be
    // resolved to a real ORIENTED_EDGE at all. Not used by the winding-
    // number path; the tessellator (step_tessellate.cpp) uses it to key
    // a shared, watertight 3D sampling cache across the two faces that
    // meet along a manifold edge.
    int edgeCurveId = -1;
    // The edge's own VERTEX_POINTs in this use's t0 -> t1 order: the
    // tessellator puts these (not curve.evalAt(t0 / t1), which differs
    // from edge to edge by rounding) at the ends of the sampled edge, so
    // every edge meeting at a vertex ends on the bit-identical point.
    Vec3 pStart = Vec3::Zero(), pEnd = Vec3::Zero();
    bool hasEnds = false;
};

// One ORIENTED_EDGE's resolved curve and *this particular use's*
// parameter range (t0 -> t1, already oriented so curve.evalAt(t0) is
// this use's traversal start and curve.evalAt(t1) its end). Shared by
// the winding-number boundary sampler (step_face.cpp) and the
// tessellator (step_tessellate.cpp) so both resolve exactly the same
// arc/segment for exactly the same (hard-won, bug-fixed) reasons.
// edgeCurveId is the stable EDGE_CURVE entity id -- the same id is
// shared by both ORIENTED_EDGE uses of a manifold edge (one per
// adjacent face), which is what lets the tessellator key a shared,
// watertight 3D sampling cache by it.
struct ResolvedEdgeUse
{
    bool ok = false;
    int edgeCurveId = -1;
    bool orientation = false;
    Curve curve;         // real curve, or a synthetic straight Line for an unsupported curve type
    double t0 = 0, t1 = 0;
    Vec3 startP = Vec3::Zero(), endP = Vec3::Zero();   // vertices, traversal order
};

// Resolves one ORIENTED_EDGE entity (id `orientedEdgeId`) into its
// EDGE_CURVE identity, curve, and this use's parameter range.
ResolvedEdgeUse resolveOrientedEdge(const Document& doc, const Entity& orientedEdge);

// Ear-clipping triangulation of a simple (non-self-intersecting,
// hole-free) 2D polygon, appending each ear's three POLYGON-INDEX
// triples to `out`. Returns false (and may leave `out` partially
// filled) if the polygon is degenerate or clipping stalls. See the
// definition in step_face.cpp for the dedup/degenerate-input handling
// this required in practice against real STEP boundary loops. Shared by
// the winding-number face resolver and the tessellator
// (step_tessellate.cpp), which bridges hole loops into the outer
// boundary before calling this.
bool triangulatePolygon(const std::vector<Eigen::Vector2d>& fullPoly,
                         std::vector<std::array<int, 3>>& out);

// Ray-casting point-in-polygon test (even-odd rule) in 2D, on a single
// loop's UV boundary (Loop::uv). Exposed so callers outside step_face.cpp
// can test trim-region membership (e.g. step_reconstruct.cpp, to find
// which of two candidate directions actually leads into a face's own
// trimmed interior) without duplicating this logic.
bool pointInPolygon(const std::vector<Eigen::Vector2d>& poly, double u, double v);

struct Loop
{
    bool isOuter = true;
    // Boundary polyline in the surface's parameter space, closed
    // (last point need not repeat the first). Used only for the
    // point-in-polygon trim test (Face::inTrim) --
    // winding-number contributions use `edges` instead (see
    // Face::windingContribution).
    std::vector<Eigen::Vector2d> uv;
    // Same boundary, in the same edge order as `uv` was built from, but
    // kept as real curves instead of flattened samples.
    std::vector<EdgeSpan> edges;
    // 3D centroid of the loop's boundary (coarse average, just used as
    // a well-conditioned fan apex -- see windingContribution's doc
    // comment for why the exact point doesn't matter).
    Vec3 centroid = Vec3::Zero();
    // 3D bounding box of the same boundary, used to build the owning
    // Face's boundMin/boundMax.
    Vec3 boundMin = Vec3::Constant(1e18);
    Vec3 boundMax = Vec3::Constant(-1e18);

    // The polyline's box in (u, v), made once the face is final (makeBox): a
    // point outside it is outside the polygon, so the trim test can skip the
    // loop -- a perforated plate has hundreds of hole loops and every ray hit
    // on it used to walk all their vertices
    double uMin = 0, uMax = 0, vMin = 0, vMax = 0;
    size_t boxPoints = 0;       // uv.size() when the box was made (0: there is none)
    void makeBox();
    bool outsideBox(double u, double v) const
    {
        return boxPoints >= 3 && boxPoints == uv.size() &&
               (u < uMin || u > uMax || v < vMin || v > vMax);
    }
};

struct FittedSurface;   // (step_fit.hpp)

struct Face
{
    Surface surface;
    std::vector<Loop> loops;
    // True if this face's ADVANCED_FACE.same_sense agreed with the
    // surface's own normal convention (see step_face.cpp for how that
    // maps to a sign on distance()).
    bool sameSense = true;

    // Axis-aligned bounding box of the face's boundary (built while
    // sampling its loops -- see resolveFace). Solid::evaluate uses this
    // for a cheap per-query-point lower bound, to skip the real
    // trim+distance computation for faces that can't possibly be the
    // closest one. Without this, evaluating a solid means fully
    // evaluating *every* one of its faces for *every* query point
    // during meshing, which is the dominant cost for any model with a
    // few hundred faces or more (confirmed: this is what made meshing
    // a 602-face file impractically slow, far more than the B-spline
    // search cost that closestPointNear/seedCache address separately).
    Vec3 boundMin = Vec3::Constant(1e18);
    Vec3 boundMax = Vec3::Constant(-1e18);

    // B-spline faces: the patch (evaluation for the fitting, ray crossings
    // for the inside / outside test), and whether the face is the whole
    // patch -- its trim loops, projected into parameter space,
    // enclose no area, as for a corner blend bounded by the patch's own
    // (partly collapsed) edges
    std::shared_ptr<const BSplinePatch> patch;
    // ... and the closed-form surface fitted to it (see step_fit.hpp), set
    // once per face by the reconstruction
    mutable std::shared_ptr<const FittedSurface> fit;
    bool fullPatch = false;

    bool valid() const { return surface.valid(); }

    // Lower bound on the distance from p to any point on this face's
    // actual (trimmed) surface -- cheap (point-to-box), always <= the
    // true distance, used to skip full evaluation.
    double boundingDistance(const Vec3& p) const
    {
        Vec3 c = p.cwiseMax(boundMin).cwiseMin(boundMax);
        return (p - c).norm();
    }

    // Whether p's projection onto the surface lands inside the trim
    bool inTrim(const Vec3& p) const;
    void trimState(const Surface::Closest& c, bool withDist, bool& inOuter, bool& inHole,
                   double& bestBoundaryDistSq, Eigen::Vector2d& bestBoundaryUV) const;
};

// Builds one Face from an ADVANCED_FACE entity id. Returns an invalid
// Face (valid() == false) if the entity isn't one, or its surface type
// isn't one of the ones step_geometry.hpp supports.
Face resolveFace(const Document& doc, int advancedFaceId);

}  // namespace step
}  // namespace libfive
