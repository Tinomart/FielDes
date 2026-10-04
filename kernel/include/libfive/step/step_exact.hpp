/*
libfive: a CAD kernel for modeling with implicit functions

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <Eigen/Eigen>

#include "libfive/render/brep/mesh.hpp"
#include "libfive/tree/tree.hpp"

namespace libfive {
namespace step {

/*  Exact regions: where an imported part's field is only approximate (a
 *  B-spline face fitted by a simple surface, a thread), the user names a
 *  region -- any field, negative inside -- and the part's real surface,
 *  meshed directly from the STEP file (step_tessellate.cpp), replaces the
 *  field's mesh inside it.  Nothing is meshed or intersected for the region
 *  itself: the exact surface's triangles are cut by evaluating the region's
 *  field on them, and the field mesh loses the triangles inside the region.
 *  The edge between the two is jagged, one cell wide.  */
struct ExactPiece
{
    std::vector<Eigen::Vector3f> verts;
    std::vector<Eigen::Vector3i> tris;
    std::string error;   // empty unless the piece couldn't be made
};

/*  Which surface to take: solid `solid` of the STEP file at `path`, placed
 *  as its instance `instance` and then by the 4x4 matrix `transform` (the
 *  unit scale and every move / rotation applied to the part since import).
 *  `turnSamples` is the tessellation's quality: points per full turn of a
 *  circle.  */
struct ExactSpec
{
    std::string path;
    int solid = 0;
    int instance = 0;
    Eigen::Matrix4d transform = Eigen::Matrix4d::Identity();
    int turnSamples = 64;

    bool operator==(const ExactSpec& o) const
    {
        return path == o.path && solid == o.solid && instance == o.instance &&
               transform == o.transform && turnSamples == o.turnSamples;
    }
};

/*  The whole of the part's exact surface, placed. The STEP file is parsed and
 *  each solid tessellated once per file version (cached).  */
ExactPiece exactSurface(const ExactSpec& spec);

/*  The tessellated import: a STEP file's parts as the exact surface itself -- every solid tessellated once
 *  (the free-form faces refined on all the threads there are, the solids a few at a time), in its own
 *  coordinates, and each of its placed occurrences described: nothing is reconstructed or fitted, the
 *  mesh is made into a distance field by the caller (mesh_import).  Instances are in the order of the
 *  main importer's parts (the first of each solid in the solid's slot, the others after the last solid).  */
struct BrepSolid
{
    ExactPiece mesh;                // in the solid's own coordinates (the file's units); `error` as for ExactPiece
    int faces = 0, bsplineFaces = 0;
};

struct BrepInstance
{
    int solid = 0, instance = 0;
    Eigen::Matrix3d linear = Eigen::Matrix3d::Identity();     // p = linear * q + offset: the solid's own
    Eigen::Vector3d offset = Eigen::Vector3d::Zero();         //   coordinates q to millimetres p
    std::string name;
    Eigen::Vector3d boundMin = Eigen::Vector3d::Zero(), boundMax = Eigen::Vector3d::Zero();   // placed, mm
    double detail = 0, areaFlat = 0, areaCurved = 0;          // placed, mm (see SolidMetrics)
};

struct BrepParts
{
    std::vector<BrepSolid> solids;
    std::vector<BrepInstance> instances;
    std::string error;              // non-empty when the file could not be read at all
};

BrepParts brepParts(const std::string& path, int turnSamples);

/*  Whether a region (a field, with the numbers `vars` for its variables) can
 *  reach the part at all, judged by the part's own bounds in the STEP file --
 *  without tessellating it.  False means the region is nowhere near.  */
bool exactReaches(const ExactSpec& spec, const Tree& field, const std::map<Tree::Id, float>& vars);

/*  The part of a surface inside a region (a field, negative inside it, with
 *  the numbers `vars` for its variables): the triangles that lie inside are
 *  kept, the ones the region's surface crosses are split until they are no
 *  longer than `cell` and kept where their middle is inside.  */
ExactPiece clipToRegion(const ExactPiece& surface, const Tree& field,
                        const std::map<Tree::Id, float>& vars, double cell);

/*  A mesh without the triangles inside a region (most of a triangle's
 *  corners inside it); nullptr if none is, which leaves the mesh as it is.  */
std::unique_ptr<Mesh> removeInside(const Mesh& m, const Tree& field,
                                   const std::map<Tree::Id, float>& vars);

/*  A mesh with pieces put into it, as one mesh (the triangles are only put
 *  together: the pieces overlap the mesh by up to a cell at the edge).  */
std::unique_ptr<Mesh> joinExact(const Mesh& field, const std::vector<ExactPiece>& pieces);

}   // namespace step
}   // namespace libfive
