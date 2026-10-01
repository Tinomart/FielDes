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
#pragma once

#include <QObject>
#include <QtConcurrent>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QMatrix4x4>
#include <QVector3D>
#include <QVector4D>

#include <chrono>
#include <memory>
#include <set>
#include <unordered_set>
#include <cstdio>
#include <cstdlib>

#include "libfive/eval/evaluator.hpp"
#include "libfive/eval/eval_array.hpp"
#include "libfive/step/step_exact.hpp"
#include "libfive/tree/tree.hpp"

#include "libfive/render/brep/mesh.hpp"
#include "libfive/render/brep/progress.hpp"
#include "libfive/render/brep/region.hpp"
#include "libfive/render/brep/settings.hpp"

#include "fieldes/settings.hpp"

namespace libfive { class Tape; /*  forward declaration */ }

namespace FielDes {


class Shape : public QObject, QOpenGLFunctions
{
    Q_OBJECT
public:
    Shape(const libfive::Tree& t,
          std::map<libfive::Tree::Id, float> vars);

    /*
     *  In destructor, wait for computation to finish
     *  (otherwise Evaluators may be destroyed early)
     */
    ~Shape();

    /*  Constructs OpenGL objects as needed  */
    void draw(const QMatrix4x4& M);

    /*
     *  Draws with a monochrome shader (for pick buffer)
     *  This is a no-op if the mesh and OpenGL buffers aren't ready
     */
    void drawMonochrome(const QMatrix4x4& M, QColor color);

    /*
     *  Kicks off a mesh rendering operation in a separate thread
     */
    void startRender(Settings s, libfive::BRepAlgorithm alg);

    /*
     *  Checks whether the shape is done rendering
     */
    bool done() const;

    /*
     *  Returns the raw mesh object pointer
     */
    const libfive::Mesh* getMesh() const { return mesh.data(); }

    /*
     *  Looks up the tree's ID
     */
    libfive::Tree::Id id() const { return tree.id(); }

    /*
     *  0-based line of the script statement that displayed this shape
     *  (-1 if unknown), used to link the viewport with the model tree
     */
    int sourceLine() const { return source_line; }
    void setSourceLine(int line) { source_line = line; }

    /*
     *  Highlights the shape as the model tree's selection
     */
    void setSelected(bool s);
    bool isSelected() const { return selected; }

    /*
     *  Bounds of the current mesh (empty region if not meshed yet)
     */
    const libfive::Region<3>& getMeshBounds() const { return mesh_bounds; }
    bool hasMesh() const { return mesh && gl_ready; }

    /*  Read-only access for evaluating the field (slice viewer)  */
    const libfive::Tree& getTree() const { return tree; }

    /*
     *  Colouring by a scalar field (an FEA result, any Shape): the field is
     *  evaluated at the mesh's vertices after every render and mapped
     *  through `colormap` over [lo, hi] -- the values' own range on the
     *  mesh when autoRange.  `label` names the quantity (legend, probe).
     */
    void setColorField(const libfive::Tree& field, float lo, float hi, bool autoRange,
                       const QString& label, const QString& colormap);
    bool hasColorField() const { return has_color; }
    const libfive::Tree& colorFieldTree() const { return color_field; }
    QString colorLabel() const { return color_label; }
    QString colorMap() const { return color_map; }
    float colorLo() const { return color_lo; }
    float colorHi() const { return color_hi; }
    /*  Identifies the colouring and the exact regions (shapes are only
     *  reused when it matches)  */
    QString colorKey() const;

    /*
     *  Exact regions (fieldes.stdlib.cad_import.exclude): inside a region --
     *  a field, negative inside it -- an imported part's surface, meshed
     *  straight from its STEP file, replaces this shape's mesh whenever it
     *  is rendered: the shape's triangles inside the region go, the exact
     *  surface's triangles inside it (cut by evaluating the region's field,
     *  to the cell of the render) are put in.  Every number is a tree -- a
     *  constant or an expression of the script's variables -- evaluated with
     *  the current variables at render time, so dragging updates the exact
     *  piece too.
     */
    struct ExactRegion
    {
        std::string path;
        int solid = 0, instance = 0;
        std::vector<libfive::Tree> matrix, region;   // 16 each, row-major
        libfive::Tree field = libfive::Tree::invalid();   // the region (invalid: everywhere), placed by `region`
        libfive::Tree quality = libfive::Tree::invalid();
    };
    void setExactRegions(std::vector<ExactRegion> regions);

