/*
libfive: a CAD kernel for modeling with implicit functions

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#pragma once

#include <cmath>
#include <limits>
#include <memory>
#include <vector>

#include <Eigen/Eigen>

#include "libfive/step/step_face.hpp"
#include "libfive/tree/tree.hpp"

namespace libfive {
namespace step {

/*
 *  A cheap closed-form surface fitted to a B-spline face: the lightweight
 *  stand-in for the spline itself (evaluating a spline takes a closest-
 *  point search per point, which the mesher can neither batch nor bound).
 *  Its zero set approximates the face and, near the face, its value is
 *  about the signed distance, so it is ordinary math to the mesher.  An
 *  approximation: its error is measured over the face.
 *
 *  Kinds (w = (p - center) / scale, frame rows = local axes):
 *    PLANE     (p - center) . n                  (n: frame row 2)
 *    QUADRIC   scale * sum_k coef_k m_k(w), the 10 monomials of degree <= 2
 *    EXTRUDED  scale * g(u, v), u = w . e1, v = w . e2: a cubic curve in the
 *              plane across e3, swept along it   (10 coefficients)
 *    REVOLVED  scale * g(r - r0, h - h0), h = w . e3, r = |w - h e3|: a
 *              cubic curve in a half-plane through the axis e3 (through
 *              `center`), spun around it; (r0, h0) = `offset`, the face's
 *              mean position in that half-plane (a gently curved face has
 *              its axis far away: measured from the axis, the curve's
 *              coefficients grew huge and cancelled, and no bound over a
 *              box could see the cancellation)       (10 coefficients)
 *    HELICAL   scale * g(u' - u0, v' - v0): (u, v) = (w . e1, w . e2)
 *              turned by -h / pitch (h = w . e3; w from `center`, a point
 *              of the axis e3): a cubic curve in a plane across the axis
 *              that turns as it advances along it -- the flank of a
 *              helical gear or a worm, a thread, a coil spring, an auger;
 *              (u0, v0) = `offset` (pitch: advance per radian, in
 *              normalized units)                      (10 coefficients)
 *              It costs time where it is used (Bandextruder.stp's gear
 *              rebuilds in 25.7 s instead of 17.5 s) but without it such
 *              parts come apart (the gear's teeth broke into fragments)
 *    FILLET    the rounded corner of radius r between the face's two
 *              neighbours A and B (the rolling-ball blend a CAD system
 *              makes along an edge): with a = sA dA, b = sB dB (their
 *              signed distances, sides chosen so the corner is where both
 *              are negative), min(max(a, b), 0) + |max((a + r, b + r), 0)|
 *              - r -- a quarter pipe round the edge where both offsets
 *              meet, running on into A and B beyond it.  None of the kinds
 *              above can follow a fillet along a curved edge: a plane was
 *              "fitted" to such faces, 1.4 mm off a 2 mm fillet, and the
 *              part came out as a staircase.
 *  Cubic monomials: u^3 u^2v uv^2 v^3 u^2 uv v^2 u v 1; quadric monomials:
 *  x^2 y^2 z^2 xy xz yz x y z 1.
 */
/*  The signed distance to a face's own surface -- an analytic one, or the
 *  surface fitted to a B-spline face: a fillet's neighbours  */
struct FittedSurface;
struct SurfaceDistance
{
    int kind = -1;      // 0 plane, 1 cylinder, 2 sphere, 3 cone, 4 torus, 5 fitted; -1 none
    Vec3 o = Vec3::Zero(), a = Vec3::UnitZ();
    double R = 0, r2 = 0, semi = 0;
    std::shared_ptr<const FittedSurface> fit;

    /*  For a face (kind -1 for a B-spline face without a usable fit)  */
    static SurfaceDistance of(const Face& face);
    double value(const Vec3& p, Vec3* grad = nullptr) const;
    Tree tree() const;
};

struct FittedSurface
{
    enum Kind { NONE = -1, PLANE = 0, QUADRIC = 1, EXTRUDED = 2, REVOLVED = 3, HELICAL = 4, FILLET = 5 };
    Kind kind = NONE;
    Vec3 center = Vec3::Zero();
    double scale = 1.0;
    Eigen::Matrix3d frame = Eigen::Matrix3d::Identity();
    Eigen::Matrix<double, 10, 1> coef = Eigen::Matrix<double, 10, 1>::Zero();
    Eigen::Vector2d offset = Eigen::Vector2d::Zero();
    double pitch = 0.0;

    // FILLET: radius, sides, the neighbours, and an overall sign (the
    // gradient along the face's normal, like the other kinds)
    double radius = 0.0, sA = 1.0, sB = 1.0, sign = 1.0;
    std::shared_ptr<const SurfaceDistance> nbA, nbB;

    // Fit quality over the face's samples, in model units, and the share
    // of samples where the fitted gradient turns against the face normal
    // (the fitted surface folds there: not a usable fit), and where it
    // turns more than 25 degrees from it (it doesn't face the way the face
    // does: a plane standing across a narrow curved strip is within half
    // the strip's width of it, but it is not that strip)
    double maxErr = std::numeric_limits<double>::infinity();
    double rmsErr = std::numeric_limits<double>::infinity();
    double flipped = 0.0;
    double turned = 0.0;

    bool ok() const { return kind != NONE && std::isfinite(maxErr); }

    /*  About the signed distance near the face; grad gets the gradient  */
    double value(const Vec3& p, Vec3* grad = nullptr) const;

    /*  The same function as a libfive tree  */
    Tree tree() const;

    static const char* name(Kind k);
};

/*  Points in a B-spline face's trimmed region, with unit normals  */
struct FaceSamples
{
    std::vector<Vec3> p, n;
    double size = 0.0;     // diagonal of the face's box
};
FaceSamples sampleFace(const Face& face, int grid = 40);

/*  Every kind fitted to the samples (kind NONE where a fit failed)  */
std::vector<FittedSurface> fitCandidates(const FaceSamples& s);

/*  The surface for a face: first, only candidates that have its shape --
 *  they don't fold and face the way the face does over most of it (turned
 *  <= 50 %; by error alone a plane standing across a narrow curved strip
 *  "fitted" it best: it is within half the strip's width of it and faces
 *  the wrong way almost everywhere) -- then, of those, the simplest whose
 *  error is within 25 % of the lowest.  (A tighter shape rule, "facing
 *  about as well as the best candidate", picked a revolved surface 1.7 mm
 *  off over a quadric 0.5 mm off on a hinge blend -- visibly worse.)  If no
 *  candidate has the shape: the one that turns away least.  Kind NONE if
 *  every candidate folds.  */
FittedSurface bestFit(const std::vector<FittedSurface>& candidates);

/*  The surface to use for a B-spline face: the best fit, or when nothing
 *  fits without folding, the best plane -- never nothing: a face that fits
 *  badly is imported anyway, its large error shown to the user  */
FittedSurface fitFace(const Face& face);

/*  A fillet between surfaces A and B, radius r: the best of the four ways
 *  it can sit between them (convex or concave on each side), measured over
 *  the samples (kind NONE if none works).  r <= 0: estimated from the
 *  samples (the median radius that puts them on the quarter pipe).  */
FittedSurface fitFillet(const FaceSamples& s, const std::shared_ptr<const SurfaceDistance>& A,
                        const std::shared_ptr<const SurfaceDistance>& B, double r);

/*  Does this fit have the face's shape at all (no folds, facing the way the
 *  face does at most of it)?  (A fillet's neighbour must, to measure from.)  */
bool fitHasShape(const FittedSurface& f);

}   // namespace step
}   // namespace libfive
