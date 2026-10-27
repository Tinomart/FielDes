/*
libfive: a CAD kernel for modeling with implicit functions

Shapes made of points; see point_shapes.hpp.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

#include <manifold/polygon.h>

#include "libfive/fields/point_shapes.hpp"

namespace libfive {
namespace pointshapes {

namespace {

const double kPi = 3.14159265358979323846;

// The vector t turned the way the gizmo turns a shape: about x, then y, then z (degrees)
Eigen::Vector3d rotated(Eigen::Vector3d t, const Eigen::Vector3d& degrees)
{
    for (int axis = 0; axis < 3; ++axis)
    {
        if (std::abs(degrees[axis]) < 1e-12) continue;
        const double a = degrees[axis] * kPi / 180.0, c = std::cos(a), s = std::sin(a);
        const double x = t.x(), y = t.y(), z = t.z();
        if (axis == 0) t = Eigen::Vector3d(x, y * c - z * s, y * s + z * c);
        else if (axis == 1) t = Eigen::Vector3d(x * c + z * s, y, z * c - x * s);
        else t = Eigen::Vector3d(x * c - y * s, x * s + y * c, z);
    }
    return t;
}

// The direction a point leaves in, turned by the point's rotation: about x, then y, then z, or -- a point of a face -- by the one angle (the z number)
// about the normal of the plane of the face
Eigen::Vector3d turnedBy(const Control& c, const Eigen::Vector3d& t)
{
    if (c.planar)
    {
        const double a = c.rotate.z() * kPi / 180.0;
        if (std::abs(a) < 1e-12) return t;
        const Eigen::Vector3d k = c.normal.normalized();
        return t * std::cos(a) + k.cross(t) * std::sin(a) + k * (k.dot(t) * (1 - std::cos(a)));
    }
    return c.turned ? rotated(t, c.rotate) : t;
}

// The places of the controls.  The points of a face are put onto its plane first (the plane through the first one, across the face's normal): a corner is rounded in the
// plane of the face, whatever height a point was given
std::vector<Eigen::Vector3d> positionsOf(const std::vector<Control>& c)
{
    std::vector<Eigen::Vector3d> p;
    p.reserve(c.size());
    for (const auto& k : c) p.push_back(k.pos);
    if (!c.empty() && c[0].planar && c[0].normal.squaredNorm() > 1e-24)
    {
        const Eigen::Vector3d n = c[0].normal.normalized();
        const Eigen::Vector3d origin = p[0];
        for (auto& q : p) q -= n * n.dot(q - origin);
    }
    return p;
}

}   // anonymous namespace

std::vector<Fillet> fillets(const std::vector<Control>& c, bool smoothDefault, bool closed)
{
    std::vector<Fillet> out;
    const int n = int(c.size());
    if (n < 3) return out;
    const std::vector<Eigen::Vector3d> P = positionsOf(c);
    auto smooth = [&](int i) { return c[size_t(i)].smooth < 0 ? smoothDefault : c[size_t(i)].smooth != 0; };
    auto prev = [&](int i) { return closed ? (i - 1 + n) % n : i - 1; };
    auto next = [&](int i) { return closed ? (i + 1) % n : i + 1; };

    // What each round corner asks for: the length t its arc takes from each of its two edges, radius / tan(half the angle between the edges)
    struct Corner { bool round = false; double t = 0, radius = 0, theta = 0; Eigen::Vector3d u1, u2; };
    std::vector<Corner> corner;
    corner.resize(size_t(n));
    for (int i = 0; i < n; ++i)
    {
        if (!(c[size_t(i)].radius > 0) || smooth(i) || (!closed && (i == 0 || i == n - 1))) continue;
        const Eigen::Vector3d a = P[size_t(prev(i))] - P[size_t(i)], b = P[size_t(next(i))] - P[size_t(i)];
        const double la = a.norm(), lb = b.norm();
        if (la < 1e-12 || lb < 1e-12) continue;
        Corner& k = corner[size_t(i)];
        k.u1 = a / la;
        k.u2 = b / lb;
        k.theta = std::acos(std::max(-1.0, std::min(1.0, k.u1.dot(k.u2))));
        if (k.theta < 1e-4 || k.theta > kPi - 1e-4) continue;                       // (a spike, or a straight run: nothing to round)
        k.round = true;
        k.radius = c[size_t(i)].radius;
        k.t = k.radius / std::tan(0.5 * k.theta);
    }
    // The two arcs on one edge may take no more than the edge is long: both are made smaller by the same share (a corner by the least share of its two edges)
    std::vector<double> share(size_t(n), 1.0);
    const int edges = closed ? n : n - 1;
    for (int i = 0; i < edges; ++i)
    {
        const int j = next(i);
        const double length = (P[size_t(j)] - P[size_t(i)]).norm();
        const double wanted = (corner[size_t(i)].round ? corner[size_t(i)].t : 0.0) + (corner[size_t(j)].round ? corner[size_t(j)].t : 0.0);
        if (wanted > length && wanted > 0)
        {
            const double s = length / wanted;
            share[size_t(i)] = std::min(share[size_t(i)], s);
            share[size_t(j)] = std::min(share[size_t(j)], s);
        }
    }
    for (int i = 0; i < n; ++i)
    {
        const Corner& k = corner[size_t(i)];
        if (!k.round) continue;
        const double radius = k.radius * share[size_t(i)], t = k.t * share[size_t(i)];
        if (!(radius > 1e-12)) continue;
        Fillet f;
        f.control = i;
        f.a = P[size_t(i)] + k.u1 * t;
        f.b = P[size_t(i)] + k.u2 * t;
        const Eigen::Vector3d bisector = (k.u1 + k.u2).normalized();
        f.centre = P[size_t(i)] + bisector * (radius / std::sin(0.5 * k.theta));
        f.radius = radius;
        f.corner = P[size_t(i)];
        f.theta = k.theta;
        out.push_back(f);
    }
    return out;
}

////////////////////////////////////////////////////////////////////////////////

std::vector<Eigen::Vector3d> tangents(const std::vector<Control>& c, bool smoothDefault, bool closed)
{
    const int n = int(c.size());
    std::vector<Eigen::Vector3d> tangent(size_t(n), Eigen::Vector3d::Zero());
    for (int i = 0; i < n; ++i)
    {
        const bool smooth = c[size_t(i)].smooth < 0 ? smoothDefault : c[size_t(i)].smooth != 0;
        if (!smooth) continue;
        Eigen::Vector3d t;
        if (n < 2) t = Eigen::Vector3d::Zero();
        else if (closed) t = 0.5 * (c[size_t((i + 1) % n)].pos - c[size_t((i - 1 + n) % n)].pos);
        else if (i == 0) t = c[1].pos - c[0].pos;
        else if (i == n - 1) t = c[size_t(n - 1)].pos - c[size_t(n - 2)].pos;
        else t = 0.5 * (c[size_t(i + 1)].pos - c[size_t(i - 1)].pos);
        tangent[size_t(i)] = turnedBy(c[size_t(i)], t) * c[size_t(i)].influence;
    }
    return tangent;
}

namespace {

// The cubic from `a` to `b`, the ends of the stretch of a line between two controls: it leaves `a` in the tangent of the first control (along the chord when that one is a corner,
// `tangentA` null) and arrives at `b` in the tangent of the second
void cubicBetween(const Eigen::Vector3d& a, const Eigen::Vector3d& b, const Eigen::Vector3d* tangentA, const Eigen::Vector3d* tangentB, Eigen::Vector3d p[4])
{
    const Eigen::Vector3d chord = b - a;
    p[0] = a;
    p[1] = a + (tangentA ? *tangentA : chord) / 3.0;
    p[2] = b - (tangentB ? *tangentB : chord) / 3.0;
    p[3] = b;
}

}   // namespace

std::vector<Curve> curves(const std::vector<Control>& c, bool smoothDefault, bool closed)
{
    std::vector<Curve> out;
    const int n = int(c.size());
    if (n < 2) return out;
    auto smooth = [&](int i) { return c[size_t(i)].smooth < 0 ? smoothDefault : c[size_t(i)].smooth != 0; };
    const std::vector<Eigen::Vector3d> P = positionsOf(c);
    const std::vector<Eigen::Vector3d> tangent = tangents(c, smoothDefault, closed);
    const std::vector<Fillet> arcs = fillets(c, smoothDefault, closed);
    std::vector<const Fillet*> round(size_t(n), nullptr);
    for (const auto& f : arcs) round[size_t(f.control)] = &f;
    const int last = closed ? n : n - 1;
    for (int i = 0; i < last; ++i)
    {
        const int j = (i + 1) % n;
        if (!smooth(i) && !smooth(j)) continue;
        Curve k;
        k.control = i;
        cubicBetween(round[size_t(i)] ? round[size_t(i)]->b : P[size_t(i)], round[size_t(j)] ? round[size_t(j)]->a : P[size_t(j)],
                     smooth(i) ? &tangent[size_t(i)] : nullptr, smooth(j) ? &tangent[size_t(j)] : nullptr, k.p);
        out.push_back(k);
    }
    return out;
}

std::vector<Eigen::Vector3d> polyline(const std::vector<Control>& c, bool smoothDefault, bool closed, int pieces,
                                      std::vector<std::pair<int, double>>* owners, int arcPieces)
{
    std::vector<Eigen::Vector3d> out;
    if (owners) owners->clear();
    const int n = int(c.size());
    if (n == 0) return out;
    if (pieces < 1) pieces = 1;
    auto smooth = [&](int i) { return c[size_t(i)].smooth < 0 ? smoothDefault : c[size_t(i)].smooth != 0; };
    const std::vector<Eigen::Vector3d> P = positionsOf(c);
    const std::vector<Eigen::Vector3d> tangent = tangents(c, smoothDefault, closed);
    const std::vector<Fillet> arcs = fillets(c, smoothDefault, closed);
    std::vector<const Fillet*> round(size_t(n), nullptr);
    for (const auto& f : arcs) round[size_t(f.control)] = &f;

    // (a vertex is not given twice in a row: two arcs that meet share the point where they do)
    auto push = [&](const Eigen::Vector3d& v, int owner, double along) {
        if (!out.empty() && (out.back() - v).norm() <= 1e-12 * (1.0 + v.norm())) return;
        out.push_back(v);
        if (owners) owners->push_back({owner, along});
    };
    // The vertices of a control itself: where it is, or -- a round corner -- its arc from `a` to `b`
    auto place = [&](int i) {
        const Fillet* f = round[size_t(i)];
        if (!f)
        {
            push(P[size_t(i)], i, 0.0);
            return;
        }
        const Eigen::Vector3d e1 = f->a - f->centre;
        const Eigen::Vector3d eb = f->b - f->centre;
        const double r = e1.norm();
        Eigen::Vector3d w = eb - e1 * (eb.dot(e1) / e1.squaredNorm());              // (across the first radius, towards b)
        const double across = w.norm();
        const double sweep = std::atan2(across, eb.dot(e1) / r);                    // (the angle from a to b, as seen from the centre: less than a half turn)
        const Eigen::Vector3d e2 = across > 1e-300 ? Eigen::Vector3d(w * (r / across)) : Eigen::Vector3d::Zero();
        const int count = arcPieces <= 0 ? 1 : std::max(1, int(std::ceil(sweep / (0.5 * kPi) * double(arcPieces))));
        push(f->a, i, 0.0);
        for (int k = 1; k < count; ++k)
        {
            const double phi = sweep * double(k) / double(count);
            push(f->centre + e1 * std::cos(phi) + e2 * std::sin(phi), i, 0.0);
        }
        push(f->b, i, 0.0);
    };

    const int last = closed ? n : n - 1;
    for (int i = 0; i < last; ++i)
    {
        const int j = (i + 1) % n;
        place(i);
        if (!smooth(i) && !smooth(j)) continue;                                      // (a straight piece: nothing between its ends)
        // (a curve between two controls starts where the arc of the first ends and ends where the arc of the second begins: along the same chord)
        const Eigen::Vector3d a = round[size_t(i)] ? round[size_t(i)]->b : P[size_t(i)];
        const Eigen::Vector3d b = round[size_t(j)] ? round[size_t(j)]->a : P[size_t(j)];
        Eigen::Vector3d h[4];
        cubicBetween(a, b, smooth(i) ? &tangent[size_t(i)] : nullptr, smooth(j) ? &tangent[size_t(j)] : nullptr, h);
        for (int k = 1; k < pieces; ++k)
        {
            const double t = double(k) / double(pieces), m = 1 - t;
            push(h[0] * (m * m * m) + h[1] * (3 * m * m * t) + h[2] * (3 * m * t * t) + h[3] * (t * t * t), i, t);
        }
    }
    if (!closed) place(n - 1);
    // (a closed line that ends where it begins -- the arcs of its last and first corners meet -- does not give that point twice)
    if (closed && out.size() > 1 && (out.front() - out.back()).norm() <= 1e-12 * (1.0 + out.front().norm()))
    {
        out.pop_back();
        if (owners) owners->pop_back();
    }
    return out;
}

bool planeOf(const std::vector<Eigen::Vector3d>& p, double frame[9])
{
    if (p.size() < 3) return false;
    const Eigen::Vector3d e1 = p[1] - p[0];
    const double n1 = e1.squaredNorm();
    if (n1 < 1e-18) return false;
    for (size_t k = 2; k < p.size(); ++k)
    {
        const Eigen::Vector3d e2 = p[k] - p[0];
        const Eigen::Vector3d cr = e1.cross(e2);
        if (cr.squaredNorm() > 1e-12 * n1 * std::max(1.0, e2.squaredNorm()))
        {
            const Eigen::Vector3d u = e1 / std::sqrt(n1);
            const Eigen::Vector3d n = cr.normalized();
            const Eigen::Vector3d v = n.cross(u);
            for (int a = 0; a < 3; ++a)
            {
                frame[a] = p[0][a];
                frame[3 + a] = u[a];
                frame[6 + a] = v[a];
            }
            return true;
        }
    }
    return false;
}

std::unique_ptr<Mesh> face(const std::vector<Eigen::Vector3d>& loop, const double frame[9], std::vector<std::vector<Eigen::Vector3f>>* outlines)
{
    if (outlines) outlines->clear();
    if (loop.size() < 3) return nullptr;
    const Eigen::Vector3d o(frame[0], frame[1], frame[2]), u(frame[3], frame[4], frame[5]), v(frame[6], frame[7], frame[8]);
    std::vector<Eigen::Vector2d> flat;
    for (const auto& p : loop) flat.emplace_back((p - o).dot(u), (p - o).dot(v));
    double area = 0;
    for (size_t i = 0; i < flat.size(); ++i)
    {
        const auto& a = flat[i];
        const auto& b = flat[(i + 1) % flat.size()];
        area += a.x() * b.y() - b.x() * a.y();
    }
    auto out = std::make_unique<Mesh>();
    std::vector<Eigen::Vector3f> line;
    manifold::SimplePolygonIdx poly;
    const size_t n = flat.size();
    const size_t first = out->verts.size();
    for (size_t i = 0; i < n; ++i)
    {
        const Eigen::Vector3d q = o + u * flat[i].x() + v * flat[i].y();
        out->verts.push_back(Eigen::Vector3f(float(q.x()), float(q.y()), float(q.z())));
        line.push_back(out->verts.back());
    }
    for (size_t k = 0; k < n; ++k)
    {
        // (counter-clockwise, as the triangulation wants it; the outline keeps the order the vertices were given in)
        const size_t i = area >= 0 ? k : n - 1 - k;
        poly.push_back({manifold::vec2(flat[i].x(), flat[i].y()), int(first + i)});
    }
    line.push_back(line.front());
    if (outlines) outlines->push_back(std::move(line));
    try
    {
        for (const auto& tri : manifold::TriangulateIdx({poly}))
        {
            out->branes.push_back(Eigen::Matrix<uint32_t, 3, 1>(uint32_t(tri.x), uint32_t(tri.y), uint32_t(tri.z)));
        }
    }
    catch (const std::exception&)
    {
        return nullptr;
    }
    return out;
}

}   // namespace pointshapes
}   // namespace libfive