    /*  A part meshed on its own (an imported part, fieldes.stdlib.cad_import.
     *  roi_resolution): over a cube of `side` around its box, at its own
     *  resolution `res` -- set by its own features and size, so a very thin
     *  part is fine without the whole scene being fine, and a big simple one is
     *  not finer than it needs.  `res` was chosen for the scene resolution
     *  `scene_res`; when the scene's resolution is changed (studio.
     *  set_resolution) the part's changes with it, in proportion.  A part whose
     *  box is not inside the render region uses the scene's resolution. */
    void setRenderHint(QVector3D lo, QVector3D hi, float res, float side, float scene_res);

    /*  Handles (fieldes.stdlib.handles): a placed part is the shape scaled about
     *  `about`, rotated by three angles (degrees; x, then y, then z) and moved,
     *  by numbers that are var()s of the script.  The mode says what can be
     *  dragged: GIZMO draws move arrows, rotation rings and scale knobs at its
     *  pivot and dragging them changes those vars (and so the script text);
     *  NATIVE lets its own surfaces be dragged instead (Studio's own handles, see
     *  nativeDragOk); LOCK lets nothing be.  An id is null where the number is a
     *  plain constant.  */
    struct Handles
    {
        enum Mode { LOCK, GIZMO, NATIVE };
        bool present = false;
        Mode mode = GIZMO;
        QVector3D about;
        libfive::Tree::Id move[3] = {nullptr, nullptr, nullptr};
        libfive::Tree::Id rotate[3] = {nullptr, nullptr, nullptr};
        libfive::Tree::Id scale[3] = {nullptr, nullptr, nullptr};
    };
    void setHandles(const Handles& h) { m_handles = h; }
    const Handles& handles() const { return m_handles; }
    /*  In the gizmo mode, and with at least one number to drag  */
    bool hasHandles() const;
    /*  The numbers now (constants and unknown ones as 0; a scale as 1)  */
    QVector3D handleMove() const;
    QVector3D handleRotate() const;
    QVector3D handleScale() const;
    /*
     *  Studio's own handles: hover a surface and drag it, which changes the numbers (the
     *  script's var()s, or those expose() made) that place that surface.  Available when
     *  the shape has such numbers and its mode is not the gizmo or the lock.
     */
    bool nativeDragOk() const;
    /*  The variables of the gizmo: dragging a surface leaves them where they are  */
    std::set<libfive::Tree::Id> gizmoVars() const;
    /*  Where the part turns after the move: the pivot the handles sit at  */
    QVector3D handlePivot() const { return m_handles.about + handleMove(); }
    /*  The rotation the angles make (the library's rotate_x/y/z, x first)  */
    static QMatrix4x4 handleRotation(const QVector3D& degrees);

    /*  How much of this shape's render is done (0..1), counted, not
     *  predicted: the levels of the render (coarse to fine, each with about
     *  4x the cells of the one before -- halving the cell size along a
     *  surface quadruples them -- which is its share) done, and the
     *  mesher's own count of the level in progress.  */
    double renderFraction() const;
    /*  How much work this shape's render is, as far as it is known, for the
     *  render bar to weigh the shapes by.  (A bar that counts every shape the
     *  same sits at 97 % while the few big shapes of a many-part scene are
     *  still meshing.)  The vertices of its finest level: those of the last
     *  level meshed, taken up by the growth per level that the level before
     *  it measured; -1 until a level of this render is done.  */
    double renderWeight() const;
    // (for the bar's log, FIELDES_TIMING)
    QString renderDebug() const;
    bool hasExactRegions() const { return !exact_regions.empty(); }
    /*  The field's value at p (a point of the displayed, possibly
     *  deformed, model); false without a colour field  */
    bool probe(const QVector3D& p, float& value);

