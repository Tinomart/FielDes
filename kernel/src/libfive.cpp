/*
libfive: a CAD kernel for modeling with implicit functions
Copyright (C) 2017  Matt Keeter

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/
#include <cstring>
#include <algorithm>
#include <chrono>
#include <map>
#include <cmath>
#include <queue>
#include <limits>
#include <mutex>
#include <thread>
#include <vector>
#include <iostream>
#include <fstream>
#include <sstream>

#include "libfive.h"

#include "libfive/tree/opcode.hpp"
#include "libfive/tree/tree.hpp"
#include "libfive/tree/data.hpp"
#include "libfive/tree/content_key.hpp"
#include "libfive/tree/expose.hpp"

#include "libfive/eval/eval_deriv_array.hpp"
#include "libfive/eval/eval_interval.hpp"

#include "libfive/render/brep/region.hpp"
#include "libfive/render/brep/contours.hpp"
#include "libfive/render/brep/mesh.hpp"
#include "libfive/render/brep/settings.hpp"

#include "libfive/render/discrete/voxels.hpp"
#include "libfive/render/discrete/heightmap.hpp"

#include "libfive/step/step_parts.hpp"
#include "libfive/step/step_exact.hpp"
#include "libfive/mesh_import/mesh_import.hpp"
#include "libfive/step/step_bspline.hpp"
#include "libfive/fea/fea.hpp"
#include "libfive/fea/tetmesh.hpp"
#include "libfive/fea/tetfea.hpp"
#include "libfive/fea/tetthermal.hpp"
#include "libfive/step/step_reconstruct.hpp"
#include "libfive/run_progress.hpp"
#include "libfive/fea/thermal.hpp"
#include "libfive/lattice/lattice_graph.hpp"
#include "libfive/fields/field_oracles.hpp"

using namespace libfive;

void libfive_contours_delete(libfive_contours* cs)
{
    for (unsigned i=0; i < cs->count; ++i)
    {
        delete [] cs->cs[i].pts;
    }
    delete [] cs->cs;
    delete cs;
}

void libfive_contours3_delete(libfive_contours3* cs)
{
    for (unsigned i=0; i < cs->count; ++i)
    {
        delete [] cs->cs[i].pts;
    }
    delete [] cs->cs;
    delete cs;
}

void libfive_mesh_delete(libfive_mesh* m)
{
    delete [] m->verts;
    delete [] m->tris;
    delete m;
}

void libfive_mesh_coords_delete(libfive_mesh_coords* m)
{
    delete [] m->verts;
    delete [] m->coord_indices;
    delete m;
}

void libfive_pixels_delete(libfive_pixels* m)
{
    delete [] m->pixels;
    delete m;
}

void libfive_vars_delete(libfive_vars* vs)
{
    delete [] vs->vars;
    delete [] vs->values;
    delete vs;
}

int libfive_opcode_enum(const char* op)
{
    auto o = Opcode::fromScmString(op);
    return (o == Opcode::INVALID || o == Opcode::LAST_OP) ? -1 : o;
}

int libfive_opcode_args(int op)
{
    return (op >= 0 && op < Opcode::LAST_OP)
        ? Opcode::args(Opcode::Opcode(op))
        : -1;
}

////////////////////////////////////////////////////////////////////////////////

libfive_tree libfive_tree_x() { return Tree::X().release(); }
libfive_tree libfive_tree_y() { return Tree::Y().release(); }
libfive_tree libfive_tree_z() { return Tree::Z().release(); }

libfive_tree libfive_tree_const(float f) { return Tree(f).release(); }
libfive_tree libfive_tree_var() { return Tree::var().release(); }

bool libfive_tree_is_var(libfive_tree t)
{
    return t->op() == Opcode::VAR_FREE;
}

float libfive_tree_get_const(libfive_tree t, bool* success)
{
    if (t->op() == Opcode::CONSTANT)
    {
        if (success) { *success = true; }
        return t->value();
    }
    if (success) { *success = false; }
    return 0;
}

static bool opcode_is_valid(int op, size_t expected_args)
{
    return op >= 0 &&
           op < Opcode::LAST_OP &&
           Opcode::args(Opcode::Opcode(op)) == expected_args;
}

libfive_tree libfive_tree_nullary(int op)
{
    return opcode_is_valid(op, 0)
        ? Tree::nullary(Opcode::Opcode(op)).release()
        : nullptr;
}

libfive_tree libfive_tree_unary(int op, libfive_tree a)
{
    return (opcode_is_valid(op, 1) && a != nullptr)
        ? Tree::unary(Opcode::Opcode(op), Tree(a)).release()
        : nullptr;
}

libfive_tree libfive_tree_binary(int op, libfive_tree a, libfive_tree b)
{
    return (opcode_is_valid(op, 2) && a != nullptr && b != nullptr)
        ? Tree::binary(Opcode::Opcode(op), Tree(a), Tree(b)).release()
        : nullptr;
}

const void* libfive_tree_id(libfive_tree t)
{
    return static_cast<const void*>(t);
}

void libfive_tree_delete(libfive_tree ptr)
{
    // Reclaim the ptr (without incrementing the refcount), which
    // then decrements the refcount when the Tree destructor is run.
    Tree::reclaim(ptr);
}

bool libfive_tree_save(libfive_tree ptr, const char* filename)
{
    std::ofstream out;
    out.open(filename, std::ios::out|std::ios::binary);
    if (out.is_open())
    {
        Tree(ptr).serialize(out);
        return true;
    }
    else
    {
        std::cerr << "libfive_tree_save: could not open file" << std::endl;
        return false;
    }
}

libfive_tree libfive_tree_load(const char* filename)
{
    auto t = Tree::load(filename);
    if (t.id() && t.is_valid())
    {
        return t.release();
    }
    else
    {
        std::cerr <<  "libfive_tree_load: could not open file" << std::endl;
        return nullptr;
    }
}

namespace {
    // Valid until the next STEP import, same convention
    // as e.g. libfive_tree_print's returned buffer.
    thread_local std::string g_step_last_message;
}

const char* libfive_import_step_last_message(void)
{
    return g_step_last_message.c_str();
}

namespace {
    thread_local std::string g_mesh_last_message;
}

static libfive_tree finishMeshImport(Tree t, bool ok, const std::string& error,
                                     const mesh::MeshImportInfo& mi,
                                     libfive_mesh_import_info* info);

libfive_tree libfive_import_mesh(const char* filename, float scale,
                                 libfive_mesh_import_info* info)
{
    bool ok = false;
    std::string error;
    mesh::MeshImportInfo mi;
    Tree t = mesh::importMeshTree(filename, scale, mi, ok, error);
    return finishMeshImport(std::move(t), ok, error, mi, info);
}

libfive_tree libfive_mesh_from_arrays(const float* xyz, uint32_t vertex_count,
                                      const uint32_t* tri, uint32_t tri_count,
                                      float scale, libfive_mesh_import_info* info)
{
    bool ok = false;
    std::string error;
    mesh::MeshImportInfo mi;
    Tree t = mesh::meshTreeFromArrays(xyz, vertex_count, tri, tri_count, scale, mi, ok, error);
    return finishMeshImport(std::move(t), ok, error, mi, info);
}

static libfive_tree finishMeshImport(Tree t, bool ok, const std::string& error,
                                     const mesh::MeshImportInfo& mi,
                                     libfive_mesh_import_info* info)
{
    if (!ok)
    {
        g_mesh_last_message = error;
        return nullptr;
    }

    std::ostringstream msg;
    msg << mi.triangles << " triangles, " << mi.components << " shell"
        << (mi.components == 1 ? "" : "s");
    if (mi.watertight)
    {
        msg << ", watertight";
    }
    else
    {
        msg << "; NOT watertight (" << mi.boundaryEdges << " open edge"
            << (mi.boundaryEdges == 1 ? "" : "s") << ", " << mi.nonManifoldEdges
            << " non-manifold): inside/outside is estimated";
    }
    if (mi.reoriented)
    {
        msg << "; " << mi.reoriented << " triangle" << (mi.reoriented == 1 ? "" : "s")
            << " re-oriented";
    }
    if (mi.dropped)
    {
        msg << "; " << mi.dropped << " degenerate/duplicate triangle"
            << (mi.dropped == 1 ? "" : "s") << " dropped";
    }
    g_mesh_last_message = msg.str();

    if (info)
    {
        info->triangles = static_cast<uint32_t>(mi.triangles);
        info->vertices = static_cast<uint32_t>(mi.vertices);
        info->components = static_cast<uint32_t>(mi.components);
        info->boundary_edges = static_cast<uint32_t>(mi.boundaryEdges);
        info->nonmanifold_edges = static_cast<uint32_t>(mi.nonManifoldEdges);
        info->reoriented = static_cast<uint32_t>(mi.reoriented);
        info->dropped = static_cast<uint32_t>(mi.dropped);
        info->watertight = mi.watertight;
        info->winding_sign = mi.windingSign;
        info->bounds.X = {static_cast<float>(mi.lower[0]), static_cast<float>(mi.upper[0])};
        info->bounds.Y = {static_cast<float>(mi.lower[1]), static_cast<float>(mi.upper[1])};
        info->bounds.Z = {static_cast<float>(mi.lower[2]), static_cast<float>(mi.upper[2])};
    }
    return t.release();
}

const char* libfive_import_mesh_last_message(void)
{
    return g_mesh_last_message.c_str();
}

// A new[]-allocated copy (NULL for an empty string), for the part structs
static char* copyString(const std::string& s)
{
    if (s.empty()) return nullptr;
    char* out = new char[s.size() + 1];
    std::memcpy(out, s.c_str(), s.size() + 1);
    return out;
}

int libfive_step_import_version(void)
{
    return libfive::step::kImportVersion;
}

void libfive_step_parts_delete(libfive_step_parts* parts)
{
    if (parts) {
        for (uint32_t i = 0; i < parts->count; i++) {
            delete[] parts->parts[i].error;
            delete[] parts->parts[i].name;
        }
        delete[] parts->parts;
        delete parts;
    }
}

libfive_step_parts* libfive_import_step_parts_reconstructed(const char* filename)
{
    bool ok = false;
    std::string error;
    int numSolids = 0, numFacesResolved = 0, numFacesSkipped = 0;
    int numReconstructed = 0, numOracleFallback = 0;
    std::vector<step::StepPart> parts = step::importStepTreePartsReconstructed(
        filename, ok, error, &numSolids, &numFacesResolved, &numFacesSkipped,
        &numReconstructed, &numOracleFallback);

    std::ostringstream msg;
    if (ok) {
        msg << "Imported " << numSolids << " solid(s), " << numFacesResolved
            << " face(s) as " << parts.size() << " separate part(s) ("
            << numReconstructed << " reconstructed as native CSG";
        if (numOracleFallback > 0) {
            msg << ", " << numOracleFallback << " FAILED (no B-spline/Oracle fallback -- "
                   "see each part's own error)";
        }
        msg << ")";
        if (numFacesSkipped > 0) {
            msg << " (" << numFacesSkipped << " face(s) skipped: unsupported surface type)";
        }
    } else {
        msg << error;
    }
    g_step_last_message = msg.str();

    if (!ok) {
        return nullptr;
    }

    auto* out = new libfive_step_parts;
    out->count = static_cast<uint32_t>(parts.size());
    out->parts = out->count ? new libfive_step_part[out->count] : nullptr;
    for (size_t i = 0; i < parts.size(); i++) {
        out->parts[i].tree = parts[i].tree.release();
        out->parts[i].bounds.X = {static_cast<float>(parts[i].boundMin.x()),
                                   static_cast<float>(parts[i].boundMax.x())};
        out->parts[i].bounds.Y = {static_cast<float>(parts[i].boundMin.y()),
                                   static_cast<float>(parts[i].boundMax.y())};
        out->parts[i].bounds.Z = {static_cast<float>(parts[i].boundMin.z()),
                                   static_cast<float>(parts[i].boundMax.z())};
        out->parts[i].error = copyString(parts[i].error);
        out->parts[i].name = copyString(parts[i].name);
        out->parts[i].marker = parts[i].marker.is_valid() ? parts[i].marker.release() : nullptr;
        out->parts[i].solid = parts[i].solid;
        out->parts[i].instance = parts[i].instance;
        out->parts[i].detail = parts[i].detail;
        out->parts[i].area_flat = parts[i].areaFlat;
        out->parts[i].area_curved = parts[i].areaCurved;
    }
    return out;
}

libfive_mesh* libfive_step_exact_clipped(const char* filename, int solid, int instance,
                                         const double* m, libfive_tree field, double cell,
                                         int turn_samples)
{
    step::ExactSpec spec;
    spec.path = filename;
    spec.solid = solid;
    spec.instance = instance;
    if (m) {
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 4; c++) spec.transform(r, c) = m[r * 4 + c];
    }
    spec.turnSamples = turn_samples > 0 ? turn_samples : 64;
    const step::ExactPiece surface = step::exactSurface(spec);
    if (!surface.error.empty()) {
        g_step_last_message = surface.error;
        return nullptr;
    }
    const step::ExactPiece piece = field
        ? step::clipToRegion(surface, Tree(field), {}, cell > 0 ? cell : 1.0)
        : surface;
    g_step_last_message = "exact region: " + std::to_string(piece.tris.size()) + " triangles";
    auto out = new libfive_mesh;
    out->vert_count = uint32_t(piece.verts.size());
    out->tri_count = uint32_t(piece.tris.size());
    out->verts = new libfive_vec3[piece.verts.size()];
    out->tris = new libfive_tri[piece.tris.size()];
    for (size_t i = 0; i < piece.verts.size(); i++) {
        out->verts[i] = {piece.verts[i].x(), piece.verts[i].y(), piece.verts[i].z()};
    }
    for (size_t i = 0; i < piece.tris.size(); i++) {
        out->tris[i] = {uint32_t(piece.tris[i].x()), uint32_t(piece.tris[i].y()), uint32_t(piece.tris[i].z())};
    }
    return out;
}

libfive_tree libfive_tree_remap(libfive_tree p, libfive_tree x, libfive_tree y, libfive_tree z)
{
    return Tree(p).remap(Tree(x), Tree(y), Tree(z)).release();
}

libfive_tree libfive_tree_optimized(libfive_tree t) {
    return Tree(t).optimized().release();
}

float libfive_tree_eval_f(libfive_tree t, libfive_vec3 p)
{
    ArrayEvaluator e((Tree(t)));
    return e.value({p.x, p.y, p.z});
}

namespace {
// Runs fn(first, last) over [0, n) in parallel chunks
template <typename F>
void parallelChunks(size_t n, size_t minChunk, F fn)
{
    const size_t hw = std::max(1u, std::thread::hardware_concurrency());
    const size_t threads = std::min(hw, std::max<size_t>(1, n / minChunk));
    if (threads <= 1)
    {
        fn(size_t(0), n);
        return;
    }
    std::vector<std::thread> pool;
    const size_t per = (n + threads - 1) / threads;
    for (size_t t = 0; t < threads; ++t)
    {
        const size_t a = t * per, b = std::min(n, a + per);
        if (a >= b) break;
        pool.emplace_back([&fn, a, b]() { fn(a, b); });
    }
    for (auto& th : pool) th.join();
}
}   // anonymous namespace

bool libfive_tree_grid_stats(libfive_tree t, const float* lower3, const float* upper3,
                             int nx, int ny, int nz, double* out9)
{
    if (!t || !lower3 || !upper3 || !out9 || nx < 1 || ny < 1 || nz < 1) return false;
    const Tree tree(t);
    const size_t total = size_t(nx) * ny * nz;
    const float h[3] = {(upper3[0] - lower3[0]) / nx, (upper3[1] - lower3[1]) / ny,
                        (upper3[2] - lower3[2]) / nz};
    std::mutex lock;
    double acc[9] = {0, 0, 0, 0, std::numeric_limits<double>::infinity(),
                     -std::numeric_limits<double>::infinity(), 0, 0, 0};
    parallelChunks(total, 1 << 16, [&](size_t first, size_t last) {
        ArrayEvaluator e(tree);
        const size_t N = ArrayEvaluator::N;
        double a[9] = {0, 0, 0, 0, std::numeric_limits<double>::infinity(),
                       -std::numeric_limits<double>::infinity(), 0, 0, 0};
        Eigen::Vector3f pts[LIBFIVE_EVAL_ARRAY_SIZE];
        for (size_t start = first; start < last; start += N)
        {
            const size_t count = std::min(N, last - start);
            for (size_t k = 0; k < count; ++k)
            {
                const size_t idx = start + k;
                const size_t i = idx % nx, j = (idx / nx) % ny, kk = idx / (size_t(nx) * ny);
                pts[k] = Eigen::Vector3f(lower3[0] + (i + 0.5f) * h[0], lower3[1] + (j + 0.5f) * h[1],
                                         lower3[2] + (kk + 0.5f) * h[2]);
                e.set(pts[k], k);
            }
            const auto vs = e.values(count);
            for (size_t k = 0; k < count; ++k)
            {
                const float v = vs(k);
                if (v < a[4]) a[4] = v;
                if (v > a[5]) a[5] = v;
                if (v < 0)
                {
                    a[0] += 1;
                    for (int c = 0; c < 3; ++c)
                    {
                        a[1 + c] += pts[k][c];
                        a[6 + c] += double(pts[k][c]) * pts[k][c];
                    }
                }
            }
        }
        std::lock_guard<std::mutex> g(lock);
        for (int c = 0; c < 4; ++c) acc[c] += a[c];
        for (int c = 6; c < 9; ++c) acc[c] += a[c];
        acc[4] = std::min(acc[4], a[4]);
        acc[5] = std::max(acc[5], a[5]);
    });
    for (int c = 0; c < 9; ++c) out9[c] = acc[c];
    return true;
}

libfive_interval libfive_tree_eval_r(libfive_tree t, libfive_region3 r)
{
    IntervalEvaluator e((Tree(t)));
    auto i = e.eval({r.X.lower, r.Y.lower, r.Z.lower},
                    {r.X.upper, r.Y.upper, r.Z.upper});
    return {i.lower(), i.upper()};
}

// (`vars`: the values of the tree's variables -- a shape made with var()s is not where its numbers are 0)
static bool bounds_impl(const Tree& root, const std::map<Tree::Id, float>& vars, libfive_region3 search,
                        int max_evals, float max_seconds, libfive_region3* out, int* open)
{
    using V = Eigen::Vector3f;
    IntervalEvaluator e(root, vars);
    int evals = 0;
    const auto start = std::chrono::steady_clock::now();
    auto outOfBudget = [&]() {
        if (evals > max_evals) return true;
        if ((evals & 255) == 0 && max_seconds > 0)
        {
            const std::chrono::duration<float> dt = std::chrono::steady_clock::now() - start;
            if (dt.count() > max_seconds) return true;
        }
        return false;
    };

    struct Box { V lo, hi; };
    const V lo0(search.X.lower, search.Y.lower, search.Z.lower);
    const V hi0(search.X.upper, search.Y.upper, search.Z.upper);

    // Points certainly inside the shape ("witnesses"), per side the most
    // extreme one found so far
    float witness[6];
    bool have_witness = false;
    auto sample = [&](const V& p) {
        ++evals;
        if (!(e.eval(p, p).upper() <= 0)) return;
        if (!have_witness)
        {
            for (int s=0; s < 6; ++s) witness[s] = p[s / 2];
            have_witness = true;
        }
        for (int a=0; a < 3; ++a)
        {
            witness[2 * a] = std::min(witness[2 * a], p[a]);
            witness[2 * a + 1] = std::max(witness[2 * a + 1], p[a]);
        }
    };

    // For one side: best-first search over boxes that may contain interior,
    // most extreme first.  Done when the most extreme remaining box can't
    // beat the best witness by more than `tol` (or, without witnesses, when
    // that box is smaller than `tol`).  The answer is conservative.
    auto side = [&](const V& lo, const V& hi, int s, float tol, float& result) {
        const int a = s / 2;
        const bool upper = s % 2;
        auto key = [&](const Box& b) { return upper ? -b.hi[a] : b.lo[a]; };
        auto later = [&](const Box& x, const Box& y) { return key(x) > key(y); };
        std::priority_queue<Box, std::vector<Box>, decltype(later)> q(later);
        q.push({lo, hi});
        while (!q.empty())
        {
            if (outOfBudget()) return false;
            const Box b = q.top();
            q.pop();
            const float k = key(b);
            if (have_witness)
            {
                const float w = upper ? -witness[s] : witness[s];
                if (k >= w - tol)
                {
                    result = upper ? -k : k;
                    return true;
                }
            }
            ++evals;
            if (e.eval(b.lo, b.hi).lower() > 0) continue;
            const V size = b.hi - b.lo;
            int d;
            if (size.maxCoeff(&d) <= tol)
            {
                result = upper ? b.hi[a] : b.lo[a];
                return true;
            }
            sample((b.lo + b.hi) / 2);
            // Also try the middle of the box's extreme face, which moves
            // the witness towards the side being searched
            V f = (b.lo + b.hi) / 2;
            f[a] = upper ? b.hi[a] - 0.25f * size[a] : b.lo[a] + 0.25f * size[a];
            sample(f);
            // Halve along the searched axis while that keeps the box from
            // becoming a thin slab (whose intervals would be loose), else
            // along its longest side
            if (size[a] >= 0.125f * size[d]) d = a;
            const float mid = 0.5f * (b.lo[d] + b.hi[d]);
            Box c1 = b, c2 = b;
            c1.hi[d] = mid;
            c2.lo[d] = mid;
            q.push(c1);
            q.push(c2);
        }
        return false;
    };

    auto pass = [&](const V& lo, const V& hi, float tol, float result[6]) {
        ++evals;
        if (e.eval(lo, hi).lower() > 0) return false;
        for (int s=0; s < 6; ++s)
        {
            if (!side(lo, hi, s, tol, result[s])) return false;
        }
        return true;
    };

    float fine[6];
    const float tol0 = 1e-3f * (hi0 - lo0).maxCoeff();
    if (!pass(lo0, hi0, tol0, fine)) return false;

    // Refine inside the box found so far, relative to the shape's own size,
    // until that stops tightening it
    float tol = tol0;
    for (int i=0; i < 8; ++i)
    {
        V lo1(fine[0], fine[2], fine[4]), hi1(fine[1], fine[3], fine[5]);
        lo1 = (lo1.array() - tol).matrix().cwiseMax(lo0);
        hi1 = (hi1.array() + tol).matrix().cwiseMin(hi0);
        // (floored by float precision at the shape's coordinates)
        const float mag = std::max({1.0f, lo1.cwiseAbs().maxCoeff(), hi1.cwiseAbs().maxCoeff()});
        const float next = std::max(1e-3f * (hi1 - lo1).maxCoeff(), 1e-6f * mag);
        if (next > 0.5f * tol) break;
        float refined[6];
        if (!pass(lo1, hi1, next, refined)) break;      // keep the coarser box
        std::copy(refined, refined + 6, fine);
        tol = next;
    }

    if (out)
    {
        out->X = {fine[0], fine[1]};
        out->Y = {fine[2], fine[3]};
        out->Z = {fine[4], fine[5]};
    }
    if (open)
    {
        *open = 0;
        for (int s=0; s < 6; ++s)
        {
            const int a = s / 2;
            const float edge = (s % 2) ? hi0[a] : lo0[a];
            if (std::abs(fine[s] - edge) <= tol0)
            {
                *open |= 1 << s;
            }
        }
    }
    return true;
}

bool libfive_tree_bounds(libfive_tree t, libfive_region3 search, int max_evals,
                         float max_seconds, libfive_region3* out, int* open)
{
    return bounds_impl(Tree(t), {}, search, max_evals, max_seconds, out, open);
}

bool libfive_tree_bounds_vars(libfive_tree t, libfive_region3 search, int max_evals, float max_seconds,
                              libfive_region3* out, int* open,
                              const libfive_tree* vars, const float* values, int count)
{
    std::map<Tree::Id, float> m;
    for (int i = 0; i < count; ++i) m[static_cast<Tree::Id>(vars[i])] = values[i];
    return bounds_impl(Tree(t), m, search, max_evals, max_seconds, out, open);
}

float libfive_tree_interval_lower(libfive_tree t, libfive_region3 box,
                                  const libfive_tree* vars, const float* values, int count)
{
    std::map<Tree::Id, float> m;
    for (int i = 0; i < count; ++i) m[static_cast<Tree::Id>(vars[i])] = values[i];
    IntervalEvaluator e(Tree(t), m);
    return e.eval({box.X.lower, box.Y.lower, box.Z.lower},
                  {box.X.upper, box.Y.upper, box.Z.upper}).lower();
}

libfive_tree libfive_tree_copy(libfive_tree t)
{
    return Tree(t).release();
}

struct libfive_fea_
{
    std::unique_ptr<fea::StaticProblem> problem;
    std::string message;
    bool prepared = false, solved = false;
};

libfive_fea* libfive_fea_new(libfive_tree shape, libfive_region3 r,
                             float element_size, float E, float nu)
{
    auto f = new libfive_fea_;
    f->problem.reset(new fea::StaticProblem(
        Tree(shape), Eigen::Vector3d(r.X.lower, r.Y.lower, r.Z.lower),
        Eigen::Vector3d(r.X.upper, r.Y.upper, r.Z.upper), element_size, E, nu));
    return f;
}

void libfive_fea_set_element(libfive_fea* f, int element)
{
    f->problem->setElement(element == 1 ? fea::Element::Hex
                           : element == 2 ? fea::Element::HexBasic : fea::Element::Tet);
    f->prepared = false;
}

void libfive_fea_add_support(libfive_fea* f, libfive_tree region, int x, int y, int z)
{
    f->problem->addSupport(Tree(region), x != 0, y != 0, z != 0);
}

void libfive_fea_add_force_case(libfive_fea* f, libfive_tree region, float fx, float fy, float fz,
                                int load_case)
{
    f->problem->addForce(Tree(region), Eigen::Vector3d(fx, fy, fz), load_case);
}

void libfive_fea_add_force(libfive_fea* f, libfive_tree region, float fx, float fy, float fz)
{
    f->problem->addForce(Tree(region), Eigen::Vector3d(fx, fy, fz));
}

void libfive_fea_set_gravity(libfive_fea* f, float gx, float gy, float gz, float density)
{
    f->problem->setGravity(Eigen::Vector3d(gx, gy, gz), density);
}

void libfive_fea_set_thermal(libfive_fea* f, libfive_tree temperature, float alpha, float reference)
{
    f->problem->setThermal(Tree(temperature), alpha, reference);
}

int libfive_fea_modal(libfive_fea* f, int count, float density, int max_iterations, float tolerance)
{
    f->message.clear();
    return f->problem->modal(count, density, max_iterations, tolerance, f->message) ? 1 : 0;
}

int libfive_fea_mode_count(libfive_fea* f)
{
    return int(f->problem->frequencies().size());
}

double libfive_fea_mode_frequency(libfive_fea* f, int i)
{
    const auto& fr = f->problem->frequencies();
    return (i >= 0 && size_t(i) < fr.size()) ? fr[size_t(i)] : 0.0;
}

libfive_tree libfive_fea_mode_field(libfive_fea* f, int i, int field)
{
    auto r = f->problem->mode(i);
    if (!r || field < 1 || field > 4) return nullptr;
    return fea::fieldTree(r, field).release();
}

float libfive_fea_mode_field_min(libfive_fea* f, int i, int field)
{
    auto r = f->problem->mode(i);
    return (r && field >= 0 && field < fea::Result::FIELD_COUNT) ? r->minValue[field] : 0.0f;
}

float libfive_fea_mode_field_max(libfive_fea* f, int i, int field)
{
    auto r = f->problem->mode(i);
    return (r && field >= 0 && field < fea::Result::FIELD_COUNT) ? r->maxValue[field] : 0.0f;
}

void libfive_run_begin(int steps)
{
    libfive::run_progress::beginScript(steps);
}

void libfive_run_step(int index, const char* label)
{
    libfive::run_progress::setStep(index, label ? label : "");
}

void libfive_run_end(void)
{
    libfive::run_progress::endScript();
}

void libfive_run_task_begin(const char* name)
{
    libfive::run_progress::pushTask(name ? name : "");
}

void libfive_run_task_set(double fraction, const char* detail)
{
    libfive::run_progress::setTask(fraction, detail ? detail : "");
}

void libfive_run_task_span(double a, double b)
{
    libfive::run_progress::spanTask(a, b);
}

void libfive_run_task_end(void)
{
    libfive::run_progress::popTask();
}

int libfive_fea_prepare(libfive_fea* f)
{
    f->message.clear();
    f->prepared = f->problem->prepare(f->message);
    return f->prepared ? 1 : 0;
}

uint64_t libfive_fea_hash(libfive_fea* f)
{
    return f->prepared ? f->problem->hash() : 0;
}

int libfive_fea_solve(libfive_fea* f, int max_iterations, float tolerance)
{
    f->message.clear();
    f->solved = f->problem->solve(max_iterations, tolerance, f->message);
    if (f->solved)
    {
        const auto r = f->problem->result();
        std::ostringstream msg;
        msg << r->elements << " elements, " << r->dofs << " unknowns, " << r->iterations
            << " iterations, " << r->seconds << " s";
        f->message = msg.str();
    }
    return f->solved ? 1 : 0;
}

const char* libfive_fea_message(libfive_fea* f)
{
    return f->message.c_str();
}

libfive_tree libfive_fea_field(libfive_fea* f, int field)
{
    if (!f->solved) return nullptr;
    Tree t = fea::fieldTree(f->problem->result(), field);
    if (!t.is_valid()) return nullptr;
    return t.release();
}

float libfive_fea_field_min(libfive_fea* f, int field)
{
    if (!f->solved || field < 0 || field >= fea::Result::FIELD_COUNT) return 0;
    return f->problem->result()->minValue[field];
}

float libfive_fea_field_max(libfive_fea* f, int field)
{
    if (!f->solved || field < 0 || field >= fea::Result::FIELD_COUNT) return 0;
    return f->problem->result()->maxValue[field];
}

int libfive_fea_grid(libfive_fea* f, double* lower3, double* h, int* dims3)
{
    if (!f->prepared) return 0;
    const auto& lo = f->problem->gridLower();
    for (int i = 0; i < 3; i++) lower3[i] = lo[i];
    *h = f->problem->elementSize();
    dims3[0] = f->problem->ex();
    dims3[1] = f->problem->ey();
    dims3[2] = f->problem->ez();
    return 1;
}

int libfive_fea_element(libfive_fea* f)
{
    switch (f->problem->element())
    {
        case fea::Element::Hex: return 1;
        case fea::Element::HexBasic: return 2;
        default: return 0;
    }
}

int libfive_fea_elements_per_cell(libfive_fea* f)
{
    return f->problem->elementsPerCell();
}

int64_t libfive_fea_element_values(libfive_fea* f, int field, float* out, int64_t capacity)
{
    if (!f->solved) return 0;
    std::vector<float> v;
    if (!f->problem->elementValues(field, v)) return 0;
    if (out)
    {
        const size_t n = std::min(v.size(), size_t(std::max<int64_t>(0, capacity)));
        std::copy(v.begin(), v.begin() + n, out);
    }
    return int64_t(v.size());
}

int libfive_fea_element_range(libfive_fea* f, int field, float* lo, float* hi)
{
    if (!f->solved) return 0;
    std::vector<float> v;
    if (!f->problem->elementValues(field, v) || v.empty()) return 0;
    const auto mm = std::minmax_element(v.begin(), v.end());
    *lo = *mm.first;
    *hi = *mm.second;
    return 1;
}

void libfive_fea_elements(libfive_fea* f, float* out)
{
    if (!f->prepared) return;
    const auto& fr = f->problem->elementFractions();
    std::copy(fr.begin(), fr.end(), out);
}

double libfive_fea_stat(libfive_fea* f, int which)
{
    if (which >= 16 && which <= 18)
    {
        const int n[3] = {f->problem->ex(), f->problem->ey(), f->problem->ez()};
        return which <= 18 ? n[which - 16] : 0;
    }
    auto r = f->problem->result();
    if (!r) return 0;
    switch (which)
    {
        case 0: return r->elements;
        case 1: return r->nodes;
        case 2: return r->dofs;
        case 3: return r->iterations;
        case 4: return r->residual;
        case 5: return r->seconds;
        case 6: return r->volume;
        case 7: return r->compliance;
        case 8: case 9: case 10: return r->reaction[which - 8];
        case 11: case 12: case 13: return r->totalLoad[which - 11];
        case 14: return r->fixedNodes;
        case 15: return r->loadedNodes;
        case 19: return r->looseElements;
        default: return 0;
    }
}

int libfive_fea_optimize(libfive_fea* f, float volume_fraction, float penalty,
                         float filter_radius, int iterations, float move,
                         const libfive_tree* keep, int keep_count,
                         const libfive_tree* avoid, int avoid_count,
                         int solver_iterations, float tolerance, int extrude)
{
    fea::StaticProblem::TopOpt s;
    s.extrude = extrude;
    s.volumeFraction = volume_fraction;
    s.penalty = penalty;
    s.filterRadius = filter_radius;
    s.iterations = iterations;
    s.move = move;
    s.solverIterations = solver_iterations;
    s.tolerance = tolerance;
    for (int i = 0; i < keep_count; ++i) s.keep.push_back(Tree(keep[i]));
    for (int i = 0; i < avoid_count; ++i) s.avoid.push_back(Tree(avoid[i]));
    f->message.clear();
    const bool ok = f->problem->optimize(s, f->message);
    return ok ? 1 : 0;
}

libfive_tree libfive_fea_density(libfive_fea* f)
{
    auto r = f->problem->densityResult();
    if (!r) return nullptr;
    return fea::fieldTree(r, 0).release();
}

int libfive_fea_history(libfive_fea* f, double* out, int max)
{
    const auto& h = f->problem->complianceHistory();
    for (int i = 0; i < std::min(max, int(h.size())); ++i) out[i] = h[size_t(i)];
    return int(h.size());
}

struct libfive_tetmesh_
{
    fea::TetMesh mesh;
};

static std::string g_tetmesh_message;

libfive_tetmesh* libfive_tetmesh_new(libfive_tree shape, libfive_region3 r, float h)
{
    auto m = new libfive_tetmesh_;
    std::string error;
    if (!fea::meshShape(Tree(shape), Eigen::Vector3d(r.X.lower, r.Y.lower, r.Z.lower),
                        Eigen::Vector3d(r.X.upper, r.Y.upper, r.Z.upper), h, m->mesh, error))
    {
        g_tetmesh_message = error;
        delete m;
        return nullptr;
    }
    g_tetmesh_message.clear();
    return m;
}

const char* libfive_tetmesh_last_message(void)
{
    return g_tetmesh_message.c_str();
}

void libfive_tetmesh_counts(libfive_tetmesh* m, int64_t* out)
{
    out[0] = int64_t(m->mesh.pos.size());
    out[1] = int64_t(m->mesh.tets.size());
    out[2] = int64_t(m->mesh.faces.size());
}

void libfive_tetmesh_vertices(libfive_tetmesh* m, double* out)
{
    for (size_t i = 0; i < m->mesh.pos.size(); ++i)
        for (int a = 0; a < 3; ++a) out[3 * i + size_t(a)] = m->mesh.pos[i][a];
}

void libfive_tetmesh_tets(libfive_tetmesh* m, int32_t* out)
{
    for (size_t i = 0; i < m->mesh.tets.size(); ++i)
        for (size_t a = 0; a < 4; ++a) out[4 * i + a] = m->mesh.tets[i][a];
}

void libfive_tetmesh_faces(libfive_tetmesh* m, int32_t* out)
{
    for (size_t i = 0; i < m->mesh.faces.size(); ++i)
        for (size_t a = 0; a < 3; ++a) out[3 * i + a] = m->mesh.faces[i][a];
}

void libfive_tetmesh_quality(libfive_tetmesh* m, double* out)
{
    const auto q = fea::meshQuality(m->mesh);
    out[0] = q.volume;
    out[1] = q.minDihedral;
    out[2] = q.maxDihedral;
    out[3] = q.meanMinDihedral;
    out[4] = q.minVolume;
    out[5] = double(q.inverted);
    out[6] = double(q.badFaces);
    out[7] = double(q.openEdges);
    out[8] = q.boundarySurfaceArea;
    out[9] = double(m->mesh.degenerate);
    out[10] = double(q.folded);
    out[11] = double(m->mesh.featureVertices);
    out[12] = double(m->mesh.featuresReverted);
}

void libfive_tetmesh_delete(libfive_tetmesh* m)
{
    delete m;
}

struct libfive_tetfea_
{
    std::unique_ptr<fea::TetProblem> problem;
    std::string message;
    bool prepared = false, solved = false;
};

libfive_tetfea* libfive_tetfea_new(libfive_tree shape, libfive_region3 r, float h, float E, float nu)
{
    auto f = new libfive_tetfea_;
    f->problem.reset(new fea::TetProblem(
        Tree(shape), Eigen::Vector3d(r.X.lower, r.Y.lower, r.Z.lower),
        Eigen::Vector3d(r.X.upper, r.Y.upper, r.Z.upper), h, E, nu));
    return f;
}

void libfive_tetfea_add_support(libfive_tetfea* f, libfive_tree region, int x, int y, int z)
{
    f->problem->addSupport(Tree(region), x != 0, y != 0, z != 0);
    f->prepared = f->solved = false;
}

void libfive_tetfea_add_force(libfive_tetfea* f, libfive_tree region, float fx, float fy, float fz)
{
    f->problem->addForce(Tree(region), Eigen::Vector3d(fx, fy, fz));
    f->prepared = f->solved = false;
}

void libfive_tetfea_add_force_case(libfive_tetfea* f, libfive_tree region, float fx, float fy, float fz,
                                   int load_case)
{
    f->problem->addForce(Tree(region), Eigen::Vector3d(fx, fy, fz), load_case);
    f->prepared = f->solved = false;
}

void libfive_tetfea_set_gravity(libfive_tetfea* f, float gx, float gy, float gz, float density)
{
    f->problem->setGravity(Eigen::Vector3d(gx, gy, gz), density);
    f->prepared = f->solved = false;
}

void libfive_tetfea_set_thermal(libfive_tetfea* f, libfive_tree temperature, float alpha, float reference)
{
    f->problem->setThermal(Tree(temperature), alpha, reference);
    f->prepared = f->solved = false;
}

int libfive_tetfea_prepare(libfive_tetfea* f)
{
    f->message.clear();
    f->prepared = f->problem->prepare(f->message);
    return f->prepared ? 1 : 0;
}

uint64_t libfive_tetfea_hash(libfive_tetfea* f)
{
    return f->prepared ? f->problem->hash() : 0;
}

int libfive_tetfea_solve(libfive_tetfea* f, int max_iterations, float tolerance)
{
    f->message.clear();
    f->solved = f->problem->solve(max_iterations, tolerance, f->message);
    return f->solved ? 1 : 0;
}

const char* libfive_tetfea_message(libfive_tetfea* f)
{
    return f->message.c_str();
}

libfive_tree libfive_tetfea_field(libfive_tetfea* f, int field)
{
    if (!f->solved) return nullptr;
    Tree t = fea::meshFieldTree(f->problem->result(), field);
    if (!t.is_valid()) return nullptr;
    return t.release();
}

float libfive_tetfea_field_min(libfive_tetfea* f, int field)
{
    if (!f->solved || field < 0 || field >= fea::Result::FIELD_COUNT) return 0;
    return f->problem->result()->minValue[field];
}

float libfive_tetfea_field_max(libfive_tetfea* f, int field)
{
    if (!f->solved || field < 0 || field >= fea::Result::FIELD_COUNT) return 0;
    return f->problem->result()->maxValue[field];
}

double libfive_tetfea_stat(libfive_tetfea* f, int which)
{
    auto r = f->problem->result();
    if (!r || !f->solved) return 0;
    switch (which)
    {
        case 0: return r->elements;
        case 1: return r->nodes;
        case 2: return r->dofs;
        case 3: return r->iterations;
        case 4: return r->residual;
        case 5: return r->seconds;
        case 6: return r->volume;
        case 7: return r->compliance;
        case 8: case 9: case 10: return r->reaction[which - 8];
        case 11: case 12: case 13: return r->totalLoad[which - 11];
        case 14: return r->fixedNodes;
        case 15: return r->loadedNodes;
        case 19: return r->looseElements;
        default: return 0;
    }
}

void libfive_tetfea_counts(libfive_tetfea* f, int64_t* out)
{
    auto m = f->problem->mesh();
    out[0] = m ? int64_t(m->pos.size()) : 0;
    out[1] = m ? int64_t(m->tets.size()) : 0;
    out[2] = m ? int64_t(m->faces.size()) : 0;
}

void libfive_tetfea_mesh(libfive_tetfea* f, float* vertices, int32_t* tets, int32_t* faces, int32_t* face_tet)
{
    auto m = f->problem->mesh();
    if (!m) return;
    for (size_t i = 0; i < m->pos.size(); ++i)
        for (int a = 0; a < 3; ++a) vertices[3 * i + size_t(a)] = float(m->pos[i][a]);
    for (size_t i = 0; i < m->tets.size(); ++i)
        for (size_t a = 0; a < 4; ++a) tets[4 * i + a] = m->tets[i][a];
    for (size_t i = 0; i < m->faces.size(); ++i)
    {
        for (size_t a = 0; a < 3; ++a) faces[3 * i + a] = m->faces[i][a];
        face_tet[i] = m->faceTet[i];
    }
}

void libfive_tetfea_node_displacements(libfive_tetfea* f, float* out)
{
    auto r = f->problem->result();
    if (!r || !f->solved) return;
    const size_t n = r->mesh->pos.size();
    for (size_t i = 0; i < n; ++i)
    {
        out[3 * i] = r->fields[fea::Result::UX][i];
        out[3 * i + 1] = r->fields[fea::Result::UY][i];
        out[3 * i + 2] = r->fields[fea::Result::UZ][i];
    }
}

int64_t libfive_tetfea_element_values(libfive_tetfea* f, int field, float* out, int64_t capacity)
{
    auto r = f->problem->result();
    if (!r || !f->solved) return 0;
    std::vector<float> v;
    r->elementValues(field, v);
    if (out)
    {
        const size_t n = std::min(v.size(), size_t(std::max<int64_t>(0, capacity)));
        std::copy(v.begin(), v.begin() + std::ptrdiff_t(n), out);
    }
    return int64_t(v.size());
}

int libfive_tetfea_element_range(libfive_tetfea* f, int field, float* lo, float* hi)
{
    auto r = f->problem->result();
    if (!r || !f->solved) return 0;
    std::vector<float> v;
    r->elementValues(field, v);
    if (v.empty()) return 0;
    const auto mm = std::minmax_element(v.begin(), v.end());
    *lo = *mm.first;
    *hi = *mm.second;
    return 1;
}

int libfive_tetfea_optimize(libfive_tetfea* f, float volume_fraction, float penalty, float filter_radius,
                            int iterations, float move, const libfive_tree* keep, int keep_count,
                            const libfive_tree* avoid, int avoid_count, int solver_iterations, float tolerance,
                            int extrude)
{
    fea::TetProblem::TopOpt s;
    s.extrude = extrude;
    s.volumeFraction = volume_fraction;
    s.penalty = penalty;
    s.filterRadius = filter_radius;
    s.iterations = iterations;
    s.move = move;
    s.solverIterations = solver_iterations;
    s.tolerance = tolerance;
    for (int i = 0; i < keep_count; ++i) s.keep.push_back(Tree(keep[i]));
    for (int i = 0; i < avoid_count; ++i) s.avoid.push_back(Tree(avoid[i]));
    f->message.clear();
    return f->problem->optimize(s, f->message) ? 1 : 0;
}

libfive_tree libfive_tetfea_density(libfive_tetfea* f)
{
    auto r = f->problem->densityResult();
    if (!r) return nullptr;
    Tree t = fea::meshFieldTree(r, 0);
    if (!t.is_valid()) return nullptr;
    return t.release();
}

int libfive_tetfea_history(libfive_tetfea* f, double* out, int max)
{
    const auto& h = f->problem->complianceHistory();
    const int n = std::min<int>(max, int(h.size()));
    for (int i = 0; i < n; ++i) out[i] = h[size_t(i)];
    return n;
}

int libfive_tetfea_modal(libfive_tetfea* f, int count, float density, int max_iterations, float tolerance)
{
    f->message.clear();
    return f->problem->modal(count, density, max_iterations, tolerance, f->message) ? 1 : 0;
}

int libfive_tetfea_mode_count(libfive_tetfea* f)
{
    return int(f->problem->frequencies().size());
}

double libfive_tetfea_mode_frequency(libfive_tetfea* f, int i)
{
    const auto& fr = f->problem->frequencies();
    return i >= 0 && size_t(i) < fr.size() ? fr[size_t(i)] : 0.0;
}

libfive_tree libfive_tetfea_mode_field(libfive_tetfea* f, int i, int field)
{
    auto r = f->problem->mode(i);
    if (!r) return nullptr;
    Tree t = fea::meshFieldTree(r, field);
    if (!t.is_valid()) return nullptr;
    return t.release();
}

float libfive_tetfea_mode_field_min(libfive_tetfea* f, int i, int field)
{
    auto r = f->problem->mode(i);
    if (!r || field < 0 || field >= fea::Result::FIELD_COUNT) return 0;
    return r->minValue[field];
}

float libfive_tetfea_mode_field_max(libfive_tetfea* f, int i, int field)
{
    auto r = f->problem->mode(i);
    if (!r || field < 0 || field >= fea::Result::FIELD_COUNT) return 0;
    return r->maxValue[field];
}

void libfive_tetfea_delete(libfive_tetfea* f)
{
    delete f;
}

struct libfive_tetthermal_
{
    std::unique_ptr<fea::TetThermalProblem> problem;
    std::string message;
    bool prepared = false, solved = false;
};

libfive_tetthermal* libfive_tetthermal_new(libfive_tree shape, libfive_region3 r, float h, float conductivity)
{
    auto f = new libfive_tetthermal_;
    f->problem.reset(new fea::TetThermalProblem(
        Tree(shape), Eigen::Vector3d(r.X.lower, r.Y.lower, r.Z.lower),
        Eigen::Vector3d(r.X.upper, r.Y.upper, r.Z.upper), h, conductivity));
    return f;
}

void libfive_tetthermal_add_temperature(libfive_tetthermal* f, libfive_tree region, float value)
{
    f->problem->addTemperature(Tree(region), value);
    f->prepared = f->solved = false;
}

void libfive_tetthermal_add_heat(libfive_tetthermal* f, libfive_tree region, float watts)
{
    f->problem->addHeat(Tree(region), watts);
    f->prepared = f->solved = false;
}

void libfive_tetthermal_add_generation(libfive_tetthermal* f, libfive_tree region, float watts)
{
    f->problem->addGeneration(Tree(region), watts);
    f->prepared = f->solved = false;
}

void libfive_tetthermal_add_convection(libfive_tetthermal* f, libfive_tree region, float coefficient, float ambient)
{
    f->problem->addConvection(Tree(region), coefficient, ambient);
    f->prepared = f->solved = false;
}

int libfive_tetthermal_prepare(libfive_tetthermal* f)
{
    f->message.clear();
    f->prepared = f->problem->prepare(f->message);
    return f->prepared ? 1 : 0;
}

uint64_t libfive_tetthermal_hash(libfive_tetthermal* f)
{
    return f->prepared ? f->problem->hash() : 0;
}

int libfive_tetthermal_solve(libfive_tetthermal* f, int max_iterations, float tolerance)
{
    f->message.clear();
    f->solved = f->problem->solve(max_iterations, tolerance, f->message);
    return f->solved ? 1 : 0;
}

const char* libfive_tetthermal_message(libfive_tetthermal* f)
{
    return f->message.c_str();
}

libfive_tree libfive_tetthermal_field(libfive_tetthermal* f, int field)
{
    if (!f->solved) return nullptr;
    Tree t = fea::meshFieldTree(f->problem->result(), field);
    if (!t.is_valid()) return nullptr;
    return t.release();
}

float libfive_tetthermal_field_min(libfive_tetthermal* f, int field)
{
    if (!f->solved || field < 0 || field >= fea::Result::FIELD_COUNT) return 0;
    return f->problem->result()->minValue[field];
}

float libfive_tetthermal_field_max(libfive_tetthermal* f, int field)
{
    if (!f->solved || field < 0 || field >= fea::Result::FIELD_COUNT) return 0;
    return f->problem->result()->maxValue[field];
}

double libfive_tetthermal_stat(libfive_tetthermal* f, int which)
{
    auto r = f->problem->result();
    if (!r || !f->solved) return 0;
    switch (which)
    {
        case 0: return r->elements;
        case 1: return r->nodes;
        case 2: return r->iterations;
        case 3: return r->residual;
        case 4: return r->seconds;
        case 5: return r->heatIn;
        case 6: return r->heatOutFixed;
        case 7: return r->heatOutConvection;
        default: return 0;
    }
}

void libfive_tetthermal_delete(libfive_tetthermal* f)
{
    delete f;
}

void libfive_fea_delete(libfive_fea* f)
{
    delete f;
}

struct libfive_thermal_
{
    std::unique_ptr<fea::ThermalProblem> problem;
    std::string message;
    bool solved = false;
};

libfive_thermal* libfive_thermal_new(libfive_tree shape, libfive_region3 r,
                                     float element_size, float conductivity)
{
    auto t = new libfive_thermal_;
    t->problem.reset(new fea::ThermalProblem(
        Tree(shape), Eigen::Vector3d(r.X.lower, r.Y.lower, r.Z.lower),
        Eigen::Vector3d(r.X.upper, r.Y.upper, r.Z.upper), element_size, conductivity));
    return t;
}

void libfive_thermal_set_element(libfive_thermal* t, int element)
{
    t->problem->setElement(element == 0 ? fea::Element::Tet : fea::Element::Hex);
}

void libfive_thermal_add_temperature(libfive_thermal* t, libfive_tree region, float value)
{
    t->problem->addTemperature(Tree(region), value);
}

void libfive_thermal_add_heat(libfive_thermal* t, libfive_tree region, float power)
{
    t->problem->addHeat(Tree(region), power);
}

void libfive_thermal_add_convection(libfive_thermal* t, libfive_tree region, float coefficient,
                                    float ambient)
{
    t->problem->addConvection(Tree(region), coefficient, ambient);
}

void libfive_thermal_add_generation(libfive_thermal* t, libfive_tree region, float power)
{
    t->problem->addGeneration(Tree(region), power);
}

int libfive_thermal_prepare(libfive_thermal* t)
{
    t->message.clear();
    return t->problem->prepare(t->message) ? 1 : 0;
}

uint64_t libfive_thermal_hash(libfive_thermal* t)
{
    return t->problem->hash();
}

int libfive_thermal_optimize(libfive_thermal* t, float volume_fraction, float penalty,
                             float filter_radius, int iterations, float move,
                             const libfive_tree* keep, int keep_count,
                             const libfive_tree* avoid, int avoid_count,
                             int solver_iterations, float tolerance, int extrude)
{
    fea::ThermalProblem::TopOpt s;
    s.extrude = extrude;
    s.volumeFraction = volume_fraction;
    s.penalty = penalty;
    s.filterRadius = filter_radius;
    s.iterations = iterations;
    s.move = move;
    s.solverIterations = solver_iterations;
    s.tolerance = tolerance;
    for (int i = 0; i < keep_count; ++i) s.keep.push_back(Tree(keep[i]));
    for (int i = 0; i < avoid_count; ++i) s.avoid.push_back(Tree(avoid[i]));
    t->message.clear();
    return t->problem->optimize(s, t->message) ? 1 : 0;
}

libfive_tree libfive_thermal_density(libfive_thermal* t)
{
    auto r = t->problem->densityResult();
    if (!r) return nullptr;
    return fea::fieldTree(r, 0).release();
}

int libfive_thermal_history(libfive_thermal* t, double* out, int max)
{
    const auto& h = t->problem->history();
    for (int i = 0; i < std::min(max, int(h.size())); ++i) out[i] = h[size_t(i)];
    return int(h.size());
}

int libfive_thermal_solve(libfive_thermal* t, int max_iterations, float tolerance)
{
    t->message.clear();
    t->solved = t->problem->solve(max_iterations, tolerance, t->message);
    return t->solved ? 1 : 0;
}

const char* libfive_thermal_message(libfive_thermal* t)
{
    return t->message.c_str();
}

libfive_tree libfive_thermal_field(libfive_thermal* t, int field)
{
    if (!t->solved || field < 0 || field > 4) return nullptr;
    Tree f = fea::fieldTree(t->problem->result(), field);
    if (!f.is_valid()) return nullptr;
    return f.release();
}

float libfive_thermal_field_min(libfive_thermal* t, int field)
{
    if (!t->solved || field < 0 || field > 4) return 0;
    return t->problem->result()->minValue[field];
}

float libfive_thermal_field_max(libfive_thermal* t, int field)
{
    if (!t->solved || field < 0 || field > 4) return 0;
    return t->problem->result()->maxValue[field];
}

double libfive_thermal_stat(libfive_thermal* t, int which)
{
    const auto& p = *t->problem;
    switch (which)
    {
        case 0: return p.elements;
        case 1: return p.nodes;
        case 2: return p.iterations;
        case 3: return p.residual;
        case 4: return p.seconds;
        case 5: return p.heatIn;
        case 6: return p.heatOutFixed;
        case 7: return p.heatOutConvection;
        default: return 0;
    }
}

void libfive_thermal_delete(libfive_thermal* t)
{
    delete t;
}

void libfive_tree_eval_points(libfive_tree t, const float* xyz, int n, float* out)
{
    if (!t || n <= 0) return;
    const Tree tree(t);
    parallelChunks(size_t(n), 4096, [&](size_t first, size_t last) {
        ArrayEvaluator e(tree);
        const size_t N = ArrayEvaluator::N;
        for (size_t start = first; start < last; start += N)
        {
            const size_t count = std::min(N, last - start);
            for (size_t k = 0; k < count; ++k)
            {
                const float* p = xyz + 3 * (start + k);
                e.set(Eigen::Vector3f(p[0], p[1], p[2]), k);
            }
            const auto vs = e.values(count);
            for (size_t k = 0; k < count; ++k) out[start + k] = vs(k);
        }
    });
}

namespace {
std::string lattice_error;

libfive_graph* toC(const libfive::lattice::Graph& g)
{
    auto out = new libfive_graph;
    out->node_count = int(g.nodes.size());
    out->beam_count = int(g.beams.size());
    out->nodes = new float[g.nodes.size() * 3 + 1];
    out->beams = new int[g.beams.size() * 2 + 1];
    for (size_t i = 0; i < g.nodes.size(); ++i)
        for (int a = 0; a < 3; ++a) out->nodes[3 * i + a] = float(g.nodes[i][a]);
    for (size_t i = 0; i < g.beams.size(); ++i)
    {
        out->beams[2 * i] = g.beams[i][0];
        out->beams[2 * i + 1] = g.beams[i][1];
    }
    return out;
}
}   // anonymous namespace

libfive_graph* libfive_lattice_volume_graph(libfive_tree body, libfive_region3 region,
                                            float spacing, int mode, int relax, unsigned seed)
{
    lattice_error.clear();
    try
    {
        const auto g = libfive::lattice::volumeGraph(
                Tree(body), Eigen::Vector3d(region.X.lower, region.Y.lower, region.Z.lower),
                Eigen::Vector3d(region.X.upper, region.Y.upper, region.Z.upper),
                spacing, mode, relax, seed, lattice_error);
        if (g.beams.empty())
        {
            if (lattice_error.empty()) lattice_error = "no beams";
            return nullptr;
        }
        return toC(g);
    }
    catch (const std::exception& e)
    {
        lattice_error = e.what();
        return nullptr;
    }
}

libfive_graph* libfive_lattice_surface_graph(libfive_tree body, libfive_region3 region,
                                             float spacing, int mode, unsigned seed)
{
    lattice_error.clear();
    try
    {
        const auto g = libfive::lattice::surfaceGraph(
                Tree(body), Eigen::Vector3d(region.X.lower, region.Y.lower, region.Z.lower),
                Eigen::Vector3d(region.X.upper, region.Y.upper, region.Z.upper),
                spacing, mode, seed, lattice_error);
        if (g.beams.empty())
        {
            if (lattice_error.empty()) lattice_error = "no beams";
            return nullptr;
        }
        return toC(g);
    }
    catch (const std::exception& e)
    {
        lattice_error = e.what();
        return nullptr;
    }
}

libfive_graph* libfive_lattice_points_graph(const float* points, int count, int mode)
{
    lattice_error.clear();
    std::vector<Eigen::Vector3d> pts;
    for (int i = 0; i < count; ++i)
        pts.emplace_back(points[3 * i], points[3 * i + 1], points[3 * i + 2]);
    if (pts.size() < 4)
    {
        lattice_error = "need at least 4 points";
        return nullptr;
    }
    return toC(libfive::lattice::pointsGraph(pts, mode));
}

void libfive_graph_delete(libfive_graph* g)
{
    if (!g) return;
    delete[] g->nodes;
    delete[] g->beams;
    delete g;
}

const char* libfive_lattice_last_error(void)
{
    return lattice_error.c_str();
}

libfive_tree libfive_beam_lattice(const float* nodes, int node_count, const int* beams,
                                  int beam_count, const float* radii, float blend)
{
    libfive::lattice::Graph g;
    std::vector<double> r;
    for (int i = 0; i < node_count; ++i)
    {
        g.nodes.emplace_back(nodes[3 * i], nodes[3 * i + 1], nodes[3 * i + 2]);
        r.push_back(radii[i]);
    }
    for (int i = 0; i < beam_count; ++i)
    {
        const int a = beams[2 * i], b = beams[2 * i + 1];
        if (a >= 0 && b >= 0 && a < node_count && b < node_count && a != b)
            g.beams.push_back({a, b});
    }
    return libfive::lattice::beamLattice(g, r, blend).release();
}

libfive_tree libfive_field_gradient(libfive_tree t, int mode)
{
    return libfive::fields::gradientField(Tree(t), mode).release();
}

libfive_tree libfive_field_thickness(libfive_tree t, float max_thickness)
{
    return libfive::fields::thicknessField(Tree(t), max_thickness).release();
}

libfive_tree libfive_field_points(const float* xyz, const float* values, int n, int k,
                                  float power)
{
    std::vector<Eigen::Vector3d> pts;
    std::vector<double> vals;
    for (int i = 0; i < n; ++i)
    {
        pts.emplace_back(xyz[3 * i], xyz[3 * i + 1], xyz[3 * i + 2]);
        vals.push_back(values[i]);
    }
    return libfive::fields::pointCloudField(pts, vals, k, power).release();
}

libfive_tree libfive_field_noise(float scale, int octaves, unsigned seed, float gain,
                                 float lacunarity)
{
    return libfive::fields::noiseField(scale, octaves, seed, gain, lacunarity).release();
}

libfive_tree libfive_field_curvature(libfive_tree t, float h)
{
    return libfive::fields::curvatureField(Tree(t), h).release();
}

libfive_vec3 libfive_tree_eval_d(libfive_tree t, libfive_vec3 p)
{
    DerivArrayEvaluator e((Tree(t)));
    auto v = e.deriv({p.x, p.y, p.z});
    return {v.x(), v.y(), v.z()};
}

char* libfive_tree_print(libfive_tree t)
{
    std::stringstream ss;
    ss << Tree(t);
    const auto str = ss.str();

    auto out = static_cast<char*>(malloc(str.size() + 1 * sizeof(char)));
    memcpy(out, str.c_str(), str.size() + 1);
    return out;
}

char* libfive_tree_content_key(libfive_tree t)
{
    const auto str = treeContentKey(Tree(t));

    auto out = static_cast<char*>(malloc(str.size() + 1 * sizeof(char)));
    memcpy(out, str.c_str(), str.size() + 1);
    return out;
}

int libfive_tree_expose_count(libfive_tree t)
{
    return int(exposableConstants(Tree(t)).size());
}

void libfive_tree_expose_values(libfive_tree t, float* out)
{
    const auto values = exposableConstants(Tree(t));
    for (size_t i = 0; i < values.size(); ++i) out[i] = values[i];
}

libfive_tree libfive_tree_expose(libfive_tree t, libfive_tree* with, int count)
{
    std::vector<Tree> trees;
    for (int i = 0; i < count; ++i) trees.push_back(Tree(with[i]));
    const Tree out = exposeConstants(Tree(t), trees);
    if (out == Tree::invalid()) return nullptr;
    return Tree(out).release();
}

void libfive_free_str(char* ptr) {
    free(ptr);
}

////////////////////////////////////////////////////////////////////////////////

libfive_contours* libfive_tree_render_slice(libfive_tree tree,
        libfive_region2 R, float z, float res)
{
    Region<2> region({R.X.lower, R.Y.lower}, {R.X.upper, R.Y.upper},
            Region<2>::Perp(z));
    BRepSettings settings;
    settings.min_feature = 1/res;
    auto cs = Contours::render(Tree(tree), region, settings);

    auto out = new libfive_contours;
    out->count = cs->contours.size();
    out->cs = new libfive_contour[out->count];

    size_t i=0;
    for (auto& c : cs->contours)
    {
        out->cs[i].count = c.size();
        out->cs[i].pts = new libfive_vec2[c.size()];

        size_t j=0;
        for (auto& pt : c)
        {
            out->cs[i].pts[j++] = {pt.x(), pt.y()};
        }
        i++;
    }

    return out;
}

libfive_contours3* libfive_tree_render_slice3(libfive_tree tree,
                                              libfive_region2 R, float z, float res)
{
    Region<2> region({R.X.lower, R.Y.lower}, {R.X.upper, R.Y.upper},
            Region<2>::Perp(z));
    BRepSettings settings;
    settings.min_feature = 1/res;
    auto cs = Contours::render(Tree(tree), region, settings);

    auto out = new libfive_contours3;
    out->count = cs->contours.size();
    out->cs = new libfive_contour3[out->count];

    size_t i=0;
    for (auto& c : cs->contours)
    {
        out->cs[i].count = c.size();
        out->cs[i].pts = new libfive_vec3[c.size()];

        size_t j=0;
        for (auto& pt : c)
        {
          // each 2D contour point is converted to a 3D point (with
          // this function's z argument as the Z coordinate)
          out->cs[i].pts[j++] = {pt.x(), pt.y(), z};
        }
        i++;
    }

    return out;
}

void libfive_tree_save_slice(libfive_tree tree, libfive_region2 R, float z, float res,
                        const char* f)
{
    Region<2> region({R.X.lower, R.Y.lower}, {R.X.upper, R.Y.upper},
            Region<2>::Perp(z));
    BRepSettings settings;
    settings.min_feature = 1/res;
    auto cs = Contours::render(Tree(tree), region, settings);
    cs->saveSVG(f);
}

libfive_mesh* libfive_tree_render_mesh_(libfive_tree tree, libfive_region3 R,
                                        const BRepSettings& settings)
{
    Region<3> region({R.X.lower, R.Y.lower, R.Z.lower},
                     {R.X.upper, R.Y.upper, R.Z.upper});
    auto ms = Mesh::render(Tree(tree), region, settings);
    if (ms.get() == nullptr)
    {
        fprintf(stderr, "libfive_tree_render_mesh: got empty mesh\n");
        return nullptr;
    }

    auto out = new libfive_mesh;
    out->verts = new libfive_vec3[ms->verts.size()];
    out->vert_count = ms->verts.size();
    out->tris = new libfive_tri[ms->branes.size()];
    out->tri_count = ms->branes.size();

    size_t i;

    i=0;
    for (auto& v : ms->verts)
    {
        out->verts[i++] = {v.x(), v.y(), v.z()};
    }

    i=0;
    for (auto& t : ms->branes)
    {
        out->tris[i++] = {(uint32_t)t.x(), (uint32_t)t.y(), (uint32_t)t.z()};
    }

    return out;
}

libfive_mesh* libfive_tree_render_mesh(libfive_tree tree, libfive_region3 R, float res) {
  BRepSettings settings;
  settings.min_feature = 1/res;
  return libfive_tree_render_mesh_(tree, R, settings);
}

libfive_mesh* libfive_tree_render_mesh_st(libfive_tree tree, libfive_region3 R, float res) {
  BRepSettings settings;
  settings.min_feature = 1/res;
  settings.workers = 1;
  return libfive_tree_render_mesh_(tree, R, settings);
}

libfive_mesh* libfive_tree_render_mesh_algo(libfive_tree tree, libfive_region3 R, float res,
                                             int algo, double max_err, int workers) {
  BRepSettings settings;
  settings.min_feature = 1/res;
  settings.workers = workers;
  settings.max_err = max_err;
  settings.alg = static_cast<BRepAlgorithm>(algo);
  return libfive_tree_render_mesh_(tree, R, settings);
}
 
libfive_mesh_coords* libfive_tree_render_mesh_coords(libfive_tree tree,
                                                     libfive_region3 R,
                                                     float res)
{
    Region<3> region({R.X.lower, R.Y.lower, R.Z.lower},
                     {R.X.upper, R.Y.upper, R.Z.upper});
    BRepSettings settings;
    settings.min_feature = 1/res;
    auto ms = Mesh::render(Tree(tree), region, settings);
    if (ms.get() == nullptr)
    {
        fprintf(stderr, "libfive_tree_render_mesh_coords: got empty mesh\n");
        return nullptr;
    }

    auto out = new libfive_mesh_coords;
    out->verts = new libfive_vec3[ms->verts.size()];
    out->vert_count = ms->verts.size();
    // need 4 times the count of triangles for coordinate indices
    // (3 vertices separated by -1 for each triangle)
    out->coord_indices = new int32_t[4 * ms->branes.size()];
    out->coord_index_count = 4 * ms->branes.size();

    size_t i;

    i=0;
    for (auto& v : ms->verts)
    {
        out->verts[i++] = {v.x(), v.y(), v.z()};
    }

    i=0;
    for (auto& t : ms->branes)
    {
      out->coord_indices[i++] = (int32_t)t.x();
      out->coord_indices[i++] = (int32_t)t.y();
      out->coord_indices[i++] = (int32_t)t.z();
      out->coord_indices[i++] = -1;
    }

    return out;
}

bool libfive_tree_save_mesh(libfive_tree tree, libfive_region3 R, float res, const char* f)
{
    Region<3> region({R.X.lower, R.Y.lower, R.Z.lower},
                     {R.X.upper, R.Y.upper, R.Z.upper});

    BRepSettings settings;
    settings.min_feature = 1/res;
    auto ms = Mesh::render(Tree(tree), region, settings);
    return ms->saveSTL(f);
}

bool libfive_evaluator_save_mesh(libfive_evaluator evaluator, libfive_region3 R, const char *f)
{
    Region<3> region({R.X.lower, R.Y.lower, R.Z.lower},
                     {R.X.upper, R.Y.upper, R.Z.upper});

    BRepSettings settings; // TODO: pass it in as an argument
    settings.workers = 1;  // NOTE: temporary limitation
    auto ms = Mesh::render(evaluator, region, settings);
    return ms->saveSTL(f);
}

bool libfive_tree_save_meshes(
        libfive_tree trees[], libfive_region3 R,
        float res, float quality, const char* f)
{
    Region<3> region({R.X.lower, R.Y.lower, R.Z.lower},
                     {R.X.upper, R.Y.upper, R.Z.upper});

    BRepSettings settings;
    settings.min_feature = 1/res;
    settings.max_err = pow(10, -quality);
    std::list<const libfive::Mesh*> meshes;
    for (unsigned i=0; trees[i] != nullptr; ++i){
        auto ms = Mesh::render(Tree(trees[i]), region, settings);
        meshes.push_back(ms.release());
    }

    const bool out = Mesh::saveSTL(f, meshes);
    for (auto& m : meshes) {
        delete m;
    }
    return out;
}

libfive_pixels* libfive_tree_render_pixels(libfive_tree tree, libfive_region2 R,
                                 float z, float res)
{
    Voxels v({R.X.lower, R.Y.lower, z},
             {R.X.upper, R.Y.upper, z}, res);
    std::atomic_bool abort(false);
    auto h = Heightmap::render(Tree(tree), v, abort);

    libfive_pixels* out = new libfive_pixels;
    out->width = h->depth.cols();
    out->height = h->depth.rows();
    out->pixels = new bool[out->width * out->height];

    size_t i=0;
    for (unsigned y=0; y < out->height; ++y)
    {
        for (unsigned x=0; x < out->width; ++x)
        {
            out->pixels[i++] = !std::isinf(h->depth(y, x));
        }
    }

    return out;
}

libfive_evaluator libfive_tree_evaluator(libfive_tree tree, libfive_vars vars)
{
    std::map<libfive::Tree::Id, float> mapOfVars;
    for (unsigned i = 0; i < vars.size; ++i)
    {
        auto treeId = static_cast<libfive::Tree::Id>(vars.vars[i]);
        mapOfVars.insert(std::make_pair(treeId, vars.values[i]));
    }
    // TODO: For more than one worker
    return new Evaluator(Tree(tree), mapOfVars);
}

bool libfive_evaluator_update_vars(libfive_evaluator eval_tree, libfive_vars vars)
{
    std::map<libfive::Tree::Id, float> mapOfVars;
    for (unsigned i = 0; i < vars.size; ++i)
    {
        auto treeId = static_cast<libfive::Tree::Id>(vars.vars[i]);
        mapOfVars.insert(std::make_pair(treeId, vars.values[i]));
    }

    return eval_tree->updateVars(mapOfVars);
}

void libfive_evaluator_delete(libfive_evaluator ptr)
{
    // TODO: For more than one worker
    std::cout << "libfive_evaluator_delete";
    delete ptr;
}

////////////////////////////////////////////////////////////////////////////////

// These variables are injected into the compiler as definitions
const char* libfive_git_version(void)
{
    return GIT_TAG;
}

const char* libfive_git_revision(void)
{
    return GIT_REV;
}

const char* libfive_git_branch(void)
{
    return GIT_BRANCH;
}

////////////////////////////////////////////////////////////////////////////////
// Debug: on an access violation anywhere in the process, print the faulting
// thread's native stack (function, file:line where debug info exists) to
// stderr.  For tracking down crashes in the multi-threaded mesher.
#ifdef _WIN32
#define NOMINMAX
#include <atomic>
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")

static LONG WINAPI libfiveCrashHandler(EXCEPTION_POINTERS* info)
{
    if (info->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    static std::atomic<bool> once{false};
    if (once.exchange(true)) return EXCEPTION_CONTINUE_SEARCH;
    HANDLE proc = GetCurrentProcess();
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(proc, nullptr, TRUE);
    CONTEXT ctx = *info->ContextRecord;
    STACKFRAME64 frame = {};
    frame.AddrPC.Offset = ctx.Rip;    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = ctx.Rbp; frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = ctx.Rsp; frame.AddrStack.Mode = AddrModeFlat;
    fprintf(stderr, "[crash] access violation at %p (%s address %p)\n",
            info->ExceptionRecord->ExceptionAddress,
            info->ExceptionRecord->ExceptionInformation[0] ? "writing" : "reading",
            (void*)info->ExceptionRecord->ExceptionInformation[1]);
    for (int k = 0; k < 40; k++) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, proc, GetCurrentThread(), &frame, &ctx,
                         nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) break;
        char buf[sizeof(SYMBOL_INFO) + 512];
        auto sym = reinterpret_cast<SYMBOL_INFO*>(buf);
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = 511;
        DWORD64 disp = 0;
        IMAGEHLP_LINE64 line = {};
        line.SizeOfStruct = sizeof(line);
        DWORD ldisp = 0;
        const bool haveSym = SymFromAddr(proc, frame.AddrPC.Offset, &disp, sym);
        const bool haveLine = SymGetLineFromAddr64(proc, frame.AddrPC.Offset, &ldisp, &line);
        fprintf(stderr, "[crash]   #%d %s  %s:%lu\n", k, haveSym ? sym->Name : "?",
                haveLine ? line.FileName : "?", haveLine ? line.LineNumber : 0ul);
    }
    fflush(stderr);
    return EXCEPTION_CONTINUE_SEARCH;
}

void libfive_debug_crash_handler(void)
{
    AddVectoredExceptionHandler(1, libfiveCrashHandler);
}
#else
void libfive_debug_crash_handler(void) {}
#endif
