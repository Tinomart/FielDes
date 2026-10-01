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
#include <QSet>
#include <QPair>
#include <atomic>

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
#include "fieldes/result_panel.hpp"
#include "fieldes/settings.hpp"

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
    QVector3D towardViewer() const { return camera.towardViewer(); }

    /*  The section handle's knob and arrow tip in widget coordinates
     *  (for scripted tests); false if there is no handle  */
    /*  The widget position of a grip of the first part with handles (for
     *  scripted tests): kind 0 = move arrow, 1 = rotation ring; the axis 0-2  */
    bool handleGripPoint(int kind, int axis, QPoint& pos) const;

    bool sectionHandlePoints(QPointF& knob, QPointF& tip) const
    {
        float len;
        return section.enabled && sectionHandle(knob, tip, len);
    }

public slots:

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

    void toOrthographic() { camera.toOrthographic();  }
    void toPerspective()  { camera.toPerspective();   }
    void toTurnZ() { y_up = false; camera.toTurnZ();  }
    void toTurnY()  { y_up = true; camera.toTurnY();   }
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

    /*
     *  Emitted when a drag operation has changed variables
     */
    void varsDragged(QMap<libfive::Tree::Id, float> vs);

    /*  A new sample of the distance field on the section plane  */
    void sliceReady(FieldSlice s);

    /*  The render region changed (the section panel's slider range)  */
    void boundsChanged(QVector3D min, QVector3D max);

    /*  Meshing started (true) or every shape finished (false)  */
    void renderBusy(bool running);

    /*  The section plane was dragged by its handle in the viewport  */
    void sectionOffsetDragged(float offset);

    /*  The distance field under the mouse on the section plane ("" = none)  */
    void sectionReadout(QString text);

    /*  Whether the section plane cuts a shown analysis result that has elements (the section
     *  card offers "whole elements" only then)  */
    void sectionCutsElements(bool cuts);

    /*
     *  A click (press + release without dragging) on a shape: the 0-based
     *  line of the statement displaying it, or -1 for empty space
     */
    void shapeClicked(int line0);

protected slots:
    void update() { QOpenGLWidget::update(); }
    void redrawPicker();

    /*  Starts (debounced) re-sampling of the section field  */
    void requestSlice();
    void startSlice() { startSliceAt(false); }
    void startSliceAt(bool fine);
    void onSliceFinished();

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
    Axes axes;
    Background background;
    BBox bbox;
    Busy busy;

    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
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
        enum { RELEASED, DRAG_ROT, DRAG_PAN, DRAG_EVAL } state = RELEASED;
        QPoint pos;
    } mouse;

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
    /*  A ruler for the scale at the middle of the view  */
    void drawScaleBar(QPainter& p);

    /*  The card for a model shown with an analysis result  */
    ResultPanel* m_resultPanel=nullptr;
    void updateResultPanel();
    void placeResultPanel();
    QScopedPointer<QOpenGLTexture> slice_color_tex;
    // The plane as a deformed vertex grid (for a model drawn deformed)
    int slice_grid_verts = 0;
    int slice_grid_generation = -1;
    float slice_grid_scale = -1, slice_grid_offset = 0;

    /*  For click-to-select: where the press happened and what it hit  */
    QPoint press_pos;
    Shape* press_target=nullptr;

public:
    ScenePanel* scenePanel() const { return m_scene; }
    const Settings& renderSettings() const { return settings; }
protected:
    ScenePanel* m_scene=nullptr;

    /*  Transparent child widget for 2D overlays (orientation triad, cursor
     *  position): QPainter drawing inside paintGL after native GL painting
     *  never reaches the screen with this core-profile context  */
    QWidget* m_overlay=nullptr;
    void paintOverlay(QPainter& painter);

    /*  Set to true on the first draw, if the OpenGL version is new enough */
    bool gl_checked=false;

    /*  Section view state  */
    SectionSettings section;
    FieldSlice slice;
    bool slice_dirty=false;           // texture needs re-upload
    QScopedPointer<QOpenGLTexture> slice_tex;
    QOpenGLBuffer slice_vbo;
    QOpenGLVertexArrayObject slice_vao;
    QTimer slice_timer;
    QFutureWatcher<FieldSlice> slice_watcher;

    /*  A shape's fields as the plane samples them (copied for the worker thread)  */
    FieldSource sourceOf(Shape* s) const;
    std::atomic<int> slice_generation{0};
    void setClipUniform(bool on);
    void drawSlicePlane(const QMatrix4x4& m);

    /*  Where the field is sampled (around the displayed models) and the
     *  distance outside them where the plane fades out  */
    void sliceRegion(QVector3D& lo, QVector3D& hi, float& fade) const;

    /*  The handle for dragging the plane: an arrow along the plane normal
     *  through the middle of the model, in widget coordinates  */
    bool sectionHandle(QPointF& p, QPointF& q, float& length) const;
    bool sectionHandleHit(QPoint pos) const;
    void drawSectionHandle(QPainter& painter);
    QPointF toScreen(const QVector3D& p) const;

    /*  The gizmo of placed parts (Shape::handles, fieldes.stdlib.handles): move
     *  arrows along the axes, rotation rings and scale knobs at the part's pivot,
     *  dragged to change the script's numbers.  kind 0 = move, 1 = rotate,
     *  2 = scale (Shift: all three axes together)  */
    struct HandleGrip
    {
        Shape* shape = nullptr;
        int kind = -1;
        int axis = 0;
        bool operator==(const HandleGrip& o) const
        { return shape == o.shape && kind == o.kind && axis == o.axis; }
    };
    HandleGrip handle_hover;
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
    QString section_readout;
    QPoint section_readout_pos;
    void updateSectionReadout(QPoint pos);
};

}   // namespace FielDes