    /*
     *  A structural result shown on the part: fields to colour it by (one
     *  at a time, switchable), the displacements to deform it by (scaled),
     *  and the analysis elements -- the tetrahedra (or hexahedra) the solver
     *  used, each with its own value -- which can be shown instead of the
     *  smooth part.
     */
    struct FieldChannel { QString name, label; libfive::Tree tree = libfive::Tree::invalid(); float lo = 0, hi = 1; };
    struct ElementGrid
    {
        QVector3D lo;
        float h = 1;
        int dims[3] = {0, 0, 0};
        std::vector<float> fraction;    // x fastest, 0 = no element
        // What a cell is made of: six tetrahedra (the solver's subdivision: for
        // each order of the axes, the corners 000, e_a, e_a + e_b, 111) or one
        // hexahedron
        bool tetrahedra = false;
        QString text;                   // e.g. "241,203 tetrahedra (six in each of 40,201 cells)"
        // Per channel: its value in every element, as the element has it -- the
        // cells with a fraction > 0 in order, six values each for tetrahedra (or, for a
        // mesh, one value per tetrahedron)
        std::vector<std::vector<float>> values;
        // A body-fitted tetrahedral mesh instead of a grid of cells: exactly what the
        // solver used
        bool isMesh = false;
        std::vector<float> mverts;          // 3 per vertex
        std::vector<int32_t> mtets;         // 4 per tetrahedron
        std::vector<int32_t> mfaces;        // 3 per boundary triangle (normals out)
        std::vector<int32_t> mfaceTet;      // the tetrahedron of each
        std::vector<float> mdisp;           // the solved displacement at every vertex (3 each)
        std::vector<int32_t> madj;          // per tetrahedron and face (opposite each vertex): the one across it, or -1
    };
    void setResult(std::vector<FieldChannel> channels, int current,
                   const libfive::Tree& ux, const libfive::Tree& uy, const libfive::Tree& uz,
                   float deformScale, float deformAuto, ElementGrid grid);
    bool hasResult() const { return has_result; }
    const std::vector<FieldChannel>& channels() const { return m_channels; }
    int channel() const { return m_channel; }
    void setChannel(int c);
    bool hasDeformation() const { return has_deform; }
    const libfive::Tree& deformTree(int axis) const { return deform_tree[axis]; }
    QString channelLabel() const
    { return m_channel >= 0 && size_t(m_channel) < m_channels.size() ? m_channels[size_t(m_channel)].label : QString(); }
    float deformScale() const { return m_deform; }
    float deformAuto() const { return m_deform_auto; }
    void setDeformScale(float s);
    bool hasElements() const { return m_grid.isMesh ? !m_grid.mtets.empty() : !m_grid.fraction.empty(); }
    bool showElements() const { return m_show_elements; }
    void setShowElements(bool b);
    QString elementText() const { return m_grid.text; }
    /*  The colour range of what is shown: of the elements' own values while the
     *  elements are shown, else of the smooth field  */
    float displayLo() const;
    float displayHi() const;
    /*  What the shown values are (for the result card), and how a hover names
     *  the value it reads  */
    QString valueNote() const;          // a few words: what the shown values are
    QString valueNoteHelp() const;      // the explanation behind it (a tooltip)
    QString probeLabel() const;
    /*  The value of the element drawn at p (a point on the drawn elements)  */
    bool probeElement(const QVector3D& p, float& value);
    /*  The section plane, for whole-element display: with `whole`, the
     *  elements on the kept side (by their centre) are shown entire
     *  instead of being cut by the plane  */
    void setElementSection(bool enabled, const QVector4D& plane, bool whole);
    bool wholeElements() const { return m_show_elements && m_sec_enabled && m_sec_whole; }

    /*
     *  Updates variables from another Shape
     *  (which must point to the same Tree)
     *
     *  Returns true if variable values have changed.
     */
    bool updateFrom(const Shape* other);

    /*
     *  Updates variables in the Evaluator, scheduling a new
     *  (min-resolution) render if things have changed
     *
     *  Returns true if variable values have changed.
     */
    bool updateVars(const std::map<libfive::Tree::Id, float>& vs);

    /*
     *  Checks to see whether this shape has attached vars
     *  (which determines whether it's draggable)
     */
    bool hasVars() const { return vars.size(); }

    /*
     *  Whether this shape depends on the variable: its tree, its colouring, its
     *  deformation, its exact regions or its handles use it.  (Every shape is
     *  given all of the script's variables, so a changed number only makes a
     *  shape render again when it is one this shape uses.)
     */
    bool usesVar(libfive::Tree::Id id) const;

    /*
     *  Returns a new evaluator specialized at the given drag position
     *
     *  Ownership is transfered, so the caller is responsible for deleting
     *  the evaluator (or storing it in an owned structure)
     */
    std::pair<libfive::JacobianEvaluator*, std::shared_ptr<libfive::Tape>>
    dragFrom(const QVector3D& pt);

