/*
libfive: a CAD kernel for modeling with implicit functions

Closed-form (and, for B-splines, numerical) geometry for the analytic and
freeform curve/surface types that appear in real-world STEP files:
  Curves:   LINE, CIRCLE, ELLIPSE, B_SPLINE_CURVE_WITH_KNOTS
  Surfaces: PLANE, CYLINDRICAL_SURFACE, CONICAL_SURFACE, SPHERICAL_SURFACE,
            TOROIDAL_SURFACE, B_SPLINE_SURFACE_WITH_KNOTS

Every surface/curve type answers the same question -- "what's the closest
point to P, and which way does the surface face there" -- in closed form
for the analytic types (no iteration, exact), and via Newton's method
(seeded from a coarse sample) for B-splines. No mesh is ever produced;
this is the same kind of math libfive's own primitives (sphere, cylinder,
...) already use internally.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#pragma once

#include <Eigen/Core>
#include <Eigen/Geometry>
#include <vector>

#include "libfive/step/step_parser.hpp"

namespace libfive {
namespace step {

using Vec3 = Eigen::Vector3d;

struct Placement
{
    Vec3 origin = Vec3::Zero();
    Vec3 zAxis = Vec3(0, 0, 1);   // normal / main axis
    Vec3 xAxis = Vec3(1, 0, 0);   // reference direction
    Vec3 yAxis = Vec3(0, 1, 0);   // zAxis cross xAxis
};

// Resolves the geometric entities that Placement/Curve/Surface are built
// from. `doc` must outlive any Curve/Surface built from it if they were
// to hold references -- they don't; everything below is copied by value.
Vec3 resolvePoint(const Document& doc, int id, bool* ok = nullptr);
Vec3 resolveDirection(const Document& doc, int id, bool* ok = nullptr);
Placement resolvePlacement(const Document& doc, int id, bool* ok = nullptr);

////////////////////////////////////////////////////////////////////////////

enum class CurveKind { Line, Circle, Ellipse, BSpline, Unsupported };

struct Curve
{
    CurveKind kind = CurveKind::Unsupported;
    Placement placement;       // origin/xAxis/yAxis define the curve's plane
    double radius = 0;         // circle radius, or ellipse semi-major axis
    double radius2 = 0;        // ellipse semi-minor axis

    // B-spline data (rational if weights is non-empty)
    int degree = 0;
    std::vector<Vec3> ctrl;
    std::vector<double> weights;
    std::vector<double> knots;  // fully expanded (length = ctrl.size()+degree+1)

    bool valid() const { return kind != CurveKind::Unsupported; }

    Vec3 evalAt(double t) const;

    struct Closest { double t = 0; Vec3 point = Vec3::Zero(); double dist = 1e9; };
    Closest closestPoint(const Vec3& p) const;

    // Parameter domain, used to clamp/seed searches
    double tMin = 0, tMax = 1;

    // Set when this curve came from a STEP TRIMMED_CURVE with explicit
    // parameter-valued trim bounds: the two literal basis-curve
    // parameter values the exporter meant to bound the segment by. When
    // present, these fully determine which arc/segment is meant --
    // there's no need (and, for a >=180 degree arc, no reliable way) to
    // guess it back out from just the edge's start/end vertex points.
    // See sampleEdgeIntoParamSpace's use of this in step_face.cpp.
    bool hasExplicitTrim = false;
    double trimA = 0, trimB = 0;
};

// Builds a Curve from a STEP curve entity (LINE / CIRCLE / ELLIPSE /
// B_SPLINE_CURVE_WITH_KNOTS). Returns an Unsupported curve if the entity
// isn't one of those types.
Curve resolveCurve(const Document& doc, int id);

////////////////////////////////////////////////////////////////////////////

enum class SurfaceKind {
    Plane, Cylinder, Cone, Sphere, Torus, BSpline, Unsupported
};

struct Surface
{
    SurfaceKind kind = SurfaceKind::Unsupported;
    Placement placement;
    // Set true only by tryCylinderFromRationalArc (step_geometry.cpp):
    // that function derives origin/zAxis/radius heuristically from a
    // SolveSpace-style rational-quadratic-Bezier control net, not from
    // an authoritative STEP CYLINDRICAL_SURFACE + AXIS2_PLACEMENT_3D --
    // there is no guarantee its result agrees with whatever sign
    // convention the file's real ADVANCED_FACE.sameSense flag was
    // authored against for a *native* cylinder. Lets halfSpaceValue
    // compensate independently of sameSense itself (see its use there).
    bool orientationUncertain = false;
    double radius = 0;    // cylinder/sphere/torus major radius, cone base radius
    double radius2 = 0;   // torus minor radius
    double semiAngle = 0; // cone half-angle, radians

    // B-spline surface data (rational if weights is non-empty)
    int degreeU = 0, degreeV = 0;
    int nCtrlU = 0, nCtrlV = 0;
    std::vector<Vec3> ctrl;      // row-major, size nCtrlU*nCtrlV
    std::vector<double> weights; // same layout, or empty
    std::vector<double> knotsU, knotsV;

    bool valid() const { return kind != SurfaceKind::Unsupported; }

    struct Closest {
        double u = 0, v = 0;
        Vec3 point = Vec3::Zero();
        Vec3 normal = Vec3(0, 0, 1);
        double dist = 1e9;  // unsigned distance to the point
        double signedDist = 1e9;  // positive on the `normal` side
    };
    // Closest point on the *untrimmed* surface.
    Closest closestPoint(const Vec3& p) const;

    // Same, but seeded from a nearby (u0,v0) instead of doing a coarse
    // grid search first -- for B-splines, this skips straight to Newton
    // refinement. Used when sampling a boundary edge into parameter
    // space, where consecutive samples are close together and a global
    // search per-sample would be needlessly expensive (this is what
    // made resolving a large multi-part assembly's trimmed B-spline
    // faces impractically slow before this was added). Falls back to
    // the full closestPoint() for analytic surface types, where there's
    // no search to seed in the first place.
    Closest closestPointNear(const Vec3& p, double u0, double v0) const;

    Vec3 evalParam(double u, double v) const;
    Vec3 normalAt(double u, double v) const;

private:
    // Shared Gauss-Newton point-inversion step for B-spline surfaces,
    // starting from a given (u,v) guess. Used by both closestPoint
    // (seeded from a coarse grid search) and closestPointNear (seeded
    // by the caller).
    Closest newtonRefineBSpline(const Vec3& p, double u, double v) const;
};

Surface resolveSurface(const Document& doc, int id);

// Signed solid angle subtended by triangle (a,b,c) as seen from p, via
// the Van Oosterom-Strackee formula. Positive when (a,b,c)'s right-hand
// winding faces away from p. Shared by Face::windingContribution
// (step_face.cpp) and Solid::rayCastInside's ray-triangle path indirectly
// depends on the same convention.
double triangleSolidAngle(const Vec3& p, const Vec3& a, const Vec3& b, const Vec3& c);

}  // namespace step
}  // namespace libfive
