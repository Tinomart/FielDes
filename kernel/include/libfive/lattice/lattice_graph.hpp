/*
libfive: a CAD kernel for modeling with implicit functions

Graph (beam) lattices: round beams along the edges of an arbitrary graph,
evaluated through a bounding volume hierarchy so graphs of many thousands
of beams stay fast, and the generators that make such graphs:

    volume graphs   Poisson-disk points inside a body, joined by their
                    Delaunay edges (a stochastic truss) or by the edges of
                    their Voronoi cells (a Voronoi foam)
    surface graphs  Poisson-disk points on a body's surface, joined by the
                    restricted Delaunay triangulation (a triangle lattice
                    on the surface) or its dual (a Voronoi / hexagonal
                    pattern on the surface)

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#pragma once

#include <array>
#include <string>
#include <vector>

#include <Eigen/Eigen>

#include "libfive/tree/tree.hpp"

namespace libfive {
namespace lattice {

struct Graph
{
    std::vector<Eigen::Vector3d> nodes;
    std::vector<std::array<int, 2>> beams;
};

/*  Round beams along a graph's edges.  radius: one per node (a beam's
 *  radius varies linearly between its ends); blend > 0 rounds the joints
 *  (smooth minimum of that radius).  */
Tree beamLattice(const Graph& g, const std::vector<double>& radius, double blend);

/*  Delaunay tetrahedralization (Bowyer-Watson).  Returns the tetrahedra as
 *  point indices; points should be in general position (random points are).  */
std::vector<std::array<int, 4>> delaunay(const std::vector<Eigen::Vector3d>& pts);

/*  A graph inside a body: Poisson-disk points `spacing` apart in the box
 *  [lo, hi] (plus a padding layer outside), mode 0 = Delaunay edges,
 *  1 = Voronoi cell edges; `relax` Lloyd-like iterations make the cells
 *  more regular.  Beams entirely outside the body are dropped.  */
Graph volumeGraph(const Tree& body, const Eigen::Vector3d& lo, const Eigen::Vector3d& hi,
                  double spacing, int mode, int relax, unsigned seed, std::string& error);

/*  A graph on a body's surface: Poisson-disk points `spacing` apart on the
 *  surface, mode 0 = restricted Delaunay triangle edges, 1 = its dual
 *  (Voronoi / hexagon-like cells).  */
Graph surfaceGraph(const Tree& body, const Eigen::Vector3d& lo, const Eigen::Vector3d& hi,
                   double spacing, int mode, unsigned seed, std::string& error);

/*  Delaunay (mode 0) or Voronoi (mode 1) graph of given points  */
Graph pointsGraph(const std::vector<Eigen::Vector3d>& pts, int mode);

}   // namespace lattice
}   // namespace libfive
