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

#include "fieldes/shape.hpp"
#include "fieldes/shader.hpp"
#include "fieldes/colormap.hpp"

#include "libfive/eval/tape.hpp"
#include "libfive/eval/evaluator.hpp"
#include "libfive/tree/data.hpp"
#include "libfive/tree/content_key.hpp"
#include "libfive/oracle/oracle_clause.hpp"

namespace FielDes {

const int Shape::MESH_DIV_EMPTY;
const int Shape::MESH_DIV_ABORT;
const int Shape::MESH_DIV_NEW_VARS;
const int Shape::MESH_DIV_NEW_VARS_SMALL;
const int Shape::MESH_DIV_NEW_TREE;

Shape::Shape(const libfive::Tree& t,
             std::map<libfive::Tree::Id, float> vars)
    : tree(t.optimized()), built_tree(t), vars(vars),
      vert_vbo(QOpenGLBuffer::VertexBuffer),
      tri_vbo(QOpenGLBuffer::IndexBuffer)
{
    cache_vars = vars;

    // Construct evaluators to run meshing (in parallel)
    es.reserve(8);
    for (unsigned i=0; i < es.capacity(); ++i)
    {
        es.emplace_back(libfive::Evaluator(tree, vars));
    }
    base_tree = tree;

    connect(this, &Shape::gotMesh, this, &Shape::redraw);
    connect(&mesh_watcher, &decltype(mesh_watcher)::finished,
            this, &Shape::onFutureFinished);
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

void Shape::setRenderHint(QVector3D lo, QVector3D hi, float res, float side, float scene_res)
{
    has_hint = res > 0 && side > 0 && scene_res > 0;
    hint_lo = lo;
    hint_hi = hi;
    hint_res = res;
    hint_side = side;
    hint_scene_res = scene_res;
}

bool Shape::hasHandles() const
{
    if (m_locked || !m_handles.present || m_handles.mode == Handles::NEVER) return false;
    if (m_handles.mode == Handles::CLICK && !isSelected()) return false;
    for (int a = 0; a < 3; ++a)
    {
        if ((m_handles.move[a] && vars.count(m_handles.move[a])) ||
            (m_handles.rotate[a] && vars.count(m_handles.rotate[a])) ||
            (m_handles.scale[a] && vars.count(m_handles.scale[a])))
        {
            return true;
        }
    }
    return false;
}

bool Shape::hasMoveVars() const
{
    if (m_locked || !m_handles.present) return false;
    for (int a = 0; a < 3; ++a)
    {
        if (m_handles.move[a] && vars.count(m_handles.move[a])) return true;
    }
    return false;
}

std::set<libfive::Tree::Id> Shape::gizmoVars() const
{
    std::set<libfive::Tree::Id> out;
    for (int a = 0; a < 3; ++a)
    {
        for (const auto id : {m_handles.move[a], m_handles.rotate[a], m_handles.scale[a]})
        {
            if (id) out.insert(id);
        }
    }
    return out;
}

bool Shape::nativeDragOk() const
{
    // (locked: the shape is not pulled by its surface; the gizmo's mode does not matter, and the gizmo has priority
    // where it is shown)
    if (m_locked) return false;
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

static float handleNumber(const std::map<libfive::Tree::Id, float>& vars, libfive::Tree::Id id)
{
    const auto it = id ? vars.find(id) : vars.end();
    return it == vars.end() ? 0.0f : it->second;
}

QVector3D Shape::handleMove() const
{
    return QVector3D(handleNumber(vars, m_handles.move[0]), handleNumber(vars, m_handles.move[1]),
                     handleNumber(vars, m_handles.move[2]));
}

QVector3D Shape::handleScale() const
{
    auto one = [&](libfive::Tree::Id id) {
        const auto it = id ? vars.find(id) : vars.end();
        return it == vars.end() ? 1.0f : it->second;
    };
    return QVector3D(one(m_handles.scale[0]), one(m_handles.scale[1]), one(m_handles.scale[2]));
}

QVector3D Shape::handleRotate() const
{
    return QVector3D(handleNumber(vars, m_handles.rotate[0]), handleNumber(vars, m_handles.rotate[1]),
                     handleNumber(vars, m_handles.rotate[2]));
}

QMatrix4x4 Shape::handleRotation(const QVector3D& degrees)
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

void Shape::placeHint()
{
    run_hint_lo = hint_lo;
    run_hint_hi = hint_hi;
    run_hint_side = hint_side;
    if (!has_hint || !m_handles.present)
    {
        return;
    }
    // The part's own-resolution cube follows its handles: scaled, moved by the
    // translation and, once turned, big enough for any turn of the box
    const QVector3D turn = handleRotate();
    const QVector3D grow = handleScale();
    QVector3D c = 0.5f * (hint_lo + hint_hi);
    QVector3D half = 0.5f * (hint_hi - hint_lo);
    float side = hint_side;
    if (grow != QVector3D(1, 1, 1))
    {
        c = m_handles.about + QVector3D(grow.x() * (c.x() - m_handles.about.x()),
                                        grow.y() * (c.y() - m_handles.about.y()),
                                        grow.z() * (c.z() - m_handles.about.z()));
        half = QVector3D(half.x() * std::fabs(grow.x()), half.y() * std::fabs(grow.y()),
                         half.z() * std::fabs(grow.z()));
        side *= std::max({std::fabs(grow.x()), std::fabs(grow.y()), std::fabs(grow.z()), 1e-3f});
    }
    if (turn != QVector3D(0, 0, 0))
    {
        c = m_handles.about + handleRotation(turn).map(c - m_handles.about);
        half *= 1.75f;
        side *= 1.75f;
    }
    c += handleMove();
    run_hint_lo = c - half;
    run_hint_hi = c + half;
    run_hint_side = side;
}

QString Shape::colorKey() const
{
    return colorKeyBase();
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
        (has_cutoff ? QString("|patch %1").arg(color_cutoff) : QString());
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
    if (!probe_eval) probe_eval.reset(new libfive::ArrayEvaluator(color_field));
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
    // (a plain shape may have steps that bring their own geometry only: the body of a flow shape optimisation)
    if (!has_result)
        for (auto& st : steps) st.channels.clear();
    m_steps = std::move(steps);
    step_meshes.clear();
    step_values.clear();
    step_disp.clear();
    m_step = m_steps.empty() ? -1 : std::max(0, std::min(int(m_steps.size()) - 1, current));
    applyStepState();
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
    stashStepMesh();
    m_step = k;
    applyStepState();
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
    if (m_show_elements && hasElements()) return "Element values";
    return solved ? "Solver values at nodes" : "Nodal average";
}

QString Shape::valueNoteHelp() const
{
    if (!has_result || m_channel < 0 || size_t(m_channel) >= m_channels.size()) return QString();
    const bool solved = solvedAtNodes(m_channels[size_t(m_channel)].name);
    if (m_show_elements && hasElements())
        return solved ? "Each element's value at its centre, from its solved node values."
                      : "Each element's own value, not averaged.";
    return solved ? "The solver's values at the nodes, interpolated between them."
                  : "The elements' values averaged at each node and interpolated between them. "
                    "Peaks are lower than the elements' own: switch on Elements to see those.";
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
    // (the same tree: only whether its handles are shown, and their pivot,
    // can differ, and whether it is locked)
    m_handles = other->m_handles;
    m_locked = other->m_locked;
    bool started = false;
    m_cache_forced.store(other->m_cache_forced.load());
    if (other->m_cache_on.load() != m_cache_on.load())
    {
        m_cache_on.store(other->m_cache_on.load());
        cache_state.store(CACHE_OFF);
        if (m_cache_on.load()) started = keepCurrent();
        emit(cacheStateChanged());
    }
    return updateVars(other->vars) || started;
}

void Shape::buildDeps() const
{
    if (!m_deps_known)
    {
        // (the variables inside an oracle -- a mesh moved by a gizmo -- are not walked with the tree)
        std::function<void(const libfive::Tree&)> add = [&](const libfive::Tree& t) {
            if (!t.is_valid()) return;
            for (const auto* d : t.walk())
            {
                if (d->op() == libfive::Opcode::VAR_FREE) m_deps.insert(d);
                else if (d->op() == libfive::Opcode::ORACLE)
                {
                    for (const auto& dep : d->oracle_clause().dependencies()) add(dep);
                }
            }
        };
        add(tree);
        add(color_field);
        for (int a = 0; a < 3; ++a) add(deform_tree[a]);
        for (const auto& c : m_channels) add(c.tree);
        for (int a = 0; a < 3; ++a)
        {
            if (m_handles.move[a]) m_deps.insert(m_handles.move[a]);
            if (m_handles.rotate[a]) m_deps.insert(m_handles.rotate[a]);
            if (m_handles.scale[a]) m_deps.insert(m_handles.scale[a]);
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
                const float t = has_cutoff ? 1.0f : (span > 0 ? (color_values[vi] - color_lo) / span : 0.5f);
                float r, g, b;
                colormapRGB(color_map, t, r, g, b);
                verts[i++] = r;
                verts[i++] = g;
                verts[i++] = b;
                if (has_cutoff) outside = color_values[vi] - color_cutoff;
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
            glUniform4f(Shader::basic->uniformLocation("color_mul"), k, k, k, 1.0f);
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
        else
        {
            glUniform4f(Shader::basic->uniformLocation("color_mul"),
                        0.96 * s, 0.75 * s, 0.63 * s, 1.0f);
            glUniform4f(Shader::basic->uniformLocation("color_add"),
                        0.03 * s, 0.21 * s, 0.26 * s, 0.0f);
        }
        glUniform1i(Shader::basic->uniformLocation("shading"), colored ? 3 : 2);
        glUniformMatrix4fv(Shader::basic->uniformLocation("M"),
                           1, GL_FALSE, M.data());
        glUniform1i(Shader::basic->uniformLocation("use_rest"), 1);
        glUniform1i(Shader::basic->uniformLocation("patch_mode"), has_cutoff ? 1 : 0);
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
        glUniformMatrix4fv(Shader::basic->uniformLocation("M"),
                           1, GL_FALSE, M.data());
        glUniform1i(Shader::basic->uniformLocation("shading"), 0);
        glUniform4f(Shader::basic->uniformLocation("color_add"),
                color.redF(), color.greenF(), color.blueF(), 1.0f);
        glUniform4f(Shader::basic->uniformLocation("color_mul"), 0, 0, 0, 0);
        glUniform1i(Shader::basic->uniformLocation("use_rest"), 1);
        glUniform1i(Shader::basic->uniformLocation("patch_mode"), has_cutoff ? 1 : 0);

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
    startRender(RenderSettings { s, s.defaultDiv(), alg });
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
            mesh_settings.cancel.store(true);
            next = s;
            next_follows = false;
        }
    }
    else
    {
        if (s.div == MESH_DIV_NEW_TREE || es_stale)
        {   // the shown step's geometry
            rebuildEvaluators();
            if (s.div == MESH_DIV_NEW_TREE) s.div = default_div;
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
        }
        if (s.div == MESH_DIV_NEW_VARS ||
            s.div == MESH_DIV_NEW_VARS_SMALL)
        {
            for (auto& e : es)
            {
                e.updateVars(vars);
            }
            // (what the render cache's keys are of: the numbers the evaluators hold now)
            cache_vars = vars;
            ++cache_vars_gen;
            s.div = (s.div == MESH_DIV_NEW_VARS) ? default_div : 0;
        }

        target_div = s.div;

        // (a level waiting in the thread pool has done nothing yet: without
        // this it would still show the last level's finished meshing)
        render_stage.store(0);
        render_progress.store(0.0);
        colour_progress.store(0.0);

        placeHint();
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

std::pair<libfive::JacobianEvaluator*, libfive::Tape::Handle>
Shape::dragFrom(const QVector3D& v)
{
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
    return base_tree.cooptimize(canonical).id();
}

////////////////////////////////////////////////////////////////////////////////
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
    // A part with its own resolution is meshed on its own (setRenderHint):
    // over a cube around it, at the resolution it needs
    double res = s.settings.res;
    libfive::Region<3> mesh_region = r;
    bool own_render = false;
    if (has_hint &&
        run_hint_lo.x() >= s.settings.min.x() && run_hint_hi.x() <= s.settings.max.x() &&
        run_hint_lo.y() >= s.settings.min.y() && run_hint_hi.y() <= s.settings.max.y() &&
        run_hint_lo.z() >= s.settings.min.z() && run_hint_hi.z() <= s.settings.max.z())
    {
        const QVector3D c = 0.5f * (run_hint_lo + run_hint_hi);
        const float h = 0.5f * run_hint_side;
        mesh_region = libfive::Region<3>({c.x() - h, c.y() - h, c.z() - h},
                                         {c.x() + h, c.y() + h, c.z() + h});
        res = double(hint_res) * (s.settings.res / double(hint_scene_res));
        own_render = true;
    }
    mesh_settings.min_feature = 1 / (res / (1 << s.div));
    mesh_settings.max_err = pow(10, -s.settings.quality);
    mesh_settings.alg = s.alg;

    // The render cache: the finished mesh of this very shape may be on disk (a coarser level is not
    // kept: a render is read from it in one step, or meshed level by level)
    const auto renderStart = std::chrono::steady_clock::now();
    std::string cacheKey;
    if (m_cache_on.load())
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

    auto m = libfive::Mesh::render(es.data(), mesh_region, mesh_settings);
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
                if (!color_evals[t]) color_evals[t].reset(new libfive::ArrayEvaluator(color_field));
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
            for (size_t t = 1; t < T; t++) threads.emplace_back(work, t);
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
                    if ((ca == cb && cb == cc) || longest <= smallest2)
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
        case CACHE_KEPT:    return "meshed, and kept in the render cache";
        case CACHE_READ:    return "read from the render cache";
        case CACHE_NO_KEY:  return "cannot be kept: it depends on something no other run can recognise "
                                   "(a variable it has no value for, or a solved analysis)";
        case CACHE_RESULT:  return "a result shown with its fields is not kept";
        case CACHE_FAILED:  return "the mesh could not be written to the cache folder";
        default:            return "on: kept once it has been meshed";
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
    if (cache_tree_key.empty()) return std::string();
    char buf[400];
    std::snprintf(buf, sizeof(buf), "|%a %a %a %a %a %a|%a|%a|%d",
                  region.lower.x(), region.lower.y(), region.lower.z(),
                  region.upper.x(), region.upper.y(), region.upper.z(),
                  res, double(s.settings.quality), int(s.alg));
    return std::string(kRenderCacheVersion) + "|" + cache_tree_key + buf;
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
