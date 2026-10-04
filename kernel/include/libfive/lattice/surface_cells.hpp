/*
libfive: a CAD kernel for modeling with implicit functions

Strut lattices laid on the surface of a body, made from its field alone (surface_cells.cpp).

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <Eigen/Eigen>

#include "libfive/tree/tree.hpp"

namespace libfive {
namespace lattice {

struct SurfaceBeamsInfo
{
    size_t cells = 0;               // cells of the grid that got the unit cell's beams
    size_t cellsDropped = 0;        // (always 0: no cell is left out)
    size_t nodes = 0, beams = 0;
    double radius = 0;              // the radius of the struts (asked for, or 12 % of the smaller of the cell and the layer)
    double thickness = 0;           // how deep the layers are, typically
    int layers = 0;                 // how many cells deep
    std::string warning;            // what the layout of the cells leaves in doubt (empty: nothing)
};

/*  A strut lattice laid on the surface of a body as explicit beams, made from its field alone (value and gradient: no
 *  mesh of the body is made).  The surface is where the field is zero, its normal points where the field grows.
 *  `heightDir` -1: the layers go into the body and FILL it -- `depth` deep, or (depth <= 0) as deep as the body is under
 *  each node (found by following the field along the normal until it leaves the body; at most three cells), so that a
 *  thin shell is a layer of cells and nothing else; +1: out of the body (depth <= 0: a cell).  The cells are ONE CLOSED MESH OF
 *  QUADS over the surface inside the box `lo`..`hi`, one quad to a cell, every corner of it on the surface: a scaffold (a
 *  closed triangle mesh of the surface), a direction field and a lattice over it, solved globally -- the rows of cells run along
 *  the edges and round the holes of the part, `direction` (zero: along x) is the way they run where the surface gives none --
 *  the triangles that lie in one square of the lattice made into regions, a region with k corners made into k quads, and the
 *  nodes then moved over the surface until the cells are even (surface_cells.cpp).  The mesh is closed and made of quads by the
 *  way it is built and is checked (every cell four different corners, every edge on two cells); a layout that is not is not used
 *  and an error is returned.  The unit cell's beams (`unitBeams`, in unit-cube coordinates) are carried to every cell, in
 *  `layers` layers (<= 0: as many as fit): the points inside a cell are found from its four edges, which run along the
 *  surface, so a cell bends round a lip with the surface.  `radius` <= 0: 12 % of the smaller of the cell and the layer; the
 *  struts are inset by it from the faces of a body they fill.  A beam is straight between its two ends.  The mesher then
 *  measures the distance to nearby beams only.  `usedDirection` is the direction used.  `gridOffset` (0 to 3) is which of four fixed
 *  grids the surface is sampled on: the layout is made ONCE, from that grid, and is the same every time; a part that does not close
 *  with one grid may with another, and the error says so.  */
bool fieldCells(const Tree& body, const std::map<Tree::Id, float>& vars, const Eigen::Vector3d& lo,
                const Eigen::Vector3d& hi, const Eigen::Vector3d& direction, int gridOffset, double cell,
                const std::vector<std::array<Eigen::Vector3d, 2>>& unitBeams, double depth, int layers,
                double radius, double heightDir, std::vector<Eigen::Vector3d>& nodes,
                std::vector<std::array<int, 2>>& beams, SurfaceBeamsInfo& info, Eigen::Vector3d& usedDirection,
                std::string& error);

/*  The same cells with a periodic surface laid on them instead of beams: a TPMS that follows the surface.  The result is a field
 *  (a tree): it finds the cell a point is in, the point's coordinates in it (s, t over the surface, w through the layers, from the
 *  edges of the cell as a Coons patch and the depth of the layer), and gives the periodic function of those -- one period for
 *  each cell on the surface and for each of `layers` layers (<= 0: as many as fit) -- trimmed to the layers.
 *  kind: 0 gyroid, 1 Schwarz P, 2 diamond, 3 Neovius, 4 Lidinoid, 5 split P, 6 IWP, 7 FRD, 8 Fischer-Koch S.
 *  style 0: sheet, walls `thickness` mm thick; 1: network, the solid on one side of the surface grown by `offset` mm;
 *  2: the other side.  `skin` (mm): a solid skin that deep against the faces of the layers.  Same cells as fieldCells.
 *  (The render cache keeps the field by a hash of the cells, rounded to a thousandth of a millimetre.)  */
bool fieldCellTpms(const Tree& body, const std::map<Tree::Id, float>& vars, const Eigen::Vector3d& lo,
                   const Eigen::Vector3d& hi, const Eigen::Vector3d& direction, int gridOffset, double cell,
                   double depth, int layers, double heightDir, int kind, double thickness, int style, double offset,
                   double skin, std::vector<Tree>& out, SurfaceBeamsInfo& info,
                   Eigen::Vector3d& usedDirection, std::string& error);

}   // namespace lattice
}   // namespace libfive
