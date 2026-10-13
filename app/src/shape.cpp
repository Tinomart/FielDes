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
#include <algorithm>
#include <chrono>
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

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
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
        fprintf(stderr, "[shape] eight evaluators in %.0f ms\n", 1000.0 * std::chrono::duration<double>(std::chrono::steady_clock::now() - tEval).count());
    m_deps_known = false;
    m_field_ready.store(true);
}

void Shape::settleField()
{
    if (!drawnFromNumbers()) ensureField();
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
    has_hint = res > 0 && side > 0 && scene_res != 0;          // (a scene resolution below 0: the resolution is absolute: custom_resolution)
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
    if (m_locked || !m_gizmo.present || m_gizmo.hidden || m_gizmo.mode == Gizmo::NEVER) return false;
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
    return false;
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
        return true;
    }
    return false;
}

bool Shape::hasMoveVars() const
{
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
    bool profileDefault = false, pathDefault = false, closed = false, placed = false, free = false;
    double twist = 0;                                                       // (degrees)
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

bool Shape::drawnFromNumbers() const
{
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

// The mesh of a sweep: the outline of the profile (from the middle of its box) carried along the path, a ring of it at both ends of every piece of the path -- in the frame
// that piece has (the one the field of `path_extrude` has: carried along without turning about the path, turned by the points' rotations, scaled by their thickness) and cut
// by the planes that bisect the corners (square at the ends of an open path) -- and the walls between the rings; the profile's face closes an open path's ends
std::unique_ptr<libfive::Mesh> Shape::sweepMesh(const SweepNumbers& in)
{
    using Vec = Eigen::Vector3d;
    // The profile in the coordinates of its own plane, from the middle of the box of its outline
    std::vector<Vec> where;
    for (const auto& c : in.profile) where.push_back(c.pos);
    // (the outline of a primitive is in the plane z = 0, its axes x and y: no plane is worked out of its corners.  In every other way it is a sketch to the sweep)
    double frame[9] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    if (!in.free && !libfive::pointshapes::planeOf(where, frame)) return nullptr;
    const Vec o(frame[0], frame[1], frame[2]), pu(frame[3], frame[4], frame[5]), pv(frame[6], frame[7], frame[8]);
    const auto outline = libfive::pointshapes::polyline(in.profile, in.profileDefault, true, 14);
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
    auto V = libfive::pointshapes::polyline(in.path, in.pathDefault, in.closed, 14, &owners);
    const int n = int(V.size());
    if (n < 2) return nullptr;
    // The sweep starts at the sketch: the path says how it travels, not where from.  The first point of the path is the middle of the box of the profile (path_extrude of sketch.py
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
    auto tangentAt = [&](int k) -> Vec {
        Vec t;
        if (closed) t = T[size_t((k + pieces - 1) % pieces)] + T[size_t(k % pieces)];
        else if (k == 0) return T[0];
        else if (k == n - 1) return T[size_t(pieces - 1)];
        else t = T[size_t(k - 1)] + T[size_t(k)];
        const double l = t.norm();
        return l > 1e-12 ? Vec(t / l) : T[size_t(std::min(k, pieces - 1))];
    };
    // The frames.  The sketch is carried along the path as it is -- its two axes u and v, each next frame carried along without turning about the path (the double reflection of
    // Wang et al.) -- so the first ring is the sketch itself, at whatever angle the path leaves it.  A sketch whose plane holds the direction of the path (nothing could be swept) is
    // turned the least that puts it across the path (path_extrude of sketch.py does the same)
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
    // (the frame the twist is measured in, across the path: u across it -- v when u lies along it -- u itself when the sketch is across the path)
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
    for (int k = 0; k < pieces; ++k)
    {
        const int j = (k + 1) % n;
        const double phi = 0.5 * (rollAt(k) + rollAt(j)) + total;
        const double cs = std::cos(phi), sn = std::sin(phi);
        const Vec Tk = T[size_t(k)];
        const Vec nk = turned(Uf[size_t(k)], Tk, cs, sn), bk = turned(Wf[size_t(k)], Tk, cs, sn);
        // (which way the profile faces along the path: a sketch that the path leaves backwards is swept the other way round)
        const double facing = nk.cross(bk).dot(Tk) >= 0 ? 1.0 : -1.0;
        const uint32_t base = uint32_t(out->verts.size());
        // (the ring at the start of the piece, then the one at its end: the profile in this piece's frame, slid along the piece onto the plane that bisects the corner)
        for (int end = 0; end < 2; ++end)
        {
            const int at = end == 0 ? k : j;
            const Vec centre = V[size_t(at)];
            const double size = sizeAt(at);
            const Vec cut = tangentAt(at);
            const double slope = cut.dot(Tk);
            // (the ends of an open path are the profile itself -- the sketch at the start, its copy at the end -- not cut square across the path)
            const bool freeEnd = !closed && ((end == 0 && k == 0) || (end == 1 && k == pieces - 1));
            for (const auto& q : prof)
            {
                Vec p = centre + (nk * q.x() + bk * q.y()) * size;
                if (!freeEnd && std::fabs(slope) > 1e-6) p -= Tk * (cut.dot(p - centre) / slope);
                out->verts.push_back(Eigen::Vector3f(float(p.x()), float(p.y()), float(p.z())));
            }
        }
        for (size_t i = 0; i < E; ++i)
        {
            const uint32_t a = base + uint32_t(i), b = base + uint32_t((i + 1) % E), c = base + uint32_t(E) + uint32_t((i + 1) % E), d = base + uint32_t(E) + uint32_t(i);
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
        if (!closed && k == 0)
            for (const Tri& t : cap->branes)
                out->branes.push_back(facing > 0 ? Tri(base + t[0] - capFirst, base + t[2] - capFirst, base + t[1] - capFirst)
                                                 : Tri(base + t[0] - capFirst, base + t[1] - capFirst, base + t[2] - capFirst));
        if (!closed && k == pieces - 1)
            for (const Tri& t : cap->branes)
                out->branes.push_back(facing > 0 ? Tri(base + uint32_t(E) + t[0] - capFirst, base + uint32_t(E) + t[1] - capFirst, base + uint32_t(E) + t[2] - capFirst)
                                                 : Tri(base + uint32_t(E) + t[0] - capFirst, base + uint32_t(E) + t[2] - capFirst, base + uint32_t(E) + t[1] - capFirst));
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
    // Placed by the gizmo: scaled about its point, turned about x, then y, then z (degrees), then moved -- as `gizmo()` places a shape
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

int Shape::controlAfterPiece(int piece) const
{
    std::vector<libfive::pointshapes::Control> cs;
    if (!controlsNow(cs)) return -1;
    std::vector<std::pair<int, double>> owners;
    libfive::pointshapes::polyline(cs, m_controls_default, m_controls_closed, 14, &owners);
    return piece >= 0 && piece < int(owners.size()) ? owners[size_t(piece)].first : -1;
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
    if (hasSweep()) return false;
    buildDeps();
    const auto gizmo = gizmoVars();
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
    // What gizmo() does to a shape (fieldes.stdlib.handles): scaled about its centre, turned about it (x, then y, then z), moved
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
    if (!mesh || !m_gizmo.present || m_isPoint) return false;
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
    if (!allowed || !m_gizmo.present || m_locked || !mesh || has_color || has_result || has_deform || m_flat || hasControls() || m_isPoint ||
        exactCurrent() || m_exact || !m_steps.empty())
        return false;
    const auto own = gizmoVars();
    bool affects = false;
    for (const auto& kv : changed)
    {
        const auto it = vars.find(kv.first);
        if (it == vars.end() || it->second == kv.second || !usesVar(kv.first)) continue;
        if (!own.count(kv.first)) return false;     // (a number that is not its gizmo's changes what the shape IS: it is made again)
        affects = true;
    }
    if (!affects) return false;                     // (not this shape's numbers: updateVars keeps its numbers in step)
    std::map<libfive::Tree::Id, float> next = vars;
    for (const auto& kv : changed)
    {
        const auto it = next.find(kv.first);
        if (it != next.end()) it->second = kv.second;
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
    return true;
}

void Shape::endGizmoPreview()
{
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
    return !m_geometry_known || !(renderGeometry(s) == m_geometry);
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

void Shape::measureExtent(const Settings& s)
{
    m_ext_valid = false;
    if (!s.autoScale) return;
    // A tree that its search could not bound in its time (intervals are too loose for it) is not bounded in it the next time either: it is the tree that is hard, not the numbers
    // -- every render of a drag started with another 50 ms of looking, in the GUI's thread.  It gets a few milliseconds (a number that makes it easy is found at once) until it is
    // another tree
    const float seconds = (tree.is_valid() && m_ext_failed == tree.id()) ? 0.004f : 0.05f;
    m_ext_valid = searchExtent(vars, s.min, s.max, m_ext_lo, m_ext_hi, seconds);
    m_ext_failed = (tree.is_valid() && !m_ext_valid) ? tree.id() : libfive::Tree::Id();
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
    if (hasSweep() || hasPrism()) return false;
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

double Shape::autoResolution(const Settings& s) const
{
    if (!s.autoScale || !m_ext_valid || has_hint || has_result) return s.res;
    const QVector3D region = s.max - s.min, size = m_ext_hi - m_ext_lo;
    const double R = std::max(region.x(), std::max(region.y(), region.z()));
    const double L = std::max(size.x(), std::max(size.y(), size.z()));
    if (!(R > 0) || !(L > 1e-6)) return s.res;
    double res = s.res * std::sqrt(std::max(1.0, R / L));
    res = std::min(res, 2000.0 / L);
    return std::max(res, double(s.res));
}

int Shape::defaultDivFor(const Settings& s)
{
    // (a shape that is drawn from the numbers of its points is meshed in a moment: there is no coarse picture to show first, one pass at the finest level is all it needs)
    if (drawnFromNumbers()) return 0;
    if (!s.autoScale || !m_ext_valid || has_hint || has_result) return s.defaultDiv();
    const QVector3D size = m_ext_hi - m_ext_lo;
    const double L = std::max(size.x(), std::max(size.y(), size.z()));
    if (!(L > 1e-6)) return s.defaultDiv();
    const double across = autoResolution(s) * L;            // (samples across the shape at the finest level)
    return std::max(0, int(std::lround(std::log2(std::max(1.0, across / 64.0)))));
}

bool Shape::ownRegion(const Settings& s, const QVector3D& box_lo, const QVector3D& box_hi, float side,
                      QVector3D& lo, QVector3D& hi, double& res) const
{
    // The region a shape with a resolution of its own is meshed over, and that resolution.  The shape's box (placed by its gizmo) in
    // the scene's region: a cube round the box, `side` long (a power of two of cells of that resolution).  An absolute resolution
    // (custom_resolution: the scene's resolution does not enter) is the shape's wherever it is: a body that reaches out of the scene's
    // region is meshed at it over the part of the cube that is in the region (nothing outside the region is drawn, as for every shape)
    // -- and not at the scene's resolution, which is what a body that only pokes out of the region by a little (the box of a lattice is
    // an estimate) used to get without a word.  A resolution that follows the scene's (an import's part) is the part's only when the
    // whole part is in the region
    if (!has_hint) return false;
    const bool inside = box_lo.x() >= s.min.x() && box_hi.x() <= s.max.x() &&
                        box_lo.y() >= s.min.y() && box_hi.y() <= s.max.y() &&
                        box_lo.z() >= s.min.z() && box_hi.z() <= s.max.z();
    if (!inside && hint_scene_res > 0) return false;
    const QVector3D c = 0.5f * (box_lo + box_hi);
    const float h = 0.5f * side;
    lo = QVector3D(c.x() - h, c.y() - h, c.z() - h);
    hi = QVector3D(c.x() + h, c.y() + h, c.z() + h);
    res = hint_scene_res > 0 ? double(hint_res) * (s.res / double(hint_scene_res)) : double(hint_res);
    if (!inside)
    {
        lo = QVector3D(std::max(lo.x(), s.min.x()), std::max(lo.y(), s.min.y()), std::max(lo.z(), s.min.z()));
        hi = QVector3D(std::min(hi.x(), s.max.x()), std::min(hi.y(), s.max.y()), std::min(hi.z(), s.max.z()));
        if (!(hi.x() > lo.x() && hi.y() > lo.y() && hi.z() > lo.z())) return false;     // (none of it is in the region)
    }
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

bool Shape::sampleSets(const std::vector<std::vector<QVector3D>>& sets, std::vector<std::vector<float>>& out) const
{
    if (!mesh || has_result || m_exact) return false;
    ensureField();
    if (m_tree_nodes > 40000) return false;
    libfive::ArrayEvaluator e(tree, vars);
    out.assign(sets.size(), std::vector<float>());
    const size_t N = LIBFIVE_EVAL_ARRAY_SIZE;
    for (size_t k = 0; k < sets.size(); ++k)
    {
        const auto& pts = sets[k];
        out[k].resize(pts.size());
        for (size_t i = 0; i < pts.size(); i += N)
        {
            const size_t n = std::min(N, pts.size() - i);
            for (size_t j = 0; j < n; ++j) e.set(Eigen::Vector3f(pts[i + j].x(), pts[i + j].y(), pts[i + j].z()), j);
            const auto vals = e.values(n);
            for (size_t j = 0; j < n; ++j) out[k][i + j] = vals[j];
        }
    }
    return true;
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
                for (float x : v)
                {
                    lo = std::min(lo, x);
                    hi = std::max(hi, x);
                }
                m_elem_lo.push_back(lo);
                m_elem_hi.push_back(hi > lo ? hi : lo + 1e-6f * (1 + std::abs(lo)));
            }
        }
        else
        {
            m_grid = ElementGrid();
        }
    }
    color_auto = false;
    color_map = "turbo";
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
    // again at the seed when it has gone the whole line, moved on by advanceFlow
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
            {
                const float tau = std::fmod(flow_time + j * S, P);
                if (tau > T || tau <= 0) continue;
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

void Shape::setShowElements(bool b)
{
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
    const float vlo = m_elem_lo[size_t(m_channel)], vhi = m_elem_hi[size_t(m_channel)];
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
    // (the section plane is in the undeformed model; a tetrahedron is kept by its centre)
    std::vector<char> kept;
    if (cut)
    {
        kept.assign(nt, 0);
        for (size_t t = 0; t < nt; ++t)
        {
            QVector3D c(0, 0, 0);
            for (int p = 0; p < 4; ++p) c += rest(g.mtets[4 * t + size_t(p)]);
            kept[t] = QVector4D::dotProduct(QVector4D(c / 4.0f, 1.0f), m_sec_plane) >= 0;
        }
    }
    const std::vector<float>& values = g.values[size_t(m_channel)];
    const float vlo = m_elem_lo[size_t(m_channel)], vhi = m_elem_hi[size_t(m_channel)];
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
    if (has_result && m_show_elements && hasElements() && size_t(m_channel) < m_elem_lo.size())
        return m_elem_lo[size_t(m_channel)];
    return color_lo;
}

float Shape::displayHi() const
{
    if (has_result && m_show_elements && hasElements() && size_t(m_channel) < m_elem_hi.size())
        return m_elem_hi[size_t(m_channel)];
    return color_hi;
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
    const bool solved = solvedAtNodes(m_channels[size_t(m_channel)].name);
    if (m_show_elements && hasElements()) return T("Element values");
    return solved ? T("Solver values at nodes") : T("Nodal average");
}

QString Shape::valueNoteHelp() const
{
    if (!has_result || m_channel < 0 || size_t(m_channel) >= m_channels.size()) return QString();
    const bool solved = solvedAtNodes(m_channels[size_t(m_channel)].name);
    if (m_show_elements && hasElements())
        return solved ? T("Each element's value at its centre, from its solved node values.")
                      : T("Each element's own value, not averaged.");
    return solved ? T("The solver's values at the nodes, interpolated between them.")
                  : T("The elements' values averaged at each node and interpolated between them. "
                    "Peaks are lower than the elements' own: switch on Elements to see those.");
}

QString Shape::probeLabel() const
{
    if (!has_result || m_channel < 0 || size_t(m_channel) >= m_channels.size()) return color_label;
    if (m_show_elements && hasElements()) return color_label + " [element]";
    return solvedAtNodes(m_channels[size_t(m_channel)].name) ? color_label : color_label + " [nodal average]";
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
    if (!monochrome && elem_lines)
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

bool Shape::updateFrom(const Shape* other)
{
    assert(other->id() == id());
    // (the same tree: only whether its gizmo is shown, and its pivot,
    // can differ, and whether it is locked)
    m_gizmo = other->m_gizmo;
    m_symDef = other->m_symDef;
    m_locked = other->m_locked;
    m_linked = other->m_linked;
    m_isPoint = other->m_isPoint;
    m_hasPointPos = other->m_hasPointPos;
    m_pointPos = other->m_pointPos;
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
    const bool varsChanged = updateVars(other->vars);
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
            const libfive::Tree flat = t.flatten();
            for (const auto* d : flat.walk())
            {
                if (d->op() == libfive::Opcode::VAR_FREE) m_deps.insert(d);
                else if (d->op() == libfive::Opcode::ORACLE)
                {
                    for (const auto& dep : d->oracle_clause().dependencies()) add(dep);
                }
            }
        };
        // (the field of a sweep or a prism that is not made is not walked: the numbers it is made of are the ones of its points, which are below)
        if (m_field_ready.load() || !(hasSweep() || hasPrism()))
        {
            ensureField();
            add(tree);
        }
        add(color_field);
        for (const ControlPoint& c : m_controls)
        {
            for (int a = 0; a < 3; ++a)
            {
                add(c.pos[a]);
                if (c.hasRot) add(c.rot[a]);
            }
            if (c.hasScale) add(c.scale);
        }
        for (const ControlPoint& c : m_prism_points)
        {
            for (int a = 0; a < 3; ++a)
            {
                add(c.pos[a]);
                if (c.hasRot) add(c.rot[a]);
            }
            if (c.hasScale) add(c.scale);
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

    for (auto& v : vs)
    {
        auto va = vars.find(v.first);
        if (va != vars.end() && va->second != v.second)
        {
            const float before = va->second;
            va->second = v.second;      // (kept: the next render takes the whole map)
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
        }
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
                float r, g, b;
                colormapRGB(color_map, t, r, g, b);
                verts[i++] = r;
                verts[i++] = g;
                verts[i++] = b;
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
                        1.0 * s, 0.82 * s, 0.42 * s, 1.0f);
            glUniform4f(Shader::basic->uniformLocation("color_add"),
                        0.10 * s, 0.10 * s, 0.05 * s, 0.0f);
        }
        else if (is2D())
        {   // A 2D shape: a very subtle light blue, so that it is told from a body at a glance
            glUniform4f(Shader::basic->uniformLocation("color_mul"),
                        0.94 * s, 0.84 * s, 0.85 * s, 1.0f);
            glUniform4f(Shader::basic->uniformLocation("color_add"),
                        0.03 * s, 0.21 * s, 0.28 * s, 0.0f);
        }
        else
        {
            glUniform4f(Shader::basic->uniformLocation("color_mul"),
                        0.96 * s, 0.75 * s, 0.63 * s, 1.0f);
            glUniform4f(Shader::basic->uniformLocation("color_add"),
                        0.03 * s, 0.21 * s, 0.26 * s, 0.0f);
        }
        glUniform1i(Shader::basic->uniformLocation("shading"), colored ? 3 : 2);
        const QMatrix4x4 shown = previewed(M);          // (a shape that is being dragged is drawn where its numbers say now, from the mesh it has)
        glUniformMatrix4fv(Shader::basic->uniformLocation("M"),
                           1, GL_FALSE, shown.data());
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
    const auto t0 = std::chrono::steady_clock::now();
    measureExtent(s);
    if (std::getenv("FIELDES_TIMING"))
        fprintf(stderr, "[shape] line %d: measureExtent %.0f ms (%s)\n", source_line + 1,
                1000.0 * std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(), m_ext_valid ? "found" : "none");
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
            if (s.div == MESH_DIV_NEW_VARS) measureExtent(s.settings);          // (the numbers changed: the shape is not where it was)
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
        if (m_flat || hasControls() || hasPrism() || hasSweep()) run_flat_vars = vars;
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
    return libfive::dragWeights(tree);
}

std::pair<libfive::JacobianEvaluator*, libfive::Tape::Handle>
Shape::dragFrom(const QVector3D& v)
{
    ensureField();
    auto e = new libfive::JacobianEvaluator(tree, vars);
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
    if (bm.mesh != nullptr)
    {
        step_values.clear();
        step_disp.clear();
        lines_dirty = true;
        mesh.reset(bm.mesh);
        m_meshGizmo = m_runGizmo;                       // (the new mesh is of the numbers its render started with; shown moved when the shape has been dragged since)
        refreshPreview();
        m_outlines = std::move(bm.outlines);
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
        if (has_color && color_auto && !color_values.empty())
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

        gl_ready = false;
        emit(gotMesh());

        auto t = timer.elapsed();

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
    if (!m_field_ready.load() && (hasSweep() || hasPrism())) return nullptr;
    ensureField();
    return base_tree.cooptimize(canonical).id();
}

QString Shape::identityKey(std::unordered_map<libfive::TreeDataKey, libfive::Tree>& canonical)
{
    QString key = colorKey();
    if (!m_prism_points.empty())
    {
        const auto pid = [&](const libfive::Tree& t) { return t.is_valid() ? quintptr(t.cooptimize(canonical).id()) : quintptr(0); };
        key += QString("|prism %1|f%2").arg(m_prism_points.size()).arg(quintptr(built_tree.id()));
        for (const ControlPoint& c : m_prism_points)
            for (int a = 0; a < 3; ++a) key += QString(",%1").arg(pid(c.pos[a]));
        key += QString(";%1,%2").arg(pid(m_prism_a)).arg(pid(m_prism_b));
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
std::unique_ptr<libfive::Mesh> Shape::prismMesh(const std::vector<libfive::pointshapes::Control>& cs, bool smoothDefault, double a, double b)
{
    std::vector<Eigen::Vector3d> where;
    for (const auto& c : cs) where.push_back(c.pos);
    double frame[9];
    if (!libfive::pointshapes::planeOf(where, frame)) return nullptr;
    const auto verts = libfive::pointshapes::polyline(cs, smoothDefault, true, 14);
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
    double res = autoResolution(s.settings);
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
    mesh_settings.max_err = pow(10, -s.settings.quality);
    mesh_settings.alg = s.alg;

    static const double identity[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    std::vector<std::vector<Eigen::Vector3f>> flat_outlines;
    std::unique_ptr<libfive::Mesh> m;
    // A prism -- a straight extrusion of a sketch of points -- is made from the numbers its points have now, and its a and b: no field is meshed, so it follows a point as it is
    // dragged.  (When it cannot be -- a handle pulled, a gizmo placing it, an outline that crosses itself -- its field is meshed as ever)
    if (hasPrism())
    {
        std::vector<libfive::pointshapes::Control> cs;
        double pa = 0, pb = 0;
        const bool ready = prismNow(run_flat_vars, cs, pa, pb);
        if (ready) m = prismMesh(cs, m_prism_default, pa, pb);
        if (std::getenv("FIELDES_TIMING"))
            fprintf(stderr, "[prism] line %d: %s\n", source_line + 1,
                    m ? "drawn from the numbers of its points" : ready ? "its outline cannot be cut into triangles: its field is meshed" : "its guard or its numbers do not hold: its field is meshed");
    }
    if (!m && hasSweep())
    {
        SweepNumbers sn;
        const bool ready = sweepNow(run_flat_vars, sn);
        if (ready) m = sweepMesh(sn);
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
    if (!m && m_cache_on.load())
    {
        if (has_result)
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
            const auto verts = libfive::pointshapes::polyline(cs, m_controls_default, m_controls_closed, 14);
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
        }
    }
    else
    {
        ensureField();                     // (made here when nothing made it yet: a sweep that could not be drawn from its numbers)
        m = run_step_exact ? exactMeshOf(*run_step_exact, identity, r)
          : exactCurrent() ? exactMeshIn(r)
          : m_flat ? libfive::Mesh::renderFlat(tree, run_flat_vars, mesh_region, run_has_frame ? run_frame : nullptr, mesh_settings,
                                               &flat_outlines)
          : libfive::Mesh::render(es.data(), mesh_region, mesh_settings);
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

    // A result: every field and the displacements at every vertex
    if (has_result && out.mesh)
    {
        const size_t nt = run_channels.size() + (run_has_deform ? 3 : 0);
        if (result_evals.size() != nt)
        {
            result_evals.clear();
            for (const auto& c : run_channels) result_evals.emplace_back(new libfive::ArrayEvaluator(c.tree));
            if (run_has_deform)
                for (int a = 0; a < 3; a++) result_evals.emplace_back(new libfive::ArrayEvaluator(run_deform[a]));
        }
        const auto& verts = out.mesh->verts;
        out.channels.resize(run_channels.size());
        for (size_t c = 0; c < run_channels.size(); c++) evalAll(*result_evals[c], verts, out.channels[c]);
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
                // (with the script's numbers: a colour field may hold var()s -- a selection on a part placed with gizmo())
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
        if (color_map == "bc" && verts.size() > 1)
        {
            double extent = 0;
            for (int a = 0; a < 3; a++) extent = std::max(extent, r.upper[a] - r.lower[a]);
            const float smallest2 = float((0.002 * extent) * (0.002 * extent));
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

namespace {

// Bump when what the mesher makes from the same shape changes (a mesh kept by an older one would
// be read in its place).  Not tied to a build: a rebuild that changes no mesh keeps the meshes.
const char* const kRenderCacheVersion = "fdrc1";

const quint32 kCacheMagic = 0x43524446u;       // "FDRC"
const quint32 kCacheFormat = 1;

QString cacheFile(const std::string& key)
{
    const QByteArray h = QCryptographicHash::hash(QByteArray::fromStdString(key), QCryptographicHash::Sha1).toHex();
    return Shape::renderCacheDir() + "/" + QString::fromLatin1(h) + ".fdmesh";
}

qint64 cacheLimitBytes()
{
    bool ok = false;
    const qint64 mb = qEnvironmentVariableIntValue("FIELDES_RENDER_CACHE_MB", &ok);
    return (ok && mb > 0 ? mb : 4096) * 1024 * 1024;
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
    char buf[400];
    std::snprintf(buf, sizeof(buf), "|%a %a %a %a %a %a|%a|%a|%d",
                  region.lower.x(), region.lower.y(), region.lower.z(),
                  region.upper.x(), region.upper.y(), region.upper.z(),
                  res, double(s.settings.quality), int(s.alg));
    return std::string(kRenderCacheVersion) + "|" + cache_tree_key + buf + (m_flat ? "|flat" : "");
}

bool Shape::readRenderCache(const std::string& key, BoundedMesh& out) const
{
    QFile f(cacheFile(key));
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) return false;
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
    if (h[0] != kCacheMagic || h[1] != kCacheFormat) return false;
    const quint64 keyLen = h[2], nv = h[3], nt = h[4], nc = h[5];
    const quint64 need = quint64(head) + keyLen + nv * 12 + nt * 12 + nc * 4;
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
    if (has_color && out.values.size() != nv) return false;
    out.mesh = mesh.release();
    out.region = libfive::Region<3>({box[0], box[1], box[2]}, {box[3], box[4], box[5]});
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
    if (!f.commit()) return false;
    pruneRenderCache();
    return true;
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
