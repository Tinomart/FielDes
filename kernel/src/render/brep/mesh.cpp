/*
libfive: a CAD kernel for modeling with implicit functions

Copyright (C) 2017  Matt Keeter

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#include <algorithm>
#include <cmath>
#include <numeric>
#include <fstream>
#include <boost/algorithm/string/predicate.hpp>

#include <manifold/polygon.h>

#include "libfive/eval/evaluator.hpp"

#include "libfive/render/brep/mesh.hpp"
#include "libfive/render/brep/contours.hpp"
#include "libfive/render/brep/dual.hpp"
#include "libfive/render/brep/region.hpp"
#include "libfive/render/brep/settings.hpp"

// Dual contouring
#include "libfive/render/brep/dc/dc_worker_pool.hpp"
#include "libfive/render/brep/dc/dc_mesher.hpp"

// Simplex meshing
#include "libfive/render/brep/simplex/simplex_worker_pool.hpp"
#include "libfive/render/brep/simplex/simplex_mesher.hpp"

// Hybrid meshing
#include "libfive/render/brep/hybrid/hybrid_worker_pool.hpp"
#include "libfive/render/brep/hybrid/hybrid_mesher.hpp"

namespace libfive {

std::unique_ptr<Mesh> Mesh::render(const Tree& t_, const Region<3>& r,
                                   const BRepSettings& settings)
{
    std::vector<Evaluator, Eigen::aligned_allocator<Evaluator>> es;
    es.reserve(settings.workers);
    const auto t = t_.optimized();
    for (unsigned i=0; i < settings.workers; ++i) {
        es.emplace_back(Evaluator(t));
    }

    return render(es.data(), r, settings);
}

std::unique_ptr<Mesh> Mesh::render(
        Evaluator* es,
        const Region<3>& r, const BRepSettings& settings)
{
    std::unique_ptr<Mesh> out;
    if (settings.alg == DUAL_CONTOURING)
    {
        if (settings.progress_handler) {
            // Pool::build, Dual::walk, t.reset
            settings.progress_handler->start({1, 1, 1});
        }
        auto t = DCWorkerPool<3>::build(es, r, settings);

        if (settings.cancel.load() || t.get() == nullptr) {
            if (settings.progress_handler) {
                settings.progress_handler->finish();
            }
            return nullptr;
        }

        // Perform marching squares
        out = Dual<3>::walk<DCMesher>(t, settings);

        // TODO: check for early return here again
        t.reset(settings);
    }
    else if (settings.alg == ISO_SIMPLEX)
    {
        if (settings.progress_handler) {
            // Pool::build, Dual::walk, t->assignIndices, t.reset
            settings.progress_handler->start({1, 1, 1});
        }
        auto t = SimplexWorkerPool<3>::build(es, r, settings);

        if (settings.cancel.load() || t.get() == nullptr) {
            if (settings.progress_handler) {
                settings.progress_handler->finish();
            }
            return nullptr;
        }

        t->assignIndices(settings);

        out = Dual<3>::walk_<SimplexMesher>(t, settings,
                [&](PerThreadBRep<3>& brep, int i) {
                    return SimplexMesher(brep, &es[i]);
                });
        t.reset(settings);
    }
    else if (settings.alg == HYBRID)
    {
        if (settings.progress_handler) {
            // Pool::build, Dual::walk, t->assignIndices, t.reset
            settings.progress_handler->start({1, 1, 1});
        }
        auto t = HybridWorkerPool<3>::build(es, r, settings);

        if (settings.cancel.load() || t.get() == nullptr) {
            if (settings.progress_handler) {
                settings.progress_handler->finish();
            }
            return nullptr;
        }

        t->assignIndices(settings);

        out = Dual<3>::walk_<HybridMesher>(t, settings,
                [&](PerThreadBRep<3>& brep, int i) {
                    return HybridMesher(brep, &es[i]);
                });
        t.reset(settings);
    }

    if (settings.progress_handler) {
        settings.progress_handler->finish();
    }
    return out;
}

std::unique_ptr<Mesh> Mesh::renderFlat(
        const Tree& t, const std::map<Tree::Id, float>& vars,
        const Region<3>& r, double z, BRepSettings& settings)
{
    auto out = std::make_unique<Mesh>();
    if (!(z >= r.lower.z() && z <= r.upper.z())) {
        return out;
    }

    // The face is cut by a box a cell and a half inside the region, so that a
    // shape that reaches the region's edge still has a closed outline
    const double inset = 1.5 * settings.min_feature;
    const double lo[2] = {r.lower.x() + inset, r.lower.y() + inset};
    const double hi[2] = {r.upper.x() - inset, r.upper.y() - inset};
    if (!(hi[0] > lo[0] && hi[1] > lo[1])) {
        return out;
    }
    auto slab = [&](const Tree& axis, int a) {
        return Tree::binary(Opcode::OP_SUB,
            Tree::unary(Opcode::OP_ABS,
                Tree::binary(Opcode::OP_SUB, axis, Tree(0.5 * (lo[a] + hi[a])))),
            Tree(0.5 * (hi[a] - lo[a])));
    };
    const Tree clipped = Tree::binary(Opcode::OP_MAX, t,
        Tree::binary(Opcode::OP_MAX, slab(Tree::X(), 0), slab(Tree::Y(), 1)));

    std::vector<Evaluator, Eigen::aligned_allocator<Evaluator>> es;
    es.reserve(settings.workers);
    for (unsigned i=0; i < settings.workers; ++i) {
        es.emplace_back(Evaluator(clipped, vars));
    }

    if (settings.progress_handler) {
        // Pool::build, Dual::walk
        settings.progress_handler->start({1, 1, 1});
    }
    const Region<2> plane({r.lower.x(), r.lower.y()}, {r.upper.x(), r.upper.y()},
                          Region<2>::Perp(z));
    auto cs = Contours::render(es.data(), plane, settings);
    if (settings.progress_handler) {
        settings.progress_handler->finish();
    }
    if (cs.get() == nullptr || settings.cancel.load()) {
        return nullptr;
    }

    // The outlines (closed: the first point is the last), their points numbered
    // from the mesh's own vertices
    std::vector<std::vector<Eigen::Vector2d>> loops;
    for (const auto& c : cs->contours) {
        if (c.size() < 4 || c.front() != c.back()) {
            continue;
        }
        std::vector<Eigen::Vector2d> loop;
        for (size_t i=0; i + 1 < c.size(); ++i) {
            loop.push_back(c[i].cast<double>());
        }
        loops.push_back(std::move(loop));
    }

    // The outer outline has the most area: if it runs clockwise, every outline
    // does (the inside is on one side of all of them), and they are turned round
    auto area = [](const std::vector<Eigen::Vector2d>& p) {
        double a = 0;
        for (size_t i=0; i < p.size(); ++i) {
            const auto& u = p[i];
            const auto& v = p[(i + 1) % p.size()];
            a += u.x() * v.y() - v.x() * u.y();
        }
        return 0.5 * a;
    };
    double biggest = 0;
    for (const auto& p : loops) {
        const double a = area(p);
        if (std::abs(a) > std::abs(biggest)) {
            biggest = a;
        }
    }
    if (biggest < 0) {
        for (auto& p : loops) {
            std::reverse(p.begin(), p.end());
        }
    }

    manifold::PolygonsIdx polygons;
    for (const auto& p : loops) {
        manifold::SimplePolygonIdx poly;
        for (const auto& q : p) {
            poly.push_back({manifold::vec2(q.x(), q.y()), int(out->verts.size())});
            out->verts.push_back(Eigen::Vector3f(float(q.x()), float(q.y()), float(z)));
        }
        polygons.push_back(std::move(poly));
    }
    if (!polygons.empty()) {
        try {
            for (const auto& tri : manifold::TriangulateIdx(polygons)) {
                out->branes.push_back(Eigen::Matrix<uint32_t, 3, 1>(
                    uint32_t(tri.x), uint32_t(tri.y), uint32_t(tri.z)));
            }
        } catch (const std::exception&) {
            // (an outline the triangulation cannot take is not drawn)
            out->branes.clear();
        }
    }
    return out;
}

void Mesh::line(const Eigen::Vector3f& a, const Eigen::Vector3f& b)
{
    uint32_t a_ = verts.size();
    verts.push_back(a);
    uint32_t b_ = verts.size();
    verts.push_back(b);

    branes.push_back({a_, a_, b_});
}

////////////////////////////////////////////////////////////////////////////////

bool Mesh::saveSTL(const std::string& filename,
                   const std::list<const Mesh*>& meshes)
{
    if (!boost::algorithm::iends_with(filename, ".stl"))
    {
        std::cerr << "Mesh::saveSTL: filename \"" << filename
                  << "\" does not end in .stl" << std::endl;
    }
    std::ofstream file;
    file.open(filename, std::ios::out | std::ios::binary);
    if (!file.is_open())
    {
        std::cout << "Mesh::saveSTL: could not open " << filename
                  << std::endl;
        return false;
    }

    // File header (giving human-readable info about file type)
    std::string header = "This is a binary STL exported from libfive.";
    file.write(header.c_str(), header.length());

    // Pad the rest of the header to 80 bytes
    for (int i=header.length(); i < 80; ++i)
    {
        file.put(' ');
    }

    // Write the triangle count to the file
    uint32_t num = std::accumulate(meshes.begin(), meshes.end(), (uint32_t)0,
            [](uint32_t i, const Mesh* m){ return i + m->branes.size(); });
    file.write(reinterpret_cast<char*>(&num), sizeof(num));

    for (const auto& m : meshes)
    {
        for (const auto& t : m->branes)
        {
            // Write out the normal vector for this face (all zeros)
            float norm[3] = {0, 0, 0};
            file.write(reinterpret_cast<char*>(&norm), sizeof(norm));

            // Iterate over vertices (which are indices into the verts list)
            for (unsigned i=0; i < 3; ++i)
            {
                auto v = m->verts[t[i]];
                float vert[3] = {v.x(), v.y(), v.z()};
                file.write(reinterpret_cast<char*>(&vert), sizeof(vert));
            }

            // Write out this face's attribute short
            uint16_t attrib = 0;
            file.write(reinterpret_cast<char*>(&attrib), sizeof(attrib));
        }
    }

    return true;
}

bool Mesh::saveSTL(const std::string& filename) const
{
    return saveSTL(filename, {this});
}

}   // namespace libfive
