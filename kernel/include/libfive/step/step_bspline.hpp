/*
libfive: a CAD kernel for modeling with implicit functions

A B-spline (NURBS) surface patch of an imported STEP face: evaluation with
first derivatives (sampled by the surface fitting, step_fit.hpp) and ray
intersections (the inside / outside test of the reconstruction).  The model
itself never evaluates a spline: each B-spline face becomes a fitted
closed-form surface.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#pragma once

#include <array>
#include <vector>

#include <Eigen/Eigen>

#include "libfive/step/step_geometry.hpp"

namespace libfive {
namespace step {

class BSplinePatch
{
public:
    /*  From a SurfaceKind::BSpline surface; check ok() afterwards  */
    explicit BSplinePatch(const Surface& s);
    bool ok() const { return m_ok; }

    /*  Parameter domain  */
    double u0 = 0, u1 = 1, v0 = 0, v1 = 1;

    /*  Point and first derivatives (parameters clamped to the domain)  */
    void eval(double u, double v, Vec3& S, Vec3* Su=nullptr, Vec3* Sv=nullptr) const;

    /*  Unit normal Su x Sv, robust at collapsed edges / poles  */
    Vec3 normal(double u, double v) const;

    /*  Crossings of the ray p + t d, t > 0: (t, u, v) each (the inside /
     *  outside test of the reconstruction casts such rays)  */
    void rayHits(const Vec3& p, const Vec3& d, std::vector<Eigen::Vector3d>& out) const;

    /*  A point of the patch nearest to p, as parameters and distance.  */
    struct Foot
    {
        double u = 0, v = 0;
        double dist = 0;
    };

    /*  The feet of p on the patch: every local minimum of the distance to p
     *  that a search of the whole patch finds (and, first, the one the
     *  search from `near` -- a parameter pair, e.g. the previous point of an
     *  outline -- runs into), each refined on the exact surface with a damped
     *  Gauss-Newton step (Levenberg-Marquardt: a step that does not bring the
     *  point closer is not taken, so a patch whose parameter speed changes
     *  by a factor of a thousand along u cannot throw the iteration back and
     *  forth as plain Newton does).  Sorted by distance, nearest first;
     *  feet that are one point of the patch are listed once.  */
    void feet(const Vec3& p, const Eigen::Vector2d* near, std::vector<Foot>& out) const;

    /*  The nearest foot  */
    Foot closest(const Vec3& p, const Eigen::Vector2d* near = nullptr) const;

    /*  `eval` and `normal` for parameters anywhere on the line, a patch that
     *  closes on itself along u (v) being the same on every period of it  */
    void evalWrapped(double u, double v, Vec3& S, Vec3* Su = nullptr, Vec3* Sv = nullptr) const;
    Vec3 normalWrapped(double u, double v) const;

    /*  The patch closes on itself: its edges u0 and u1 (v0 and v1) are one
     *  curve, so a point there has two parameter values a period apart  */
    bool closedU = false, closedV = false;

    /*  What a unit of u (v) is worth in space: the mean length of Su (Sv)  */
    double speedU = 1, speedV = 1;

    /*  How fast the surface normal turns (radians) per unit of u and of v: a
     *  quarter of the patch turns faster than this.  A mesh of the patch wants
     *  triangles as long in v as they are in u times the ratio of these, so
     *  that each edge turns the surface by about the same, whichever way
     *  it runs: a tube's triangles are long along its axis and short round it.  */
    void turnRates(double& rateU, double& rateV) const;

    /*  Bounding box of the patch  */
    Vec3 lo, hi;

protected:
    void setup();

    // Levenberg-Marquardt from (u, v) to the nearest point of the patch to
    // p, parameters clamped to the domain; returns the squared distance
    double refine(const Vec3& p, double& u, double& v) const;

    bool m_ok = false;
    int pU = 0, pV = 0, nU = 0, nV = 0;
    std::vector<double> KU, KV;
    std::vector<Eigen::Vector4d> Pw;    // homogeneous control points (w x, w y, w z, w)
    bool rational = false;

    // Tessellation over the domain, and a BVH over its triangles
    int gu = 0, gv = 0;
    std::vector<Vec3> P;                // (gu + 1) * (gv + 1) points
    std::vector<Eigen::Vector2d> UV;    // their parameters
    std::vector<std::array<int, 3>> T;  // triangles (indices into P)
    double tessErr = 0;                 // how far the triangles stray from the surface
    double scale = 1;                   // size of the patch

    struct Node
    {
        Eigen::AlignedBox3d box;
        int left = -1, right = -1;
        int start = 0, count = 0;
    };
    std::vector<Node> nodes;
    std::vector<int> order;
    int build(int start, int count, std::vector<Vec3>& centroids);
    int buildInto(std::vector<Node>& ns, std::vector<int>& ord, int start, int count,
                  std::vector<Vec3>& centroids);
};

}   // namespace step
}   // namespace libfive
