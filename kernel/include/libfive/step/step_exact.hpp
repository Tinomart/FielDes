/*
libfive: a CAD kernel for modeling with implicit functions

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#pragma once

#include <string>
#include <vector>

#include <Eigen/Eigen>

namespace libfive {
namespace step {

/*  The exact surface of an imported part, meshed directly from its STEP file (step_tessellate.cpp): where the
 *  import's field is only approximate (a B-spline face fitted by a simple surface, a thread), the user
 *  excludes a region (fieldes.stdlib.cad_import.exclude) and this mesh -- made into a distance field by the
 *  caller (mesh_import) -- is the part inside it.  Nothing is fitted or reconstructed.  */
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

}   // namespace step
}   // namespace libfive
