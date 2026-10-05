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

#include <array>
#include <chrono>
#include <map>
#include <memory>
#include <set>
#include <unordered_set>
#include <cstdio>
#include <cstdlib>

#include "libfive/eval/evaluator.hpp"
#include "libfive/eval/eval_array.hpp"
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
    const libfive::Mesh* getMesh() const { return mesh.get(); }

    /*
     *  Looks up the tree's ID
     */
    /*  (of the shape as the script made it: a step of a result may show another geometry)  */
    libfive::Tree::Id id() const { return base_tree.id(); }

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
    /*
     *  The symbols of boundary conditions drawn over the model by the viewport (see boundary_conditions.py): arrows and
     *  pads at points of the surface, and the texts beside them.  `kind` is the category of the colour map "bc"
     *  (1 fixed support, 2 sliding support, 3 force, 4 gravity)
     */
    struct BcGlyph
    {
        int kind = 0;
        bool tip = false;           // the point is the arrow's tip (it pushes in), else its tail
        QVector3D pos, dir;
        float size = 1;
    };
    struct BcLabel
    {
        int kind = 0;
        QString text;
        QVector3D pos;
    };
    void setBoundarySymbols(std::vector<BcGlyph> glyphs, std::vector<BcLabel> labels);
    const std::vector<BcGlyph>& boundaryGlyphs() const { return bc_glyphs; }
    const std::vector<BcLabel>& boundaryLabels() const { return bc_labels; }

    bool hasColorField() const { return has_color; }

    /*  A patch of a surface (fieldes.stdlib.selection): the shape is drawn only where its colour field is at
     *  most `cutoff` -- the surface beyond is not drawn -- in the colour at the top of the colour map, and a hair
     *  towards the eye so that it wins over the part it lies on  */
    void setColorCutoff(float cutoff) { has_cutoff = true; color_cutoff = cutoff; }
    bool isSurfacePatch() const { return has_cutoff; }
    const libfive::Tree& colorFieldTree() const { return color_field; }
    QString colorLabel() const { return color_label; }
    QString colorMap() const { return color_map; }
    float colorLo() const { return color_lo; }
    float colorHi() const { return color_hi; }
    /*  Identifies the colouring (shapes are only reused when it matches)  */
    QString colorKey() const;

    /*  A part meshed on its own (an imported part, fieldes.stdlib.cad_import.
     *  roi_resolution): over a cube of `side` around its box, at its own
     *  resolution `res` -- set by its own features and size, so a very thin
     *  part is fine without the whole scene being fine, and a big simple one is
     *  not finer than it needs.  `res` was chosen for the scene resolution
     *  `scene_res`; when the scene's resolution is changed (studio.
     *  set_resolution) the part's changes with it, in proportion.  A part whose
     *  box is not inside the render region uses the scene's resolution. */
    void setRenderHint(QVector3D lo, QVector3D hi, float res, float side, float scene_res);

    /*  The render cache (fieldes.stdlib.render_cache.render_cache): when it is on, the finished mesh of
     *  the shape is kept on disk -- by what the shape is: its expression with the numbers it is drawn
     *  with, its colours, the region and resolution it is meshed at -- and read from
     *  there when the same shape is rendered again: in another run of the program, or after the script
     *  was changed and changed back.  Whatever changes about the math is another key: the shape is
     *  meshed again, shown, and kept again.  A shape that depends on something no other run can
     *  recognise (a solved analysis) is not kept.  */
    void setRenderCache(bool on, bool forced = false)
    {
        m_cache_on.store(on);
        m_cache_forced.store(forced);
    }
    bool renderCache() const { return m_cache_on.load(); }
    /*  What the cache did for the last render, in words ("" when it is off)  */
    QString renderCacheState() const;
    /*  ...and in one word: "" (off), "wait" (on, not meshed yet), "kept", "read", "no" (cannot be kept)  */
    QString renderCacheKind() const;
    /*  Where the meshes are kept (FIELDES_RENDER_CACHE_DIR, else the program's cache folder)  */
    static QString renderCacheDir();
    /*  Deletes every kept mesh; the number of files deleted  */
    static int clearRenderCache();

    /*  Handles (fieldes.stdlib.handles): a placed part is the shape scaled about
     *  `about`, rotated by three angles (degrees; x, then y, then z) and moved,
     *  by numbers that are var()s of the script.  The gizmo draws move arrows,
     *  rotation rings and scale knobs at its pivot and dragging them changes those
     *  vars (and so the script text); the mode says when it is shown: CLICK (the
     *  default) while the shape is selected, NEVER, ALWAYS.  Dragging the shape's own
     *  surfaces (Studio's own handles, see nativeDragOk) is always possible, whatever
     *  the mode; the gizmo has priority where it is shown.  A locked shape
     *  (fieldes.stdlib.handles.lock, a switch of its own that keeps the mode) lets
     *  neither be dragged.  An id is null where the number is a plain constant.  */
    struct Handles
    {
        enum Mode { CLICK, NEVER, ALWAYS };
        bool present = false;
        Mode mode = CLICK;
        QVector3D about;
        libfive::Tree::Id move[3] = {nullptr, nullptr, nullptr};
        libfive::Tree::Id rotate[3] = {nullptr, nullptr, nullptr};
        libfive::Tree::Id scale[3] = {nullptr, nullptr, nullptr};
    };
    void setHandles(const Handles& h) { m_handles = h; }
    const Handles& handles() const { return m_handles; }
    /*  A locked shape (`x = lock(x)` in the script) cannot be dragged at all  */
    void setLocked(bool l) { m_locked = l; }
    bool isLocked() const { return m_locked; }
    /*  The gizmo is shown now: with at least one number to drag, not locked, and its mode says so (ALWAYS, or CLICK while
     *  the shape is selected)  */
    bool hasHandles() const;
    /*  Has a number that moves it (`handles(x, move=(var, ...))`), in whatever mode, and is not locked: the shared
     *  gizmo of a multi-selection can move it, whatever its own gizmo mode is  */
    bool hasMoveVars() const;
    /*  The numbers now (constants and unknown ones as 0; a scale as 1)  */
    QVector3D handleMove() const;
    QVector3D handleRotate() const;
    QVector3D handleScale() const;
    /*
     *  Studio's own handles: hover a surface and drag it, which changes the numbers (the
     *  script's var()s, or those expose() made) that place that surface.  Available when
     *  the shape has such numbers and is not locked, whatever the gizmo's mode.
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
    /*  How finely a result's colours are read: the render's triangles are split until no edge is longer
     *  than this (mm, the solver's element; 0: as meshed)  */
    void setColorDetail(float d) { color_detail = d; }
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
    bool hasElements() const;
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
     *  The steps of a result (fieldes: _color_steps): the slider of the result card walks through them,
     *  play runs them in turn.  A step brings its own fields (the same names, other values: the flow at
     *  another time, a fraction of the load), its own deformation, its own streamlines, or its own
     *  geometry (the part after an iteration of an optimisation: meshed when the step is first shown,
     *  then kept) -- what it does not bring is the result's own.
     */
    using FlowLine = std::vector<std::array<float, 5>>;     // x y z speed time, along the line
    struct Step
    {
        QString label;
        std::vector<FieldChannel> channels;                 // empty: the result's own
        bool hasDeform = false;
        libfive::Tree deform[3] = {libfive::Tree::invalid(), libfive::Tree::invalid(), libfive::Tree::invalid()};
        bool hasLines = false;
        std::vector<FlowLine> lines;
        libfive::Tree tree = libfive::Tree::invalid();      // valid: the step's own geometry
    };
    void setSteps(std::vector<Step> steps, int current);
    int stepCount() const { return int(m_steps.size()); }
    int step() const { return m_step; }
    QString stepLabel() const;
    void setStep(int k);

    /*
     *  Streamlines through a flow (fieldes.stdlib.fluid): coloured by the speed over [lo, hi] (the speed
     *  field's colour range), with particles moving along them.  The viewport draws them over every shape
     *  (drawFlowLines, with the depth test off) once the shapes are drawn, so they are seen through the
     *  fluid, which is drawn like any other result.
     */
    void setFlowLines(std::vector<FlowLine> lines, float lo, float hi);
    bool hasFlowLines() const;
    bool showFlowLines() const { return m_show_lines; }
    void setShowFlowLines(bool b);
    bool showsFlowLines() const { return hasFlowLines() && m_show_lines; }
    void drawFlowLines(const QMatrix4x4& M);
    /*  Moves the particles on by dt seconds of wall-clock time  */
    void advanceFlow(float dt);

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
     *  deformation or its handles use it.  (Every shape is
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
    /*  The render cache read or kept the mesh (or could not)  */
    void cacheStateChanged();

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

    libfive::Tree tree;                 // (what is meshed: the shown step's geometry, usually base_tree)
    libfive::Tree base_tree = libfive::Tree::invalid();     // the shape as the script made it (optimized)
    // (the tree as the script built it: the render cache's key is made of it -- the optimizer's output differs
    // from run to run in the order of operands)
    libfive::Tree built_tree;
    std::map<libfive::Tree::Id, float> vars;
    std::vector<libfive::Evaluator,
                Eigen::aligned_allocator<libfive::Evaluator>> es;

    std::shared_ptr<libfive::Mesh> mesh;
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
    // After meshing, a level colours its vertices
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
    const static int MESH_DIV_NEW_TREE=-5;      // the shown step's geometry changed: the evaluators are rebuilt

    int default_div=MESH_DIV_EMPTY;
    int target_div=MESH_DIV_EMPTY;

    QString colorKeyBase() const;

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
    bool m_locked = false;

    // The render cache (see setRenderCache)
    enum CacheState { CACHE_OFF = 0, CACHE_KEPT, CACHE_READ, CACHE_NO_KEY, CACHE_RESULT, CACHE_FAILED };
    // On unless the script says render_cache(x, False).  A shape the script does not mention is kept only if
    // meshing it took a while (a sphere is not worth a file); one under an explicit render_cache(x) always is.
    std::atomic<bool> m_cache_on{true};
    std::atomic<bool> m_cache_forced{false};
    std::atomic<int> cache_state{CACHE_OFF};
    std::atomic<bool> cache_read{false};        // the level just rendered was read from the cache (it is the finished one)
    bool next_follows = false;                  // `next` is the next level of this render, not a newer request
    // The numbers the evaluators hold: a snapshot of `vars` made where they are given them (the keys are of those)
    std::map<libfive::Tree::Id, float> cache_vars;
    unsigned long cache_vars_gen = 1, cache_tree_gen = 0;
    std::string cache_tree_key;                 // (of the expression and the colours: made once per snapshot)
    std::string cache_miss_key;                 // the last key that was looked for and not found
    std::string renderCacheTreeKey() const;
    std::string renderCacheKey(const RenderSettings& s, const libfive::Region<3>& region, double res);
    bool readRenderCache(const std::string& key, BoundedMesh& out) const;
    bool writeRenderCache(const std::string& key, const BoundedMesh& out) const;
    /*  The finished mesh is not in the cache yet: mesh again, at the finest level, to keep it  */
    bool keepCurrent();

    // The free variables the shape uses (found on first use)
    mutable std::unordered_set<const void*> m_deps;
    mutable bool m_deps_known = false;
    void buildDeps() const;

    std::vector<BcGlyph> bc_glyphs;
    std::vector<BcLabel> bc_labels;
    QString bc_key;                                        // identifies them (shapes are only reused when it matches)

    // Colouring by a field (see setColorField)
    bool has_color=false;
    bool has_cutoff=false;                                 // (a patch of a surface: see setColorCutoff)
    float color_cutoff=0;
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
    float color_detail = 0;                                // (see setColorDetail)
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

    // Steps (see setSteps).  The result's own fields and deformation are kept apart from the shown
    // step's (m_channels, deform_tree, has_deform are the shown step's); the render thread works on
    // a copy taken when the render starts.
    std::vector<Step> m_steps;
    int m_step = -1;
    const Step* currentStep() const
    { return (m_step >= 0 && size_t(m_step) < m_steps.size()) ? &m_steps[size_t(m_step)] : nullptr; }
    int geometryKey() const
    { const Step* st = currentStep(); return (st && st->tree.is_valid()) ? m_step : -1; }
    std::vector<FieldChannel> base_channels;
    libfive::Tree base_deform[3] = {libfive::Tree::invalid(), libfive::Tree::invalid(), libfive::Tree::invalid()};
    bool base_has_deform = false;
    std::vector<FieldChannel> run_channels;                // what the render in progress evaluates
    libfive::Tree run_deform[3] = {libfive::Tree::invalid(), libfive::Tree::invalid(), libfive::Tree::invalid()};
    bool run_has_deform = false;
    float run_color_detail = 0;
    QString run_key;
    int step_rendering = -1;                               // the geometry step the render in progress is of (-1: the result's own)
    bool es_stale = false;                                 // the evaluators are of another geometry than `tree`
    struct StepMesh
    {
        std::shared_ptr<libfive::Mesh> mesh;
        libfive::Region<3> region;
        std::vector<std::vector<float>> channels;
        std::vector<Eigen::Vector3f> disp;
    };
    std::map<int, StepMesh> step_meshes;                   // geometry steps meshed so far (their finished mesh)
    std::map<std::pair<int, int>, std::vector<float>> step_values;   // (step, channel): a step's own field at the vertices
    std::map<int, std::vector<Eigen::Vector3f>> step_disp;            // step: a step's own displacement at the vertices
    void applyStepState();
    void rebuildEvaluators();
    void stashStepMesh();
    const std::vector<Eigen::Vector3f>& dispNow();
    const std::vector<FlowLine>* currentLines() const;

    // Flow lines (see setFlowLines)
    std::vector<FlowLine> flow_lines;
    float flow_lo = 0, flow_hi = 1;
    bool m_show_lines = true;
    float flow_time = 0;                                   // where the particles are (seconds of flow)
    float flow_period = 0;                                 // the longest line's time
    bool lines_dirty = true;
    QOpenGLVertexArrayObject line_vao, streak_vao;
    QOpenGLBuffer line_vbo, streak_vbo;
    int line_verts = 0;
    void buildFlowLines();
};

} // namespace FielDes