    /*
     *  Returns another pointer to the solution map
     */
    const std::map<libfive::Tree::Id, float>& getVars() const
    { return vars; }

    /*
     *  Looks up the shape's bounds
     */
    const libfive::Region<3>& getRenderBounds() const { return render_bounds; }

    /*
     *  Sets grabbed and redraws as necessary
     */
    void setGrabbed(bool g);

    /*
     *  Sets hover and redraws as necessary
     */
    void setHover(bool h);

    /*
     *  Destroys all OpenGL objects associated with this shape
     *
     *  This must be called when the context is current, and must
     *  be called before the context is destroyed (otherwise, the Shape
     *  destructor may try to use a now-destroyed context)
     */
    void freeGL();

    /*
     *  Returns a unique ID using the given deduplication map
     */
    libfive::Tree::Id getUniqueId(
        std::unordered_map<libfive::TreeDataKey, libfive::Tree>& canonical);

signals:
    void gotMesh();
    void redraw();

public slots:
    void deleteLater();

protected slots:
    void onFutureFinished();

protected:
    struct RenderSettings {
        Settings settings;
        int div;
        libfive::BRepAlgorithm alg;
    };
    struct BoundedMesh {
        libfive::Mesh* mesh = nullptr;
        libfive::Region<3> region;
        std::vector<float> values;      // colour field at the vertices
        std::vector<std::vector<float>> channels;   // result fields at the vertices
        std::vector<Eigen::Vector3f> disp;          // displacements at the vertices
    };

    void startRender(RenderSettings s);
    BoundedMesh renderMesh(RenderSettings s);

    bool grabbed=false;
    bool hover=false;
    bool selected=false;
    int source_line=-1;

    QFuture<BoundedMesh> mesh_future;
    QFutureWatcher<BoundedMesh> mesh_watcher;
    libfive::BRepSettings mesh_settings;

    libfive::Tree tree;
    std::map<libfive::Tree::Id, float> vars;
    std::vector<libfive::Evaluator,
                Eigen::aligned_allocator<libfive::Evaluator>> es;

    QScopedPointer<libfive::Mesh> mesh;
    libfive::Region<3> render_bounds;
    libfive::Region<3> mesh_bounds;
    RenderSettings next;

    /*  running marks not just whether the future has finished, but whether
     *  the main thread has handled it.  This prevents situations where the
     *  mesh_future.isRunning() == false but onFutureFinished hasn't yet
     *  been called.  */
    bool running=false;

    bool gl_ready=false;
    QOpenGLVertexArrayObject vao;
    QOpenGLBuffer vert_vbo;
    QOpenGLBuffer tri_vbo;

    QElapsedTimer timer;

    // The mesher's progress on the level being rendered (0..1), from a
    // ProgressHandler made for each level (one only works once: reused,
    // it reported nothing after the first level).  It counts three phases
    // as a third each -- building the octree, walking it for the mesh,
    // freeing it -- and records when each ended (it passed 1/3, 2/3).
    struct RenderProgress : public libfive::ProgressHandler
    {
        explicit RenderProgress(std::atomic<double>& v, int div)
            : value(v), div(div), log(std::getenv("FIELDES_MESH_PROGRESS") != nullptr),
              t0(std::chrono::steady_clock::now()) {}
        double elapsed() const
        {
            return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        }
        void progress(double d) override
        {
            value.store(d);
            const double t = elapsed();
            if (t13 < 0 && d >= 1.0 / 3 - 1e-6) t13 = t;
            if (t23 < 0 && d >= 2.0 / 3 - 1e-6) t23 = t;
            if (log) fprintf(stderr, "[mesh-progress] %d %.3f %.4f\n", div, t, d);
        }
        std::atomic<double>& value;
        int div;
        bool log;
        std::chrono::steady_clock::time_point t0;
        double t13 = -1, t23 = -1;      // (read once the render is done)
    };
    std::atomic<double> render_progress{0.0};
    // The phases' real shares of a level's time vary a lot between models
    // (building the octree took 65-100 %): the shares the previous level of
    // this render measured (the first level: a third each, as counted)
    std::atomic<double> share_build{1.0 / 3}, share_walk{1.0 / 3};
    // After meshing, a level joins exact regions and colours its vertices
    // (by a colour field or a result's fields): how far the colouring is
    // (0..1), and that stage's share of a level's time as the previous
    // level of this render measured it (0 on the first level)
    std::atomic<int> render_stage{0};           // 0 meshing, 1 after meshing
    std::atomic<double> colour_progress{0.0};
    std::atomic<double> colour_share{0.0};
    // The render's first (coarsest) level, and the last level started
    std::atomic<int> seq_first_div{0};
    int seq_div = -100;
    // The last two levels meshed: their level (0 is the finest), the render
    // they belong to (its first level) and their vertices
    int mesh_div = -1, mesh_seq = -1, prev_div = -1, prev_seq = -1;
    size_t mesh_verts = 0, prev_verts = 0;

