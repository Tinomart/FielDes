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

#include <QElapsedTimer>
#include <QJsonObject>
#include <QSet>
#include <QPair>
#include <atomic>
#include <functional>

#include <QFutureWatcher>
#include <QOpenGLWidget>
#include <QOpenGLFramebufferObject>
#include <QOpenGLTexture>

#include "fieldes/arrow.hpp"
#include "fieldes/axes.hpp"
#include "fieldes/background.hpp"
#include "fieldes/bbox.hpp"
#include "fieldes/busy.hpp"
#include "fieldes/camera.hpp"
#include "fieldes/shape.hpp"
#include "fieldes/section.hpp"
#include "fieldes/result.hpp"
#include "fieldes/result_panel.hpp"
#include "fieldes/settings.hpp"

class QMenu;
namespace FielDes { class ScenePanel; }

#include "libfive/eval/eval_jacobian.hpp"

namespace FielDes {

class View : public QOpenGLWidget, QOpenGLFunctions
{
    Q_OBJECT
public:
    View(QWidget* parent=nullptr);

    enum StandardView { VIEW_FRONT, VIEW_BACK, VIEW_RIGHT, VIEW_LEFT,
                        VIEW_TOP, VIEW_BOTTOM, VIEW_ISO };

    /*
     *  Requests that every active Shape cancels its render operation
     *  This is used when quitting to clean up threads quickly
     */
    void cancelShapes();

    /*
     *  In the destructor, free the OpenGL data associated with
     *  all children Shapes (because there could be shapes that
     *  aren't explicitly stored in the object, which will be freed
     *  by the QObject destructor, which runs after the OpenGL context
     *  is gone).
     */
    ~View();

    /*  Whether any shape is still being rendered, and a ballpark of the
     *  seconds until all are (negative: no estimate yet); they render at
     *  the same time, so the longest one  */
    bool isRendering() const;
    /*  How many shapes the viewport holds (for scripted tests)  */
    int shapeCount() const { return shapes.size(); }
    double renderFraction() const;

    /*  The render's progress bar (in paintOverlay)  */
    void drawRenderProgress(QPainter& painter);
    QElapsedTimer m_renderClock;
    double m_renderShown = 0.0;
    bool m_renderActive = false;
    int m_renderLogged = -1;

public slots:
    void setShapes(QList<Shape*> shapes);

    /*  Model-tree selection: highlight the shapes displayed by these
     *  (0-based) script lines  */
    void highlightLines(QList<int> lines0);

    /*  Frames the camera on a box, or (if the box is empty) on the meshes
     *  of the shapes displayed by the given lines  */
    void focusOn(QVector3D min, QVector3D max, QList<int> lines0);

    /*  Frames every shape that has a mesh  */
    void frameAll();

public:
    /*  Union of the displayed shapes' mesh bounds; false if none yet  */
    bool meshBounds(QVector3D& lo, QVector3D& hi) const;

    /*  The 0-based lines of the shapes highlighted as the selection (for scripted tests)  */
    QList<int> highlightedLines() const;
    QVector3D towardViewer() const { return camera.towardViewer(); }

    /*  How the section view is set now (for the guided tour)  */
    const SectionSettings& sectionSettings() const { return section; }

    /*  The section handle's knob and arrow tip in widget coordinates
     *  (for scripted tests); false if there is no handle  */
    /*  The widget position of a grip of the first part with handles (for
     *  scripted tests): kind 0 = move arrow, 1 = rotation ring; the axis 0-2  */
    bool handleGripPoint(int kind, int axis, QPoint& pos) const;

    /*  Where a point of the model is in the widget (for the guided tour)  */
    QPoint screenPoint(const QVector3D& p) const { return toScreen(p).toPoint(); }

    /*  Selects the surface under a widget position as the menu of a right-click does (for scripted
     *  tests); false if no shape with a line of the script is there  */
    bool selectSurfaceAt(QPoint pos, const QString& mode, double angle, double thickness, double radius);

    /*  What the context menus create: the primitives and operations of the library, as the interpreter lists
     *  them (fieldes.menu_catalog); asked once, when a script has run, so a right-click never waits for Python  */
    void setMenuCatalogSource(std::function<QString()> f) { m_catalogSource = f; }
    void loadMenuCatalog();

    bool sectionHandlePoints(QPointF& knob, QPointF& tip) const
    {
        float len;
        return section.enabled && sectionHandle(knob, tip, len);
    }

public slots:

    /*  The gizmo of the selected model, at once.  The gizmo is made of numbers in the script (a `handles(...)` line), which a model gets
     *  when it is selected, and the script has to run for them to exist: that takes a moment.  Until then the gizmo is drawn from where
     *  the model is (`pivot`: the middle of its box, the same point the real one has), a little transparent, so that it is there the
     *  moment the model is selected.  Pressing it before it is ready does not lose the press: the drag starts as soon as the real
     *  gizmo is there, if the button is still down.  `lines0` are the script lines the model is shown by  */
    void setProvisionalGizmo(bool on, QVector3D pivot, QList<int> lines0);

    /*  Zoom to the script's bounds the next time they arrive (e.g. after
     *  an import changed the region of interest)  */
    void zoomOnNextSettings() { zoom_on_next_settings = true; }

    void showAxes(bool a);
    void showBBox(bool b);
    void showTriad(bool t) { show_triad = t; update(); }
    /*  Colour legends shown at all (View menu); showing them again also
     *  brings back the ones closed with their x  */
    void showLegends(bool b) { show_legends = b; if (b) hidden_legends.clear(); update(); }

    /*  Standard camera views (relative to the current up axis); which
     *  is a StandardView  */
    void standardView(int which);

    /*  Section view / field viewer  */
    void setSection(SectionSettings s);

    /*  The field models of the script, after a run (the field viewer shows the selected one: see showField)  */
    void setFieldSources(QList<FieldEntry> fields);
    /*  The field viewer's disc shows the field model with this key (a variable's name, or "line:N"): the field's value at every
     *  point of the disc.  An empty key (or none that exists) ends it.  The disc is a plane of its own, with its own samples: the
     *  section view is another one and works whether or not a field is shown  */
    void showField(QString key);
    bool fieldShown() const { return !m_fieldKey.isEmpty() && m_fieldSources.contains(m_fieldKey); }
    /*  The field viewer's disc (see FieldPanel): where it is, which way it faces, how big, how opaque  */
    void setFieldView(FieldViewSettings s);
    /*  Where the shown field is about (the point or body it was made from; else the origin), and whether that is known  */
    QVector3D fieldCentre(bool* known = nullptr) const;

    void toOrthographic() { camera.toOrthographic();  }
    void toPerspective()  { camera.toPerspective();   }
    void toTurnZ() { y_up = false; camera.toTurnZ();  }
    void toTurnY()  { y_up = true; camera.toTurnY();   }
    /*  Which axis is up, at once (the start of a session: no turn to animate)  */
    void setUpAxis(bool y) { y_up = y; camera.setUpAxis(y); update(); }
    void setLowRotSensitivity()  { camera.setRotationSensitivity(240); }
    void setMedRotSensitivity()  { camera.setRotationSensitivity(360); }
    void setHighRotSensitivity() { camera.setRotationSensitivity(720); }
    void setZoomCursorCentric() { zoom_cursor_centric = true; }
    void setZoomSceneCentric() { zoom_cursor_centric = false; }
    void zoomTo() { camera.zoomTo(settings.min, settings.max); }

    void toDCMeshing();
    void toIsoMeshing();
    void toHybridMeshing();

    /*
     *  Emits shapesReady if all the shapes being drawn
     *  are at their final (highest) resolution
     */
    void checkMeshes() const;

    /*  What the render cache did for each shape that has it on, by the 0-based line that displays the shape:
     *  "state|words" (see ScenePanel::setCacheStates)  */
    QHash<int, QString> cacheStates() const;

    /*
     *  Called when the script changes settings
     *  If first is true, then the camera zooms to the new bounds.
     */
    void onSettingsFromScript(Settings s, bool first);

signals:
    /*
     *  Called to kick off a render and start the busy spinner running
     */
    void startRender(Settings s);

    /*
     *  Emitted when all shapes are done rendering at their highest resolution
     */
    void meshesReady(QList<const libfive::Mesh*> shapes) const;

    /*
     *  Indicates when a drag operation begins and ends
     */
    void dragStart();
    void dragEnd();
    /*  The provisional gizmo was pressed: the model is made ready to be dragged now, without waiting  */
    void provisionalPressed();

    /*
     *  Emitted when a drag operation has changed variables
     */
    void varsDragged(QMap<libfive::Tree::Id, float> vs);

    /*  A new sample of the distance field on the section plane  */
    void sliceReady(FieldSlice s);
    /*  A new sample of the shown field on the field viewer's disc  */
    void fieldSliceReady(FieldSlice s);

