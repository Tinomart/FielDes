/*
libfive: a CAD kernel for modeling with implicit functions

The mender of tetrahedral meshes: the poor tetrahedra of a mesh made by isosurface stuffing (tetmesh.hpp) made better by the passes of Netgen's volume
optimization (the logic of the open source of Salome's tetrahedral mesher: libsrc/meshing meshfunc.cpp OptimizeVolume, improve3.cpp CombineImprove /
SplitImprove / SwapImprove, smoothing3.cpp ImproveMesh, improve2.cpp EdgeSwapping, smoothing2.cpp CalcTriangleBadness; nothing of its code is used):

  * the measure is Netgen's badness of a tetrahedron, (0.0080187537 (the sum of its squared edges)^1.5 / V)^2 -- 1 for a regular one, the cube of the inverse of
    its mean ratio, so the worst weigh most;
  * an interior node moves to where the sum of the badness of its tetrahedra is least (a BFGS search; a node never leaves its worst tetrahedron poorer);
  * an edge inside the mesh is taken out when the ring of tetrahedra round it can be filled again in a way that makes the worst of them better (the 3-2, 4-4, ...
    swaps), a face inside is replaced by three tetrahedra (2-3), a node inside is taken into a neighbour (collapse);
  * the edges of the worst tetrahedra are split (a node in the middle, moved to where the sum of badness is least);
  * on the surface: a needle triangle (a smallest angle under 30 degrees) gets the other diagonal of its quadrilateral where the new and the old triangles are within
    30 degrees of the surface's own normal (the gradient of the field), the tetrahedra under it made again; a node of the surface moves along the surface and goes
    back onto it; a node of the surface is taken into a neighbour on it where that leaves the surface as it is.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#pragma once

#include <cstddef>
#include <utility>
#include <vector>

#include "libfive/fea/tetmesh.hpp"

namespace libfive {
namespace fea {

struct MendOptions
{
    int rounds = 2;                 // passes of the whole sequence (surface smoothing, swaps and collapses, splits, node smoothing)
    int smoothSweeps = 6;           // sweeps over the nodes of poor tetrahedra in each smoothing
    double poor = 0.6;              // a tetrahedron is poor when its mean ratio (12 (3 V)^(2/3) over the sum of its squared edges) is below this
    double splitBadness = 100.0;    // an edge is split when a tetrahedron round it has this badness or more (Netgen's 100: a mean ratio of 0.215)
    // A mesh of more tetrahedra than this is left as it is: the tables of the mender take about a thousand bytes a tetrahedron (the sort of the faces alone 100), and a mesh of ten million would
    // take the memory of the computer for a few poor cells
    size_t maxTets = 3000000;
    // false: nothing on the surface moves or changes -- the faces, the edges and the nodes of the surface stay as they are
    bool surface = true;
    // Planes (axis 0, 1, 2 and where) that the mesh is to be mirrored about, as it is meshed on one side of them: a node within `holdTolerance` of one is held where it is, and no face of the
    // surface that has such a node is flipped or taken into a neighbour -- so the faces on the plane stay as they are, and the mirror image continues the mesh
    std::vector<std::pair<int, double>> holdPlanes;
    double holdTolerance = 0.0;
    // H225: how far a collapse may leave the skin from the exact surface, as a fraction of the local element size (kSurfaceTolerance, tetmesh.hpp; the choice surface_fit changes it)
    double surfaceTolerance = 0.06;
};

struct MendStats
{
    size_t swaps = 0;               // edge swaps, face swaps and collapses
    size_t flips = 0;               // edges of the surface flipped
    size_t splits = 0;              // edges split
    size_t moved = 0;               // nodes moved by the smoothing
};

/*  Mends `mesh` in place (its nodes and tetrahedra; the vertices no tetrahedron uses are dropped and the boundary is made again, as finalizeMesh does).  `shape` is
 *  the field whose surface the mesh is the inside of (null: the surface is left as it is, as with options.surface false).  Returns false, the mesh untouched, if the
 *  mesh is too large to number (2^31 nodes) or the run was cancelled  */
bool mendTets(TetMesh& mesh, const Tree* shape, const MendOptions& options = MendOptions(), MendStats* stats = nullptr);

}   // namespace fea
}   // namespace libfive
