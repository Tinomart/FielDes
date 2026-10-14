/*
libfive: a CAD kernel for modeling with implicit functions

The 10-node (quadratic) tetrahedron: what static and modal analysis with element='tet10' are made of.

A linear mesh (tetmesh.hpp) becomes a mesh of 10-node tetrahedra by putting a node in the middle of every edge: four corners and six
midside nodes, quadratic shape functions (a displacement that is a quadratic in each element, a strain and a stress that are linear in
it), integrated with the four-point rule that is exact for a straight-sided element.  A midside node of an edge on the boundary is moved
onto the part's surface (along the faces' normals, until the shape's field is zero), so the element's edge is curved the way the part is;
an element that this would turn inside out is put back straight.

The results are delivered on the same nodes cut into linear tetrahedra -- every element into eight, the four at its corners and the four
in the octahedron between the midside nodes -- so that the displacement and stress fields, the locator that finds a point in the mesh and
the cache of results are those of the linear tetrahedra, only with six or seven times as many nodes (refinedMesh).

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "tet_common.hpp"
#include "libfive/tree/tree.hpp"

namespace libfive {
namespace fea {
namespace tet {

struct QuadMesh
{
    size_t corners = 0;                         // the nodes 0 .. corners - 1 are the linear mesh's vertices, in its order
    std::vector<Vec3> pos;                      // every node: the corners, then one in the middle of each edge
    std::vector<std::array<int, 10>> elem;      // per element: its four corners, then the nodes of its edges 01 02 03 12 13 23
    std::vector<uint8_t> diagonal;              // per element: the diagonal of the octahedron in its middle that cuts it into four (0, 1, 2)
    size_t moved = 0;                           // midside nodes that were put on the surface
    size_t straightened = 0;                    // ... of which put back where an element would have been turned inside out

    /*  The node on the edge between the corners with the vertex numbers a and b of element e  */
    int midNode(size_t e, int a, int b) const;
};

/*  The physical memory that is free now, in bytes (0 if the system does not say)  */
double availableMemory();

/*  Makes the 10-node elements of a linear mesh.  `shape` is the part: the nodes in the middle of boundary edges are moved onto its surface  */
bool buildQuadMesh(const TetMesh& linear, const Tree& shape, QuadMesh& q, std::string& error);

/*  The same nodes as a linear mesh of eight tetrahedra to the element (the boundary faces cut in four), vertices numbered as the nodes are  */
std::shared_ptr<TetMesh> refinedMesh(const TetMesh& linear, const QuadMesh& q);

/*  The corners of the eight tetrahedra an element is cut into, as numbers 0..9 of its nodes, for each diagonal  */
extern const int kSubTets[3][8][4];
/*  Natural coordinates (the barycentric coordinates 1 2 3) of the element's ten nodes  */
extern const double kNodeXi[10][3];

/*  The shape functions' derivatives with respect to the natural coordinates, at a point  */
void quadShapeDerivatives(const double xi[3], double dN[10][3]);
/*  The same with respect to x, y, z in element e, and the volume factor det J; false if the element is turned inside out at the point  */
bool quadGlobalDerivatives(const QuadMesh& q, size_t e, const double dN[10][3], double dNdx[10][3], double& detJ);

/*  Element e's volume, and the integrals over it of each shape function and of each one's gradient (what gravity and thermal expansion load it with)  */
void quadIntegrals(const QuadMesh& q, size_t e, double volume[1], double intN[10], double intGrad[10][3]);
void quadVolumes(const QuadMesh& q, std::vector<double>& volumes);

/*  The matrix's block pattern (a block row for every node), and the nodes' elements (Assembly::vtStart / vtList: ten entries to an element)  */
void buildPatternQuad(const QuadMesh& q, Assembly& A);
/*  The stiffness matrix, each element's stiffness times scale[e] (all 1 if null)  */
void assembleQuad(const QuadMesh& q, const Mat6& D, const std::vector<double>* scale, Assembly& A);

/*  The strain of element e at its natural point xi, from the nodal displacements ue (3 per node of the element)  */
Vec6 quadStrainAt(const QuadMesh& q, size_t e, const double xi[3], const double ue[30]);

}   // namespace tet
}   // namespace fea
}   // namespace libfive