    /*  The render region changed (the section panel's slider range)  */
    void boundsChanged(QVector3D min, QVector3D max);

    /*  Meshing started (true) or every shape finished (false)  */
    void renderBusy(bool running);

    /*  A shape's render cache read or kept its mesh (see cacheStates)  */
    void cacheStatesChanged();

    /*  The section plane was dragged along its normal by its handle in the viewport  */
    void sectionOffsetDragged(float offset);
    /*  The field viewer's disc was dragged (by its arrow along its normal, by one in its plane, or by the dot in the middle)  */
    void fieldCentreDragged(QVector3D centre);

    /*  The distance field under the mouse on the section plane ("" = none)  */
    void sectionReadout(QString text);
    /*  The shown field's value under the mouse on the field viewer's disc ("" = none)  */
    void fieldReadout(QString text);

    /*  Whether the section plane cuts a shown analysis result that has elements (the section
     *  card offers "whole elements" only then)  */
    void sectionCutsElements(bool cuts);

    /*
     *  A click (press + release without dragging) on a shape: the 0-based
     *  line of the statement displaying it, or -1 for empty space
     */
    void shapeClicked(int line0);

    /*
     *  A Ctrl+click (no dragging) on a shape: the 0-based line of the statement displaying it joins the selection,
     *  or leaves it
     */
    void shapeToggled(int line0);

    /*
     *  A selection rectangle was drawn (left drag) and let go: the 0-based lines of the statements displaying
     *  the shapes that lie wholly inside it; with `add` (Ctrl held too) they join the selection instead of
     *  replacing it
     */
    void shapesRectSelected(QList<int> lines0, bool add);

    /*
     *  The menu of a right-click on a shape was confirmed: select the surface of the shape displayed by
     *  the (0-based) line `line0` around `point` (a flood fill: mode "flat" or "smooth", the angle in
     *  degrees, the thickness and the radius in mm, 0 = automatic / no limit)
     */
    void surfaceSelectRequested(int line0, QVector3D point, QString mode, double angle, double thickness,
                                double radius);

    /*
     *  An entry of a context menu was chosen: a new primitive (kind "primitive") placed at `point`, or an
     *  operation (kind "operation") on the model displayed by the (0-based) line `line0`, or, for -1, the
     *  selected one; `scale` is how many mm about a hundred pixels span at that place
     */
    void createRequested(QString kind, QString name, QVector3D point, double scale, int line0, int generation);

protected slots:
    void update() { QOpenGLWidget::update(); }
    void redrawPicker();

    /*  Starts (debounced) re-sampling of the section field, and of the field viewer's disc  */
    void requestSlice();
    void requestFieldSlice();

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int width, int height) override;

    void setAlgorithm(libfive::BRepAlgorithm alg);

    /*
     *  Converts from mouse event coordinates to model coordinates
     *  using the z depth from the pick buffer
     */
    QVector3D toModelPos(QPoint pt) const;

    /*
     *  Converts from mouse event coordinates to model coordinates
     *  using a user-provided z depth
     */
    QVector3D toModelPos(QPoint pt, float z) const;

    /*  Background items to render  */
    Arrow arrow;
    Glyphs glyphs;                      // the arrows and pins of boundary conditions
    Axes axes;
    Background background;
    BBox bbox;
    Busy busy;

    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

    /*  A right-click (not a pan) on a shape: first a menu with Operation (the operations, on that shape) and
     *  Select Surface, which opens the menu of the surface selection  */
    void showSurfaceMenu(QPoint globalPos, int line, const QVector3D& point, double scale, bool onSurface = true);
    void showSelectMenu(QPoint globalPos, int line, const QVector3D& point);
    /*  A right-click (not a pan) on empty space: New 3D shape, 2D shape, point, surface, field, custom block, Add operation
     *  and Add simulation.  `atLine` (from the text editor): what is made is written under that line  */
    void showEmptyMenu(QPoint globalPos, QPoint pos, int atLine = -1);
    /*  The operations as a menu's entries (the combining ones greyed when there is no second model); the simulations are
     *  their own menu: `simulations` says which of the two is listed  */
    void fillOperations(QMenu* menu, int line, const QVector3D& point, double scale, bool simulations = false);
    /*  Where a primitive goes: the point of the ray under a widget position that is closest to the origin  */
    QVector3D placeOnRay(QPoint pos) const;
    /*  How many mm about a hundred pixels span, at the depth of a point  */
    double scaleAt(QPoint pos, const QVector3D& point) const;
    QPoint right_press_pos = QPoint(-100000, -100000);
    Shape* right_press_target = nullptr;
    bool right_press_empty = false;
    QVector3D right_press_point;
    std::function<QString()> m_catalogSource;
    QJsonObject m_catalog;

