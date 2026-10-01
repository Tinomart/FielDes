/*
libfive: a CAD kernel for modeling with implicit functions
This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#include "libfive/step/step_geometry.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <optional>

namespace libfive {
namespace step {

////////////////////////////////////////////////////////////////////////////
// Entity resolution: CARTESIAN_POINT / DIRECTION / AXIS2_PLACEMENT_3D

Vec3 resolvePoint(const Document& doc, int id, bool* ok)
{
    if (ok) *ok = false;
    const Entity* e = doc.get(id);
    if (!e) return Vec3::Zero();
    const ValueList* coords = e->find("CARTESIAN_POINT");
    if (!coords || coords->size() < 2) return Vec3::Zero();
    // args: (name, (x,y,z))
    const ValueList& xyz = (*coords)[1].asList();
    Vec3 p = Vec3::Zero();
    for (size_t i = 0; i < xyz.size() && i < 3; i++) p[static_cast<int>(i)] = xyz[i].asNumber();
    if (ok) *ok = true;
    return p;
}

Vec3 resolveDirection(const Document& doc, int id, bool* ok)
{
    if (ok) *ok = false;
    const Entity* e = doc.get(id);
    if (!e) return Vec3(0, 0, 1);
    const ValueList* args = e->find("DIRECTION");
    if (!args || args->size() < 2) return Vec3(0, 0, 1);
    const ValueList& xyz = (*args)[1].asList();
    Vec3 d = Vec3::Zero();
    for (size_t i = 0; i < xyz.size() && i < 3; i++) d[static_cast<int>(i)] = xyz[i].asNumber();
    double n = d.norm();
    if (n > 1e-12) d /= n;
    else d = Vec3(0, 0, 1);
    if (ok) *ok = true;
    return d;
}

Placement resolvePlacement(const Document& doc, int id, bool* ok)
{
    if (ok) *ok = false;
    Placement pl;
    const Entity* e = doc.get(id);
    if (!e) return pl;

    // AXIS2_PLACEMENT_3D(name, location, axis(optional), ref_direction(optional))
    if (auto* args = e->find("AXIS2_PLACEMENT_3D")) {
        if (args->size() >= 2) pl.origin = resolvePoint(doc, (*args)[1].asRef());
        if (args->size() >= 3 && (*args)[2].isRef()) {
            pl.zAxis = resolveDirection(doc, (*args)[2].asRef());
        }
        if (args->size() >= 4 && (*args)[3].isRef()) {
            Vec3 x = resolveDirection(doc, (*args)[3].asRef());
            // Gram-Schmidt to keep it exactly orthogonal to zAxis
            x = (x - x.dot(pl.zAxis) * pl.zAxis);
            if (x.norm() > 1e-9) pl.xAxis = x.normalized();
        } else {
            // Pick an arbitrary reference direction perpendicular to zAxis
            Vec3 helper = (std::abs(pl.zAxis.x()) < 0.9) ? Vec3(1, 0, 0) : Vec3(0, 1, 0);
            pl.xAxis = (helper - helper.dot(pl.zAxis) * pl.zAxis).normalized();
        }
        pl.yAxis = pl.zAxis.cross(pl.xAxis).normalized();
        if (ok) *ok = true;
        return pl;
    }

    // AXIS2_PLACEMENT_2D or plain CARTESIAN_POINT fallback: origin only
    if (auto* p = e->find("CARTESIAN_POINT")) {
        (void)p;
        pl.origin = resolvePoint(doc, id);
        if (ok) *ok = true;
    }
    return pl;
}

////////////////////////////////////////////////////////////////////////////
// Shared knot-vector expansion: STEP stores (multiplicities, distinct
// knots); B-spline math wants the fully expanded vector.

static std::vector<double> expandKnots(const ValueList& mults, const ValueList& knots)
{
    std::vector<double> out;
    size_t n = std::min(mults.size(), knots.size());
    for (size_t i = 0; i < n; i++) {
        int m = static_cast<int>(mults[i].asNumber());
        double k = knots[i].asNumber();
        for (int j = 0; j < m; j++) out.push_back(k);
    }
    return out;
}

// The knot vector the STEP B-spline forms without explicit knots stand
// for (ISO 10303-42), for degree p and n control points: UNIFORM (every
// knot simple, evenly spaced, unclamped), QUASI_UNIFORM (the same, but
// clamped: end knots of multiplicity p + 1), BEZIER (one segment) and
// PIECEWISE_BEZIER (interior knots of multiplicity p). Empty if the form
// and counts don't fit.
static std::vector<double> impliedKnots(const std::string& form, int p, int n)
{
    std::vector<double> k;
    if (p < 1 || n <= p) return k;
    if (form == "UNIFORM") {
        for (int i = 0; i < n + p + 1; i++) k.push_back(double(i - p));
    } else if (form == "QUASI_UNIFORM") {
        for (int i = 0; i <= p; i++) k.push_back(0.0);
        for (int i = 1; i < n - p; i++) k.push_back(double(i));
        for (int i = 0; i <= p; i++) k.push_back(double(n - p));
    } else if (form == "BEZIER" || form == "PIECEWISE_BEZIER") {
        if ((n - 1) % p != 0) return k;
        const int segs = (n - 1) / p;
        for (int i = 0; i <= p; i++) k.push_back(0.0);
        for (int sg = 1; sg < segs; sg++) for (int i = 0; i < p; i++) k.push_back(double(sg));
        for (int i = 0; i <= p; i++) k.push_back(double(segs));
    }
    return k;
}

// Which of those forms an entity is written in ("" for none), given the
// entity kind ("CURVE" / "SURFACE"); complex entities list it as an empty
// aspect (e.g. QUASI_UNIFORM_CURVE()), flat ones as the entity itself
static std::string impliedForm(const Entity* e, const std::string& kind)
{
    for (const char* form : {"QUASI_UNIFORM", "UNIFORM", "PIECEWISE_BEZIER", "BEZIER"}) {
        if (e->find(std::string(form) + "_" + kind)) return form;
    }
    return "";
}

// Finds the knot span index i such that U[i] <= t < U[i+1] (clamped),
// for a curve/row of degree p with n+1 control points (knots.size() ==
// n+p+2).
static int findSpan(int n, int p, double t, const std::vector<double>& U)
{
    if (t >= U[n + 1]) return n;
    if (t <= U[p]) return p;
    int lo = p, hi = n + 1;
    while (hi - lo > 1) {
        int mid = (lo + hi) / 2;
        if (t < U[mid]) hi = mid; else lo = mid;
    }
    return lo;
}

// De Boor's algorithm on homogeneous control points (x*w, y*w, z*w, w).
// Returns the (still-homogeneous) point; caller divides by w.
static Eigen::Vector4d deBoor(int p, const std::vector<double>& U,
                               const std::vector<Eigen::Vector4d>& Pw, double t)
{
    int n = static_cast<int>(Pw.size()) - 1;
    int k = findSpan(n, p, t, U);
    std::vector<Eigen::Vector4d> d(Pw.begin() + (k - p), Pw.begin() + (k + 1));
    for (int r = 1; r <= p; r++) {
        for (int j = p; j >= r; j--) {
            double denom = U[k + 1 + j - r] - U[k - p + j];
            double alpha = (denom > 1e-12) ? (t - U[k - p + j]) / denom : 0.0;
            d[j] = (1 - alpha) * d[j - 1] + alpha * d[j];
        }
    }
    return d[p];
}

static std::vector<Eigen::Vector4d> toHomogeneous(const std::vector<Vec3>& ctrl,
                                                    const std::vector<double>& w)
{
    std::vector<Eigen::Vector4d> out(ctrl.size());
    for (size_t i = 0; i < ctrl.size(); i++) {
        double wi = w.empty() ? 1.0 : w[i];
        out[i] = Eigen::Vector4d(ctrl[i].x() * wi, ctrl[i].y() * wi, ctrl[i].z() * wi, wi);
    }
    return out;
}

////////////////////////////////////////////////////////////////////////////
// Curve

Curve resolveCurve(const Document& doc, int id)
{
    Curve c;
    const Entity* e = doc.get(id);
    if (!e) return c;

    if (auto* args = e->find("LINE")) {
        // LINE(name, point, vector)
        if (args->size() < 3) return c;
        c.kind = CurveKind::Line;
        c.placement.origin = resolvePoint(doc, (*args)[1].asRef());
        // VECTOR(name, direction, magnitude)
        const Entity* vecE = doc.get((*args)[2].asRef());
        Vec3 dir(1, 0, 0);
        double mag = 1.0;
        if (vecE) {
            if (auto* vargs = vecE->find("VECTOR")) {
                if (vargs->size() >= 3) {
                    dir = resolveDirection(doc, (*vargs)[1].asRef());
                    mag = (*vargs)[2].asNumber(1.0);
                }
            }
        }
        c.placement.zAxis = dir;
        c.radius = mag;  // reused as "speed" for evalAt below
        c.tMin = 0; c.tMax = 1e6;  // lines are trimmed externally by edge params
        return c;
    }

    if (auto* args = e->find("CIRCLE")) {
        // CIRCLE(name, position, radius)
        if (args->size() < 3) return c;
        c.kind = CurveKind::Circle;
        c.placement = resolvePlacement(doc, (*args)[1].asRef());
        c.radius = (*args)[2].asNumber();
        c.tMin = 0; c.tMax = 2 * M_PI;
        return c;
    }

    if (auto* args = e->find("ELLIPSE")) {
        // ELLIPSE(name, position, semi_axis_1, semi_axis_2)
        if (args->size() < 4) return c;
        c.kind = CurveKind::Ellipse;
        c.placement = resolvePlacement(doc, (*args)[1].asRef());
        c.radius = (*args)[2].asNumber();
        c.radius2 = (*args)[3].asNumber();
        c.tMin = 0; c.tMax = 2 * M_PI;
        return c;
    }

    // B-spline curves appear in STEP files in two different shapes, and
    // real exporters use both:
    //   (a) a genuine "complex entity" with separate B_SPLINE_CURVE and
    //       B_SPLINE_CURVE_WITH_KNOTS (and optionally
    //       RATIONAL_B_SPLINE_CURVE) aspects, each with their own args
    //   (b) a single B_SPLINE_CURVE_WITH_KNOTS(...) call whose argument
    //       list concatenates every field from both levels (this is
    //       what e.g. "Spatial InterOp 3D" -- nTop's STEP translator --
    //       writes; confirmed against a real HingedTable.step export)
    // Form (b) is detected by arg count and handled directly; form (a)
    // falls back to the original per-aspect lookups.
    // (c) the forms whose knots are implied (QUASI_UNIFORM_CURVE, ...; see
    //     impliedKnots), flat or as a complex entity -- read as a
    //     straight chord before, e.g. the hyperbolic edges where a screw's
    //     socket chamfer meets the hexagon (Bandextruder.stp)
    const std::string curveForm = e->find("B_SPLINE_CURVE_WITH_KNOTS") ? "" : impliedForm(e, "CURVE");
    const ValueList* curveKnotArgs = e->find("B_SPLINE_CURVE_WITH_KNOTS");
    if (!curveKnotArgs && !curveForm.empty()) curveKnotArgs = e->find(curveForm + "_CURVE");
    if (auto* wargs = curveKnotArgs) {
        bool flat = !e->find("B_SPLINE_CURVE") && wargs->size() >= (curveForm.empty() ? 9u : 3u);
        const ValueList* bargs = flat ? wargs : e->find("B_SPLINE_CURVE");
        // The flat form carries a leading `name` argument; the separate
        // B_SPLINE_CURVE aspect of a complex entity (what SolveSpace
        // writes) does NOT -- there the degree is argument 0. Reading it
        // at the flat form's offset made the degree 0 and the control-point
        // list empty, so every such curve was "unsupported" and silently
        // replaced by a straight chord between its endpoints (every arc
        // edge in every SolveSpace export was a chord in the face trims).
        const size_t off = flat ? 1 : 0;
        if (bargs && bargs->size() >= off + 2) {
            c.degree = static_cast<int>((*bargs)[off].asNumber());
            for (auto& pv : (*bargs)[off + 1].asList()) {
                c.ctrl.push_back(resolvePoint(doc, pv.asRef()));
            }
            if (!curveForm.empty()) {
                c.knots = impliedKnots(curveForm, c.degree, int(c.ctrl.size()));
            } else if (flat) {
                // B_SPLINE_CURVE_WITH_KNOTS(name, degree, ctrl_pts, form,
                //   closed, self_int, knot_mults, knots, knot_spec)
                c.knots = expandKnots((*wargs)[6].asList(), (*wargs)[7].asList());
            } else if (wargs->size() >= 2) {
                c.knots = expandKnots((*wargs)[0].asList(), (*wargs)[1].asList());
            }
            if (auto* rargs = e->find("RATIONAL_B_SPLINE_CURVE")) {
                if (!rargs->empty()) {
                    for (auto& wv : (*rargs)[0].asList()) c.weights.push_back(wv.asNumber(1.0));
                }
            }
            if (c.degree > 0 && !c.ctrl.empty() &&
                static_cast<int>(c.knots.size()) == static_cast<int>(c.ctrl.size()) + c.degree + 1) {
                c.kind = CurveKind::BSpline;
                c.tMin = c.knots[c.degree];
                c.tMax = c.knots[c.knots.size() - 1 - c.degree];
            }
        }
        return c;
    }

    if (auto* args = e->find("TRIMMED_CURVE")) {
        // TRIMMED_CURVE(name, basis_curve, trim_1, trim_2, sense_agreement,
        //   master_representation). trim_1/trim_2 are each a SELECT,
        //   written as a 1-2 item list that can hold a literal parameter
        //   value and/or a cartesian point reference -- real exporters
        //   (confirmed against a HingedTable.step export) wrap every
        //   circular/B-spline edge in one of these, so leaving this
        //   unhandled silently degraded every curved edge in a real file
        //   to a straight chord between its endpoints.
        if (args->size() < 4) return c;
        c = resolveCurve(doc, (*args)[1].asRef());
        if (!c.valid()) return c;

        // A literal trim parameter on a circle/ellipse is an angle, written
        // in the file's plane-angle unit (degrees in many exports).
        const bool angular = c.kind == CurveKind::Circle || c.kind == CurveKind::Ellipse;
        const double paramScale = angular ? doc.planeAngleFactor() : 1.0;
        auto paramFromSelect = [&](const Value& sel) -> std::optional<double> {
            if (!sel.isList()) return std::nullopt;
            for (auto& item : sel.asList()) {
                if (item.isNumber()) return item.asNumber() * paramScale;
            }
            // No literal parameter given -- fall back to projecting
            // whatever cartesian point is present onto the basis curve.
            for (auto& item : sel.asList()) {
                if (item.isRef()) {
                    Vec3 p = resolvePoint(doc, item.asRef());
                    return c.closestPoint(p).t;
                }
            }
            return std::nullopt;
        };
        auto ta = paramFromSelect((*args)[2]);
        auto tb = paramFromSelect((*args)[3]);
        if (ta && tb) {
            c.hasExplicitTrim = true;
            c.trimA = *ta;
            c.trimB = *tb;
        }
        return c;
    }

    return c;  // Unsupported (e.g. OFFSET_CURVE, ...)
}

Vec3 Curve::evalAt(double t) const
{
    switch (kind) {
        case CurveKind::Line:
            return placement.origin + t * placement.zAxis;
        case CurveKind::Circle:
            return placement.origin + radius * (std::cos(t) * placement.xAxis +
                                                 std::sin(t) * placement.yAxis);
        case CurveKind::Ellipse:
            return placement.origin + radius * std::cos(t) * placement.xAxis +
                   radius2 * std::sin(t) * placement.yAxis;
        case CurveKind::BSpline: {
            auto Pw = toHomogeneous(ctrl, weights);
            double tc = std::clamp(t, tMin, tMax);
            Eigen::Vector4d h = deBoor(degree, knots, Pw, tc);
            return h.head<3>() / h.w();
        }
        default:
            return Vec3::Zero();
    }
}

Curve::Closest Curve::closestPoint(const Vec3& p) const
{
    Closest best;
    switch (kind) {
        case CurveKind::Line: {
            double t = (p - placement.origin).dot(placement.zAxis);
            best.t = t;
            best.point = placement.origin + t * placement.zAxis;
            best.dist = (p - best.point).norm();
            return best;
        }
        case CurveKind::Circle: {
            Vec3 d = p - placement.origin;
            double x = d.dot(placement.xAxis), y = d.dot(placement.yAxis);
            double t = std::atan2(y, x);
            best.t = t;
            best.point = evalAt(t);
            best.dist = (p - best.point).norm();
            return best;
        }
        case CurveKind::Ellipse: {
            // No closed form; a handful of Newton steps on the angle
            // converges quickly since ellipses are well-conditioned.
            Vec3 d = p - placement.origin;
            double t = std::atan2(d.dot(placement.yAxis) / std::max(radius2, 1e-9),
                                   d.dot(placement.xAxis) / std::max(radius, 1e-9));
            for (int i = 0; i < 8; i++) {
                Vec3 c = evalAt(t);
                Vec3 dc = radius * -std::sin(t) * placement.xAxis +
                          radius2 * std::cos(t) * placement.yAxis;
                double num = (c - p).dot(dc);
                double den = dc.dot(dc);
                if (den < 1e-12) break;
                t -= num / den;
            }
            best.t = t;
            best.point = evalAt(t);
            best.dist = (p - best.point).norm();
            return best;
        }
        case CurveKind::BSpline: {
            // Coarse sample, then Newton refine using a numerical derivative.
            const int N = 24;
            double bestT = tMin, bestD = 1e18;
            for (int i = 0; i <= N; i++) {
                double t = tMin + (tMax - tMin) * i / N;
                double d = (evalAt(t) - p).squaredNorm();
                if (d < bestD) { bestD = d; bestT = t; }
            }
            double t = bestT;
            const double h = std::max((tMax - tMin) * 1e-4, 1e-6);
            for (int i = 0; i < 6; i++) {
                Vec3 c = evalAt(t);
                Vec3 dc = (evalAt(std::min(t + h, tMax)) - evalAt(std::max(t - h, tMin))) /
                          (std::min(t + h, tMax) - std::max(t - h, tMin));
                double den = dc.dot(dc);
                if (den < 1e-12) break;
                double step = (c - p).dot(dc) / den;
                t = std::clamp(t - step, tMin, tMax);
            }
            best.t = t;
            best.point = evalAt(t);
            best.dist = (p - best.point).norm();
            return best;
        }
        default:
            return best;
    }
}

////////////////////////////////////////////////////////////////////////////
// Surface

namespace {

// Exact circumcircle of 3 non-collinear 3D points, via the standard
// "equidistant + coplanar" 3x3 linear system (verified by hand against
// a known case: p0=(0,0,0),p1=(1,0,0),p2=(0,1,0) -> center=(0.5,0.5,0),
// radius=sqrt(0.5)).
struct Circle3 { Vec3 center; double radius; Vec3 normal; bool ok; };

Circle3 circumcircle3D(const Vec3& p0, const Vec3& p1, const Vec3& p2)
{
    Vec3 d1 = p1 - p0, d2 = p2 - p0;
    Vec3 n = d1.cross(d2);
    double nLen = n.norm();
    if (nLen < 1e-12) return {Vec3::Zero(), 0.0, Vec3::Zero(), false};
    n /= nLen;
    Eigen::Matrix3d A;
    A.row(0) = d1;
    A.row(1) = d2;
    A.row(2) = n;
    Eigen::Vector3d b;
    b(0) = 0.5 * (p1.squaredNorm() - p0.squaredNorm());
    b(1) = 0.5 * (p2.squaredNorm() - p0.squaredNorm());
    b(2) = n.dot(p0);
    Vec3 center = A.colPivHouseholderQr().solve(b);
    return {center, (center - p0).norm(), n, true};
}

// SolveSpace's STEP exporter (confirmed against a real user-provided
// file, 2026-09-24) writes EVERY surface as a B_SPLINE_SURFACE, even
// trivially analytic ones -- a flat quad becomes a degree-(1,1)
// bilinear patch, and a cylindrical wall becomes a degree-(2,1) patch
// where the degree-2 direction is a single rational-quadratic-Bezier
// arc (the standard exact NURBS representation of a circular arc: the
// classic "weights (1, cos(theta/2), 1)" construction). Both are
// genuinely analytic surfaces wearing a B-spline entity type, not
// approximations -- detecting them here (instead of leaving every such
// face to fall back to the Oracle) is what makes files from this (and
// likely other, similarly-simplified) exporters usable for real CSG
// reconstruction at all.

// Degree-(1,1), 2x2-control-point bilinear patch whose 4 corners are
// coplanar -> exact PLANE. ctrl is row-major (nCtrlU outer, nCtrlV
// inner): ctrl[0]=(u0,v0), ctrl[1]=(u0,v1), ctrl[2]=(u1,v0), ctrl[3]=(u1,v1).
bool tryPlaneFromBilinear(Surface& s)
{
    if (s.degreeU != 1 || s.degreeV != 1 || s.nCtrlU != 2 || s.nCtrlV != 2) return false;
    if (s.ctrl.size() != 4) return false;
    const Vec3& p00 = s.ctrl[0];
    const Vec3& p01 = s.ctrl[1];
    const Vec3& p10 = s.ctrl[2];
    const Vec3& p11 = s.ctrl[3];
    Vec3 eu = p10 - p00, ev = p01 - p00;
    Vec3 normal = eu.cross(ev);
    double normalLen = normal.norm();
    double scale = std::max({eu.norm(), ev.norm(), 1e-9});
    if (normalLen < 1e-9 * scale) return false;  // degenerate (zero-area) patch
    normal /= normalLen;
    // Coplanarity of the 4th corner, relative to the patch's own size.
    double planarity = std::abs((p11 - p00).dot(normal));
    if (planarity > 1e-6 * scale) return false;

    s.kind = SurfaceKind::Plane;
    s.placement.origin = p00;
    s.placement.zAxis = normal;
    Vec3 xref = eu.normalized();
    s.placement.xAxis = xref;
    s.placement.yAxis = normal.cross(xref);
    return true;
}

// Degree-(2,1) (arc direction U, linear/axial direction V) or
// degree-(1,2) (swapped), where the degree-2 direction's weights follow
// the (1, w, 1) rational-arc pattern at every axial control row ->
// exact CYLINDRICAL_SURFACE. Verifies the arc radius and axis direction
// agree between the two axial rows (not just curve-fitting one row and
// hoping), using exact points sampled from the rational-quadratic-Bezier
// formula itself (t=0 and t=1 are exactly the endpoints; t=0.5 is the
// exact weighted midpoint -- no approximation/iteration involved).
bool tryCylinderFromRationalArc(Surface& s)
{
    bool swapped = false;
    int arcDeg = s.degreeU, axialDeg = s.degreeV;
    int nArc = s.nCtrlU, nAxial = s.nCtrlV;
    if (arcDeg == 1 && axialDeg == 2 && s.nCtrlV == 3 && s.nCtrlU == 2) {
        swapped = true;
        arcDeg = s.degreeV; axialDeg = s.degreeU;
        nArc = s.nCtrlV; nAxial = s.nCtrlU;
    }
    if (arcDeg != 2 || axialDeg != 1 || nArc != 3 || nAxial != 2) return false;
    if (s.weights.size() != s.ctrl.size() || s.ctrl.size() != 6) return false;

    // Fetch (arc-index, axial-index) respecting the possibly-swapped
    // (U,V) roles -- storage is always row-major with U as the outer
    // index, nCtrlV as the inner stride.
    auto ctrlAt = [&](int arcIdx, int axialIdx) -> const Vec3& {
        return swapped ? s.ctrl[static_cast<size_t>(axialIdx) * s.nCtrlV + arcIdx]
                        : s.ctrl[static_cast<size_t>(arcIdx) * s.nCtrlV + axialIdx];
    };
    auto weightAt = [&](int arcIdx, int axialIdx) -> double {
        return swapped ? s.weights[static_cast<size_t>(axialIdx) * s.nCtrlV + arcIdx]
                        : s.weights[static_cast<size_t>(arcIdx) * s.nCtrlV + axialIdx];
    };

    Circle3 circles[2];
    for (int axialIdx = 0; axialIdx < 2; axialIdx++) {
        double w0 = weightAt(0, axialIdx), w1 = weightAt(1, axialIdx), w2 = weightAt(2, axialIdx);
        if (std::abs(w0 - 1.0) > 1e-6 || std::abs(w2 - 1.0) > 1e-6) return false;
        if (w1 <= 1e-6 || w1 >= 1.0 - 1e-6) return false;  // must be a genuine arc, not degenerate
        Vec3 p0 = ctrlAt(0, axialIdx), p1 = ctrlAt(1, axialIdx), p2 = ctrlAt(2, axialIdx);
        // Exact rational-quadratic-Bezier point at t=0.5: weighted basis
        // (0.25, 0.5*w1, 0.25) normalized by their sum.
        Vec3 mid = (0.25 * p0 + 0.5 * w1 * p1 + 0.25 * p2) / (0.5 + 0.5 * w1);
        Circle3 c = circumcircle3D(p0, mid, p2);
        if (!c.ok) return false;
        circles[axialIdx] = c;
    }

    double radius = 0.5 * (circles[0].radius + circles[1].radius);
    if (std::abs(circles[0].radius - circles[1].radius) > 1e-4 * std::max(radius, 1e-9)) return false;

    Vec3 axis = circles[1].center - circles[0].center;
    double axisLen = axis.norm();
    if (axisLen < 1e-9) return false;  // both rows at the same axial position -- degenerate
    axis /= axisLen;
    // The two arcs' own fitted-plane normals should agree with the
    // derived axis direction (up to sign) -- confirms this is truly a
    // uniform extrusion (a cylinder), not some other doubly-curved
    // surface that happened to fit a circle at these two rows alone.
    if (std::abs(circles[0].normal.dot(axis)) < 1.0 - 1e-3) return false;
    if (std::abs(circles[1].normal.dot(axis)) < 1.0 - 1e-3) return false;

    s.kind = SurfaceKind::Cylinder;
    s.orientationUncertain = true;  // conservative default; resolved below when the net allows
    s.radius = radius;
    s.placement.origin = circles[0].center;
    s.placement.zAxis = axis;

    // Resolve the orientation definitively instead of guessing: the
    // analytic cylinder's sign convention is "positive on the radially
    // OUTWARD side", but the STEP face's sameSense is authored against the
    // B-spline surface's own parametric normal (dS/dU x dS/dV), which for
    // this degenerate encoding may point either radially outward or
    // inward, independently per patch. So the sameSense flip to apply is
    // exactly "does the parametric normal point radially inward". The
    // original control net is still here, so evaluate the (Bezier)
    // rational patch at its centre and compare directly. Previously every
    // such cylinder was flipped unconditionally by the reconstruction and
    // none by the oracle -- both wrong for at least some patches.
    {
        auto bern = [](int deg, int i, double t) {
            double c = 1.0;
            for (int k = 1; k <= i; k++) c = c * (deg - k + 1) / k;
            return c * std::pow(t, i) * std::pow(1.0 - t, deg - i);
        };
        auto evalNet = [&](double u, double v) -> Vec3 {
            Vec3 num = Vec3::Zero();
            double den = 0.0;
            for (int i = 0; i < s.nCtrlU; i++) {
                for (int j = 0; j < s.nCtrlV; j++) {
                    size_t idx = static_cast<size_t>(i) * s.nCtrlV + j;
                    double w = s.weights[idx] * bern(s.degreeU, i, u) * bern(s.degreeV, j, v);
                    num += w * s.ctrl[idx];
                    den += w;
                }
            }
            return num / den;
        };
        const double h = 1e-3;
        Vec3 P = evalNet(0.5, 0.5);
        Vec3 dU = (evalNet(0.5 + h, 0.5) - evalNet(0.5 - h, 0.5)) / (2 * h);
        Vec3 dV = (evalNet(0.5, 0.5 + h) - evalNet(0.5, 0.5 - h)) / (2 * h);
        Vec3 nB = dU.cross(dV);
        Vec3 d = P - circles[0].center;
        Vec3 radialOut = d - d.dot(axis) * axis;
        if (nB.norm() > 1e-12 && radialOut.norm() > 1e-12) {
            double c = nB.normalized().dot(radialOut.normalized());
            if (std::abs(c) > 0.9) s.orientationUncertain = (c < 0.0);
            if (std::getenv("FIELDES_STEP_DEBUG_EDGES"))
                fprintf(stderr, "[cylOrient] param normal . radialOut = %.4f -> flip=%d\n",
                        c, int(s.orientationUncertain));
        }
    }
    Vec3 xref = (ctrlAt(0, 0) - circles[0].center);
    xref -= xref.dot(axis) * axis;  // project off the axis, in case of noise
    double xrefLen = xref.norm();
    if (xrefLen < 1e-9) return false;
    xref /= xrefLen;
    s.placement.xAxis = xref;
    s.placement.yAxis = axis.cross(xref);
    return true;
}

// Same degree-(2,1)/(1,2) rational-arc encoding as tryCylinderFromRationalArc
// (two axial rows, each a genuine (1,w,1) rational-quadratic-Bezier circular
// arc), but for the two rows' radii DIFFERING -- a conical frustum instead of
// a cylinder. SolveSpace writes a face this way whenever a revolved profile
// gets cut/intersected so the resulting band isn't a pure cylinder (confirmed
// against a real user file, RevolveAndExtrude.step, where a revolve and an
// extrude intersect). Shares every verification step with the cylinder case
// (both rows genuinely circular, coplanar-with-axis, axis well-defined) and
// only diverges at the final radius check.
bool tryConeFromRationalArc(Surface& s)
{
    bool swapped = false;
    int arcDeg = s.degreeU, axialDeg = s.degreeV;
    int nArc = s.nCtrlU, nAxial = s.nCtrlV;
    if (arcDeg == 1 && axialDeg == 2 && s.nCtrlV == 3 && s.nCtrlU == 2) {
        swapped = true;
        arcDeg = s.degreeV; axialDeg = s.degreeU;
        nArc = s.nCtrlV; nAxial = s.nCtrlU;
    }
    if (arcDeg != 2 || axialDeg != 1 || nArc != 3 || nAxial != 2) return false;
    if (s.weights.size() != s.ctrl.size() || s.ctrl.size() != 6) return false;

    auto ctrlAt = [&](int arcIdx, int axialIdx) -> const Vec3& {
        return swapped ? s.ctrl[static_cast<size_t>(axialIdx) * s.nCtrlV + arcIdx]
                        : s.ctrl[static_cast<size_t>(arcIdx) * s.nCtrlV + axialIdx];
    };
    auto weightAt = [&](int arcIdx, int axialIdx) -> double {
        return swapped ? s.weights[static_cast<size_t>(axialIdx) * s.nCtrlV + arcIdx]
                        : s.weights[static_cast<size_t>(arcIdx) * s.nCtrlV + axialIdx];
    };

    Circle3 circles[2];
    for (int axialIdx = 0; axialIdx < 2; axialIdx++) {
        double w0 = weightAt(0, axialIdx), w1 = weightAt(1, axialIdx), w2 = weightAt(2, axialIdx);
        if (std::abs(w0 - 1.0) > 1e-6 || std::abs(w2 - 1.0) > 1e-6) return false;
        if (w1 <= 1e-6 || w1 >= 1.0 - 1e-6) return false;
        Vec3 p0 = ctrlAt(0, axialIdx), p1 = ctrlAt(1, axialIdx), p2 = ctrlAt(2, axialIdx);
        Vec3 mid = (0.25 * p0 + 0.5 * w1 * p1 + 0.25 * p2) / (0.5 + 0.5 * w1);
        Circle3 c = circumcircle3D(p0, mid, p2);
        if (!c.ok) return false;
        circles[axialIdx] = c;
    }

    // The cylinder recogniser already handles equal-radius rows; only take
    // the genuinely conical case here, and require a well-formed frustum
    // (both radii non-negative, not both ~0).
    double rMax = std::max(circles[0].radius, circles[1].radius);
    if (std::abs(circles[0].radius - circles[1].radius) <= 1e-4 * std::max(rMax, 1e-9)) return false;
    if (rMax < 1e-9) return false;

    Vec3 axis = circles[1].center - circles[0].center;
    double axisLen = axis.norm();
    if (axisLen < 1e-9) return false;
    axis /= axisLen;
    if (std::abs(circles[0].normal.dot(axis)) < 1.0 - 1e-3) return false;
    if (std::abs(circles[1].normal.dot(axis)) < 1.0 - 1e-3) return false;
    // Both rows' fitted-circle axes must also coincide on a single 3D line
    // (not just be parallel) -- otherwise this is some other doubly-curved
    // surface, not a true cone. The apex, if any, lies on that shared line.
    Vec3 centerOff = circles[1].center - circles[0].center;
    if ((centerOff - centerOff.dot(axis) * axis).norm() > 1e-4 * std::max(axisLen, 1.0)) return false;

    s.kind = SurfaceKind::Cone;
    s.orientationUncertain = true;
    s.radius = circles[0].radius;
    s.semiAngle = std::atan2(circles[1].radius - circles[0].radius, axisLen);
    s.placement.origin = circles[0].center;
    s.placement.zAxis = axis;

    {
        auto bern = [](int deg, int i, double t) {
            double c = 1.0;
            for (int k = 1; k <= i; k++) c = c * (deg - k + 1) / k;
            return c * std::pow(t, i) * std::pow(1.0 - t, deg - i);
        };
        auto evalNet = [&](double u, double v) -> Vec3 {
            Vec3 num = Vec3::Zero();
            double den = 0.0;
            for (int i = 0; i < s.nCtrlU; i++) {
                for (int j = 0; j < s.nCtrlV; j++) {
                    size_t idx = static_cast<size_t>(i) * s.nCtrlV + j;
                    double w = s.weights[idx] * bern(s.degreeU, i, u) * bern(s.degreeV, j, v);
                    num += w * s.ctrl[idx];
                    den += w;
                }
            }
            return num / den;
        };
        const double h = 1e-3;
        Vec3 P = evalNet(0.5, 0.5);
        Vec3 dU = (evalNet(0.5 + h, 0.5) - evalNet(0.5 - h, 0.5)) / (2 * h);
        Vec3 dV = (evalNet(0.5, 0.5 + h) - evalNet(0.5, 0.5 - h)) / (2 * h);
        Vec3 nB = dU.cross(dV);
        // Analytic outward normal at the same point: radial minus the
        // along-axis slope contribution (see Surface::closestPoint's Cone
        // case for the same construction).
        Vec3 d = P - s.placement.origin;
        double ax = d.dot(axis);
        Vec3 e_r = (d - ax * axis).normalized();
        double c = std::cos(s.semiAngle), sn = std::sin(s.semiAngle);
        Vec3 nA = e_r * c - axis * sn;
        if (nB.norm() > 1e-12 && nA.norm() > 1e-12) {
            double dp = nB.normalized().dot(nA.normalized());
            if (std::abs(dp) > 0.9) s.orientationUncertain = (dp < 0.0);
            if (std::getenv("FIELDES_STEP_DEBUG_EDGES"))
                fprintf(stderr, "[coneOrient] R0=%.8f R1=%.8f dR=%.8f axisLen=%.6f semiAngle=%.8f param normal . outward = %.4f -> flip=%d\n",
                        circles[0].radius, circles[1].radius, circles[1].radius - circles[0].radius,
                        axisLen, s.semiAngle, dp, int(s.orientationUncertain));
        }
    }
    Vec3 xref = (ctrlAt(0, 0) - circles[0].center);
    xref -= xref.dot(axis) * axis;
    double xrefLen = xref.norm();
    if (xrefLen < 1e-9) return false;
    xref /= xrefLen;
    s.placement.xAxis = xref;
    s.placement.yAxis = axis.cross(xref);
    return true;
}

// Exact point on a single rational Bezier patch (clamped knots 0..1).
Vec3 evalBezierNet(const Surface& s, double u, double v)
{
    auto bern = [](int deg, int i, double t) {
        double c = 1.0;
        for (int k = 1; k <= i; k++) c = c * (deg - k + 1) / k;
        return c * std::pow(t, i) * std::pow(1.0 - t, deg - i);
    };
    Vec3 num = Vec3::Zero();
    double den = 0.0;
    for (int i = 0; i < s.nCtrlU; i++) {
        for (int j = 0; j < s.nCtrlV; j++) {
            size_t idx = static_cast<size_t>(i) * s.nCtrlV + j;
            double w = s.weights[idx] * bern(s.degreeU, i, u) * bern(s.degreeV, j, v);
            num += w * s.ctrl[idx];
            den += w;
        }
    }
    return num / den;
}

bool isBezierKnots(const std::vector<double>& k, int deg)
{
    if (static_cast<int>(k.size()) != 2 * (deg + 1)) return false;
    for (int i = 0; i <= deg; i++) {
        if (std::abs(k[i] - k[0]) > 1e-9) return false;
        if (std::abs(k[deg + 1 + i] - k[deg + 1]) > 1e-9) return false;
    }
    return k[deg + 1] > k[0];
}

// Degree-(2,2), 3x3 rational Bezier patch that is a surface of revolution
// whose profile is a circle: exact SPHERE (profile centre on the axis) or
// TORUS (profile centre off the axis; spindle tori with minor > major
// radius included). SolveSpace writes every revolved arc this way -- one
// patch per quarter turn -- and until recognised they left the whole solid
// oracle-backed. Detection is by fit-and-verify: find the revolution axis
// from three sweep circles, fit the profile circle in the (radius, height)
// half plane, then check that a 5x5 grid of exact surface points lies on
// the fitted analytic surface.
bool tryRevolvedArcPatch(Surface& s)
{
    if (s.degreeU != 2 || s.degreeV != 2 || s.nCtrlU != 3 || s.nCtrlV != 3) return false;
    if (s.ctrl.size() != 9 || s.weights.size() != 9) return false;
    if (!isBezierKnots(s.knotsU, 2) || !isBezierKnots(s.knotsV, 2)) return false;

    double scale = 0.0;
    for (auto& p : s.ctrl) scale = std::max(scale, (p - s.ctrl[0]).norm());
    if (scale < 1e-9) return false;

    auto P = [&](bool sweepIsU, double sweep, double prof) {
        return sweepIsU ? evalBezierNet(s, sweep, prof) : evalBezierNet(s, prof, sweep);
    };

    for (int dir = 0; dir < 2; dir++) {
        const bool sweepIsU = (dir == 0);
        // Three sweep circles (constant profile parameter).
        Circle3 cs[3];
        const double profT[3] = {0.3, 0.5, 0.7};
        bool ok = true;
        for (int k = 0; k < 3 && ok; k++) {
            cs[k] = circumcircle3D(P(sweepIsU, 0.0, profT[k]), P(sweepIsU, 0.5, profT[k]),
                                   P(sweepIsU, 1.0, profT[k]));
            ok = cs[k].ok && cs[k].radius > 1e-6 * scale;
        }
        if (!ok) continue;
        Vec3 axis = cs[0].normal.normalized();
        for (int k = 1; k < 3 && ok; k++) {
            if (std::abs(cs[k].normal.dot(axis)) < 1.0 - 1e-6) ok = false;
            Vec3 dc = cs[k].center - cs[0].center;
            if ((dc - dc.dot(axis) * axis).norm() > 1e-6 * scale) ok = false;
        }
        if (!ok) continue;
        const Vec3 c0 = cs[0].center;

        // Profile points in the (rho, h) half plane at sweep parameter 0.
        auto rh = [&](const Vec3& p) {
            Vec3 d = p - c0;
            double h = d.dot(axis);
            return std::make_pair((d - h * axis).norm(), h);
        };
        auto q0 = rh(P(sweepIsU, 0.0, 0.0));
        auto q1 = rh(P(sweepIsU, 0.0, 0.5));
        auto q2 = rh(P(sweepIsU, 0.0, 1.0));
        // Circle through 3 2D points.
        double ax = q0.first, ay = q0.second, bx = q1.first, by = q1.second,
               cx = q2.first, cy = q2.second;
        double dd = 2.0 * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by));
        if (std::abs(dd) < 1e-12 * scale * scale) continue;
        double ux = ((ax * ax + ay * ay) * (by - cy) + (bx * bx + by * by) * (cy - ay) +
                     (cx * cx + cy * cy) * (ay - by)) / dd;
        double uy = ((ax * ax + ay * ay) * (cx - bx) + (bx * bx + by * by) * (ax - cx) +
                     (cx * cx + cy * cy) * (bx - ax)) / dd;
        double r = std::hypot(ax - ux, ay - uy);
        if (r < 1e-6 * scale) continue;
        const bool isSphere = std::abs(ux) < 1e-6 * r;
        if (!isSphere && ux < 0) continue;

        // Verify: every grid point lies on the fitted profile circle.
        for (int i = 0; i <= 4 && ok; i++)
            for (int j = 0; j <= 4 && ok; j++) {
                auto q = rh(P(sweepIsU, i / 4.0, j / 4.0));
                if (std::abs(std::hypot(q.first - ux, q.second - uy) - r) > 1e-5 * scale)
                    ok = false;
            }
        if (!ok) continue;

        Vec3 xref = P(sweepIsU, 0.0, 0.0) - c0;
        xref -= xref.dot(axis) * axis;
        if (xref.norm() < 1e-9 * scale) xref = P(sweepIsU, 0.5, 0.5) - c0, xref -= xref.dot(axis) * axis;
        if (xref.norm() < 1e-9 * scale) continue;
        xref.normalize();

        Surface t = s;
        t.placement.origin = c0 + uy * axis;
        t.placement.zAxis = axis;
        t.placement.xAxis = xref;
        t.placement.yAxis = axis.cross(xref);
        t.kind = isSphere ? SurfaceKind::Sphere : SurfaceKind::Torus;
        t.radius = isSphere ? r : ux;
        t.radius2 = isSphere ? 0.0 : r;

        // Orientation, as for the cylinder: does the parametric normal
        // (dS/dU x dS/dV) point against the analytic outward normal?
        t.orientationUncertain = false;
        {
            const double h = 1e-3;
            Vec3 Pm = evalBezierNet(s, 0.5, 0.5);
            Vec3 dU = (evalBezierNet(s, 0.5 + h, 0.5) - evalBezierNet(s, 0.5 - h, 0.5)) / (2 * h);
            Vec3 dV = (evalBezierNet(s, 0.5, 0.5 + h) - evalBezierNet(s, 0.5, 0.5 - h)) / (2 * h);
            Vec3 nB = dU.cross(dV);
            Vec3 d = Pm - t.placement.origin;
            Vec3 nA;
            if (isSphere) {
                nA = d;
            } else {
                double hh = d.dot(axis);
                Vec3 radial = d - hh * axis;
                Vec3 tubeCentre = radial.normalized() * t.radius;
                nA = (radial - tubeCentre) + hh * axis;
            }
            if (nB.norm() > 1e-12 && nA.norm() > 1e-12) {
                double c = nB.normalized().dot(nA.normalized());
                if (std::abs(c) > 0.9) t.orientationUncertain = (c < 0.0);
                if (std::getenv("FIELDES_STEP_DEBUG_EDGES"))
                    fprintf(stderr, "[revOrient] %s R=%.4f r=%.4f param normal . outward = %.4f -> flip=%d\n",
                            isSphere ? "sphere" : "torus", t.radius, isSphere ? r : t.radius2,
                            c, int(t.orientationUncertain));
            }
        }
        s = t;
        return true;
    }
    return false;
}

}  // namespace

Surface resolveSurface(const Document& doc, int id)
{
    Surface s;
    const Entity* e = doc.get(id);
    if (!e) return s;

    if (auto* args = e->find("PLANE")) {
        if (args->size() < 2) return s;
        s.kind = SurfaceKind::Plane;
        s.placement = resolvePlacement(doc, (*args)[1].asRef());
        return s;
    }
    if (auto* args = e->find("CYLINDRICAL_SURFACE")) {
        if (args->size() < 3) return s;
        s.kind = SurfaceKind::Cylinder;
        s.placement = resolvePlacement(doc, (*args)[1].asRef());
        s.radius = (*args)[2].asNumber();
        return s;
    }
    if (auto* args = e->find("CONICAL_SURFACE")) {
        if (args->size() < 4) return s;
        s.placement = resolvePlacement(doc, (*args)[1].asRef());
        s.radius = (*args)[2].asNumber();
        // The semi-angle is written in the file's plane-angle unit, which
        // is often DEGREES (e.g. Bandextruder.stp: "45.000000000000043").
        // Read as radians, a 45-degree countersink became a 45-radian cone
        // (tan ~ 1.6, flaring the wrong way), which is what turned a button
        // head screw's hex socket and chamfers into flat "wings".
        s.semiAngle = (*args)[3].asNumber() * doc.planeAngleFactor();
        // A cone with zero semi-angle is a cylinder; use the cylinder code.
        s.kind = (std::abs(s.semiAngle) < 1e-9) ? SurfaceKind::Cylinder : SurfaceKind::Cone;
        return s;
    }
    if (auto* args = e->find("SPHERICAL_SURFACE")) {
        if (args->size() < 3) return s;
        s.kind = SurfaceKind::Sphere;
        s.placement = resolvePlacement(doc, (*args)[1].asRef());
        s.radius = (*args)[2].asNumber();
        return s;
    }
    if (auto* args = e->find("TOROIDAL_SURFACE")) {
        if (args->size() < 4) return s;
        s.kind = SurfaceKind::Torus;
        s.placement = resolvePlacement(doc, (*args)[1].asRef());
        s.radius = (*args)[2].asNumber();
        s.radius2 = (*args)[3].asNumber();
        return s;
    }
    // Same two shapes as B-spline curves above (see the comment there):
    // a genuine complex entity with separate B_SPLINE_SURFACE /
    // B_SPLINE_SURFACE_WITH_KNOTS aspects, or a single flattened
    // B_SPLINE_SURFACE_WITH_KNOTS(...) call with every field
    // concatenated (confirmed against a real HingedTable.step export).
    const std::string surfForm = e->find("B_SPLINE_SURFACE_WITH_KNOTS") ? "" : impliedForm(e, "SURFACE");
    const ValueList* surfKnotArgs = e->find("B_SPLINE_SURFACE_WITH_KNOTS");
    if (!surfKnotArgs && !surfForm.empty()) surfKnotArgs = e->find(surfForm + "_SURFACE");
    if (auto* wargs = surfKnotArgs) {
        bool flat = !e->find("B_SPLINE_SURFACE") && wargs->size() >= (surfForm.empty() ? 13u : 4u);
        const ValueList* bargs = flat ? wargs : e->find("B_SPLINE_SURFACE");
        // CONFIRMED BUG this fixes (found on a real user-provided
        // SolveSpace export, 2026-09-24): the complex-entity aspect form
        // doesn't always carry a leading name string the way the flat
        // form does -- e.g. B_SPLINE_SURFACE(1,1,((...)),...) with NO
        // name argument at all, degree first. Blindly assuming index 0
        // is a name (skip to index 1 for degreeU) silently misread
        // every field one slot early for this file: degreeV's value
        // landed in degreeU, the control-point list landed in degreeV
        // (as a non-number, .asNumber() defaulting to 0), and 'form'
        // landed where the control list was expected (as a non-list,
        // .asList() defaulting to empty) -- degreeV=0 and nCtrlU=0
        // silently failed the validity check below, leaving surface.kind
        // Unsupported for literally every face in the file (21/21
        // skipped, "no importable solids found"), with no error pointing
        // at the real cause. Detect the offset instead of assuming it.
        int nameOffset = (bargs && !bargs->empty() && (*bargs)[0].isString()) ? 1 : 0;
        if (bargs && bargs->size() >= static_cast<size_t>(3 + nameOffset)) {
            s.degreeU = static_cast<int>((*bargs)[nameOffset + 0].asNumber());
            s.degreeV = static_cast<int>((*bargs)[nameOffset + 1].asNumber());
            const ValueList& rows = (*bargs)[nameOffset + 2].asList();
            s.nCtrlU = static_cast<int>(rows.size());
            s.nCtrlV = rows.empty() ? 0 : static_cast<int>(rows[0].asList().size());
            s.ctrl.reserve(static_cast<size_t>(s.nCtrlU) * s.nCtrlV);
            for (auto& row : rows) {
                for (auto& pv : row.asList()) {
                    s.ctrl.push_back(resolvePoint(doc, pv.asRef()));
                }
            }
            if (!surfForm.empty()) {
                s.knotsU = impliedKnots(surfForm, s.degreeU, s.nCtrlU);
                s.knotsV = impliedKnots(surfForm, s.degreeV, s.nCtrlV);
            } else if (flat) {
                // B_SPLINE_SURFACE_WITH_KNOTS(name, u_deg, v_deg, ctrl,
                //   form, u_closed, v_closed, self_int,
                //   u_mults, v_mults, u_knots, v_knots, knot_spec)
                s.knotsU = expandKnots((*wargs)[8].asList(), (*wargs)[10].asList());
                s.knotsV = expandKnots((*wargs)[9].asList(), (*wargs)[11].asList());
            } else if (wargs->size() >= 4) {
                s.knotsU = expandKnots((*wargs)[0].asList(), (*wargs)[2].asList());
                s.knotsV = expandKnots((*wargs)[1].asList(), (*wargs)[3].asList());
            }
            if (auto* rargs = e->find("RATIONAL_B_SPLINE_SURFACE")) {
                if (!rargs->empty()) {
                    for (auto& row : (*rargs)[0].asList())
                        for (auto& wv : row.asList())
                            s.weights.push_back(wv.asNumber(1.0));
                }
            }
            bool knotsOk =
                static_cast<int>(s.knotsU.size()) == s.nCtrlU + s.degreeU + 1 &&
                static_cast<int>(s.knotsV.size()) == s.nCtrlV + s.degreeV + 1;
            if (s.degreeU > 0 && s.degreeV > 0 && s.nCtrlU > 0 && s.nCtrlV > 0 && knotsOk) {
                s.kind = SurfaceKind::BSpline;
                // A B-spline entity type doesn't always mean genuinely
                // freeform geometry -- some exporters (confirmed:
                // SolveSpace) write every surface this way even when
                // it's exactly a plane or a cylinder. Detect those
                // specific, well-understood patterns and use the real
                // analytic type instead of falling back to the Oracle
                // for geometry that's actually simple. Order matters:
                // try the cheaper/more specific check (plane) first.
                if (!tryPlaneFromBilinear(s)) {
                    if (!tryCylinderFromRationalArc(s) && !tryConeFromRationalArc(s))
                        tryRevolvedArcPatch(s);
                }
            }
        }
        return s;
    }

    return s;
}

Vec3 Surface::evalParam(double u, double v) const
{
    switch (kind) {
        case SurfaceKind::Plane:
            return placement.origin + u * placement.xAxis + v * placement.yAxis;
        case SurfaceKind::Cylinder:
            return placement.origin + radius * (std::cos(u) * placement.xAxis +
                                                 std::sin(u) * placement.yAxis) +
                   v * placement.zAxis;
        case SurfaceKind::Cone: {
            double r = radius + v * std::tan(semiAngle);
            return placement.origin + r * (std::cos(u) * placement.xAxis +
                                            std::sin(u) * placement.yAxis) +
                   v * placement.zAxis;
        }
        case SurfaceKind::Sphere:
            return placement.origin +
                   radius * std::cos(v) * (std::cos(u) * placement.xAxis +
                                            std::sin(u) * placement.yAxis) +
                   radius * std::sin(v) * placement.zAxis;
        case SurfaceKind::Torus: {
            Vec3 radial = std::cos(u) * placement.xAxis + std::sin(u) * placement.yAxis;
            return placement.origin + (radius + radius2 * std::cos(v)) * radial +
                   radius2 * std::sin(v) * placement.zAxis;
        }
        case SurfaceKind::BSpline: {
            // Tensor-product de Boor: reduce along V for each of the
            // degreeU+1 relevant control rows, then along U.
            auto Pw = toHomogeneous(ctrl, weights);
            double uc = std::clamp(u, knotsU[degreeU], knotsU[knotsU.size() - 1 - degreeU]);
            double vc = std::clamp(v, knotsV[degreeV], knotsV[knotsV.size() - 1 - degreeV]);
            std::vector<Eigen::Vector4d> uCurve(static_cast<size_t>(nCtrlU));
            for (int iu = 0; iu < nCtrlU; iu++) {
                std::vector<Eigen::Vector4d> row(static_cast<size_t>(nCtrlV));
                for (int iv = 0; iv < nCtrlV; iv++) row[iv] = Pw[iu * nCtrlV + iv];
                uCurve[iu] = deBoor(degreeV, knotsV, row, vc);
            }
            Eigen::Vector4d h = deBoor(degreeU, knotsU, uCurve, uc);
            return h.head<3>() / h.w();
        }
        default:
            return Vec3::Zero();
    }
}

Vec3 Surface::normalAt(double u, double v) const
{
    const double h = 1e-4;
    Vec3 du, dv;
    switch (kind) {
        case SurfaceKind::Plane:
            return placement.zAxis;
        default:
            du = (evalParam(u + h, v) - evalParam(u - h, v)) / (2 * h);
            dv = (evalParam(u, v + h) - evalParam(u, v - h)) / (2 * h);
            Vec3 n = du.cross(dv);
            double len = n.norm();
            return len > 1e-9 ? (n / len) : placement.zAxis;
    }
}

Surface::Closest Surface::newtonRefineBSpline(const Vec3& p, double u, double v) const
{
    Closest r;
    double u0 = knotsU[degreeU], u1 = knotsU[knotsU.size() - 1 - degreeU];
    double v0 = knotsV[degreeV], v1 = knotsV[knotsV.size() - 1 - degreeV];
    u = std::clamp(u, u0, u1);
    v = std::clamp(v, v0, v1);
    const double h = std::max((u1 - u0), (v1 - v0)) * 1e-4 + 1e-6;
    for (int it = 0; it < 8; it++) {
        Vec3 S = evalParam(u, v);
        Vec3 Su = (evalParam(std::min(u + h, u1), v) - evalParam(std::max(u - h, u0), v)) /
                  (std::min(u + h, u1) - std::max(u - h, u0));
        Vec3 Sv = (evalParam(u, std::min(v + h, v1)) - evalParam(u, std::max(v - h, v0))) /
                  (std::min(v + h, v1) - std::max(v - h, v0));
        Vec3 R = S - p;
        double a = Su.dot(Su), b = Su.dot(Sv), cc = Sv.dot(Sv);
        double e0 = Su.dot(R), e1 = Sv.dot(R);
        double det = a * cc - b * b;
        if (std::abs(det) < 1e-12) break;
        double du = (e0 * cc - e1 * b) / det;
        double dv = (e1 * a - e0 * b) / det;
        u = std::clamp(u - du, u0, u1);
        v = std::clamp(v - dv, v0, v1);
    }
    r.u = u; r.v = v;
    r.point = evalParam(u, v);
    r.normal = normalAt(u, v);
    r.signedDist = (p - r.point).dot(r.normal);
    r.dist = (p - r.point).norm();
    return r;
}

Surface::Closest Surface::closestPointNear(const Vec3& p, double u0, double v0) const
{
    if (kind == SurfaceKind::BSpline) {
        return newtonRefineBSpline(p, u0, v0);
    }
    // Analytic surfaces are already O(1) closed-form -- no search to seed.
    return closestPoint(p);
}

Surface::Closest Surface::closestPoint(const Vec3& p) const
{
    Closest r;
    Vec3 d = p - placement.origin;

    switch (kind) {
        case SurfaceKind::Plane: {
            double du = d.dot(placement.xAxis), dv = d.dot(placement.yAxis);
            double dn = d.dot(placement.zAxis);
            r.u = du; r.v = dv;
            r.point = placement.origin + du * placement.xAxis + dv * placement.yAxis;
            r.normal = placement.zAxis;
            r.signedDist = dn;
            r.dist = std::abs(dn);
            return r;
        }
        case SurfaceKind::Cylinder: {
            double axial = d.dot(placement.zAxis);
            Vec3 radialVec = d - axial * placement.zAxis;
            double rd = radialVec.norm();
            // Threshold scaled to the cylinder's own radius (not a bare
            // 1e-9): a query point near the axis but *not* right on it
            // still passes 1e-9 while radialVec/rd amplifies float-noise
            // in the query point (evalPoint's input is only float32) by
            // up to ~1e9x, making the resulting angle -- and therefore
            // the trim in/out classification -- change chaotically
            // between calls a fraction of a unit apart. That inconsistency
            // is what breaks a bisection search's convergence: confirmed
            // by reproducing a specific domain-size/resolution combo that
            // deterministically hung meshing a vaulted box (a single
            // cylindrical face) for 1M+ point evaluations with no
            // progress, isolated via direct call-count instrumentation.
            Vec3 e_r = (rd > radius * 1e-4) ? (radialVec / rd) : placement.xAxis;
            r.u = std::atan2(e_r.dot(placement.yAxis), e_r.dot(placement.xAxis));
            r.v = axial;
            r.point = placement.origin + radius * e_r + axial * placement.zAxis;
            r.normal = e_r;
            r.signedDist = rd - radius;
            r.dist = std::abs(r.signedDist);
            return r;
        }
        case SurfaceKind::Cone: {
            double axial = d.dot(placement.zAxis);
            Vec3 radialVec = d - axial * placement.zAxis;
            double rd = radialVec.norm();
            // See the matching comment on the Cylinder case above.
            Vec3 e_r = (rd > radius * 1e-4) ? (radialVec / rd) : placement.xAxis;
            // 2D cross-section: point (rd, axial); line r = radius + h*tan(semiAngle)
            double c = std::cos(semiAngle), sn = std::sin(semiAngle);
            double signedDist2D = (rd - radius) * c - axial * sn;
            r.u = std::atan2(e_r.dot(placement.yAxis), e_r.dot(placement.xAxis));
            // Foot of the perpendicular in the 2D (radial, axial) section:
            // `slant` is its distance along the cone's generator line from
            // the reference circle; its AXIAL height is slant*cos. v must be
            // that axial height -- evalParam (and STEP's CONICAL_SURFACE)
            // parameterise v axially. Returning the slant length here made
            // every cone's trim loops disagree with evalParam by a factor
            // cos(semiAngle), e.g. a 120-degree drill point's bounds sampled
            // to twice the part's real width.
            double slant = axial * c + (rd - radius) * sn;
            double hFoot = slant * c;
            r.v = hFoot;
            double rAtFoot = radius + slant * sn;
            r.point = placement.origin + rAtFoot * e_r + hFoot * placement.zAxis;
            r.normal = (e_r * c - placement.zAxis * sn);
            r.signedDist = signedDist2D;
            r.dist = std::abs(signedDist2D);
            return r;
        }
        case SurfaceKind::Sphere: {
            double dn = d.norm();
            Vec3 e = (dn > 1e-9) ? (d / dn) : placement.zAxis;
            r.v = std::asin(std::clamp(e.dot(placement.zAxis), -1.0, 1.0));
            Vec3 eq = e - e.dot(placement.zAxis) * placement.zAxis;
            r.u = std::atan2(eq.dot(placement.yAxis), eq.dot(placement.xAxis));
            r.point = placement.origin + radius * e;
            r.normal = e;
            r.signedDist = dn - radius;
            r.dist = std::abs(r.signedDist);
            return r;
        }
        case SurfaceKind::Torus: {
            double axial = d.dot(placement.zAxis);
            Vec3 radialVec = d - axial * placement.zAxis;
            double rd = radialVec.norm();
            Vec3 e_r = (rd > 1e-9) ? (radialVec / rd) : placement.xAxis;
            double cr = rd - radius, cz = axial;
            double tubeDist = std::sqrt(cr * cr + cz * cz);
            Vec3 tubeDir = (tubeDist > 1e-9)
                ? (cr / tubeDist) * e_r + (cz / tubeDist) * placement.zAxis
                : e_r;
            r.u = std::atan2(e_r.dot(placement.yAxis), e_r.dot(placement.xAxis));
            r.v = std::atan2(cz, cr);
            r.point = placement.origin + radius * e_r + radius2 * tubeDir;
            r.normal = tubeDir;
            r.signedDist = tubeDist - radius2;
            r.dist = std::abs(r.signedDist);
            return r;
        }
        case SurfaceKind::BSpline: {
            // Coarse grid search to find a starting point, then hand off
            // to the shared Newton refinement (also used directly by
            // closestPointNear when a good seed is already known).
            const int N = 10;
            double u0 = knotsU[degreeU], u1 = knotsU[knotsU.size() - 1 - degreeU];
            double v0 = knotsV[degreeV], v1 = knotsV[knotsV.size() - 1 - degreeV];
            double bestU = u0, bestV = v0, bestD = 1e18;
            for (int i = 0; i <= N; i++) {
                for (int j = 0; j <= N; j++) {
                    double u = u0 + (u1 - u0) * i / N;
                    double v = v0 + (v1 - v0) * j / N;
                    double dd = (evalParam(u, v) - p).squaredNorm();
                    if (dd < bestD) { bestD = dd; bestU = u; bestV = v; }
                }
            }
            return newtonRefineBSpline(p, bestU, bestV);
        }
        default:
            return r;
    }
}

double triangleSolidAngle(const Vec3& p, const Vec3& a, const Vec3& b, const Vec3& c)
{
    Vec3 va = a - p, vb = b - p, vc = c - p;
    double la = va.norm(), lb = vb.norm(), lc = vc.norm();
    if (la < 1e-12 || lb < 1e-12 || lc < 1e-12) return 0.0;
    double numerator = va.dot(vb.cross(vc));
    double denominator = la * lb * lc + va.dot(vb) * lc + vb.dot(vc) * la + vc.dot(va) * lb;
    return 2.0 * std::atan2(numerator, denominator);
}

}  // namespace step
}  // namespace libfive
