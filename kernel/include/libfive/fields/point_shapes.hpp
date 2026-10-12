/*
libfive: a CAD kernel for modeling with implicit functions

Shapes made of points: the polyline or spline through them, the plane a face of them lies in, and the face (their fields are plain math, made in Python)
(fieldes.stdlib.curves and fieldes.stdlib.sketch are made on these).

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#pragma once

#include <memory>
#include <utility>
#include <vector>

#include <Eigen/Eigen>

#include "libfive/tree/tree.hpp"
#include "libfive/render/brep/mesh.hpp"

namespace libfive {
namespace pointshapes {

/*  One point of a polyline or spline  */
struct Control
{
    Eigen::Vector3d pos = Eigen::Vector3d::Zero();
    int smooth = -1;                                       // -1: what the shape says, 0: a corner, 1: a spline point
    bool turned = false;                                   // the point is rotated: the direction a spline leaves it is turned
    Eigen::Vector3d rotate = Eigen::Vector3d::Zero();      // degrees, about x, then y, then z (the way a gizmo turns a shape)
    double scale = 1.0;                                    // how strongly a spline leaves it: the direction it leaves in is this many times as long (1: Catmull-Rom)
    bool planar = false;                                   // the point belongs to a face: its rotation is ONE angle (the z number of `rotate`), about `normal`
    Eigen::Vector3d normal = Eigen::Vector3d::UnitZ();     // (the normal of the plane of the face)
};

/*
 *  The direction the line leaves each spline point in (zero at a corner): half the way from the point before to the point after, turned by the point's
 *  rotation (about x, then y, then z; for a point of a face the one angle about the normal of its plane) and made as many times as long as the point's
 *  scale says; the end of an open line looks along its one edge
 */
std::vector<Eigen::Vector3d> tangents(const std::vector<Control>& controls, bool smoothDefault, bool closed);

/*
 *  The vertices of the line through the controls: the points themselves, and between two points that are not both corners the pieces of a
 *  cubic curve that leaves one in its tangent and arrives at the other in its (Catmull-Rom: half the way from the point before to the
 *  point after, turned by the point's rotation; an end of an open line looks along its one edge).  A closed line ends where it
 *  begins; an open one at its last point.  `owners` (when given) gets, for every vertex, the number of the control it is after and how far
 *  along towards the next it is (0 to 1)
 */
std::vector<Eigen::Vector3d> polyline(const std::vector<Control>& controls, bool smoothDefault, bool closed, int pieces = 14,
                                      std::vector<std::pair<int, double>>* owners = nullptr);

/*
 *  The plane through the first three points that are not on a line: frame = (origin, u, v), nine numbers, u and v unit vectors,
 *  u along the first edge.  False when there are fewer than three points or they are all on a line
 */
bool planeOf(const std::vector<Eigen::Vector3d>& points, double frame[9]);

/*
 *  The face the closed loop of vertices encloses (all in the plane `frame`), as flat triangles, and its outline as a closed line of
 *  points in space; nullptr when it cannot be triangulated
 */
std::unique_ptr<Mesh> face(const std::vector<Eigen::Vector3d>& loop, const double frame[9],
                           std::vector<std::vector<Eigen::Vector3f>>* outlines = nullptr);

}   // namespace pointshapes
}   // namespace libfive