    void leaveEvent(QEvent* event) override
    {
        if (triad_hover >= 0)
        {
            triad_hover = -1;       // no stale highlight on the triad
            update();
        }
        QOpenGLWidget::leaveEvent(event);
    }
    void wheelEvent(QWheelEvent *event) override;

    /*  Orientation triad in the bottom-right corner: painted with QPainter,
     *  its axis tips are clickable (look along that axis)  */
    void drawTriad(QPainter& painter);
    int triadHit(QPoint pos) const;
    QVector<QPair<QPointF, QVector3D>> triad_hits;
    int triad_hover=-1;

    /*
     *  Updates hover_target based on mouse cursor position
     */
    void checkHoverTarget(QPoint pos);

    /*
     *  Ensures that the pick buffer is synced, forcing a pick render
     *  if the timer is currently running.
     */
    void syncPicker();

    Camera camera;

    struct {
        enum { RELEASED, DRAG_ROT, DRAG_PAN, DRAG_EVAL, DRAG_RECT } state = RELEASED;
        QPoint pos;
    } mouse;

    /*  The selection rectangle (left drag; Shift + left drag and the middle button turn the view): where it began and where the mouse is now, in widget pixels  */
    QPoint rect_start, rect_now;
    bool rect_add = false;
    /*  A Ctrl+click toggles the selection instead of replacing it  */
    bool press_ctrl = false;
    /*  The 0-based lines of the shapes whose whole mesh lies inside a rectangle of the widget  */
    QList<int> linesInside(const QRect& r) const;

    QList<Shape*> shapes;
    bool settings_enabled=true;
    Settings settings;
    libfive::BRepAlgorithm alg = libfive::BRepAlgorithm::DUAL_CONTOURING;

    bool show_axes=true;
    bool show_bbox=false;
    bool show_triad=true;
    /*  Emits sectionCutsElements when the answer changed  */
    void updateSectionElements();
    bool m_sectionElements=false;

    bool show_legends=true;
    QSet<QString> hidden_legends;                  // closed with their x (by label)
    QVector<QPair<QRect, QString>> legend_close;   // the x buttons drawn, for clicks
    QVector<QRectF> bottom_items;                  // legends and scale bar drawn (the render bar avoids them)
    bool y_up=false;

    bool zoom_cursor_centric = true;
    bool zoom_on_next_settings = false;

    /*  Framebuffer to render pick data  */
    QScopedPointer<QOpenGLFramebufferObject> pick_fbo;
    QTimer pick_timer;
    QImage pick_img;
    QVector<float> pick_depth;

    /*  Data to handle direct modification of shapes */
    QVector3D drag_start;
    QVector3D drag_dir;
    std::pair<std::unique_ptr<libfive::JacobianEvaluator>,
              std::shared_ptr<libfive::Tape>> drag_eval;
    Shape* drag_target=nullptr;
    bool drag_valid=false;
    Shape* hover_target=nullptr;

    QVector3D cursor_pos;
    bool cursor_pos_valid=false;

    /*  The colour field's value under the cursor (a model shown coloured
     *  by a field, e.g. an FEA result)  */
    bool probe_valid=false;
    float probe_value=0;
    QString probe_label;
    QPoint probe_pos;

    /*  Colour bars of the models shown coloured by a field  */
    void drawLegends(QPainter& p);
    void drawBoundaryLabels(QPainter& p);
    /*  A ruler for the scale at the middle of the view  */
    void drawScaleBar(QPainter& p);

    /*  The card for a model shown with an analysis result  */
    ResultPanel* m_resultPanel=nullptr;
    void updateResultPanel();
    void placeResultPanel();
    QTimer m_stepTimer;                 // play: the next step of the results
    QTimer m_flowTimer;                 // the particles on the streamlines of a flow move
    int m_playMode = 0;                 // 0 loop, 1 back and forth, 2 once (stop at the end)
    float m_playSpeed = 1.0f;           // steps per second = 10 x this
    int m_playDir = 1;                  // (back and forth: which way)

