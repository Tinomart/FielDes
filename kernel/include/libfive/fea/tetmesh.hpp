/*
libfive: a CAD kernel for modeling with implicit functions

A tetrahedral mesh of the inside of an implicit shape whose boundary IS the
shape's surface -- not a voxelization.

The method is isosurface stuffing (Labelle & Shewchuk, "Isosurface Stuffing:
Fast Tetrahedral Meshes with Good Dihedral Angles", SIGGRAPH 2007), driven by
the shape's own field: the body-centred cubic lattice (cube corners and cube
centres, cut into tetrahedra) is laid over the region; every lattice edge that
crosses the surface gets its crossing point found on the actual field (not a
linear guess); a lattice vertex that lies close to the surface is moved onto
it (so no sliver forms there); and the tetrahedra the surface still cuts are
split along it into tetrahedra whose faces lie on it.  The result is
unstructured, conforming (every interior face is shared by exactly two
tetrahedra), positively oriented, and its boundary vertices lie on the surface.

Sharp edges and corners are followed to about the element size: a vertex is
found where a lattice edge meets the surface, so an edge that runs between
lattice edges is rounded within an element.  Features thinner than the element
size can fall between lattice vertices and be lost, as in any sampling.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <vector>

#include <Eigen/Eigen>

#include "libfive/tree/tree.hpp"

namespace libfive {
namespace fea {

struct TetMesh
{
    std::vector<Eigen::Vector3d> pos;
    std::vector<std::array<int, 4>> tets;       // positively oriented: det(p1-p0, p2-p0, p3-p0) > 0
    // The faces belonging to one tetrahedron only: the surface, normals out
    std::vector<std::array<int, 3>> faces;
    std::vector<int> faceTet;                   // the tetrahedron each boundary face belongs to
    std::vector<char> onSurface;                // per vertex: lies on the boundary
    double h = 0;                               // the lattice spacing it was made with
    size_t degenerate = 0;                      // tetrahedra of (nearly) no volume, dropped
    size_t featureVertices = 0;                 // vertices snapped onto a sharp edge or corner
    size_t featuresReverted = 0;                // snaps put back because they folded or thinned a tetrahedron
};

struct MeshOptions
{
    // A lattice vertex whose incident edge is crossed by the surface within
    // this fraction of the edge's length from it is moved onto the surface
    // (the paper's alpha_long 0.24999 and alpha_short 0.410; the short edges
    // join a cube's centre to its corners)
    double alphaLong = 0.24999;
    double alphaShort = 0.410;
    size_t maxTets = 12000000;
    // Sharp edges and corners: a vertex near a crease (where the surface normals around it
    // differ by more than featureAngle degrees) is moved onto it if that is within
    // featureReach * h of the vertex and on the surface
    bool features = true;
    double featureAngle = 30.0;
    double featureReach = 0.6;
};

/*  Meshes the inside (field <= 0) of `shape` over [lo, hi] with lattice
 *  spacing h (tetrahedron edges 0.87 h to h).  False with a message if the
 *  shape has nothing inside, or the mesh would be too large.  */
bool meshShape(const Tree& shape, const Eigen::Vector3d& lo, const Eigen::Vector3d& hi,
               double h, TetMesh& out, std::string& error,
               const MeshOptions& options = MeshOptions());

struct MeshQuality
{
    size_t tets = 0, vertices = 0, boundaryFaces = 0;
    double volume = 0;                          // of all tetrahedra
    double minVolume = 0;                       // of the smallest tetrahedron, in units of h^3 / 12
    double minDihedral = 0, maxDihedral = 0, meanMinDihedral = 0;   // degrees
    size_t inverted = 0;                        // (should be 0)
    size_t folded = 0;                          // faces whose two tetrahedra lie on the same side (should be 0)
    size_t badFaces = 0;                        // faces shared by more than two tetrahedra (should be 0)
    size_t openEdges = 0;                       // boundary edges not shared by two boundary faces (should be 0)
    double boundarySurfaceArea = 0;
};

MeshQuality meshQuality(const TetMesh& mesh);

/*  Drops the vertices no tetrahedron uses and (re)builds the boundary faces
 *  and the vertices' on-surface flags from `tets`  */
void finalizeMesh(TetMesh& mesh);

/*  A tree's values at many points (on all cores)  */
void evalTreePoints(const Tree& tree, const std::vector<Eigen::Vector3f>& pts, std::vector<float>& out);

}   // namespace fea
}   // namespace libfive
