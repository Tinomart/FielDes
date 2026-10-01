/*
libfive: a CAD kernel for modeling with implicit functions

Fitting cheap closed-form surfaces to B-spline faces (see step_fit.hpp).

Each fit is a linear least-squares problem once its frame is known: the
fitted function should vanish at points sampled on the face and have the
face's unit normal as its gradient there.  The gradient condition fixes
the function's scale and sign (so near the face its value is about the
signed distance, like the other surfaces of a reconstruction) and keeps
the fit from bending away between the samples.  Everything is done in
coordinates centred on the samples and scaled to about unit size, both for
the least squares and for libfive's single-precision evaluation.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "libfive/step/step_fit.hpp"
#include "libfive/step/step_bspline.hpp"
#include "libfive/step/step_model.hpp"
#include "libfive/tree/operations.hpp"

namespace libfive {
namespace step {

namespace {

typedef Eigen::Matrix<double, 10, 1> Vec10;

// Weights of the two kinds of conditions (in the normalized coordinates a
// value of 0.01 means 1 % of the face's size, a gradient of 1 a unit normal)
const double W_VALUE = 1.0;
const double W_GRAD = 0.1;

void quadricMonomials(const Vec3& w, Vec10& m, Vec10& gx, Vec10& gy, Vec10& gz)
{
    const double x = w.x(), y = w.y(), z = w.z();
    m  << x * x, y * y, z * z, x * y, x * z, y * z, x, y, z, 1;
    gx << 2 * x, 0, 0, y, z, 0, 1, 0, 0, 0;
    gy << 0, 2 * y, 0, x, 0, z, 0, 1, 0, 0;
    gz << 0, 0, 2 * z, 0, x, y, 0, 0, 1, 0;
}

void cubicMonomials(double u, double v, Vec10& m, Vec10& gu, Vec10& gv)
{
    m  << u * u * u, u * u * v, u * v * v, v * v * v, u * u, u * v, v * v, u, v, 1;
    gu << 3 * u * u, 2 * u * v, v * v, 0, 2 * u, v, 0, 1, 0, 0;
    gv << 0, u * u, 2 * u * v, 3 * v * v, 0, u, 2 * v, 0, 1, 0;
}

// Even-odd point-in-polygon, as the trim test of the reconstruction
bool inPolygon(const std::vector<Eigen::Vector2d>& poly, double u, double v)
{
    bool in = false;
    const size_t n = poly.size();
    if (n < 3) return false;
    for (size_t i = 0, j = n - 1; i < n; j = i++)
    {
        const auto& a = poly[i];
        const auto& b = poly[j];
        if ((a.y() > v) != (b.y() > v))
        {
            const double x = a.x() + (v - a.y()) * (b.x() - a.x()) / (b.y() - a.y());
            if (u < x) in = !in;
        }
    }
    return in;
}

struct LeastSquares
{
    std::vector<Vec10> rows;
    std::vector<double> rhs;
    void add(const Vec10& r, double target, double w)
    {
        rows.push_back(w * r);
        rhs.push_back(w * target);
    }
    // A mild pull of the first `n` (nonlinear) coefficients towards zero:
    // combinations the samples hardly constrain (a thin band of a face
    // leaves several) otherwise take huge values that cancel along the
    // face -- right at the samples, but wild beyond them and hopeless for
    // the mesher's bounds over boxes
    void damp(int n)
    {
        const double w = 1e-3 * std::sqrt(double(rows.size()));
        for (int k = 0; k < n; ++k)
        {
            Vec10 r = Vec10::Zero();
            r(k) = 1;
            add(r, 0, w);
        }
    }
    bool solve(Vec10& out) const
    {
        if (rows.size() < 40) return false;
        Eigen::MatrixXd A(rows.size(), 10);
        Eigen::VectorXd b(rows.size());
        for (size_t i = 0; i < rows.size(); ++i)
        {
            A.row(Eigen::Index(i)) = rows[i].transpose();
            b(Eigen::Index(i)) = rhs[i];
        }
        out = A.colPivHouseholderQr().solve(b);
        return out.allFinite();
    }
};

// A unit vector across a
Vec3 across(const Vec3& a)
{
    return (std::abs(a.x()) < 0.9 ? Vec3(1, 0, 0) : Vec3(0, 1, 0)).cross(a).normalized();
}

void measure(FittedSurface& f, const FaceSamples& s)
{
    double mx = 0, sum = 0;
    size_t flips = 0, turns = 0;
    const double cosTurn = 0.90630778703665;    // cos 25 deg
    for (size_t i = 0; i < s.p.size(); ++i)
    {
        Vec3 g;
        const double v = f.value(s.p[i], &g);
        const double gn = g.norm();
        if (!std::isfinite(v) || !(gn > 1e-12))
        {
            f.maxErr = f.rmsErr = std::numeric_limits<double>::infinity();
            return;
        }
        const double e = std::abs(v) / gn;
        mx = std::max(mx, e);
        sum += e * e;
        if (g.dot(s.n[i]) < 0) flips++;
        if (g.dot(s.n[i]) < cosTurn * gn) turns++;
    }
    f.maxErr = mx;
    f.rmsErr = std::sqrt(sum / double(std::max<size_t>(s.p.size(), 1)));
    f.flipped = double(flips) / double(std::max<size_t>(s.p.size(), 1));
    f.turned = double(turns) / double(std::max<size_t>(s.p.size(), 1));
}

}   // namespace

////////////////////////////////////////////////////////////////////////////////

const char* FittedSurface::name(Kind k)
{
    switch (k)
    {
        case PLANE:    return "plane";
        case QUADRIC:  return "quadric";
        case EXTRUDED: return "extruded";
        case REVOLVED: return "revolved";
        case HELICAL:  return "helical";
        case FILLET:   return "fillet";
        default:       return "none";
    }
}

double FittedSurface::value(const Vec3& p, Vec3* grad) const
{
    switch (kind)
    {
        case PLANE:
        {
            const Vec3 n = frame.row(2).transpose();
            if (grad) *grad = n;
            return (p - center).dot(n);
        }
        case QUADRIC:
        {
            Vec10 m, gx, gy, gz;
            quadricMonomials((p - center) / scale, m, gx, gy, gz);
            if (grad) *grad = Vec3(coef.dot(gx), coef.dot(gy), coef.dot(gz));
            return scale * coef.dot(m);
        }
        case EXTRUDED:
        {
            const Vec3 w = (p - center) / scale;
            const Vec3 e1 = frame.row(0).transpose(), e2 = frame.row(1).transpose();
            Vec10 m, gu, gv;
            cubicMonomials(w.dot(e1), w.dot(e2), m, gu, gv);
            if (grad) *grad = coef.dot(gu) * e1 + coef.dot(gv) * e2;
            return scale * coef.dot(m);
        }
        case REVOLVED:
        {
            const Vec3 w = (p - center) / scale;
            const Vec3 a = frame.row(2).transpose();
            const double h = w.dot(a);
            const Vec3 radial = w - h * a;
            const double r = radial.norm();
            const Vec3 rhat = r > 1e-12 ? Vec3(radial / r) : Vec3(frame.row(0).transpose());
            Vec10 m, gr, gh;
            cubicMonomials(r - offset.x(), h - offset.y(), m, gr, gh);
            if (grad) *grad = coef.dot(gr) * rhat + coef.dot(gh) * a;
            return scale * coef.dot(m);
        }
        case HELICAL:
        {
            const Vec3 w = (p - center) / scale;
            const Vec3 e1 = frame.row(0).transpose(), e2 = frame.row(1).transpose();
            const Vec3 a = frame.row(2).transpose();
            const double phi = w.dot(a) / pitch;
            const double u = w.dot(e1), v = w.dot(e2), cs = std::cos(phi), sn = std::sin(phi);
            const double ut = u * cs + v * sn, vt = -u * sn + v * cs;
            Vec10 m, gu, gv;
            cubicMonomials(ut - offset.x(), vt - offset.y(), m, gu, gv);
            if (grad)
            {
                // d(ut)/dw = cs e1 + sn e2 + vt a / pitch, d(vt)/dw = -sn e1 + cs e2 - ut a / pitch
                const Vec3 dut = cs * e1 + sn * e2 + (vt / pitch) * a;
                const Vec3 dvt = -sn * e1 + cs * e2 - (ut / pitch) * a;
                *grad = coef.dot(gu) * dut + coef.dot(gv) * dvt;
            }
            return scale * coef.dot(m);
        }
        case FILLET:
        {
            Vec3 gA = Vec3::Zero(), gB = Vec3::Zero();
            const double a = sA * nbA->value(p, grad ? &gA : nullptr);
            const double b = sB * nbB->value(p, grad ? &gB : nullptr);
            const double ma = std::max(a + radius, 0.0), mb = std::max(b + radius, 0.0);
            const double len = std::sqrt(ma * ma + mb * mb);
            if (grad)
            {
                Vec3 g = Vec3::Zero();
                if (std::max(a, b) < 0) g += a > b ? Vec3(sA * gA) : Vec3(sB * gB);
                if (len > 1e-12) g += (ma / len) * sA * gA + (mb / len) * sB * gB;
                *grad = sign * g;
            }
            return sign * (std::min(std::max(a, b), 0.0) + len - radius);
        }
        default:
            if (grad) *grad = Vec3::Zero();
            return std::numeric_limits<double>::infinity();
    }
}

////////////////////////////////////////////////////////////////////////////////

SurfaceDistance SurfaceDistance::of(const Face& face)
{
    SurfaceDistance d;
    const Surface& s = face.surface;
    d.o = s.placement.origin;
    d.a = s.placement.zAxis.normalized();
    d.R = s.radius;
    d.r2 = s.radius2;
    d.semi = s.semiAngle;
    switch (s.kind)
    {
        case SurfaceKind::Plane:    d.kind = 0; break;
        case SurfaceKind::Cylinder: d.kind = 1; break;
        case SurfaceKind::Sphere:   d.kind = 2; break;
        case SurfaceKind::Cone:     d.kind = 3; break;
        case SurfaceKind::Torus:    d.kind = 4; break;
        case SurfaceKind::BSpline:
            if (face.fit && face.fit->ok() && face.fit->kind != FittedSurface::FILLET &&
                fitHasShape(*face.fit))
            {
                d.kind = 5;
                d.fit = face.fit;
            }
            break;
        default: break;
    }
    return d;
}

double SurfaceDistance::value(const Vec3& p, Vec3* grad) const
{
    const Vec3 w = p - o;
    switch (kind)
    {
        case 0:
            if (grad) *grad = a;
            return w.dot(a);
        case 1:
        case 3:
        case 4:
        {
            const double h = w.dot(a);
            const Vec3 radial = w - h * a;
            const double rho = radial.norm();
            Vec3 rhat = rho > 1e-12 ? Vec3(radial / rho) : across(a);
            if (kind == 1)
            {
                if (grad) *grad = rhat;
                return rho - R;
            }
            if (kind == 3)
            {
                // (the distance across the cone's slant line)
                const double t = std::tan(semi), c = std::cos(semi);
                if (grad) *grad = c * (rhat - t * a);
                return c * (rho - (R + t * h));
            }
            const double q0 = rho - R, len = std::sqrt(q0 * q0 + h * h);
            if (grad) *grad = len > 1e-12 ? Vec3((q0 * rhat + h * a) / len) : rhat;
            return len - r2;
        }
        case 2:
        {
            const double len = w.norm();
            if (grad) *grad = len > 1e-12 ? Vec3(w / len) : Vec3(a);
            return len - R;
        }
        case 5:
            return fit->value(p, grad);
        default:
            if (grad) *grad = Vec3::Zero();
            return std::numeric_limits<double>::infinity();
    }
}

Tree SurfaceDistance::tree() const
{
    const Tree wx = Tree::X() - o.x(), wy = Tree::Y() - o.y(), wz = Tree::Z() - o.z();
    const Tree h = wx * a.x() + wy * a.y() + wz * a.z();
    auto rho = [&]() {
        // (from two axes across the axis: see FittedSurface::tree, REVOLVED)
        const Vec3 e1 = across(a), e2 = a.cross(e1);
        const Tree u = wx * e1.x() + wy * e1.y() + wz * e1.z();
        const Tree v = wx * e2.x() + wy * e2.y() + wz * e2.z();
        return sqrt(square(u) + square(v));
    };
    switch (kind)
    {
        case 0: return h;
        case 1: return rho() - R;
        case 2: return sqrt(square(wx) + square(wy) + square(wz)) - R;
        case 3: return (rho() - (R + std::tan(semi) * h)) * std::cos(semi);
        case 4: return sqrt(square(rho() - R) + square(h)) - r2;
        case 5: return fit->tree();
        default: return Tree(1e9);
    }
}

Tree FittedSurface::tree() const
{
    const Tree wx = (Tree::X() - center.x()) * (1.0 / scale);
    const Tree wy = (Tree::Y() - center.y()) * (1.0 / scale);
    const Tree wz = (Tree::Z() - center.z()) * (1.0 / scale);
    auto dotRow = [&](int row) {
        return wx * frame(row, 0) + wy * frame(row, 1) + wz * frame(row, 2);
    };
    auto cubic = [&](const Tree& u, const Tree& v) {
        const Tree u2 = square(u), v2 = square(v);
        return u2 * u * coef(0) + u2 * v * coef(1) + u * v2 * coef(2) + v2 * v * coef(3) +
               u2 * coef(4) + u * v * coef(5) + v2 * coef(6) + u * coef(7) + v * coef(8) +
               Tree(coef(9));
    };
    switch (kind)
    {
        case PLANE:
            return dotRow(2) * scale;
        case QUADRIC:
            return (square(wx) * coef(0) + square(wy) * coef(1) + square(wz) * coef(2) +
                    wx * wy * coef(3) + wx * wz * coef(4) + wy * wz * coef(5) +
                    wx * coef(6) + wy * coef(7) + wz * coef(8) + Tree(coef(9))) * scale;
        case EXTRUDED:
            return cubic(dotRow(0), dotRow(1)) * scale;
        case REVOLVED:
        {
            // (r from the two axes across the axis: |w|^2 - h^2 is the same
            // number, but as an interval over a box it subtracts two wide
            // ranges and bounds nothing -- every cell of a large part then
            // looked like it held the surface)
            const Tree r = sqrt(square(dotRow(0)) + square(dotRow(1)));
            return cubic(r - offset.x(), dotRow(2) - offset.y()) * scale;
        }
        case HELICAL:
        {
            const Tree phi = dotRow(2) * (1.0 / pitch);
            const Tree cs = cos(phi), sn = sin(phi);
            const Tree u = dotRow(0), v = dotRow(1);
            return cubic(u * cs + v * sn - offset.x(), v * cs - u * sn - offset.y()) * scale;
        }
        case FILLET:
        {
            const Tree a = nbA->tree() * sA, b = nbB->tree() * sB;
            const Tree ma = max(a + radius, Tree(0.0)), mb = max(b + radius, Tree(0.0));
            return (min(max(a, b), Tree(0.0)) + sqrt(square(ma) + square(mb)) - radius) * sign;
        }
        default:
            return Tree(1e9);
    }
}

////////////////////////////////////////////////////////////////////////////////

FaceSamples sampleFace(const Face& face, int grid)
{
    FaceSamples out;
    out.size = (face.boundMax - face.boundMin).norm();
    if (!face.patch) return out;
    const BSplinePatch& bs = *face.patch;
    auto inFace = [&](double u, double v) {
        if (face.fullPatch) return true;
        bool inside = false;
        for (const auto& loop : face.loops)
        {
            if (inPolygon(loop.uv, u, v)) inside = !inside;
        }
        return inside;
    };
    // (a narrow face may catch few samples of the first grid: then a finer one)
    for (int g : {grid, 3 * grid})
    {
        out.p.clear();
        out.n.clear();
        for (int i = 0; i < g; ++i)
        {
            for (int j = 0; j < g; ++j)
            {
                const double u = bs.u0 + (bs.u1 - bs.u0) * (i + 0.5) / g;
                const double v = bs.v0 + (bs.v1 - bs.v0) * (j + 0.5) / g;
                if (!inFace(u, v)) continue;
                Vec3 S, Su, Sv;
                bs.eval(u, v, S, &Su, &Sv);
                const Vec3 n = Su.cross(Sv);
                const double len = n.norm();
                if (!S.allFinite() || !(len > 1e-300) || !std::isfinite(len)) continue;
                out.p.push_back(S);
                out.n.push_back(n / len);
            }
        }
        if (out.p.size() >= 100) break;
    }
    return out;
}

std::vector<FittedSurface> fitCandidates(const FaceSamples& s)
{
    std::vector<FittedSurface> out(5);
    for (int k = 0; k < 5; ++k) out[size_t(k)].kind = FittedSurface::NONE;
    const size_t N = s.p.size();
    if (N < 20) return out;

    Vec3 c = Vec3::Zero();
    for (const Vec3& p : s.p) c += p;
    c /= double(N);
    double r2 = 0;
    for (const Vec3& p : s.p) r2 += (p - c).squaredNorm();
    const double scale = std::max(std::sqrt(r2 / double(N)), 1e-12);
    Vec3 meanNormal = Vec3::Zero();
    for (const Vec3& n : s.n) meanNormal += n;

    {   // Plane: the principal axes of the points
        Eigen::Matrix3d C = Eigen::Matrix3d::Zero();
        for (const Vec3& p : s.p) C += (p - c) * (p - c).transpose();
        Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> es(C);
        FittedSurface& f = out[0];
        Vec3 n = es.eigenvectors().col(0).normalized();
        if (n.dot(meanNormal) < 0) n = -n;
        f.kind = FittedSurface::PLANE;
        f.center = c;
        f.scale = scale;
        f.frame.row(0) = across(n).transpose();
        f.frame.row(1) = n.cross(across(n)).transpose();
        f.frame.row(2) = n.transpose();
        measure(f, s);
    }

    {   // Quadric
        LeastSquares ls;
        for (size_t i = 0; i < N; ++i)
        {
            Vec10 m, gx, gy, gz;
            quadricMonomials((s.p[i] - c) / scale, m, gx, gy, gz);
            ls.add(m, 0, W_VALUE);
            ls.add(gx, s.n[i].x(), W_GRAD);
            ls.add(gy, s.n[i].y(), W_GRAD);
            ls.add(gz, s.n[i].z(), W_GRAD);
        }
        ls.damp(6);
        FittedSurface& f = out[1];
        f.center = c;
        f.scale = scale;
        if (ls.solve(f.coef))
        {
            f.kind = FittedSurface::QUADRIC;
            measure(f, s);
        }
    }

    {   // Extruded: along the direction the normals are all across
        Eigen::Matrix3d M = Eigen::Matrix3d::Zero();
        for (const Vec3& n : s.n) M += n * n.transpose();
        Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> es(M);
        const Vec3 d = es.eigenvectors().col(0).normalized();
        const Vec3 e1 = across(d), e2 = d.cross(e1);
        LeastSquares ls;
        for (size_t i = 0; i < N; ++i)
        {
            const Vec3 w = (s.p[i] - c) / scale;
            Vec10 m, gu, gv;
            cubicMonomials(w.dot(e1), w.dot(e2), m, gu, gv);
            ls.add(m, 0, W_VALUE);
            ls.add(gu, s.n[i].dot(e1), W_GRAD);
            ls.add(gv, s.n[i].dot(e2), W_GRAD);
        }
        ls.damp(7);
        FittedSurface& f = out[2];
        f.center = c;
        f.scale = scale;
        f.frame.row(0) = e1.transpose();
        f.frame.row(1) = e2.transpose();
        f.frame.row(2) = d.transpose();
        if (ls.solve(f.coef))
        {
            f.kind = FittedSurface::EXTRUDED;
            measure(f, s);
        }
    }

    {   // Revolved: every normal line of a surface of revolution meets its
        // axis, so the axis is the line (a, abar) -- direction, moment --
        // with a . (w x n) + abar . n = 0 for every sample, found by least
        // squares with |a| = 1 (the moment eliminated, then the smallest
        // eigenvector of what remains)
        Eigen::Matrix3d A = Eigen::Matrix3d::Zero(), B = A, C = A;
        for (size_t i = 0; i < N; ++i)
        {
            const Vec3 w = (s.p[i] - c) / scale;
            const Vec3 l = s.n[i], lb = w.cross(s.n[i]);
            A += lb * lb.transpose();
            B += lb * l.transpose();
            C += l * l.transpose();
        }
        Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> ec(C);
        Eigen::Matrix3d Cp = Eigen::Matrix3d::Zero();
        const double cmax = ec.eigenvalues().maxCoeff();
        for (int k = 0; k < 3; ++k)
        {
            const double lam = ec.eigenvalues()(k);
            if (lam > 1e-9 * cmax)
            {
                const Vec3 v = ec.eigenvectors().col(k);
                Cp += v * v.transpose() / lam;
            }
        }
        const Eigen::Matrix3d S = A - B * Cp * B.transpose();
        Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> es(S);
        const Vec3 a = es.eigenvectors().col(0).normalized();
        const Vec3 abar = -Cp * B.transpose() * a;
        const Vec3 axisPoint = c + scale * a.cross(abar);
        const Vec3 e1 = across(a), e2 = a.cross(e1);
        Eigen::Vector2d offset = Eigen::Vector2d::Zero();
        for (size_t i = 0; i < N; ++i)
        {
            const Vec3 w = (s.p[i] - axisPoint) / scale;
            const double h = w.dot(a);
            offset += Eigen::Vector2d((w - h * a).norm(), h);
        }
        offset /= double(N);
        LeastSquares ls;
        for (size_t i = 0; i < N; ++i)
        {
            const Vec3 w = (s.p[i] - axisPoint) / scale;
            const double h = w.dot(a);
            const Vec3 radial = w - h * a;
            const double r = radial.norm();
            if (!(r > 1e-9)) continue;
            const Vec3 rhat = radial / r;
            Vec10 m, gr, gh;
            cubicMonomials(r - offset.x(), h - offset.y(), m, gr, gh);
            ls.add(m, 0, W_VALUE);
            ls.add(gr, s.n[i].dot(rhat), W_GRAD);
            ls.add(gh, s.n[i].dot(a), W_GRAD);
        }
        ls.damp(7);
        FittedSurface& f = out[3];
        f.offset = offset;
        f.center = axisPoint;
        f.scale = scale;
        f.frame.row(0) = e1.transpose();
        f.frame.row(1) = e2.transpose();
        f.frame.row(2) = a.transpose();
        if (axisPoint.allFinite() && ls.solve(f.coef))
        {
            f.kind = FittedSurface::REVOLVED;
            measure(f, s);
        }

        // Helical: the same line equation holds for a screw motion's
        // invariant surfaces, with a . abar = its pitch (advance per
        // radian) -- zero for a surface of revolution, which the above fits
        const double pitch = a.dot(abar);
        if (std::abs(pitch) > 1e-4 && axisPoint.allFinite())
        {
            auto turned = [&](const Vec3& w, double& ut, double& vt, double& phi) {
                phi = w.dot(a) / pitch;
                const double u = w.dot(e1), v = w.dot(e2), cs = std::cos(phi), sn = std::sin(phi);
                ut = u * cs + v * sn;
                vt = -u * sn + v * cs;
            };
            Eigen::Vector2d hoff = Eigen::Vector2d::Zero();
            for (size_t i = 0; i < N; ++i)
            {
                double ut, vt, phi;
                turned((s.p[i] - axisPoint) / scale, ut, vt, phi);
                hoff += Eigen::Vector2d(ut, vt);
            }
            hoff /= double(N);
            LeastSquares lh;
            for (size_t i = 0; i < N; ++i)
            {
                double ut, vt, phi;
                turned((s.p[i] - axisPoint) / scale, ut, vt, phi);
                const double cs = std::cos(phi), sn = std::sin(phi);
                const Vec3 dut = cs * e1 + sn * e2 + (vt / pitch) * a;
                const Vec3 dvt = -sn * e1 + cs * e2 - (ut / pitch) * a;
                Vec10 m, gu, gv;
                cubicMonomials(ut - hoff.x(), vt - hoff.y(), m, gu, gv);
                lh.add(m, 0, W_VALUE);
                for (int k = 0; k < 3; ++k)
                {
                    lh.add(gu * dut(k) + gv * dvt(k), s.n[i](k), W_GRAD);
                }
            }
            lh.damp(7);
            FittedSurface& g = out[4];
            g.offset = hoff;
            g.pitch = pitch;
            g.center = axisPoint;
            g.scale = scale;
            g.frame = f.frame;
            if (lh.solve(g.coef))
            {
                g.kind = FittedSurface::HELICAL;
                measure(g, s);
            }
        }
    }
    return out;
}

FittedSurface fitFace(const Face& face)
{
    const FaceSamples s = sampleFace(face);
    const auto cands = fitCandidates(s);
    static const bool dbgAll = std::getenv("FIELDES_STEP_DEBUG_FIT_ALL") != nullptr;
    if (dbgAll)
    {
        fprintf(stderr, "[fit-cands] %.4g across:", s.size);
        for (const auto& c : cands)
        {
            if (c.ok()) fprintf(stderr, " %s %.3g (flipped %.0f%%, turned %.0f%%)", FittedSurface::name(c.kind),
                                c.maxErr, 100.0 * c.flipped, 100.0 * c.turned);
        }
        fprintf(stderr, "\n");
    }
    const FittedSurface best = bestFit(cands);
    if (best.ok()) return best;
    // Nothing fits without folding: the best plane (a plane never folds
    // into the part), however far off
    if (cands.size() > 0 && cands[0].ok()) return cands[0];
    // Too few samples even for that: the plane through the face's box
    // across the patch's normal
    FittedSurface p;
    p.kind = FittedSurface::PLANE;
    p.center = 0.5 * (face.boundMin + face.boundMax);
    Vec3 n(0, 0, 1);
    if (face.patch)
    {
        const auto& bs = *face.patch;
        n = bs.normal(0.5 * (bs.u0 + bs.u1), 0.5 * (bs.v0 + bs.v1));
    }
    const Vec3 e1 = across(n);
    p.frame.row(0) = e1.transpose();
    p.frame.row(1) = n.cross(e1).transpose();
    p.frame.row(2) = n.transpose();
    p.maxErr = p.rmsErr = std::max(s.size, 1e-9);
    return p;
}

bool fitHasShape(const FittedSurface& f)
{
    return f.ok() && f.flipped < 0.01 && f.turned <= 0.5;
}

FittedSurface bestFit(const std::vector<FittedSurface>& candidates)
{
    // only those with the face's shape, then the error among them
    double best = std::numeric_limits<double>::infinity();
    for (const auto& f : candidates)
    {
        if (fitHasShape(f)) best = std::min(best, f.maxErr);
    }
    for (const auto& f : candidates)   // (in order: simplest first)
    {
        if (fitHasShape(f) && f.maxErr <= 1.25 * best) return f;
    }
    // none has it: the one that turns away least (not a plane by default)
    const FittedSurface* least = nullptr;
    for (const auto& f : candidates)
    {
        if (f.ok() && f.flipped < 0.01 && (!least || f.turned < least->turned)) least = &f;
    }
    return least ? *least : FittedSurface();
}

FittedSurface fitFillet(const FaceSamples& s, const std::shared_ptr<const SurfaceDistance>& A,
                        const std::shared_ptr<const SurfaceDistance>& B, double r)
{
    FittedSurface best;
    if (!A || !B || A->kind < 0 || B->kind < 0 || s.p.size() < 10) return best;
    Vec3 c = Vec3::Zero();
    for (const auto& q : s.p) c += q;
    c /= double(s.p.size());
    for (double sa : {1.0, -1.0})
    {
        for (double sb : {1.0, -1.0})
        {
            double rr = r;
            if (!(rr > 0))
            {
                // the radius that puts each sample on the quarter pipe:
                // (a + r)^2 + (b + r)^2 = r^2 with a, b <= 0
                std::vector<double> est;
                for (const auto& q : s.p)
                {
                    const double a = sa * A->value(q), b = sb * B->value(q);
                    if (a <= 0 && b <= 0) est.push_back(-(a + b) + std::sqrt(2.0 * a * b));
                }
                if (est.size() < s.p.size() / 2) continue;
                std::nth_element(est.begin(), est.begin() + est.size() / 2, est.end());
                rr = est[est.size() / 2];
                if (!(rr > 0)) continue;
            }
            FittedSurface f;
            f.kind = FittedSurface::FILLET;
            f.center = c;
            f.radius = rr;
            f.sA = sa;
            f.sB = sb;
            f.nbA = A;
            f.nbB = B;
            measure(f, s);
            if (f.ok() && f.flipped > 0.5)
            {
                f.sign = -1.0;      // (its gradient along the face's normal)
                measure(f, s);
            }
            if (f.ok() && (!best.ok() || f.maxErr < best.maxErr)) best = f;
        }
    }
    return best;
}

}   // namespace step
}   // namespace libfive

////////////////////////////////////////////////////////////////////////////////

// Report: for every B-spline face of every solid, each kind's fit error
// (the go / no-go measurement for replacing B-spline leaves with fits).
// One line per face; sizes and errors in model units and in mm.
extern "C" int libfive_step_fit_report(const char* path, const char* outPath)
{
    using namespace libfive::step;
    ImportResult result = importStepFile(path);
    if (!result.ok || !result.model) return -1;
    FILE* out = fopen(outPath, "w");
    if (!out) return -1;
    const double mm = result.fileUnitMM;
    fprintf(out, "# %s: %zu solids, %.6g mm per unit\n", path, result.model->solids.size(), mm);
    fprintf(out, "# solid face samples size_mm | max error mm (flipped %%) plane quadric extruded revolved helical"
                 " | best kind err_mm err_%%_of_face\n");
    int count = 0;
    for (size_t si = 0; si < result.model->solids.size(); ++si)
    {
        const Solid& solid = result.model->solids[si];
        for (size_t fi = 0; fi < solid.faces.size(); ++fi)
        {
            const Face& face = solid.faces[fi];
            if (face.surface.kind != SurfaceKind::BSpline || !face.patch) continue;
            const FaceSamples s = sampleFace(face);
            const auto cands = fitCandidates(s);
            const FittedSurface best = bestFit(cands);
            fprintf(out, "%zu %zu %zu %.3f |", si, fi, s.p.size(), s.size * mm);
            for (const auto& f : cands)
            {
                if (f.ok()) fprintf(out, " %.4g(%.0f%%)", f.maxErr * mm, 100.0 * f.flipped);
                else fprintf(out, " -");
            }
            if (best.ok())
            {
                fprintf(out, " | %s %.4g %.3f\n", FittedSurface::name(best.kind), best.maxErr * mm,
                        s.size > 0 ? 100.0 * best.maxErr / s.size : 0.0);
            }
            else
            {
                fprintf(out, " | none\n");
            }
            count++;
        }
    }
    fclose(out);
    return count;
}