    /*  For click-to-select: where the press happened and what it hit  */
    QPoint press_pos;
    Shape* press_target=nullptr;

public:
    ScenePanel* scenePanel() const { return m_scene; }
    const Settings& renderSettings() const { return settings; }
    /*  The menu of the viewport for the model displayed by a (0-based) line, opened at `globalPos` from outside the viewport
     *  (a right-click in the text editor or the model tree): as if that model had been right-clicked there.  A line that
     *  displays no model opens the menu of empty space, and what it creates goes under that line  */
    void showMenuForLine(int line0, QPoint globalPos);
protected:
    ScenePanel* m_scene=nullptr;

    /*  Transparent child widget for 2D overlays (orientation triad, cursor
     *  position): QPainter drawing inside paintGL after native GL painting
     *  never reaches the screen with this core-profile context  */
    QWidget* m_overlay=nullptr;
    void paintOverlay(QPainter& painter);

    /*  Set to true on the first draw, if the OpenGL version is new enough */
    bool gl_checked=false;

    /*  A plane that shows a sampled field: what the section view has and what the field viewer's disc has, each its own.  The
     *  samples are made in a worker thread (a quick pass, then a fine one) and drawn from a texture  */
    struct Plane
    {
        FieldSlice slice;
        bool dirty = false;                // the texture needs re-upload
        QScopedPointer<QOpenGLTexture> tex;
        QScopedPointer<QOpenGLTexture> colorTex;
        QOpenGLBuffer vbo;
        QOpenGLVertexArrayObject vao;
        // (the plane as a deformed vertex grid, for a model drawn deformed)
        int gridVerts = 0;
        int gridGeneration = -1;
        float gridScale = -1, gridOffset = 0;
        QTimer timer;
        QFutureWatcher<FieldSlice> watcher;
        std::atomic<int> generation{0};
        /*  How long the last quick pass took (ms): the fine pass is about sixteen times that, so a field that is slow to evaluate
         *  gets a coarser fine pass, and the plane is never far behind the hand that moves it  */
        QElapsedTimer clock;
        double quickMs = 0;
    };

    /*  Section view state: the plane that cuts the models and shows the distance field on them  */
    SectionSettings section;
    Plane m_sec;
    /*  The field viewer's disc: a plane of its own, never the section's  */
    Plane m_fld;

    /*  A shape's fields as the plane samples them (copied for the worker thread)  */
    FieldSource sourceOf(Shape* s) const;

    /*  The field models of the script (they are not shapes, nothing of them is drawn) and the one whose field the disc shows
     *  (a key: a variable's name, or "line:N").  Their colour ranges are found when a field is first shown, and kept until the
     *  script runs again  */
    QMap<QString, FieldSource> m_fieldSources;
    QMap<QString, QPair<float, float>> m_fieldRanges;
    QString m_fieldKey;
    void startSliceAt(Plane& plane, bool fine);
    void onSliceFinished(Plane& plane);
    void setClipUniform(bool on);
    void drawSlicePlane(const QMatrix4x4& m, Plane& plane, const SectionSettings& at);

    /*  The disc as the settings of a plane (enabled, no cut): the one that is sampled and drawn  */
    SectionSettings fieldPlane() const;

    /*  Where the field is sampled (around the displayed models) and the
     *  distance outside them where the plane fades out  */
    void sliceRegion(QVector3D& lo, QVector3D& hi, float& fade) const;

    /*  The handle for dragging the section plane: an arrow along the plane normal
     *  through the middle of the model, in widget coordinates  */
    bool sectionHandle(QPointF& p, QPointF& q, float& length) const;
    bool sectionHandleHit(QPoint pos) const;
    void drawSectionHandle(QPainter& painter);
    /*  The double arrow along a plane's normal through `c`, in widget coordinates, and whether a widget position is on it  */
    bool normalArrow(int axis, const QVector3D& c, float length, QPointF& p, QPointF& q) const;
    static bool onArrow(QPoint pos, QPointF p, QPointF q);
    static void paintNormalArrow(QPainter& painter, QPointF p, QPointF q, bool hot);

    /*  The field viewer's disc has the same arrow along its normal, two more in its plane, and a dot in the middle that moves it
     *  freely in the plane: the screen points of its middle and of the tips of the two arrows (along the two axes of the plane)
     *  and of the one along the normal  */
    bool fieldGizmo(QPointF& middle, QPointF& tipU, QPointF& tipV, QPointF& tipN, float& length) const;
    /*  What is under a widget position: 0 nothing, 1 / 2 an arrow in the plane, 3 the dot, 4 the arrow along the normal  */
    int fieldGizmoHit(QPoint pos) const;
    void drawFieldGizmo(QPainter& painter);
    void dragFieldGizmo(QPoint pos);
    float fieldRadius() const;
    void applyFieldView();
    FieldViewSettings m_fieldView;
    QMap<QString, QPair<QVector3D, bool>> m_fieldCentres;
    int field_drag = 0;                       // (which part of the gizmo is held: see fieldGizmoHit)
    int field_hover = 0;                      // (and which is under the cursor)
    QPoint field_press;
    QVector3D field_press_centre, field_press_grab;
    QPointF field_dir;                        // (the screen direction and the length in mm of the arrow that is held)
    float field_len = 0;
    QPointF toScreen(const QVector3D& p) const;

