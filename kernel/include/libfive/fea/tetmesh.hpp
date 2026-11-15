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
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <Eigen/Eigen>

#include "libfive/tree/tree.hpp"

namespace libfive {
namespace fea {

// How far a boundary node of a mesh may stand off the true surface and still be taken for being ON it, as a fraction of the local element size.  It is a fraction of the element and NOT a length, so it scales with
// the part and with the mesh size: a part ten times as large, meshed ten times as coarse, gets ten times the length.  The value is the one the mender has always used for a change of the surface
// (tetmender.cpp: a collapse may leave the skin this far from the surface), and the mirrored mesh (meshShapeMirrored) is judged by the same number, so that "on the surface" means one thing in the whole mesher.
// (It is a tolerance, not a derived law: the two sides of an imported part are tessellated on their own and differ from each other by a few hundredths of an element -- measured 0.020 to 0.028 on the bracket.)
constexpr double kSurfaceTolerance = 0.06;

// H225 (the user, 2026-11-15: "The threshold of when elements are removed ... sound like something that should be an argument that the user can choose to have an active choice between less odd elements or more
// surface conformity"): the numbers that decide what the mesher gives up are one choice, made by the user (the argument surface_fit of the meshing and analysis functions, sim_mesh.py), and the numbers below
// are what a mesh is made with while a choice stands.  'balanced', the default, is what the mesher has always done; 'surface' puts the edges and the skin first (the mender takes fewer elements out, poorer
// tetrahedra are accepted beside an edge), 'elements' puts the elements first (the mender works on every tetrahedron under 0.70 and may take the skin further off the exact surface to do it; the skin gets
// coarser and the mesh takes longer -- measured on the real bracket).  Set with libfive_tetmesh_set_limits; every MeshOptions starts from them
struct MeshLimits
{
    double floorRatio = 0.15;                   // the poorest mean ratio that a move of a node onto an edge or a corner may leave a tetrahedron (the edge pass and the bite pass)
    double sliverAngle = 10.0;                  // the smallest dihedral angle, in degrees, that such a move may leave in a tetrahedron
    double surfaceTolerance = kSurfaceTolerance;    // how far the skin may stand off the exact surface, as a fraction of the element size: the mender's collapses of the skin, the check of a mirrored mesh
    double menderPoor = 0.6;                    // the mean ratio under which the mender takes a tetrahedron for poor and swaps, collapses or splits it (tetmender.hpp MendOptions::poor): the threshold of when elements are removed
};
MeshLimits& meshLimits();

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
    size_t mirrored = 0;                        // the planes the mesh was mirrored about (meshShapeMirrored)
    size_t mended = 0;                          // the swaps, flips and splits of the mender (tetmender.hpp)
    size_t rimsSnapped = 0;                     // nodes put on the rim of a flat face (MeshOptions::rims)
    size_t edgesSnapped = 0;                    // nodes put on an edge or a corner of the body that the lattice cut across (MeshOptions::edges)
    size_t surfaceSmoothed = 0;                 // nodes of the skin moved along the surface to make its triangles regular (MeshOptions::smooth)
    size_t chainsSmoothed = 0;                  // nodes on an edge or a rim moved along it to space the nodes of the chain evenly (MeshOptions::chains)
    size_t cornersCollapsed = 0;                // pairs of nodes across an inside corner made one node on the corner (FIELDES_TET_CORNERS=0: none)
    size_t bitesCollapsed = 0;                  // nodes of faces that lean away from the surface collapsed into a neighbour, which removes the bite (FIELDES_TET_BITES=0: none)
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
    // The spacing of the lattice along each axis is adjusted -- never more than a twentieth coarser than h, as much finer as it takes (at most a half) -- so that the region is a whole number of cubes across
    // and both its faces lie on the planes of the cube corners (H202): the element size is a target that the mesh fits to the body, not a request
    // that is rounded off wherever the lattice falls, and the edges and corners of the body lie on lines and at points of the lattice.
    // false: the same spacing h along every axis (FIELDES_TET_FIT=0)
    bool fit = true;
    // The poor tetrahedra of the lattice cut are made better by the passes of Netgen's volume optimization (tetmender.hpp; the raw lattice of the body in the box that an
    // analysis was given had 449 tetrahedra with a scaled Jacobian below 0.2 and 699 with a skewness above 0.85 in the 17,000 of the imported bracket at 6 mm, the mended one 9 and 52).
    // `mendSurface` false: nothing on the surface moves or changes (the half of a mesh that is mirrored: the nodes on the plane stay on it).  FIELDES_TET_MEND=0: no mending
    bool mend = true;
    bool mendSurface = true;
    // H223 (the user, 2026-11-14, of the real example 05 in his window: the rims of its bores were SAWTOOTHED -- of the 62 nodes that the rims of four bores of 20 mm hold at 4 mm, 2 were on them, the others
    // stood off by up to 0.6 of an element, where mesh_tet4, which had a python pass for it, put 60 on them): the nodes on the RIM of a flat face -- the circle where a bore meets the face of an arm -- are
    // put on it before the mending.  The lattice cannot: the field is zero all over a flat face (it is the maximum of the face's and the wall's), so the distance of a node on the face to the rim cannot be
    // read at the node, and the crease pass above puts the node on a point of the tangent planes whose Newton step then leaves the crease.  FIELDES_TET_RIMS=0: none
    bool rims = true;
    // H223, the edges (the same order, "the edges along the arms are wavy and there are spiky triangles along the creases"; measured on the bracket at 4 mm: 8.2 % of the boundary faces of the analysis mesh lean more than 12 degrees
    // away from the field in their middle, 4.2 % of mesh_tet4's, which has a python pass for it): the nodes of the faces that lean away from the field are put on the edge or the corner of the body that the lattice cut across,
    // before the rims.  FIELDES_TET_EDGES=0: none
    bool edges = true;
    // H223 (the user, 2026-11-14, of the surface triangles on the real bracket: "THAT IS HOW SALOME MESHES TETS IN YOUR VIEW OR WHAT"): the skin made regular after the edges and the rims, as the surface optimizer of
    // Netgen does (MeshOptimize2d smoothing): the nodes of the smooth parts of the surface go along the surface towards the middle of their neighbours and are put back on it.  Not for a graded mesh, whose triangles
    // are meant to differ in size.  FIELDES_TET_SMOOTH=0: none
    bool smooth = true;
    // H223 (the user, 2026-11-14: "the defects are less frequent, but more noticable ... it looks like sore thumb"; the worst triangles of the skin all touch a node of a crease, and 15 % of the nodes of a crease have
    // two chain edges that differ by more than 1.5): the nodes along an edge or the rim of a flat face are spaced evenly along it, as Netgen's surface optimizer spaces the points of an edge.  Only with `smooth`.
    // FIELDES_TET_CHAINS=0: none
    bool chains = true;
    // H223 (found by the NAFEMS LE1 benchmark, 2026-11-15: at three element sizes of nine the stress at the tip of the hole read 14 to 29 % high, the mesh had no node at the corner where the wall of the hole, the plane of symmetry
    // and a face of the plate meet, and the two edges that end there stopped half an element short of it): a node at the end of the chain of an edge is moved along the edge to the corner where the surface closes it, when that
    // is within an element.  Only with `edges`.  FIELDES_TET_CORNERS=0: none
    bool corners = true;
    // The poorest mean ratio that a move of a node onto a rim may leave a tetrahedron round it (0.3; 0.1 for a graded mesh, where the layers meet the walls in flat tetrahedra)
    double rimFloor = 0.3;
    // H225: the choice between fewer odd elements and more surface conformity, see MeshLimits (the values that stand when the options are made)
    double floorRatio = meshLimits().floorRatio;
    double sliverAngle = meshLimits().sliverAngle;
    double surfaceTolerance = meshLimits().surfaceTolerance;
    double menderPoor = meshLimits().menderPoor;
    // Planes (axis, where) the mesh is the half of a mesh that is mirrored about: the nodes on them are held and the faces on them kept (meshShapeMirrored)
    std::vector<std::pair<int, double>> holdPlanes;
};

/*  Meshes the inside (field <= 0) of `shape` over [lo, hi] with lattice
 *  spacing h (tetrahedron edges 0.87 h to h), fitted along each axis so that
 *  the faces of the region lie on planes of the lattice (see MeshOptions::fit;
 *  TetMesh::h is the spacing it ended up with).  [lo, hi] should be the box
 *  round the shape: the lattice fits that, not the shape.  False with a message if the
 *  shape has nothing inside, or the mesh would be too large.  */
bool meshShape(const Tree& shape, const Eigen::Vector3d& lo, const Eigen::Vector3d& hi,
               double h, TetMesh& out, std::string& error,
               const MeshOptions& options = MeshOptions());

/*  H223 (the user, 2026-11-14: "WHY NOT HAVE EVERYTHING BE A PART OF THE KERNEL", "DONT HAVE TWO FUCKING MESHERS"): the clean-up of the surface of a tetrahedral mesh, in the kernel and nowhere else: the edges and corners of the
 *  body that the lattice cut across put back (options.edges), then the nodes on the rim of a flat face put on it (options.rims, options.rimFloor).  meshShape does it on the mesh it makes; this does it on a mesh that was
 *  refined after it (the graded mesh of mesh_tet4).  The boundary is made again first; `edgesSnapped` and `rimsSnapped` of the mesh say how many nodes were moved  */
void snapSurface(TetMesh& mesh, const Tree& shape, const MeshOptions& options);

/*  H193: the lattice vertices of a mesh at spacing h that lie near the surface of a shape, for a look at how thick the material is there.  A wall thinner than the lattice is lost, or half filled, and
 *  isosurface stuffing says nothing: it is the thickness of the walls against the spacing that decides (a plate of 1.5 mm held 41 % of its volume at spacings of 2.5 and 1.9 mm and all of it from 1.5, a tube
 *  with a wall of 1 mm 58 % at 1.0 and all of it from 0.8).  `nearby` counts the vertices whose value, the field taken as a distance, is under `reach` of the spacing on either side; `pos` and `value` are a
 *  sample of them (every `stride`-th, at most the number asked for) with the field's value there; `vertices` and `inside` count the vertices of the lattice and those inside the shape (a tetrahedron cut
 *  from the lattice has about 6 of them per vertex that is inside); `h` is the spacing the lattice was fitted to  */
struct SurfaceProbe
{
    double h = 0;
    size_t vertices = 0, inside = 0, nearby = 0, stride = 1;
    std::vector<Eigen::Vector3d> pos;
    std::vector<double> value;
};

bool probeSurface(const Tree& shape, const Eigen::Vector3d& lo, const Eigen::Vector3d& hi, double h, double reach, size_t maxReturn, bool fit,
                  SurfaceProbe& out, std::string& error);

/*  A plane that a shape is mirror symmetric about: the axis (0, 1, 2 = x, y, z) it is perpendicular to and its coordinate  */
struct MeshMirror
{
    int axis = 0;
    double at = 0;
};

/*  The planes through the middle of [lo, hi] (one for each axis) that `shape` is symmetric about, found by comparing its field with its mirror image at
 *  3000 fixed pseudo-random points (the same points on every machine, so the same answer for the same input)  */
std::vector<MeshMirror> findMeshMirrors(const Tree& shape, const Eigen::Vector3d& lo, const Eigen::Vector3d& hi, double h);

/*  meshShape for a shape that is symmetric about the planes given.  The lattice cut by a shape is not symmetric: a quadrilateral is cut along the diagonal
 *  through its lowest-numbered vertex, a vertex with two equal crossings goes to the first -- numbers a mirror reverses -- so a mesh of the whole has no
 *  mirror image however the lattice is placed.  Here the part of the shape on the upper side of every plane is meshed, with the planes as its boundary, and
 *  mirrored: vertices on a plane are shared, the others get an image, the mirrored tetrahedra swap two vertices to keep their orientation.  Where a plane
 *  meets the surface the half mesh has a corner that the mirror image does not continue (the surface runs straight on across the plane): the vertices on a
 *  plane that belong to a face of the surface are moved along the plane onto the surface first.  A plane whose
 *  mirrored boundary does not lie on the shape's surface is dropped (and the mesh made without it).  Planes that do not run through the middle of the
 *  region are ignored.  `mirrored` of the mesh says how many planes were used  */
bool meshShapeMirrored(const Tree& shape, const Eigen::Vector3d& lo, const Eigen::Vector3d& hi, double h, const std::vector<MeshMirror>& planes,
                       TetMesh& out, std::string& error, const MeshOptions& options = MeshOptions());

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

/*  H192 (the user, 2026-11-13: "Add rotation restraint control to the fixed restraint with the same vector based restriction input as for the translational restriction"):
 *  a restraint of the TURNING of a set of nodes.  A tetrahedron has no rotation of its own, so the turn of a support's region is the mean turn of its nodes about an axis
 *  through their centre: theta = sum_i coef_i . u_i (the nodes' displacements u_i, coef_i = (axis x r_i) / sum |axis x r_j|^2, r_i the node's place from the centre), the
 *  angle a rotation about the axis by theta moves a rigid body by.  It is held at `target` (radians) by a penalty, the energy 1/2 stiffness (theta - target)^2 added to the
 *  stiffness matrix -- a stiff spring on the mean turn, stiffer than any part (a thousand times the stiffness the nodes have against turning), so the turn is held to a
 *  thousandth of what it would be.  The directions the support holds node by node are not in it (a held direction has no displacement to turn with)  */
struct TurnConstraint
{
    std::vector<uint32_t> node;
    std::vector<Eigen::Vector3d> coef;      // per node: the weights of its displacement in the turn
    double stiffness = 0;                   // N mm per radian squared
    double target = 0;                      // radians
};

/*  Drops the vertices no tetrahedron uses and (re)builds the boundary faces
 *  and the vertices' on-surface flags from `tets`  */
void finalizeMesh(TetMesh& mesh);

/*  A mesh made elsewhere (meshing.py's mesh_tet4), as the tetrahedral mesh the solvers take in place of the one meshShape makes from a shape: `positions` is
 *  the x y z of each of the `vertexCount` nodes and `tets` the four nodes of each of the `tetCount` tetrahedra.  A tetrahedron that is inside out is turned (two
 *  nodes swapped) and one of no volume is dropped; the boundary is the faces that belong to one tetrahedron only.  False with a message when a node is not a
 *  number, a tetrahedron names a node that is not there, or a face is shared by more than two tetrahedra (a mesh whose elements cross).  The `h` of the mesh is
 *  the middle one of its edge lengths  */
bool meshFromArrays(size_t vertexCount, const double* positions, size_t tetCount, const int32_t* tets, TetMesh& out, std::string& error);

/*  A tree's values at many points (on all cores)  */
void evalTreePoints(const Tree& tree, const std::vector<Eigen::Vector3f>& pts, std::vector<float>& out);

}   // namespace fea
}   // namespace libfive
