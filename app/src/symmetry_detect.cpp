/*
FielDes: field-driven design

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/
#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

#include "fieldes/shape.hpp"
#include "fieldes/symmetry.hpp"

namespace FielDes {

namespace {

// Whether a field is the same at a place and at its mirror image across a plane through `centre`, at a spread of places round the model.  Many planes are asked at once: the
// field is read with one evaluator
class Probe
{
public:
    Probe(const Shape& shape, const QVector3D& centre, float half) : m_shape(shape), m_centre(centre), m_tolerance(2e-3f * 2.0f * half)
    {
        std::mt19937 rng(11);
        std::uniform_real_distribution<float> u(-1.4f * half, 1.4f * half);
        for (int i = 0; i < 160; ++i) m_points.push_back(QVector3D(centre.x() + u(rng), centre.y() + u(rng), centre.z() + u(rng)));
        std::vector<std::vector<float>> values;
        m_ok = shape.sampleSets({m_points}, values);
        if (m_ok) m_base = values[0];
    }
    bool ok() const { return m_ok; }
    // (which of the planes -- unit normals -- the field is the same across)
    QList<bool> invariant(const QList<QVector3D>& normals) const
    {
        QList<bool> out;
        for (int i = 0; i < normals.size(); ++i) out << false;
        if (!m_ok || normals.isEmpty()) return out;
        std::vector<std::vector<QVector3D>> sets;
        for (const QVector3D& normal : normals)
        {
            const QVector3D n = normal.normalized();
            std::vector<QVector3D> mirrored;
            mirrored.reserve(m_points.size());
            for (const QVector3D& p : m_points) mirrored.push_back(p - 2.0f * QVector3D::dotProduct(p - m_centre, n) * n);
            sets.push_back(std::move(mirrored));
        }
        std::vector<std::vector<float>> values;
        if (!m_shape.sampleSets(sets, values)) return out;
        for (int k = 0; k < normals.size(); ++k)
        {
            bool same = true;
            for (size_t i = 0; i < m_base.size() && same; ++i) same = std::fabs(values[size_t(k)][i] - m_base[i]) <= m_tolerance;
            out[k] = same;
        }
        return out;
    }
    bool invariant(const QVector3D& normal) const { return invariant(QList<QVector3D>{normal})[0]; }

private:
    const Shape& m_shape;
    QVector3D m_centre;
    float m_tolerance;
    std::vector<QVector3D> m_points;
    std::vector<float> m_base;
    bool m_ok = false;
};

bool parallel(const QVector3D& a, const QVector3D& b)
{
    return std::fabs(QVector3D::dotProduct(a.normalized(), b.normalized())) > 0.9999f;
}

}   // namespace

bool detectSymmetry(const Shape& shape, Symmetry& out)
{
    if (shape.isPoint() || !shape.done()) return false;
    QVector3D lo, hi;
    if (!shape.meshBox(lo, hi)) return false;
    const QVector3D size = hi - lo;
    const float half = 0.5f * std::max({size.x(), size.y(), size.z()});
    if (!(half > 1e-4f) || !std::isfinite(half)) return false;
    const QVector3D centre = (lo + hi) * 0.5f;
    Probe probe(shape, centre, half);
    if (!probe.ok()) return false;

    // The planes to try first: the world's axes and their diagonals, and the model's own axes (as its gizmo turns it) and theirs
    QList<QVector3D> candidates;
    auto add = [&](const QVector3D& n) {
        if (n.length() < 1e-6f) return;
        for (const QVector3D& c : candidates) if (parallel(c, n)) return;
        candidates << n.normalized();
    };
    auto addFrame = [&](const QVector3D& x, const QVector3D& y, const QVector3D& z) {
        for (const QVector3D& e : {x, y, z}) add(e);
        add(x + y); add(x - y); add(x + z); add(x - z); add(y + z); add(y - z);
    };
    addFrame(QVector3D(1, 0, 0), QVector3D(0, 1, 0), QVector3D(0, 0, 1));
    if (shape.gizmo().present)
    {
        const QMatrix4x4 R = shape.gizmoOrientation();
        addFrame(R.mapVector(QVector3D(1, 0, 0)), R.mapVector(QVector3D(0, 1, 0)), R.mapVector(QVector3D(0, 0, 1)));
    }
    QList<QVector3D> found;
    {
        const QList<bool> same = probe.invariant(candidates);
        for (int i = 0; i < candidates.size(); ++i)
            if (same[i]) found << candidates[i];
    }
    if (found.isEmpty()) return false;

    // The axes the planes may be round: the normal of a plane (the one that goes across the axis), and the line two planes cross in
    QList<QVector3D> axes;
    auto addAxis = [&](const QVector3D& a) {
        if (a.length() < 1e-4f) return;
        for (const QVector3D& c : axes) if (parallel(c, a)) return;
        axes << a.normalized();
    };
    for (const QVector3D& n : found) addAxis(n);
    for (int i = 0; i < found.size(); ++i)
        for (int j = i + 1; j < found.size(); ++j) addAxis(QVector3D::crossProduct(found[i], found[j]));

    // Round each axis: the planes that contain it, every 7.5 degrees (24 of them), and whether the plane across it is one -- the axis with the most planes is the symmetry's
    struct Fit { QVector3D c; QList<int> v; bool across = false; QVector3D ref; int score = -1; };
    Fit best;
    for (const QVector3D& c : axes)
    {
        Fit f;
        f.c = c;
        QVector3D unused;
        Symmetry::frameFrom(c, QVector3D(0, 0, 1), f.ref, unused);
        const QVector3D second = QVector3D::crossProduct(c, f.ref);
        QList<QVector3D> planes;
        for (int k = 0; k < 24; ++k)
        {
            const float t = float(M_PI) * float(k) / 24.0f;
            planes << QVector3D::crossProduct(c, f.ref * std::cos(t) + second * std::sin(t));
        }
        planes << c;
        const QList<bool> same = probe.invariant(planes);
        for (int k = 0; k < 24; ++k)
            if (same[k]) f.v << k;
        f.across = same[24];
        f.score = int(f.v.size()) * 2 + (f.across ? 1 : 0);
        if (f.score > best.score) best = f;
    }
    if (best.score <= 0) return false;

    Symmetry s;
    s.on = true;
    s.centre = centre;
    if (best.v.isEmpty())
    {
        // Only the plane across an axis: that plane alone, as the one plane (its normal is `best.c`) of an axis that is in it
        Symmetry::frameFrom(best.c, QVector3D(0, 0, 1), s.a, s.c);                    // (a: across best.c)
        s.c = QVector3D::crossProduct(s.a, best.c).normalized();                      // (the axis: in the plane; the first plane's normal c x a is best.c)
        s.kind = Symmetry::Axis;
        s.count = 1;
        out = s;
        return true;
    }
    // How many planes about the axis, equally spaced: the most of 8, 6, 4, 3, 2, 1 that all are there (from the first)
    int count = 1;
    const int first = best.v.first();
    for (const int n : {8, 6, 4, 3, 2, 1})
    {
        const int step = 24 / n;
        bool all = true;
        for (int k = 0; k < n && all; ++k) all = best.v.contains((first + k * step) % 24);
        if (all) { count = n; break; }
    }
    s.kind = best.across ? Symmetry::AxisAndAcross : Symmetry::Axis;
    s.count = count;
    s.c = best.c;
    const QVector3D second = QVector3D::crossProduct(best.c, best.ref);
    const float t = float(M_PI) * float(first) / 24.0f;
    // (the first plane: the line `a` is in it, across the axis, and its normal is c x a)
    s.a = (best.ref * std::cos(t) + second * std::sin(t)).normalized();
    if (best.across && count == 4)
    {
        // The nine planes of a cube: the axis, and four planes round it that are 45 degrees apart, are a cube's when the planes tilted 45 degrees to the axis are there too.  The
        // cube's axis planes are two of the four, 90 degrees apart: the first and the third, or the second and the fourth
        for (int shift = 0; shift < 2; ++shift)
        {
            const float u = float(M_PI) * float(first + 6 * shift) / 24.0f;
            const QVector3D d = (best.ref * std::cos(u) + second * std::sin(u)).normalized();
            const QVector3D na = QVector3D::crossProduct(best.c, d).normalized();            // (a normal of a cube's axis plane)
            const QVector3D nb = QVector3D::crossProduct(best.c, na).normalized();
            const float r = std::sqrt(0.5f);
            const QList<bool> tilted = probe.invariant(QList<QVector3D>{(na + best.c) * r, (na - best.c) * r, (nb + best.c) * r, (nb - best.c) * r});
            if (tilted[0] && tilted[1] && tilted[2] && tilted[3])
            {
                s.kind = Symmetry::Cube;
                s.a = na;
                break;
            }
        }
    }
    out = s;
    return true;
}

}   // namespace FielDes
