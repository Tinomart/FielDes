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

}   // anonymous namespace

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
        tangent[size_t(i)] = turnedBy(c[size_t(i)], t) * c[size_t(i)].scale;
    }
    return tangent;
}

std::vector<Eigen::Vector3d> polyline(const std::vector<Control>& c, bool smoothDefault, bool closed, int pieces,
                                      std::vector<std::pair<int, double>>* owners)
{
    std::vector<Eigen::Vector3d> out;
    if (owners) owners->clear();
    const int n = int(c.size());
    if (n == 0) return out;
    if (pieces < 1) pieces = 1;
    auto smooth = [&](int i) { return c[size_t(i)].smooth < 0 ? smoothDefault : c[size_t(i)].smooth != 0; };
    const std::vector<Eigen::Vector3d> tangent = tangents(c, smoothDefault, closed);

    const int last = closed ? n : n - 1;
    for (int i = 0; i < last; ++i)
    {
        const int j = (i + 1) % n;
        const Eigen::Vector3d& a = c[size_t(i)].pos;
        const Eigen::Vector3d& b = c[size_t(j)].pos;
        if (!smooth(i) && !smooth(j))
        {
            out.push_back(a);
            if (owners) owners->push_back({i, 0.0});
            continue;
        }
        const Eigen::Vector3d chord = b - a;
        const Eigen::Vector3d h1 = a + (smooth(i) ? tangent[size_t(i)] : chord) / 3.0;
        const Eigen::Vector3d h2 = b - (smooth(j) ? tangent[size_t(j)] : chord) / 3.0;
        for (int k = 0; k < pieces; ++k)
        {
            const double t = double(k) / double(pieces), m = 1 - t;
            out.push_back(a * (m * m * m) + h1 * (3 * m * m * t) + h2 * (3 * m * t * t) + b * (t * t * t));
            if (owners) owners->push_back({i, t});
        }
    }
    if (!closed)
    {
        out.push_back(c[size_t(n - 1)].pos);
        if (owners) owners->push_back({n - 1, 0.0});
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
