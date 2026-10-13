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
#include <QStringList>
#include <QtConcurrent>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QMatrix4x4>
#include <QVector3D>
#include <QVector4D>

#include <array>
#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <unordered_set>
#include <cstdio>
#include <cstdlib>

#include "libfive/eval/evaluator.hpp"
#include "libfive/eval/eval_array.hpp"
#include "libfive/tree/tree.hpp"

#include "libfive/render/brep/mesh.hpp"
#include "libfive/fields/point_shapes.hpp"
#include "libfive/render/brep/progress.hpp"
#include "libfive/render/brep/region.hpp"
#include "libfive/render/brep/settings.hpp"

#include "fieldes/settings.hpp"
#include "fieldes/symmetry.hpp"

namespace libfive { class Tape; /*  forward declaration */ }
namespace libfive { namespace fea { class TetProblem; } }

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
    libfive::Tree::Id id() const { return m_field_ready.load() ? base_tree.id() : built_tree.id(); }

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
     *  A point is not painted at all: the gizmo is what shows where it is.  It is still a shape (it has its place, its numbers and its
     *  gizmo), but nothing of it is drawn or picked, and the camera never frames it
     */
    void setNoPaint(bool on) { no_paint = on; }
    bool noPaint() const { return no_paint; }

    /*
     *  A 2D shape (fieldes.stdlib.points.displayed) is FLAT: its field does not depend on z, and what is drawn is its face in the plane
     *  z = 0 -- a sheet with no thickness (libfive::Mesh::renderFlat), the same as a surface with no curvature.  There is no surface to
     *  pull inside the face: a drag that starts on it pulls its nearest edge (flatEdgeFrom)
     */
    void setFlat(bool on) { m_flat = on; }
    bool isFlat() const { return m_flat; }

    /*  The point of the outline of a flat shape that is nearest to `p` (in its plane): `p` is moved onto it.  False when there is none  */
    bool flatEdgeFrom(QVector3D& p) const;

    /*
     *  The plane a flat shape lies in when it is not z = 0 (fieldes.stdlib.sketch: the plane through the first three points): the point and the
     *  two unit vectors that span it, nine expressions of the script's numbers -- they move with the points that make the plane
     */
    void setFlatFrame(std::vector<libfive::Tree> trees);
    bool hasFlatFrame() const { return m_frame_trees.size() == 9; }

    /*
     *  The outline of the face of a flat shape, and the line of a curve: polylines in space (closed ones end where they begin)
     */
    const std::vector<std::vector<Eigen::Vector3f>>& outlines() const { return m_outlines; }

    /*
     *  The points of a sketch or a curve (fieldes.stdlib.sketch, curves): each three expressions of the script's numbers for where it is, three for its
     *  rotation, and what it says of itself.  The line or the face is made from their numbers natively, at every render -- and so while a point is dragged --
     *  by libfive::pointshapes: no script is run, and no field is needed.  A face is painted; a line is not
     */
    struct ControlPoint
    {
        libfive::Tree pos[3] = {libfive::Tree::invalid(), libfive::Tree::invalid(), libfive::Tree::invalid()};
        libfive::Tree rot[3] = {libfive::Tree::invalid(), libfive::Tree::invalid(), libfive::Tree::invalid()};
        bool hasRot = false;
        libfive::Tree scale = libfive::Tree::invalid();                 // how strongly a spline leaves it, and how thick a sweep is there (one number: see curves.Control)
        bool hasScale = false;
        int flag = -1;                                                  // -1: what the shape says, 0: a corner, 1: a spline point
        libfive::Tree::Id var[3] = {nullptr, nullptr, nullptr};         // the number that moves it along each axis (its gizmo's), when it has one
    };
    void setControls(std::vector<ControlPoint> points, bool smoothDefault, bool closed, bool face);
    /*
     *  A straight extrusion of a sketch of points (`extrude(sketch, a, b)`) is a PRISM: it is drawn from the numbers of the points and of a and b at every render -- as the face
     *  of a sketch is -- so that it follows a point while the point is dragged, with no script run and no field meshed.  `guard` are numbers of the script (a gizmo's, a pulled
     *  face's) that must still have the values they were made with: with another value the shape is something else than the prism, and its field is meshed as ever
     */
    void setPrism(std::vector<ControlPoint> points, bool smoothDefault, libfive::Tree a, libfive::Tree b, std::vector<std::pair<libfive::Tree::Id, float>> guard);
    bool hasPrism() const { return !m_prism_points.empty(); }
    /*
     *  A SWEEP -- `path_extrude(sketch, curve)` of a sketch of points along a curve of points -- is drawn the same way: ring by ring, from the numbers the points of the sketch and
     *  of the path have now (the thickness of a path point, its rotation, the twist), placed by the numbers of the shape's gizmo.  Nothing is meshed, so a point that is dragged is
     *  followed at once, and a sweep that took minutes to mesh takes a few milliseconds
     */
    void setSweep(std::vector<ControlPoint> profile, bool profileDefault, std::vector<ControlPoint> path, bool pathDefault, bool closed, libfive::Tree twist,
                  bool placed, const double about[3], const libfive::Tree move[3], const libfive::Tree rotate[3], const libfive::Tree scale[3]);
    /*  The profile of the sweep is the outline of a primitive (a circle, a rectangle, ...) rather than a sketch: its corners are in the plane z = 0, its axes x and y (no plane is
     *  worked out of them).  In every other way it is a sketch to the sweep: the sweep starts at it  */
    void setSweepFree(bool free) { m_sweep_free = free; }
    /*  Numbers of the script that must still be what they were for the sweep to be drawn from its outline (a gizmo's turn about x or y of the profile, its move along z: a profile
     *  that leaves the plane z = 0 is not the outline): (a var, its value)  */
    void setSweepGuard(std::vector<std::pair<libfive::Tree::Id, float>> guard) { m_sweep_guard = std::move(guard); }
    bool hasSweep() const { return !m_sweep_path.empty() && !m_sweep_profile.empty(); }
    /*  The one number (var) an expression of the script's numbers is made of, or null when it has none or several: what moves a point along an axis  */
    static libfive::Tree::Id soleVarOf(const libfive::Tree& expression);
    bool hasControls() const { return !m_controls.empty(); }
    bool isCurve() const { return hasControls() && !m_controls_face; }
    bool isFace() const { return hasControls() && m_controls_face; }
    /*  A 2D shape (a flat one, or a sketch): drawn over the 3D ones, whatever is in front of it, in a shade of its own, and picked before them  */
    bool is2D() const { return m_flat || isFace(); }
    const std::vector<ControlPoint>& controlPoints() const { return m_controls; }
    bool smoothDefault() const { return m_controls_default; }
    bool closedLine() const { return m_controls_closed; }
    /*  Their numbers now (all of them, or false when one cannot be read)  */
    bool controlsNow(std::vector<libfive::pointshapes::Control>& out) const;
    /*  The plane of a face now (origin, u, v), and the direction the line leaves control i in  */
    bool planeNow(double frame[9]) const;
    /*  The plane a 2D shape lies in -- (origin, u, v), nine numbers -- as it is now: a sketch's, a flat shape's own, z = 0 for the others; false when it is not a 2D shape  */
    bool flatPlane(double frame[9]) const;
    bool tangentAt(int i, QVector3D& out) const;
    /*  Which control of this shape a point is (by the numbers that move it), or -1  */
    int controlOf(const Shape* point) const;
    /*  The control whose three coordinates are plain numbers and are where `p` is, or -1: how a point that has no gizmo (no numbers of its own) is found  */
    int controlAt(const QVector3D& p) const;
    /*  The control the piece of the line numbered `piece` (as outlines() lists them) is after  */
    int controlAfterPiece(int piece) const;
    /*  A point being pulled out of the line, not in the script yet: drawn as a control after `after`, at `pos`, until the script has it  */
    void setVirtualPoint(bool on, int after = 0, QVector3D pos = QVector3D());
    bool hasVirtualPoint() const { return m_virtual_on; }
    /*  Render again with the numbers it has now  */
    void refresh();

    /*  A shape whose outline is a place to press (a sketch, a curve): a press on it pulls a point out of it, one that is drawn blue  */
    void setEditable(bool on) { m_editable = on; }
    bool isEditable() const { return m_editable; }

    /*  How the dot of a point is drawn: 0 as any point, 1 a corner of a sketch or a curve (a square), 2 a spline point (a circle with a ring)  */
    void setPointStyle(int s) { m_point_style = s; }
    int pointStyle() const { return m_point_style; }
    bool isHovered() const { return hover; }

    /*
     *  A point (fieldes.stdlib.points.Point) is drawn as a dot: the middle point of a gizmo, wherever it is and whatever its gizmo's mode.
     *  Click the dot and the point is selected (its full gizmo shows, as that of any model).  `pos` is where it is when the library could tell
     *  (`hasPos`); once it has a gizmo, the gizmo's pivot is where it is, and follows a drag
     */
    void setPoint(QVector3D pos, bool hasPos) { m_isPoint = true; m_pointPos = pos; m_hasPointPos = hasPos; }
    bool isPoint() const { return m_isPoint; }
    bool dotPosition(QVector3D& p) const;

    /*
     *  A name given by the app (fieldes.app.viewport): the viewport of the app window draws it over the model, as a card in the style of the
     *  labels of boundary conditions.  `shown`: the model is one the app's viewport shows (the others the script displays are not shown there)
     */
    void setAppLabel(const QString& name, bool shown) { m_appLabel = name; m_appShown = shown; }
    const QString& appLabel() const { return m_appLabel; }
    bool appShown() const { return m_appShown; }

    /*
     *  Points that are drawn as the dot in the middle of a gizmo, with none of its handles (fieldes.stdlib.lattices.PointSet): a set of
     *  points is data, not something to drag.  The shape that carries them is not painted (setNoPaint) and nothing of it is picked; two
     *  sets are two shapes (the points are part of colorKey)
     */
    void setMarkers(std::vector<QVector3D> points);
    const std::vector<QVector3D>& markers() const { return m_markers; }

    /*
     *  How opaque a result is drawn (1: opaque): a flow is drawn translucent when the body in it is to be seen through it
     */
    void setOpacity(float a) { opacity = std::max(0.05f, std::min(1.0f, a)); }
    float opacityValue() const { return opacity; }

    /*
     *  Bounds of the current mesh (empty region if not meshed yet)
     */
    const libfive::Region<3>& getMeshBounds() const { return mesh_bounds; }
    bool hasMesh() const { return mesh && gl_ready; }
    /*  The box of the mesh as a drag shows it -- moved, turned and scaled by the numbers of the gizmo that are being dragged -- false when no such picture is shown  */
    bool previewBox(QVector3D& lo, QVector3D& hi) const;
    /*  The same box for a shape that is not drawn moved but meshed again as it is dragged (the section view cuts, a colour, a result): where its mesh would be at the numbers of the
     *  gizmo it has now -- false when the mesh is of these numbers  */
    bool movedBox(QVector3D& lo, QVector3D& hi) const;
    /*  Where the shape would be with some of its numbers other (the ones of a drag that has not been given to it yet), searched for over the box -- a search of the field, a few
     *  milliseconds at the most; false when the shape cannot be searched (a face or a line of points, a sweep, a very large tree, a result)  */
    bool extentWith(const std::map<libfive::Tree::Id, float>& vs, const QVector3D& search_lo, const QVector3D& search_hi, QVector3D& lo, QVector3D& hi);

    /*  Read-only access for evaluating the field (slice viewer)  */
    const libfive::Tree& getTree() const { ensureField(); return tree; }

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
    /*  Only the places where the colour field is at least `floor` are drawn (the places of boundary conditions: category 1 and up; the
     *  rest of the body is not), in the colours of the map  */
    void setColorFloor(float floor) { has_floor = true; color_floor = floor; }
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
     *  box is not inside the render region uses the scene's resolution.  A `scene_res` below 0 says the resolution is the
     *  part's own, absolute (custom_resolution(x, r)): it does not follow the scene's.  */
    void setRenderHint(QVector3D lo, QVector3D hi, float res, float side, float scene_res);

    /*  A part that is the exact distance to triangles of its own (a tessellated STEP part: fieldes tessellate()) is drawn from
     *  those triangles, not meshed from its field: a wall as thin as you like is there at any resolution, and nothing is
     *  meshed.  `mesh` holds the triangles in the part's own coordinates (one is shared by every run of the script and every
     *  copy of the part: `key` says which it is), `matrix` (row-major 3 x 4) puts them where the script has the part.  The
     *  numbers (var()s) the part used when the script ran are part of what it is: a drag of one of them moves the part, and
     *  until the script has run again its field is meshed instead of the triangles.  The field stays the truth: colours,
     *  sections, probes and every operation on the part use it.  */
    struct ExactMesh
    {
        std::string key;
        std::vector<float> verts;               // x, y, z of each vertex
        std::vector<uint32_t> tris;             // three vertex numbers each
    };
    void setExactMesh(std::shared_ptr<const ExactMesh> mesh, const double* matrix);
    /*  The shape is drawn from these triangles, placed as they are (a picture of an optimisation: its density surface), not meshed from its
     *  field -- which stays the truth for the colours  */
    void setSurfaceMesh(std::shared_ptr<const ExactMesh> mesh) { m_step_exact = std::move(mesh); }
    /*  Whether a render with these settings would mesh something other than the one that is going or was done last (see
     *  RenderGeometry): the viewport restarts only the shapes for which it would  */
    bool wouldRenderDifferently(const Settings& s) const;

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

    /*  Gizmo (fieldes.stdlib.handles): a placed part is the shape scaled about
     *  `about`, rotated by three angles (degrees; x, then y, then z) and moved,
     *  by numbers that are var()s of the script.  The gizmo draws move arrows,
     *  rotation rings and scale knobs at its pivot and dragging them changes those
     *  vars (and so the script text); the mode says when it is shown: CLICK (the
     *  default) while the shape is selected, NEVER, ALWAYS.  Dragging the shape's own
     *  surfaces (Studio's own handles, see nativeDragOk) is always possible, whatever
     *  the mode; the gizmo has priority where it is shown.  A locked shape
     *  (fieldes.stdlib.handles.lock, a switch of its own that keeps the mode) lets
     *  neither be dragged.  An id is null where the number is a plain constant.  */
    struct Gizmo
    {
        enum Mode { CLICK, NEVER, ALWAYS };
        bool present = false;
        Mode mode = CLICK;
        QVector3D about;
        libfive::Tree::Id move[3] = {nullptr, nullptr, nullptr};
        libfive::Tree::Id rotate[3] = {nullptr, nullptr, nullptr};
        libfive::Tree::Id scale[3] = {nullptr, nullptr, nullptr};
        // The gizmo of the image of a model (fieldes.stdlib.symmetry.symmetry_image): its numbers are the original's, and `map` takes a place of the original to the
        // image's: the gizmo is drawn there, and what is dragged moves the original by what the image's move is when it is taken back
        bool mapped = false;
        QMatrix4x4 map;
    };
    void setGizmo(const Gizmo& h) { m_gizmo = h; }
    /*  A symmetry model (fieldes.stdlib.symmetries.symmetry_planes): the arrangement of its planes and where it was made, in its own place -- before its gizmo moved and turned it.
        The planes as they are now are `symmetryNow`'s: the definition, moved and turned by the numbers of the gizmo  */
    struct SymmetryDef
    {
        bool present = false;
        int kind = 0;                       // Symmetry::Kind
        int count = 1;
        QVector3D centre{0, 0, 0}, axis{0, 0, 1}, first{0, 1, 0};
    };
    void setSymmetryDef(const SymmetryDef& d) { m_symDef = d; }
    const SymmetryDef& symmetryDef() const { return m_symDef; }
    bool isSymmetry() const { return m_symDef.present; }
    bool symmetryNow(Symmetry& out) const;
    const Gizmo& gizmo() const { return m_gizmo; }
    /*  A locked shape (`x = lock(x)` in the script) cannot be dragged at all  */
    void setLocked(bool l) { m_locked = l; }
    bool isLocked() const { return m_locked; }
    /*  A linked copy of another shape (`instance(shape)`): it is that shape, so its own surfaces are not dragged (they are the original's numbers) -- only its gizmo
        moves and turns it  */
    void setLinked(bool l) { m_linked = l; }
    bool isLinked() const { return m_linked; }
    /*  The gizmo is shown now: with at least one number to drag, not locked, and its mode says so (ALWAYS, or CLICK while
     *  the shape is selected)  */
    bool hasGizmo() const;
    /*  Has a number that moves it (`gizmo(x, move=(var, ...))`), in whatever mode, and is not locked: the shared
     *  gizmo of a multi-selection can move it, whatever its own gizmo mode is  */
    bool hasMoveVars() const;
    /*  The numbers now (constants and unknown ones as 0; a scale as 1)  */
    QVector3D gizmoMove() const;
    QVector3D gizmoRotate() const;
    QVector3D gizmoScale() const;
    /*
     *  Studio's own handles: hover a surface and drag it, which changes the numbers (the
     *  script's var()s, or those handles() made) that place that surface.  Available when
     *  the shape has such numbers and is not locked, whatever the gizmo's mode.
     */
    bool nativeDragOk() const;
    /*  The direction the curve this shape is made of faces at its point number `index` (the line through its vertices there, from the numbers the points have now); false for
        what is not a curve of three or more... of two or more points  */
    bool curveTangentAt(int index, QVector3D& t) const;
    /*  The field of the shape at several sets of places (as its numbers are now): its distance there, negative inside.  False when it cannot be read (not meshed yet, a result, an
        imported part, or one with so many parts that reading it would be slow)  */
    bool sampleSets(const std::vector<std::vector<QVector3D>>& sets, std::vector<std::vector<float>>& out) const;
    /*  The box of the vertices the triangles of the mesh use (false: not meshed)  */
    bool meshBox(QVector3D& lo, QVector3D& hi) const;
    /*  The variables of the gizmo: dragging a surface leaves them where they are  */
    std::set<libfive::Tree::Id> gizmoVars() const;
    /*  How much each number of the shape resists a drag of one of its surfaces (see libfive::dragWeights): the centre of a round feature stays  */
    std::map<libfive::Tree::Id, float> dragWeights() const;
    /*  Where the part turns after the move: the pivot the gizmo sits at  */
    QVector3D gizmoPivot() const { const QVector3D p = m_gizmo.about + gizmoMove(); return m_gizmo.present && m_gizmo.mapped ? m_gizmo.map.map(p) : p; }
    /*  The gizmo is an image's (the numbers of the original); the turns and mirror images of the way it was made (without the shift), whether it turns the way round (+1, -1),
        and the part's own axes as the image has them (the original's, taken through the symmetry)  */
    bool gizmoMapped() const { return m_gizmo.present && m_gizmo.mapped; }
    QMatrix4x4 gizmoMapLinear() const;
    float gizmoMapSign() const;
    QMatrix4x4 gizmoOrientation() const;
    /*  The rotation the angles make (the library's rotate_x/y/z, x first)  */
    static QMatrix4x4 gizmoRotation(const QVector3D& degrees);

    /*
     *  A DRAG OF THE GIZMO does not change what a shape IS, only where it is: its gizmo's numbers (move, rotate, scale) place the same mesh.  While they are
     *  dragged the mesh that is drawn already is drawn again with the matrix that takes it from where it was made to where the numbers say now, and nothing is
     *  meshed again; the real render is done once, when the drag ends (endGizmoPreview).  `changed` are the numbers dragged: when they are all this shape's own
     *  gizmo numbers, and it is a plain mesh (no colours, no result, nothing cut by the section), this takes them and returns true -- the caller does not
     *  update the shape.  False: the shape is another shape for these numbers (it is made of something that moves, or its surfaces are dragged) and renders again
     */
    bool previewGizmoVars(const std::map<libfive::Tree::Id, float>& changed, bool allowed);
    /*  The drag is over: the mesh is made again at the numbers the shape has now (it goes on being shown moved until that mesh is there)  */
    void endGizmoPreview();
    /*  The matrix of a gizmo's numbers: scaled about its centre, turned there (x, then y, then z), moved  */
    QMatrix4x4 gizmoMatrix(const QVector3D& move, const QVector3D& rotate, const QVector3D& scale) const;

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
    /*  The key of its exact distance in the Python side's registry (empty: it has none, its own field is the distance): see FieldSource  */
    void setExactKey(const QString& key) { m_exactKey = key; }
    const QString& exactKey() const { return m_exactKey; }
    /*  The key numbers of a result -- label, text, unit of each (fieldes.stdlib.exporting.key_results): the result card prints them  */
    void setKeyResults(const QList<QStringList>& rows) { m_keyResults = rows; }
    const QList<QStringList>& keyResults() const { return m_keyResults; }
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
    /*  The steps of an optimisation of tetrahedra (topology_optimization) bring their geometry as triangles too: the surface of the
     *  design after each iteration, made from the optimisation's own densities in a few tens of milliseconds, so that stepping through the
     *  iterations meshes no field.  A step's triangles are made the first time they are wanted -- or ahead of that, by a thread that goes
     *  through them all (setSteps) -- and kept.  The surface holds the optimisation: it lives as long as the steps are drawn  */
    struct StepSurface
    {
        StepSurface(std::shared_ptr<libfive::fea::TetProblem> problem, double level, int count);
        ~StepSurface();
        std::shared_ptr<const ExactMesh> mesh(int k);       // (from any thread; null when the step has no surface to draw)
        void prepareAll();                                  // all that are not made yet, as far as memory allows
        std::shared_ptr<libfive::fea::TetProblem> problem;
        double level;
        int count;
    private:
        std::mutex m_mutex;
        std::vector<std::shared_ptr<const ExactMesh>> m_made;
        size_t m_bytes = 0;
    };
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
        std::shared_ptr<StepSurface> surface;               // set: the step is drawn from the triangles surface->mesh(surfaceStep)
        int surfaceStep = 0;
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
     *  deformation or its gizmo use it.  (Every shape is
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
     *  The field of the shape -- its tree, optimised, and the evaluators that mesh it -- is made when something reads it, not when the script makes the shape.  A sweep or
     *  a prism that is drawn from the numbers of its points never needs it, and the field of a sweep (a piece for every piece of the path, each holding the whole profile)
     *  is seconds to minutes of work to make, to key and to search for nothing.  settleField() is what the thread that makes a shape calls when it is made: the shapes that
     *  are not drawn from numbers have their field made there, where it always was.  Whatever reads the field calls ensureField() first (it does nothing when it is made)
     */
    void ensureField() const;
    void settleField();
    /*  Whether the shape is drawn, now, from the numbers of its points (a prism, a sweep): its field is not read to draw it  */
    bool drawnFromNumbers() const;

    /*
     *  Returns a unique ID using the given deduplication map
     */
    libfive::Tree::Id getUniqueId(
        std::unordered_map<libfive::TreeDataKey, libfive::Tree>& canonical);

    /*  What tells this shape from the others of a run, next to its tree: its colours, and -- for a line or a face of points, which is shown with a tree
     *  that says nothing of them -- the points: how many, what each says of itself (corner or spline point) and the numbers its place, rotation and scale are
     *  made of (their VALUES are not part of it: a drag changes those, and the shape goes on).  A sketch whose point was added, or toggled, is another
     *  shape: it is made again from its points, not kept with the points it had.  */
    QString identityKey(std::unordered_map<libfive::TreeDataKey, libfive::Tree>& canonical);

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
        std::vector<std::vector<Eigen::Vector3f>> outlines;     // the outline of a flat face, the line of a curve
    };

    void startRender(RenderSettings s);
    BoundedMesh renderMesh(RenderSettings s);
    static std::unique_ptr<libfive::Mesh> prismMesh(const std::vector<libfive::pointshapes::Control>& cs, bool smoothDefault, double a, double b);

    bool grabbed=false;
    bool hover=false;
    bool selected=false;
    bool no_paint=false;
    bool m_flat=false;
    std::map<libfive::Tree::Id, float> run_flat_vars;     // (the numbers a flat face is meshed with: those of the moment the render started)
    std::vector<libfive::Tree> m_frame_trees;             // (the plane of a flat shape: see setFlatFrame)
    double m_frame_now[9] = {0, 0, 0, 1, 0, 0, 0, 1, 0};  // (what they are now: updated when a render starts)
    bool m_frame_now_valid = false;
    double run_frame[9] = {0, 0, 0, 1, 0, 0, 0, 1, 0};    // (what the render in the thread uses)
    bool run_has_frame = false;
    bool m_editable = false;
    int m_point_style = 0;
    std::vector<ControlPoint> m_controls;
    bool m_controls_default = false, m_controls_closed = false, m_controls_face = false;
    bool m_virtual_on = false, m_run_virtual_on = false;
    int m_virtual_after = 0, m_run_virtual_after = 0;
    QVector3D m_virtual_pos, m_run_virtual_pos;
    bool controlNumbers(const std::map<libfive::Tree::Id, float>& v, std::vector<libfive::pointshapes::Control>& out) const;
    static bool controlNumbersOf(const std::vector<ControlPoint>& points, bool face, const std::map<libfive::Tree::Id, float>& v, std::vector<libfive::pointshapes::Control>& out,
                                 const Shape* owner);
    struct SweepNumbers;
    std::vector<ControlPoint> m_sweep_profile, m_sweep_path;
    bool m_sweep_profile_default = false, m_sweep_path_default = false, m_sweep_closed = false, m_sweep_placed = false, m_sweep_free = false;
    std::vector<std::pair<libfive::Tree::Id, float>> m_sweep_guard;
    libfive::Tree m_sweep_twist = libfive::Tree::invalid();
    double m_sweep_about[3] = {0, 0, 0};
    libfive::Tree m_sweep_move[3] = {libfive::Tree::invalid(), libfive::Tree::invalid(), libfive::Tree::invalid()};
    libfive::Tree m_sweep_rotate[3] = {libfive::Tree::invalid(), libfive::Tree::invalid(), libfive::Tree::invalid()};
    libfive::Tree m_sweep_scale[3] = {libfive::Tree::invalid(), libfive::Tree::invalid(), libfive::Tree::invalid()};
    bool sweepNow(const std::map<libfive::Tree::Id, float>& v, SweepNumbers& out) const;
    static std::unique_ptr<libfive::Mesh> sweepMesh(const SweepNumbers& in);
    std::vector<ControlPoint> m_prism_points;
    bool m_prism_default = false;
    libfive::Tree m_prism_a = libfive::Tree::invalid(), m_prism_b = libfive::Tree::invalid();
    std::vector<std::pair<libfive::Tree::Id, float>> m_prism_guard;
    /*  The prism's numbers now -- its points, and the distances a and b -- when it is drawn as one (its guard holds) and everything can be read; false when it is the field's to mesh  */
    bool prismNow(const std::map<libfive::Tree::Id, float>& v, std::vector<libfive::pointshapes::Control>& cs, double& a, double& b) const;
    std::vector<std::vector<Eigen::Vector3f>> m_outlines;
    double evalConstant(const libfive::Tree& t, const std::map<libfive::Tree::Id, float>& v) const;
    void updateFrameNumbers();
    float opacity=1.0f;
    int source_line=-1;

    QFuture<BoundedMesh> mesh_future;
    QFutureWatcher<BoundedMesh> mesh_watcher;
    libfive::BRepSettings mesh_settings;
    /*  Whether the level that is being meshed is expected to take long (see startRender): its threads then run at a lower priority,
     *  so that a small shape that was changed meanwhile, or the window, get the processor first -- the long render itself is never
     *  stopped or made to wait, it uses every core that nothing else wants.  force_low marks the renders made in the background
     *  (the steps that are not shown) as such  */
    bool run_heavy = false;
    bool force_low = false;
    QString m_exactKey;
    QList<QStringList> m_keyResults;

    libfive::Tree tree;                 // (what is meshed: the shown step's geometry, usually base_tree)
    libfive::Tree base_tree = libfive::Tree::invalid();     // the shape as the script made it (optimized)
    // (the tree as the script built it: the render cache's key is made of it -- the optimizer's output differs
    // from run to run in the order of operands)
    libfive::Tree built_tree;
    // (whether `tree` is the optimised one, `base_tree` is made and the evaluators `es` are: see ensureField)
    std::atomic<bool> m_field_ready{false};
    mutable std::mutex m_field_mutex;
    size_t m_tree_nodes = 0;            // (the nodes of the optimised tree, counted when it is made)
    void buildField();
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
    // ...where the part is now (its gizmo moves and turns it): what the
    // render in progress uses
    QVector3D run_hint_lo, run_hint_hi;
    float run_hint_side=0;
    void placeHint();
    // where the part's own-resolution cube is now, without making it the one a render in progress uses
    void placedHint(QVector3D& lo, QVector3D& hi, float& side) const;

    // What a render with some settings meshes: the region and the resolution (the part's own cube and resolution when it has
    // them and they fit in the scene's region, else the scene's) and the quality.  Settings that change none of it must not
    // restart a render that is going, nor throw away a mesh that is done: the parts of an import that are meshing do not start
    // again because a part was added to the script that changed the scene's region
    struct RenderGeometry
    {
        QVector3D lo, hi;
        double res = 0;
        float quality = 0;
        std::string exact;                      // (the triangles it is drawn from, if it is: see setExactMesh)
        bool operator==(const RenderGeometry& o) const
        { return lo == o.lo && hi == o.hi && res == o.res && quality == o.quality && exact == o.exact; }
    };
    bool exactCurrent() const;

    /*  Auto scale (Settings::autoScale, the default).  A shape that has no resolution of its own is meshed at the scene's resolution scaled by how
     *  small it is next to the render region -- times the square root of (region / shape), never less than the scene's, and no finer than 2000
     *  samples across it -- so that a small part next to a big one has samples enough across it and the big one is not asked for the fineness
     *  of the small one.  Every shape scales with the scene's number: a higher resolution is a finer picture of every part.  The first picture
     *  (the coarse level) is sized by the shape too: about 64 samples across it, however large the region round it is.  The shape's box is
     *  found by a quick search (a few milliseconds at the most) when its render starts  */
    void measureExtent(const Settings& s);
    /*  The box of the shape at these numbers, searched for over a box (its own search, a few milliseconds at the most): false when it cannot be found  */
    bool searchExtent(const std::map<libfive::Tree::Id, float>& numbers, const QVector3D& search_lo, const QVector3D& search_hi, QVector3D& lo, QVector3D& hi, float seconds = 0.05f,
                      float coarse = 0.0f);
    libfive::Tree::Id m_ext_failed = libfive::Tree::Id();      // (the tree whose box the last search could not find in its time)
    double autoResolution(const Settings& s) const;
    int defaultDivFor(const Settings& s);
    bool m_ext_valid = false;
    QVector3D m_ext_lo, m_ext_hi;

    // The shown step's own triangles (a step of an optimisation), else none; and the ones the render in progress draws from
    std::shared_ptr<const ExactMesh> m_step_exact;
    std::shared_ptr<const ExactMesh> run_step_exact;
    void applyStepExact();
    static std::unique_ptr<libfive::Mesh> exactMeshOf(const ExactMesh& E, const double* matrix, const libfive::Region<3>& r);
    std::unique_ptr<libfive::Mesh> exactMeshIn(const libfive::Region<3>& r) const;
    std::shared_ptr<const ExactMesh> m_exact;
    double m_exact_matrix[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    std::map<libfive::Tree::Id, float> m_exact_vars;     // (the numbers the script had when it said what the part is)
    RenderGeometry renderGeometry(const Settings& s) const;
    /*  The region and the resolution a shape with a resolution of its own is meshed at in these settings (false: the scene's)  */
    bool ownRegion(const Settings& s, const QVector3D& box_lo, const QVector3D& box_hi, float side,
                   QVector3D& lo, QVector3D& hi, double& res) const;
    RenderGeometry m_geometry;                  // (of the render that is going, or was done last)
    bool m_geometry_known = false;
    Settings m_last_settings;                   // (the settings that render was started with)
    libfive::BRepAlgorithm m_last_alg = libfive::BRepAlgorithm::DUAL_CONTOURING;

    Gizmo m_gizmo;
    bool m_locked = false;
    bool m_linked = false;
    SymmetryDef m_symDef;
    bool m_isPoint = false;
    bool m_hasPointPos = false;
    QVector3D m_pointPos;
    QString m_appLabel;
    bool m_appShown = false;

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
    // The drag preview (see previewGizmoVars): the gizmo numbers the mesh that is shown was made with, those of the render that is going on, and the matrix that takes
    // the mesh from the first to the numbers now
    struct GizmoNumbers { QVector3D move, rotate, scale{1, 1, 1}; };
    GizmoNumbers m_meshGizmo, m_runGizmo;
    QMatrix4x4 m_previewXform;
    bool m_previewShown = false;                 // the mesh is shown moved (m_previewXform is not the identity)
    GizmoNumbers gizmoNumbersIn(const std::map<libfive::Tree::Id, float>& v) const;
    void refreshPreview();                       // the matrix from what the mesh was made with to the numbers now
    QMatrix4x4 previewed(const QMatrix4x4& M) const { return m_previewShown ? M * m_previewXform : M; }
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
    std::vector<QVector3D> m_markers;                      // the dots of a set of points (see setMarkers)
    QString marker_key;

    // Colouring by a field (see setColorField)
    bool has_color=false;
    bool has_cutoff=false;                                 // (a patch of a surface: see setColorCutoff)
    float color_cutoff=0;
    bool has_floor=false;                                  // (only the coloured places: see setColorFloor)
    float color_floor=0;
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
    /*  The geometry of a step that has not been meshed yet, meshed in the background while another is shown, and kept (see stashStepMesh):
     *  once the shown one is finished, every step is meshed one after the other, so that playing the steps back finds them all made.
     *  Does nothing while a render runs (the next one is started by the end of that one)  */
    void prefetchSteps();
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
