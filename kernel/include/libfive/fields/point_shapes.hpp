/*
libfive: a CAD kernel for modeling with implicit functions

Shapes made of points: the polyline or spline through them, the plane a face of them lies in, and the face (their fields are plain math, made in Python)
(fieldes.stdlib.curves and fieldes.stdlib.profile are made on these).

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
    double radius = 0.0;                                   // > 0 at a corner of a face: a ROUND corner -- an exact circular arc of this radius, tangent to both edges, takes the
                                                           // place of the corner (the point stays where the two edges would meet).  Ignored at a spline point and at the ends of an open line
};

/*
 *  The arc that takes the place of a round corner: from `a` (on the edge before the point) to `b` (on the edge after it), round `centre`, `radius` (as made: the radius
 *  that was asked for, made smaller where the arc would run into the next one or past the next point).  The arc is the short way round from `a` to `b` and lies in the
 *  plane of the two edges (the plane of the face, for a face)
 */
struct Fillet
{
    int control = 0;
    Eigen::Vector3d a = Eigen::Vector3d::Zero(), b = Eigen::Vector3d::Zero(), centre = Eigen::Vector3d::Zero();
    double radius = 0.0;
    Eigen::Vector3d corner = Eigen::Vector3d::Zero();      // where the corner would be: the place of the point (on the plane of the face), the arc's apex is on the line from it to `centre`
    double theta = 0.0;                                    // the angle between the two edges at the corner (0 to pi)
};

/*
 *  The round corners of the line or face through the controls, in the order of the controls (a control that is not round, or cannot be -- a straight run, a spike, a
 *  spline point, an end of an open line -- has none).  The radius of each is limited by the two edges it sits on: with t = radius / tan(half the angle between the edges)
 *  the length its arc takes from each edge, the two arcs on one edge may take no more than the edge is long, so that a square with a round corner of at least half its side
 *  at each corner is a circle
 */
std::vector<Fillet> fillets(const std::vector<Control>& controls, bool smoothDefault, bool closed);

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
 *  along towards the next it is (0 to 1).  A round corner (see `Fillet`) is its arc in place of the corner: the vertices from `a` to `b`, `arcPieces` pieces to a
 *  quarter turn (0: only `a` and `b`, the chord -- what the exact field of a face is made on); they all belong to the control
 */
std::vector<Eigen::Vector3d> polyline(const std::vector<Control>& controls, bool smoothDefault, bool closed, int pieces = 14,
                                      std::vector<std::pair<int, double>>* owners = nullptr, int arcPieces = 8);

/*
 *  A stretch of the line that is a cubic Bezier curve: the spline after control `control`, from p[0] (the point itself, or where its arc ends) to p[3] (the next point, or where
 *  its arc begins) with the handles p[1] and p[2] -- the curve `polyline` takes its vertices from (the vertices owned by `control` at 0 < along < 1).  They are in the order of
 *  the line; a stretch between two corners is a straight piece and is not among them
 */
struct Curve
{
    int control = 0;
    Eigen::Vector3d p[4] = {Eigen::Vector3d::Zero(), Eigen::Vector3d::Zero(), Eigen::Vector3d::Zero(), Eigen::Vector3d::Zero()};
};
std::vector<Curve> curves(const std::vector<Control>& controls, bool smoothDefault, bool closed);

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
