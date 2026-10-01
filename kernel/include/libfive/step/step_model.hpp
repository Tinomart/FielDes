/*
libfive: a CAD kernel for modeling with implicit functions

Top-level STEP import: walks MANIFOLD_SOLID_BREP entities into Solids
(each a list of trimmed Faces, see step_face.hpp), and combines them
into one signed-distance function for the whole file.

KNOWN LIMITATION (v1): every solid's geometry is treated as already
being in the file's global coordinate system. Proper STEP assemblies
(NEXT_ASSEMBLY_USAGE_OCCURRENCE / ITEM_DEFINED_TRANSFORMATION) place
each part's geometry in its own local frame and expect a transform to
be applied when instancing it into the assembly; that transform
resolution is not implemented, so a multi-part assembly file may come
out with parts in the wrong place relative to each other even though
each part's own shape is read correctly. Single-part files, and
multi-part files whose exporter already bakes absolute coordinates
into each part (common for simpler exporters), are unaffected.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "libfive/step/step_assembly.hpp"
#include "libfive/step/step_face.hpp"

namespace libfive {
namespace step {

// A bounding-volume tree over the boxes of a solid's faces: the faces a ray
// can hit, without testing every one.  (The inside vote casts several rays
// per sample point, and a part can have thousands of faces.)
struct FaceBoxTree
{
    struct Node
    {
        Vec3 lo, hi;                // the faces' boxes, padded as faceRayHits pads them
        int left = -1, right = -1;
        int start = 0, count = 0;   // (a leaf) a range of `order`
    };
    std::vector<Node> nodes;
    std::vector<int> order;         // the valid faces' indices
    size_t faceCount = 0;           // faces.size() when built

    void build(const std::vector<Face>& faces);
    // Every valid face whose padded box the ray p + t d (t >= 0) crosses
    void rayFaces(const Vec3& p, const Vec3& d, std::vector<int>& out) const;
};

struct Solid
{
    std::vector<Face> faces;
    std::shared_ptr<const FaceBoxTree> faceTree;    // (set in resolveSolid)

    // Aggregate bbox over all of `faces` (set in resolveSolid)
    Vec3 boundMin = Vec3::Constant(1e18);
    Vec3 boundMax = Vec3::Constant(-1e18);

    // The MANIFOLD_SOLID_BREP entity this came from and, from the file's
    // units and assembly structure, every placed occurrence of it
    // (millimetres, top-level frame; see step_assembly.hpp).  The solid's
    // own geometry stays in its file coordinates.  Never empty after
    // importStepFile.
    int entityId = -1;
    std::string name;
    std::vector<SolidInstance> instances;
};

struct Model
{
    std::vector<Solid> solids;
};

struct ImportResult
{
    bool ok = false;
    std::string error;
    std::shared_ptr<Model> model;

    // Diagnostics -- surfaced through the Python binding so a failed or
    // partial import says *why*, instead of just "didn't work".
    int numSolids = 0;
    int numFacesResolved = 0;
    int numFacesSkipped = 0;  // e.g. unsupported surface types

    // Millimetres per unit of the file's first declared length unit
    double fileUnitMM = 1.0;
};

ImportResult importStepFile(const std::string& path);

}  // namespace step
}  // namespace libfive
