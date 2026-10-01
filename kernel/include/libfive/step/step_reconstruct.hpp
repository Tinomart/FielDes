/*
libfive: a CAD kernel for modeling with implicit functions

Reconstructs a STEP-imported solid as a genuine native libfive::Tree --
real CSG built from box/half-space primitives, not an opaque Oracle --
for solids whose faces are entirely analytic (Plane/Cylinder/Cone/
Sphere/Torus). This is what actually lets the imported geometry
participate in libfive's own var()-adjustable, Tree-native machinery,
and it's what lets Studio's normal dual-contouring renderer mesh it
(native primitive CSG is exactly the case dual contouring is designed
for -- unlike an opaque Oracle's field, which this session found it
handles very poorly for dense curved geometry).

Approach: surface-topology-native convex decomposition, purely from the
B-rep's own face-adjacency graph -- no spatial sampling or octree of any
kind. Every shared edge between two faces is classified convex/concave
from the two surfaces' own equations (isConvexEdge, in the .cpp): a
probe steps from the shared edge into one face's own trimmed interior
(verified via a UV point-in-polygon test against that face's real
boundary, not the file's loop-winding direction, which turned out not to
be a trustworthy convention across STEP exporters) and checks whether
that real boundary point still satisfies the other face's half-space.
Faces touching any concave edge -- gated by size, so a large "hub" face
bordering several small unrelated features doesn't get wrongly absorbed
-- become a separate "pocket" (subtracted) group; the rest cluster into
"positive" (added) groups via convex edges, validated against each
cluster's own accumulated real boundary points before every merge (not
just the two faces on the edge being merged) so a chain of pairwise-valid
merges can't silently produce a globally-inconsistent group. Positive
clusters intersect their own half-spaces (bounded by the solid's box) and
union together; pocket clusters do the same, tightened along any curved
member's own trim extent (a bare cylinder has no cap face to bound its
axial length, so it needs its own clamp beyond the solid's outer box),
and get subtracted from the union.

This is NOT a reconstruction of the original design's feature tree --
there's no unique such tree recoverable from a plain B-rep (many
different CSG trees can produce the same boundary). It's *a* valid,
exact-where-it-matters tree built from the file's own real surfaces and
their real parameters, which is what actually matters for having
meaningful, draggable values.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#pragma once

#include <string>
#include <vector>

#include "libfive/step/step_model.hpp"
#include "libfive/step/step_parts.hpp"
#include "libfive/tree/tree.hpp"

namespace libfive {
namespace step {

// The version of the import algorithm.  The cache of imported parts
// (libfive.stdlib.cad_import) stays valid for as long as this number does,
// however often the library is rebuilt.  Raise it when -- and only when -- a
// change alters what an import produces (the trees, the parts' metrics, the fit
// report).  To decide, import some files fresh before and after the change and
// compare the two caches file by file (byte-identical: leave it alone).  A
// speed-up, a diagnostic or a Studio change never raises it.
//   1  2026-09-30  the first explicit version (the cache used to follow the
//                  library file's hash, so every rebuild lost every cache)
//   2  2026-09-30  a part's smallest feature includes the material's
//                  thickness (2 x volume / area), so hollow tubes and perforated
//                  plates report their wall (their metrics change)
//   3  2026-10-01  one continuous solid: where two cells of the part meet
//                  inside the material the field is no longer 0 on the wall
//                  between them (the field of a part's tree changes)
//   4  2026-10-01  the bridges across those walls are consensus cubes, which
//                  leave the field outside the part as it was (version 3 grew
//                  them and lowered the outside: section field, outward shell
//                  and thickening came out wrong)
//   5  2026-10-01  (a first try at the next: boxes round whole cubes; replaced)
//   6  2026-10-01  outside the part the field of a plane is no smaller than
//                  the distance to the box of its own faces (it was the
//                  distance to the infinite plane, so shells, thickenings and
//                  offsets of an imported part grew blocks and fins)
//   7  2026-10-01  convex edges of an imported part's offsets are round: where
//                  a cube's planes are axis-parallel its outside value is the
//                  straight-line distance to the box they bound, not the max
//                  of the plane distances (a mitre)
constexpr int kImportVersion = 7;

// True if every face of `solid` has an analytic (non-B-spline) surface
// -- the precondition for reconstruct() to be usable at all. A solid
// with any B-spline face isn't representable as half-space CSG and
// must fall back to the Oracle path (step_oracle.cpp).
bool isFullyAnalytic(const Solid& solid);

struct ReconstructStats
{
    int leavesInside = 0;
    int leavesOutside = 0;
    int maxDepthHit = 0;
};

// Builds a native Tree for `solid` via the arrangement method (see the .cpp
// file comment above reconstructByArrangement). `lo`/`hi` bound the region
// to reconstruct (normally the solid's own padded bounding box). `maxDepth`
// is unused (kept only for call-site/header compatibility with earlier
// octree-based attempts at this same problem). There is no fallback: on
// failure (e.g. a surface the arrangement method can't resolve, or one
// solid too complex for it), *ok is set false and, if `error` is non-null,
// *error is set to a message describing why -- the caller must not use the
// returned Tree(1e9) sentinel as real geometry.
Tree reconstructSolid(const Solid& solid, const Vec3& lo, const Vec3& hi,
                       int maxDepth = 14, ReconstructStats* stats = nullptr,
                       bool* ok = nullptr, std::string* error = nullptr);

// Same parsing/part-splitting as importStepTreeParts (step_oracle.hpp), but
// builds each part's Tree via reconstructSolid() -- real CSG, not an
// Oracle. There is no Oracle fallback: `ok`/`error` here only report
// whether the FILE itself was read; a solid with any non-analytic face, or
// one the arrangement method can't resolve, does not fail the whole
// import -- that one StepPart's `error` field is set instead (see
// StepPart's comment) and every other solid still imports normally.
// `numOracleFallback` is kept under its old name for ABI/header
// compatibility; it now counts solids that FAILED (no Oracle exists to
// fall back to any more).
std::vector<StepPart> importStepTreePartsReconstructed(
    const std::string& path, bool& ok, std::string& error,
    int* numSolids = nullptr, int* numFacesResolved = nullptr,
    int* numFacesSkipped = nullptr, int* numReconstructed = nullptr,
    int* numOracleFallback = nullptr);

}  // namespace step
}  // namespace libfive