    /*  The gizmo of placed parts (Shape::handles, fieldes.stdlib.handles): move
     *  arrows along the axes, rotation rings and scale knobs at the part's pivot,
     *  dragged to change the script's numbers.  kind 0 = move, 1 = rotate,
     *  2 = scale (Shift: all three axes together)  */
    struct HandleGrip
    {
        Shape* shape = nullptr;
        int kind = -1;              // 0 move arrow, 1 rotation ring, 2 scale knob, 3 the dot in the middle (free move)
        int axis = 0;
        bool group = false;         // the gizmo shared by all the selected shapes (kinds 0 and 3 only)
        bool operator==(const HandleGrip& o) const
        { return shape == o.shape && kind == o.kind && axis == o.axis && group == o.group; }
    };
    /*  Two or more shapes are selected (the multi-select state): one gizmo, at the middle of the pivots of the ones that
     *  have numbers to move them by, moves them all, whatever way of editing each of them is in when it is alone (its
     *  arrows and its dot; turning and scaling are one shape at a time).  Gives them and the gizmo's place; false when
     *  fewer than two are selected (every shape then has its own gizmo, as usual)  */
    bool groupHandles(QList<Shape*>& members, QVector3D& center) const;
    /*  How many shapes are selected (lit up as the model tree's selection)  */
    int selectedShapeCount() const;
    /*  The numbers a gizmo drag changes: each shape's move number for an axis, with its value when the drag began  */
    struct HandleVar { libfive::Tree::Id id; int axis; float value0; };
    QVector<HandleVar> handle_vars;
    QVector3D free_hit0, free_normal;   // a free move: where the cursor's ray met the plane at the start, and the plane's normal
    HandleGrip handle_hover;
    // The provisional gizmo (see setProvisionalGizmo), and a press on it that waits for the real one
    bool m_prov = false;
    QVector3D m_provPivot;
    QList<int> m_provLines;
    struct WaitGrip { bool on = false; int kind = -1, axis = 0, ticks = 0; QList<int> lines; };
    WaitGrip m_wait;
    QTimer m_waitTimer;
    bool provisionalVisible() const;
    bool provisionalGripAt(QPoint pos, int* kind, int* axis) const;
    void drawProvisional(QPainter& painter);
    void startWaitingGrip();
    void cancelWaitingGrip();
    HandleGrip handle_active;
    bool handle_drag = false;
    libfive::Tree::Id handle_id = nullptr;
    float handle_value0 = 0;            // the number when the drag began
    QPoint handle_press;
    QPointF handle_dir;                 // a move arrow on the screen
    float handle_len = 0;               // ...and its length in the model
    QVector3D handle_pivot, handle_axis, handle_vec;   // a ring's
    float handle_turn = 0;              // degrees turned so far
    float handle_scale0[3] = {1, 1, 1}; // the scale numbers when a scale drag began
    libfive::Tree::Id handle_scale_id[3] = {nullptr, nullptr, nullptr};
    float handleLength(const QVector3D& pivot) const;
    HandleGrip handleAt(QPoint pos) const;
    void drawHandles(QPainter& painter);
    void beginHandleDrag(const HandleGrip& g, QPoint pos);
    void dragHandle(QPoint pos);
    bool ringPoint(QPoint pos, const QVector3D& pivot, const QVector3D& axis, QVector3D& out) const;
    void applyHandleNumber(libfive::Tree::Id id, float value);
    void applyHandleNumbers(const std::map<libfive::Tree::Id, float>& numbers);

    bool section_hover=false;
    bool section_drag=false;
    QPoint section_press;
    float section_press_offset=0;
    QString section_readout;                  // (what the box at the cursor says: the plane nearest the viewer that is under it)
    QString m_sectionText, m_fieldText;       // (what each plane says about the point under the cursor: its card shows it)
    QPoint section_readout_pos;
    void updateSectionReadout(QPoint pos);
};

}   // namespace FielDes