    const static int MESH_DIV_EMPTY=-1;
    const static int MESH_DIV_ABORT=-2;
    const static int MESH_DIV_NEW_VARS=-3;
    const static int MESH_DIV_NEW_VARS_SMALL=-4;

    int default_div=MESH_DIV_EMPTY;
    int target_div=MESH_DIV_EMPTY;

    QString colorKeyBase() const;

    // Exact regions (see setExactRegions)
    std::vector<ExactRegion> exact_regions;
    std::vector<libfive::step::ExactSpec> exact_done;      // render thread: what
    std::vector<libfive::step::ExactPiece> exact_surfaces; // the surfaces are of
    std::vector<char> exact_have;                          // (made yet, only for parts a region reaches)
    std::vector<double> exact_clip_key;                    // ...and what the clipped pieces are of
    std::vector<libfive::step::ExactPiece> exact_pieces;   // (the part's surface cut by each region)

    // Own render (see setRenderHint)
    bool has_hint=false;
    QVector3D hint_lo, hint_hi;
    float hint_res=0, hint_side=0, hint_scene_res=0;
    // ...where the part is now (its handles move and turn it): what the
    // render in progress uses
    QVector3D run_hint_lo, run_hint_hi;
    float run_hint_side=0;
    void placeHint();

    Handles m_handles;


    // The free variables the shape uses (found on first use)
    mutable std::unordered_set<const void*> m_deps;
    mutable bool m_deps_known = false;
    void buildDeps() const;

    // Colouring by a field (see setColorField)
    bool has_color=false;
    libfive::Tree color_field = libfive::Tree::invalid();
    bool color_auto=true;
    float color_lo=0, color_hi=1;
    QString color_label;
    QString color_map;
    std::vector<float> color_values;                       // current channel at the vertices
    std::vector<std::unique_ptr<libfive::ArrayEvaluator>> color_evals;   // render thread (one per colouring thread)
    std::unique_ptr<libfive::ArrayEvaluator> probe_eval;   // GUI thread

    // Structural result (see setResult)
    bool has_result=false;
    std::vector<FieldChannel> m_channels;
    int m_channel=0;
    std::vector<std::vector<float>> channel_values;        // every channel at the vertices
    bool has_deform=false;
    libfive::Tree deform_tree[3] = {libfive::Tree::invalid(), libfive::Tree::invalid(),
                                    libfive::Tree::invalid()};
    float m_deform=0, m_deform_auto=1;
    std::vector<Eigen::Vector3f> disp;                     // displacement at the vertices
    std::vector<std::unique_ptr<libfive::ArrayEvaluator>> result_evals;   // render thread
    std::unique_ptr<libfive::ArrayEvaluator> probe_disp[3];              // GUI thread
    std::unique_ptr<libfive::ArrayEvaluator> probe_channel;              // GUI thread
    int probe_channel_index=-1;
    void applyChannel();

    // Elements
    ElementGrid m_grid;
    bool m_show_elements=false;
    bool m_sec_enabled=false, m_sec_whole=false;
    QVector4D m_sec_plane;
    bool elem_dirty=true;
    int elem_tris=0, elem_lines=0;
    std::vector<float> m_elem_lo, m_elem_hi;               // per channel: the range of the elements' values
    struct ElemTri { QVector3D p[3]; float value; };
    std::vector<ElemTri> elem_probe;                       // the triangles drawn, where drawn (for hovering)
    std::vector<Eigen::Vector3f> node_disp;                // displacement at grid nodes
    QOpenGLVertexArrayObject elem_vao, elem_line_vao;
    QOpenGLBuffer elem_vbo, elem_line_vbo;
    void buildElements();
    void buildMeshElements();
    void drawElements(const QMatrix4x4& M, bool monochrome, QColor color);
};

} // namespace FielDes
