/*
FielDes: field-driven design
Derived from Studio, a simple GUI for the libfive CAD kernel
Copyright (C) 2017  Matt Keeter

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/
#include <unordered_map>
#include <tuple>
#include <unordered_set>
#include <algorithm>
#include <chrono>
#include <atomic>
#include <mutex>
#include <set>
#include <thread>
#include <QDateTime>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <unordered_map>
#include <functional>
#include <limits>
#include <map>
#include <utility>
#include <vector>
#include <Eigen/Dense>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

#include "fieldes/i18n.hpp"
#include "fieldes/shape.hpp"
#include "libfive/tree/expose.hpp"
#include "libfive.h"
#include "fieldes/shader.hpp"
#include "fieldes/colormap.hpp"

#include "libfive/render/brep/free_thread_handler.hpp"
#include "libfive/render/brep/spawn.hpp"
#include "libfive/eval/tape.hpp"
#include "libfive/eval/evaluator.hpp"
#include "libfive/tree/data.hpp"
#include "libfive/tree/content_key.hpp"
#include "libfive/oracle/oracle_clause.hpp"
#include "libfive/fea/tetfea.hpp"

namespace FielDes {

const int Shape::MESH_DIV_EMPTY;
const int Shape::MESH_DIV_ABORT;
const int Shape::MESH_DIV_NEW_VARS;
const int Shape::MESH_DIV_NEW_VARS_SMALL;
const int Shape::MESH_DIV_NEW_TREE;

// (FIELDES_TIMING: what making a shape's tree costs, to stderr)
static libfive::Tree optimizedTimed(const libfive::Tree& t)
{
    const auto t0 = std::chrono::steady_clock::now();
    libfive::Tree o = t.optimized();
    if (std::getenv("FIELDES_TIMING"))
        fprintf(stderr, "[shape] optimized the tree in %.0f ms\n", 1000.0 * std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    return o;
}

Shape::Shape(const libfive::Tree& t,
             std::map<libfive::Tree::Id, float> vars)
    : tree(t), built_tree(t), vars(vars),
      vert_vbo(QOpenGLBuffer::VertexBuffer),
      tri_vbo(QOpenGLBuffer::IndexBuffer)
{
    cache_vars = vars;
    m_sourceId = built_tree.id();

    // (the optimised tree and the evaluators that mesh it are made when something reads the field: see ensureField)

    // The watcher that reports a finished level is the shape's child, so that it goes where the shape goes: a shape is made
    // by the script's thread and moved to the GUI thread (Interpreter::recordShape), and a watcher left behind delivers its
    // events to the thread that runs the script, which handles none until the run is over -- no shape was meshed on the
    // screen while an analysis ran
    mesh_watcher.setParent(this);
    connect(this, &Shape::gotMesh, this, &Shape::redraw);
    connect(&mesh_watcher, &decltype(mesh_watcher)::finished,
            this, &Shape::onFutureFinished);
}

// A field that is not negative anywhere (the field of a surface: zero on the sheet, the distance to it elsewhere) has no inside to find the sheet by.  It is drawn by its zero set all the same:
// the thin shell that is within a cell of the sheet is meshed, as any body is, and every vertex of it is carried onto the sheet along the field's own gradient.  What comes of it is the sheet
// A part that is placed turned is meshed in its own frame, so that its mesh does not depend on how it stands (the user, 2026-11-04: "the meshing does not follow the direction of the body either, so they body is
// meshes differently when it is rotated. That should not be the case ever."): the field the mesher sees is the placed field taken through the part's own placement, which gives the part as it was made, upright and
// at its own size, and the finished mesh is carried into the world by the same placement.  Where the part is not turned, nothing changes
static libfive::Tree inFrame(const libfive::Tree& t, const QMatrix4x4& M)
{
    auto row = [&](int r) {
        return libfive::Tree(double(M(r, 0))) * libfive::Tree::X() + libfive::Tree(double(M(r, 1))) * libfive::Tree::Y() +
               libfive::Tree(double(M(r, 2))) * libfive::Tree::Z() + libfive::Tree(double(M(r, 3)));
    };
    return t.remap(row(0), row(1), row(2));
}

static libfive::Region<3> localRegion(const libfive::Region<3>& region, const QMatrix4x4& Mi)
{
    Eigen::Vector3d lo(1e30, 1e30, 1e30), hi(-1e30, -1e30, -1e30);
    for (int c = 0; c < 8; ++c)
    {
        const QVector3D corner((c & 1) ? float(region.upper.x()) : float(region.lower.x()), (c & 2) ? float(region.upper.y()) : float(region.lower.y()),
                               (c & 4) ? float(region.upper.z()) : float(region.lower.z()));
        const QVector3D q = Mi.map(corner);
        for (int a = 0; a < 3; ++a)
        {
            lo[a] = std::min(lo[a], double(q[a]));
            hi[a] = std::max(hi[a], double(q[a]));
        }
    }
    libfive::Region<3> out = region;
    out.lower = lo;
    out.upper = hi;
    return out;
}

// The region a turned part is meshed over, made so that its cells are the cells the same part has upright (the user, 2026-11-04, H146, "When you rotate an object ... its resolution changes"): the mesher
// cuts a region into halves until a cell is no bigger than the feature size, so its cell is the side of the region over a power of two -- and the region of a turned part in its own frame is the
// world's region turned, a bigger box, whose cell comes out on another power of two (the same cylinder had 47738 points of its surface upright and 29468 turned).  The cell of the world's region is
// taken (`refSide` over the power of two that gets it below `minFeature`), and the region is a cube round the box, of that cell times a power of two, so the mesher ends on the same cell
static libfive::Region<3> sameCellRegion(const libfive::Region<3>& local, double refSide, double minFeature)
{
    // (the cell is `minFeature` itself, whatever the scene's region is -- the user, 2026-11-04, H146, after the first try went the other way when the region grew with the turn: "It should just be
    // dependent on the [part], so it measures the same value for the geometries, no matter the rotation".  The side of the cube is `minFeature` times a power of two, so the mesher ends on a cell of
    // exactly `minFeature`; `refSide` is no longer used)
    (void)refSide;
    if (!(minFeature > 0)) return local;
    const double need = double((local.upper - local.lower).maxCoeff());
    const int k = std::max(0, int(std::ceil(std::log2(std::max(need, minFeature) / minFeature) - 1e-9)));
    const double side = minFeature * std::ldexp(1.0, k);
    const Eigen::Vector3d c = 0.5 * (local.lower + local.upper);
    libfive::Region<3> out = local;
    out.lower = c - Eigen::Vector3d::Constant(0.5 * side);
    out.upper = c + Eigen::Vector3d::Constant(0.5 * side);
    return out;
}

static void intoWorld(std::unique_ptr<libfive::Mesh>& m, const QMatrix4x4& M)
{
    if (!m) return;
    for (auto& v : m->verts)
    {
        const QVector3D w = M.map(QVector3D(v.x(), v.y(), v.z()));
        v = Eigen::Vector3f(w.x(), w.y(), w.z());
    }
}

bool Shape::ownFrame(QMatrix4x4& M) const
{
    if (!m_gizmo.present || m_flat || has_deform || m_isPoint || hasControls() || hasSweep() || hasPrism() || hasMorph() || !m_steps.empty() || m_exact) return false;
    const GizmoNumbers now = gizmoNumbersIn(vars);
    bool turned = false;
    for (int a = 0; a < 3; ++a) turned = turned || std::fabs(std::remainder(now.rotate[a], 360.0f)) > 1e-4f;
    if (!turned) return false;
    M = gizmoMatrix(now.move, now.rotate, now.scale);
    bool ok = false;
    M.inverted(&ok);
    return ok && std::fabs(M.determinant()) > 1e-12f;
}

std::unique_ptr<libfive::Mesh> Shape::renderInOwnFrame(const libfive::Region<3>& region)
{
    QMatrix4x4 M;
    if (!ownFrame(M)) return libfive::Mesh::render(es.data(), sameCellRegion(region, 0, double(mesh_settings.min_feature)), mesh_settings);
    ensureField();
    const QMatrix4x4 Mi = M.inverted();
    const auto savedFeature = mesh_settings.min_feature;
    mesh_settings.min_feature = savedFeature / std::cbrt(std::fabs(double(M.determinant())));          // (the cell of the part's own size)
    const libfive::Tree local = inFrame(tree, M);
    std::vector<libfive::Evaluator> ev;
    ev.reserve(8);
    for (unsigned i = 0; i < 8; ++i) ev.emplace_back(local, vars);
    const double refLocal = double((region.upper - region.lower).maxCoeff()) / std::cbrt(std::fabs(double(M.determinant())));
    auto m = libfive::Mesh::render(ev.data(), sameCellRegion(localRegion(region, Mi), refLocal, double(mesh_settings.min_feature)), mesh_settings);
    mesh_settings.min_feature = savedFeature;
    intoWorld(m, M);
    return m;
}

std::unique_ptr<libfive::Mesh> Shape::unsignedMesh(const libfive::Region<3>& region)
{
    ensureField();
    QMatrix4x4 M;
    if (!ownFrame(M)) return unsignedMeshWith(sameCellRegion(region, 0, double(mesh_settings.min_feature)), tree, m_facing, mesh_settings);
    const QMatrix4x4 Mi = M.inverted();
    const auto savedFeature = mesh_settings.min_feature;
    mesh_settings.min_feature = savedFeature / std::cbrt(std::fabs(double(M.determinant())));          // (the cell of the part's own size)
    const libfive::Tree T = inFrame(tree, M);
    const libfive::Tree F = m_facing.is_valid() ? inFrame(m_facing, M) : m_facing;
    const double refLocal = double((region.upper - region.lower).maxCoeff()) / std::cbrt(std::fabs(double(M.determinant())));
    auto m = unsignedMeshWith(sameCellRegion(localRegion(region, Mi), refLocal, double(mesh_settings.min_feature)), T, F, mesh_settings);
    mesh_settings.min_feature = savedFeature;
    intoWorld(m, M);
    return m;
}

std::unique_ptr<libfive::Mesh> Shape::unsignedMeshWith(const libfive::Region<3>& region, const libfive::Tree& T, const libfive::Tree& F, const libfive::BRepSettings& settings)
{
    ensureField();
    // (the field is the distance to the sheet only where the sheet is flat: where it is steep -- a wave of a big amplitude -- the field grows |grad| times faster than the distance, so a shell cut at a fixed
    // value of it is |grad| times thinner in space, its two layers fall in one cell and the sheet tears into strips (measured: a wave of amplitude 6 and period 10, drawn in strips with gaps, and the same after
    // a handle drag, the user, 2026-11-04).  The steepest |grad| found at a spread of places of the region thickens the cut by that much, so the shell is as thick in space on a steep sheet as on a flat one)
    float steep = 1.0f;
    {
        libfive::JacobianEvaluator probe(T, vars);
        const Eigen::Vector3f lo(float(region.lower.x()), float(region.lower.y()), float(region.lower.z()));
        const Eigen::Vector3f hi(float(region.upper.x()), float(region.upper.y()), float(region.upper.z()));
        for (int k = 0; k < 2048; ++k)
        {
            // (a low-discrepancy spread, so that no period of a wave is sampled at one phase only)
            const float fx = std::fmod(0.5f + float(k) * 0.7548777f, 1.0f), fy = std::fmod(0.5f + float(k) * 0.5698403f, 1.0f), fz = std::fmod(0.5f + float(k) * 0.4301597f, 1.0f);
            const Eigen::Vector3f p = lo + (hi - lo).cwiseProduct(Eigen::Vector3f(fx, fy, fz));
            const auto vt = probe.valueAndPush(p);
            const auto g = probe.deriv(p, *vt.second);
            const float len = std::sqrt(g.x() * g.x() + g.y() * g.y() + g.z() * g.z());
            if (std::isfinite(len)) steep = std::max(steep, len);
        }
        steep = std::min(steep, 8.0f);
    }
    const libfive::Tree shell = T - libfive::Tree(1.75 * double(settings.min_feature) * double(steep));          // (a shell that is two cells thick or more: its two layers are not in one cell)
    std::vector<libfive::Evaluator> ev;
    ev.reserve(8);
    for (unsigned i = 0; i < 8; ++i) ev.emplace_back(shell, vars);
    auto m = libfive::Mesh::render(ev.data(), region, settings);
    if (!m) return m;
    libfive::JacobianEvaluator j(T, vars);
    const std::vector<Eigen::Vector3f, Eigen::aligned_allocator<Eigen::Vector3f>> shellVerts(m->verts.begin(), m->verts.end());
    for (auto& v : m->verts)
    {
        Eigen::Vector3f p = v;
        for (int it = 0; it < 4; ++it)
        {
            const auto vt = j.valueAndPush(p);
            const auto g = j.deriv(p, *vt.second);
            const Eigen::Vector3f gd(g.x(), g.y(), g.z());
            const float g2 = gd.squaredNorm();
            if (!std::isfinite(g2) || !(g2 > 1e-12f) || !std::isfinite(vt.first)) break;
            p -= gd * (vt.first / g2);
        }
        v = p;
    }
    // The shell has two layers that are now one on the other (the side of the sheet above and the side below), and a rim that has become the edge.  One layer is the sheet: a vertex says which side of the
    // sheet it was on by the direction it was carried in; a triangle whose corners were carried in different directions is a piece of the rim; the pieces that are left, joined by their corners,
    // are the layers, and the biggest is drawn
    const size_t N = m->verts.size();
    if (F.is_valid())
    {
        // The layer is said by the sheet's own signed field: a vertex of the shell is on the side of the sheet where that field is positive or where it is negative, and the layer that is drawn is the positive one --
        // every triangle with its three corners there.  A triangle across the rim has corners on both sides and goes with the other layer.  Nothing is joined into pieces and no piece is dropped, so the sheet cannot
        // lose a part of itself, and what is left of the mesh is never both layers (the user, 2026-11-04: "the rendering is so unstable that it often just disappears or doesnt render its parts")
        libfive::JacobianEvaluator jf(F, vars);
        std::vector<char> positive(N, 0);
        for (size_t i = 0; i < N; ++i)
        {
            const auto vt = jf.valueAndPush(shellVerts[i]);
            positive[i] = (std::isfinite(vt.first) && vt.first > 0.0f) ? 1 : 0;
        }
        std::vector<Eigen::Matrix<uint32_t, 3, 1>, Eigen::aligned_allocator<Eigen::Matrix<uint32_t, 3, 1>>> layer;
        for (const auto& tri : m->branes)
            if (positive[tri[0]] && positive[tri[1]] && positive[tri[2]]) layer.push_back(tri);
        if (!layer.empty())
        {
            m->branes = std::move(layer);
            return m;
        }
    }
    std::vector<Eigen::Vector3f, Eigen::aligned_allocator<Eigen::Vector3f>> way(N, Eigen::Vector3f::Zero());
    for (size_t i = 0; i < N; ++i)
    {
        const Eigen::Vector3f d = shellVerts[i] - m->verts[i];
        const float len = d.norm();
        if (len > 1e-12f) way[i] = d / len;
    }
    std::vector<uint32_t> parent(N);
    for (size_t i = 0; i < N; ++i) parent[i] = uint32_t(i);
    auto find = [&](uint32_t a) {
        while (parent[a] != a) { parent[a] = parent[parent[a]]; a = parent[a]; }
        return a;
    };
    std::vector<char> kept(m->branes.size(), 0);
    for (size_t k = 0; k < m->branes.size(); ++k)
    {
        const auto& tri = m->branes[k];
        const Eigen::Vector3f &a = way[tri[0]], &b = way[tri[1]], &c = way[tri[2]];
        if (!(a.squaredNorm() > 0.5f && b.squaredNorm() > 0.5f && c.squaredNorm() > 0.5f)) continue;
        if (a.dot(b) < 0.9f || b.dot(c) < 0.9f || c.dot(a) < 0.9f) continue;
        kept[k] = 1;
        parent[find(tri[0])] = find(tri[1]);
        parent[find(tri[1])] = find(tri[2]);
    }
    std::map<uint32_t, size_t> size;
    for (size_t k = 0; k < m->branes.size(); ++k)
        if (kept[k]) ++size[find(m->branes[k][0])];
    if (size.empty()) return m;
    uint32_t biggest = size.begin()->first;
    for (const auto& kv : size)
        if (kv.second > size[biggest]) biggest = kv.first;
    std::vector<Eigen::Matrix<uint32_t, 3, 1>, Eigen::aligned_allocator<Eigen::Matrix<uint32_t, 3, 1>>> one;
    for (size_t k = 0; k < m->branes.size(); ++k)
        if (kept[k] && find(m->branes[k][0]) == biggest) one.push_back(m->branes[k]);
    m->branes = std::move(one);
    return m;
}

void Shape::ensureField() const
{
    if (!m_field_ready.load()) const_cast<Shape*>(this)->buildField();
}

void Shape::buildField()
{
    std::lock_guard<std::mutex> lock(m_field_mutex);
    if (m_field_ready.load()) return;
    tree = optimizedTimed(built_tree);
    base_tree = tree;
    // Construct evaluators to run meshing (in parallel)
    const auto tEval = std::chrono::steady_clock::now();
    m_tree_nodes = tree.size();
    es.clear();
    es.reserve(8);
    for (unsigned i = 0; i < 8; ++i) es.emplace_back(libfive::Evaluator(tree, vars));
    if (std::getenv("FIELDES_TIMING"))
        fprintf(stderr, "[shape] eight evaluators in %.0f ms (line %d, point %d, image %d, morph %d, sweep %d, prism %d)\n", 1000.0 * std::chrono::duration<double>(std::chrono::steady_clock::now() - tEval).count(),
                sourceLine(), int(m_isPoint), int(isSymmetryImage()), int(hasMorph()), int(hasSweep()), int(hasPrism()));
    m_deps_known = false;
    m_field_ready.store(true);
}

void Shape::settleField()
{
    // (a point is drawn as a dot from its numbers: the optimized tree and the eight evaluators, 5 ms each, were made for each of the 884 points of an import on the thread of the window; whatever reads a field calls ensureField)
    if (!drawnFromNumbers() && !isPoint()) ensureField();
}

Shape::~Shape()
{
    if (mesh_future.isRunning())
    {
        mesh_future.waitForFinished();
    }
}

void Shape::setColorField(const libfive::Tree& field, float lo, float hi, bool autoRange,
                          const QString& label, const QString& colormap)
{
    has_color = field.is_valid();
    color_field = field;
    color_auto = autoRange;
    color_lo = lo;
    color_hi = hi;
    color_label = label;
    color_map = colormap;
}

void Shape::setBoundarySymbols(std::vector<BcGlyph> glyphs, std::vector<BcLabel> labels)
{
    bc_glyphs = std::move(glyphs);
    bc_labels = std::move(labels);
    QString key;
    for (const auto& g : bc_glyphs)
        key += QString("%1,%2,%3,%4,%5,%6,%7,%8,%9;").arg(g.kind).arg(g.tip).arg(g.pos.x()).arg(g.pos.y()).arg(g.pos.z())
                   .arg(g.dir.x()).arg(g.dir.y()).arg(g.dir.z()).arg(g.size);
    for (const auto& l : bc_labels) key += QString("%1,%2;").arg(l.kind).arg(l.text);
    bc_key = key;
}

void Shape::setMarkers(std::vector<QVector3D> points)
{
    m_markers = std::move(points);
    // (a hash of the points, not the points: a set can have thousands)
    quint64 h = 1469598103934665603ull;
    for (const QVector3D& p : m_markers)
        for (const float c : {p.x(), p.y(), p.z()})
        {
            quint32 bits;
            std::memcpy(&bits, &c, sizeof(bits));
            h = (h ^ bits) * 1099511628211ull;
        }
    marker_key = m_markers.empty() ? QString() : QString("|dots %1 %2").arg(m_markers.size()).arg(h, 0, 16);
}

void Shape::setRenderHint(QVector3D lo, QVector3D hi, float res, float side, float scene_res)
{
    has_hint = res > 0 && side > 0 && scene_res > 0;
    hint_lo = lo;
    hint_hi = hi;
    hint_res = res;
    hint_side = side;
    hint_scene_res = scene_res;
}

void Shape::setExactMesh(std::shared_ptr<const ExactMesh> mesh, const double* matrix)
{
    m_exact = std::move(mesh);
    for (int i = 0; i < 12; ++i) m_exact_matrix[i] = matrix[i];
    m_exact_vars = vars;
}

// The triangles are what the part is as long as the numbers it was made with are the ones it has: a var() that was dragged since
// the script ran has moved it, and only the field knows where to
bool Shape::exactCurrent() const
{
    if (!m_exact) return false;
    for (const auto& v : m_exact_vars)
    {
        const auto it = vars.find(v.first);
        if (it == vars.end()) return false;
        if (std::fabs(it->second - v.second) > 1e-6f && usesVar(v.first)) return false;
    }
    return true;
}

// The exact triangles, placed, that touch the region (a triangle whose box meets it is whole: nothing is cut at its edge)
std::unique_ptr<libfive::Mesh> Shape::exactMeshIn(const libfive::Region<3>& r) const
{
    return exactMeshOf(*m_exact, m_exact_matrix, r);
}

Shape::StepSurface::StepSurface(std::shared_ptr<libfive::fea::TetProblem> p, double lvl, int n)
    : problem(std::move(p)), level(lvl), count(n), m_made(size_t(std::max(0, n)))
{
}

Shape::StepSurface::~StepSurface() {}

std::shared_ptr<const Shape::ExactMesh> Shape::StepSurface::mesh(int k)
{
    if (k < 0 || k >= count || !problem) return nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_made[size_t(k)]) return m_made[size_t(k)];
    }
    auto m = std::make_shared<ExactMesh>();
    if (!problem->densitySurface(size_t(k), level, m->verts, m->tris) || m->tris.size() < 3) return nullptr;
    m->key = "step:" + std::to_string(reinterpret_cast<uintptr_t>(this)) + ":" + std::to_string(k);
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_made[size_t(k)])
    {
        m_bytes += m->verts.size() * sizeof(float) + m->tris.size() * sizeof(uint32_t);
        m_made[size_t(k)] = m;
    }
    return m_made[size_t(k)];
}

void Shape::StepSurface::prepareAll()
{
    for (int k = 0; k < count; ++k)
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_bytes > (size_t(600) << 20)) return;      // (the rest is made when it is wanted)
        }
        mesh(k);
    }
}

std::unique_ptr<libfive::Mesh> Shape::exactMeshOf(const ExactMesh& E, const double* A, const libfive::Region<3>& r)
{
    auto out = std::make_unique<libfive::Mesh>();
    const size_t nv = E.verts.size() / 3;
    std::vector<Eigen::Vector3f> pos(nv);
    for (size_t i = 0; i < nv; ++i)
    {
        const double x = E.verts[3 * i], y = E.verts[3 * i + 1], z = E.verts[3 * i + 2];
        pos[i] = Eigen::Vector3f(float(A[0] * x + A[1] * y + A[2] * z + A[3]),
                                 float(A[4] * x + A[5] * y + A[6] * z + A[7]),
                                 float(A[8] * x + A[9] * y + A[10] * z + A[11]));
    }
    const double det = A[0] * (A[5] * A[10] - A[6] * A[9]) - A[1] * (A[4] * A[10] - A[6] * A[8]) +
                       A[2] * (A[4] * A[9] - A[5] * A[8]);
    const bool mirrored = det < 0;          // (a mirror turns the triangles inside out)
    std::vector<uint32_t> number(nv, UINT32_MAX);
    for (size_t t = 0; t + 2 < E.tris.size(); t += 3)
    {
        const uint32_t v[3] = {E.tris[t], E.tris[t + 1], E.tris[t + 2]};
        if (v[0] >= nv || v[1] >= nv || v[2] >= nv) continue;
        Eigen::Array3f lo = pos[v[0]].array().min(pos[v[1]].array()).min(pos[v[2]].array());
        Eigen::Array3f hi = pos[v[0]].array().max(pos[v[1]].array()).max(pos[v[2]].array());
        bool inside = true;
        for (int a = 0; a < 3 && inside; ++a) inside = hi[a] >= r.lower[a] && lo[a] <= r.upper[a];
        if (!inside) continue;
        uint32_t id[3];
        for (int k = 0; k < 3; ++k)
        {
            if (number[v[k]] == UINT32_MAX)
            {
                number[v[k]] = uint32_t(out->verts.size());
                out->verts.push_back(pos[v[k]]);
            }
            id[k] = number[v[k]];
        }
        if (mirrored) std::swap(id[1], id[2]);
        out->branes.push_back(Eigen::Matrix<uint32_t, 3, 1>(id[0], id[1], id[2]));
    }
    return out;
}

bool Shape::hasGizmo() const
{
    // A point that is locked to its body -- a corner, a point of an edge -- cannot be moved, but its influence is locked to nothing: the ring that drags it is the gizmo it has (the user, 2026-10-28:
    // "the point is locked to the corner, but the influence is not and thus the influence gizmo should appear")
    const bool ringOfLocked = m_locked && m_isPoint && m_gizmo.present && m_gizmo.influence && vars.count(m_gizmo.influence);
    if ((m_locked && !ringOfLocked) || !m_gizmo.present || m_gizmo.hidden || (m_gizmo.dotOnly && !m_activeCopy) || m_gizmo.mode == Gizmo::NEVER) return false;
    if (m_gizmo.mode == Gizmo::CLICK && !isSelected()) return false;
    for (int a = 0; a < 3; ++a)
    {
        if ((m_gizmo.move[a] && vars.count(m_gizmo.move[a])) ||
            (m_gizmo.rotate[a] && vars.count(m_gizmo.rotate[a])) ||
            (m_gizmo.scale[a] && vars.count(m_gizmo.scale[a])))
        {
            return true;
        }
    }
    return m_gizmo.influence && vars.count(m_gizmo.influence);
}

bool Shape::dotPosition(QVector3D& p) const
{
    if (m_gizmo.present)
    {
        p = gizmoPivot();
        return true;
    }
    if (m_hasPointPos)
    {
        p = m_pointPos;
        // (a point found on a body: carried by the numbers that move the faces it is made of, while one of them is pulled -- see LiveTerm)
        for (const LiveTerm& t : m_liveTerms)
        {
            float now = t.v0;
            const auto given = m_liveNow.find(t.id);
            if (given != m_liveNow.end()) now = given->second;
            else if (const auto own = vars.find(t.id); own != vars.end()) now = own->second;
            p += t.j * (now - t.v0);
        }
        return true;
    }
    return false;
}

bool Shape::carriesParts() const
{
    if (m_locked || m_gizmo.present) return false;
    for (const Gizmo& g : m_carried)
    {
        for (int a = 0; a < 3; ++a)
        {
            if (g.move[a] && vars.count(g.move[a])) return true;
        }
    }
    return false;
}

QVector3D Shape::carriedPivotOf(const Gizmo& g) const
{
    QVector3D move;
    for (int a = 0; a < 3; ++a)
    {
        const auto it = g.move[a] ? vars.find(g.move[a]) : vars.end();
        move[a] = it != vars.end() ? it->second : 0.0f;
    }
    return g.about + move;
}

QVector3D Shape::carriedPivot() const
{
    QVector3D lo, hi;
    bool first = true;
    for (const Gizmo& g : m_carried)
    {
        bool moves = false;
        for (int a = 0; a < 3; ++a) moves = moves || (g.move[a] && vars.count(g.move[a]));
        if (!moves) continue;
        const QVector3D P = carriedPivotOf(g);
        lo = first ? P : QVector3D(std::min(lo.x(), P.x()), std::min(lo.y(), P.y()), std::min(lo.z(), P.z()));
        hi = first ? P : QVector3D(std::max(hi.x(), P.x()), std::max(hi.y(), P.y()), std::max(hi.z(), P.z()));
        first = false;
    }
    return first ? QVector3D() : 0.5f * (lo + hi);
}

bool Shape::hasMoveVars() const
{
    if (carriesParts()) return true;
    if (m_locked || !m_gizmo.present || m_gizmo.hidden) return false;
    for (int a = 0; a < 3; ++a)
    {
        if (m_gizmo.move[a] && vars.count(m_gizmo.move[a])) return true;
    }
    return false;
}

std::set<libfive::Tree::Id> Shape::gizmoVars() const
{
    std::set<libfive::Tree::Id> out;
    for (int a = 0; a < 3; ++a)
    {
        for (const auto id : {m_gizmo.move[a], m_gizmo.rotate[a], m_gizmo.scale[a]})
        {
            if (id) out.insert(id);
        }
    }
    if (m_gizmo.influence) out.insert(m_gizmo.influence);
    return out;
}

std::set<libfive::Tree::Id> Shape::pullMask() const
{
    std::set<libfive::Tree::Id> out = gizmoVars();
    out.insert(m_frozenVars.begin(), m_frozenVars.end());
    if (m_sceneGizmoVars) out.insert(m_sceneGizmoVars->begin(), m_sceneGizmoVars->end());
    return out;
}

double Shape::evalConstant(const libfive::Tree& t, const std::map<libfive::Tree::Id, float>& v) const
{
    libfive::ArrayEvaluator e(t, v);
    e.set(Eigen::Vector3f::Zero(), 0);
    return double(e.values(1)[0]);
}

void Shape::setFlatFrame(std::vector<libfive::Tree> trees)
{
    m_frame_trees = std::move(trees);
    updateFrameNumbers();
}

// The plane of a flat shape, as the numbers of the script are now
void Shape::updateFrameNumbers()
{
    m_frame_now_valid = false;
    if (m_frame_trees.size() != 9) return;
    double f[9];
    for (int i = 0; i < 9; ++i)
    {
        f[i] = evalConstant(m_frame_trees[size_t(i)], vars);
        if (!std::isfinite(f[i])) return;
    }
    for (int i = 0; i < 9; ++i) m_frame_now[i] = f[i];
    m_frame_now_valid = true;
}

bool Shape::controlNumbers(const std::map<libfive::Tree::Id, float>& v, std::vector<libfive::pointshapes::Control>& out) const
{
    return controlNumbersOf(m_controls, m_controls_face, v, out, this);
}

bool Shape::controlNumbersOf(const std::vector<ControlPoint>& points, bool face, const std::map<libfive::Tree::Id, float>& v, std::vector<libfive::pointshapes::Control>& out,
                             const Shape* owner)
{
    out.clear();
    for (const ControlPoint& c : points)
    {
        libfive::pointshapes::Control k;
        double n[3];
        for (int a = 0; a < 3; ++a)
        {
            n[a] = owner->evalConstant(c.pos[a], v);
            if (!std::isfinite(n[a])) return false;
        }
        k.pos = Eigen::Vector3d(n[0], n[1], n[2]);
        k.smooth = c.flag;
        if (c.hasRot)
        {
            for (int a = 0; a < 3; ++a)
            {
                n[a] = owner->evalConstant(c.rot[a], v);
                if (!std::isfinite(n[a])) return false;
            }
            k.rotate = Eigen::Vector3d(n[0], n[1], n[2]);
            k.turned = k.rotate.cwiseAbs().maxCoeff() > 1e-12;
        }
        if (c.hasScale)
        {
            const double s = owner->evalConstant(c.scale, v);
            if (!std::isfinite(s)) return false;
            k.scale = std::max(0.0, s);
        }
        if (c.hasInfluence)
        {
            const double s = owner->evalConstant(c.influence, v);
            if (!std::isfinite(s)) return false;
            k.influence = std::max(0.0, s);
        }
        if (c.hasRadius)
        {
            const double r = owner->evalConstant(c.radius, v);
            if (!std::isfinite(r)) return false;
            k.radius = std::max(0.0, r);
        }
        out.push_back(k);
    }
    // The points of a face turn about the normal of its plane, by one angle
    if (face)
    {
        std::vector<Eigen::Vector3d> p;
        for (const auto& k : out) p.push_back(k.pos);
        double frame[9];
        if (libfive::pointshapes::planeOf(p, frame))
        {
            const Eigen::Vector3d u(frame[3], frame[4], frame[5]), w(frame[6], frame[7], frame[8]);
            const Eigen::Vector3d normal = u.cross(w).normalized();
            for (auto& k : out)
            {
                k.planar = true;
                k.normal = normal;
            }
        }
    }
    return true;
}

libfive::Tree::Id Shape::soleVarOf(const libfive::Tree& expression)
{
    libfive::Tree::Id found = nullptr;
    int count = 0;
    const libfive::Tree flat = expression.flatten();
    for (const auto* node : flat.walk())
    {
        if (node->op() == libfive::Opcode::VAR_FREE)
        {
            found = node;
            ++count;
        }
    }
    return count == 1 ? found : nullptr;
}

void Shape::setPrism(std::vector<ControlPoint> points, bool smoothDefault, libfive::Tree a, libfive::Tree b, std::vector<std::pair<libfive::Tree::Id, float>> guard)
{
    m_prism_points = std::move(points);
    m_prism_default = smoothDefault;
    m_prism_a = a;
    m_prism_b = b;
    m_prism_guard = std::move(guard);
}

bool Shape::prismNow(const std::map<libfive::Tree::Id, float>& v, std::vector<libfive::pointshapes::Control>& cs, double& a, double& b) const
{
    static const bool off = std::getenv("FIELDES_NO_NATIVE") != nullptr;
    if (off || m_prism_points.empty() || !m_prism_a.is_valid() || !m_prism_b.is_valid()) return false;
    // (what colours or bends the shape is not a prism's to draw)
    if (color_field.is_valid() || has_result || !m_channels.empty()) return false;
    for (const auto& g : m_prism_guard)
    {
        const auto it = v.find(g.first);
        if (it == v.end() || std::fabs(it->second - g.second) > 1e-6f * std::max(1.0f, std::fabs(g.second))) return false;
    }
    if (!controlNumbersOf(m_prism_points, true, v, cs, this) || cs.size() < 3) return false;
    a = evalConstant(m_prism_a, v);
    b = evalConstant(m_prism_b, v);
    return std::isfinite(a) && std::isfinite(b);
}

bool Shape::prismWallAt(const QVector3D& press, PrismWall& out) const
{
    // The wall of the prism -- the extrusion of a profile of points -- that a press on its side is on: the chord between the two points of the outline that is nearest to the press in the plane
    // of the profile, with the numbers that move those two points (a gizmo's, one for each axis) and the direction in the plane that the wall faces.  Pulled, the wall is the two points moved
    // together along that direction: the same as the points of the profile are moved, and the profile and the prism follow.  False where the press is on an end face (its a and b are pulled by
    // the handles), where the prism is not what is drawn, or where the points have no numbers to move them by
    std::vector<libfive::pointshapes::Control> cs;
    double a = 0, b = 0;
    if (!prismNow(vars, cs, a, b)) return false;
    std::vector<Eigen::Vector3d> where;
    for (const auto& c : cs) where.push_back(c.pos);
    double frame[9];
    if (!libfive::pointshapes::planeOf(where, frame)) return false;
    const Eigen::Vector3d o(frame[0], frame[1], frame[2]), u(frame[3], frame[4], frame[5]), w(frame[6], frame[7], frame[8]);
    const Eigen::Vector3d n = u.cross(w).normalized();
    const Eigen::Vector3d p(press.x(), press.y(), press.z());
    const double lo = std::min(a, b), hi = std::max(a, b), height = n.dot(p - o);
    const double tol = 0.05 * (hi - lo) + 1e-4;
    if (!(hi - lo > 1e-9) || height < lo + tol || height > hi - tol) return false;             // (an end face, or no thickness)
    const size_t count = cs.size();
    if (count != m_prism_points.size()) return false;
    const Eigen::Vector3d in = p - n * n.dot(p - o);                                           // (the press, in the plane of the profile)
    double area = 0;
    for (size_t i = 0; i < count; ++i)
    {
        const Eigen::Vector3d q = cs[i].pos - o, r = cs[(i + 1) % count].pos - o;
        area += q.dot(u) * r.dot(w) - r.dot(u) * q.dot(w);
    }
    double best = 1e300;
    size_t first = 0;
    Eigen::Vector3d direction(0, 0, 0);
    for (size_t i = 0; i < count; ++i)
    {
        const Eigen::Vector3d s = cs[i].pos, e = cs[(i + 1) % count].pos, d = e - s;
        const double len2 = d.squaredNorm();
        if (!(len2 > 1e-18)) continue;
        const double t = std::max(0.0, std::min(1.0, (in - s).dot(d) / len2));
        const double dist = ((s + d * t) - in).norm();
        if (dist < best)
        {
            best = dist;
            first = i;
            // (outward: to the right of the edge for an outline that runs anticlockwise about the normal, to the left for one that runs clockwise)
            direction = d.cross(n).normalized() * (area >= 0 ? 1.0 : -1.0);
        }
    }
    if (!(best < 1e299) || !(direction.norm() > 0.5)) return false;
    out.vars.clear();
    for (const size_t k : {first, (first + 1) % count})
    {
        for (int axis = 0; axis < 3; ++axis)
        {
            if (m_prism_points[k].var[axis]) out.vars.push_back({m_prism_points[k].var[axis], axis});
        }
    }
    if (out.vars.empty()) return false;
    out.direction = QVector3D(float(direction.x()), float(direction.y()), float(direction.z()));
    out.planeNormal = QVector3D(float(n.x()), float(n.y()), float(n.z()));
    out.scales.clear();
    return true;
}

bool Shape::prismEndAt(const QVector3D& press, PrismWall& out, bool needNumber) const
{
    // The END FACE of the prism that a press is on, for pulling its length (the user, E1: "the handles to drag the extrusion just straight up dont work").  The field of an extrusion of a profile of
    // points is a mesh with a and b baked in, so the general pull of a surface has no number to follow (the kernel gives a mesh no slope along a variable); the end is pulled by its own number as a wall
    // is: the face moves along the normal of the plane of the profile, and a (or b) changes by exactly that move.  False where the press is not on an end, or where that end is not a number of its
    // own (a number or an expression in the call): the pull of a surface is what is left then
    std::vector<libfive::pointshapes::Control> cs;
    double a = 0, b = 0;
    if (!prismNow(vars, cs, a, b)) return false;
    std::vector<Eigen::Vector3d> where;
    for (const auto& c : cs) where.push_back(c.pos);
    double frame[9];
    if (!libfive::pointshapes::planeOf(where, frame)) return false;
    const Eigen::Vector3d o(frame[0], frame[1], frame[2]), u(frame[3], frame[4], frame[5]), w(frame[6], frame[7], frame[8]);
    const Eigen::Vector3d n = u.cross(w).normalized();
    const double height = n.dot(Eigen::Vector3d(press.x(), press.y(), press.z()) - o);
    const double lo = std::min(a, b), hi = std::max(a, b);
    if (!(hi - lo > 1e-9)) return false;
    const double tol = 0.05 * (hi - lo) + 1e-4;                       // (the same width as prismWallAt: what is not a wall is an end)
    const bool atA = std::fabs(height - a) <= tol, atB = std::fabs(height - b) <= tol;
    if (!atA && !atB) return false;
    if (!needNumber) return true;                                       // (a held press only asks whether it is on an end: the number may not be there yet, see View::replayEndPress)
    const libfive::Tree& end = (atB && (!atA || std::fabs(height - b) <= std::fabs(height - a))) ? m_prism_b : m_prism_a;
    // (a number of its own: the end is a variable of the script itself -- the var() that handles(extrusion, [a, b]) wrote -- and not an expression of one)
    if (!end.is_valid() || vars.find(end.id()) == vars.end()) return false;
    int axis = 0;
    for (int i = 1; i < 3; ++i)
    {
        if (std::fabs(n[i]) > std::fabs(n[axis])) axis = i;
    }
    if (!(std::fabs(n[axis]) > 0.2)) return false;
    out.vars.clear();
    out.scales.clear();
    out.vars.push_back({end.id(), axis});
    out.scales.push_back(float(1.0 / n[axis]));                       // (the move along n has n[axis] of it on that axis: the number changes by the move along n)
    out.direction = QVector3D(float(n.x()), float(n.y()), float(n.z()));
    out.planeNormal = out.direction;
    return true;
}

void Shape::setSweep(std::vector<ControlPoint> profile, bool profileDefault, std::vector<ControlPoint> path, bool pathDefault, bool closed, libfive::Tree twist,
                     bool placed, const double about[3], const libfive::Tree move[3], const libfive::Tree rotate[3], const libfive::Tree scale[3])
{
    m_sweep_profile = std::move(profile);
    m_sweep_profile_default = profileDefault;
    m_sweep_path = std::move(path);
    m_sweep_path_default = pathDefault;
    m_sweep_closed = closed;
    m_sweep_twist = twist;
    m_sweep_placed = placed;
    for (int a = 0; a < 3; ++a)
    {
        m_sweep_about[a] = about[a];
        m_sweep_move[a] = move[a];
        m_sweep_rotate[a] = rotate[a];
        m_sweep_scale[a] = scale[a];
    }
}

struct Shape::SweepNumbers
{
    std::vector<libfive::pointshapes::Control> profile, path;
    bool profileDefault = false, pathDefault = false, closed = false, placed = false, free = false, plain = false;          // (plain: the profile carried along the path, nothing taken out -- what the viewport draws; the field gets the cleaned mesh)
    double twist = 0;                                                       // (degrees)
    int steps = 14;                                                         // (the pieces a piece of a curve is cut in, for the outline and the path: follows the scene's resolution, see curveSteps)
    Eigen::Vector3d about = Eigen::Vector3d::Zero(), move = Eigen::Vector3d::Zero(), rotate = Eigen::Vector3d::Zero(), scale = Eigen::Vector3d::Ones();
};

bool Shape::sweepNow(const std::map<libfive::Tree::Id, float>& v, SweepNumbers& out) const
{
    static const bool off = std::getenv("FIELDES_NO_NATIVE") != nullptr;          // (for comparing with the field: every sweep and prism is meshed from its field)
    if (off || !hasSweep() || !m_sweep_twist.is_valid()) return false;
    if (color_field.is_valid() || has_result || !m_channels.empty()) return false;
    for (const auto& g : m_sweep_guard)
    {
        const auto it = v.find(g.first);
        if (it == v.end() || std::fabs(it->second - g.second) > 1e-6f * std::max(1.0f, std::fabs(g.second))) return false;
    }
    if (!controlNumbersOf(m_sweep_profile, true, v, out.profile, this) || out.profile.size() < 3) return false;
    if (!controlNumbersOf(m_sweep_path, false, v, out.path, this) || out.path.size() < 2) return false;
    out.profileDefault = m_sweep_profile_default;
    out.pathDefault = m_sweep_path_default;
    out.closed = m_sweep_closed;
    out.free = m_sweep_free;
    out.twist = evalConstant(m_sweep_twist, v);
    if (!std::isfinite(out.twist)) return false;
    out.placed = m_sweep_placed;
    if (m_sweep_placed)
    {
        for (int a = 0; a < 3; ++a)
        {
            out.about[a] = m_sweep_about[a];
            out.move[a] = m_sweep_move[a].is_valid() ? evalConstant(m_sweep_move[a], v) : 0.0;
            out.rotate[a] = m_sweep_rotate[a].is_valid() ? evalConstant(m_sweep_rotate[a], v) : 0.0;
            out.scale[a] = m_sweep_scale[a].is_valid() ? evalConstant(m_sweep_scale[a], v) : 1.0;
            if (!std::isfinite(out.move[a]) || !std::isfinite(out.rotate[a]) || !std::isfinite(out.scale[a])) return false;
        }
    }
    return true;
}

void Shape::setMorph(std::vector<libfive::Tree> controls, std::vector<uint32_t> recipeStart, std::vector<uint32_t> recipeControl, std::vector<double> recipeWeight, std::vector<uint32_t> triangles,
                     std::vector<int32_t> tapeOp, std::vector<int32_t> tapeA, std::vector<int32_t> tapeB, std::vector<double> tapeConst, std::vector<uint32_t> tapeOut)
{
    m_morph_tape_op = std::move(tapeOp);
    m_morph_tape_a = std::move(tapeA);
    m_morph_tape_b = std::move(tapeB);
    m_morph_tape_const = std::move(tapeConst);
    m_morph_tape_out = std::move(tapeOut);
    m_morph_controls = std::move(controls);
    m_morph_recipe_start = std::move(recipeStart);
    m_morph_recipe_control = std::move(recipeControl);
    m_morph_recipe_weight = std::move(recipeWeight);
    m_morph_triangles = std::move(triangles);
    std::lock_guard<std::mutex> lock(m_morph_mutex);
    m_morph_eval.reset();
    m_morph_last.clear();
    m_morph_plan.reset();
    m_morph_plan_failed = false;
}

namespace
{
// The numbers of the control points of a body of points, worked out DIRECTLY (H68, the user, 2026-11-02: "Also dragging Points out of the side STILL LAGS THE ENTIRE PROGRAM ... FIX THAT. IT IS VERY IMPORTANT"):
// the expressions of all the control points are walked once (every distinct node once, leaves to root) into a list of plain operations, and a step of a drag runs through that list with the numbers of the
// variables.  What was done before -- ONE tree, the sum over k of hat(x - k) times number k, read in batches of the evaluator's width -- ran through the WHOLE tape for each batch, so that the cost of a step went
// with the square of the number of control points.
struct MorphPlan
{
    struct Node
    {
        int op = 0;
        int a = -1;
        int b = -1;
        double value = 0.0;
    };
    std::vector<Node> nodes;
    std::vector<std::pair<int, libfive::Tree::Id>> vars;       // (the node of each variable, and its id)
    std::vector<int> output;                                   // (the node of each number of the control points: three to a control point)
    std::vector<double> value;
    std::vector<double> tape;                                  // (H164: the value of each operation of the tape)
};

// The nodes of the expression `root` that are not in `slot` yet, leaves first, each once.  The walk goes only into nodes that are new, so the part that the expressions of many control points have in common
// is entered once in all and not once for each of them: `root.walk()` for each of 432 control points of a smooth body went over its 42 000 nodes again and again and took about 600 ms (the log of the window:
// "direct reading: 41490 operations for 1296 numbers" and the body drawn 615 ms into the level; the user, 2026-11-02, H70).  False for an operation the direct reading does not do
bool newNodes(const libfive::Tree& root, const std::unordered_map<const void*, int>& slot, std::vector<const libfive::Tree::Data*>& order)
{
    namespace Op = libfive::Opcode;
    std::unordered_map<const void*, char> seen;
    std::vector<std::pair<const libfive::Tree::Data*, bool>> todo;          // (a node, and whether its operands have been put on the list: it is put in the order when it comes up again)
    todo.emplace_back(root.get(), false);
    while (!todo.empty())
    {
        const libfive::Tree::Data* d = todo.back().first;
        const bool expanded = todo.back().second;
        todo.pop_back();
        if (expanded)
        {
            order.push_back(d);
            continue;
        }
        if (slot.count(d) || !seen.emplace(d, 1).second) continue;
        todo.emplace_back(d, true);
        switch (d->op())
        {
            case Op::CONSTANT: case Op::VAR_FREE: break;
            case Op::CONST_VAR: case Op::OP_SQUARE: case Op::OP_SQRT: case Op::OP_NEG: case Op::OP_SIN: case Op::OP_COS: case Op::OP_ABS: case Op::OP_RECIP:
                todo.emplace_back(d->lhs().get(), false);
                break;
            case Op::OP_ADD: case Op::OP_SUB: case Op::OP_MUL: case Op::OP_DIV: case Op::OP_MIN: case Op::OP_MAX:
                todo.emplace_back(d->rhs().get(), false);
                todo.emplace_back(d->lhs().get(), false);
                break;
            default: return false;
        }
    }
    return true;
}

std::shared_ptr<MorphPlan> makeMorphPlan(const std::vector<libfive::Tree>& controls)
{
    namespace Op = libfive::Opcode;
    auto plan = std::make_shared<MorphPlan>();
    std::unordered_map<const void*, int> slot;
    try
    {
        for (const libfive::Tree& root : controls)
        {
            if (!root.is_valid()) return nullptr;
            std::vector<const libfive::Tree::Data*> order;
            if (!newNodes(root, slot, order)) return nullptr;
            for (const libfive::Tree::Data* d : order)
            {
                if (slot.count(d)) continue;
                MorphPlan::Node n;
                n.op = int(d->op());
                switch (d->op())
                {
                    case Op::CONSTANT: n.value = double(d->value()); break;
                    case Op::VAR_FREE: break;
                    case Op::CONST_VAR: case Op::OP_SQUARE: case Op::OP_SQRT: case Op::OP_NEG: case Op::OP_SIN: case Op::OP_COS: case Op::OP_ABS: case Op::OP_RECIP:
                        n.a = slot.at(d->lhs().id());
                        break;
                    case Op::OP_ADD: case Op::OP_SUB: case Op::OP_MUL: case Op::OP_DIV: case Op::OP_MIN: case Op::OP_MAX:
                        n.a = slot.at(d->lhs().id());
                        n.b = slot.at(d->rhs().id());
                        break;
                    default: return nullptr;                  // (an operation the direct reading does not do: the evaluator reads them)
                }
                const int index = int(plan->nodes.size());
                slot[d] = index;
                if (d->op() == Op::VAR_FREE) plan->vars.emplace_back(index, static_cast<libfive::Tree::Id>(d));
                plan->nodes.push_back(n);
            }
            plan->output.push_back(slot.at(root.get()));
        }
    }
    catch (const std::exception&)
    {
        return nullptr;
    }
    plan->value.assign(plan->nodes.size(), 0.0);
    for (size_t i = 0; i < plan->nodes.size(); ++i)
        if (plan->nodes[i].op == int(Op::CONSTANT)) plan->value[i] = plan->nodes[i].value;
    return plan;
}
}

bool Shape::morphDirect(const std::map<libfive::Tree::Id, float>& v, std::vector<Eigen::Vector3d>& at) const
{
    namespace Op = libfive::Opcode;
    static const bool legacy = std::getenv("FIELDES_MORPH_LEGACY") != nullptr;          // (for comparing with the evaluator that read them before)
    if (legacy || m_morph_plan_failed) return false;
    if (!m_morph_plan)
    {
        std::shared_ptr<MorphPlan> made = makeMorphPlan(m_morph_controls);
        if (!made)
        {
            m_morph_plan_failed = true;
            return false;
        }
        m_morph_plan = made;
        if (std::getenv("FIELDES_TIMING")) fprintf(stderr, "[morph] direct reading: %zu operations for %zu numbers\n", made->nodes.size(), made->output.size());
    }
    MorphPlan& plan = *static_cast<MorphPlan*>(m_morph_plan.get());
    for (const auto& var : plan.vars)
    {
        const auto it = v.find(var.second);
        if (it == v.end())                                       // (a number the evaluator above would have been told about: it reads them this time)
        {
            static bool told = false;
            if (!told && std::getenv("FIELDES_TIMING")) { told = true; fprintf(stderr, "[morph] direct reading: a variable of the control points is not in the map of numbers (%zu variables in the plan, %zu in the map): the evaluator reads them\n", plan.vars.size(), v.size()); }
            return false;
        }
        plan.value[size_t(var.first)] = double(it->second);
    }
    double* val = plan.value.data();
    const size_t count = plan.nodes.size();
    for (size_t i = 0; i < count; ++i)
    {
        const MorphPlan::Node& n = plan.nodes[i];
        switch (n.op)
        {
            case Op::CONSTANT: case Op::VAR_FREE: break;
            case Op::CONST_VAR: val[i] = val[n.a]; break;
            case Op::OP_SQUARE: val[i] = val[n.a] * val[n.a]; break;
            case Op::OP_SQRT: val[i] = std::sqrt(val[n.a]); break;
            case Op::OP_NEG: val[i] = -val[n.a]; break;
            case Op::OP_SIN: val[i] = std::sin(val[n.a]); break;
            case Op::OP_COS: val[i] = std::cos(val[n.a]); break;
            case Op::OP_ABS: val[i] = std::fabs(val[n.a]); break;
            case Op::OP_RECIP: val[i] = 1.0 / val[n.a]; break;
            case Op::OP_ADD: val[i] = val[n.a] + val[n.b]; break;
            case Op::OP_SUB: val[i] = val[n.a] - val[n.b]; break;
            case Op::OP_MUL: val[i] = val[n.a] * val[n.b]; break;
            case Op::OP_DIV: val[i] = val[n.a] / val[n.b]; break;
            case Op::OP_MIN: val[i] = val[n.a] < val[n.b] ? val[n.a] : val[n.b]; break;
            case Op::OP_MAX: val[i] = val[n.a] > val[n.b] ? val[n.a] : val[n.b]; break;
            default: return false;
        }
    }
    if (!m_morph_tape_out.empty())
    {
        // (H164: the numbers just worked out are the INPUTS of the tape -- the numbers of the points --; the control points are made from them by the tape's plain operations)
        const size_t ops = m_morph_tape_op.size();
        if (plan.tape.size() != ops) plan.tape.assign(ops, 0.0);
        double* t = plan.tape.data();
        for (size_t i = 0; i < ops; ++i)
        {
            const int a = m_morph_tape_a[i], b = m_morph_tape_b[i];
            switch (m_morph_tape_op[i])
            {
                case 0: { const size_t k = size_t(a); if (k >= plan.output.size()) return false; t[i] = val[plan.output[k]]; break; }
                case 1: t[i] = m_morph_tape_const[size_t(a)]; break;
                case 2: t[i] = t[a] + t[b]; break;
                case 3: t[i] = t[a] - t[b]; break;
                case 4: t[i] = t[a] * t[b]; break;
                case 5: t[i] = t[a] / t[b]; break;
                case 6: t[i] = t[a] < t[b] ? t[a] : t[b]; break;
                case 7: t[i] = t[a] > t[b] ? t[a] : t[b]; break;
                case 8: t[i] = std::sqrt(t[a] > 0.0 ? t[a] : 0.0); break;
                case 9: t[i] = t[a] * t[a]; break;
                case 10: t[i] = -t[a]; break;
                case 11: t[i] = std::sin(t[a]); break;
                case 12: t[i] = std::cos(t[a]); break;
                default: return false;
            }
        }
        for (size_t k = 0; k < m_morph_tape_out.size(); ++k)
        {
            const double x = t[m_morph_tape_out[k]];
            if (!std::isfinite(x)) return false;
            at[k / 3][int(k % 3)] = x;
        }
        return true;
    }
    for (size_t k = 0; k < plan.output.size(); ++k)
    {
        const double x = val[plan.output[k]];
        if (!std::isfinite(x)) return false;
        at[k / 3][int(k % 3)] = x;
    }
    return true;
}

bool Shape::morphNow(const std::map<libfive::Tree::Id, float>& v, std::vector<Eigen::Vector3f>& out) const
{
    static const bool off = std::getenv("FIELDES_NO_NATIVE") != nullptr;          // (for comparing with the field: every body of points is meshed from its field)
    if (off || !hasMorph()) return false;
    // (what colours or bends the shape is not a body of points' to draw)
    if (color_field.is_valid() || has_result || !m_channels.empty()) return false;
    // (each control point of the patches is worked out once; a vertex of the surface is a sum of a few of them, each times its weight)
    const size_t controls = m_morph_tape_out.empty() ? m_morph_controls.size() / 3 : m_morph_tape_out.size() / 3;
    std::vector<Eigen::Vector3d> at(controls);
    {
        // All the numbers of all the control points are the places (k, 0, 0) of ONE tree: the sum over k of hat(x - k) times number k, hat(d) = max(0, 1 - |d|), which is 1 at the whole number k and 0 at every
        // other whole number.  The tree and its evaluator are made once and kept: a step of a drag tells it the numbers that changed (setVar) and reads all of them in one pass over its tape, in batches
        // of the evaluator's width.  What was done before -- an evaluator for each number, built and zeroed again at each step -- cost 425 to 620 ms for a body of eight spline points.
        std::lock_guard<std::mutex> lock(m_morph_mutex);
        const bool direct = morphDirect(v, at);
        if (!direct && !m_morph_tape_out.empty()) return false;      // (H164: a body made on a tape has no graph for the evaluator below)
        if (!direct)
        {
        using Evaluator = libfive::ArrayEvaluator;
        int changedNumbers = 0;                 // (how many numbers this step told the evaluator: with FIELDES_TIMING it is printed, to see whether the ring of an influence is among them)
        // (the evaluator is made again only when a number that the control points are made of was not told to it -- not whenever the map has another size: the map of a drag can hold numbers of other things,
        // and a first step that made the evaluator again cost 200 to 770 ms, measured in the timing logs of the window runs: the lag at the start of a drag, the user, 2026-11-02, H56)
        bool remake = !m_morph_eval;
        if (!remake)
        {
            for (const auto& kv : v)
            {
                if (m_morph_last.count(kv.first)) continue;
                buildDeps();
                if (m_deps.count(kv.first)) { remake = true; break; }
            }
        }
        if (remake)
        {
            libfive::Tree sum = libfive::Tree(0.0);
            for (size_t k = 0; k < m_morph_controls.size(); ++k)
            {
                const libfive::Tree hat = libfive::max(libfive::Tree(0.0), libfive::Tree(1.0) - libfive::abs(libfive::Tree::X() - libfive::Tree(double(k))));
                sum = sum + hat * m_morph_controls[k];
            }
            m_morph_eval = std::make_shared<Evaluator>(sum, v);
            m_morph_last = v;
        }
        else
        {
            Evaluator* e = static_cast<Evaluator*>(m_morph_eval.get());
            for (const auto& kv : v)
            {
                auto it = m_morph_last.find(kv.first);
                if (it == m_morph_last.end())
                {
                    m_morph_last[kv.first] = kv.second;           // (a number the control points are not made of: nothing to tell the evaluator)
                    continue;
                }
                if (it->second != kv.second)
                {
                    e->setVar(kv.first, kv.second);
                    m_morph_last[kv.first] = kv.second;
                    ++changedNumbers;
                }
            }
        }
        if (changedNumbers > 0 && std::getenv("FIELDES_TIMING")) fprintf(stderr, "[morph] a step changed %d numbers\n", changedNumbers);
        Evaluator* e = static_cast<Evaluator*>(m_morph_eval.get());
        const size_t total = m_morph_controls.size();
        for (size_t start = 0; start < total; start += Evaluator::N)
        {
            const size_t count = std::min<size_t>(Evaluator::N, total - start);
            for (size_t j = 0; j < count; ++j) e->set(Eigen::Vector3f(float(start + j), 0.0f, 0.0f), j);
            const auto values = e->values(count);
            for (size_t j = 0; j < count; ++j)
            {
                const size_t k = start + j;
                const double x = double(values(Eigen::Index(j)));
                if (!std::isfinite(x)) return false;
                at[k / 3][int(k % 3)] = x;
            }
        }
        }       // (the evaluator above reads them only when the direct reading could not)
    }
    const size_t count = m_morph_recipe_start.size() - 1;
    out.resize(count);
    for (size_t i = 0; i < count; ++i)
    {
        Eigen::Vector3d sum = Eigen::Vector3d::Zero();
        for (uint32_t k = m_morph_recipe_start[i]; k < m_morph_recipe_start[i + 1]; ++k) sum += m_morph_recipe_weight[k] * at[m_morph_recipe_control[k]];
        out[i] = Eigen::Vector3f(float(sum.x()), float(sum.y()), float(sum.z()));
    }
    return true;
}

// The mesh of a body of points: the vertices of its surface as the numbers are now, and the triangles between them (the library wrote them counterclockwise seen from outside)
std::unique_ptr<libfive::Mesh> Shape::morphMesh(const std::vector<Eigen::Vector3f>& vertices, const std::vector<uint32_t>& triangles)
{
    auto out = std::make_unique<libfive::Mesh>();
    // (the first vertex of a libfive mesh is nobody's: the vertices start after it, and so do the indices of the triangles)
    const uint32_t off = uint32_t(out->verts.size());
    for (const auto& p : vertices) out->verts.push_back(p);
    using Tri = Eigen::Matrix<uint32_t, 3, 1>;
    for (size_t t = 0; t + 2 < triangles.size(); t += 3)
    {
        if (triangles[t] >= vertices.size() || triangles[t + 1] >= vertices.size() || triangles[t + 2] >= vertices.size()) return nullptr;
        out->branes.push_back(Tri(off + triangles[t], off + triangles[t + 1], off + triangles[t + 2]));
    }
    return out;
}

bool Shape::drawnFromNumbers() const
{
    if (hasMorph()) return !(color_field.is_valid() || has_result || !m_channels.empty());
    if (hasPrism())
    {
        std::vector<libfive::pointshapes::Control> cs;
        double pa = 0, pb = 0;
        return prismNow(vars, cs, pa, pb);
    }
    if (hasSweep())
    {
        SweepNumbers sn;
        return sweepNow(vars, sn);
    }
    return false;
}

// How many pieces a piece of a curve is cut in when a shape is drawn from the numbers of its points (the sweep, the prism): 14 at the scene's resolution 10, in proportion to it -- so that the resolution
// says how fine they are drawn, as it does for a shape that is meshed from its field, and as it does for a body of points (the user, 2026-11-02: "The path extrude ALSO DOES NOT REACT TO THE RESOLUTION",
// "you should have inferred that when you fixed it for morph shapes that that applies TO EVERYTHING").  At least 6, at most 40
static int curveSteps(double sceneResolution)
{
    if (!(sceneResolution > 0)) return 14;
    return std::max(6, std::min(40, int(std::lround(14.0 * sceneResolution / 10.0))));
}

// The mesh of a sweep: the outline of the profile (from the middle of its box) carried along the path, a ring of it at both ends of every piece of the path -- in the frame
// that piece has (the one the field of `path_extrude` has: carried along without turning about the path, turned by the points' rotations, scaled by their thickness) and cut
// by the planes that bisect the corners (square at the ends of an open path) -- and the walls between the rings; the profile's face closes an open path's ends
// THE FIELD OF A SWEEP AND OF A PRISM (H63, the user, 2026-11-02: "UNDERLYING SHOULD ALWAYS BE A FUNCTIONING FIELD THAT CAN BE INTERACTED WITH PROPERLY", and of the field of the path extrude: "THAT THE FIELD IS WAY TOO HEAVY
// AND YOU SHOULD IMPROVE IT ... I ACTUALLY MENTIONED SPECIFICALLY THAT YOU SHOULD IMPROVE THE PATH EXTRUDE AS WELL"): the mesh that the viewport draws a path extrude or an extrude from, as the vertices and triangles that
// libfive_mesh_from_arrays makes the exact distance field of the triangles of -- the field of a body of points is made so (morph.py _native_field).  An operation on the shape (a union, an offset, round_edges, the selection
// of an edge) reads that field, and the expression of the shape is not meshed.  The numbers are those the script has when it runs: a number that is dragged moves the picture of the viewport at once, and the field when the script
// runs again, as it does for a body of points
bool Shape::nativeMeshArrays(const std::map<libfive::Tree::Id, float>& v, std::vector<float>& xyz, std::vector<uint32_t>& tri) const
{
    std::unique_ptr<libfive::Mesh> m;
    if (hasSweep())
    {
        SweepNumbers sn;
        if (sweepNow(v, sn))
        {
            sn.steps = curveSteps(20.0);
            m = sweepMesh(sn);
        }
    }
    else if (hasPrism())
    {
        std::vector<libfive::pointshapes::Control> cs;
        double a = 0.0, b = 0.0;
        if (prismNow(v, cs, a, b)) m = prismMesh(cs, m_prism_default, a, b, curveSteps(20.0));
    }
    if (!m || m->verts.size() < 5 || m->branes.empty()) return false;
    // (the first vertex of a libfive mesh is nobody's: the vertices start after it, and so do the indices of the triangles)
    for (size_t i = 1; i < m->verts.size(); ++i)
    {
        xyz.push_back(m->verts[i].x());
        xyz.push_back(m->verts[i].y());
        xyz.push_back(m->verts[i].z());
    }
    for (const auto& t : m->branes)
    {
        if (t[0] == 0 || t[1] == 0 || t[2] == 0) continue;
        tri.push_back(t[0] - 1);
        tri.push_back(t[1] - 1);
        tri.push_back(t[2] - 1);
    }
    return !tri.empty();
}

std::unique_ptr<libfive::Mesh> Shape::sweepMesh(const SweepNumbers& in)
{
    const auto sweepStart = std::chrono::steady_clock::now();          // (the time of one sweep, printed at its end under FIELDES_SWEEP_DEBUG or FIELDES_TIMING: what a frame of a drag of a swept shape costs -- 2026-11-03, H52c: the cut made it 1.3 s and nothing showed in the log)
    using Vec = Eigen::Vector3d;
    // The profile in the coordinates of its own plane, from the middle of the box of its outline
    std::vector<Vec> where;
    for (const auto& c : in.profile) where.push_back(c.pos);
    // (the outline of a primitive is in the plane z = 0, its axes x and y: no plane is worked out of its corners.  In every other way it is a profile to the sweep)
    double frame[9] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    if (!in.free && !libfive::pointshapes::planeOf(where, frame)) return nullptr;
    const Vec o(frame[0], frame[1], frame[2]), pu(frame[3], frame[4], frame[5]), pv(frame[6], frame[7], frame[8]);
    const auto outline = libfive::pointshapes::polyline(in.profile, in.profileDefault, true, in.steps);
    if (outline.size() < 3) return nullptr;
    std::vector<Eigen::Vector2d> prof;
    double lox = 1e300, hix = -1e300, loy = 1e300, hiy = -1e300;
    for (const auto& p : outline)
    {
        prof.emplace_back((p - o).dot(pu), (p - o).dot(pv));
        lox = std::min(lox, prof.back().x());
        hix = std::max(hix, prof.back().x());
        loy = std::min(loy, prof.back().y());
        hiy = std::max(hiy, prof.back().y());
    }
    const double cx = 0.5 * (lox + hix), cy = 0.5 * (loy + hiy);
    std::vector<Vec> flat;
    double area = 0;
    for (auto& q : prof)
    {
        q.x() -= cx;
        q.y() -= cy;
        flat.push_back(Vec(q.x(), q.y(), 0));
    }
    const size_t E = prof.size();
    for (size_t i = 0; i < E; ++i) area += prof[i].x() * prof[(i + 1) % E].y() - prof[(i + 1) % E].x() * prof[i].y();
    static const double unitFrame[9] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    const auto cap = libfive::pointshapes::face(flat, unitFrame);                // (the triangles of the profile's face, as indices of its outline)
    if (!cap) return nullptr;
    // (a mesh of libfive has a first vertex that is nobody's: the loop's vertices are the ones after it, and so are the indices of the triangles)
    const uint32_t capFirst = uint32_t(cap->verts.size()) - uint32_t(E);

    // The path
    std::vector<std::pair<int, double>> owners;
    auto V = libfive::pointshapes::polyline(in.path, in.pathDefault, in.closed, in.steps, &owners);
    const int n = int(V.size());
    if (n < 2) return nullptr;
    // The sweep starts at the profile: the path says how it travels, not where from.  The first point of the path is the middle of the box of the profile (path_extrude of profile.py
    // moves the field the same way)
    {
        const Vec shift = (o + pu * cx + pv * cy) - V[0];
        for (auto& p : V) p += shift;
    }
    const bool closed = in.closed;
    const int pieces = closed ? n : n - 1;
    std::vector<Vec> T;
    T.resize(size_t(pieces));
    for (int k = 0; k < pieces; ++k)
    {
        const Vec d = V[size_t((k + 1) % n)] - V[size_t(k)];
        const double l = d.norm();
        T[size_t(k)] = l > 1e-12 ? Vec(d / l) : (k > 0 ? T[size_t(k - 1)] : Vec(Vec::UnitZ()));
    }
    // (what a bend does to the section is dealt with below, once the rings are made: the excess material is taken out of them)
    auto tangentAt = [&](int k) -> Vec {
        Vec t;
        if (closed) t = T[size_t((k + pieces - 1) % pieces)] + T[size_t(k % pieces)];
        else if (k == 0) return T[0];
        else if (k == n - 1) return T[size_t(pieces - 1)];
        else t = T[size_t(k - 1)] + T[size_t(k)];
        const double l = t.norm();
        return l > 1e-12 ? Vec(t / l) : T[size_t(std::min(k, pieces - 1))];
    };
    // The frames.  The profile is carried along the path as it is -- its two axes u and v, each next frame carried along without turning about the path (the double reflection of
    // Wang et al.) -- so the first ring is the profile itself, at whatever angle the path leaves it.  A profile whose plane holds the direction of the path (nothing could be swept) is
    // turned the least that puts it across the path (path_extrude of profile.py does the same)
    std::vector<Vec> Uf, Wf, N, B;
    Uf.resize(size_t(pieces));
    Wf.resize(size_t(pieces));
    N.resize(size_t(pieces));
    B.resize(size_t(pieces));
    {
        const Vec ns = pu.cross(pv).normalized();
        if (std::fabs(ns.dot(T[0])) >= 0.1)
        {
            Uf[0] = pu;
            Wf[0] = pv;
        }
        else
        {
            Vec n0 = pu - T[0] * pu.dot(T[0]);
            if (n0.squaredNorm() < 1e-12) n0 = pv - T[0] * pv.dot(T[0]);
            n0 = n0.norm() > 1e-12 ? Vec(n0.normalized()) : Vec(Vec::UnitX());
            Uf[0] = n0;
            Wf[0] = T[0].cross(n0);
        }
    }
    auto carried = [&](const Vec& w, int k) -> Vec {
        const Vec v1 = V[size_t(k + 1)] - V[size_t(k)];
        const double c1 = v1.dot(v1) + 1e-300;
        const Vec rl = w - v1 * (v1.dot(w) * 2 / c1);
        const Vec tl = T[size_t(k)] - v1 * (v1.dot(T[size_t(k)]) * 2 / c1);
        const Vec v2 = T[size_t(k + 1)] - tl;
        const double c2 = v2.dot(v2) + 1e-300;
        return rl - v2 * (v2.dot(rl) * 2 / c2);
    };
    for (int k = 0; k + 1 < pieces; ++k)
    {
        const Vec nu = carried(Uf[size_t(k)], k), nw = carried(Wf[size_t(k)], k);
        Uf[size_t(k + 1)] = nu.norm() > 1e-12 ? Vec(nu.normalized()) : Uf[size_t(k)];
        Wf[size_t(k + 1)] = nw.norm() > 1e-12 ? Vec(nw.normalized()) : Wf[size_t(k)];
    }
    // (the frame the twist is measured in, across the path: u across it -- v when u lies along it -- u itself when the profile is across the path)
    for (int k = 0; k < pieces; ++k)
    {
        Vec across = Uf[size_t(k)] - T[size_t(k)] * Uf[size_t(k)].dot(T[size_t(k)]);
        if (across.squaredNorm() < 1e-12) across = Wf[size_t(k)] - T[size_t(k)] * Wf[size_t(k)].dot(T[size_t(k)]);
        N[size_t(k)] = across.norm() > 1e-12 ? Vec(across.normalized()) : Vec(Vec::UnitX());
        B[size_t(k)] = T[size_t(k)].cross(N[size_t(k)]);
    }
    auto turned = [](const Vec& v, const Vec& t, double cs, double sn) -> Vec { return v * cs + t.cross(v) * sn + t * (t.dot(v) * (1.0 - cs)); };
    // The twist: the rotation of a point turns its normal about the path; between the points the angle runs along the line
    const int m = int(in.path.size());
    std::map<int, double> angleOf;
    for (int i = 0; i < m; ++i)
    {
        if (!in.path[size_t(i)].turned) continue;
        int vertex = -1;
        for (int j = 0; j < n; ++j) if (owners[size_t(j)].first == i && owners[size_t(j)].second == 0.0) { vertex = j; break; }
        if (vertex < 0) continue;
        const int k = std::min(vertex, pieces - 1);
        Vec r = N[size_t(k)];
        for (int a = 0; a < 3; ++a)
        {
            const double angle = in.path[size_t(i)].rotate[a];
            if (std::fabs(angle) < 1e-12) continue;
            const double rad = angle * M_PI / 180.0, c = std::cos(rad), s = std::sin(rad);
            if (a == 0) r = Vec(r.x(), r.y() * c - r.z() * s, r.y() * s + r.z() * c);
            else if (a == 1) r = Vec(r.x() * c + r.z() * s, r.y(), r.z() * c - r.x() * s);
            else r = Vec(r.x() * c - r.y() * s, r.x() * s + r.y() * c, r.z());
        }
        angleOf[i] = std::atan2(r.dot(B[size_t(k)]), r.dot(N[size_t(k)]));
    }
    auto angleAt = [&](int i) { const auto it = angleOf.find(i); return it == angleOf.end() ? 0.0 : it->second; };
    auto rollAt = [&](int j) {
        const int i = owners[size_t(j)].first;
        const double t = owners[size_t(j)].second;
        const double a0 = angleAt(i);
        const int i1 = (i + 1) % m;
        const double a1 = (closed || i + 1 < m) ? angleAt(i1) : a0;
        return a0 * (1 - t) + a1 * t;
    };
    auto sizeAt = [&](int j) {
        const int i = owners[size_t(j)].first;
        const double t = owners[size_t(j)].second;
        const double s0 = in.path[size_t(i)].scale;
        const int i1 = (i + 1) % m;
        const double s1 = (closed || i + 1 < m) ? in.path[size_t(i1)].scale : s0;
        return std::max(s0 * (1 - t) + s1 * t, 1e-3);
    };
    const double total = in.twist * M_PI / 180.0;

    auto out = std::make_unique<libfive::Mesh>();
    using Tri = Eigen::Matrix<uint32_t, 3, 1>;
    // The rings.  Two pieces that meet at a vertex of the path each have a ring there -- the profile in the frame of that piece, slid along the piece onto the plane that bisects the corner -- and the two are the
    // same only for a profile that is its own mirror image: the frames are carried along the path, not mirrored.  They used to be two sets of vertices, and where they differ the walls of the two pieces cross
    // or leave a slit: the thin dark lines inside the bends of a horn (measured in a window picture, 2026-11-02; the user, A8: "THERE SHOULD NOT BE A VISUAL INTERRUPTION HERE").  The surface is built on ONE
    // ring at each vertex of the path -- the mean of the two -- so that it is whole by construction, and the walls of a piece run from the ring at its start to the ring at its end
    std::vector<Vec> ringStart(size_t(pieces) * E), ringEnd(size_t(pieces) * E);
    std::vector<double> facingOf(size_t(pieces), 1.0);
    std::vector<Vec> planeNormal(size_t(pieces), Vec(0.0, 0.0, 1.0));       // (the normal of the plane of the profile in the frame of each piece: the plane of the rings at the ends of the path)
    for (int k = 0; k < pieces; ++k)
    {
        const int j = (k + 1) % n;
        const double phi = 0.5 * (rollAt(k) + rollAt(j)) + total;
        const double cs = std::cos(phi), sn = std::sin(phi);
        const Vec Tk = T[size_t(k)];
        const Vec nk = turned(Uf[size_t(k)], Tk, cs, sn), bk = turned(Wf[size_t(k)], Tk, cs, sn);
        // (which way the profile faces along the path: a profile that the path leaves backwards is swept the other way round)
        facingOf[size_t(k)] = nk.cross(bk).dot(Tk) >= 0 ? 1.0 : -1.0;
        planeNormal[size_t(k)] = nk.cross(bk).normalized();
        // (the ring at the start of the piece, then the one at its end: the profile in this piece's frame, slid along the piece onto the plane that bisects the corner)
        for (int end = 0; end < 2; ++end)
        {
            std::vector<Vec>& ring = end == 0 ? ringStart : ringEnd;
            const int at = end == 0 ? k : j;
            const Vec centre = V[size_t(at)];
            const double size = sizeAt(at);
            const Vec cut = tangentAt(at);
            const double slope = cut.dot(Tk);
            // (the ends of an open path are the profile itself -- the profile at the start, its copy at the end -- not cut square across the path)
            const bool freeEnd = !closed && ((end == 0 && k == 0) || (end == 1 && k == pieces - 1));
            size_t i = 0;
            for (const auto& q : prof)
            {
                Vec off = (nk * q.x() + bk * q.y()) * size;
                Vec p = centre + off;
                if (!freeEnd && std::fabs(slope) > 1e-6) p -= Tk * (cut.dot(p - centre) / slope);
                ring[size_t(k) * E + i] = p;
                ++i;
            }
        }
    }
    // THE EXCESS MATERIAL AT A TIGHT BEND IS TAKEN OUT AND THE SURFACE IS BUILT AROUND IT (the user, 2026-11-02, H52: "THE SOLUTION IS REMOVING THE ACCESS MATERIAL AND BUILDING AROUND IT CLEANLY.  THERE SHOULD NOT BE A
    // VISUAL INTERRUPTION HERE.  THAT IS THE REQUEST."; H52b: "THE APPROACH IS WRONG ... REDO IT" and "THE ISSUE IS WITH THE TASK THAT WAS GIVEN TO YOU" -- after a cap, a relaxation and a soft limit, which all press the
    // section to fit the bend and leave the material where it is).  Each point of the profile travels along the path as a line for each piece: its wall in that piece is the stretch of that line between the ring at the
    // start of the piece and the ring at its end.  Where the section is wider than the bend, the wall of a piece for the points inside the bend runs backwards or has hardly any length: that stretch is the excess, the
    // material of the section that lies inside the sections of the pieces next to it and is no part of the surface.  It is taken out: the piece has no wall for that point, and the lines of the piece before it and of the
    // piece after it meet in one point, where both rings have that point (the offset of a polyline with its backward stretches taken out, as the offset of an outline is cleaned).  What stays is the surface of the union
    // of the sections as far as the pieces next to each other go: no fold, no crease, nothing pressed flat.  The first and the last piece of an open path have nothing on their other side to be joined to; the cut
    // below holds those
    if (!in.plain && !std::getenv("FIELDES_SWEEP_RAW"))         // (a measurement: with it set the sweep is drawn as the profile carried along the path, nothing taken out, to put next to the result)
    {
        const std::vector<Vec> ringStart0 = ringStart, ringEnd0 = ringEnd;
        // Where the line of the piece `before` and the line of the piece `after` meet for the point `i` of the profile (the middle of their nearest points when they do not quite meet); false when they do
        // not meet, or meet far away (no spike is made of them).  `reach` is how far the pieces between them and next to them reach
        auto meet = [&](size_t i, int before, int after, double reach, Vec& point) -> bool {
            const Vec p1 = ringEnd[size_t(before) * E + i], p2 = ringStart[size_t(after) * E + i];
            const Vec t1 = T[size_t(before)], t2 = T[size_t(after)];
            const double cs = t1.dot(t2), det = cs * cs - 1.0;
            if (std::fabs(det) < 1e-9) return false;
            const Vec w = p2 - p1;
            const double u = (w.dot(t2) - cs * w.dot(t1)) / det;
            const double s = w.dot(t1) + cs * u;
            point = 0.5 * ((p1 + t1 * s) + (p2 + t2 * u));
            return (point - 0.5 * (p1 + p2)).norm() <= 3.0 * reach;
        };
        auto pieceLength = [&](int k) { return (V[size_t((k + 1) % n)] - V[size_t(k)]).norm(); };
        std::vector<std::vector<char>> goneAt(E, std::vector<char>(size_t(pieces), 0));
        for (size_t i = 0; i < E; ++i)
        {
            std::vector<char>& gone = goneAt[i];
            std::vector<int> work;
            for (int k = pieces - 1; k >= 0; --k) work.push_back(k);
            long guard = 0;
            while (!work.empty() && guard++ < 400000)
            {
                const int k = work.back();
                work.pop_back();
                if (gone[size_t(k)]) continue;
                const double length = pieceLength(k);
                const Vec a = ringStart[size_t(k) * E + i], b = ringEnd[size_t(k) * E + i];
                if ((b - a).dot(T[size_t(k)]) > 0.05 * length) continue;
                // (the nearest pieces before and after this one that are still there)
                int before = -1, after = -1;
                for (int d = 1; d < pieces && before < 0; ++d)
                {
                    int p = k - d;
                    if (closed) p = ((p % pieces) + pieces) % pieces;
                    else if (p < 0) break;
                    if (!gone[size_t(p)]) before = p;
                }
                for (int d = 1; d < pieces && after < 0; ++d)
                {
                    int q = k + d;
                    if (closed) q %= pieces;
                    else if (q >= pieces) break;
                    if (!gone[size_t(q)]) after = q;
                }
                if (before < 0 || after < 0 || before == after) continue;
                // (where the line of the piece before and the line of the piece after meet)
                Vec M;
                if (!meet(i, before, after, length + pieceLength(before) + pieceLength(after), M)) continue;
                ringEnd[size_t(before) * E + i] = M;
                ringStart[size_t(after) * E + i] = M;
                for (int r = (before + 1) % pieces; r != after; r = (r + 1) % pieces)
                {
                    ringStart[size_t(r) * E + i] = M;
                    ringEnd[size_t(r) * E + i] = M;
                    gone[size_t(r)] = 1;
                }
                work.push_back(before);
                work.push_back(after);
            }
        }
        // THE POINTS OF THE PROFILE NEXT TO EACH OTHER TAKE OUT THE SAME PIECES (the user, 2026-11-03, H52c: "The scarring looks just as ugly on this.  You did not improve it, still needs fixing."; measured on the horn, 416 pairs
        // of triangles that cross and a serrated band of teeth on the inner tube of the tight curl).  What is taken out was decided for each point of the profile on its own, so that a piece one point drops and its
        // neighbour keeps -- a hole in the mask of removed pieces, or one piece of it -- leaves the strip of walls between the two points to run from one line to another that are not the same: a fan of slivers, a tooth.  The
        // mask is made the median of three along the profile (a piece is out for a point when it is out for two of the three points about it), and every point whose pieces change is taken out again from its own
        // rings as they were made
        if (E >= 3)
        {
            std::vector<std::vector<char>> want = goneAt;
            long changed = 0;
            for (size_t i = 0; i < E; ++i)
            {
                for (int k = 0; k < pieces; ++k)
                {
                    const int votes = int(goneAt[(i + E - 1) % E][size_t(k)]) + int(goneAt[i][size_t(k)]) + int(goneAt[(i + 1) % E][size_t(k)]);
                    want[i][size_t(k)] = votes >= 2 ? 1 : 0;
                    if (want[i][size_t(k)] != goneAt[i][size_t(k)]) ++changed;
                }
            }
            if (changed)
            {
                for (size_t i = 0; i < E; ++i)
                {
                    if (want[i] == goneAt[i]) continue;
                    for (int k = 0; k < pieces; ++k)
                    {
                        ringStart[size_t(k) * E + i] = ringStart0[size_t(k) * E + i];
                        ringEnd[size_t(k) * E + i] = ringEnd0[size_t(k) * E + i];
                    }
                    const std::vector<char>& drop = want[i];
                    for (int k = 0; k < pieces; ++k)
                    {
                        if (!drop[size_t(k)]) continue;
                        const int prev = closed ? (k + pieces - 1) % pieces : k - 1;
                        if (prev >= 0 && drop[size_t(prev)]) continue;                 // (not the first piece of its run)
                        int last = k;
                        double reach = pieceLength(k);
                        while (true)
                        {
                            const int next = closed ? (last + 1) % pieces : last + 1;
                            if (next >= pieces || next == k || !drop[size_t(next)]) break;
                            last = next;
                            reach += pieceLength(last);
                        }
                        const int before = closed ? (k + pieces - 1) % pieces : k - 1;
                        const int after = closed ? (last + 1) % pieces : last + 1;
                        if (before < 0 || after >= pieces || before == after || drop[size_t(before)] || drop[size_t(after)]) continue;
                        Vec M;
                        if (!meet(i, before, after, reach + pieceLength(before) + pieceLength(after), M)) continue;
                        ringEnd[size_t(before) * E + i] = M;
                        ringStart[size_t(after) * E + i] = M;
                        for (int r = k;; r = (r + 1) % pieces)
                        {
                            ringStart[size_t(r) * E + i] = M;
                            ringEnd[size_t(r) * E + i] = M;
                            if (r == last) break;
                        }
                    }
                }
            }
            if (changed && std::getenv("FIELDES_SWEEP_DEBUG")) fprintf(stderr, "[sweep] %ld pieces were taken out (or kept) as the points of the profile about them do\n", changed);
            if (std::getenv("FIELDES_SWEEP_DEBUG"))
            {
                // (a measurement: for each point of the profile the runs of pieces that are out, "first-last"; a run that one point has and its neighbour has not is a hole in the mask, where a tooth can come from)
                for (size_t i = 0; i < E; ++i)
                {
                    std::string runs;
                    for (int k = 0; k < pieces; ++k)
                        if (want[i][size_t(k)] && (k == 0 || !want[i][size_t(k - 1)]))
                        {
                            int last = k;
                            while (last + 1 < pieces && want[i][size_t(last + 1)]) ++last;
                            runs += " " + std::to_string(k) + "-" + std::to_string(last);
                        }
                    fprintf(stderr, "[sweep] column %zu: pieces out:%s\n", i, runs.empty() ? " none" : runs.c_str());
                }
            }
        }
    }
    // (the ring at each vertex of the path: the mean of the end ring of the piece before it and the start ring of the piece after it; the two ends of an open path have the one they have)
    std::vector<uint32_t> ringBase = std::vector<uint32_t>(size_t(n));
    for (int v = 0; v < n; ++v)
    {
        ringBase[size_t(v)] = uint32_t(out->verts.size());
        const int before = closed ? (v + pieces - 1) % pieces : v - 1;
        const int after = closed ? v % pieces : v;
        for (size_t i = 0; i < E; ++i)
        {
            Vec p;
            if (before >= 0 && after < pieces) p = 0.5 * (ringEnd[size_t(before) * E + i] + ringStart[size_t(after) * E + i]);
            else if (after < pieces) p = ringStart[size_t(after) * E + i];
            else p = ringEnd[size_t(before) * E + i];
            out->verts.push_back(Eigen::Vector3f(float(p.x()), float(p.y()), float(p.z())));
        }
    }
    // What is left of a fold: a wall that the removal above could not take out -- the first or the last piece of an open path, with no piece on its other side to be joined to -- is not let run backwards: no point of a
    // ring is let lie behind the same point of the ring before it along the path.  A wall that has no length because its excess was taken out is left as it is
    {
        if (!closed && !std::getenv("FIELDES_SWEEP_RAW"))
        {
            long cut = 0;
            // THE ENDS OF AN OPEN PATH.  The profile at the start is the profile itself, at whatever angle the path leaves it, and the rings after it are cut square across the path, so where the profile leans
            // the rings that follow lie BEHIND the point the same column has in the first ring -- for as many rings as the lean is long (the wedge between the profile and the first section across the path).
            // The wall of such a column would run backwards from the face, through the material, and stand out as a flat lip at the root of a horn, longer the larger the section (the user, 2026-11-03, his
            // pictures 69 to 77; measured on his file: at twice the section 38 to 78 of the 168 columns of each of the first five rings lay behind the face, up to 0.37 mm).  What is behind the face is no part of
            // the sweep: it is put on the point of the face, so that a wall starts at the face; the same at the other end of the path, against the last ring
            long snapped = 0;
            if (pieces >= 2 && n >= 4)
            {
                const Eigen::Vector3f t0(float(T[0].x()), float(T[0].y()), float(T[0].z()));
                const Eigen::Vector3f t1(float(T[size_t(pieces - 1)].x()), float(T[size_t(pieces - 1)].y()), float(T[size_t(pieces - 1)].z()));
                // (a ring point put on the face is carried along the path by the way the path has gone since the face: the vertex of its ring minus the vertex of the ring the face is at)
                auto pathShift = [&](int ring, int from) {
                    const Vec d = V[size_t(ring)] - V[size_t(from)];
                    return Eigen::Vector3f(float(d.x()), float(d.y()), float(d.z()));
                };
                const uint32_t firstRing = ringBase[0], lastRing = ringBase[size_t(n - 1)];
                int quiet = 0;
                for (int v = 1; v < n - 1 && v < 200 && quiet < 3; ++v)
                {
                    long here = 0;
                    for (size_t i = 0; i < E; ++i)
                        if ((out->verts[ringBase[size_t(v)] + i] - out->verts[firstRing + i]).dot(t0) < 0.0f) { out->verts[ringBase[size_t(v)] + i] = out->verts[firstRing + i] + pathShift(v, 0); ++here; }
                    snapped += here;
                    quiet = here ? 0 : quiet + 1;
                }
                quiet = 0;
                for (int v = n - 2; v > 0 && v > n - 202 && quiet < 3; --v)
                {
                    long here = 0;
                    for (size_t i = 0; i < E; ++i)
                        if ((out->verts[lastRing + i] - out->verts[ringBase[size_t(v)] + i]).dot(t1) < 0.0f) { out->verts[ringBase[size_t(v)] + i] = out->verts[lastRing + i] + pathShift(v, n - 1); ++here; }
                    snapped += here;
                    quiet = here ? 0 : quiet + 1;
                }
            }
            if (snapped && std::getenv("FIELDES_SWEEP_DEBUG")) fprintf(stderr, "[sweep] %ld ring points that lay behind the face at an end of the path were put on it\n", snapped);
            for (int v = 0; v < pieces; ++v)
            {
                // (the plain sweep keeps its bends as the profile carries them and holds only the first and the last piece, which have no piece on their other side to be joined to: without it their walls run backwards and stand out as a flat lip at the root end of the horn, the user, 2026-11-03, his pictures 69 and 70; FIELDES_SWEEP_NOENDCUT=1 leaves them out, to put next to the result)
                if (in.plain && ((v != 0 && v != pieces - 1) || std::getenv("FIELDES_SWEEP_NOENDCUT"))) continue;
                const Eigen::Vector3f ahead(float(T[size_t(v)].x()), float(T[size_t(v)].y()), float(T[size_t(v)].z()));
                const float least = 0.05f * float((V[size_t(v + 1)] - V[size_t(v)]).norm());
                const uint32_t a = ringBase[size_t(v)], b = ringBase[size_t(v + 1)];
                for (size_t i = 0; i < E; ++i)
                {
                    const float along = (out->verts[b + i] - out->verts[a + i]).dot(ahead);
                    if (along < 0.0f)
                    {
                        out->verts[b + i] += ahead * (least - along);
                        ++cut;
                    }
                }
            }
            if (cut && std::getenv("FIELDES_SWEEP_DEBUG")) fprintf(stderr, "[sweep] %ld ring points were held ahead of the ring before them (folds cut)\n", cut);
        }
    }
    // The rounded ends (the user, 2026-10-28, RE1: "Spline edges at the ends of path extrusions should have rounded ends", and "If an end point is a spline point it is rounded off" -- a corner or a round corner at the end
    // stays the flat cutoff).  The end ring of an open path whose end point is a spline point is closed by a dome: `domeRings` rings, each the end ring shrunk toward the point of the path by cos and carried on along the
    // path by D sin, the angle running a quarter turn, the last ring one point.  D is half the thinner side of the box of the profile, times its size at the point and the influence of the point.  The ring is carried
    // along the path -- sheared, as the walls of a slanted end run -- so that the cross-section at a distance s past the end plane is the profile shrunk by sqrt(1 - (s / D)^2): exactly what the field of path_extrude
    // (profile.py) makes, so that the field and the mesh are the one shape.  This is done after the rings are made smooth, from the rings as they are
    const int domeRings = 12;
    struct Dome
    {
        bool on = false;
        uint32_t first = 0;             // (the first vertex of the first ring after the end ring; the rings follow one another, `E` vertices each)
        uint32_t apex = 0;
    };
    Dome dome[2];                       // (0: the start of the path, 1: its end)
    if (!closed)
    {
        const double thin = 0.5 * std::min(hix - lox, hiy - loy);
        for (int e = 0; e < 2; ++e)
        {
            const int v = e == 0 ? 0 : n - 1;
            const int piece = e == 0 ? 0 : pieces - 1;
            const auto& endPoint = in.path[e == 0 ? size_t(0) : in.path.size() - 1];
            const bool spline = endPoint.smooth == 1 || (endPoint.smooth < 0 && in.pathDefault);
            const double depth = thin * sizeAt(v) * std::max(0.0, endPoint.influence);
            if (!spline || !(depth > 1e-9)) continue;
            // (the normal of the end plane that leaves the sweep: forward at the end of the path, backward at its start)
            Vec outward = planeNormal[size_t(piece)];
            if (outward.dot(T[size_t(piece)]) < 0) outward = -outward;
            if (e == 0) outward = -outward;
            const double lean = std::max(0.1, std::fabs(T[size_t(piece)].dot(outward)));
            const Vec advance = (e == 0 ? Vec(-T[size_t(piece)]) : Vec(T[size_t(piece)])) / lean;
            dome[e].on = true;
            dome[e].first = uint32_t(out->verts.size());
            for (int j = 1; j < domeRings; ++j)
            {
                const double angle = 0.5 * M_PI * double(j) / double(domeRings);
                const double shrink = std::cos(angle), height = depth * std::sin(angle);
                for (size_t i = 0; i < E; ++i)
                {
                    const Eigen::Vector3f r = out->verts[ringBase[size_t(v)] + i];
                    const Vec q = Vec(double(r.x()), double(r.y()), double(r.z())) - V[size_t(v)];
                    const Vec p = V[size_t(v)] + q * shrink + advance * height;
                    out->verts.push_back(Eigen::Vector3f(float(p.x()), float(p.y()), float(p.z())));
                }
            }
            dome[e].apex = uint32_t(out->verts.size());
            const Vec tip = V[size_t(v)] + advance * depth;
            out->verts.push_back(Eigen::Vector3f(float(tip.x()), float(tip.y()), float(tip.z())));
        }
    }
    for (int k = 0; k < pieces; ++k)
    {
        const double facing = facingOf[size_t(k)];
        const uint32_t base = ringBase[size_t(k)], next = ringBase[size_t((k + 1) % n)];
        for (size_t i = 0; i < E; ++i)
        {
            const uint32_t a = base + uint32_t(i), b = base + uint32_t((i + 1) % E), c = next + uint32_t((i + 1) % E), d = next + uint32_t(i);
            if (area * facing >= 0)
            {
                out->branes.push_back(Tri(a, b, c));
                out->branes.push_back(Tri(a, c, d));
            }
            else
            {
                out->branes.push_back(Tri(a, c, b));
                out->branes.push_back(Tri(a, d, c));
            }
        }
        // (the triangles of the face face the way the profile's own normal does: the start cap faces back along the path, the end cap on along it)
        if (!closed && k == 0 && !dome[0].on)
            for (const Tri& t : cap->branes)
                out->branes.push_back(facing > 0 ? Tri(base + t[0] - capFirst, base + t[2] - capFirst, base + t[1] - capFirst)
                                                 : Tri(base + t[0] - capFirst, base + t[1] - capFirst, base + t[2] - capFirst));
        if (!closed && k == pieces - 1 && !dome[1].on)
            for (const Tri& t : cap->branes)
                out->branes.push_back(facing > 0 ? Tri(next + t[0] - capFirst, next + t[1] - capFirst, next + t[2] - capFirst)
                                                 : Tri(next + t[0] - capFirst, next + t[2] - capFirst, next + t[1] - capFirst));
    }
    // The triangles of the domes: a strip of walls between each two rings, in the order of the path (at its start the rings are taken from the tip back to the start ring), so that they face as the walls of the piece do,
    // and a fan between the last ring and the tip
    auto strip = [&](uint32_t base, uint32_t next, double facing) {
        for (size_t i = 0; i < E; ++i)
        {
            const uint32_t a = base + uint32_t(i), b = base + uint32_t((i + 1) % E), c = next + uint32_t((i + 1) % E), d = next + uint32_t(i);
            if (area * facing >= 0)
            {
                out->branes.push_back(Tri(a, b, c));
                out->branes.push_back(Tri(a, c, d));
            }
            else
            {
                out->branes.push_back(Tri(a, c, b));
                out->branes.push_back(Tri(a, d, c));
            }
        }
    };
    for (int e = 0; e < 2; ++e)
    {
        if (!dome[e].on) continue;
        const int v = e == 0 ? 0 : n - 1;
        const double facing = facingOf[size_t(e == 0 ? 0 : pieces - 1)];
        const bool forward = area * facing >= 0;
        std::vector<uint32_t> ringsAlong;
        ringsAlong.push_back(ringBase[size_t(v)]);
        for (int j = 1; j < domeRings; ++j) ringsAlong.push_back(dome[e].first + uint32_t(size_t(j - 1) * E));
        if (e == 0) std::reverse(ringsAlong.begin(), ringsAlong.end());
        for (size_t j = 0; j + 1 < ringsAlong.size(); ++j) strip(ringsAlong[j], ringsAlong[j + 1], facing);
        if (e == 1)
        {
            const uint32_t last = ringsAlong.back();
            for (size_t i = 0; i < E; ++i)
            {
                const uint32_t a = last + uint32_t(i), b = last + uint32_t((i + 1) % E);
                out->branes.push_back(forward ? Tri(a, b, dome[e].apex) : Tri(a, dome[e].apex, b));
            }
        }
        else
        {
            const uint32_t first = ringsAlong.front();
            for (size_t i = 0; i < E; ++i)
            {
                const uint32_t c = first + uint32_t((i + 1) % E), d = first + uint32_t(i);
                out->branes.push_back(forward ? Tri(dome[e].apex, c, d) : Tri(dome[e].apex, d, c));
            }
        }
    }
    // THE VERTICES THAT THE REMOVAL ABOVE PUT AT ONE POINT ARE ONE VERTEX (the user, 2026-11-02, H52b: the plain sweep has the right look and only has mesh intersections, "UNACCEPTABLE IN A FUCKING ENGINEERING SOFTWARE MADE
    // FOR SIMULATIONS": nothing but the intersections is changed).  The rings that the removal put at one point -- the same point of the profile at two rings that follow each other, the same position -- are welded, and a
    // triangle that is left with two corners at one vertex is no triangle and is dropped: the mesh stays one closed surface and has no face of no area.  Nothing else moves
    {
        std::vector<uint32_t> same(out->verts.size());
        for (uint32_t v = 0; v < uint32_t(same.size()); ++v) same[v] = v;
        // (H52c, the serrated band of teeth on the inside of the tight curl of the horn: where the tube bends tighter than it is wide the rings pile up on the inside and the strip of walls between two rings is a row of
        // slivers -- long along the profile, almost nothing along the path.  A ring pair whose rings are at some points of the profile much closer than at most of them -- under 0.3 of the median distance of
        // the pair -- is welded there as well, so that a sliver is no triangle and is dropped below; FIELDES_SWEEP_NOSLIVER=1 leaves it out, to compare the two)
        const bool noSliver = std::getenv("FIELDES_SWEEP_NOSLIVER") != nullptr;
        size_t nearWelded = 0;
        for (int k = 0; k < pieces; ++k)
        {
            const int j = (k + 1) % n;
            if (j < k) continue;                                        // (the ring that closes a closed path: the first one stays)
            std::vector<float> gap(E);
            for (size_t i = 0; i < E; ++i) gap[i] = (out->verts[ringBase[size_t(j)] + uint32_t(i)] - out->verts[ringBase[size_t(k)] + uint32_t(i)]).norm();
            std::vector<float> middle(gap);
            std::nth_element(middle.begin(), middle.begin() + long(E / 2), middle.end());
            const float close = noSliver ? 0.0f : 0.3f * middle[E / 2];
            for (size_t i = 0; i < E; ++i)
            {
                const uint32_t a = ringBase[size_t(k)] + uint32_t(i), b = ringBase[size_t(j)] + uint32_t(i);
                if (out->verts[same[a]] == out->verts[b]) same[b] = same[a];
                else if (close > 0.0f && (out->verts[same[a]] - out->verts[b]).norm() <= close) { same[b] = same[a]; ++nearWelded; }
            }
        }
        if (nearWelded && std::getenv("FIELDES_SWEEP_DEBUG")) fprintf(stderr, "[sweep] %zu vertices of rings much closer than most of their ring pair welded (the slivers of a tight bend)\n", nearWelded);
        std::vector<Tri> kept;
        kept.reserve(out->branes.size());
        for (const auto& tri : out->branes)
        {
            const Tri m(same[tri[0]], same[tri[1]], same[tri[2]]);
            if (m[0] != m[1] && m[1] != m[2] && m[0] != m[2]) kept.push_back(m);
        }
        const size_t dropped = out->branes.size() - kept.size();
        out->branes.clear();
        for (const Tri& m : kept) out->branes.push_back(m);
        if (dropped && std::getenv("FIELDES_SWEEP_DEBUG")) fprintf(stderr, "[sweep] %zu triangles of no area dropped after the removal of the crossing parts\n", dropped);
    }
    if (std::getenv("FIELDES_SWEEP_DEBUG"))
    {
        // (the number of pairs of triangles that cross each other -- two that share a corner are not counted: what a simulation cannot have.  Every edge of one against the other triangle, in a grid of the boxes of the triangles)
        const size_t count = out->branes.size();
        if (count > 0 && count < 400000)
        {
            std::vector<Vec> pts(out->verts.size());
            for (size_t v = 0; v < pts.size(); ++v) pts[v] = Vec(double(out->verts[v].x()), double(out->verts[v].y()), double(out->verts[v].z()));
            double mean = 0.0;
            for (const auto& tri : out->branes) mean += (pts[tri[0]] - pts[tri[1]]).norm();
            const double cell = std::max(2.0 * mean / double(count), 1e-9);
            std::unordered_map<long long, std::vector<uint32_t>> grid;
            auto cellKey = [](long long x, long long y, long long z) { return (x * 73856093LL) ^ (y * 19349663LL) ^ (z * 83492791LL); };
            for (uint32_t t = 0; t < uint32_t(count); ++t)
            {
                const auto& tri = out->branes[t];
                const Vec lo = pts[tri[0]].cwiseMin(pts[tri[1]]).cwiseMin(pts[tri[2]]), hi = pts[tri[0]].cwiseMax(pts[tri[1]]).cwiseMax(pts[tri[2]]);
                long long a[3], z[3];
                for (int d = 0; d < 3; ++d) { a[d] = (long long)std::floor(lo[d] / cell); z[d] = std::min(a[d] + 16, (long long)std::floor(hi[d] / cell)); }
                for (long long x = a[0]; x <= z[0]; ++x)
                    for (long long y = a[1]; y <= z[1]; ++y)
                        for (long long w = a[2]; w <= z[2]; ++w) grid[cellKey(x, y, w)].push_back(t);
            }
            auto pierces = [&](const Vec& p, const Vec& q, uint32_t tb) -> bool {
                const auto& tri = out->branes[tb];
                const Vec& a0 = pts[tri[0]];
                const Vec e1 = pts[tri[1]] - a0, e2 = pts[tri[2]] - a0, d = q - p, h = d.cross(e2);
                const double det = e1.dot(h);
                if (std::fabs(det) < 1e-18) return false;
                const double f = 1.0 / det;
                const Vec s = p - a0;
                const double u = f * s.dot(h);
                if (u < 1e-6 || u > 1.0 - 1e-6) return false;
                const Vec qv = s.cross(e1);
                const double v = f * d.dot(qv);
                if (v < 1e-6 || u + v > 1.0 - 1e-6) return false;
                const double tt = f * e2.dot(qv);
                return tt > 1e-6 && tt < 1.0 - 1e-6;
            };
            std::unordered_set<unsigned long long> seen;
            long crossings = 0, listed = 0;
            std::unordered_map<long, long> byRing, byColumn;       // (a measurement: where along the path and at which point of the profile the crossing pairs are, by the lowest vertex of each triangle)
            for (const auto& cellTris : grid)
            {
                const auto& list = cellTris.second;
                for (size_t x = 0; x < list.size(); ++x)
                    for (size_t y = x + 1; y < list.size(); ++y)
                    {
                        const uint32_t ta = std::min(list[x], list[y]), tb = std::max(list[x], list[y]);
                        if (ta == tb || !seen.insert((unsigned long long)ta << 32 | tb).second) continue;
                        const auto& A = out->branes[ta];
                        const auto& B = out->branes[tb];
                        bool shares = false;
                        for (int i = 0; i < 3 && !shares; ++i) for (int j = 0; j < 3; ++j) if (A[i] == B[j]) { shares = true; break; }
                        if (shares) continue;
                        bool cross = false;
                        for (int i = 0; i < 3 && !cross; ++i) cross = pierces(pts[A[i]], pts[A[(i + 1) % 3]], tb) || pierces(pts[B[i]], pts[B[(i + 1) % 3]], ta);
                        if (cross)
                        {
                            ++crossings;
                            const uint32_t ia = std::min(A[0], std::min(A[1], A[2])), ib = std::min(B[0], std::min(B[1], B[2])), rings = uint32_t(n) * uint32_t(E);
                            const long ra = ia < rings ? long(ia / E) : -1, rb = ib < rings ? long(ib / E) : -1;
                            ++byRing[ra];
                            ++byRing[rb];
                            ++byColumn[ia < rings ? long(ia % E) : -1];
                            ++byColumn[ib < rings ? long(ib % E) : -1];
                            if (listed++ < 12)
                            {
                                const Vec at = (pts[A[0]] + pts[A[1]] + pts[A[2]]) / 3.0;
                                fprintf(stderr, "[sweep]   crossing %ld: rings %ld and %ld, columns %ld and %ld, near (%.3f %.3f %.3f)\n", listed, ra, rb, ia < rings ? long(ia % E) : -1L, ib < rings ? long(ib % E) : -1L, at.x(), at.y(), at.z());
                            }
                        }
                    }
            }
            fprintf(stderr, "[sweep] %ld pairs of triangles cross each other (of %zu triangles)\n", crossings, count);
            if (crossings)
            {
                for (int which = 0; which < 2; ++which)
                {
                    std::vector<std::pair<long, long>> sorted((which == 0 ? byRing : byColumn).begin(), (which == 0 ? byRing : byColumn).end());
                    std::sort(sorted.begin(), sorted.end());
                    std::string text;
                    for (const auto& pr : sorted) text += " " + std::to_string(pr.first) + ":" + std::to_string(pr.second);
                    fprintf(stderr, "[sweep] crossings by %s (%s: triangles):%s\n", which == 0 ? "ring along the path" : "point of the profile", which == 0 ? "ring" : "column", text.c_str());
                }
            }
        }
    }
    if (std::getenv("FIELDES_SWEEP_DEBUG"))
    {
        double longest = 0;
        uint32_t worst = 0, highest = 0;
        for (size_t t = 0; t < out->branes.size(); ++t)
        {
            const auto& tri = out->branes[t];
            for (int e = 0; e < 3; ++e)
            {
                highest = std::max(highest, tri[e]);
                const double l = (out->verts[tri[e]] - out->verts[tri[(e + 1) % 3]]).norm();
                if (l > longest) { longest = l; worst = uint32_t(t); }
            }
        }
        const auto& tri = out->branes[worst];
        fprintf(stderr, "[sweep] %zu vertices, %zu triangles, highest index %u; longest edge %.2f in triangle %u: (%.1f %.1f %.1f) (%.1f %.1f %.1f) (%.1f %.1f %.1f); ring %zu, pieces %d\n", out->verts.size(), out->branes.size(),
                highest, longest, worst, out->verts[tri[0]].x(), out->verts[tri[0]].y(), out->verts[tri[0]].z(), out->verts[tri[1]].x(), out->verts[tri[1]].y(), out->verts[tri[1]].z(),
                out->verts[tri[2]].x(), out->verts[tri[2]].y(), out->verts[tri[2]].z(), E, pieces);
    }
    // (FIELDES_SWEEP_DUMP=<file>: the mesh as it is, as an OBJ, to look at it outside the program -- a measurement of the teeth of H52c on the real mesh)
    if (const char* dump = std::getenv("FIELDES_SWEEP_DUMP"))
    {
        static std::atomic<int> dumpCount{0};
        const std::string dumpName = std::string(dump) + "." + std::to_string(++dumpCount) + ".obj";          // (one file for every sweep: the sweeps of one render, the shape and its mirror image, wrote one file at the same time and corrupted it)
        if (FILE* file = fopen(dumpName.c_str(), "w"))
        {
            for (const auto& p : out->verts) fprintf(file, "v %.6f %.6f %.6f\n", double(p.x()), double(p.y()), double(p.z()));
            for (const auto& t : out->branes) fprintf(file, "f %u %u %u\n", unsigned(t[0]) + 1u, unsigned(t[1]) + 1u, unsigned(t[2]) + 1u);
            fclose(file);
        }
    }
    // Placed by the gizmo: scaled about its point, turned about x, then y, then z (degrees), then moved -- as the placement arguments place a shape
    if (in.placed)
    {
        const Eigen::Matrix3d R = (Eigen::AngleAxisd(in.rotate.z() * M_PI / 180.0, Vec::UnitZ()) * Eigen::AngleAxisd(in.rotate.y() * M_PI / 180.0, Vec::UnitY()) *
                                   Eigen::AngleAxisd(in.rotate.x() * M_PI / 180.0, Vec::UnitX())).toRotationMatrix();
        for (auto& p : out->verts)
        {
            const Vec q = Vec(p.x(), p.y(), p.z()) - in.about;
            const Vec placed = in.about + in.move + R * Vec(q.x() * in.scale.x(), q.y() * in.scale.y(), q.z() * in.scale.z());
            p = Eigen::Vector3f(float(placed.x()), float(placed.y()), float(placed.z()));
        }
    }
    if (std::getenv("FIELDES_SWEEP_DEBUG") || std::getenv("FIELDES_TIMING")) fprintf(stderr, "[sweep] sweepMesh took %.1f ms (%d rings, %zu columns, %zu triangles)\n", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - sweepStart).count(), int(n), size_t(E), size_t(out->branes.size()));
    return out;
}

void Shape::setControls(std::vector<ControlPoint> points, bool smoothDefault, bool closed, bool face)
{
    m_controls = std::move(points);
    m_controls_default = smoothDefault;
    m_controls_closed = closed;
    m_controls_face = face;
}

bool Shape::controlsNow(std::vector<libfive::pointshapes::Control>& out) const
{
    return controlNumbers(vars, out);
}

bool Shape::flatPlane(double frame[9]) const
{
    if (isFace() && planeNow(frame)) return true;
    if (!isFlat()) return false;
    static const double z0[9] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    for (int i = 0; i < 9; ++i) frame[i] = m_frame_now_valid ? m_frame_now[i] : z0[i];
    return true;
}

bool Shape::patchPlane(double frame[9]) const
{
    // The triangles of the mesh whose corners are all on the patch (the value at a vertex at most the cutoff: see setColorCutoff) are the patch.  Each is an area, as a vector (the mesh is
    // counter-clockwise from outside).  The plane that the patch covers the most area of is the one whose normal is the most along the normals of its triangles, weighted by their areas:
    // the strongest direction of the sum of area n n^T (for a flat face that is its normal, and for a bent one the one most of it faces)
    if (!has_cutoff || !mesh || color_values.size() < mesh->verts.size()) return false;
    Eigen::Matrix3d moment = Eigen::Matrix3d::Zero();
    Eigen::Vector3d sum = Eigen::Vector3d::Zero(), centre = Eigen::Vector3d::Zero();
    double area = 0;
    for (const auto& t : mesh->branes)
    {
        if (!(color_values[t[0]] <= color_cutoff && color_values[t[1]] <= color_cutoff && color_values[t[2]] <= color_cutoff)) continue;
        const Eigen::Vector3d a = mesh->verts[t[0]].cast<double>(), b = mesh->verts[t[1]].cast<double>(), c = mesh->verts[t[2]].cast<double>();
        const Eigen::Vector3d vector = 0.5 * (b - a).cross(c - a);
        const double w = vector.norm();
        if (!(w > 0)) continue;
        const Eigen::Vector3d n = vector / w;
        moment += w * n * n.transpose();
        sum += vector;
        centre += w * (a + b + c) / 3.0;
        area += w;
    }
    if (!(area > 0)) return false;
    Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> solver(moment);
    Eigen::Vector3d n = solver.eigenvectors().col(2);            // (the largest eigenvalue's: they are in increasing order)
    if (n.dot(sum) < 0) n = -n;                                  // (the side the patch faces)
    // (a plane through the middle of the patch with u and v across it: u is perpendicular to the normal and to the axis it is the least along)
    int axis = 0;
    for (int i = 1; i < 3; ++i)
        if (std::fabs(n[i]) < std::fabs(n[axis])) axis = i;
    Eigen::Vector3d e = Eigen::Vector3d::Zero();
    e[axis] = 1.0;
    const Eigen::Vector3d u = e.cross(n).normalized();
    const Eigen::Vector3d v = n.cross(u);
    const Eigen::Vector3d o = centre / area;
    for (int i = 0; i < 3; ++i)
    {
        frame[i] = o[i];
        frame[3 + i] = u[i];
        frame[6 + i] = v[i];
    }
    return true;
}

bool Shape::planeNow(double frame[9]) const
{
    std::vector<libfive::pointshapes::Control> cs;
    if (!isFace() || !controlsNow(cs)) return false;
    std::vector<Eigen::Vector3d> p;
    for (const auto& c : cs) p.push_back(c.pos);
    return libfive::pointshapes::planeOf(p, frame);
}

bool Shape::tangentAt(int i, QVector3D& out) const
{
    std::vector<libfive::pointshapes::Control> cs;
    if (i < 0 || !controlsNow(cs) || i >= int(cs.size())) return false;
    const auto t = libfive::pointshapes::tangents(cs, m_controls_default, m_controls_closed);
    const Eigen::Vector3d& v = t[size_t(i)];
    out = QVector3D(float(v.x()), float(v.y()), float(v.z()));
    return out.length() > 1e-9f;
}

int Shape::controlAt(const QVector3D& p) const
{
    for (size_t i = 0; i < m_controls.size(); ++i)
    {
        bool match = true;
        for (int a = 0; a < 3 && match; ++a)
        {
            const auto* number = m_controls[i].pos[a].is_valid() ? std::get_if<libfive::TreeConstant>(m_controls[i].pos[a].get()) : nullptr;
            match = number && std::fabs(double(number->value) - double(p[a])) <= 1e-4 * (1.0 + std::fabs(double(p[a])));
        }
        if (match) return int(i);
    }
    return -1;
}

int Shape::controlOf(const Shape* point) const
{
    if (!point) return -1;
    const auto& g = point->gizmo();
    for (int i = 0; i < int(m_controls.size()); ++i)
    {
        for (int a = 0; a < 3; ++a)
        {
            if (g.move[a] && m_controls[size_t(i)].var[a] == g.move[a]) return i;
        }
    }
    return -1;
}

// Which control of this sweep -- of its profile (counted first) or of its path -- a point is: the one whose number the point's gizmo moves, or -1
int Shape::sweepControlOf(const Shape* point) const
{
    if (!point || !hasSweep()) return -1;
    const auto& g = point->gizmo();
    int base = 0;
    for (const auto* list : {&m_sweep_profile, &m_sweep_path})
    {
        for (int i = 0; i < int(list->size()); ++i)
        {
            for (int a = 0; a < 3; ++a)
            {
                if (g.move[a] && (*list)[size_t(i)].var[a] == g.move[a]) return base + i;
            }
        }
        base += int(list->size());
    }
    return -1;
}

// Whether a point is one of the points a body of points -- morph_polyhedron, morph_body -- is made of: a number its gizmo moves is one of the numbers the control points of the body are made of
bool Shape::morphUses(const Shape* point) const
{
    // (H164, measured 2026-11-09 in the window: a body meshed from its field has no drawing data, so hasMorph() is false for it, and then no point was "a point of a body": the wheel over a point zoomed the view
    // instead of changing its influence.  A body of points says so itself -- `_morph_faces` -> isPointBody -- and the numbers its points move are in its field)
    if (!point || !(hasMorph() || isPointBody())) return false;
    const auto& g = point->gizmo();
    for (int a = 0; a < 3; ++a)
    {
        if (g.move[a] && usesVar(g.move[a])) return true;
    }
    return false;
}

int Shape::controlAfterPiece(int piece) const
{
    std::vector<libfive::pointshapes::Control> cs;
    if (!controlsNow(cs)) return -1;
    std::vector<std::pair<int, double>> owners;
    libfive::pointshapes::polyline(cs, m_controls_default, m_controls_closed, 14, &owners);
    return piece >= 0 && piece < int(owners.size()) ? owners[size_t(piece)].first : -1;
}

int Shape::arcControlOfPiece(int piece) const
{
    std::vector<libfive::pointshapes::Control> cs;
    if (!isFace() || !controlsNow(cs)) return -1;
    std::vector<std::pair<int, double>> owners;
    libfive::pointshapes::polyline(cs, m_controls_default, m_controls_closed, 14, &owners);
    const int n = int(owners.size());
    if (n < 3 || piece < 0 || piece >= n) return -1;
    // (every vertex of an arc belongs to its control, at 0: the piece between two of them is a piece of the arc -- a straight piece joins vertices of two controls, and a control that
    // is not round has one vertex)
    const auto& a = owners[size_t(piece)];
    const auto& b = owners[size_t((piece + 1) % n)];
    if (a.first != b.first || a.second != 0.0 || b.second != 0.0) return -1;
    for (const auto& f : libfive::pointshapes::fillets(cs, m_controls_default, m_controls_closed))
        if (f.control == a.first) return a.first;
    return -1;
}

bool Shape::filletHandle(int control, FilletHandle& out) const
{
    std::vector<libfive::pointshapes::Control> cs;
    if (!isFace() || !controlsNow(cs) || control < 0 || control >= int(cs.size()) || control >= int(m_controls.size())) return false;
    const libfive::Tree::Id var = m_controls[size_t(control)].radiusVar;
    const auto found = vars.find(var);
    if (!var || found == vars.end()) return false;
    const auto arcs = libfive::pointshapes::fillets(cs, m_controls_default, m_controls_closed);
    const auto mine = std::find_if(arcs.begin(), arcs.end(), [&](const libfive::pointshapes::Fillet& f) { return f.control == control; });
    if (mine == arcs.end()) return false;
    // The largest radius this corner can have, the others as they are: what the kernel makes of a very large one
    auto big = cs;
    big[size_t(control)].radius = 1e9;
    double cap = 0.0;
    for (const auto& f : libfive::pointshapes::fillets(big, m_controls_default, m_controls_closed))
        if (f.control == control) cap = f.radius;
    double frame[9];
    if (!(cap > 0) || !planeNow(frame)) return false;
    const Eigen::Vector3d toCentre = (mine->centre - mine->corner).normalized();
    const Eigen::Vector3d normal = Eigen::Vector3d(frame[3], frame[4], frame[5]).cross(Eigen::Vector3d(frame[6], frame[7], frame[8])).normalized();
    out.control = control;
    for (int a = 0; a < 3; ++a)
    {
        out.corner[a] = mine->corner[a];
        out.toCentre[a] = toCentre[a];
        out.normal[a] = normal[a];
    }
    out.sinHalf = std::sin(0.5 * mine->theta);
    out.scale = std::max(1e-6, cs[size_t(control)].influence);          // (the factor of the radius: the point's influence)
    out.radius = mine->radius;
    out.cap = cap;
    out.var = var;
    out.value0 = found->second;
    return true;
}

bool Shape::roundWithoutVar(int control) const
{
    return control >= 0 && control < int(m_controls.size()) && m_controls[size_t(control)].hasRadius && !m_controls[size_t(control)].radiusVar;
}

void Shape::setVirtualPoint(bool on, int after, QVector3D pos)
{
    m_virtual_on = on;
    m_virtual_after = after;
    m_virtual_pos = pos;
}

void Shape::refresh()
{
    // (the same as when a number it uses changed: the next render takes the numbers as they are)
    if (target_div != default_div) mesh_settings.cancel.store(true);
    startRender({next.settings, MESH_DIV_NEW_VARS, next.alg});
}

bool Shape::flatEdgeFrom(QVector3D& p) const
{
    // The plane: z = 0, or the shape's own
    Eigen::Vector3d o(0, 0, 0), n(0, 0, 1);
    if (m_frame_now_valid)
    {
        o = Eigen::Vector3d(m_frame_now[0], m_frame_now[1], m_frame_now[2]);
        n = Eigen::Vector3d(m_frame_now[3], m_frame_now[4], m_frame_now[5]).cross(
                Eigen::Vector3d(m_frame_now[6], m_frame_now[7], m_frame_now[8]));
        if (!(n.norm() > 1e-9)) return false;
        n.normalize();
    }
    auto onPlane = [&](Eigen::Vector3d q) { return q - n * (q - o).dot(n); };
    Eigen::Vector3d q = onPlane(Eigen::Vector3d(p.x(), p.y(), p.z()));
    ensureField();
    libfive::JacobianEvaluator e(tree, vars);
    int nudges = 0;
    for (int i = 0; i < 12; ++i)
    {
        const Eigen::Vector3f at(float(q.x()), float(q.y()), float(q.z()));
        const auto vt = e.valueAndPush(at);
        const auto g = e.deriv(at, *vt.second);
        Eigen::Vector3d gd(g.x(), g.y(), g.z());
        gd -= n * gd.dot(n);                                // (along the plane)
        const double g2 = gd.squaredNorm();
        if (!std::isfinite(g2) && nudges < 3)
        {
            // (exactly on a side of a shape whose distance has no gradient there -- the square root of zero: a hair away it has one)
            Eigen::Vector3d w = n.cross(Eigen::Vector3d::UnitX());
            if (w.norm() < 1e-3) w = n.cross(Eigen::Vector3d::UnitY());
            q = onPlane(q + w.normalized() * (1e-3 * double(1 + nudges)));
            ++nudges;
            continue;
        }
        if (!(g2 > 1e-12)) return false;
        if (std::fabs(vt.first) < 1e-5f) break;
        q = onPlane(q - gd * (double(vt.first) / g2));
    }
    p = QVector3D(float(q.x()), float(q.y()), float(q.z()));
    return true;
}

bool Shape::nativeDragOk() const
{
    // (locked: the shape is not pulled by its surface; the gizmo's mode does not matter, and the gizmo has priority
    // where it is shown)
    // (a linked copy has the numbers of its original in its tree: its surfaces are pulled by them, and so the original's are -- the two are one shape, and dragging
    // either moves both)
    if (m_locked) return false;
    // (a sweep has no surfaces to pull -- its numbers are the points', which have their own gizmos -- and pulling one meant making and compiling its whole field, in the GUI's
    // thread, when a click that missed a point of the path landed on the tube beside it: seconds to minutes of a window that did not answer)
    if (hasSweep() || hasMorph()) return false;
    buildDeps();
    const auto gizmo = pullMask();
    for (const void* d : m_deps)
    {
        if (vars.count(static_cast<libfive::Tree::Id>(d)) && !gizmo.count(static_cast<libfive::Tree::Id>(d)))
        {
            return true;
        }
    }
    return false;
}

bool Shape::curveTangentAt(int index, QVector3D& t) const
{
    if (!hasControls() || m_controls_face) return false;
    std::vector<libfive::pointshapes::Control> cs;
    if (!controlNumbers(vars, cs) || index < 0 || index >= int(cs.size()) || cs.size() < 2) return false;
    std::vector<std::pair<int, double>> owners;
    const auto V = libfive::pointshapes::polyline(cs, m_controls_default, m_controls_closed, 14, &owners);
    const int n = int(V.size());
    int at = -1;
    for (int j = 0; j < n; ++j)
        if (owners[size_t(j)].first == index && owners[size_t(j)].second == 0.0) { at = j; break; }
    if (at < 0 || n < 2) return false;
    const int before = at > 0 ? at - 1 : (m_controls_closed ? n - 1 : at);
    const int after = at + 1 < n ? at + 1 : (m_controls_closed ? 0 : at);
    Eigen::Vector3d d = V[size_t(after)] - V[size_t(before)];
    if (!(d.norm() > 1e-12)) return false;
    d.normalize();
    t = QVector3D(float(d.x()), float(d.y()), float(d.z()));
    return true;
}

static float gizmoNumber(const std::map<libfive::Tree::Id, float>& vars, libfive::Tree::Id id)
{
    const auto it = id ? vars.find(id) : vars.end();
    return it == vars.end() ? 0.0f : it->second;
}

QVector3D Shape::gizmoMove() const
{
    return QVector3D(gizmoNumber(vars, m_gizmo.move[0]), gizmoNumber(vars, m_gizmo.move[1]),
                     gizmoNumber(vars, m_gizmo.move[2]));
}

QVector3D Shape::gizmoCentreNow() const
{
    QVector3D c = m_gizmo.centre;
    for (const auto& s : m_gizmo.centreLive)
    {
        const auto it = s.id ? vars.find(s.id) : vars.end();
        if (it == vars.end()) continue;
        const double dv = double(it->second) - s.v0;
        c += QVector3D(float(s.d[0] * dv), float(s.d[1] * dv), float(s.d[2] * dv));
    }
    return c;
}

QVector3D Shape::gizmoScale() const
{
    auto one = [&](libfive::Tree::Id id) {
        const auto it = id ? vars.find(id) : vars.end();
        return it == vars.end() ? 1.0f : it->second;
    };
    return QVector3D(one(m_gizmo.scale[0]), one(m_gizmo.scale[1]), one(m_gizmo.scale[2]));
}

QVector3D Shape::gizmoRotate() const
{
    return QVector3D(gizmoNumber(vars, m_gizmo.rotate[0]), gizmoNumber(vars, m_gizmo.rotate[1]),
                     gizmoNumber(vars, m_gizmo.rotate[2]));
}

QMatrix4x4 Shape::gizmoRotation(const QVector3D& degrees)
{
    // The matrices of fieldes.stdlib.transforms (rotate_x/y/z, as the exact
    // regions apply them): Rx = [1 0 0; 0 c -s; 0 s c], Ry = [c 0 -s; 0 1 0; s 0 c],
    // Rz = [c -s 0; s c 0; 0 0 1]; the part turns by Rz * Ry * Rx
    auto rad = [](float d) { return double(d) * M_PI / 180.0; };
    const float cx = float(std::cos(rad(degrees.x()))), sx = float(std::sin(rad(degrees.x())));
    const float cy = float(std::cos(rad(degrees.y()))), sy = float(std::sin(rad(degrees.y())));
    const float cz = float(std::cos(rad(degrees.z()))), sz = float(std::sin(rad(degrees.z())));
    const QMatrix4x4 Rx(1, 0, 0, 0,   0, cx, -sx, 0,   0, sx, cx, 0,   0, 0, 0, 1);
    const QMatrix4x4 Ry(cy, 0, -sy, 0,   0, 1, 0, 0,   sy, 0, cy, 0,   0, 0, 0, 1);
    const QMatrix4x4 Rz(cz, -sz, 0, 0,   sz, cz, 0, 0,   0, 0, 1, 0,   0, 0, 0, 1);
    return Rz * Ry * Rx;
}

bool Shape::symmetryNow(Symmetry& out) const
{
    if (!m_symDef.present) return false;
    // The planes where the numbers of the gizmo have put them: it turns the definition about its centre (x, then y, then z) and moves it
    QVector3D axis = m_symDef.axis, first = m_symDef.first, centre = m_symDef.centre;
    if (m_gizmo.present)
    {
        const QMatrix4x4 R = gizmoRotation(gizmoRotate());
        axis = R.mapVector(axis);
        first = R.mapVector(first);
        centre = m_gizmo.about + gizmoMove();
    }
    out = Symmetry();
    out.on = true;
    out.kind = m_symDef.kind;
    out.count = m_symDef.count;
    out.centre = centre;
    out.c = axis;
    out.a = first;
    out.normalise();
    return true;
}

QMatrix4x4 Shape::gizmoMapLinear() const
{
    QMatrix4x4 L = m_gizmo.map;
    L(0, 3) = L(1, 3) = L(2, 3) = 0;
    return L;
}

float Shape::gizmoMapSign() const
{
    const QMatrix4x4 L = gizmoMapLinear();
    const float d = L(0, 0) * (L(1, 1) * L(2, 2) - L(1, 2) * L(2, 1)) - L(0, 1) * (L(1, 0) * L(2, 2) - L(1, 2) * L(2, 0)) + L(0, 2) * (L(1, 0) * L(2, 1) - L(1, 1) * L(2, 0));
    return d < 0 ? -1.0f : 1.0f;
}

QMatrix4x4 Shape::gizmoOrientation() const
{
    const QMatrix4x4 R = gizmoRotation(gizmoRotate());
    return gizmoMapped() ? gizmoMapLinear() * R : R;
}

QMatrix4x4 Shape::gizmoMatrix(const QVector3D& move, const QVector3D& rotate, const QVector3D& scale) const
{
    // What the placement arguments do to a shape (fieldes.stdlib.handles): scaled about its centre, turned about it (x, then y, then z), moved
    QMatrix4x4 A;
    A.translate(m_gizmo.about + move);
    A *= gizmoRotation(rotate);
    QMatrix4x4 S;
    S.scale(scale.x(), scale.y(), scale.z());
    A *= S;
    A.translate(-m_gizmo.about);
    if (m_gizmo.mapped)
    {
        // (an image: what the numbers do to the original, done to the image -- taken to the original's places, done there, and brought back)
        bool ok = false;
        const QMatrix4x4 back = m_gizmo.map.inverted(&ok);
        if (ok) A = m_gizmo.map * A * back;
    }
    return A;
}

Shape::GizmoNumbers Shape::gizmoNumbersIn(const std::map<libfive::Tree::Id, float>& v) const
{
    auto get = [&](libfive::Tree::Id id, float otherwise) {
        const auto it = id ? v.find(id) : v.end();
        return it == v.end() ? otherwise : it->second;
    };
    GizmoNumbers g;
    g.move = QVector3D(get(m_gizmo.move[0], 0.0f), get(m_gizmo.move[1], 0.0f), get(m_gizmo.move[2], 0.0f));
    g.rotate = QVector3D(get(m_gizmo.rotate[0], 0.0f), get(m_gizmo.rotate[1], 0.0f), get(m_gizmo.rotate[2], 0.0f));
    g.scale = QVector3D(get(m_gizmo.scale[0], 1.0f), get(m_gizmo.scale[1], 1.0f), get(m_gizmo.scale[2], 1.0f));
    // An operation that carries parts and has no gizmo of its own (H96, the user: "I can still drag a union out of the visible field and it reappears only after a full render"): its one gizmo moves the numbers
    // of its parts, all by the same amount.  Those numbers are tracked as its own move -- the first part that has one for an axis speaks for the axis -- so that it is drawn moved while it is pulled, as a shape
    // with a gizmo of its own is, with nothing meshed and the region held by its moved box, and not meshed again at every step in a region that it has already left
    if (!m_gizmo.present && !m_carried.empty())
    {
        for (int a = 0; a < 3; ++a)
        {
            for (const Gizmo& c : m_carried)
            {
                const auto it = c.move[a] ? v.find(c.move[a]) : v.end();
                if (it == v.end()) continue;
                g.move[a] = it->second;
                break;
            }
        }
    }
    return g;
}

void Shape::refreshPreview()
{
    const GizmoNumbers now = gizmoNumbersIn(vars);
    if (now.move == m_meshGizmo.move && now.rotate == m_meshGizmo.rotate && now.scale == m_meshGizmo.scale)
    {
        m_previewShown = false;                     // (the mesh is of the numbers it has: nothing to move)
        m_previewXform.setToIdentity();
        return;
    }
    const QMatrix4x4 from = gizmoMatrix(m_meshGizmo.move, m_meshGizmo.rotate, m_meshGizmo.scale);
    bool invertible = false;
    const QMatrix4x4 back = from.inverted(&invertible);
    if (!invertible)
    {
        m_previewShown = false;
        return;
    }
    m_previewXform = gizmoMatrix(now.move, now.rotate, now.scale) * back;
    m_previewShown = true;
}

bool Shape::previewBox(QVector3D& lo, QVector3D& hi) const
{
    if (!m_previewShown || !mesh) return false;
    const auto& b = mesh_bounds;
    bool any = false;
    for (int i = 0; i < 8; ++i)
    {
        const QVector3D corner((i & 1) ? b.upper.x() : b.lower.x(), (i & 2) ? b.upper.y() : b.lower.y(), (i & 4) ? b.upper.z() : b.lower.z());
        const QVector3D at = m_previewXform.map(corner);
        if (!any) { lo = hi = at; any = true; continue; }
        lo = QVector3D(std::min(lo.x(), at.x()), std::min(lo.y(), at.y()), std::min(lo.z(), at.z()));
        hi = QVector3D(std::max(hi.x(), at.x()), std::max(hi.y(), at.y()), std::max(hi.z(), at.z()));
    }
    return any;
}

bool Shape::movedBox(QVector3D& lo, QVector3D& hi) const
{
    if (!mesh || (!m_gizmo.present && !carriesParts()) || m_isPoint) return false;
    const GizmoNumbers now = gizmoNumbersIn(vars);
    if (now.move == m_meshGizmo.move && now.rotate == m_meshGizmo.rotate && now.scale == m_meshGizmo.scale) return false;
    bool invertible = false;
    const QMatrix4x4 back = gizmoMatrix(m_meshGizmo.move, m_meshGizmo.rotate, m_meshGizmo.scale).inverted(&invertible);
    if (!invertible) return false;
    const QMatrix4x4 xform = gizmoMatrix(now.move, now.rotate, now.scale) * back;
    const auto& b = mesh_bounds;
    bool any = false;
    for (int i = 0; i < 8; ++i)
    {
        const QVector3D corner((i & 1) ? b.upper.x() : b.lower.x(), (i & 2) ? b.upper.y() : b.lower.y(), (i & 4) ? b.upper.z() : b.lower.z());
        const QVector3D at = xform.map(corner);
        if (!any) { lo = hi = at; any = true; continue; }
        lo = QVector3D(std::min(lo.x(), at.x()), std::min(lo.y(), at.y()), std::min(lo.z(), at.z()));
        hi = QVector3D(std::max(hi.x(), at.x()), std::max(hi.y(), at.y()), std::max(hi.z(), at.z()));
    }
    return any;
}

bool Shape::previewGizmoVars(const std::map<libfive::Tree::Id, float>& changed, bool allowed)
{
    // Only a plain mesh of this shape can be moved as it is: with colours (they are values of a field at the places of the mesh), a result, a deformation, steps, a
    // face or a line of points, an imported part (its triangles are placed by a matrix of their own) or a mesh that is not there yet it is made again
    if (!allowed || (!m_gizmo.present && !carriesParts()) || m_locked || !mesh || has_color || has_result || has_deform || m_flat || hasControls() || m_isPoint ||
        exactCurrent() || m_exact || !m_steps.empty())
    {
        static const bool why = std::getenv("FIELDES_TIMING") != nullptr;
        if (why && m_unsigned)
            fprintf(stderr, "[preview] previewGizmoVars refused (surface): allowed=%d present=%d locked=%d mesh=%d color=%d result=%d deform=%d flat=%d controls=%d exact=%d steps=%d\n", int(allowed),
                  int(m_gizmo.present), int(m_locked), int(bool(mesh)), int(has_color), int(has_result), int(has_deform), int(m_flat), int(hasControls()), int(bool(m_exact)), int(!m_steps.empty()));
        return false;
    }
    auto own = gizmoVars();
    const bool carrier = !m_gizmo.present;                  // (an operation that carries parts, H96: the numbers that move its parts are what it is pulled by)
    if (carrier)
    {
        for (const Gizmo& c : m_carried)
        {
            for (int a = 0; a < 3; ++a)
            {
                if (c.move[a]) own.insert(c.move[a]);
            }
        }
    }
    bool affects = false;
    for (const auto& kv : changed)
    {
        const auto it = vars.find(kv.first);
        if (it == vars.end() || it->second == kv.second || !usesVar(kv.first)) continue;
        if (!own.count(kv.first))
        {
            if (m_unsigned && std::getenv("FIELDES_TIMING")) fprintf(stderr, "[preview] previewGizmoVars refused (surface): a changed number is not a gizmo number of it (own %d)\n", int(own.size()));
            return false;                           // (a number that is not its gizmo's changes what the shape IS: it is made again)
        }
        affects = true;
    }
    if (!affects)
    {
        if (m_unsigned && std::getenv("FIELDES_TIMING")) fprintf(stderr, "[preview] previewGizmoVars refused (surface): none of the changed numbers is used by it\n");
        return false;                               // (not this shape's numbers: updateVars keeps its numbers in step)
    }
    std::map<libfive::Tree::Id, float> next = vars;
    for (const auto& kv : changed)
    {
        const auto it = next.find(kv.first);
        if (it != next.end()) it->second = kv.second;
    }
    if (carrier)
    {
        // (the union is the same shape moved only if every part moves by the same amount along an axis: the pull of one part, or of a part and not the others, makes it another shape and it is made again)
        for (int a = 0; a < 3; ++a)
        {
            bool have = false;
            float step = 0.0f;
            for (const Gizmo& c : m_carried)
            {
                const auto o = c.move[a] ? vars.find(c.move[a]) : vars.end();
                const auto n = c.move[a] ? next.find(c.move[a]) : next.end();
                if (o == vars.end() || n == next.end()) continue;
                const float d = n->second - o->second;
                if (!have)
                {
                    have = true;
                    step = d;
                }
                else if (std::fabs(d - step) > 1e-4f)
                {
                    return false;
                }
            }
        }
    }
    const GizmoNumbers now = gizmoNumbersIn(next);
    for (int a = 0; a < 3; ++a)
        if (std::fabs(now.scale[a]) < 1e-4f) return false;       // (scaled to nothing: it cannot be taken back from that)
    for (const auto& kv : changed)
    {
        const auto it = vars.find(kv.first);
        if (it != vars.end()) it->second = kv.second;
    }
    refreshPreview();
    m_gizmoHeld = true;
    if (m_unsigned && std::getenv("FIELDES_TIMING")) fprintf(stderr, "[preview] previewGizmoVars accepted (surface)\n");
    return true;
}

void Shape::endGizmoPreview()
{
    m_gizmoHeld = false;
    if (!m_previewShown || !m_geometry_known) return;
    // (the numbers are in `vars`: the render takes them, and the mesh that is shown goes on being shown moved until the new one is there)
    startRender(RenderSettings { m_last_settings, MESH_DIV_NEW_VARS, m_last_alg });
}

void Shape::placeHint()
{
    placedHint(run_hint_lo, run_hint_hi, run_hint_side);
}

bool Shape::wouldRenderDifferently(const Settings& s) const
{
    if (!m_geometry_known) return true;
    const RenderGeometry g = renderGeometry(s);
    if (g == m_geometry) return false;
    // H189 (the user, 2026-11-12: "A large render should never have its render interrupted when nothing in it changes from a script change and when rendered once, never rerender when things are
    // change that dont affect it."): the region of the scene grows with the models that are shown -- a model made visible, a model that is moved -- and a body whose box is known and lies inside the
    // region it was meshed over AND inside the new one, at the same resolution and quality, is the same mesh in either: nothing in it changes, so nothing is drawn again and the render that is going is
    // not interrupted (measured on the small bowl: box_1 made visible grew the region from 334 to 636 mm, and difference_2, whose tree and box were the same, was meshed twice more)
    if (g.res != m_geometry.res || g.quality != m_geometry.quality || g.exact != m_geometry.exact) return true;
    if (!g.exact.empty()) return false;                 // (drawn from triangles of its own: the region is no part of it)
    if (!s.autoScale || !m_ext_valid || m_ext_stale || has_hint) return true;
    const double cell = g.res > 0 ? 1.0 / g.res : 0.0;
    const QVector3D pad(float(3 * cell), float(3 * cell), float(3 * cell));
    auto holds = [&](const QVector3D& lo, const QVector3D& hi) {
        const QVector3D a = m_ext_lo - pad, b = m_ext_hi + pad;
        return a.x() >= lo.x() && a.y() >= lo.y() && a.z() >= lo.z() && b.x() <= hi.x() && b.y() <= hi.y() && b.z() <= hi.z();
    };
    return !(holds(m_geometry.lo, m_geometry.hi) && holds(g.lo, g.hi));
}

Shape::RenderGeometry Shape::renderGeometry(const Settings& s) const
{
    // (as renderMesh decides it)
    RenderGeometry g;
    g.lo = s.min;
    g.hi = s.max;
    g.res = s.res;
    g.quality = s.quality;
    if (m_step_exact)
    {
        // (a step of an optimisation, drawn from its own triangles)
        g.exact = m_step_exact->key;
        g.res = 0;
        g.quality = 0;
        return g;
    }
    if (exactCurrent())
    {
        // (drawn from its own triangles: no region of its own, no resolution to ask for; only what they are)
        g.exact = m_exact->key;
        for (int i = 0; i < 12; ++i) g.exact += "," + std::to_string(float(m_exact_matrix[i]));
        g.res = 0;
        g.quality = 0;
        return g;
    }
    QVector3D lo, hi;
    float side = 0;
    placedHint(lo, hi, side);
    QVector3D own_lo, own_hi;
    double own_res = 0;
    if (ownRegion(s, lo, hi, side, own_lo, own_hi, own_res))
    {
        g.lo = own_lo;
        g.hi = own_hi;
        g.res = own_res;
    }
    else
    {
        g.res = autoResolution(s);
    }
    return g;
}

std::string Shape::extentWhy(const Settings& s)
{
    if (!s.autoScale) return "auto scale is off";
    if (has_hint) return "it has a render hint of its own";
    if (has_result) return "it is a result";
    if (m_step_exact || exactCurrent()) return "it is drawn from triangles of its own";
    if (!tree.is_valid()) return "no tree";
    if (hasControls() || (m_flat && hasFlatFrame())) return "a line or a face of points";
    if (hasSweep() || hasPrism() || hasMorph()) return "a sweep, a prism or a body of points";
    if (m_tree_nodes > 100000) return "a tree of " + std::to_string(m_tree_nodes) + " nodes";
    return "the search ran out of time or evaluations (" + std::to_string(m_tree_nodes) + " nodes)";
}

void Shape::measureExtent(const Settings& s)
{
    // (the box is made in locals and put in its members at the end: they are never half-made, or cleared while the search is going)
    if (!s.autoScale)
    {
        m_ext_valid = false;
        m_ext_stale = false;
        return;
    }
    // A tree that its search could not bound in its time (intervals are too loose for it) is not bounded in it the next time either: it is the tree that is hard, not the numbers
    // -- every render of a drag started with another 50 ms of looking, in the GUI's thread.  It gets a few milliseconds (a number that makes it easy is found at once) until it is
    // another tree, or the region it is looked for in is another: the first render of a script is made over the region the program starts with, before the script's own is
    // there, and a shape that is not in that one fails at once -- that must not leave it with a few milliseconds for good
    m_ext_own_L = 0;
    const bool again = tree.is_valid() && m_ext_failed == tree.id() && m_ext_failed_lo == s.min && m_ext_failed_hi == s.max;
    const float seconds = again ? 0.004f : 0.05f;
    QVector3D lo, hi;
    const bool found = searchExtent(vars, s.min, s.max, lo, hi, seconds);
    m_ext_failed = (tree.is_valid() && !found) ? tree.id() : libfive::Tree::Id();
    if (!found)
    {
        m_ext_failed_lo = s.min;
        m_ext_failed_hi = s.max;
    }
    if (found)
    {
        m_ext_lo = lo;
        m_ext_hi = hi;
        m_ext_valid = true;
        m_ext_stale = false;
        // A part that is placed turned has its size in its own frame too (the same search through the field taken through the part's placement, as the mesher sees it): the resolution is
        // normed by that, and so does not change when the part is turned (H146).  The longest side is taken in world units: the own box times the cube root of the scale
        QMatrix4x4 M;
        if (ownFrame(M))
        {
            bool ok = false;
            const QMatrix4x4 Mi = M.inverted(&ok);
            if (ok)
            {
                QVector3D llo(1e30f, 1e30f, 1e30f), lhi(-1e30f, -1e30f, -1e30f);
                for (int c = 0; c < 8; ++c)
                {
                    const QVector3D q = Mi.map(QVector3D((c & 1) ? s.max.x() : s.min.x(), (c & 2) ? s.max.y() : s.min.y(), (c & 4) ? s.max.z() : s.min.z()));
                    for (int a = 0; a < 3; ++a)
                    {
                        llo[a] = std::min(llo[a], q[a]);
                        lhi[a] = std::max(lhi[a], q[a]);
                    }
                }
                const libfive::Tree local = inFrame(tree, M);
                std::vector<libfive_tree> var_trees;
                std::vector<float> values;
                for (const auto& kv : vars)
                {
                    var_trees.push_back(reinterpret_cast<libfive_tree>(kv.first));
                    values.push_back(kv.second);
                }
                libfive_region3 search;
                search.X = {llo.x(), lhi.x()};
                search.Y = {llo.y(), lhi.y()};
                search.Z = {llo.z(), lhi.z()};
                libfive_region3 outBox;
                int openCells = 0;
                if (libfive_tree_bounds_vars(reinterpret_cast<libfive_tree>(local.id()), search, 20000, 0.05f, &outBox, &openCells, var_trees.data(), values.data(), int(var_trees.size())))
                {
                    const float dx = outBox.X.upper - outBox.X.lower, dy = outBox.Y.upper - outBox.Y.lower, dz = outBox.Z.upper - outBox.Z.lower;
                    if (dx >= 0 && dy >= 0 && dz >= 0) m_ext_own_L = double(std::max(dx, std::max(dy, dz))) * std::cbrt(std::fabs(double(M.determinant())));
                }
            }
        }
        return;
    }
    // A shape made with var()s -- a primitive that has been given handles when it was selected -- is not found by a search of 50 ms (the script's own search, which has hundreds of
    // them, says "a part is found in tens of milliseconds, one that has var()s in a few hundred"): it was left with no box, and so with the scene's plain resolution, in place of the
    // finer one that its size next to the region gives it -- a small part in a large region turned coarse the moment its handles were pulled.  The script says what box the shape
    // fills (its `_bounds`, from the numbers the primitive was made with): that is its size where the search has none
    if (m_script_box_valid && extentSearchable())
    {
        if (!m_script_vars_known)
        {
            m_script_vars = vars;
            m_script_vars_known = true;
        }
        m_ext_lo = m_script_lo;
        m_ext_hi = m_script_hi;
        m_ext_valid = true;
        m_ext_stale = !scriptBoxFresh();
        return;
    }
    m_ext_valid = false;
    m_ext_stale = false;
}

void Shape::setScriptBox(QVector3D lo, QVector3D hi)
{
    m_script_box_valid = hi.x() >= lo.x() && hi.y() >= lo.y() && hi.z() >= lo.z();
    m_script_lo = lo;
    m_script_hi = hi;
    m_script_vars = vars;
    m_script_vars_known = true;
}

bool Shape::scriptBoxFresh() const
{
    // (a number of the shape that is not what it was when the script said the box, and that the shape is made of: a surface of it has been pulled since)
    for (const auto& kv : m_script_vars)
    {
        const auto it = vars.find(kv.first);
        if (it != vars.end() && it->second != kv.second && usesVar(kv.first)) return false;
    }
    return true;
}

bool Shape::extentSearchable() const
{
    return tree.is_valid() && !has_hint && !has_result && !m_step_exact && !exactCurrent() && !hasControls() && !(m_flat && hasFlatFrame()) && !hasSweep() && !hasPrism() && !hasMorph() &&
           m_tree_nodes <= 100000;
}

bool Shape::extentWith(const std::map<libfive::Tree::Id, float>& vs, const QVector3D& search_lo, const QVector3D& search_hi, QVector3D& lo, QVector3D& hi)
{
    std::map<libfive::Tree::Id, float> numbers = vars;
    for (const auto& kv : vs)
    {
        const auto it = numbers.find(kv.first);
        if (it != numbers.end()) it->second = kv.second;
    }
    // (roughly, and in a few milliseconds: it is for the region a drag must be meshed over, which has room round the shape anyway; it is asked at every move)
    return searchExtent(numbers, search_lo, search_hi, lo, hi, 0.006f, 0.02f);
}

bool Shape::searchExtent(const std::map<libfive::Tree::Id, float>& numbers, const QVector3D& search_lo, const QVector3D& search_hi, QVector3D& lo, QVector3D& hi, float seconds,
                         float coarse)
{
    if (has_hint || has_result || m_step_exact || exactCurrent() || !tree.is_valid()) return false;
    if (hasControls() || (m_flat && hasFlatFrame())) return false;          // (a line or a face of points, or a face in a plane of its own: no extent is measured)
    // (a sweep or a prism: it is drawn from the numbers of its points, and a search through the field of a sweep -- a piece for every piece of the path -- took seconds, in the
    // GUI's thread, for an extent only the field's own render could use; if it has to be meshed from its field it is at the scene's resolution)
    if (hasSweep() || hasPrism() || hasMorph()) return false;
    ensureField();
    // (a tree of more than 100000 nodes -- anything made of a sweep's field -- costs more to be made ready for the search, in the GUI's thread, than the extent is worth: it is
    // not searched, and the shape is rendered at the scene's resolution)
    if (m_tree_nodes > 100000) return false;
    std::vector<libfive_tree> var_trees;
    std::vector<float> values;
    for (const auto& kv : numbers)
    {
        var_trees.push_back(reinterpret_cast<libfive_tree>(kv.first));
        values.push_back(kv.second);
    }
    libfive_region3 search;
    search.X = {search_lo.x(), search_hi.x()};
    search.Y = {search_lo.y(), search_hi.y()};
    search.Z = {search_lo.z(), search_hi.z()};
    if (m_flat) search.Z = {-1e-3f, 1e-3f};         // (a flat shape is in the plane z = 0: its extent is that of its face)
    libfive_region3 out;
    int open = 0;
    // (a search of at most 50 ms, in the GUI's thread -- the clock is read at every evaluation: a shape it cannot bound in that time is rendered as it was before.  The few
    // milliseconds that the shapes of the examples take are well inside it)
    const bool found = coarse > 0
        ? libfive_tree_bounds_vars_coarse(reinterpret_cast<libfive_tree>(tree.id()), search, 20000, seconds, coarse, &out, &open, var_trees.data(), values.data(), int(var_trees.size()))
        : libfive_tree_bounds_vars(reinterpret_cast<libfive_tree>(tree.id()), search, 20000, seconds, &out, &open, var_trees.data(), values.data(), int(var_trees.size()));
    if (!found) return false;
    lo = QVector3D(out.X.lower, out.Y.lower, out.Z.lower);
    hi = QVector3D(out.X.upper, out.Y.upper, out.Z.upper);
    return hi.x() >= lo.x() && hi.y() >= lo.y() && hi.z() >= lo.z();
}

double Shape::finestFeature() const
{
    if (!(m_feature > 0)) return 0.0;
    // (the placement's scale makes the feature as much larger or smaller as it does the shape)
    double k = 1.0;
    if (m_gizmo.present)
    {
        const QVector3D g = gizmoScale();
        k = std::min(std::fabs(double(g.x())), std::min(std::fabs(double(g.y())), std::fabs(double(g.z()))));
    }
    return k > 1e-9 ? double(m_feature) * k : 0.0;
}

double Shape::autoResolution(const Settings& s) const
{
    const double res = scaledResolution(s);
    const double feature = finestFeature();
    if (!(feature > 0) || !m_ext_valid) return res;
    // (a shape that says the size of its smallest feature -- the pitch of a thread -- is meshed at ten samples to it at the finest level, so that its turns are turns; not past 2000 across its
    // box, which is what a huge number of turns would ask)
    double want = 10.0 / feature;
    const QVector3D size = m_ext_hi - m_ext_lo;
    const double L = std::max(size.x(), std::max(size.y(), size.z()));
    if (L > 1e-6) want = std::min(want, 2000.0 / L);
    return std::max(res, want);
}

double Shape::scaledResolution(const Settings& s) const
{
    // (a body with a resolution of its own -- custom_resolution -- has it in the place of the scene's, and is scaled by its size as the scene's is)
    const double base = m_res_override > 0 ? double(m_res_override) : double(s.res);
    // (a point is not painted -- its dot is drawn where its numbers say -- so its mesh is seen by nobody: it is meshed at the scene's plain resolution, not at a hundred samples across its little ball, which
    // made a script of forty points cost half a minute: the user's sheep horn, 2026-10-29, "just way slower")
    // (a result that is drawn from the elements of a solver is meshed at the scene's resolution: its colours are read from them.  One whose fields are expressions -- the optimization of a
    // topology, a check of a process: no grid and no mesh of elements -- is a body like any other and is normed like one.  At the scene's plain 20 to the mm the optimized beam of the tour was
    // 1200 samples across its 60 mm, where a part of that size gets 200: six times finer in each direction, 8.8 s for the second finest level and minutes for the finest, and the step "Allow less
    // material" never finished drawing (the user, 2026-11-03, H105: "STILL BROKEN NOT RENDERING"))
    const bool fromElements = has_result && (m_grid.isMesh || !m_grid.fraction.empty());
    if (!s.autoScale || has_hint || fromElements || (m_isPoint && no_paint)) return base;
    // The resolution is NORMED by the size of the part (the user, 2026-10-29: "Make resolution be normed by the bounding box size of the part ... so that when you scale the same part it does not change its
    // amount of triangles and when you create a huge cylinder next to a small one, they both have the same amount of vertices"): the setting says how many samples go across the longest side of the part's
    // box -- ten for every unit of it, a hundred at the default 10 -- whatever the size of the part in mm and whatever is beside it.  The sqrt(R / L) of before made a part finer the smaller it was
    // next to the region, and a big one cost as many samples per mm as a small one: a cube of 50 mm at "10" was 500 samples across, and took seconds
    constexpr double kAcross = 10.0;
    double L = 0;
    QVector3D lo, hi;
    if (m_ext_valid)
    {
        const QVector3D size = m_ext_hi - m_ext_lo;
        L = std::max(size.x(), std::max(size.y(), size.z()));
        // (a part that is placed turned is normed by the box it has in its own frame, not by the box it fills in the world's axes, which is larger the more it is turned: its resolution is the
        // same however it stands -- the user, 2026-11-04, H146: "When you rotate an object, its meshing keeps the meshing direction correctly, but its resolution changes ... the render should not change")
        // (where the search through the field could not measure that box, the script says it: the box the part had before it was placed)
        double ownL = m_ext_own_L;
        QMatrix4x4 placedAs;
        if (ownL <= 0 && m_script_own_L > 0 && !m_ext_stale && ownFrame(placedAs)) ownL = m_script_own_L;
        if (ownL > 0 && (m_ext_own_L > 0 || !m_ext_stale)) L = ownL;
    }
    else if (meshSize(lo, hi))
    {
        // (the box that the search through the formula did not find -- a shape drawn from its points, a formula too large or too slow to search -- is the box of the mesh it has)
        const QVector3D size = hi - lo;
        L = std::max(size.x(), std::max(size.y(), size.z()));
    }
    if (!(L > 1e-6))
    {
        // (a part that has no box and no mesh yet -- its first picture: the region, which follows the models, is what there is to norm it by)
        const QVector3D region = s.max - s.min;
        L = std::max(region.x(), std::max(region.y(), region.z()));
    }
    if (!(L > 1e-6)) return base;
    double res = std::min(base * kAcross / L, 2000.0 / L);              // (at most 2000 samples across)
    // (the script's box of a shape whose surface is being pulled is the box it had before: the shape is larger, or smaller, than it says, and the region has grown to hold it -- the box
    // would make it finer at every move.  It is meshed as finely as it was, and the script, which says the box again when the pull is let go, sizes it then)
    if (m_ext_stale && m_geometry_known && m_geometry.res > 0) res = std::min(res, double(m_geometry.res));
    return res;
}

bool Shape::meshSize(QVector3D& lo, QVector3D& hi) const
{
    if (!mesh) return false;
    const auto& b = mesh_bounds;
    const QVector3D l(b.lower.x(), b.lower.y(), b.lower.z()), h(b.upper.x(), b.upper.y(), b.upper.z());
    if (!(h.x() > l.x() || h.y() > l.y() || h.z() > l.z())) return false;
    lo = l;
    hi = h;
    return true;
}

void Shape::sizeFirstLevel(const Settings& s)
{
    if (m_div_sized || default_div == MESH_DIV_EMPTY) return;
    // (a shape whose last level took the mesher a quarter of a second has learned its level from that: it is not a small shape in a big region)
    if (m_levelMs.load() > 250.0) { m_div_sized = true; return; }
    QVector3D lo, hi;
    if (!m_ext_valid && !meshSize(lo, hi)) return;
    default_div = defaultDivFor(s);
    m_div_sized = true;
}

int Shape::defaultDivFor(const Settings& s)
{
    int div = sizedDivFor(s);
    const double feature = finestFeature();
    if (div > 0 && feature > 0 && m_ext_valid)
    {
        // (the first level -- the picture a drag shows, and what a long render starts with -- has cells of at most a fifth of the smallest feature of the surface: a coarser one draws a thread as the
        // random spikes of a grid it does not fit, which changes at every move of the hand.  The user, 2026-10-28: "super weird lower res renders when changing it in real time ... nauseating")
        const int fine = int(std::floor(std::log2(std::max(1.0, autoResolution(s) * feature / 5.0))));
        div = std::min(div, std::max(0, fine));
    }
    return div;
}

int Shape::sizedDivFor(const Settings& s)
{
    // (a shape that is drawn from the numbers of its points is meshed in a moment: there is no coarse picture to show first, one pass at the finest level is all it needs)
    if (drawnFromNumbers()) return 0;
    if (!s.autoScale || has_hint || has_result) return s.defaultDiv();
    // (its size: from the search for its box, else from the mesh it has -- a shape that is a few millimetres across in a region of a hundred starts at a level at which it is
    // a few cells wide, which draws a leg as a wedge, at every move of a drag)
    QVector3D lo = m_ext_lo, hi = m_ext_hi;
    if (!m_ext_valid && !meshSize(lo, hi)) return s.defaultDiv();
    const QVector3D size = hi - lo;
    const double L = std::max(size.x(), std::max(size.y(), size.z()));
    if (!(L > 1e-6)) return s.defaultDiv();
    const double across = autoResolution(s) * L;            // (samples across the shape at the finest level)
    // (H164, the user, 2026-11-09: "THERE IS NO COARSE LEVEL RENDER WHEN DRAGGING A POINT OUT OF A SURFACE. IT IS JUST FROZEN FOR LIKE 15 SECONDS": measured on a body of 26 points -- the first picture at 64 cells across took
    // 1.2 s, the finest 8.5 s, and a body with more points costs more at every level.  A body of points starts at 16 cells across, two levels coarser: its first picture comes in about a tenth of a second, as the
    // drag of a point does, and the finer levels follow one after the other)
    return std::max(0, int(std::lround(std::log2(std::max(1.0, across / (isPointBody() ? 16.0 : 64.0))))));
}

bool Shape::ownRegion(const Settings& s, const QVector3D& box_lo, const QVector3D& box_hi, float side,
                      QVector3D& lo, QVector3D& hi, double& res) const
{
    // The region a part with a resolution of its own (an import's part: fieldes.stdlib.cad_import.roi_resolution) is meshed over, and that resolution.  The part's box (placed by its
    // gizmo) in the scene's region: a cube round the box, `side` long (a power of two of cells of that resolution).  The resolution follows the scene's -- in proportion, or the number
    // of custom_resolution(part, r) in the scene's place -- and is the part's only when the whole part is in the region
    if (!has_hint) return false;
    const bool inside = box_lo.x() >= s.min.x() && box_hi.x() <= s.max.x() &&
                        box_lo.y() >= s.min.y() && box_hi.y() <= s.max.y() &&
                        box_lo.z() >= s.min.z() && box_hi.z() <= s.max.z();
    if (!inside) return false;
    const QVector3D c = 0.5f * (box_lo + box_hi);
    const float h = 0.5f * side;
    lo = QVector3D(c.x() - h, c.y() - h, c.z() - h);
    hi = QVector3D(c.x() + h, c.y() + h, c.z() + h);
    res = double(hint_res) * ((m_res_override > 0 ? double(m_res_override) : double(s.res)) / double(hint_scene_res));
    return true;
}

void Shape::placedHint(QVector3D& out_lo, QVector3D& out_hi, float& out_side) const
{
    out_lo = hint_lo;
    out_hi = hint_hi;
    out_side = hint_side;
    if (!has_hint || !m_gizmo.present)
    {
        return;
    }
    // The part's own-resolution cube follows its gizmo: scaled, moved by the
    // translation and, once turned, big enough for any turn of the box
    const QVector3D turn = gizmoRotate();
    const QVector3D grow = gizmoScale();
    QVector3D c = 0.5f * (hint_lo + hint_hi);
    QVector3D half = 0.5f * (hint_hi - hint_lo);
    float side = hint_side;
    if (grow != QVector3D(1, 1, 1))
    {
        c = m_gizmo.about + QVector3D(grow.x() * (c.x() - m_gizmo.about.x()),
                                        grow.y() * (c.y() - m_gizmo.about.y()),
                                        grow.z() * (c.z() - m_gizmo.about.z()));
        half = QVector3D(half.x() * std::fabs(grow.x()), half.y() * std::fabs(grow.y()),
                         half.z() * std::fabs(grow.z()));
        side *= std::max({std::fabs(grow.x()), std::fabs(grow.y()), std::fabs(grow.z()), 1e-3f});
    }
    if (turn != QVector3D(0, 0, 0))
    {
        c = m_gizmo.about + gizmoRotation(turn).map(c - m_gizmo.about);
        half *= 1.75f;
        side *= 1.75f;
    }
    c += gizmoMove();
    out_lo = c - half;
    out_hi = c + half;
    out_side = side;
}

QString Shape::colorKey() const
{
    return colorKeyBase() + marker_key;
}

QString Shape::colorKeyBase() const
{
    if (!has_color) return QString();
    QString lines = QString("|lines %1 %2 %3").arg(flow_lines.size()).arg(flow_lo).arg(flow_hi);
    if (has_result)
    {   // the result itself; which field, step and scale are shown is view state
        QString key = "result";
        for (const auto& c : base_channels) key += QString("|%1").arg(quintptr(c.tree.id()));
        for (const auto& st : m_steps)
        {
            key += "|step";
            for (const auto& c : st.channels) key += QString(" %1").arg(quintptr(c.tree.id()));
            if (st.hasDeform)
                for (int a = 0; a < 3; ++a) key += QString(" d%1").arg(quintptr(st.deform[a].id()));
            if (st.tree.is_valid()) key += QString(" g%1").arg(quintptr(st.tree.id()));
            if (st.hasLines) key += QString(" l%1").arg(st.lines.size());
        }
        return key + lines;
    }
    QString steps;
    for (const auto& st : m_steps)
        if (st.tree.is_valid()) steps += QString("|g%1").arg(quintptr(st.tree.id()));
    return QString("%1|%2|%3|%4|%5|%6|%7").arg(quintptr(color_field.id()))
        .arg(color_auto).arg(color_lo).arg(color_hi).arg(color_label, color_map, bc_key) + lines + steps +
        (has_cutoff ? QString("|patch %1").arg(color_cutoff) : QString()) +
        (has_floor ? QString("|floor %1").arg(color_floor) : QString());
}

namespace {
// A tree's values at many points, in evaluator-sized batches
template <typename Points>
void evalAll(libfive::ArrayEvaluator& e, const Points& pts, std::vector<float>& out)
{
    out.resize(pts.size());
    const size_t N = LIBFIVE_EVAL_ARRAY_SIZE;
    for (size_t i = 0; i < pts.size(); i += N)
    {
        const size_t n = std::min(N, pts.size() - i);
        for (size_t j = 0; j < n; j++) e.set(Eigen::Vector3f(pts[i + j].template cast<float>()), j);
        const auto vals = e.values(n);
        for (size_t j = 0; j < n; j++) out[i + j] = vals[j];
    }
}

}   // namespace

bool Shape::meshBox(QVector3D& lo, QVector3D& hi) const
{
    if (!mesh || mesh->branes.empty()) return false;
    bool any = false;
    for (const auto& t : mesh->branes)
    {
        for (int k = 0; k < 3; ++k)
        {
            if (t[k] >= mesh->verts.size()) continue;
            const Eigen::Vector3f v = mesh->verts[t[k]];
            const QVector3D p(v.x(), v.y(), v.z());
            if (!any) { lo = hi = p; any = true; continue; }
            lo = QVector3D(std::min(lo.x(), p.x()), std::min(lo.y(), p.y()), std::min(lo.z(), p.z()));
            hi = QVector3D(std::max(hi.x(), p.x()), std::max(hi.y(), p.y()), std::max(hi.z(), p.z()));
        }
    }
    return any;
}

namespace {

// A result's colours are read at the render's vertices and interpolated across its triangles.  A flat face comes
// as a few big triangles, which would smear a field that varies across it: the triangles are split until no edge is
// longer than `detail` (the solver's element), red-green -- a triangle with a long edge is split in four, a
// neighbour that got a midpoint on an edge of its own is split in two or three to meet it -- so no edge is left with
// a hanging vertex: no cracks, no seams in the colour.
void refineForColour(libfive::Mesh& m, float detail)
{
    using Tri = Eigen::Matrix<uint32_t, 3, 1>;
    const float d2 = detail * detail;
    auto edgeKey = [](uint32_t a, uint32_t b) {
        if (a > b) std::swap(a, b);
        return (uint64_t(a) << 32) | uint64_t(b);
    };
    for (int level = 0; level < 10; ++level)
    {
        std::unordered_map<uint64_t, uint32_t> mids;      // an edge split: its midpoint vertex
        bool any = false;
        for (const auto& t : m.branes)
        {
            float longest = 0;
            for (int e = 0; e < 3; ++e)
                longest = std::max(longest, (m.verts[t[e]] - m.verts[t[(e + 1) % 3]]).squaredNorm());
            if (longest <= d2) continue;
            any = true;
            for (int e = 0; e < 3; ++e)
            {
                const uint32_t a = t[e], b = t[(e + 1) % 3];
                const uint64_t k = edgeKey(a, b);
                if (mids.find(k) == mids.end())
                {
                    mids.emplace(k, uint32_t(m.verts.size()));
                    m.verts.push_back(0.5f * (m.verts[a] + m.verts[b]));
                }
            }
        }
        if (!any) break;
        decltype(m.branes) out;
        out.reserve(m.branes.size() * 2);
        auto mid = [&](uint32_t a, uint32_t b) -> uint32_t {
            auto it = mids.find(edgeKey(a, b));
            return it == mids.end() ? 0 : it->second;          // (0 is the mesh's unused first vertex: never a midpoint)
        };
        auto tri = [&](uint32_t a, uint32_t b, uint32_t c) { out.push_back(Tri(a, b, c)); };
        for (const auto& t : m.branes)
        {
            const uint32_t v[3] = {t[0], t[1], t[2]};
            const uint32_t e[3] = {mid(v[0], v[1]), mid(v[1], v[2]), mid(v[2], v[0])};
            const int n = (e[0] != 0) + (e[1] != 0) + (e[2] != 0);
            if (n == 0)
            {
                out.push_back(t);
                continue;
            }
            if (n == 3)
            {
                tri(v[0], e[0], e[2]);
                tri(e[0], v[1], e[1]);
                tri(e[2], e[1], v[2]);
                tri(e[0], e[1], e[2]);
                continue;
            }
            // green: turned so that edge 0 is the split one (with two, edges 0 and 1)
            int r = 0;
            if (n == 1) { while (e[r] == 0) ++r; }
            else { while (!(e[r] != 0 && e[(r + 1) % 3] != 0)) ++r; }
            const uint32_t a = v[r], b = v[(r + 1) % 3], c = v[(r + 2) % 3];
            const uint32_t ab = e[r], bc = e[(r + 1) % 3];
            if (n == 1)
            {
                tri(a, ab, c);
                tri(ab, b, c);
            }
            else
            {
                tri(a, ab, c);
                tri(ab, b, bc);
                tri(ab, bc, c);
            }
        }
        m.branes.swap(out);
    }
}

// The CHANGE colour of a manufacturing result is a CLASS -- 1 added, -1 cut, 0 as it was -- read at the vertices of the mesh and run across its triangles.  A flat face is a few big triangles: where the edge of
// the red lies inside one, the value running from a corner that is red to one far away drew a pale red over a face that was never touched (rays round the hole of a plate, wedges on the top of a boss), and a
// face that is red all over but whose corners all lie on its rim, where nothing changed, came out grey.  So a triangle whose corners differ by more than a twentieth, and every triangle longer than `coarse`
// (the inside of a big face is looked at), is cut into four at the middles of its edges, again and again, down to `smallest`: the edge of the colour is as fine as the smallest triangles.  The shape of the surface
// does not change.  `evaluate(first, last)` fills values[first .. last) for the vertices first .. last - 1
template <typename Evaluate>
void cutByColourClass(libfive::Mesh& m, std::vector<float>& values, float coarse, float smallest, Evaluate evaluate, bool always = false)
{
    float lo = std::numeric_limits<float>::infinity(), hi = -lo;
    for (float v : values)
        if (std::isfinite(v)) { lo = std::min(lo, v); hi = std::max(hi, v); }
    // (one value all over: nothing to cut -- unless the value is what the vertices happen to read and the inside of the faces may hold another: the check of a sharp box reads
    // nothing at any corner of its overhang, all of them on edges, and the face is broken all over -- `always`)
    if (!(hi > lo) && !always) return;
    using Tri = Eigen::Matrix<uint32_t, 3, 1>;
    const float coarse2 = coarse * coarse, smallest2 = smallest * smallest;
    for (int level = 0; level < 10; ++level)
    {
        const size_t before = m.verts.size();
        std::map<std::pair<uint32_t, uint32_t>, uint32_t> middles;
        auto middle = [&](uint32_t a, uint32_t b) {
            const auto key = a < b ? std::make_pair(a, b) : std::make_pair(b, a);
            const auto it = middles.find(key);
            if (it != middles.end()) return it->second;
            const uint32_t id = uint32_t(m.verts.size());
            m.verts.push_back(0.5f * (m.verts[a] + m.verts[b]));
            middles.emplace(key, id);
            return id;
        };
        decltype(m.branes) cut;
        cut.reserve(m.branes.size() + m.branes.size() / 8);
        bool any = false;
        for (const auto& t : m.branes)
        {
            const float v0 = values[t[0]], v1 = values[t[1]], v2 = values[t[2]];
            const float longest = std::max({(m.verts[t[0]] - m.verts[t[1]]).squaredNorm(), (m.verts[t[1]] - m.verts[t[2]]).squaredNorm(),
                                            (m.verts[t[2]] - m.verts[t[0]]).squaredNorm()});
            const bool same = !(std::isfinite(v0) && std::isfinite(v1) && std::isfinite(v2)) ||
                              std::max({v0, v1, v2}) - std::min({v0, v1, v2}) <= 0.05f;
            if ((same && longest <= coarse2) || longest <= smallest2)
            {
                cut.push_back(t);
                continue;
            }
            any = true;
            const uint32_t m01 = middle(t[0], t[1]), m12 = middle(t[1], t[2]), m20 = middle(t[2], t[0]);
            cut.push_back(Tri(t[0], m01, m20));
            cut.push_back(Tri(m01, t[1], m12));
            cut.push_back(Tri(m20, m12, t[2]));
            cut.push_back(Tri(m01, m12, m20));
        }
        if (!any) break;
        values.resize(m.verts.size());
        evaluate(before, m.verts.size());
        m.branes = std::move(cut);
    }
}

// A tree's values at the vertices first .. last - 1 of a mesh, on up to `threads` threads: the first evaluator is the caller's, the others are built by the threads themselves and kept in `pool` for the next call.
// The result of a manufacturing shell is a formula that costs tens of microseconds a point, and a mesh has a hundred thousand vertices: on one thread that was seconds, and the cutting of the triangles of its
// change colour adds as many again
template <typename Points>
void evalRangeThreaded(libfive::ArrayEvaluator& main, const libfive::Tree& tree, std::vector<std::unique_ptr<libfive::ArrayEvaluator>>& pool,
                       const Points& pts, std::vector<float>& out, size_t first, size_t last, size_t threads)
{
    if (last <= first) return;
    const size_t N = LIBFIVE_EVAL_ARRAY_SIZE;
    const size_t batches = (last - first + N - 1) / N;
    const size_t T = std::max<size_t>(1, std::min<size_t>(threads, (batches + 3) / 4));
    if (pool.size() < T) pool.resize(T);
    std::atomic<size_t> next{0};
    auto work = [&](libfive::ArrayEvaluator& e) {
        for (size_t b; (b = next.fetch_add(1)) < batches; )
        {
            const size_t i = first + b * N;
            const size_t n = std::min(N, last - i);
            for (size_t j = 0; j < n; j++) e.set(Eigen::Vector3f(pts[i + j].template cast<float>()), j);
            const auto v = e.values(n);
            for (size_t j = 0; j < n; j++) out[i + j] = v[j];
        }
    };
    std::vector<std::thread> workers;
    for (size_t t = 1; t < T; t++)
        workers.emplace_back([&, t] {
            if (!pool[t]) pool[t].reset(new libfive::ArrayEvaluator(tree));
            work(*pool[t]);
        });
    work(main);
    for (auto& w : workers) w.join();
}
}   // anonymous namespace

bool Shape::probe(const QVector3D& p, float& value)
{
    if (!has_color) return false;
    // The elements shown: the value of the element under the cursor
    if (has_result && m_show_elements && hasElements()) return probeElement(p, value);
    Eigen::Vector3f q(p.x(), p.y(), p.z());
    if (has_result)
    {
        // The deformed model: find the undeformed point that moved here (the
        // point q with q + scale * u(q) = target, iterated until it holds)
        if (has_deform && m_deform != 0)
        {
            for (int a = 0; a < 3; a++)
            {
                if (!probe_disp[a]) probe_disp[a].reset(new libfive::ArrayEvaluator(deform_tree[a]));
            }
            const Eigen::Vector3f target = q;
            for (int it = 0; it < 40; it++)
            {
                const Eigen::Vector3f u(probe_disp[0]->value(q), probe_disp[1]->value(q),
                                        probe_disp[2]->value(q));
                const Eigen::Vector3f next = target - m_deform * u;
                const bool done = (next - q).norm() < 1e-5f * (1.0f + target.norm());
                q = next;
                if (done) break;
            }
        }
        if (!probe_channel || probe_channel_index != m_channel)
        {
            probe_channel.reset(new libfive::ArrayEvaluator(m_channels[size_t(m_channel)].tree));
            probe_channel_index = m_channel;
        }
        value = probe_channel->value(q);
        return true;
    }
    if (!probe_eval) probe_eval.reset(new libfive::ArrayEvaluator(color_field, vars));
    value = probe_eval->value(q);
    return true;
}

namespace {
// For each tetrahedron and face (opposite each vertex), the tetrahedron across it (-1: none)
void buildAdjacency(Shape::ElementGrid& g)
{
    const size_t nt = g.mtets.size() / 4;
    struct Rec { int a, b, c; int tet; int local; };
    std::vector<Rec> recs;
    recs.reserve(4 * nt);
    for (size_t t = 0; t < nt; ++t)
        for (int l = 0; l < 4; ++l)
        {
            int f[3], m = 0;
            for (int p = 0; p < 4; ++p) if (p != l) f[m++] = g.mtets[4 * t + size_t(p)];
            std::sort(f, f + 3);
            recs.push_back({f[0], f[1], f[2], int(t), l});
        }
    std::sort(recs.begin(), recs.end(), [](const Rec& x, const Rec& y) {
        if (x.a != y.a) return x.a < y.a;
        if (x.b != y.b) return x.b < y.b;
        return x.c < y.c;
    });
    g.madj.assign(4 * nt, -1);
    for (size_t r = 0; r + 1 < recs.size(); ++r)
        if (recs[r].a == recs[r + 1].a && recs[r].b == recs[r + 1].b && recs[r].c == recs[r + 1].c)
        {
            g.madj[4 * size_t(recs[r].tet) + size_t(recs[r].local)] = recs[r + 1].tet;
            g.madj[4 * size_t(recs[r + 1].tet) + size_t(recs[r + 1].local)] = recs[r].tet;
        }
}
}   // anonymous namespace

void Shape::setResult(std::vector<FieldChannel> channels, int current,
                      const libfive::Tree& ux, const libfive::Tree& uy, const libfive::Tree& uz,
                      float deformScale, float deformAuto, ElementGrid grid)
{
    if (channels.empty()) return;
    has_result = true;
    has_color = true;
    m_channels = std::move(channels);
    m_channel = std::max(0, std::min(int(m_channels.size()) - 1, current));
    has_deform = ux.is_valid() && uy.is_valid() && uz.is_valid();
    deform_tree[0] = ux;
    deform_tree[1] = uy;
    deform_tree[2] = uz;
    m_deform = has_deform ? deformScale : 0;
    m_deform_auto = deformAuto > 0 ? deformAuto : 1;
    m_grid = std::move(grid);
    // The elements are drawn from each element's own values.  A grid without
    // them (or not matching the fields) is not drawn: nothing is made up from
    // the smooth fields.
    m_elem_lo.clear();
    m_elem_hi.clear();
    m_user_range.clear();                // (a new result: the ends typed for the one before are not its own -- the viewer puts them back, View::applyViewerChoices)
    if (m_grid.isMesh || !m_grid.fraction.empty())
    {
        size_t expect = 0;
        bool good = m_grid.values.size() == m_channels.size();
        if (m_grid.isMesh)
        {
            const size_t nv = m_grid.mverts.size() / 3, nt = m_grid.mtets.size() / 4;
            expect = nt;
            good = good && nt > 0 && m_grid.mverts.size() == 3 * nv && m_grid.mdisp.size() == 3 * nv &&
                   m_grid.mtets.size() == 4 * nt && m_grid.mfaces.size() % 3 == 0 &&
                   m_grid.mfaceTet.size() == m_grid.mfaces.size() / 3;
            for (int32_t v : m_grid.mtets) good = good && v >= 0 && size_t(v) < nv;
            // (the diagonals not drawn, and the element of each tetrahedron: pairs of vertices, and one number per tetrahedron or none)
            good = good && m_grid.mhidden.size() % 2 == 0 && (m_grid.mowner.empty() || m_grid.mowner.size() == nt);
            for (int32_t v : m_grid.mhidden) good = good && v >= 0 && size_t(v) < nv;
            for (int32_t v : m_grid.mowner) good = good && v >= 0;
            if (good)
            {
                buildAdjacency(m_grid);
                // a typical edge length (for how near a hover must be)
                double sum = 0;
                size_t n = 0;
                for (size_t t = 0; t < nt && n < 2000; ++t, ++n)
                {
                    const size_t a = size_t(m_grid.mtets[4 * t]), b = size_t(m_grid.mtets[4 * t + 1]);
                    sum += (QVector3D(m_grid.mverts[3 * a], m_grid.mverts[3 * a + 1], m_grid.mverts[3 * a + 2]) -
                            QVector3D(m_grid.mverts[3 * b], m_grid.mverts[3 * b + 1], m_grid.mverts[3 * b + 2])).length();
                }
                m_grid.h = n ? float(sum / double(n)) : 1.0f;
            }
        }
        else
        {
            // The elements are drawn from each element's own values.  A grid without
            // them (or not matching the fields) is not drawn: nothing is made up from
            // the smooth fields.
            size_t active = 0;
            for (float f : m_grid.fraction) active += f > 0;
            expect = active * (m_grid.tetrahedra ? 6 : 1);
            good = good && expect > 0;
        }
        for (const auto& v : m_grid.values) good = good && v.size() == expect;
        if (good)
        {
            for (const auto& v : m_grid.values)
            {
                float lo = v[0], hi = v[0];
                if (m_grid.isMesh && m_grid.visibleRange && !m_grid.mfaceTet.empty())
                {
                    // (H219: the range of a displayed mesh is the one of the cells that are drawn -- the tetrahedra of its boundary faces; the cells inside cannot be seen, and a range over all of them put every
                    // visible cell of a cube at the dark end of the map: "WHY IS EVERYTHING THE LOWEST VALUE???")
                    lo = std::numeric_limits<float>::max();
                    hi = std::numeric_limits<float>::lowest();
                    for (int32_t t : m_grid.mfaceTet)
                    {
                        if (t < 0 || size_t(t) >= v.size()) continue;
                        const float x = v[size_t(t)];
                        lo = std::min(lo, x);
                        hi = std::max(hi, x);
                    }
                    if (!(hi >= lo)) lo = hi = v[0];            // (no valid boundary face: the range stays the one of the first cell, widened below)
                }
                else
                {
                    for (float x : v)
                    {
                        lo = std::min(lo, x);
                        hi = std::max(hi, x);
                    }
                }
                if (!(hi > lo))
                {
                    // (every element has the same value -- a mesh of cubes of one size: the value is the middle of the colour map, not the dark end that the lowest value of a range of no width gets; the mesh was
                    // drawn all black and its edges were lost on it, H203)
                    const float half = lo != 0.0f ? 0.5f * std::abs(lo) : 0.5f;
                    lo -= half;
                    hi += half;
                }
                m_elem_lo.push_back(lo);
                m_elem_hi.push_back(hi);
            }
        }
        else
        {
            m_grid = ElementGrid();
        }
    }
    color_auto = false;
    if (color_map.isEmpty() || color_map == "bc") color_map = "turbo";      // (a result the script gave a map of its own -- "share", "change" -- keeps it)
    const auto& c = m_channels[size_t(m_channel)];
    color_field = c.tree;
    color_lo = c.lo;
    color_hi = c.hi > c.lo ? c.hi : c.lo + 1e-6f * (1 + std::abs(c.lo));
    color_label = c.label;
    elem_dirty = true;
    base_channels = m_channels;
    for (int a = 0; a < 3; ++a) base_deform[a] = deform_tree[a];
    base_has_deform = has_deform;
    m_steps.clear();
    m_step = -1;
}

bool Shape::hasElements() const
{
    // (the elements carry the result's own values: not those of a step with its own fields or geometry)
    const Step* st = currentStep();
    if (st && (!st->channels.empty() || st->tree.is_valid())) return false;
    return m_grid.isMesh ? !m_grid.mtets.empty() : !m_grid.fraction.empty();
}

void Shape::applyChannel()
{
    if (!has_result || m_channels.empty()) return;
    const auto& c = m_channels[size_t(m_channel)];
    color_field = c.tree;
    color_lo = c.lo;
    color_hi = c.hi > c.lo ? c.hi : c.lo + 1e-6f * (1 + std::abs(c.lo));
    applyUserRange();                    // (H191: the ends typed in the legend for this channel stand over the values' own)
    color_label = c.label;
    const Step* st = currentStep();
    if (st && !st->channels.empty() && !st->tree.is_valid())
    {   // a step's own field on the result's mesh: evaluated when it is first shown, then kept
        const auto key = std::make_pair(m_step, m_channel);
        auto it = step_values.find(key);
        if (it == step_values.end() && mesh)
        {
            libfive::ArrayEvaluator e(c.tree);
            std::vector<float> vals;
            evalAll(e, mesh->verts, vals);
            if (getenv("FIELDES_STEP_DEBUG"))
            {
                float mn = 1e30f, mx = -1e30f; size_t zeros = 0, nans = 0;
                for (float v : vals) { if (v != v) { ++nans; continue; } mn = std::min(mn, v); mx = std::max(mx, v); if (v == 0) ++zeros; }
                const auto& base = size_t(m_channel) < channel_values.size() ? channel_values[size_t(m_channel)] : std::vector<float>();
                float bmn = 1e30f, bmx = -1e30f;
                for (float v : base) { bmn = std::min(bmn, v); bmx = std::max(bmx, v); }
                fprintf(stderr, "[steps] step %d channel %d (%s): %zu verts, values %g .. %g, %zu zeros, %zu nan; base %zu values %g .. %g; first %g %g %g vs base %g %g %g\n",
                        m_step, m_channel, c.name.toUtf8().constData(), vals.size(), mn, mx, zeros, nans, base.size(), bmn, bmx,
                        vals.size() > 2 ? vals[0] : 0.f, vals.size() > 2 ? vals[1] : 0.f, vals.size() > 2 ? vals[2] : 0.f,
                        base.size() > 2 ? base[0] : 0.f, base.size() > 2 ? base[1] : 0.f, base.size() > 2 ? base[2] : 0.f);
            }
            it = step_values.emplace(key, std::move(vals)).first;
        }
        if (it != step_values.end()) color_values = it->second;
        else color_values.clear();
    }
    else if (size_t(m_channel) < channel_values.size()) color_values = channel_values[size_t(m_channel)];
    else color_values.clear();
}

const std::vector<Eigen::Vector3f>& Shape::dispNow()
{
    const Step* st = currentStep();
    if (st && st->hasDeform && !st->tree.is_valid())
    {   // a step's own deformation on the result's mesh: evaluated when it is first shown, then kept
        auto it = step_disp.find(m_step);
        if (it == step_disp.end() && mesh)
        {
            std::vector<Eigen::Vector3f> d(mesh->verts.size(), Eigen::Vector3f::Zero());
            std::vector<float> vals;
            for (int a = 0; a < 3; ++a)
            {
                libfive::ArrayEvaluator e(st->deform[a]);
                evalAll(e, mesh->verts, vals);
                for (size_t q = 0; q < d.size(); ++q) d[q][a] = vals[q];
            }
            if (getenv("FIELDES_STEP_DEBUG"))
            {
                float mx = 0, bmx = 0; size_t zeros = 0;
                for (const auto& v : d) { mx = std::max(mx, v.norm()); if (v.norm() == 0) ++zeros; }
                for (const auto& v : disp) bmx = std::max(bmx, v.norm());
                fprintf(stderr, "[steps] step %d deform: %zu verts, largest %g (%zu zero), base %zu largest %g; first %g %g %g vs base %g %g %g\n",
                        m_step, d.size(), mx, zeros, disp.size(), bmx,
                        d.empty() ? 0.f : d[0].x(), d.empty() ? 0.f : d[0].y(), d.empty() ? 0.f : d[0].z(),
                        disp.empty() ? 0.f : disp[0].x(), disp.empty() ? 0.f : disp[0].y(), disp.empty() ? 0.f : disp[0].z());
            }
            it = step_disp.emplace(m_step, std::move(d)).first;
        }
        if (it != step_disp.end()) return it->second;
    }
    return disp;
}

void Shape::applyStepState()
{
    const Step* st = currentStep();
    m_channels = (st && !st->channels.empty()) ? st->channels : base_channels;
    m_channel = std::max(0, std::min(int(m_channels.size()) - 1, m_channel));
    if (st && st->hasDeform)
    {
        for (int a = 0; a < 3; ++a) deform_tree[a] = st->deform[a];
        has_deform = true;
    }
    else
    {
        for (int a = 0; a < 3; ++a) deform_tree[a] = base_deform[a];
        has_deform = base_has_deform;
    }
    for (int a = 0; a < 3; ++a) probe_disp[a].reset();
    probe_channel.reset();
    probe_channel_index = -1;
    m_deps_known = false;
    lines_dirty = true;
}

void Shape::rebuildEvaluators()
{
    // (only while no render runs: the render thread uses them)
    es.clear();
    for (unsigned i = 0; i < 8; ++i) es.emplace_back(libfive::Evaluator(tree, vars));
    color_evals.clear();
    probe_eval.reset();
    m_deps_known = false;
    es_stale = false;
    ++cache_tree_gen;
}

void Shape::setSteps(std::vector<Step> steps, int current)
{
    ensureField();
    // (a plain shape may have steps that bring their own geometry only: the body of a flow shape optimisation)
    if (!has_result)
        for (auto& st : steps) st.channels.clear();
    m_steps = std::move(steps);
    step_meshes.clear();
    step_values.clear();
    step_disp.clear();
    m_step = m_steps.empty() ? -1 : std::max(0, std::min(int(m_steps.size()) - 1, current));
    applyStepState();
    applyStepExact();
    {
        // The surfaces of the steps that have them are made ahead, by a thread of their own: stepping through the iterations then
        // finds them made (the first one shown is made at once if it is wanted before)
        std::set<StepSurface*> started;
        for (const auto& st : m_steps)
            if (st.surface && started.insert(st.surface.get()).second)
            {
                auto surface = st.surface;
                std::thread([surface] { surface->prepareAll(); }).detach();
            }
    }
    // (a step with its own geometry shown first: the first render is of it)
    const Step* st = currentStep();
    const libfive::Tree want = (st && st->tree.is_valid()) ? st->tree : base_tree;
    if (want.id() != tree.id())
    {
        tree = want;
        rebuildEvaluators();
    }
    applyChannel();
}

void Shape::applyStepExact()
{
    const Step* st = currentStep();
    m_step_exact = (st && st->surface) ? st->surface->mesh(st->surfaceStep) : nullptr;
}

QString Shape::stepLabel() const
{
    const Step* st = currentStep();
    return st ? st->label : QString();
}

void Shape::stashStepMesh()
{
    // The finished mesh of the geometry shown (a step's own, or the result's) is kept while another is shown
    if (!mesh || running || target_div != 0) return;
    StepMesh sm;
    sm.mesh = mesh;
    sm.region = render_bounds;
    sm.channels = channel_values;
    sm.disp = disp;
    step_meshes[geometryKey()] = std::move(sm);
}

void Shape::setStep(int k)
{
    if (m_steps.empty() || k < 0 || k >= int(m_steps.size()) || k == m_step) return;
    ensureField();
    stashStepMesh();
    m_step = k;
    applyStepState();
    applyStepExact();
    const Step& st = m_steps[size_t(k)];
    const libfive::Tree want = st.tree.is_valid() ? st.tree : base_tree;
    if (want.id() != tree.id())
    {
        tree = want;
        es_stale = true;
        auto it = step_meshes.find(geometryKey());
        if (it != step_meshes.end())
        {   // meshed before: shown at once
            mesh = it->second.mesh;
            render_bounds = it->second.region;
            channel_values = it->second.channels;
            disp = it->second.disp;
            step_values.clear();
            step_disp.clear();
        }
        else if (default_div != MESH_DIV_EMPTY)
        {   // meshed when first shown (what is shown stays until it is)
            if (running) mesh_settings.cancel.store(true);
            startRender({next.settings, MESH_DIV_NEW_TREE, next.alg});
        }
    }
    applyChannel();
    gl_ready = false;
    elem_dirty = true;
    emit(redraw());
}

void Shape::setFlowLines(std::vector<FlowLine> lines, float lo, float hi)
{
    flow_lines = std::move(lines);
    flow_lo = lo;
    flow_hi = hi > lo ? hi : lo + 1e-6f * (1 + std::abs(lo));
    lines_dirty = true;
}

const std::vector<Shape::FlowLine>* Shape::currentLines() const
{
    const Step* st = currentStep();
    return (st && st->hasLines) ? &st->lines : &flow_lines;
}

bool Shape::hasFlowLines() const
{
    const auto* l = currentLines();
    return l && !l->empty();
}

void Shape::setShowFlowLines(bool b)
{
    if (b == m_show_lines) return;
    m_show_lines = b;
    emit(redraw());
}

void Shape::advanceFlow(float dt)
{
    // (a particle crosses the longest line in eight seconds)
    if (!(flow_period > 0)) return;
    flow_time = std::fmod(flow_time + dt * flow_period / 8.0f, flow_period);
}

void Shape::buildFlowLines()
{
    initializeOpenGLFunctions();
    const auto& lines = *currentLines();
    std::vector<GLfloat> v;
    flow_period = 0;
    for (const auto& l : lines)
    {
        if (!l.empty()) flow_period = std::max(flow_period, l.back()[4]);
        for (size_t i = 0; i + 1 < l.size(); ++i)
            for (int e = 0; e < 2; ++e)
            {
                const auto& p = l[i + size_t(e)];
                const float t = std::max(0.0f, std::min(1.0f, (p[3] - flow_lo) / (flow_hi - flow_lo)));
                float r, g, b;
                colormapRGB("turbo", t, r, g, b);
                v.insert(v.end(), {p[0], p[1], p[2], r, g, b});
            }
    }
    line_verts = int(v.size() / 6);
    auto attributes = [&]() {
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(GLfloat), NULL);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(GLfloat), (GLvoid*)(3 * sizeof(GLfloat)));
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
    };
    if (!line_vao.isCreated()) line_vao.create();
    line_vao.bind();
    if (!line_vbo.isCreated())
    {
        line_vbo.create();
        line_vbo.setUsagePattern(QOpenGLBuffer::StaticDraw);
    }
    line_vbo.bind();
    line_vbo.allocate(v.empty() ? nullptr : v.data(), int(v.size() * sizeof(GLfloat)));
    attributes();
    line_vao.release();
    if (!streak_vao.isCreated())
    {
        streak_vao.create();
        streak_vao.bind();
        streak_vbo.create();
        streak_vbo.setUsagePattern(QOpenGLBuffer::DynamicDraw);
        streak_vbo.bind();
        streak_vbo.allocate(nullptr, 0);
        attributes();
        streak_vao.release();
    }
    lines_dirty = false;
}

void Shape::drawFlowLines(const QMatrix4x4& M)
{
    if (lines_dirty) buildFlowLines();
    if (line_verts == 0) return;
    Shader::basic->bind();
    glUniformMatrix4fv(Shader::basic->uniformLocation("M"), 1, GL_FALSE, M.data());
    glUniform1i(Shader::basic->uniformLocation("shading"), 0);
    glUniform1i(Shader::basic->uniformLocation("use_rest"), 0);
    glUniform4f(Shader::basic->uniformLocation("color_mul"), 0.75f, 0.75f, 0.75f, 1.0f);
    glUniform4f(Shader::basic->uniformLocation("color_add"), 0.0f, 0.0f, 0.0f, 0.0f);
    glLineWidth(1.0f);
    line_vao.bind();
    glDrawArrays(GL_LINES, 0, line_verts);
    line_vao.release();

    // The particles: a short bright streak on every line every tenth of the longest line's time, released
    // again at the seed when it has gone the whole line, moved on by advanceFlow.  A streak is born at the seed
    // (its head leaves it and the tail follows: it grows) and dies the same way at the end of the line: the head
    // stops there and the tail runs on into it until the streak is gone -- not cut off whole when its head arrives
    const auto& lines = *currentLines();
    const float P = flow_period;
    std::vector<GLfloat> v;
    if (P > 0)
    {
        const int n = 10;
        const float S = P / n, L = 0.05f * P;
        for (const auto& l : lines)
        {
            if (l.size() < 2) continue;
            const float T = l.back()[4];
            for (int j = 0; j < n; ++j)
            for (int lap = 0; lap < 2; ++lap)       // (a streak at the end of the longest line is still being swallowed when the next one is released: its tail is in the lap before)
            {
                const float tau = std::fmod(flow_time + j * S, P) + lap * P;
                if (tau <= 0 || tau - L >= T) continue;
                const float t0 = tau - L;
                float prev[3] = {0, 0, 0};
                bool have = false;
                auto addPoint = [&](const float* p) {
                    if (have) v.insert(v.end(), {prev[0], prev[1], prev[2], 1.0f, 1.0f, 1.0f, p[0], p[1], p[2], 1.0f, 1.0f, 1.0f});
                    prev[0] = p[0];
                    prev[1] = p[1];
                    prev[2] = p[2];
                    have = true;
                };
                auto at = [&](size_t i, float t, float* out) {   // the point at time t between points i and i + 1
                    const auto& a = l[i];
                    const auto& b = l[i + 1];
                    const float u = std::max(0.0f, std::min(1.0f, (t - a[4]) / std::max(b[4] - a[4], 1e-9f)));
                    for (int c = 0; c < 3; ++c) out[c] = a[size_t(c)] + u * (b[size_t(c)] - a[size_t(c)]);
                };
                // the first point after t0, the points up to tau, the point at tau
                size_t i0 = 0;
                while (i0 < l.size() && l[i0][4] <= t0) ++i0;
                float p[3];
                if (i0 == 0) addPoint(l[0].data());
                else { at(i0 - 1, t0, p); addPoint(p); }
                size_t i = i0;
                for (; i < l.size() && l[i][4] <= tau; ++i) addPoint(l[i].data());
                if (i < l.size() && i > 0) { at(i - 1, tau, p); addPoint(p); }
            }
        }
    }
    if (!v.empty())
    {
        glUniform4f(Shader::basic->uniformLocation("color_mul"), 1.0f, 1.0f, 1.0f, 1.0f);
        glLineWidth(2.5f);
        streak_vao.bind();
        streak_vbo.bind();
        streak_vbo.allocate(v.data(), int(v.size() * sizeof(GLfloat)));
        glDrawArrays(GL_LINES, 0, GLsizei(v.size() / 6));
        streak_vao.release();
        glLineWidth(1.0f);
    }
    Shader::basic->release();
}

void Shape::setChannel(int ch)
{
    if (!has_result || ch < 0 || ch >= int(m_channels.size()) || ch == m_channel) return;
    m_channel = ch;
    applyChannel();
    gl_ready = false;
    elem_dirty = true;
    emit(redraw());
}

void Shape::setDeformScale(float s)
{
    if (!has_deform || s == m_deform) return;
    m_deform = s;
    gl_ready = false;
    elem_dirty = true;
    emit(redraw());
}

void Shape::setCellsOnly(bool b)
{
    m_cells_only = b;
    if (b) setShowElements(true);
}

void Shape::setShowElements(bool b)
{
    if (m_cells_only) b = true;
    if (b == m_show_elements) return;
    m_show_elements = b;
    elem_dirty = true;
    emit(redraw());
}

void Shape::setElementSection(bool enabled, const QVector4D& plane, bool whole)
{
    if (enabled == m_sec_enabled && whole == m_sec_whole && plane == m_sec_plane) return;
    const bool matters = m_show_elements && (whole || m_sec_whole) &&
                         (enabled || m_sec_enabled);
    m_sec_enabled = enabled;
    m_sec_whole = whole;
    m_sec_plane = plane;
    if (matters)
    {
        elem_dirty = true;
        emit(redraw());
    }
}

void Shape::buildElements()
{
    elem_dirty = false;
    elem_tris = elem_lines = 0;
    elem_probe.clear();
    if (!hasElements()) return;
    if (m_grid.isMesh)
    {
        buildMeshElements();
        return;
    }
    const int nx = m_grid.dims[0], ny = m_grid.dims[1], nz = m_grid.dims[2];
    const float h = m_grid.h;
    const QVector3D lo = m_grid.lo;
    auto eidx = [&](int i, int j, int k) { return (size_t(k) * ny + j) * nx + i; };
    auto nidx = [&](int i, int j, int k) { return (size_t(k) * (ny + 1) + j) * (nx + 1) + i; };
    auto centre = [&](int i, int j, int k) {
        return lo + QVector3D(i + 0.5f, j + 0.5f, k + 0.5f) * h;
    };
    const bool cut = m_sec_enabled && m_sec_whole;
    auto kept = [&](int i, int j, int k) {
        if (i < 0 || j < 0 || k < 0 || i >= nx || j >= ny || k >= nz) return false;
        if (!(m_grid.fraction[eidx(i, j, k)] > 0)) return false;
        if (!cut) return true;
        const QVector3D c = centre(i, j, k);
        return QVector4D::dotProduct(QVector4D(c, 1.0f), m_sec_plane) >= 0;
    };

    // The displacements at the grid nodes (the solver's; computed once, then reused)
    const size_t nNodes = size_t(nx + 1) * (ny + 1) * (nz + 1);
    if (has_deform && node_disp.size() != nNodes)
    {
        std::vector<Eigen::Vector3f> pts(nNodes);
        for (int k = 0; k <= nz; k++)
            for (int j = 0; j <= ny; j++)
                for (int i = 0; i <= nx; i++)
                {
                    const QVector3D p = lo + QVector3D(i, j, k) * h;
                    pts[nidx(i, j, k)] = Eigen::Vector3f(p.x(), p.y(), p.z());
                }
        node_disp.assign(nNodes, Eigen::Vector3f::Zero());
        for (int a = 0; a < 3; a++)
        {
            libfive::ArrayEvaluator e(deform_tree[a]);
            std::vector<float> vals;
            evalAll(e, pts, vals);
            for (size_t q = 0; q < nNodes; q++) node_disp[q][a] = vals[q];
        }
    }
    auto rest = [&](int i, int j, int k) { return lo + QVector3D(i, j, k) * h; };
    auto node = [&](int i, int j, int k) {
        QVector3D p = lo + QVector3D(i, j, k) * h;
        if (has_deform && m_deform != 0 && !node_disp.empty())
        {
            const auto& u = node_disp[nidx(i, j, k)];
            p += m_deform * QVector3D(u.x(), u.y(), u.z());
        }
        return p;
    };

    // Each element's own value: the cells with a fraction > 0 in order (as
    // the solver numbers them), six values each for tetrahedra, one for
    // hexahedra
    const bool tetrahedra = m_grid.tetrahedra;
    const size_t per = tetrahedra ? 6 : 1;
    std::vector<size_t> slot(m_grid.fraction.size(), 0);
    {
        size_t n = 0;
        for (size_t q = 0; q < slot.size(); q++)
            if (m_grid.fraction[q] > 0) slot[q] = n++;
    }
    const std::vector<float>& values = m_grid.values[size_t(m_channel)];
    float vlo, vhi;
    elementRange(vlo, vhi);                 // (the ends of the legend: the elements' own, or the ones typed in it -- H191)
    const float span = vhi - vlo;

    std::vector<GLfloat> tri, line;
    auto vertex = [&](std::vector<GLfloat>& out, const QVector3D& P, const QVector3D& R,
                      float r, float g, float b) {
        out.insert(out.end(), {P.x(), P.y(), P.z(), r, g, b, R.x(), R.y(), R.z()});
    };
    auto colourOf = [&](float value, float& r, float& g, float& b) {
        colormapRGB(color_map, span > 0 ? (value - vlo) / span : 0.5f, r, g, b);
    };
    // A face of an element that borders a missing (or cut-away) one: a triangle
    // (of a tetrahedron) or a quad (of a hexahedron), filled with the element's
    // colour and outlined
    auto addFace = [&](const QVector3D* P, const QVector3D* R, int corners, float value) {
        float r, g, b;
        colourOf(value, r, g, b);
        static const int tris[6] = {0, 1, 2, 0, 2, 3};
        const int n = corners == 3 ? 3 : 6;
        for (int q = 0; q < n; q++) vertex(tri, P[tris[q]], R[tris[q]], r, g, b);
        for (int q = 0; q < corners; q++)
        {
            const int q2 = (q + 1) % corners;
            vertex(line, P[q], R[q], 0.08f, 0.1f, 0.12f);
            vertex(line, P[q2], R[q2], 0.08f, 0.1f, 0.12f);
        }
        for (int q = 0; q < (corners == 3 ? 1 : 2); q++)
        {
            ElemTri e;
            e.value = value;
            for (int a = 0; a < 3; a++) e.p[a] = P[q == 0 ? a : (a == 0 ? 0 : a + 1)];
            elem_probe.push_back(e);
        }
    };

    const int dirs[6][3] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
    // the tetrahedra of a cell: for each order (a, b, c) of the axes, the corners
    // 000, e_a, e_a + e_b and 111 (the subdivision the solver used)
    static const int order[6][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
    for (int k = 0; k < nz; k++)
        for (int j = 0; j < ny; j++)
            for (int i = 0; i < nx; i++)
            {
                if (!kept(i, j, k)) continue;
                const size_t base = slot[eidx(i, j, k)] * per;
                if (tetrahedra)
                {
                    for (int t = 0; t < 6; t++)
                    {
                        int p[4][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {1, 1, 1}};
                        p[1][order[t][0]] = 1;
                        p[2][order[t][0]] = 1;
                        p[2][order[t][1]] = 1;
                        // its four faces (each omits one corner); the ones lying in a
                        // face of the cell can be seen from outside
                        for (int omit = 0; omit < 4; omit++)
                        {
                            int c[3], m = 0;
                            for (int q = 0; q < 4; q++)
                                if (q != omit) c[m++] = q;
                            int cellFace = -1;
                            for (int ax = 0; ax < 3 && cellFace < 0; ax++)
                                for (int s = 0; s < 2; s++)
                                    if (p[c[0]][ax] == s && p[c[1]][ax] == s && p[c[2]][ax] == s)
                                    {
                                        cellFace = 2 * ax + s;
                                        break;
                                    }
                            if (cellFace < 0) continue;
                            if (kept(i + dirs[cellFace][0], j + dirs[cellFace][1], k + dirs[cellFace][2])) continue;
                            QVector3D P[3], R[3];
                            for (int q = 0; q < 3; q++)
                            {
                                P[q] = node(i + p[c[q]][0], j + p[c[q]][1], k + p[c[q]][2]);
                                R[q] = rest(i + p[c[q]][0], j + p[c[q]][1], k + p[c[q]][2]);
                            }
                            // counter-clockwise seen from outside
                            const QVector3D nrm = QVector3D::crossProduct(R[1] - R[0], R[2] - R[0]);
                            if (nrm[cellFace / 2] * ((cellFace % 2) ? 1.0f : -1.0f) < 0)
                            {
                                std::swap(P[1], P[2]);
                                std::swap(R[1], R[2]);
                            }
                            addFace(P, R, 3, values[base + size_t(t)]);
                        }
                    }
                    continue;
                }
                for (int d = 0; d < 6; d++)
                {
                    if (kept(i + dirs[d][0], j + dirs[d][1], k + dirs[d][2])) continue;
                    // the face's four corners, counter-clockwise from outside
                    const int ax = d / 2, side = d % 2;
                    int c[4][3];
                    const int u = (ax + 1) % 3, v = (ax + 2) % 3;
                    const int corners[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
                    for (int q = 0; q < 4; q++)
                    {
                        const int qq = side ? q : 3 - q;
                        c[q][ax] = side;
                        c[q][u] = corners[qq][0];
                        c[q][v] = corners[qq][1];
                    }
                    QVector3D P[4], R[4];
                    for (int q = 0; q < 4; q++)
                    {
                        P[q] = node(i + c[q][0], j + c[q][1], k + c[q][2]);
                        R[q] = rest(i + c[q][0], j + c[q][1], k + c[q][2]);
                    }
                    addFace(P, R, 4, values[base]);
                }
            }
    elem_tris = int(tri.size() / 9);
    elem_lines = int(line.size() / 9);
    measureElementEdges(line);
    auto upload = [&](QOpenGLVertexArrayObject& vao_, QOpenGLBuffer& vbo, const std::vector<GLfloat>& data) {
        if (!vbo.isCreated())
        {
            vbo.create();
            vbo.setUsagePattern(QOpenGLBuffer::DynamicDraw);
        }
        if (!vao_.isCreated()) vao_.create();
        vao_.bind();
        vbo.bind();
        vbo.allocate(data.data(), int(data.size() * sizeof(GLfloat)));
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(GLfloat), NULL);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(GLfloat), (GLvoid*)(3 * sizeof(GLfloat)));
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(GLfloat), (GLvoid*)(6 * sizeof(GLfloat)));
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glEnableVertexAttribArray(3);
        vao_.release();
    };
    upload(elem_vao, elem_vbo, tri);
    upload(elem_line_vao, elem_line_vbo, line);
}

// H193 (a thin plate of 303,584 elements came out SOLID BLACK in the window: the outline of every element, 5 pixels across, covers the colours): the mean length of the outline segments and
// the middle of them, so that the outlines can be left out where the elements are only a few pixels across (drawElements)
void Shape::measureElementEdges(const std::vector<float>& line)
{
    elem_edge = 0;
    elem_centre = QVector3D(0, 0, 0);
    const size_t segments = line.size() / 18;               // (two vertices of nine floats each)
    if (segments == 0) return;
    const size_t step = std::max<size_t>(1, segments / 20000);
    double sum = 0;
    QVector3D centre(0, 0, 0);
    size_t count = 0;
    for (size_t s = 0; s < segments; s += step)
    {
        const QVector3D a(line[18 * s], line[18 * s + 1], line[18 * s + 2]);
        const QVector3D b(line[18 * s + 9], line[18 * s + 10], line[18 * s + 11]);
        sum += double((b - a).length());
        centre += 0.5f * (a + b);
        ++count;
    }
    elem_edge = float(sum / double(count));
    elem_centre = centre / float(count);
}

void Shape::buildMeshElements()
{
    const ElementGrid& g = m_grid;
    const size_t nt = g.mtets.size() / 4;
    const bool cut = m_sec_enabled && m_sec_whole;
    const bool moved = has_deform && m_deform != 0;
    auto rest = [&](int v) {
        return QVector3D(g.mverts[3 * size_t(v)], g.mverts[3 * size_t(v) + 1], g.mverts[3 * size_t(v) + 2]);
    };
    auto pos = [&](int v) {
        QVector3D p = rest(v);
        if (moved) p += m_deform * QVector3D(g.mdisp[3 * size_t(v)], g.mdisp[3 * size_t(v) + 1], g.mdisp[3 * size_t(v) + 2]);
        return p;
    };
    // The edges of the drawn tetrahedra that are not an edge of their element (the diagonal across a face of a cube) are not drawn: a hexahedron has its twelve edges and nothing else (H203)
    std::unordered_set<uint64_t> hiddenEdges;
    hiddenEdges.reserve(g.mhidden.size());
    for (size_t i = 0; i + 1 < g.mhidden.size(); i += 2)
    {
        const uint64_t a = uint64_t(std::min(g.mhidden[i], g.mhidden[i + 1])), b = uint64_t(std::max(g.mhidden[i], g.mhidden[i + 1]));
        hiddenEdges.insert((a << 32) | b);
    }
    auto edgeShown = [&](int u, int v) {
        if (hiddenEdges.empty()) return true;
        const uint64_t a = uint64_t(std::min(u, v)), b = uint64_t(std::max(u, v));
        return hiddenEdges.find((a << 32) | b) == hiddenEdges.end();
    };
    // (the section plane is in the undeformed model; an element is kept by its centre -- a tetrahedron by its own, an element made of tetrahedra by the mean of theirs, so that it is cut away whole)
    std::vector<char> kept;
    if (cut)
    {
        kept.assign(nt, 0);
        std::vector<QVector3D> centre(nt);
        for (size_t t = 0; t < nt; ++t)
        {
            QVector3D c(0, 0, 0);
            for (int p = 0; p < 4; ++p) c += rest(g.mtets[4 * t + size_t(p)]);
            centre[t] = c / 4.0f;
        }
        if (!g.mowner.empty())
        {
            int32_t cells = 0;
            for (int32_t o : g.mowner) cells = std::max(cells, o + 1);
            std::vector<QVector3D> sum(size_t(cells), QVector3D(0, 0, 0));
            std::vector<int> count(size_t(cells), 0);
            for (size_t t = 0; t < nt; ++t)
            {
                sum[size_t(g.mowner[t])] += centre[t];
                ++count[size_t(g.mowner[t])];
            }
            for (size_t t = 0; t < nt; ++t)
            {
                const size_t o = size_t(g.mowner[t]);
                kept[t] = QVector4D::dotProduct(QVector4D(sum[o] / float(count[o]), 1.0f), m_sec_plane) >= 0;
            }
        }
        else
        {
            for (size_t t = 0; t < nt; ++t) kept[t] = QVector4D::dotProduct(QVector4D(centre[t], 1.0f), m_sec_plane) >= 0;
        }
    }
    const std::vector<float>& values = g.values[size_t(m_channel)];
    float vlo, vhi;
    elementRange(vlo, vhi);                 // (the ends of the legend: the elements' own, or the ones typed in it -- H191)
    const float span = vhi - vlo;

    std::vector<GLfloat> tri, line;
    auto vertex = [&](std::vector<GLfloat>& out, int v, float r, float gg, float b) {
        const QVector3D P = pos(v), R = rest(v);
        out.insert(out.end(), {P.x(), P.y(), P.z(), r, gg, b, R.x(), R.y(), R.z()});
    };
    // A face of a tetrahedron that borders none (or a cut-away one): its filled triangle
    // in the tetrahedron's colour and its outline
    auto addFace = [&](int a, int b, int c, float value) {
        float r, gg, bb;
        colormapRGB(color_map, span > 0 ? (value - vlo) / span : 0.5f, r, gg, bb);
        const int v[3] = {a, b, c};
        for (int q = 0; q < 3; ++q) vertex(tri, v[q], r, gg, bb);
        for (int q = 0; q < 3; ++q)
        {
            if (!edgeShown(v[q], v[(q + 1) % 3])) continue;         // (the diagonal of a face of an element that is not a tetrahedron)
            vertex(line, v[q], 0.08f, 0.1f, 0.12f);
            vertex(line, v[(q + 1) % 3], 0.08f, 0.1f, 0.12f);
        }
        ElemTri e;
        e.value = value;
        for (int q = 0; q < 3; ++q) e.p[q] = pos(v[q]);
        elem_probe.push_back(e);
    };
    if (!cut)
    {
        for (size_t f = 0; f < g.mfaces.size() / 3; ++f)
            addFace(g.mfaces[3 * f], g.mfaces[3 * f + 1], g.mfaces[3 * f + 2], values[size_t(g.mfaceTet[f])]);
    }
    else
    {
        for (size_t t = 0; t < nt; ++t)
        {
            if (!kept[t]) continue;
            for (int l = 0; l < 4; ++l)
            {
                const int n = g.madj[4 * t + size_t(l)];
                if (n >= 0 && kept[size_t(n)]) continue;
                int f[3], m = 0;
                for (int p = 0; p < 4; ++p) if (p != l) f[m++] = g.mtets[4 * t + size_t(p)];
                // normal out of the tetrahedron
                const QVector3D nrm = QVector3D::crossProduct(rest(f[1]) - rest(f[0]), rest(f[2]) - rest(f[0]));
                if (QVector3D::dotProduct(nrm, rest(g.mtets[4 * t + size_t(l)]) - rest(f[0])) > 0) std::swap(f[1], f[2]);
                addFace(f[0], f[1], f[2], values[t]);
            }
        }
    }
    elem_tris = int(tri.size() / 9);
    elem_lines = int(line.size() / 9);
    measureElementEdges(line);
    auto upload = [&](QOpenGLVertexArrayObject& vao_, QOpenGLBuffer& vbo, const std::vector<GLfloat>& data) {
        if (!vbo.isCreated())
        {
            vbo.create();
            vbo.setUsagePattern(QOpenGLBuffer::DynamicDraw);
        }
        if (!vao_.isCreated()) vao_.create();
        vao_.bind();
        vbo.bind();
        vbo.allocate(data.data(), int(data.size() * sizeof(GLfloat)));
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(GLfloat), NULL);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(GLfloat), (GLvoid*)(3 * sizeof(GLfloat)));
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(GLfloat), (GLvoid*)(6 * sizeof(GLfloat)));
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glEnableVertexAttribArray(3);
        vao_.release();
    };
    upload(elem_vao, elem_vbo, tri);
    upload(elem_line_vao, elem_line_vbo, line);
}

float Shape::displayLo() const
{
    if (has_result && hasUserRange()) return m_user_range.at(m_channel).first;            // (typed in the legend: H191)
    if (has_result && m_show_elements && hasElements() && size_t(m_channel) < m_elem_lo.size())
        return m_elem_lo[size_t(m_channel)];
    return color_lo;
}

float Shape::displayHi() const
{
    if (has_result && hasUserRange()) return m_user_range.at(m_channel).second;
    if (has_result && m_show_elements && hasElements() && size_t(m_channel) < m_elem_hi.size())
        return m_elem_hi[size_t(m_channel)];
    return color_hi;
}

// H191: the ends of the colour scale, as typed in the legend.  An empty range (high not above low) is not taken: the legend keeps what it had
void Shape::setUserRange(float lo, float hi)
{
    setUserRangeOf(rangeKey(), lo, hi);
}

void Shape::setUserRangeOf(int key, float lo, float hi)
{
    if (!std::isfinite(lo) || !std::isfinite(hi) || !(hi > lo)) return;
    if (!has_result && m_user_range.count(key) == 0) m_range_before = {color_lo, color_hi};
    m_user_range[key] = {lo, hi};
    if (key != rangeKey()) return;
    applyUserRange();
    gl_ready = false;
    elem_dirty = true;
    emit(redraw());
}

void Shape::clearUserRange()
{
    if (!m_user_range.erase(rangeKey())) return;
    if (has_result) applyChannel();
    else if (has_color && color_auto && !color_values.empty()) autoRangeFromValues();
    else { color_lo = m_range_before.first; color_hi = m_range_before.second; }
    gl_ready = false;
    elem_dirty = true;
    emit(redraw());
}

void Shape::applyUserRange()
{
    const auto it = m_user_range.find(rangeKey());
    if (it == m_user_range.end()) return;
    color_lo = it->second.first;
    color_hi = it->second.second;
}

// (the range of a colouring that is automatic: the lowest and the highest value of the vertices)
void Shape::autoRangeFromValues()
{
    float lo = std::numeric_limits<float>::infinity(), hi = -lo;
    for (float v : color_values)
    {
        if (std::isfinite(v)) { lo = std::min(lo, v); hi = std::max(hi, v); }
    }
    if (lo <= hi)
    {
        color_lo = lo;
        color_hi = hi > lo ? hi : lo + 1e-6f * (1.0f + std::abs(lo));
    }
}

// The ends the elements are coloured between: their own, or the ones typed in the legend
void Shape::elementRange(float& lo, float& hi) const
{
    const auto it = m_user_range.find(m_channel);
    if (has_result && it != m_user_range.end())
    {
        lo = it->second.first;
        hi = it->second.second;
        return;
    }
    lo = m_elem_lo[size_t(m_channel)];
    hi = m_elem_hi[size_t(m_channel)];
}

namespace {
// The fields the solver solves for (at the nodes); the others are computed from
// them, per element, and averaged at each node for the smooth display
bool solvedAtNodes(const QString& name)
{
    return name == "displacement" || name == "ux" || name == "uy" || name == "uz" ||
           name == "temperature";
}
}   // anonymous namespace

QString Shape::valueNote() const
{
    if (!has_result || m_channel < 0 || size_t(m_channel) >= m_channels.size()) return QString();
    if (m_hasValueNote) return m_valueNote;             // (the script says what the values are: they are no solver's)
    const bool solved = solvedAtNodes(m_channels[size_t(m_channel)].name);
    if (m_show_elements && hasElements()) return T("Element values");
    return solved ? T("Solver values at nodes") : T("Recovered at nodes");
}

QString Shape::valueNoteHelp() const
{
    if (!has_result || m_channel < 0 || size_t(m_channel) >= m_channels.size()) return QString();
    if (m_hasValueNote) return QString();
    const QString& name = m_channels[size_t(m_channel)].name;
    const bool solved = solvedAtNodes(name);
    QString help;
    if (m_show_elements && hasElements())
        help = solved ? T("Each element's value at its centre, from its solved node values.")
                      : T("Each element's own value, not averaged.");
    else
        help = solved ? T("The solver's values at the nodes, interpolated between them.")
                      : T("The elements' values recovered at each node (a plane fitted through the values of the elements round it, taken at the node) and interpolated between the nodes. "
                        "Peaks are lower than the elements' own: switch on Elements to see those.");
    // (the tester of version 1.2.0: "how can the stress be negative there?" -- the largest principal stress is below zero where the part is pressed in every direction)
    if (name == "max_principal")
        help += T("\n\nThe maximum principal stress is the largest of the three principal stresses at a point. "
                  "It is below zero where the part is pressed together in every direction there, as at a support or under a load that presses.");
    else if (name == "min_principal")
        help += T("\n\nThe minimum principal stress is the smallest of the three principal stresses at a point: "
                  "below zero where the material is pressed, above zero only where it is pulled in every direction.");
    return help;
}

QString Shape::probeLabel() const
{
    if (!has_result || m_channel < 0 || size_t(m_channel) >= m_channels.size()) return color_label;
    if (m_hasValueNote) return m_channels[size_t(m_channel)].label;
    if (m_show_elements && hasElements()) return color_label + " [element]";
    return solvedAtNodes(m_channels[size_t(m_channel)].name) ? color_label : color_label + " [recovered at nodes]";
}

namespace {
// The squared distance from p to the triangle abc (Ericson, Real-Time Collision Detection)
float distanceSquaredToTriangle(const QVector3D& p, const QVector3D& a, const QVector3D& b, const QVector3D& c)
{
    using V = QVector3D;
    const V ab = b - a, ac = c - a, ap = p - a;
    const float d1 = V::dotProduct(ab, ap), d2 = V::dotProduct(ac, ap);
    if (d1 <= 0 && d2 <= 0) return ap.lengthSquared();
    const V bp = p - b;
    const float d3 = V::dotProduct(ab, bp), d4 = V::dotProduct(ac, bp);
    if (d3 >= 0 && d4 <= d3) return bp.lengthSquared();
    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) return (p - (a + ab * (d1 / (d1 - d3)))).lengthSquared();
    const V cp = p - c;
    const float d5 = V::dotProduct(ab, cp), d6 = V::dotProduct(ac, cp);
    if (d6 >= 0 && d5 <= d6) return cp.lengthSquared();
    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) return (p - (a + ac * (d2 / (d2 - d6)))).lengthSquared();
    const float va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0)
        return (p - (b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6))))).lengthSquared();
    const float denom = 1.0f / (va + vb + vc);
    return (p - (a + ab * (vb * denom) + ac * (vc * denom))).lengthSquared();
}
}   // anonymous namespace

bool Shape::probeElement(const QVector3D& p, float& value)
{
    // (the triangles are built when the elements are drawn)
    if (elem_dirty || elem_probe.empty()) return false;
    float best = std::numeric_limits<float>::max();
    const ElemTri* hit = nullptr;
    for (const auto& e : elem_probe)
    {
        const float d = distanceSquaredToTriangle(p, e.p[0], e.p[1], e.p[2]);
        if (d < best)
        {
            best = d;
            hit = &e;
        }
    }
    // (p lies on the drawn surface: allow for the picking's depth precision)
    if (!hit || best > 9.0f * m_grid.h * m_grid.h) return false;
    value = hit->value;
    return true;
}

void Shape::drawElements(const QMatrix4x4& M, bool monochrome, QColor color)
{
    if (elem_dirty) buildElements();
    if (!elem_tris) return;
    // Whole elements: not cut by the section plane
    const bool clipping = glIsEnabled(GL_CLIP_DISTANCE0);
    if (wholeElements() && clipping) glDisable(GL_CLIP_DISTANCE0);

    Shader::basic->bind();
    glUniformMatrix4fv(Shader::basic->uniformLocation("M"), 1, GL_FALSE, M.data());
    if (monochrome)
    {
        glUniform1i(Shader::basic->uniformLocation("shading"), 0);
        glUniform4f(Shader::basic->uniformLocation("color_add"),
                    color.redF(), color.greenF(), color.blueF(), 1.0f);
        glUniform4f(Shader::basic->uniformLocation("color_mul"), 0, 0, 0, 0);
    }
    else
    {
        const float k = (grabbed || hover || selected) ? 1.0f : 0.92f;
        glUniform4f(Shader::basic->uniformLocation("color_mul"), k, k, k, 1.0f);
        glUniform4f(Shader::basic->uniformLocation("color_add"), 0, 0, 0, 0);
        glUniform1i(Shader::basic->uniformLocation("shading"), 3);
    }
    glUniform1i(Shader::basic->uniformLocation("use_rest"), 1);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.0f, 1.0f);
    elem_vao.bind();
    glDrawArrays(GL_TRIANGLES, 0, elem_tris);
    elem_vao.release();
    glDisable(GL_POLYGON_OFFSET_FILL);
    // The outline of the elements: left out where they are only a few pixels across (H193: the plate of 303,584 elements, each 5 pixels, was solid black -- the outlines covered the colours).  The
    // length of an element's edge on the screen is the mean length of the outline segments (made with them), at the middle of them, through the matrix of the picture, the largest of the three axes
    bool outline = !monochrome && elem_lines;
    if (outline && elem_edge > 0.0f)
    {
        GLint vp[4] = {0, 0, 0, 0};
        glGetIntegerv(GL_VIEWPORT, vp);
        const QVector4D c0 = M * QVector4D(elem_centre, 1.0f);
        float pixels = 0.0f;
        for (int a = 0; a < 3; ++a)
        {
            QVector3D d(0, 0, 0);
            d[a] = elem_edge;
            const QVector4D c1 = M * QVector4D(elem_centre + d, 1.0f);
            if (!(c0.w() > 1e-6f) || !(c1.w() > 1e-6f)) continue;
            const float dx = (c1.x() / c1.w() - c0.x() / c0.w()) * 0.5f * float(vp[2]);
            const float dy = (c1.y() / c1.w() - c0.y() / c0.w()) * 0.5f * float(vp[3]);
            pixels = std::max(pixels, std::sqrt(dx * dx + dy * dy));
        }
        if (pixels > 0.0f && pixels < 4.0f) outline = false;
    }
    if (outline)
    {
        glUniform1i(Shader::basic->uniformLocation("shading"), 0);
        glUniform4f(Shader::basic->uniformLocation("color_mul"), 1, 1, 1, 1);
        glUniform4f(Shader::basic->uniformLocation("color_add"), 0, 0, 0, 0);
        elem_line_vao.bind();
        glDrawArrays(GL_LINES, 0, elem_lines);
        elem_line_vao.release();
    }
    glUniform1i(Shader::basic->uniformLocation("use_rest"), 0);
    Shader::basic->release();
    if (wholeElements() && clipping) glEnable(GL_CLIP_DISTANCE0);
}

// The numbers of the script, copied into a shape that has them already.  Every shape of a run has all the numbers of the script (3321 of them for the 1107 points of a mesh) and the ids are the same
// in the same order from run to run, so the values are written in place: no node of the map is made or freed (the copy of the map made 1107 x 3321 of them at every release of a drag: with the
// find for each number in updateVars it was 1.4 ms a shape, 1.5 s on the window's thread, H90, the stall test of 2026-11-03)
static void assignNumbers(std::map<libfive::Tree::Id, float>& into, const std::map<libfive::Tree::Id, float>& from)
{
    if (into.size() == from.size())
    {
        auto a = into.begin();
        auto b = from.begin();
        for (; a != into.end() && a->first == b->first; ++a, ++b) a->second = b->second;
        if (a == into.end()) return;
    }
    into = from;
}

bool Shape::updateFrom(Shape* other)
{
    assert(other->id() == id());
    // (the same tree: only whether its gizmo is shown, and its pivot,
    // can differ, and whether it is locked)
    m_gizmo = other->m_gizmo;
    m_outlineMode = other->m_outlineMode;           // (when the gizmo of a profile or a line is shown: the mode of its gizmo button changes nothing in its tree)
    m_editLine = other->m_editLine;                 // (the line of the outline an image is an image of: what pressing its outline edits)
    m_editMap = other->m_editMap;
    m_editReversed = other->m_editReversed;
    m_frozenVars = other->m_frozenVars;
    m_sceneGizmoVars = other->m_sceneGizmoVars;
    m_symmetryImage = other->m_symmetryImage;
    m_symmetrySourceId = other->m_symmetrySourceId;
    m_sourceId = other->m_sourceId;
    m_followId = other->m_followId;
    m_followKind = other->m_followKind;
    m_symDef = other->m_symDef;
    m_locked = other->m_locked;
    m_linked = other->m_linked;
    m_isPoint = other->m_isPoint;
    // (what kind of point it is -- a corner, a spline point, a round corner: the picture of its dot -- is what the script says NOW.  A double click on a dot changes the kind in the text, the run makes a new shape that
    // stands for this one, and this one kept the kind it had: the dot changed only when the file was closed and opened -- the user, 2026-11-11, H172: "The point type icon does not change in real time")
    m_point_style = other->m_point_style;
    m_hasPointPos = other->m_hasPointPos;
    m_pointPos = other->m_pointPos;
    m_liveTerms = other->m_liveTerms;
    m_liveNow.clear();                              // (the script has run: the numbers are the script's now)
    m_appLabel = other->m_appLabel;
    m_appShown = other->m_appShown;
    bool started = false;
    // The part's own render (its cube and resolution) is what the script says NOW: roi_resolution gives other ones when the models
    // of the scene change.  If that changes what is meshed, this render goes again with it -- and only then
    // (a hint the script no longer gives -- the bin of a custom_resolution row -- goes: the shape is drawn at the scene's again)
    if ((other->has_hint || has_hint) &&
        (has_hint != other->has_hint || hint_lo != other->hint_lo || hint_hi != other->hint_hi || hint_res != other->hint_res ||
         hint_side != other->hint_side || hint_scene_res != other->hint_scene_res))
    {
        setRenderHint(other->hint_lo, other->hint_hi, other->hint_res, other->hint_side, other->hint_scene_res);
        if (m_geometry_known && wouldRenderDifferently(m_last_settings))
        {
            startRender(RenderSettings { m_last_settings, defaultDivFor(m_last_settings), m_last_alg });
            started = true;
        }
    }
    // The resolution of its own is what the script says NOW too (a number typed over in the code changes it, the bin of its row takes it away)
    if (other->m_res_override != m_res_override)
    {
        setResolutionOverride(other->m_res_override);
        if (!started && m_geometry_known && wouldRenderDifferently(m_last_settings))
        {
            startRender(RenderSettings { m_last_settings, defaultDivFor(m_last_settings), m_last_alg });
            started = true;
        }
    }
    // ...and so is the size of its smallest feature (the pitch of a thread made thinner by a handle)
    if (other->m_feature != m_feature)
    {
        setFeatureSize(other->m_feature);
        m_div_sized = false;                    // (its level is chosen again: it is capped by the feature)
        if (!started && m_geometry_known && wouldRenderDifferently(m_last_settings))
        {
            startRender(RenderSettings { m_last_settings, defaultDivFor(m_last_settings), m_last_alg });
            started = true;
        }
    }
    // The triangles a part is drawn from are what the script says NOW (moved, or the file imported again): if they changed, it is
    // drawn again (after the numbers below are the script's: they are part of what the triangles are)
    const bool exactChanged = other->m_exact != m_exact ||
        (other->m_exact && std::memcmp(other->m_exact_matrix, m_exact_matrix, sizeof(m_exact_matrix)) != 0);
    if (exactChanged)
    {
        m_exact = other->m_exact;
        std::memcpy(m_exact_matrix, other->m_exact_matrix, sizeof(m_exact_matrix));
        m_exact_vars = other->m_exact_vars;
    }
    m_cache_forced.store(other->m_cache_forced.load());
    if (other->m_cache_on.load() != m_cache_on.load())
    {
        m_cache_on.store(other->m_cache_on.load());
        cache_state.store(CACHE_OFF);
        if (m_cache_on.load()) started = keepCurrent();
        emit(cacheStateChanged());
    }
    // (the box the script says NOW, for the numbers it has now: it sizes the shape, for its resolution, where its own search finds none -- before the numbers are given, which may start a render)
    m_script_box_valid = other->m_script_box_valid;
    m_script_lo = other->m_script_lo;
    m_script_hi = other->m_script_hi;
    bool varsChanged = false;
    if (m_isPoint && no_paint)
    {
        // A point is not drawn from them and renders nothing, whichever of them changed: it takes the numbers that the new shape of this run was made with -- the map of 3321 numbers of a mesh of 1107
        // points -- as they are, by swapping, with no walk over them (the walk was 0.4 ms a shape, 0.5 s of every release of a dragged point, H90).  The new shape is discarded after this and takes the
        // old numbers with it
        vars.swap(other->vars);
    }
    else
    {
        assignNumbers(m_script_vars, other->vars);
        m_script_vars_known = true;
        varsChanged = updateVars(other->vars);
    }
    if (exactChanged && !varsChanged && !started && m_geometry_known && wouldRenderDifferently(m_last_settings))
    {
        startRender(RenderSettings { m_last_settings, defaultDivFor(m_last_settings), m_last_alg });
        started = true;
    }
    return varsChanged || started;
}

void Shape::buildDeps() const
{
    if (!m_deps_known)
    {
        // (the variables inside an oracle -- a mesh moved by a gizmo -- are not walked with the tree)
        std::function<void(const libfive::Tree&)> add = [&](const libfive::Tree& t) {
            if (!t.is_valid()) return;
            // (walk() of a tree that has remaps gives pointers into a flattened copy that is gone when it returns: it is flattened
            // here and kept while its nodes are looked at -- a tree moved by a gizmo has remaps, and this read freed memory)
            // (shared, with the calls kept, and the nodes of the fields that calls call walked too: the same variables and oracles as the inline expansion finds, in a time that does not grow with every
            // call.  The unshared inline expansion of the formula of a manufacturing result -- the part called dozens of times, each call of a field made of calls -- ran the window to 8 GB when the
            // mouse first rested on the thin red layer of a moulding result, the shape whose tree is that formula)
            const libfive::Tree flat = t.flatten(true, libfive::Tree::Calls::Keep);
            for (const auto* d : flat.walk_deep())
            {
                if (d->op() == libfive::Opcode::VAR_FREE) m_deps.insert(d);
                else if (d->op() == libfive::Opcode::ORACLE)
                {
                    for (const auto& dep : d->oracle_clause().dependencies()) add(dep);
                }
            }
        };
        // (the field of a sweep or a prism that is not made is not walked: the numbers it is made of are the ones of its points, which are below)
        if (m_field_ready.load() || !(hasSweep() || hasPrism() || hasMorph()))
        {
            // (a point is not drawn from its field: the numbers its tree is made of are read from the tree as it was made, without the optimized tree and the eight evaluators, 1 to 3 ms each -- 50 of them, the images
            // of the points of the horn, were made after every run that changed a number, the user, 2026-11-10: putting a point in a body takes seconds.  The tree as it was made has every number the optimized one has)
            if (m_isPoint && !m_field_ready.load())
            {
                add(built_tree);
            }
            else
            {
                ensureField();
                add(tree);
            }
        }
        // (a body of points: the numbers its control points are made of.  They are read from the plan of the direct reading, which walks every node once: a tree for each of the 2241 numbers of a mesh of 747
        // points was flattened and walked one by one, which is what the first step of a drag waited for, 714 ms in the log of the window, H90.  A control that has an operation the plan does not do -- an oracle --
        // is walked tree by tree as before)
        bool controlsKnown = false;
        if (!m_morph_controls.empty())
        {
            if (const std::shared_ptr<MorphPlan> plan = makeMorphPlan(m_morph_controls))
            {
                for (const auto& var : plan->vars) m_deps.insert(var.second);
                controlsKnown = true;
            }
        }
        if (!controlsKnown)
            for (const auto& t : m_morph_controls) add(t);
        add(color_field);
        for (const ControlPoint& c : m_controls)
        {
            for (int a = 0; a < 3; ++a)
            {
                add(c.pos[a]);
                if (c.hasRot) add(c.rot[a]);
            }
            if (c.hasScale) add(c.scale);
            if (c.hasInfluence) add(c.influence);
            if (c.hasRadius) add(c.radius);                 // (a round corner's radius: dragged by its arc)
        }
        for (const ControlPoint& c : m_prism_points)
        {
            for (int a = 0; a < 3; ++a)
            {
                add(c.pos[a]);
                if (c.hasRot) add(c.rot[a]);
            }
            if (c.hasScale) add(c.scale);
            if (c.hasInfluence) add(c.influence);
            if (c.hasRadius) add(c.radius);
        }
        add(m_prism_a);
        add(m_prism_b);
        for (const auto& g : m_prism_guard) m_deps.insert(g.first);
        for (const auto* list : {&m_sweep_profile, &m_sweep_path})
        {
            for (const ControlPoint& c : *list)
            {
                for (int a = 0; a < 3; ++a)
                {
                    add(c.pos[a]);
                    if (c.hasRot) add(c.rot[a]);
                }
                if (c.hasScale) add(c.scale);
                if (c.hasInfluence) add(c.influence);
                if (c.hasRadius) add(c.radius);
            }
        }
        add(m_sweep_twist);
        for (const auto& g : m_sweep_guard) m_deps.insert(g.first);
        for (int a = 0; a < 3; ++a)
        {
            add(m_sweep_move[a]);
            add(m_sweep_rotate[a]);
            add(m_sweep_scale[a]);
        }
        for (int a = 0; a < 3; ++a) add(deform_tree[a]);
        for (const auto& c : m_channels) add(c.tree);
        for (int a = 0; a < 3; ++a)
        {
            if (m_gizmo.move[a]) m_deps.insert(m_gizmo.move[a]);
            if (m_gizmo.rotate[a]) m_deps.insert(m_gizmo.rotate[a]);
            if (m_gizmo.scale[a]) m_deps.insert(m_gizmo.scale[a]);
        }
        if (m_gizmo.influence) m_deps.insert(m_gizmo.influence);
        m_deps_known = true;
    }
}

bool Shape::usesVar(libfive::Tree::Id id) const
{
    buildDeps();
    return m_deps.count(id) > 0;
}

bool Shape::updateVars(const std::map<libfive::Tree::Id, float>& vs)
{
    bool changed = false;

    // If all of the variable changes are small (< 1e-6), then this is
    // probably an update caused by a script re-evaluation, where the
    // textual form of the float is slightly different from the dragged
    // value.  In this case, we want to render at div = 0; otherwise, we
    // see a visual glitch when the script re-evaluates after a drag.
    int div = MESH_DIV_NEW_VARS_SMALL;
    std::vector<libfive::Tree::Id> used;            // (the numbers this shape uses that changed)

    // The numbers of the script that this shape has and that are not what it has: found by walking the two maps side by side when they hold the same ids (they do, for every shape of a run: a find
    // for each of the 3321 numbers of a mesh of 1107 points was most of the 1.4 ms a shape took, H90), by a find for each when they do not
    std::vector<std::pair<std::map<libfive::Tree::Id, float>::iterator, float>> differing;
    {
        bool sameIds = vs.size() == vars.size();
        if (sameIds)
        {
            auto a = vars.begin();
            auto b = vs.begin();
            for (; a != vars.end(); ++a, ++b)
            {
                if (a->first != b->first) { sameIds = false; break; }
                if (a->second != b->second) differing.emplace_back(a, b->second);
            }
        }
        if (!sameIds)
        {
            differing.clear();
            for (const auto& entry : vs)
            {
                const auto found = vars.find(entry.first);
                if (found != vars.end() && found->second != entry.second) differing.emplace_back(found, entry.second);
            }
        }
    }
    for (const auto& d : differing)
    {
        const auto va = d.first;
        const std::pair<const libfive::Tree::Id, float> v(va->first, d.second);
        {
            const float before = va->second;
            va->second = v.second;      // (kept: the next render takes the whole map)
            // A point is not painted: its number is kept and nothing is rendered, whichever numbers it uses.  Asking which it uses builds its field, 1 ms for each of the 1107 points of a mesh, which is the
            // 1.1 s that the first step of a drag waited for on the window's thread (H90, the stall test of 2026-11-03)
            if (m_isPoint && no_paint) continue;
            // ...but only a variable this shape uses makes it render again
            if (!usesVar(v.first))
            {
                continue;
            }
            if (fabs(before - v.second) > 1e-6)
            {
                div = MESH_DIV_NEW_VARS;
            }
            changed = true;
            used.push_back(v.first);
        }
    }

    // A point is not painted -- its dot is drawn where its numbers say, taken from `vars` -- so a number of its own that changes needs no mesh made again: the number is kept (above)
    // and nothing is rendered.  The drag of the gizmo of a point, or of the points a profile is made of, measured and meshed every one of them at every step (27 to 40 ms each on the
    // main thread in the first step: 143 ms for the four points of a square), for a mesh that nothing draws
    if (changed && m_isPoint && no_paint) return false;

    // A shape that is another one moved or mirrored, and the numbers that changed are the model's own geometry (a surface of it is pulled): it is not meshed -- it is the same
    // shape -- it takes the mesh of the model when that is made.  (The evaluators are given the numbers now: whatever renders it next is of them)
    if (changed && canFollow(used))
    {
        for (auto& e : es) e.updateVars(vars);
        color_evals.clear();
        probe_eval.reset();
        cache_vars = vars;
        ++cache_vars_gen;
        m_followPending = true;
        if (std::getenv("FIELDES_TIMING"))
            fprintf(stderr, "[vars] the shape of line %d follows the mesh of the shape of line %d\n", source_line + 1, m_follows ? m_follows->source_line + 1 : 0);
        return false;
    }

    if (changed)
    {
        if (std::getenv("FIELDES_TIMING"))
        {
            fprintf(stderr, "[vars] the shape of line %d renders again (a variable it uses changed)\n",
                    source_line + 1);
        }

        // Only abort non-default renders
        if (target_div != default_div)
        {
            mesh_settings.cancel.store(true);
        }

        // Start a special render operation that uses a flag in the div
        // field that tells the system load new var values before starting
        startRender({next.settings, div, next.alg});
    }

    return changed;
}

void Shape::linkSource(Shape* source)
{
    if (source == m_follows.data()) return;
    if (m_followConnection) QObject::disconnect(m_followConnection);
    m_followConnection = QMetaObject::Connection();
    m_follows = source;
    m_followPending = false;
    if (source && m_followKind != FOLLOW_NONE) m_followConnection = connect(source, &Shape::gotMesh, this, &Shape::adoptMeshFromSource);
}

bool Shape::plainMesh() const
{
    return mesh && !has_color && !has_result && !has_deform && !m_flat && !hasControls() && !m_isPoint && !exactCurrent() && !m_exact && m_steps.empty() &&
           !hasSweep() && !hasPrism() && !hasMorph();
}

bool Shape::canFollow(const std::vector<libfive::Tree::Id>& changed) const
{
    Shape* source = m_follows.data();
    // (idle: a shape that is being meshed goes on with that)
    if (!source || source == this || m_followKind == FOLLOW_NONE || !m_gizmo.present || running || next.div != MESH_DIV_EMPTY) return false;
    if (!plainMesh() || !source->plainMesh()) return false;
    if (m_followKind == FOLLOW_IMAGE && !m_gizmo.mapped) return false;
    if (m_followKind == FOLLOW_COPY && !source->m_gizmo.present) return false;
    // (only the numbers that make the model's own geometry: its gizmo's are drawn by moving its mesh, no mesh is made; the symmetry's change the map; a copy's own place is its own)
    const auto mine = gizmoVars(), theirs = source->gizmoVars();
    for (const auto id : changed)
    {
        if (!source->usesVar(id) || mine.count(id) || theirs.count(id) || m_frozenVars.count(id)) return false;
    }
    return true;
}

void Shape::adoptMeshFromSource()
{
    Shape* source = m_follows.data();
    if (!source || !m_followPending || running || next.div != MESH_DIV_EMPTY) return;
    auto meshAgain = [&]() {
        m_followPending = false;
        startRender(RenderSettings { m_last_settings, MESH_DIV_NEW_VARS, m_last_alg });
    };
    if (!source->mesh || !plainMesh() || !source->plainMesh()) { meshAgain(); return; }
    // What takes the model's mesh to this shape's: the map of the symmetry; or, for a copy, its own move and turn after the model's undone (both are moved and turned about the same
    // place, by their own numbers -- the model's scale is the copy's too)
    QMatrix4x4 X;
    GizmoNumbers made = m_meshGizmo;
    if (m_followKind == FOLLOW_IMAGE)
    {
        X = m_gizmo.map;
        made = source->m_meshGizmo;
    }
    else
    {
        if ((m_gizmo.about - source->m_gizmo.about).length() > 1e-3f) { meshAgain(); return; }
        const QVector3D one(1, 1, 1);
        bool invertible = false;
        const QMatrix4x4 back = source->gizmoMatrix(source->m_meshGizmo.move, source->m_meshGizmo.rotate, one).inverted(&invertible);
        if (!invertible) { meshAgain(); return; }
        made = gizmoNumbersIn(vars);
        X = gizmoMatrix(made.move, made.rotate, one) * back;
    }
    const auto& sv = source->mesh->verts;
    auto copy = std::make_shared<libfive::Mesh>();
    copy->verts.resize(sv.size());
    for (size_t i = 0; i < sv.size(); ++i)
    {
        const QVector3D p = X.map(QVector3D(sv[i].x(), sv[i].y(), sv[i].z()));
        copy->verts[i] = Eigen::Vector3f(p.x(), p.y(), p.z());
    }
    // (a mirror image faces the other way: its triangles are turned over)
    const bool flip = X.determinant() < 0;
    copy->branes.reserve(source->mesh->branes.size());
    for (const auto& t : source->mesh->branes)
    {
        auto u = t;
        if (flip) std::swap(u[1], u[2]);
        copy->branes.push_back(u);
    }
    mesh = copy;
    m_meshGizmo = made;
    refreshPreview();
    m_outlines.clear();
    m_outline_curves.clear();
    color_values.clear();
    step_values.clear();
    step_disp.clear();
    lines_dirty = true;
    prev_div = mesh_div;
    prev_seq = mesh_seq;
    prev_verts = mesh_verts;
    mesh_div = source->mesh_div;
    mesh_seq = source->mesh_seq;
    mesh_verts = copy->verts.size();
    {
        const auto& b = source->render_bounds;
        QVector3D lo, hi;
        for (int i = 0; i < 8; ++i)
        {
            const QVector3D at = X.map(QVector3D((i & 1) ? b.upper.x() : b.lower.x(), (i & 2) ? b.upper.y() : b.lower.y(), (i & 4) ? b.upper.z() : b.lower.z()));
            lo = i == 0 ? at : QVector3D(std::min(lo.x(), at.x()), std::min(lo.y(), at.y()), std::min(lo.z(), at.z()));
            hi = i == 0 ? at : QVector3D(std::max(hi.x(), at.x()), std::max(hi.y(), at.y()), std::max(hi.z(), at.z()));
        }
        render_bounds = libfive::Region<3>({lo.x(), lo.y(), lo.z()}, {hi.x(), hi.y(), hi.z()});
    }
    if (mesh_div == 0) m_followPending = false;           // (the model's finest level is here: nothing more is to come)
    gl_ready = false;
    emit(gotMesh());
}

void Shape::setPictureMesh(std::shared_ptr<libfive::Mesh> picture, float alpha)
{
    mesh = std::move(picture);
    mesh_verts = mesh ? mesh->verts.size() : 0;
    gl_ready = false;                       // (the next draw gives the new mesh to the graphics card)
    setOpacity(alpha);
}

void Shape::draw(const QMatrix4x4& M)
{
    if (mesh && !gl_ready)
    {
        initializeOpenGLFunctions();

        mesh_bounds = libfive::Region<3>({0,0,0}, {0,0,0});
        GLfloat* verts = new GLfloat[mesh->verts.size() * 10];
        unsigned i = 0;

        // Unpack vertices into a flat array that will loaded into OpenGL
        const auto& dd = dispNow();
        const bool deform = has_deform && m_deform != 0 && dd.size() == mesh->verts.size();
        // A patch of a surface is drawn at the top of its colour map -- except in the boundary-condition map, whose colours are the categories
        // (its top is the last one, gold): there it is the category its label names ("bc:6", a selected surface), the colour its key shows
        float patchT = 1.0f;
        if (has_cutoff && color_map == "bc")
        {
            const int category = color_label.section(':', 1).split(',', Qt::SkipEmptyParts).value(0).toInt();
            patchT = float(std::max(0, std::min(kBcCategories, category))) / float(kBcCategories);
        }
        // A continuous colour map is made per fragment from the interpolated value; the categories of the boundary conditions' map and a patch (one colour for all its vertices) are the colours themselves
        lut_in_use = has_color && color_values.size() == mesh->verts.size() && !has_cutoff && color_map != "bc";
        size_t vk = 0;
        for (auto& v0 : mesh->verts)
        {
            const Eigen::Vector3f v = deform ? Eigen::Vector3f(v0 + m_deform * dd[vk]) : Eigen::Vector3f(v0);
            vk++;
            const auto v_ = v.template cast<double>().array().eval();
            // Track mesh's bounding box
            if (i == 0)
            {
                mesh_bounds.lower = v_;
                mesh_bounds.upper = v_;
            }
            else
            {
                mesh_bounds.lower = mesh_bounds.lower.array().cwiseMin(v_);
                mesh_bounds.upper = mesh_bounds.upper.array().cwiseMax(v_);
            }

            // Position
            verts[i++] = v.x();
            verts[i++] = v.y();
            verts[i++] = v.z();

            // Color: white, or the colour field's value through the map (a patch of a surface: the colour at the
            // top of the map, on the vertices of the patch)
            const size_t vi = vk - 1;
            float outside = -1.0f;      // (above zero: the vertex is not drawn, see basic.frag)
            if (has_color && color_values.size() == mesh->verts.size())
            {
                const float span = color_hi - color_lo;
                const float t = has_cutoff ? patchT : (span > 0 ? (color_values[vi] - color_lo) / span : 0.5f);
                if (lut_in_use)
                {
                    // (the value, for the fragment shader to make the colour of: see lut_tex)
                    verts[i++] = std::min(1.0f, std::max(0.0f, t));
                    verts[i++] = 0.0f;
                    verts[i++] = 0.0f;
                }
                else
                {
                    float r, g, b;
                    colormapRGB(color_map, t, r, g, b);
                    verts[i++] = r;
                    verts[i++] = g;
                    verts[i++] = b;
                }
                if (has_cutoff) outside = color_values[vi] - color_cutoff;
                else if (has_floor) outside = color_floor - color_values[vi];      // (above zero below the floor: not drawn)
            }
            else
            {
                verts[i++] = 1;
                verts[i++] = 1;
                verts[i++] = 1;
            }
            // Undeformed position (where the section cuts)
            verts[i++] = v0.x();
            verts[i++] = v0.y();
            verts[i++] = v0.z();
            verts[i++] = outside;
        }
        vert_vbo.create();
        vert_vbo.setUsagePattern(QOpenGLBuffer::StaticDraw);
        vert_vbo.bind();
        vert_vbo.allocate(verts, i*sizeof(*verts));
        delete [] verts;

        uint32_t* tris = new uint32_t[mesh->branes.size() * 3];
        i = 0;
        for (auto& t: mesh->branes)
        {
            tris[i++] = t[0];
            tris[i++] = t[1];
            tris[i++] = t[2];
        }
        tri_vbo.create();
        tri_vbo.setUsagePattern(QOpenGLBuffer::StaticDraw);
        tri_vbo.bind();
        tri_vbo.allocate(tris, i*sizeof(*tris));
        delete [] tris;

        if (!vao.isCreated())
        {
            vao.create();
        }
        vao.bind();
        vert_vbo.bind();
        tri_vbo.bind();
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 10*sizeof(GLfloat), NULL);
        glVertexAttribPointer(
                1, 3, GL_FLOAT, GL_FALSE, 10 * sizeof(GLfloat),
                (GLvoid*)(3 * sizeof(GLfloat)));
        glVertexAttribPointer(
                3, 3, GL_FLOAT, GL_FALSE, 10 * sizeof(GLfloat),
                (GLvoid*)(6 * sizeof(GLfloat)));
        // (per vertex: above zero, not part of the patch of a surface)
        glVertexAttribPointer(
                4, 1, GL_FLOAT, GL_FALSE, 10 * sizeof(GLfloat),
                (GLvoid*)(9 * sizeof(GLfloat)));
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glEnableVertexAttribArray(3);
        glEnableVertexAttribArray(4);

        gl_ready = true;
    }

    if (gl_ready && m_show_elements && hasElements())
    {
        drawElements(M, false, QColor());
        return;
    }
    if (gl_ready)
    {
        auto s = (grabbed || hover) ? 1 : 0.9;

        Shader::basic->bind();
        const bool colored = has_color && color_values.size() == mesh->verts.size();
        if (colored)
        {   // The field's colours, lit; hover / selection brighten a little
            const float k = selected ? 1.0f : s;
            glUniform4f(Shader::basic->uniformLocation("color_mul"), k, k, k, opacity);
            glUniform4f(Shader::basic->uniformLocation("color_add"),
                        selected ? 0.12f : 0.0f, selected ? 0.10f : 0.0f, 0.0f, 0.0f);
        }
        else if (selected)
        {   // Model-tree selection: warm highlight
            glUniform4f(Shader::basic->uniformLocation("color_mul"),
                        1.0 * s, 0.82 * s, 0.42 * s, opacity);
            glUniform4f(Shader::basic->uniformLocation("color_add"),
                        0.10 * s, 0.10 * s, 0.05 * s, 0.0f);
        }
        else if (is2D())
        {   // A 2D shape: a very subtle light blue, so that it is told from a body at a glance
            glUniform4f(Shader::basic->uniformLocation("color_mul"),
                        0.94 * s, 0.84 * s, 0.85 * s, opacity);
            glUniform4f(Shader::basic->uniformLocation("color_add"),
                        0.03 * s, 0.21 * s, 0.28 * s, 0.0f);
        }
        else
        {
            glUniform4f(Shader::basic->uniformLocation("color_mul"),
                        0.96 * s, 0.75 * s, 0.63 * s, opacity);
            glUniform4f(Shader::basic->uniformLocation("color_add"),
                        0.03 * s, 0.21 * s, 0.26 * s, 0.0f);
        }
        glUniform1i(Shader::basic->uniformLocation("shading"), colored ? 3 : 2);
        const bool byValue = colored && lut_in_use;
        if (byValue)
        {
            // The table of the colour map, 1024 colours: the shader looks the interpolated value up in it (the vertices carry values, see lut_in_use)
            if (!lut_tex || lut_map != color_map)
            {
                if (lut_tex) glDeleteTextures(1, &lut_tex);
                const int n = 1024;
                std::vector<GLubyte> table(size_t(n) * 3);
                for (int k = 0; k < n; ++k)
                {
                    float r, g, b;
                    colormapRGB(color_map, float(k) / float(n - 1), r, g, b);
                    table[size_t(k) * 3] = GLubyte(std::lround(255.0f * std::min(1.0f, std::max(0.0f, r))));
                    table[size_t(k) * 3 + 1] = GLubyte(std::lround(255.0f * std::min(1.0f, std::max(0.0f, g))));
                    table[size_t(k) * 3 + 2] = GLubyte(std::lround(255.0f * std::min(1.0f, std::max(0.0f, b))));
                }
                glGenTextures(1, &lut_tex);
                glBindTexture(GL_TEXTURE_2D, lut_tex);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, n, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, table.data());
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                lut_map = color_map;
            }
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, lut_tex);
            glUniform1i(Shader::basic->uniformLocation("color_lut"), 0);
        }
        glUniform1i(Shader::basic->uniformLocation("lut_mode"), byValue ? 1 : 0);
        const QMatrix4x4 shown = previewed(M);          // (a shape that is being dragged is drawn where its numbers say now, from the mesh it has)
        glUniformMatrix4fv(Shader::basic->uniformLocation("M"),
                           1, GL_FALSE, shown.data());
        glUniform1i(Shader::basic->uniformLocation("use_rest"), 1);
        glUniform1i(Shader::basic->uniformLocation("patch_mode"), (has_cutoff || has_floor) ? 1 : 0);
        vao.bind();
        glDrawElements(GL_TRIANGLES, mesh->branes.size() * 3, GL_UNSIGNED_INT, NULL);
        vao.release();
        glUniform1i(Shader::basic->uniformLocation("lut_mode"), 0);
        glUniform1i(Shader::basic->uniformLocation("patch_mode"), 0);
        glUniform1i(Shader::basic->uniformLocation("use_rest"), 0);
        Shader::basic->release();
    }
}

void Shape::drawMonochrome(const QMatrix4x4& M, QColor color)
{
    if (gl_ready && m_show_elements && hasElements())
    {
        drawElements(M, true, color);
        return;
    }
    if (gl_ready)
    {
        Shader::basic->bind();
        const QMatrix4x4 shown = previewed(M);
        glUniformMatrix4fv(Shader::basic->uniformLocation("M"),
                           1, GL_FALSE, shown.data());
        glUniform1i(Shader::basic->uniformLocation("shading"), 0);
        glUniform4f(Shader::basic->uniformLocation("color_add"),
                color.redF(), color.greenF(), color.blueF(), 1.0f);
        glUniform4f(Shader::basic->uniformLocation("color_mul"), 0, 0, 0, 0);
        glUniform1i(Shader::basic->uniformLocation("use_rest"), 1);
        glUniform1i(Shader::basic->uniformLocation("patch_mode"), (has_cutoff || has_floor) ? 1 : 0);

        vao.bind();
        glDrawElements(GL_TRIANGLES, mesh->branes.size() * 3, GL_UNSIGNED_INT, NULL);
        vao.release();
        glUniform1i(Shader::basic->uniformLocation("patch_mode"), 0);
        glUniform1i(Shader::basic->uniformLocation("use_rest"), 0);
        Shader::basic->release();
    }
}

void Shape::startRender(Settings s, libfive::BRepAlgorithm alg)
{
    // A point is not painted -- its dot is drawn where its numbers say -- so nobody sees its mesh: its extent is not searched for and it is not meshed at all.  The 692 points of a mesh that is imported as a body
    // of points (the user, 2026-11-03, H90: "it took a long time and when dragging points it sometimes froze the program") cost 20 s of the extent search and 56 s of the mesher's time -- 28 s of a window
    // that did not answer, again after every edit of the script, which a point let go of is -- for balls that nothing draws.  Such a shape has no render to wait for: done() is true, as it is for a shape
    // that has not been asked to render yet
    if (m_isPoint && no_paint)
    {
        if (std::getenv("FIELDES_TIMING")) fprintf(stderr, "[shape] line %d: a point is not rendered (no extent, no mesh)\n", source_line + 1);
        // (done() is `next.div == MESH_DIV_EMPTY && mesh_future.isFinished()`: a shape that never rendered has neither said: the first build with this change left every point not done, the bar "Rendering" for ever and the
        // window at the whole of a core -- bar-render: shapes 9 done 1 unknown 8.  A point has no render: nothing is waiting and nothing is running)
        if (!running)
        {
            next.div = MESH_DIV_EMPTY;
            mesh_future = QFuture<BoundedMesh>();
        }
        return;
    }
    const auto t0 = std::chrono::steady_clock::now();
    measureExtent(s);
    if (std::getenv("FIELDES_TIMING"))
        fprintf(stderr, "[shape] line %d: measureExtent %.0f ms (%s%s%s)\n", source_line + 1,
                1000.0 * std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(), m_ext_valid ? "found" : "none",
                m_ext_valid ? "" : ": ", m_ext_valid ? "" : extentWhy(s).c_str());
    if (!m_geometry_known || s != m_last_settings) m_div_sized = false;           // (another region or resolution: its level is chosen again)
    sizeFirstLevel(s);
    startRender(RenderSettings { s, defaultDivFor(s), alg });
}

void Shape::startRender(RenderSettings s)
{
    if (default_div == MESH_DIV_EMPTY)
    {
        default_div = s.div;
    }

    if (running)
    {
        if (next.div != MESH_DIV_ABORT)
        {
            // New numbers (a handle that is being pulled) do not cancel a render that is at the first level -- the quick one that a drag shows: they wait for it, the newest of them,
            // and the picture follows at the pace of the mesher.  Cancelled at every move, a level that takes longer than the time between two moves of the mouse (8 ms of a real
            // mouse; a level of tens) was never finished: the numbers in the script followed the hand and the shape stood still -- seconds -- until the hand rested.  A finer level that is
            // in progress is cancelled as ever
            const bool numbers = s.div == MESH_DIV_NEW_VARS || s.div == MESH_DIV_NEW_VARS_SMALL;
            if (!(numbers && target_div == default_div)) mesh_settings.cancel.store(true);
            next = s;
            next_follows = false;
        }
    }
    else
    {
        if (s.div == MESH_DIV_NEW_TREE || es_stale)
        {   // the shown step's geometry
            const auto tRebuild = std::chrono::steady_clock::now();
            rebuildEvaluators();
            if (std::getenv("FIELDES_TIMING"))
                fprintf(stderr, "[shape] line %d: rebuilt the evaluators in %.0f ms\n", source_line + 1,
                        1000.0 * std::chrono::duration<double>(std::chrono::steady_clock::now() - tRebuild).count());
            if (s.div == MESH_DIV_NEW_TREE)
            {
                const auto tExt = std::chrono::steady_clock::now();
                measureExtent(s.settings);
                if (std::getenv("FIELDES_TIMING"))
                    fprintf(stderr, "[shape] line %d: measureExtent (new tree) %.0f ms (%s)\n", source_line + 1,
                            1000.0 * std::chrono::duration<double>(std::chrono::steady_clock::now() - tExt).count(), m_ext_valid ? "found" : "none");
                s.div = default_div;
            }
        }
        {   // what this render evaluates at the vertices (the shown step's fields: the GUI may change them meanwhile)
            QString key;
            for (const auto& c : m_channels) key += QString("|%1").arg(quintptr(c.tree.id()));
            for (int a = 0; a < 3; ++a) key += QString("|%1").arg(quintptr(deform_tree[a].id()));
            if (key != run_key)
            {
                result_evals.clear();
                result_pool.clear();
                run_key = key;
            }
            run_channels = m_channels;
            for (int a = 0; a < 3; ++a) run_deform[a] = deform_tree[a];
            run_has_deform = has_deform;
            run_color_detail = color_detail;
            step_rendering = geometryKey();
            run_step_exact = m_step_exact;
        }
        if (s.div == MESH_DIV_NEW_VARS ||
            s.div == MESH_DIV_NEW_VARS_SMALL)
        {
            for (auto& e : es)
            {
                e.updateVars(vars);
            }
            // (the colour field's evaluators hold the numbers too: made again with the new ones)
            color_evals.clear();
            probe_eval.reset();
            // (what the render cache's keys are of: the numbers the evaluators hold now)
            cache_vars = vars;
            ++cache_vars_gen;
            if (s.div == MESH_DIV_NEW_VARS)
            {
                const auto tExt = std::chrono::steady_clock::now();
                measureExtent(s.settings);          // (the numbers changed: the shape is not where it was)
                if (std::getenv("FIELDES_TIMING"))
                    fprintf(stderr, "[shape] line %d: measureExtent (new numbers) %.0f ms (%s)\n", source_line + 1,
                            1000.0 * std::chrono::duration<double>(std::chrono::steady_clock::now() - tExt).count(), m_ext_valid ? "found" : "none");
                sizeFirstLevel(s.settings);
            }
            s.div = (s.div == MESH_DIV_NEW_VARS) ? default_div : 0;
        }

        target_div = s.div;
        m_runGizmo = gizmoNumbersIn(cache_vars);        // (the gizmo numbers this render's mesh will be of: the ones the evaluators hold)

        // (a level waiting in the thread pool has done nothing yet: without
        // this it would still show the last level's finished meshing)
        render_stage.store(0);
        render_progress.store(0.0);
        colour_progress.store(0.0);

        placeHint();
        // (the box of the body, as the render that starts now knows it: the worker reads these, not the members the GUI's search writes -- see renderCacheKey)
        run_ext_known = s.settings.autoScale && m_ext_valid && !m_ext_stale && !has_hint;
        run_ext_lo = m_ext_lo;
        run_ext_hi = m_ext_hi;
        // (the resolution of this level, before m_geometry is this render's: a box that is out of date does not raise it above the last one's -- see autoResolution)
        s.res = autoResolution(s.settings);
        if (std::getenv("FIELDES_TIMING"))
        {
            const QVector3D region = s.settings.max - s.settings.min, size = m_ext_hi - m_ext_lo;
            fprintf(stderr, "[shape] line %d: level %d is meshed at %.3g per mm (the scene's %.3g; box %s, %.3g long; region %.3g long)\n", source_line + 1, s.div, s.res,
                    double(s.settings.res), !m_ext_valid ? "NOT known" : m_ext_stale ? "the script's, out of date" : "known",
                    m_ext_valid ? double(std::max(size.x(), std::max(size.y(), size.z()))) : 0.0, double(std::max(region.x(), std::max(region.y(), region.z()))));
        }
        m_geometry = renderGeometry(s.settings);
        m_geometry_known = true;
        if (const char* log = std::getenv("FIELDES_SHAPE_LOG"))
        {
            // (for the tests: what this render is of -- the box and the resolution it is meshed at -- and the hint the script gave it)
            if (FILE* f = std::fopen(log, "a"))
            {
                std::fprintf(f, "render line %d: box (%.1f %.1f %.1f)-(%.1f %.1f %.1f) at %.3g per mm, quality %.3g;  hint %s (%.1f %.1f %.1f)-(%.1f %.1f %.1f) res %.3g"
                                " side %.1f scene %.3g\n",
                             sourceLine(), m_geometry.lo.x(), m_geometry.lo.y(), m_geometry.lo.z(), m_geometry.hi.x(), m_geometry.hi.y(),
                             m_geometry.hi.z(), double(m_geometry.res), double(m_geometry.quality), has_hint ? "yes" : "no", hint_lo.x(),
                             hint_lo.y(), hint_lo.z(), hint_hi.x(), hint_hi.y(), hint_hi.z(), double(hint_res), double(hint_side),
                             double(hint_scene_res));
                std::fclose(f);
            }
        }
        m_last_settings = s.settings;
        m_last_alg = s.alg;
        {   // (the vertices of this level are expected from the last one's: about four times as many for every halving of the cell)
            const double expected = mesh_verts > 0 ? double(mesh_verts) * (s.div < mesh_div ? 4.0 : 1.0) : 0.0;
            run_heavy = force_low || expected > 150000.0;
        }
        if (m_flat || hasControls() || hasPrism() || hasSweep() || hasMorph()) run_flat_vars = vars;
        m_run_virtual_on = m_virtual_on;
        m_run_virtual_after = m_virtual_after;
        m_run_virtual_pos = m_virtual_pos;
        updateFrameNumbers();
        run_has_frame = m_flat && m_frame_now_valid;
        for (int i = 0; i < 9; ++i) run_frame[i] = m_frame_now[i];
        timer.start();
        running = true;
#if QT_VERSION >= 0x060000
        mesh_future = QtConcurrent::run(&Shape::renderMesh, this, s);
#else
        mesh_future = QtConcurrent::run(this, &Shape::renderMesh, s);
#endif
        mesh_watcher.setFuture(mesh_future);

        next = {s.settings, s.div - 1, s.alg};
        next_follows = true;
    }
}

double Shape::renderFraction() const
{
    if (done()) return 1.0;
    const int cur = std::max(0, running ? target_div : next.div);
    const int first = std::max(cur, std::max(0, seq_first_div.load()));
    double total = 0, count = 0;
    // Within a level: the mesher's three phases, then what follows it, each
    // by its share of the time (as the previous level measured them)
    const double c = colour_share.load();
    const double v = render_progress.load();
    const double s1 = share_build.load(), s2 = share_walk.load(), s3 = std::max(0.0, 1 - s1 - s2);
    const double meshed = v < 1.0 / 3 ? s1 * 3 * v
                        : v < 2.0 / 3 ? s1 + s2 * (3 * v - 1)
                                      : s1 + s2 + s3 * std::min(1.0, 3 * v - 2);
    const double level = render_stage.load() == 0 ? (1 - c) * meshed
                                                  : (1 - c) + c * colour_progress.load();
    for (int d = first; d >= 0; d--)
    {
        const double w = std::pow(4.0, -d);
        total += w;
        if (d > cur) count += w;
        else if (d == cur && running) count += w * level;
    }
    return total > 0 ? count / total : 0.0;
}

double Shape::renderWeight() const
{
    if (mesh_div < 0 || mesh_verts == 0 || mesh_seq != seq_first_div.load()) return -1.0;
    double growth = 4.0;    // (about 4 x the cells for every halving of the cell size)
    if (prev_verts > 0 && prev_seq == mesh_seq && prev_div == mesh_div + 1)
        growth = std::min(8.0, std::max(2.0, double(mesh_verts) / double(prev_verts)));
    return double(mesh_verts) * std::pow(growth, mesh_div);
}

QString Shape::renderDebug() const
{
    return QString("weight %1 fraction %2 first %3 target %4 next %5 mesher %6 stage %7 running %8")
        .arg(renderWeight(), 0, 'g', 4).arg(renderFraction(), 0, 'f', 3).arg(seq_first_div.load())
        .arg(target_div).arg(next.div).arg(render_progress.load(), 0, 'f', 3).arg(render_stage.load())
        .arg(int(running));
}

bool Shape::done() const
{
    return next.div == MESH_DIV_EMPTY && mesh_future.isFinished();
}

////////////////////////////////////////////////////////////////////////////////

void Shape::setGrabbed(bool g)
{
    bool changed = grabbed != g;
    grabbed = g;
    if (changed)
    {
        emit(redraw());
    }
}

void Shape::setSelected(bool s)
{
    bool changed = selected != s;
    selected = s;
    if (changed)
    {
        emit(redraw());
    }
}

void Shape::setHover(bool h)
{
    bool changed = hover != h;
    hover = h;
    if (changed)
    {
        emit(redraw());
    }
}

////////////////////////////////////////////////////////////////////////////////

std::map<libfive::Tree::Id, float> Shape::dragWeights() const
{
    ensureField();
    return libfive::dragWeights(pullField());
}

std::pair<libfive::JacobianEvaluator*, libfive::Tape::Handle>
Shape::dragFrom(const QVector3D& v)
{
    ensureField();
    // A surface is pulled by a signed field: near its end (within the band the view gives: a few pixels) by the way past the end, so that the size changes, elsewhere by the side of the sheet
    m_pullEdge = false;
    if (m_unsigned && m_limit.is_valid() && m_pullBand > 0.0f)
    {
        libfive::JacobianEvaluator probe(m_limit, vars);
        const auto at = probe.valueAndPush({v.x(), v.y(), v.z()});
        m_pullEdge = std::fabs(at.first) < m_pullBand;
    }
    auto e = new libfive::JacobianEvaluator(pullField(), vars);
    auto o = e->valueAndPush({v.x(), v.y(), v.z()});
    return std::make_pair(e, o.second);
}

void Shape::deleteLater()
{
    if (running)
    {
        next.div = MESH_DIV_ABORT;
        mesh_settings.cancel.store(true);
    }
    else
    {
        QObject::deleteLater();
    }
}

void Shape::onFutureFinished()
{
    running = false;

    auto bm = mesh_future.result();
    // The mesh was read from the render cache: it is the finished one, whatever level asked for it
    const bool fromCache = bm.mesh != nullptr && cache_read.exchange(false);
    if (fromCache)
    {
        target_div = 0;
        if (next_follows) next.div = MESH_DIV_EMPTY;
    }
    if (m_cache_on.load()) emit(cacheStateChanged());
    if (bm.mesh != nullptr && step_rendering != geometryKey())
    {   // a render of a geometry no longer shown: kept when it is finished, not shown
        std::shared_ptr<libfive::Mesh> m(bm.mesh);
        if (target_div == 0)
        {
            StepMesh sm;
            sm.mesh = m;
            sm.region = bm.region;
            sm.channels = std::move(bm.channels);
            sm.disp = std::move(bm.disp);
            step_meshes[step_rendering] = std::move(sm);
        }
        bm.mesh = nullptr;
    }
    // H165 (the user, 2026-11-09, with the log of his own hand test: "THE TASK STATES TAHT WHILE MOVING THE GIZMO THIS HAPPENS AND WHEN YOU LET GO IT SNAPS BACK TO THE RIGHT POSITON"): the part and the pivot of its gizmo were the same
    // to 0.000 until a render that had been running when the gizmo was pressed finished in the middle of the drag; from then on the part was 7.8 mm from the pivot for as long as the gizmo was held, and the render after the
    // release put it right.  While a gizmo is held the mesh that is drawn moved is exact, and that mesh is of numbers that the render did not know: it is not replaced.  The render at the release takes the new numbers
    if (bm.mesh != nullptr && m_gizmoHeld && mesh)
    {
        delete bm.mesh;
        bm.mesh = nullptr;
    }
    if (bm.mesh != nullptr)
    {
        step_values.clear();
        step_disp.clear();
        lines_dirty = true;
        mesh.reset(bm.mesh);
        m_meshGizmo = m_runGizmo;                       // (the new mesh is of the numbers its render started with; shown moved when the shape has been dragged since)
        refreshPreview();
        m_outlines = std::move(bm.outlines);
        m_outline_curves = std::move(bm.curves);
        prev_div = mesh_div;
        prev_seq = mesh_seq;
        prev_verts = mesh_verts;
        mesh_div = target_div;
        mesh_seq = seq_first_div.load();
        mesh_verts = mesh->verts.size();
        render_bounds = bm.region;
        color_values = std::move(bm.values);
        if (has_result)
        {
            channel_values = std::move(bm.channels);
            disp = std::move(bm.disp);
            applyChannel();
        }
        if (has_color && color_auto && !color_values.empty()) autoRangeFromValues();
        applyUserRange();                       // (H191: the ends typed in the legend stand over the values' own, after every mesh)

        gl_ready = false;
        emit(gotMesh());

        // How long the level took the mesher.  It was the time from the render's start to the GUI's thread seeing it finished -- and the GUI's thread is busy while a drag goes on
        // (a search for the region at every move, the painting): every level then took "more than 50 ms", and the level a drag starts at went one coarser at every move, until a leg
        // was a wedge at the start of every move
        const double t = m_levelMs.load();
        if (std::getenv("FIELDES_TIMING"))
            fprintf(stderr, "[shape] line %d: level %d took the mesher %.1f ms (starts at %d)\n", source_line + 1, target_div, t, default_div);

        if (target_div == default_div && !fromCache)
        {
            if (t < 20 && default_div > 0)
            {
                default_div--;
            }
            else if (t > 50)
            {
                default_div++;
            }
        }
    }

    if (next.div == MESH_DIV_ABORT)
    {
        QObject::deleteLater();
    }
    else if (next.div >= 0 || next.div == MESH_DIV_NEW_VARS
                           || next.div == MESH_DIV_NEW_VARS_SMALL
                           || next.div == MESH_DIV_NEW_TREE)
    {
        startRender(next);
    }
    else
    {
        prefetchSteps();        // (idle: the steps whose geometry has not been meshed are made now, one after the other)
    }
}

void Shape::prefetchSteps()
{
    if (running || m_steps.size() < 2 || m_step < 0 || default_div == MESH_DIV_EMPTY || mesh == nullptr) return;
    // (only when the shown geometry is finished: the picture the user is looking at comes first)
    if (mesh_div != 0 || target_div != 0) return;
    const int shown = m_step;
    int k = -1;
    for (int i = 0; i < int(m_steps.size()); ++i)
    {
        if (i != shown && m_steps[size_t(i)].tree.is_valid() && !m_steps[size_t(i)].surface && !step_meshes.count(i)) { k = i; break; }
    }
    if (k < 0) return;
    ensureField();
    const libfive::Tree shownTree = tree;
    const bool shownTreeIsValid = shownTree.is_valid();
    stashStepMesh();                                    // (the finished mesh of the shown geometry is kept)
    // The render takes what the step is made of -- its geometry, its fields -- at its start: the shown step is put back at once.  The
    // evaluators are the step's until the render has read them (the next render of the shown geometry makes them again)
    m_step = k;
    applyStepState();
    tree = m_steps[size_t(k)].tree;
    rebuildEvaluators();
    force_low = true;
    startRender(RenderSettings{m_last_settings, 0, m_last_alg});        // (straight to the finest level: only a finished mesh is kept)
    force_low = false;
    m_step = shown;
    applyStepState();
    if (shownTreeIsValid) tree = shownTree;
    es_stale = true;
}

void Shape::freeGL()
{
    if (gl_ready)
    {
        vao.destroy();
        vert_vbo.destroy();
        tri_vbo.destroy();

        gl_ready = false;
    }
    if (lut_tex)
    {
        glDeleteTextures(1, &lut_tex);
        lut_tex = 0;
        lut_map.clear();
    }
    if (elem_vbo.isCreated())
    {
        elem_vao.destroy();
        elem_line_vao.destroy();
        elem_vbo.destroy();
        elem_line_vbo.destroy();
        elem_dirty = true;
    }
    if (line_vbo.isCreated())
    {
        line_vao.destroy();
        line_vbo.destroy();
    }
    if (streak_vbo.isCreated())
    {
        streak_vao.destroy();
        streak_vbo.destroy();
    }
    line_verts = 0;
    lines_dirty = true;
}

libfive::Tree::Id Shape::getUniqueId(
    std::unordered_map<libfive::TreeDataKey, libfive::Tree>& canonical)
{
    // (a sweep or a prism whose field is not made is told from the others by its points and by the tree it was built as -- see identityKey --, not by a tree it
    // would have to make and co-optimise: seconds for a sweep)
    if (!m_field_ready.load() && (hasSweep() || hasPrism() || hasMorph())) return nullptr;
    // (a point is drawn as a dot from its numbers, its field is not read: its own tree, made one with the other trees of the run, tells it from the others.  H90, measured in a window with an import of
    // 1106 points: every run made the optimized tree and the eight evaluators of every point, twice (the two setShapes of a run), 4 to 12 s each time on the thread of the window)
    if (isPoint()) return built_tree.cooptimize(canonical).id();
    ensureField();
    return base_tree.cooptimize(canonical).id();
}

QString Shape::identityKey(std::unordered_map<libfive::TreeDataKey, libfive::Tree>& canonical)
{
    QString key = colorKey();
    if (opacity < 0.999f) key += QString("|alpha%1").arg(int(opacity * 1000));        // (the same geometry drawn see-through -- alpha= -- is another shape)
    if (!m_prism_points.empty())
    {
        const auto pid = [&](const libfive::Tree& t) { return t.is_valid() ? quintptr(t.cooptimize(canonical).id()) : quintptr(0); };
        key += QString("|prism %1|f%2").arg(m_prism_points.size()).arg(quintptr(built_tree.id()));
        for (const ControlPoint& c : m_prism_points)
        {
            for (int a = 0; a < 3; ++a) key += QString(",%1").arg(pid(c.pos[a]));
            if (c.hasRadius) key += QString(",R%1").arg(pid(c.radius));            // (a round corner: its radius is a number of the shape)
        }
        key += QString(";%1,%2").arg(pid(m_prism_a)).arg(pid(m_prism_b));
    }
    if (hasMorph())
    {
        // (a body of points is told from the others by the tree it was built as and by how many vertices it has: its vertices are made from the same numbers, and a run makes new trees)
        key += QString("|morph %1 %2|f%3").arg(m_morph_controls.size()).arg(m_morph_triangles.size()).arg(quintptr(built_tree.id()));
    }
    if (hasSweep())
    {
        const auto sid = [&](const libfive::Tree& t) { return t.is_valid() ? quintptr(t.cooptimize(canonical).id()) : quintptr(0); };
        key += QString("|sweep %1 %2 %3%4|f%5").arg(m_sweep_profile.size()).arg(m_sweep_path.size()).arg(m_sweep_closed ? 1 : 0).arg(m_sweep_free ? "F" : "").arg(quintptr(built_tree.id()));
        for (const auto* list : {&m_sweep_profile, &m_sweep_path})
        {
            for (const ControlPoint& c : *list)
            {
                key += QString(";%1").arg(c.flag);
                for (int a = 0; a < 3; ++a) key += QString(",%1").arg(sid(c.pos[a]));
                if (c.hasRot)
                    for (int a = 0; a < 3; ++a) key += QString(",r%1").arg(sid(c.rot[a]));
                if (c.hasScale) key += QString(",s%1").arg(sid(c.scale));
                if (c.hasInfluence) key += QString(",i%1").arg(sid(c.influence));
                if (c.hasRadius) key += QString(",R%1").arg(sid(c.radius));
            }
        }
        key += QString(";t%1").arg(sid(m_sweep_twist));
        for (int a = 0; a < 3; ++a) key += QString(";g%1,%2,%3").arg(sid(m_sweep_move[a])).arg(sid(m_sweep_rotate[a])).arg(sid(m_sweep_scale[a]));
    }
    if (m_controls.empty()) return key;
    key += QString("|controls %1 %2%3%4").arg(m_controls.size()).arg(m_controls_default ? 1 : 0).arg(m_controls_closed ? 1 : 0).arg(m_controls_face ? 1 : 0);
    // (the same canonical map as the tree's: expressions that are the same come out as the same node, the script's numbers -- its var()s -- as themselves)
    const auto id = [&](const libfive::Tree& t) { return t.is_valid() ? quintptr(t.cooptimize(canonical).id()) : quintptr(0); };
    for (const ControlPoint& c : m_controls)
    {
        key += QString(";%1").arg(c.flag);
        for (int a = 0; a < 3; ++a) key += QString(",%1").arg(id(c.pos[a]));
        if (c.hasRot)
            for (int a = 0; a < 3; ++a) key += QString(",r%1").arg(id(c.rot[a]));
        if (c.hasScale) key += QString(",s%1").arg(id(c.scale));
        if (c.hasInfluence) key += QString(",i%1").arg(id(c.influence));
        if (c.hasRadius) key += QString(",R%1").arg(id(c.radius));
    }
    return key;
}

namespace {

/*  What a mesher thread does while it waits for work (libfive's FreeThreadHandler).  Left alone, a thread with nothing to do
 *  loops at full speed, and a render whose work is mostly in one thread (the end of the octree, the walk of a deep cell) keeps
 *  all the cores busy doing nothing -- so a small shape that was changed meanwhile, or a field slice, got a fraction of the machine
 *  and took a hundred times as long as it would alone.  The thread gives its turn away at once, and after a longer wait sleeps a moment
 *  (a thread that finds work again starts over, see the gap below), so a long render keeps going at its own speed and the others
 *  go first  */
class RenderIdle : public libfive::FreeThreadHandler
{
public:
    void offerWait() override
    {
        // (the calls of one thread that follow one another closely are one wait: a longer gap means it found work in between)
        thread_local std::chrono::steady_clock::time_point last;
        thread_local unsigned waiting = 0;
        const auto now = std::chrono::steady_clock::now();
        if (now - last > std::chrono::microseconds(200)) waiting = 0;
        if (++waiting < 2000)
            std::this_thread::yield();
        else
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        last = std::chrono::steady_clock::now();
    }
};

RenderIdle g_renderIdle;

}   // namespace

////////////////////////////////////////////////////////////////////////////////
// The mesh of a prism: the face of the outline the points make, at the distances a and b along the normal of its plane, and the walls between -- or null when the outline
// cannot be cut into triangles (it crosses itself: the field's to mesh)
std::unique_ptr<libfive::Mesh> Shape::prismMesh(const std::vector<libfive::pointshapes::Control>& cs, bool smoothDefault, double a, double b, int steps)
{
    std::vector<Eigen::Vector3d> where;
    for (const auto& c : cs) where.push_back(c.pos);
    double frame[9];
    if (!libfive::pointshapes::planeOf(where, frame)) return nullptr;
    const auto verts = libfive::pointshapes::polyline(cs, smoothDefault, true, steps);
    if (verts.size() < 3) return nullptr;
    const Eigen::Vector3d o(frame[0], frame[1], frame[2]), u(frame[3], frame[4], frame[5]), w(frame[6], frame[7], frame[8]);
    const Eigen::Vector3d normal = u.cross(w).normalized();
    std::vector<Eigen::Vector3d> loop;
    for (const auto& p : verts) loop.push_back(o + u * (p - o).dot(u) + w * (p - o).dot(w));
    auto cap = libfive::pointshapes::face(loop, frame);
    if (!cap) return nullptr;
    const size_t n = loop.size();
    double area = 0;
    for (size_t i = 0; i < n; ++i)
    {
        const double x0 = (loop[i] - o).dot(u), y0 = (loop[i] - o).dot(w), x1 = (loop[(i + 1) % n] - o).dot(u), y1 = (loop[(i + 1) % n] - o).dot(w);
        area += x0 * y1 - x1 * y0;
    }
    const double lo = std::min(a, b), hi = std::max(a, b);
    if (hi - lo < 1e-9) return nullptr;             // (no thickness: nothing to draw as a solid -- the field says what it is)
    auto out = std::make_unique<libfive::Mesh>();
    const uint32_t off = uint32_t(out->verts.size());                       // (the first vertex of a libfive mesh is nobody's: the layers start after it)
    const uint32_t capFirst = uint32_t(cap->verts.size()) - uint32_t(n);      // ...and so does the face's loop, in the triangles of the face
    for (int layer = 0; layer < 2; ++layer)
    {
        const Eigen::Vector3d shift = normal * (layer == 0 ? lo : hi);
        for (const auto& p : loop)
        {
            const Eigen::Vector3d q = p + shift;
            out->verts.push_back(Eigen::Vector3f(float(q.x()), float(q.y()), float(q.z())));
        }
    }
    using Tri = Eigen::Matrix<uint32_t, 3, 1>;
    for (const Tri& t : cap->branes)
    {
        out->branes.push_back(Tri(off + uint32_t(n) + t[0] - capFirst, off + uint32_t(n) + t[1] - capFirst, off + uint32_t(n) + t[2] - capFirst));         // (the top: facing the way the normal does)
        out->branes.push_back(Tri(off + t[0] - capFirst, off + t[2] - capFirst, off + t[1] - capFirst));                                                  // (the bottom: facing away)
    }
    for (size_t i = 0; i < n; ++i)
    {
        const uint32_t j = off + uint32_t((i + 1) % n), k = off + uint32_t(i), up = uint32_t(n);
        // (a wall: two triangles, facing out of the outline -- the other way round for an outline that runs clockwise)
        if (area >= 0)
        {
            out->branes.push_back(Tri(k, j, up + j));
            out->branes.push_back(Tri(k, up + j, up + k));
        }
        else
        {
            out->branes.push_back(Tri(k, up + j, j));
            out->branes.push_back(Tri(k, up + k, up + j));
        }
    }
    return out;
}

// This function is called in a separate thread:
Shape::BoundedMesh Shape::renderMesh(RenderSettings s)
{
    // Use the global bounds settings
    libfive::Region<3> r(
            {s.settings.min.x(), s.settings.min.y(), s.settings.min.z()},
            {s.settings.max.x(), s.settings.max.y(), s.settings.max.z()});

    // (a level no finer than the last one starts a new render)
    if (s.div >= seq_div)
    {
        seq_first_div.store(s.div);
        colour_share.store(0.0);
        share_build.store(1.0 / 3);
        share_walk.store(1.0 / 3);
    }
    seq_div = s.div;
    const auto tLevel = std::chrono::steady_clock::now();
    struct LevelClock
    {
        LevelClock(std::atomic<double>& out, std::chrono::steady_clock::time_point from) : m_out(out), m_from(from) {}
        ~LevelClock() { m_out.store(1000.0 * std::chrono::duration<double>(std::chrono::steady_clock::now() - m_from).count()); }
        std::atomic<double>& m_out;
        std::chrono::steady_clock::time_point m_from;
    } levelClock(m_levelMs, tLevel);

    mesh_settings.reset();
    render_stage.store(0);
    colour_progress.store(0.0);
    render_progress.store(0.0);
    RenderProgress handler(render_progress, s.div);
    mesh_settings.progress_handler = &handler;
    mesh_settings.free_thread_handler = &g_renderIdle;
    mesh_settings.low_priority = run_heavy;
    // A part with its own resolution is meshed on its own (setRenderHint):
    // over a cube around it, at the resolution it needs
    // (the resolution was worked out when the render started, in the GUI's thread: the box it is of is made there, and is not read from here)
    double res = s.res > 0 ? s.res : autoResolution(s.settings);
    libfive::Region<3> mesh_region = r;
    bool own_render = false;
    {
        QVector3D own_lo, own_hi;
        double own_res = 0;
        if (ownRegion(s.settings, run_hint_lo, run_hint_hi, run_hint_side, own_lo, own_hi, own_res))
        {
            mesh_region = libfive::Region<3>({own_lo.x(), own_lo.y(), own_lo.z()}, {own_hi.x(), own_hi.y(), own_hi.z()});
            res = own_res;
            own_render = true;
        }
    }
    mesh_settings.min_feature = 1 / (res / (1 << s.div));
    // A surface is a thin shell around its sheet: at a coarse level its cells are bigger than what it bends by and the sheet comes out ragged, which a handle drag (that meshes it again from the coarsest
    // level at every step) shows over and over (the user, 2026-11-04: "seemingly SPAZZES OUT EVEN MORE WIHT HANDLE DRAGS").  A surface is never coarser than one level above its final cell
    if (m_unsigned) mesh_settings.min_feature = 1 / (res / (1 << std::min(s.div, 1)));
    mesh_settings.max_err = pow(10, -s.settings.quality);
    mesh_settings.alg = s.alg;

    static const double identity[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    std::vector<std::vector<Eigen::Vector3f>> flat_outlines;
    std::vector<OutlineCurve> flat_curves;
    std::unique_ptr<libfive::Mesh> m;
    // A prism -- a straight extrusion of a profile of points -- is made from the numbers its points have now, and its a and b: no field is meshed, so it follows a point as it is
    // dragged.  (When it cannot be -- a handle pulled, a gizmo placing it, an outline that crosses itself -- its field is meshed as ever)
    if (hasPrism())
    {
        std::vector<libfive::pointshapes::Control> cs;
        double pa = 0, pb = 0;
        const bool ready = prismNow(run_flat_vars, cs, pa, pb);
        if (ready) m = prismMesh(cs, m_prism_default, pa, pb, curveSteps(double(s.settings.res)));
        if (std::getenv("FIELDES_TIMING"))
            fprintf(stderr, "[prism] line %d: %s\n", source_line + 1,
                    m ? "drawn from the numbers of its points" : ready ? "its outline cannot be cut into triangles: its field is meshed" : "its guard or its numbers do not hold: its field is meshed");
    }
    // A body of points -- morph_polyhedron, morph_body -- is drawn from the numbers its points have now: its vertices are evaluated and its triangles drawn, no field is meshed, so a point that is dragged is
    // followed at once (the user, 2026-10-29: "the standard is instant real time editing like the path extrude and the profile editing")
    if (!m && hasMorph())
    {
        std::vector<Eigen::Vector3f> positions;
        const bool ready = morphNow(run_flat_vars, positions);
        if (ready) m = morphMesh(positions, m_morph_triangles);
        if (std::getenv("FIELDES_TIMING"))
            fprintf(stderr, "[morph] line %d: %s (%.1f ms into the level)\n", source_line + 1,
                    m ? "drawn from the numbers of its points" : ready ? "its triangles cannot be drawn: its field is meshed" : "its numbers cannot be read: its field is meshed",
                    1000.0 * std::chrono::duration<double>(std::chrono::steady_clock::now() - tLevel).count());
    }
    if (!m && hasSweep())
    {
        SweepNumbers sn;
        const bool ready = sweepNow(run_flat_vars, sn);
        if (ready)
        {
            sn.steps = curveSteps(double(s.settings.res)); sn.plain = true;          /* (H52c, 2026-11-03, put back at 21:40: the viewport draws the PLAIN sweep, which is what he judged better than the cleaned one -- the cleaning makes long teeth at a tight bend of a thick tube; the field, nativeMeshArrays, keeps the cleaned mesh) */          // (the resolution of the scene says how finely it is cut, as it does for what is meshed)
            m = sweepMesh(sn);
        }
        if (std::getenv("FIELDES_TIMING"))
            fprintf(stderr, "[sweep] line %d: %s (%.0f ms into the level)\n", source_line + 1,
                    m ? "drawn from the numbers of its points" : ready ? "its profile cannot be cut into triangles: its field is meshed" : "its numbers cannot be read: its field is meshed",
                    1000.0 * std::chrono::duration<double>(std::chrono::steady_clock::now() - tLevel).count());
    }
    // The render cache: the finished mesh of this very shape may be on disk (a coarser level is not
    // kept: a render is read from it in one step, or meshed level by level).  Not asked of a mesh that is made from the numbers of
    // points: its key is a hash of the whole field, which costs seconds for a sweep, and the mesh is made faster than a file is read
    const auto renderStart = std::chrono::steady_clock::now();
    std::string cacheKey;
    // (a result whose fields are expressions -- the check and the optimization of the manufacturing tools: no solver, no elements, no displacements -- is kept like any shape: its mesh with the values of
    // every field at its vertices.  The result of a solver has a cache of its own)
    const bool keepableResult = has_result && !run_has_deform && run_color_detail == 0 && !m_grid.isMesh && m_grid.fraction.empty();
    if (!m && m_cache_on.load())
    {
        if (has_result && !keepableResult)
        {
            cache_state.store(CACHE_RESULT);
        }
        else
        {
            cacheKey = renderCacheKey(s, mesh_region, res);
            if (cacheKey.empty())
            {
                cache_state.store(CACHE_NO_KEY);
            }
            else if (cacheKey != cache_miss_key)
            {
                BoundedMesh hit;
                const auto tRead = std::chrono::steady_clock::now();
                if (readRenderCache(cacheKey, hit))
                {
                    if (std::getenv("FIELDES_TIMING"))
                        fprintf(stderr, "[render cache] read the mesh of line %d: %zu vertices, %.0f ms\n",
                                source_line + 1, hit.mesh->verts.size(),
                                1000.0 * std::chrono::duration<double>(std::chrono::steady_clock::now() - tRead).count());
                    mesh_settings.progress_handler = nullptr;
                    cache_state.store(CACHE_READ);
                    cache_read.store(true);
                    return hit;
                }
                cache_miss_key = cacheKey;
            }
        }
    }

    if (m)
    {
        // (made: the field of the shape is not read)
    }
    else if (hasControls())
    {
        // A line or a face of points: made from the numbers the points have now (no script is run, no field is read), the way a point being dragged needs it
        m = std::make_unique<libfive::Mesh>();
        std::vector<libfive::pointshapes::Control> cs;
        if (controlNumbers(run_flat_vars, cs) && !cs.empty())
        {
            double frame[9];
            std::vector<Eigen::Vector3d> where;
            for (const auto& c : cs) where.push_back(c.pos);
            // (the plane is the plane of the points that are in the script: a point being pulled out of the outline is put onto it)
            const bool planar = m_controls_face && libfive::pointshapes::planeOf(where, frame);
            if (m_run_virtual_on)
            {
                libfive::pointshapes::Control v;
                v.pos = Eigen::Vector3d(m_run_virtual_pos.x(), m_run_virtual_pos.y(), m_run_virtual_pos.z());
                cs.insert(cs.begin() + std::min<size_t>(cs.size(), size_t(std::max(0, m_run_virtual_after)) + 1), v);
            }
            std::vector<std::pair<int, double>> owners;
            const auto verts = libfive::pointshapes::polyline(cs, m_controls_default, m_controls_closed, 14, &owners);
            std::vector<Eigen::Vector3f> line;
            bool faced = false;
            if (planar && verts.size() >= 3)
            {
                const Eigen::Vector3d o(frame[0], frame[1], frame[2]), u(frame[3], frame[4], frame[5]), w(frame[6], frame[7], frame[8]);
                std::vector<Eigen::Vector3d> loop;
                for (const auto& p : verts) loop.push_back(o + u * (p - o).dot(u) + w * (p - o).dot(w));
                if (auto f = libfive::pointshapes::face(loop, frame, &flat_outlines))
                {
                    m = std::move(f);
                    faced = true;
                }
            }
            if (!faced)
            {
                for (const auto& p : verts) line.push_back(Eigen::Vector3f(float(p.x()), float(p.y()), float(p.z())));
                if (m_controls_closed && !line.empty()) line.push_back(line.front());
                flat_outlines.clear();
                if (line.size() >= 2) flat_outlines.push_back(std::move(line));
            }
            // The arcs of the round corners and the splines among the pieces of that outline: the viewport draws them from the curves themselves, to the pixel at any zoom (the pieces stay what a press is
            // looked for on, and what the face is cut into).  The pieces of an arc are the ones whose two ends are on it: vertices it owns, or that are its start or its end (where
            // it meets the arc before it, the vertex is the other's)
            const int N = int(verts.size());
            const bool closes = !flat_outlines.empty() && int(flat_outlines[0].size()) == N + 1;
            if (N >= 2 && !flat_outlines.empty() && (closes || int(flat_outlines[0].size()) == N))
            {
                const int pieces = N + (closes ? 1 : 0) - 1;
                for (const auto& f : libfive::pointshapes::fillets(cs, m_controls_default, m_controls_closed))
                {
                    const double tol = 1e-9 * (1.0 + f.a.norm());
                    auto on = [&](int k) {
                        const size_t v = size_t(k % N);
                        return (owners[v].first == f.control && owners[v].second == 0.0) || (verts[v] - f.a).norm() <= tol || (verts[v] - f.b).norm() <= tol;
                    };
                    int first = -1, last = -1;
                    for (int k = 0; k < pieces; ++k)
                    {
                        if (on(k) && on(k + 1))
                        {
                            if (first < 0) first = k;
                            last = k + 1;
                        }
                        else if (first >= 0) break;
                    }
                    const Eigen::Vector3d e1 = f.a - f.centre, eb = f.b - f.centre;
                    const double r = e1.norm();
                    if (first < 0 || !(r > 0)) continue;
                    const Eigen::Vector3d across = eb - e1 * (eb.dot(e1) / e1.squaredNorm());
                    const double sideways = across.norm();
                    if (!(sideways > 1e-300)) continue;
                    OutlineCurve arc;
                    arc.first = first;
                    arc.last = last;
                    arc.centre = f.centre;
                    arc.e1 = e1;
                    arc.e2 = across * (r / sideways);
                    arc.sweep = std::atan2(sideways, eb.dot(e1) / r);
                    flat_curves.push_back(arc);
                }
                // (and the splines: the cubic after a control is the run of vertices it owns at 0 < along < 1, between the vertex before them and the one after)
                for (const auto& cubic : libfive::pointshapes::curves(cs, m_controls_default, m_controls_closed))
                {
                    int s = -1, e = -1;
                    for (int v = 0; v < N; ++v)
                    {
                        if (owners[size_t(v)].first != cubic.control || !(owners[size_t(v)].second > 0.0)) continue;
                        if (s < 0) s = v;
                        e = v;
                    }
                    if (s < 1 || e + 1 > pieces) continue;
                    OutlineCurve spline;
                    spline.arc = false;
                    spline.first = s - 1;
                    spline.last = e + 1;
                    for (int k = 0; k < 4; ++k) spline.p[k] = cubic.p[k];
                    flat_curves.push_back(spline);
                }
            }
        }
    }
    else
    {
        ensureField();                     // (made here when nothing made it yet: a sweep that could not be drawn from its numbers)
        m = run_step_exact ? exactMeshOf(*run_step_exact, identity, r)
          : exactCurrent() ? exactMeshIn(r)
          : m_flat ? libfive::Mesh::renderFlat(tree, run_flat_vars, mesh_region, run_has_frame ? run_frame : nullptr, mesh_settings,
                                               &flat_outlines)
          : m_unsigned ? unsignedMesh(mesh_region)
          : renderInOwnFrame(mesh_region);
    }
    // A flat face is a few big triangles: a colour read at the vertices only would run across them.  Where it is coloured by a field, the
    // triangles are cut (at the middles of their edges) until they are about as small as the cells of the render
    if (m_flat && has_color && m && !m->branes.empty())
    {
        const float longest = float(8 * mesh_settings.min_feature);
        const float longest2 = longest * longest;
        using Tri = Eigen::Matrix<uint32_t, 3, 1>;
        auto& verts = m->verts;
        for (int level = 0; level < 12 && m->branes.size() < 2000000; level++)
        {
            std::map<std::pair<uint32_t, uint32_t>, uint32_t> middles;
            auto middle = [&](uint32_t a, uint32_t b) {
                const auto key = a < b ? std::make_pair(a, b) : std::make_pair(b, a);
                const auto it = middles.find(key);
                if (it != middles.end()) return it->second;
                const uint32_t id = uint32_t(verts.size());
                verts.push_back(0.5f * (verts[a] + verts[b]));
                middles.emplace(key, id);
                return id;
            };
            std::vector<Tri, Eigen::aligned_allocator<Tri>> cut;
            cut.reserve(m->branes.size() * 2);
            bool any = false;
            for (const Tri t : m->branes)
            {
                const float l = std::max({(verts[t[0]] - verts[t[1]]).squaredNorm(), (verts[t[1]] - verts[t[2]]).squaredNorm(),
                                          (verts[t[2]] - verts[t[0]]).squaredNorm()});
                if (l <= longest2)
                {
                    cut.push_back(t);
                    continue;
                }
                any = true;
                const uint32_t m01 = middle(t[0], t[1]), m12 = middle(t[1], t[2]), m20 = middle(t[2], t[0]);
                cut.push_back(Tri(t[0], m01, m20));
                cut.push_back(Tri(m01, t[1], m12));
                cut.push_back(Tri(m20, m12, t[2]));
                cut.push_back(Tri(m01, m12, m20));
            }
            if (!any) break;
            m->branes = std::move(cut);
        }
    }
    if (std::getenv("FIELDES_TIMING") && s.div == 0 && m && (own_render || m->verts.empty()))
    {   // (a shape meshed on its own, or one that came out empty)
        fprintf(stderr, "[fieldes] shape of line %d: %s, %zu vertices\n", source_line + 1,
                m->verts.empty() ? "EMPTY" : "own render", m->verts.size());
        if (own_render && m->verts.empty())
            fprintf(stderr, "[fieldes]   (own resolution %g/mm over a %g mm cube)\n", res, double(run_hint_side));
    }
    mesh_settings.progress_handler = nullptr;   // (the handler ends with this function)
    const auto tMesher = std::chrono::steady_clock::now();
    {   // the phases' shares of this level's meshing, for the next level
        const double T = handler.elapsed();
        if (T > 0 && handler.t13 >= 0 && handler.t23 >= handler.t13)
        {
            share_build.store(std::min(1.0, handler.t13 / T));
            share_walk.store(std::min(1.0, (handler.t23 - handler.t13) / T));
        }
    }
    render_stage.store(1);
    // What follows the meshing (colours, result fields): its
    // share of this level's time, for the next level
    struct AfterShare
    {
        std::atomic<double>& share;
        std::chrono::steady_clock::time_point t0, t1;
        ~AfterShare()
        {
            const auto t2 = std::chrono::steady_clock::now();
            const double all = std::chrono::duration<double>(t2 - t0).count();
            if (all > 0) share.store(std::chrono::duration<double>(t2 - t1).count() / all);
        }
    } afterShare{colour_share, tLevel, tMesher};

    // (a result's colours: as fine as the solver's elements, see refineForColour)
    if (has_result && run_color_detail > 0 && m && !m->branes.empty()) refineForColour(*m, run_color_detail);
    BoundedMesh out;
    out.mesh = m.release();
    out.region = r;
    out.outlines = std::move(flat_outlines);
    out.curves = std::move(flat_curves);

    // A result: every field and the displacements at every vertex
    if (has_result && out.mesh)
    {
        const size_t nt = run_channels.size() + (run_has_deform ? 3 : 0);
        if (result_evals.size() != nt)
        {
            result_evals.clear();
            result_pool.clear();
            for (const auto& c : run_channels) result_evals.emplace_back(new libfive::ArrayEvaluator(c.tree));
            if (run_has_deform)
                for (int a = 0; a < 3; a++) result_evals.emplace_back(new libfive::ArrayEvaluator(run_deform[a]));
        }
        auto& verts = out.mesh->verts;
        out.channels.resize(run_channels.size());
        // (the CHANGE colour of a manufacturing result is a class, not an amount: its triangles are cut where it changes, see cutByColourClass.  It goes first: the cut adds vertices, and every channel is then
        // read at all of them.  So is the colour of a manufacturing CHECK -- 1 where a rule is broken, 0 where it is kept --, which is a class too: the check leaves out the places near an edge (the normal there
        // is a mix of two faces), so on a sharp box the overhang of a flat face is a few big triangles whose corners all lie on its edges, all of them read 0, and the face came out grey under a card that said
        // 12 % of the surface was broken.  Its triangles are looked at inside, `always`: all the corners reading the same is what the problem is)
        const bool violation = color_map == "violation";
        size_t classChannel = run_channels.size();
        if (color_map == "change" || violation)
            for (size_t c = 0; c < run_channels.size(); c++)
                if (run_channels[c].name == (violation ? "all" : "change")) { classChannel = c; break; }
        if (classChannel < run_channels.size())
        {
            auto& values = out.channels[classChannel];
            auto& e = *result_evals[classChannel];
            const size_t threads = std::max<size_t>(1, es.size());
            auto& pool = result_pool;
            values.resize(verts.size());
            evalRangeThreaded(e, run_channels[classChannel].tree, pool, verts, values, 0, verts.size(), threads);
            double extent = 0;
            for (int a = 0; a < 3; a++) extent = std::max(extent, r.upper[a] - r.lower[a]);
            const size_t vertsBefore = verts.size();
            const auto tCut = std::chrono::steady_clock::now();
            cutByColourClass(*out.mesh, values, float(8 * mesh_settings.min_feature), float(0.00025 * extent), [&](size_t first, size_t last) {
                evalRangeThreaded(e, run_channels[classChannel].tree, pool, verts, values, first, last, threads);
            }, violation);
            if (violation)
                fprintf(stderr, "[fieldes] colour of a check, the big faces looked at inside: %zu -> %zu vertices, %.0f ms\n", vertsBefore, verts.size(),
                        1000.0 * std::chrono::duration<double>(std::chrono::steady_clock::now() - tCut).count());
        }
        for (size_t c = 0; c < run_channels.size(); c++)
            if (c != classChannel) evalAll(*result_evals[c], verts, out.channels[c]);
        if (run_has_deform)
        {
            out.disp.assign(verts.size(), Eigen::Vector3f::Zero());
            std::vector<float> vals;
            for (int a = 0; a < 3; a++)
            {
                evalAll(*result_evals[run_channels.size() + size_t(a)], verts, vals);
                for (size_t q = 0; q < verts.size(); q++) out.disp[q][a] = vals[q];
            }
        }
        if (!cacheKey.empty() && out.mesh && s.div == 0 && m_cache_on.load())
        {
            // (kept like the mesh of any shape, below: it took a while, or it was asked for by name)
            const double sinceStart = std::chrono::duration<double>(std::chrono::steady_clock::now() - renderStart).count();
            if (m_cache_forced.load() || sinceStart >= 0.4)
            {
                const auto tWrite = std::chrono::steady_clock::now();
                const bool ok = writeRenderCache(cacheKey, out);
                cache_state.store(ok ? CACHE_KEPT : CACHE_FAILED);
                if (ok) cache_miss_key.clear();
                if (std::getenv("FIELDES_TIMING"))
                    fprintf(stderr, "[render cache] %s the mesh of line %d: %zu vertices, %zu fields, %.0f ms\n",
                            ok ? "kept" : "could not keep", source_line + 1, out.mesh->verts.size(), out.channels.size(),
                            1000.0 * std::chrono::duration<double>(std::chrono::steady_clock::now() - tWrite).count());
            }
        }
        return out;
    }

    // The colour field at every vertex, in batches, on as many threads as
    // the mesher uses (it can cost as much as meshing), counted for the bar
    if (has_color && out.mesh)
    {
        const auto tMeshed = std::chrono::steady_clock::now();
        auto& verts = out.mesh->verts;
        out.values.resize(verts.size());
        const size_t N = LIBFIVE_EVAL_ARRAY_SIZE;
        // The colour field at the vertices first..last, in batches, on as many threads as the mesher uses; returns the
        // number of threads
        auto evaluate = [&](size_t first, size_t last, bool report) -> size_t {
            const size_t batches = (last - first + N - 1) / N;
            const size_t T = std::max<size_t>(1, std::min<size_t>(es.size(), (batches + 3) / 4));
            color_evals.resize(std::max(color_evals.size(), T));
            std::atomic<size_t> next{0}, done{0};
            auto work = [&](size_t t) {
                // (with the script's numbers: a colour field may hold var()s -- a selection on a part placed by its placement arguments)
                if (!color_evals[t]) color_evals[t].reset(new libfive::ArrayEvaluator(color_field, vars));
                auto& e = *color_evals[t];
                for (size_t b; (b = next.fetch_add(1)) < batches; )
                {
                    const size_t i = first + b * N;
                    const size_t n = std::min(N, last - i);
                    for (size_t j = 0; j < n; j++) e.set(verts[i + j].template cast<float>(), j);
                    const auto vals = e.values(n);
                    for (size_t j = 0; j < n; j++) out.values[i + j] = vals[j];
                    if (report) colour_progress.store(double(done.fetch_add(1) + 1) / double(batches));
                }
            };
            std::vector<std::thread> threads;
            const bool low = run_heavy;
            for (size_t t = 1; t < T; t++)
                threads.emplace_back([&work, t, low] { if (low) libfive::lowerThreadPriority(); work(t); });
            work(0);
            for (auto& th : threads) th.join();
            return T;
        };
        const size_t T = evaluate(0, verts.size(), true);

        // A colour that is a CATEGORY (the places of boundary conditions, a selected surface) changes all at once at the
        // edge of its region, but it is only known at the vertices of the mesh, and a flat face is a few big triangles:
        // the colour would run across them from one category to the other.  A triangle whose corners are in different
        // categories is cut into four (at the middles of its edges), again and again, until it is small, so that the
        // edge of the region is as sharp as the mesh is fine.  The shape of the surface does not change
        //
        // A patch of a surface (has_cutoff: the value is a distance, at most zero on the patch) is found the same way, but
        // not by the categories of the corners: a patch in the middle of a big flat triangle -- the box's face inside a
        // sphere -- has corners that are all far from it, and no category tells.  The value changes by at most two per mm
        // (it is the difference of two distances), so a triangle whose nearest corner is further from the patch than
        // that over its longest edge cannot hold any of it, and is left; one with all its corners on the patch is drawn
        // as it is; every other triangle may hold the edge of the patch, and is cut
        // (the CHANGE colour of a manufacturing result is a result with a channel, and is cut in the result branch above, `cutByColourClass`)
        if (color_map == "bc" && verts.size() > 1)
        {
            double extent = 0;
            for (int a = 0; a < 3; a++) extent = std::max(extent, r.upper[a] - r.lower[a]);
            const double smallest = 0.002 * extent;
            const float smallest2 = float(smallest * smallest);
            using Tri = Eigen::Matrix<uint32_t, 3, 1>;
            auto category = [](float v) { return std::isfinite(v) ? int(std::lround(v)) : -1; };
            for (int level = 0; level < 9; level++)
            {
                const size_t before = verts.size();
                std::map<std::pair<uint32_t, uint32_t>, uint32_t> middles;
                auto middle = [&](uint32_t a, uint32_t b) {
                    const auto key = a < b ? std::make_pair(a, b) : std::make_pair(b, a);
                    const auto it = middles.find(key);
                    if (it != middles.end()) return it->second;
                    const Eigen::Vector3f p = 0.5f * (verts[a] + verts[b]);
                    const uint32_t id = uint32_t(verts.size());
                    verts.push_back(p);
                    middles.emplace(key, id);
                    return id;
                };
                std::vector<Tri, Eigen::aligned_allocator<Tri>> cut;
                cut.reserve(out.mesh->branes.size() + out.mesh->branes.size() / 8);
                bool any = false;
                for (const Tri t : out.mesh->branes)
                {
                    const int ca = category(out.values[t[0]]), cb = category(out.values[t[1]]), cc = category(out.values[t[2]]);
                    const float longest = std::max({(verts[t[0]] - verts[t[1]]).squaredNorm(), (verts[t[1]] - verts[t[2]]).squaredNorm(),
                                                    (verts[t[2]] - verts[t[0]]).squaredNorm()});
                    bool keep;
                    if (has_cutoff)
                    {
                        const float v0 = out.values[t[0]] - color_cutoff, v1 = out.values[t[1]] - color_cutoff,
                                    v2 = out.values[t[2]] - color_cutoff;
                        const float vmin = std::min({v0, v1, v2}), vmax = std::max({v0, v1, v2});
                        keep = !(std::isfinite(v0) && std::isfinite(v1) && std::isfinite(v2)) ||
                               vmax <= 0.0f || vmin > 2.0f * std::sqrt(longest);
                    }
                    else
                    {
                        keep = ca == cb && cb == cc;
                    }
                    if (keep || longest <= smallest2)
                    {
                        cut.push_back(t);
                        continue;
                    }
                    any = true;
                    const uint32_t m01 = middle(t[0], t[1]), m12 = middle(t[1], t[2]), m20 = middle(t[2], t[0]);
                    cut.push_back(Tri(t[0], m01, m20));
                    cut.push_back(Tri(m01, t[1], m12));
                    cut.push_back(Tri(m20, m12, t[2]));
                    cut.push_back(Tri(m01, m12, m20));
                }
                if (!any) break;
                out.values.resize(verts.size());
                evaluate(before, verts.size(), false);
                out.mesh->branes = std::move(cut);
            }
        }

        const auto tEnd = std::chrono::steady_clock::now();
        const double meshing = std::chrono::duration<double>(tMesher - tLevel).count();
        const double colouring = std::chrono::duration<double>(tEnd - tMeshed).count();
        if (std::getenv("FIELDES_TIMING"))
            fprintf(stderr, "[fieldes] level %d: meshing %.0f ms, colouring %zu vertices %.0f ms (%zu threads)\n",
                    s.div, 1000 * meshing, verts.size(), 1000 * colouring, T);
    }

    // The finished mesh goes to the render cache (a shape the script has not asked for by name only if it took a while
    // to mesh: a sphere is not worth a file)
    const double sinceStart = std::chrono::duration<double>(std::chrono::steady_clock::now() - renderStart).count();
    if (!cacheKey.empty() && out.mesh && s.div == 0 && m_cache_on.load() && (m_cache_forced.load() || sinceStart >= 0.4))
    {
        const auto tWrite = std::chrono::steady_clock::now();
        const bool ok = writeRenderCache(cacheKey, out);
        cache_state.store(ok ? CACHE_KEPT : CACHE_FAILED);
        if (ok) cache_miss_key.clear();
        if (std::getenv("FIELDES_TIMING"))
            fprintf(stderr, "[render cache] %s the mesh of line %d: %zu vertices, %.0f ms\n",
                    ok ? "kept" : "could not keep", source_line + 1, out.mesh->verts.size(),
                    1000.0 * std::chrono::duration<double>(std::chrono::steady_clock::now() - tWrite).count());
    }
    return out;
}

////////////////////////////////////////////////////////////////////////////////
// The render cache (see Shape::setRenderCache)

// The build the program is.  fieldesBuildStamp: the time of its executable, shown at the right of the status bar so that a picture of a window says which build it is from.
// fieldesBuildKey: the newest time of the executable and of the Python library beside it, part of the key of every mesh the render cache keeps in the user's folder, so that a mesh kept by
// another build is never read back as this one's (2026-11-03: a mesh kept at 13:36 was read by a later build at 18:57).  Both are read the first time they are asked, the window asks when it is
// made: a window that goes on while the next build replaces the file keeps the build it started as.
QString fieldesBuildStamp()
{
    static const QString stamp = QFileInfo(QCoreApplication::applicationFilePath()).lastModified().toString("yyyy-MM-dd HH:mm");
    return stamp;
}

std::string fieldesBuildKey()
{
    static const std::string key = [] {
        qint64 newest = QFileInfo(QCoreApplication::applicationFilePath()).lastModified().toMSecsSinceEpoch();
        QDirIterator it(QDir(QCoreApplication::applicationDirPath()).filePath("python/fieldes"), QStringList{"*.py"}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext())
        {
            it.next();
            newest = std::max(newest, it.fileInfo().lastModified().toMSecsSinceEpoch());
        }
        return QString::number(newest, 36).toStdString();
    }();
    return key;
}
// The script file that is open (set by the window): the meshes the render cache keeps while it is open are noted as its (owners.idx in the folder), and fieldesSweepRenderCache keeps a mesh only
// while its script is among the recently opened files and still exists, and while the mesh has been used in the last two weeks.
static std::mutex g_cacheOwnerMutex;
static QString g_cacheOwner;

void fieldesSetCacheOwner(const QString& path)
{
    std::lock_guard<std::mutex> lock(g_cacheOwnerMutex);
    g_cacheOwner = path.isEmpty() ? QString() : QFileInfo(path).absoluteFilePath();
}

namespace {

// Bump when what the mesher makes from the same shape changes (a mesh kept by an older one would
// be read in its place).  The meshes kept in the user's folder are tied to the build (fieldesBuildKey below: the executable and the Python library by their times), so a mesh kept by another build is not read back; the meshes that come with the program (seed-cache) are keyed by this version alone.
const char* const kRenderCacheVersion = "fdrc5";        // (5: the flank of a gusset or a teardrop is smoothed in the kernel's lattice: a mesh kept by fdrc4 has it in ribs.  4: the colour of a manufacturing check is read inside the big faces, cut as the change colour's are: a mesh kept by fdrc3 has the flat overhang of a sharp box grey under a card that says 12 %.  3: the change colour of a manufacturing result is a class read as the difference of two fields, and its triangles are cut where it changes: a mesh kept by fdrc1 or 2 has the pale red over untouched faces)

const quint32 kCacheMagic = 0x43524446u;       // "FDRC"
const quint32 kCacheFormat = 2;         // (2: after the colour values, the values of the fields of a result at the vertices -- a count, then for each its length and its values; 1: none, and still read)

QString cacheFile(const std::string& key)
{
    const QByteArray h = QCryptographicHash::hash(QByteArray::fromStdString(key + "|build=" + fieldesBuildKey()), QCryptographicHash::Sha1).toHex();
    return Shape::renderCacheDir() + "/" + QString::fromLatin1(h) + ".fdmesh";
}

// What comes with the program: `seed-cache/render-cache` beside the executable, the meshes of what the guided tour shows and takes long to mesh, read where it is when the user's own folder has no file for
// the key (the folder may be read-only; nothing is copied).  FIELDES_NO_SEED=1 has none; FIELDES_SEED_CACHE_DIR names the folder of the solved problems (`result-cache`), and the meshes are in `render-cache` beside it
QString seedCacheFile(const std::string& key)
{
    if (!qEnvironmentVariableIsEmpty("FIELDES_NO_SEED")) return QString();
    const QByteArray env = qgetenv("FIELDES_SEED_CACHE_DIR");
    const QString dir = !env.isEmpty() ? QFileInfo(QString::fromLocal8Bit(env)).absoluteDir().absoluteFilePath("render-cache")
                                       : QCoreApplication::applicationDirPath() + "/seed-cache/render-cache";
    const QByteArray h = QCryptographicHash::hash(QByteArray::fromStdString(key), QCryptographicHash::Sha1).toHex();
    const QString path = dir + "/" + QString::fromLatin1(h) + ".fdmesh";
    return QFileInfo::exists(path) ? path : QString();
}

qint64 cacheLimitBytes()
{
    bool ok = false;
    const qint64 mb = qEnvironmentVariableIntValue("FIELDES_RENDER_CACHE_MB", &ok);
    return (ok && mb > 0 ? mb : 2048) * 1024 * 1024;
}

// The oldest kept meshes go when the folder is over its limit
void pruneRenderCache()
{
    const qint64 limit = cacheLimitBytes();
    QFileInfoList files = QDir(Shape::renderCacheDir()).entryInfoList({"*.fdmesh"}, QDir::Files);
    qint64 total = 0;
    for (const auto& f : files) total += f.size();
    if (total <= limit) return;
    std::sort(files.begin(), files.end(), [](const QFileInfo& a, const QFileInfo& b) {
        return a.lastModified() < b.lastModified();
    });
    for (const auto& f : files)
    {
        if (total <= limit * 8 / 10) break;
        total -= f.size();
        QFile::remove(f.absoluteFilePath());
    }
}

}   // anonymous namespace

QString Shape::renderCacheDir()
{
    const QByteArray env = qgetenv("FIELDES_RENDER_CACHE_DIR");
    const QString dir = !env.isEmpty() ? QString::fromLocal8Bit(env)
                                       : QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/render-cache";
    QDir().mkpath(dir);

    // The meshes kept by another build go (the key of a file has the build in it, so they would only lie there and grow the folder with every build): the first time this process asks, a marker
    // file names the build the folder belongs to, and when it is another one the older files are deleted, on a thread of their own so that the window does not wait for the disk.  Only files
    // older than this start go: what this build writes is not touched.
    static std::atomic<bool> cleaned{false};
    if (!cleaned.exchange(true))
    {
        const QString marker = dir + "/build.key";
        QString kept;
        {
            QFile f(marker);
            if (f.open(QIODevice::ReadOnly)) kept = QString::fromUtf8(f.readAll()).trimmed();
        }
        const QString mine = QString::fromStdString(fieldesBuildKey());
        if (kept != mine)
        {
            const QDateTime started = QDateTime::currentDateTime().addSecs(-1);
            QFile f(marker);
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(mine.toUtf8());
            f.close();
            std::thread([dir, started] {
                for (const auto& old : QDir(dir).entryInfoList({"*.fdmesh", "*.fdmesh.*"}, QDir::Files))
                    if (old.lastModified() < started) QFile::remove(old.absoluteFilePath());
            }).detach();
        }
    }
    return dir;
}

int Shape::clearRenderCache()
{
    int n = 0;
    for (const auto& f : QDir(renderCacheDir()).entryInfoList({"*.fdmesh", "*.fdmesh.*"}, QDir::Files))
    {
        if (QFile::remove(f.absoluteFilePath())) ++n;
    }
    return n;
}

QString Shape::renderCacheKind() const
{
    if (!m_cache_on.load()) return QString();
    switch (cache_state.load())
    {
        case CACHE_KEPT:   return "kept";
        case CACHE_READ:   return "read";
        case CACHE_NO_KEY:
        case CACHE_RESULT:
        case CACHE_FAILED: return "no";
        default:           return "wait";
    }
}

QString Shape::renderCacheState() const
{
    if (!m_cache_on.load()) return QString();
    switch (cache_state.load())
    {
        case CACHE_KEPT:    return T("meshed, and kept in the render cache");
        case CACHE_READ:    return T("read from the render cache");
        case CACHE_NO_KEY:  return T("cannot be kept: it depends on something no other run can recognise "
                                   "(a variable it has no value for, or a solved analysis)");
        case CACHE_RESULT:  return T("a result shown with its fields is not kept");
        case CACHE_FAILED:  return T("the mesh could not be written to the cache folder");
        default:            return T("on: kept once it has been meshed");
    }
}

// What the shape is, apart from where and how finely it is meshed: its expression, with the numbers
// it is drawn with (the snapshot of the evaluators') and what colours it.  Empty when some of it cannot be told from one run to the next.
std::string Shape::renderCacheTreeKey() const
{
    using libfive::treePersistentKey;
    std::string out = treePersistentKey(built_tree, cache_vars);
    if (out.empty()) return out;
    if (has_color)
    {
        const std::string c = treePersistentKey(color_field, cache_vars);
        if (c.empty()) return c;
        out += "|colour|" + c;
        // (a patch of a surface: its mesh is cut finer along the edge of the patch than the one the first version kept, whose
        // triangles on a flat face were as big as the face)
        if (has_cutoff) out += "|patch2";
        if (has_floor) out += "|floor1";
    }
    return out;
}

std::string Shape::renderCacheKey(const RenderSettings& s, const libfive::Region<3>& region, double res)
{
    if (cache_tree_gen != cache_vars_gen)
    {
        cache_tree_key = renderCacheTreeKey();
        cache_tree_gen = cache_vars_gen;
    }
    if (cache_tree_key.empty() || m_flat || hasControls()) return std::string();         // (a face and a line are quick to make, and have outlines)
    // (the fields of a result are read at the vertices of its mesh, and the mesh holds them: what they are, and how the colour is cut, is part of what it holds)
    std::string resultKey;
    if (has_result)
    {
        for (const auto& c : m_channels)
        {
            const std::string k = libfive::treePersistentKey(c.tree, cache_vars);
            if (k.empty()) return std::string();
            resultKey += "|ch|" + c.name.toStdString() + "|" + k;
        }
        resultKey += "|map|" + color_map.toStdString();
    }
    // H189: a body whose box is known and lies inside the region it is meshed over is the same mesh whatever the room round it is -- the scene's region grows with the models that are shown --, so
    // its key says its box (the box of the render that starts, rounded out to 64ths of its length and a few cells: the search that finds it may differ in the last digits from one region to another),
    // not the region: the mesh that was kept when a model was shown is the one that is read when it is hidden again, and in another session whose region is another
    Eigen::Vector3d keyLo(region.lower.x(), region.lower.y(), region.lower.z()), keyHi(region.upper.x(), region.upper.y(), region.upper.z());
    bool boxKey = false;
    if (run_ext_known && res > 0)
    {
        const double cell = 1.0 / res;
        const Eigen::Vector3d blo(run_ext_lo.x() - 3 * cell, run_ext_lo.y() - 3 * cell, run_ext_lo.z() - 3 * cell);
        const Eigen::Vector3d bhi(run_ext_hi.x() + 3 * cell, run_ext_hi.y() + 3 * cell, run_ext_hi.z() + 3 * cell);
        bool inside = true;
        double longest = 0;
        for (int k = 0; k < 3; ++k)
        {
            inside = inside && blo[k] >= keyLo[k] && bhi[k] <= keyHi[k];
            longest = std::max(longest, bhi[k] - blo[k]);
        }
        if (inside && longest > 0)
        {
            const double step = std::max(longest / 64.0, cell);
            for (int k = 0; k < 3; ++k)
            {
                keyLo[k] = std::floor(blo[k] / step) * step;
                keyHi[k] = std::ceil(bhi[k] / step) * step;
            }
            boxKey = true;
        }
    }
    char buf[400];
    std::snprintf(buf, sizeof(buf), "|%a %a %a %a %a %a|%a|%a|%d",
                  keyLo.x(), keyLo.y(), keyLo.z(),
                  keyHi.x(), keyHi.y(), keyHi.z(),
                  res, double(s.settings.quality), int(s.alg));
    return std::string(kRenderCacheVersion) + "|" + cache_tree_key + resultKey + buf + (m_flat ? "|flat" : "") + (boxKey ? "|box" : "");
}

bool Shape::readRenderCache(const std::string& key, BoundedMesh& out) const
{
    QFile f(cacheFile(key));
    const bool userFile = f.exists();          // (a hit on the user's own folder sets the file time to now at the end: the time of a kept mesh is the time it was last used)
    if (!f.exists())
    {
        const QString seed = seedCacheFile(key);            // (the user's own folder first)
        if (seed.isEmpty()) return false;
        f.setFileName(seed);
    }
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QByteArray data = f.readAll();
    f.close();

    // header: magic, format, key length, vertex count, triangle count, colour count; then the box (6
    // doubles), the key, the vertices, the triangles and the colours
    const qint64 head = 6 * sizeof(quint32) + 6 * sizeof(double);
    if (data.size() < head) return false;
    const char* p = data.constData();
    quint32 h[6];
    std::memcpy(h, p, sizeof(h));
    double box[6];
    std::memcpy(box, p + sizeof(h), sizeof(box));
    if (h[0] != kCacheMagic || (h[1] != 1 && h[1] != kCacheFormat)) return false;
    const quint64 keyLen = h[2], nv = h[3], nt = h[4], nc = h[5];
    quint64 need = quint64(head) + keyLen + nv * 12 + nt * 12 + nc * 4;
    std::vector<quint64> fieldLen;                   // (format 2: the values of the fields of a result at the vertices)
    if (h[1] >= 2)
    {
        if (quint64(data.size()) < need + 4) return false;
        quint32 count = 0;
        std::memcpy(&count, data.constData() + need, 4);
        need += 4;
        for (quint32 i = 0; i < count; ++i)
        {
            if (quint64(data.size()) < need + 4) return false;
            quint32 n = 0;
            std::memcpy(&n, data.constData() + need, 4);
            fieldLen.push_back(n);
            need += 4 + quint64(n) * 4;
        }
    }
    if (quint64(data.size()) != need || keyLen != key.size()) return false;
    p += head;
    if (std::memcmp(p, key.data(), keyLen) != 0) return false;       // (the file name is a hash of the key)
    p += keyLen;

    auto mesh = std::make_unique<libfive::Mesh>();
    mesh->verts.resize(nv);
    for (quint64 i = 0; i < nv; ++i)
    {
        float v[3];
        std::memcpy(v, p + 12 * i, 12);
        mesh->verts[i] = Eigen::Vector3f(v[0], v[1], v[2]);
    }
    p += nv * 12;
    mesh->branes.resize(nt);
    for (quint64 i = 0; i < nt; ++i)
    {
        quint32 t[3];
        std::memcpy(t, p + 12 * i, 12);
        if (t[0] >= nv || t[1] >= nv || t[2] >= nv) return false;
        mesh->branes[i] = Eigen::Matrix<uint32_t, 3, 1>(t[0], t[1], t[2]);
    }
    p += nt * 12;
    out.values.resize(nc);
    if (nc) std::memcpy(out.values.data(), p, nc * 4);
    p += nc * 4;
    if (has_result)
    {
        // (a result: the values of its fields at the vertices -- as many as it has fields now, each at every vertex)
        if (fieldLen.size() != run_channels.size()) return false;
        out.channels.resize(fieldLen.size());
        p += 4;                                       // (the count)
        for (size_t i = 0; i < fieldLen.size(); ++i)
        {
            if (fieldLen[i] != nv) return false;
            p += 4;                                   // (its length)
            out.channels[i].resize(fieldLen[i]);
            if (fieldLen[i]) std::memcpy(out.channels[i].data(), p, fieldLen[i] * 4);
            p += fieldLen[i] * 4;
        }
    }
    else if (has_color && out.values.size() != nv) return false;
    out.mesh = mesh.release();
    out.region = libfive::Region<3>({box[0], box[1], box[2]}, {box[3], box[4], box[5]});
    if (userFile)
    {
        QFile used(cacheFile(key));
        if (used.open(QIODevice::ReadWrite))
        {
            used.setFileTime(QDateTime::currentDateTime(), QFileDevice::FileModificationTime);
            used.close();
        }
    }
    return true;
}

bool Shape::writeRenderCache(const std::string& key, const BoundedMesh& out) const
{
    if (!out.mesh) return false;
    QSaveFile f(cacheFile(key));
    if (!f.open(QIODevice::WriteOnly)) return false;
    const auto& verts = out.mesh->verts;
    const auto& tris = out.mesh->branes;
    const quint32 head[6] = {kCacheMagic, kCacheFormat, quint32(key.size()), quint32(verts.size()),
                             quint32(tris.size()), quint32(out.values.size())};
    const double box[6] = {out.region.lower.x(), out.region.lower.y(), out.region.lower.z(),
                           out.region.upper.x(), out.region.upper.y(), out.region.upper.z()};
    f.write(reinterpret_cast<const char*>(head), sizeof(head));
    f.write(reinterpret_cast<const char*>(box), sizeof(box));
    f.write(key.data(), qint64(key.size()));
    QByteArray buf;
    buf.resize(int(verts.size() * 12));
    for (size_t i = 0; i < verts.size(); ++i)
    {
        const float v[3] = {verts[i].x(), verts[i].y(), verts[i].z()};
        std::memcpy(buf.data() + 12 * i, v, 12);
    }
    f.write(buf);
    buf.resize(int(tris.size() * 12));
    for (size_t i = 0; i < tris.size(); ++i)
    {
        const quint32 t[3] = {tris[i][0], tris[i][1], tris[i][2]};
        std::memcpy(buf.data() + 12 * i, t, 12);
    }
    f.write(buf);
    if (!out.values.empty())
        f.write(reinterpret_cast<const char*>(out.values.data()), qint64(out.values.size() * 4));
    // (the values of the fields of a result at the vertices: a count, then for each its length and its values)
    const quint32 fields = quint32(out.channels.size());
    f.write(reinterpret_cast<const char*>(&fields), 4);
    for (const auto& c : out.channels)
    {
        const quint32 n = quint32(c.size());
        f.write(reinterpret_cast<const char*>(&n), 4);
        if (n) f.write(reinterpret_cast<const char*>(c.data()), qint64(n) * 4);
    }
    if (!f.commit()) return false;
    {   // (which script made it: the sweep of the folder keeps a mesh only while its script is among the recently opened files)
        QString owner;
        {
            std::lock_guard<std::mutex> lock(g_cacheOwnerMutex);
            owner = g_cacheOwner;
        }
        if (!owner.isEmpty())
        {
            QFile notes(Shape::renderCacheDir() + "/owners.idx");
            if (notes.open(QIODevice::Append | QIODevice::Text)) notes.write((QFileInfo(cacheFile(key)).fileName() + "\t" + owner + "\n").toUtf8());
        }
    }
    pruneRenderCache();
    return true;
}

// The sweep of the render cache folder (the window calls it a little after the start and every six hours, on a thread of its own, with the list of the recently opened files): a mesh goes when it has not
// been used for 14 days (the file time is the time of its last use), when the script it was made for is not among the recently opened files any more or is gone, and a mesh with no script noted (made before
// the notes, or by a script that has no file) goes after a day.  A mesh of a script that is still listed stays after the script is edited: a part that did not change still finds its mesh.
void fieldesSweepRenderCache(const QStringList& recent)
{
    const QString dir = Shape::renderCacheDir();
    std::map<QString, QString> owner;
    {
        QFile notes(dir + "/owners.idx");
        if (notes.open(QIODevice::ReadOnly | QIODevice::Text))
            for (const QByteArray& line : notes.readAll().split('\n'))
            {
                const int tab = line.indexOf('\t');
                if (tab > 0) owner[QString::fromUtf8(line.left(tab))] = QString::fromUtf8(line.mid(tab + 1)).trimmed();
            }
    }
    std::set<QString> listed;
    for (const QString& r : recent) listed.insert(QFileInfo(r).absoluteFilePath());
    const QDateTime now = QDateTime::currentDateTime();
    std::map<QString, QString> kept;
    for (const QFileInfo& f : QDir(dir).entryInfoList({"*.fdmesh"}, QDir::Files))
    {
        const qint64 idleDays = f.lastModified().daysTo(now);
        const auto it = owner.find(f.fileName());
        bool drop;
        if (idleDays >= 14) drop = true;
        else if (it == owner.end()) drop = idleDays >= 1;
        else drop = !listed.count(it->second) || !QFileInfo::exists(it->second);
        if (drop) QFile::remove(f.absoluteFilePath());
        else if (it != owner.end()) kept[it->first] = it->second;
    }
    QSaveFile out(dir + "/owners.idx");
    if (out.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        for (const auto& e : kept) out.write((e.first + "\t" + e.second + "\n").toUtf8());
        out.commit();
    }
}
bool Shape::keepCurrent()
{
    if (running || !mesh || mesh_div != 0 || has_result || next.div != MESH_DIV_EMPTY) return false;
    // (the mesh on screen was not meshed with the cache on: mesh it again at its level, which keeps it
    // -- or reads what an earlier run kept)
    startRender(RenderSettings{next.settings, 0, next.alg});
    return true;
}

}   // namespace FielDes
