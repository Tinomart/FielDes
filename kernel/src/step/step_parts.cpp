/*
libfive: a CAD kernel for modeling with implicit functions

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#include <algorithm>
#include <cmath>
#include <limits>

#include "libfive/step/step_parts.hpp"
#include "libfive/step/step_assembly.hpp"
#include "libfive/step/step_tessellate.hpp"

namespace libfive {
namespace step {

SolidMetrics solidMetrics(const Solid& solid)
{
    SolidMetrics m;
    // Surface area by face, from a coarse tessellation (12 points a turn:
    // an area within a few percent, at a fraction of the cost)
    std::vector<double> faceArea(solid.faces.size(), 0.0);
    double volume = 0.0;    // (signed, from the same triangles: a closed surface encloses it)
    try {
        const TessMesh mesh = tessellateSolid(solid, 12);
        for (size_t t = 0; t < mesh.tris.size(); ++t) {
            const int f = mesh.triFaceIdx[t];
            if (f < 0 || size_t(f) >= faceArea.size()) continue;
            const Vec3& a = mesh.verts[size_t(mesh.tris[t][0])];
            const Vec3& b = mesh.verts[size_t(mesh.tris[t][1])];
            const Vec3& c = mesh.verts[size_t(mesh.tris[t][2])];
            faceArea[size_t(f)] += 0.5 * (b - a).cross(c - a).norm();
            volume += a.dot(b.cross(c)) / 6.0;
        }
    } catch (const std::exception&) {
        std::fill(faceArea.begin(), faceArea.end(), 0.0);
        volume = 0.0;
    }
    double total = 0;
    for (double a : faceArea) total += a;

    const Vec3 ext = solid.boundMax - solid.boundMin;
    if (!(total > 0) || !ext.allFinite()) {
        // (no tessellation: the bounding box's area, as if all curved --
        // the pessimistic guess)
        if (ext.allFinite() && ext.minCoeff() >= 0) {
            m.areaCurved = 2 * (ext.x() * ext.y() + ext.y() * ext.z() + ext.z() * ext.x());
        }
    } else {
        for (size_t f = 0; f < solid.faces.size(); ++f) {
            (solid.faces[f].surface.kind == SurfaceKind::Plane ? m.areaFlat : m.areaCurved) += faceArea[f];
        }
    }

    // The smallest feature: the thinnest side of the box, the smallest radius
    // of a curved face that is more than 1 % of the surface, and the material's
    // thickness -- twice the volume over the surface: a sheet's thickness, a
    // hollow tube's wall, less for a perforated plate.  (A tube 38 mm across
    // with a 1.2 mm wall has neither a thin box nor a small radius.)
    const double diag = ext.allFinite() ? ext.norm() : 0.0;
    double detail = std::numeric_limits<double>::infinity();
    if (ext.allFinite()) {
        for (int i = 0; i < 3; ++i) {
            if (ext[i] > 1e-6 * std::max(diag, 1e-12)) detail = std::min(detail, ext[i]);
        }
    }
    if (total > 0 && std::isfinite(volume)) {
        const double thickness = 2.0 * std::abs(volume) / total;
        if (thickness > 1e-6 * std::max(diag, 1e-12)) detail = std::min(detail, thickness);
    }
    for (size_t f = 0; f < solid.faces.size(); ++f) {
        if (!(total > 0) || faceArea[f] < 0.01 * total) continue;
        const Surface& s = solid.faces[f].surface;
        double r = 0;
        switch (s.kind) {
            case SurfaceKind::Cylinder:
            case SurfaceKind::Sphere: r = s.radius; break;
            case SurfaceKind::Torus: r = s.radius2; break;
            default: break;
        }
        if (r > 1e-6 * std::max(diag, 1e-12)) detail = std::min(detail, r);
    }
    m.detail = std::isfinite(detail) ? detail : 0.0;
    return m;
}

// Slot `first` gets the first instance, further instances go to `extra` so
// every solid keeps its index.  Bounds come from the solid's face boxes,
// each placed, which stays tight for rotated parts.
void emitInstances(const Solid& solid, const Tree& local, const std::string& error,
                   StepPart& first, std::vector<StepPart>& extra, const Tree& marker,
                   int solidIndex, const SolidMetrics& metrics)
{
    for (size_t k = 0; k < solid.instances.size(); ++k) {
        const SolidInstance& inst = solid.instances[k];
        Vec3 lo = Vec3::Constant(std::numeric_limits<double>::infinity());
        Vec3 hi = -lo;
        for (const auto& f : solid.faces) {
            placeBox(f.boundMin, f.boundMax, inst, lo, hi);
        }
        if (!lo.allFinite() || !hi.allFinite()) {
            lo = hi = Vec3::Zero();
            placeBox(solid.boundMin, solid.boundMax, inst, lo, hi);
        }
        StepPart part{error.empty() ? placeTree(local, inst) : local, lo, hi, error};
        part.name = inst.name.empty() ? solid.name : inst.name;
        if (error.empty() && marker.is_valid()) part.marker = placeTree(marker, inst, false);   // (relative)
        part.solid = solidIndex;
        part.instance = int(k);
        // (a placed copy is in the delivered units: lengths scale by the
        // instance's unit, areas by its square)
        const double unit = std::cbrt(std::abs(inst.linear.determinant()));
        part.detail = metrics.detail * unit;
        part.areaFlat = metrics.areaFlat * unit * unit;
        part.areaCurved = metrics.areaCurved * unit * unit;
        if (k == 0) first = part;
        else extra.push_back(part);
    }
}

}  // namespace step
}  // namespace libfive
