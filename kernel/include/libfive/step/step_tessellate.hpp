/*
libfive: a CAD kernel for modeling with implicit functions

Direct B-rep tessellation for STEP-imported solids: turns a Solid (see
step_model.hpp) into a real, watertight triangle mesh by tessellating
each trimmed face's own UV domain and stitching adjacent faces via a
shared per-EDGE_CURVE 3D sampling cache -- no octree, no implicit-
function/SDF sampling anywhere in this path. See step_tessellate.cpp's
top comment for why this replaces dual-contouring for STEP geometry.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#pragma once

#include <array>
#include <vector>

#include "libfive/step/step_model.hpp"

namespace libfive {
namespace step {

struct TessMesh
{
    std::vector<Vec3> verts;
    std::vector<std::array<int, 3>> tris;
    // Parallel to `tris`: which face (index into Solid::faces) each
    // triangle came from, and whether that face's surface is a B-spline.
    // Added specifically so callers can visually mark where B-spline
    // surfaces occur in a real model (e.g. to trace back which CAD
    // feature produced one) without needing a separate lookup.
    std::vector<int> triFaceIdx;
    std::vector<bool> triIsBSpline;
};

// Tessellates one solid into a single triangle mesh. Faces that meet
// along a shared STEP edge get bit-identical boundary vertices (see
// EdgeSpan::edgeCurveId), so the result is watertight by construction
// rather than by chance -- this is the property dual contouring can't
// give us over an oracle wrapping the raw trimmed B-rep.
// `turnSamples`: points per full turn of a circle / ellipse (B-spline
// edges get three quarters as many) -- the tessellation's quality.
TessMesh tessellateSolid(const Solid& solid, int turnSamples = 64);

}  // namespace step
}  // namespace libfive
