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

#include <QPolygonF>
#include <QImage>
#include <QElapsedTimer>
#include <QHash>
#include <QMutex>
#include <QJsonObject>
#include <QSet>
#include <QPair>
#include <atomic>
#include <functional>
#include <map>
#include <memory>

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
#include "fieldes/symmetry.hpp"
#include "fieldes/workplane.hpp"
#include "fieldes/result.hpp"
#include "fieldes/result_panel.hpp"
#include "fieldes/live_panel.hpp"
#include "fieldes/settings.hpp"
#include "libfive/run_progress.hpp"

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

    /*  The viewport of a FielDes app window: no model tree and no context menu -- what is there is looked at, and dragged by its gizmo and its
     *  handles (a model that is not locked) -- and the names the app gives its models are drawn over them  */
    void setAppMode(bool on);
    bool appMode() const { return m_appMode; }

    /*  Whether any shape is still being rendered, and a ballpark of the
     *  seconds until all are (negative: no estimate yet); they render at
     *  the same time, so the longest one  */
    bool isRendering() const;
    /*  The graphics driver the viewport draws with -- vendor, renderer, OpenGL version -- for a bug report; empty when there is no context yet  */
    QString graphicsInfo();
    /*  How many shapes the viewport holds (for scripted tests)  */
    int shapeCount() const { return shapes.size(); }
    const QList<Shape*>& allShapes() const { return shapes; }
    double renderFraction() const;

    /*  The render's progress bar (in paintOverlay)  */
    void drawRenderProgress(QPainter& painter);
    QElapsedTimer m_renderClock;
    double m_renderShown = 0.0;
    bool m_renderActive = false;
    int m_renderLogged = -1;

public slots:
    /*  The shapes of a run.  With keepFromLine >= 0 (a partial picture: the run is not over) the shapes shown that come from statements at or
     *  after that line, and are not in the list, stay -- they are not made yet in this run, and a render that is going on is not stopped  */
    void setShapes(QList<Shape*> shapes, int keepFromLine = -1);

    /*  Model-tree selection: highlight the shapes displayed by these
     *  (0-based) script lines  */
    void highlightLines(QList<int> lines0);

    /*  The model tree followed an edit of the script before it was run (a line went in or out): the lines the shapes were made on are the
     *  lines they are on now -- moved[old line] = new line, or -1 when the line is gone.  Until the run that answers the edit has made new
     *  shapes, a click on a shape would otherwise select the row of another model, and the row that is selected would light up another shape  */
    void moveSourceLines(QVector<int> moved);

    /*  The point put on an outline is displayed by this line (-1: none was made): the press that put it there goes on as the drag of its dot  */
    void expectNewPoint(int line0);

    /*  Frames the camera on a box, or (if the box is empty) on the meshes
     *  of the shapes displayed by the given lines  */
    void focusOn(QVector3D min, QVector3D max, QList<int> lines0);

    /*  Frames every shape that has a mesh  */
    void frameAll();

public:
    /*  The mouse gestures the view has beside the ones everyone has (see setNavigationPreset)  */
    enum NavigationPreset { NAV_DEFAULT, NAV_BLENDER, NAV_TOUCHPAD, NAV_LEFT_TURN, NAV_CUSTOM };
    /*  Union of the displayed shapes' mesh bounds; false if none yet  */
    bool meshBounds(QVector3D& lo, QVector3D& hi) const;

    /*  The 0-based lines of the shapes highlighted as the selection (for scripted tests)  */
    QList<int> highlightedLines() const;
    QVector3D towardViewer() const { return camera.towardViewer(); }

    /*  How the section view is set now (for the guided tour)  */
    const SectionSettings& sectionSettings() const { return section; }

    /*  The script has an error: a red banner at the top of the viewport and a frame round it say so, with the message and the
     *  (0-based) line, -1 when it is not known; the picture is the last one that worked.  An empty text takes them away.  (The
     *  window decides when: not at the first key of a line that is being typed)  */
    void setError(const QString& text, int line0);
    bool hasError() const { return !m_errorText.isEmpty(); }

    /*  The section handle's knob and arrow tip in widget coordinates
     *  (for scripted tests); false if there is no handle  */
    /*  The widget position of a grip of the gizmo of the first part (for
     *  scripted tests): kind 0 = move arrow, 1 = rotation ring; the axis 0-2  */
    bool gizmoGripPoint(int kind, int axis, QPoint& pos) const;
    /*  What the viewport has under the mouse for a pull of a surface (the automation verb `hovertarget`): the line of its shape, whether it is pullable, how many are selected  */
    QString hoverDescription(QPoint pos) const;
    /*  Holds Shift down for the gizmo (for scripted tests: the events sent from a script do not change the keyboard)  */
    void holdShift(bool on);
    /*  Whether a real Shift key has been down since `clearShiftWas` -- or is down now; the automation's holdShift does not count (for the guided tour: the step about the Shift gizmo
     *  is done when the user holds Shift)  */
    bool shiftWasDown() const;
    void clearShiftWas() { m_shiftWas = false; }

    /*  Where a point of the model is in the widget (for the guided tour)  */
    QPoint screenPoint(const QVector3D& p) const { return toScreen(p).toPoint(); }

    /*  Selects the surface under a widget position as the menu of a right-click does (for scripted
     *  tests); false if no shape with a line of the script is there  */
    bool selectSurfaceAt(QPoint pos, const QString& mode, double angle, double radius);
    /*  What the entries of the right-click menu near an edge or a corner do, for the body under `pos`: `kind` "corner", "edge" or "edge_point" asks for it to be written into the script;
     *  "list" writes what the menu would offer there to the log (for the tests)  */
    bool featureAt(QPoint pos, const QString& kind);

    /*  What the context menus create: the primitives and operations of the library, as the interpreter lists
     *  them (fieldes.menu_catalog); asked once, when a script has run, so a right-click never waits for Python  */
    void setMenuCatalogSource(std::function<QString()> f) { m_catalogSource = f; }
    void loadMenuCatalog();
    /*  The menus are made again from the library's catalog (the primitives of the Primitives menu have changed)  */
    void reloadMenuCatalog() { m_catalog = QJsonObject(); loadMenuCatalog(); }
    bool sectionHandlePoints(QPointF& knob, QPointF& tip) const
    {
        float len;
        return section.enabled && sectionHandle(knob, tip, len);
    }

public slots:

    /*  The gizmo of the selected model, at once.  The gizmo is made of numbers in the script (its `position=`, `rotation=` and `scale=` arguments), which a model gets
     *  when it is selected, and the script has to run for them to exist: that takes a moment.  Until then the gizmo is drawn from where
     *  the model is (`pivot`: the middle of its box, the same point the real one has), a little transparent, so that it is there the
     *  moment the model is selected.  Pressing it before it is ready does not lose the press: the drag starts as soon as the real
     *  gizmo is there, if the button is still down.  `lines0` are the script lines the model is shown by  */
    void setProvisionalGizmo(bool on, QVector3D pivot, QList<int> lines0);
    /*  The gizmo buttons of the model tree were pressed: the shapes take the modes they say (ScenePanel::gizmoModeOfLine)  */
    void refreshGizmoModes();

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
    /*  The guided tour: where the point of the orientation triad for a world direction is (in the view's coordinates; false while the triad is not drawn), and whether the view looks
     *  straight along an axis of the world -- a standard side  */
    bool triadTip(const QVector3D& dir, QPointF& tip) const;
    bool onStandardSide();

    /*  Section view / field viewer  */
    void setSection(SectionSettings s);

    /*  The work plane (see workplane.hpp): a plane to put points in, with a grid.  Its view is opened and closed with Shift+W (a plane is made when there is
     *  none), a new plane -- the plane of the selected 2D shape, or parallel to the view through the origin -- is Ctrl+Shift+W, and W turns the camera to face it  */
    /*  Settings > Units was changed: what shows numbers -- the ruler, the colour bars, the card of a result -- is drawn again in the units chosen  */
    void unitsChanged();
    const WorkPlane& workPlane() const { return m_wp; }
    void setWorkPlaneOpen(bool open);
    /*  Escape: one step out of the special mode the viewport is in, the innermost first -- the line being drawn on the work plane is dropped, then the kind of drawing that is on is
     *  stopped (clicks select again), then the work plane view is closed.  False when there is nothing to leave (the window then goes on: it closes the section view, or cancels the render)  */
    bool exitMode();
    void newWorkPlane();
    void workPlaneFromView();
    void faceWorkPlane();
    void setWorkPlaneOptions(bool grid, bool snap, bool snapPoints, float spacing);
    void setWorkPlaneOffset(float offset);
    /*  The step of the grid now (the one the card gives, or what automatic works out to from how large the picture is)  */
    float workPlaneStep() const;
    /*  `p` moved to the nearest point that is there (a dot, within a few pixels) and then onto the grid, as the plane is set to snap; unchanged while its view is closed.
        `exclude` is the point that is being dragged; `snapped` says what was done  */
    QVector3D snapToWorkPlane(const QVector3D& p, const Shape* exclude = nullptr, bool* snapped = nullptr, bool toPoints = true) const;
    /*  What a click does: 0 nothing of its own -- things are selected, moved, turned and scaled as usual (Q) --, 1 puts down the points of a line, 2 of a spline, 3 a point.
        E goes round 1, 2, 3 while the work plane view is open  */
    void setWorkPlaneMode(int mode);
    void cycleWorkPlaneMode();
    /*  The plane set to a frame at once: its origin and its two axes (for scripted tests)  */
    void setWorkPlaneFrame(const QVector3D& origin, const QVector3D& u, const QVector3D& v);
    /*  The snapping mode (see workplane.hpp, SnapGrid): Tab in the viewport turns it on and off.  While it is on everything that is moved -- a model by an arrow or a square of its gizmo,
     *  a face or a point that is pulled, what a click puts down -- goes to a node of a grid of the whole space, every turn goes to a multiple of the angle step, and the grid is drawn
     *  as a faint lattice round the place the view looks at.  It is off while the work plane view is open (the plane has a grid of its own) and comes back when that is left;
     *  holding Alt moves things freely for as long as it is held  */
    const SnapGrid& snapGrid() const { return m_snap; }
    bool snapMode() const { return m_snap.on; }
    /*  The render's progress bar with its label (bottom right of the viewport), in the viewport's pixels: empty when no picture is being drawn  */
    QRect renderBarRect() const { return m_renderBarArea.toAlignedRect(); }
    void setSnapMode(bool on);
    void setSnapOptions(bool show, float spacing, float angle, int distance, bool snapPoints);
    /*  The step of the grid now (the one the card gives, or what automatic works out to from how large the picture is)  */
    float snapStep() const;
    /*  Whether what is moved is snapped just now: the mode is on, the work plane view is not open, and Alt is not held  */
    bool snapActive() const;
    /*  `p` moved to the nearest node of the grid -- or onto a point that is there, when `toPoints` and one is within a few pixels --; unchanged when nothing is snapped just now  */
    QVector3D snapPoint(const QVector3D& p, const Shape* exclude = nullptr, bool toPoints = true) const;
    // (what a click puts down, and what is hovered: a point that is there, else the node of the lattice that the cursor is over -- the nearest to the viewer of those under it --, else the plane's)
    QVector3D snapPlace(QPoint pos, const Shape* exclude = nullptr, bool toPoints = true) const;
    QVector3D snapNodeAlongRay(QPoint pos, bool* hit = nullptr) const;
    bool surfacePointAt(QPoint pos, QVector3D& point) const;
    /*  The step of a turn in degrees: the card's, for the turns of the mode and for the Shift gizmo  */
    float angleStep() const { return qMax(0.1f, m_snap.angle); }
    /*  A click at `pos` on the work plane in the current mode: the next point of the line or the spline that is being drawn (the first point is where a click closes it
        into a profile), or a point of its own.  Nothing of a line or a spline is written until it ends: `finishPlacement` ends it (closed: a profile; open: the line or the
        spline), `cancelPlacement` drops it, `undoPlacedPoint` takes the last point back  */
    void placeAt(QPoint pos);
    void finishPlacement(bool close);
    void cancelPlacement();
    void undoPlacedPoint();
    int placedCount() const { return m_wpPlaced.size(); }
    /*  The symmetry: a model of the script -- `mirror = symmetry_planes(...)` -- that is selected in the model tree is the one the viewport works with.  Its planes are drawn, and
        what is made while it is IN USE is made at its images too (Shift+S turns that on and off; with no symmetry in the script one is made).  The card (SymmetryPanel) shows it
        and changes it: the arrangement of its planes is written into its call, where it is and how it is turned into the numbers of its gizmo  */
    const Symmetry& symmetry() const { return m_sym; }
    /*  The lines that show the symmetry model that is selected (0-based; none: no symmetry); `chosen`: it is another symmetry than before, which is to use it  */
    void setActiveSymmetry(QList<int> lines0, bool chosen);
    void setSymmetryInUse(bool on);
    void toggleSymmetry();
    Symmetry newSymmetryHere() const;                    // (what a new symmetry is: on the work plane while its view is open, else the mirror x = 0)
    void setSymmetryKind(int kind);
    void setSymmetryCount(int count);
    /*  The axis along a world axis (0 x, 1 y, 2 z), the work plane's normal (3), up in the view (4), or the normal of the selected 2D shape or surface (5)  */
    void setSymmetryAxis(int which);
    void setSymmetryAngle(float degrees);
    void setSymmetryCentre(const QVector3D& centre);
    /*  The centre at the origin (0), the middle of what is selected (1), the work plane's origin (2)  */
    void setSymmetryCentreFrom(int which);
    /*  The symmetry put where `target` says: its centre and its axes are the numbers of the gizmo of the model  */
    void placeSymmetry(const Symmetry& target);
    bool symmetryExists() const { return m_symExists; }
    bool symmetryInUse() const { return m_symInUse; }
    /*  How many images the symmetry makes of what is made (every element but the identity), while it is in use  */
    int symmetryImageCount() const { return int(m_symElements.size()); }
    /*  The images of a point: the point, then one for each element of the symmetry (for scripted tests)  */
    QList<QVector3D> imagesOf(const QVector3D& p) const { return symmetryImages(p); }
    /*  Where a new model made at `pos` goes, and how large it is: on the work plane while its view is open, snapped; else the place on the ray under the cursor.
        `createAt` makes one as the menu of empty space does (for scripted tests)  */
    void placementAt(QPoint pos, QVector3D& point, double& scale) const;
    void createAt(const QString& name, QPoint pos);

    /*  The field models of the script, after a run: every field has an icon in the scene (a card with its name over it, at the place of the
     *  object it was made from) while it is shown, and a viewer -- a disc of its field, of a radius, that can be taken anywhere -- while it is open  */
    void setFieldSources(QList<FieldEntry> fields);
    /*  The fields whose viewers are open (the ones selected in the model tree, or whose icons were clicked): by key (a variable's name, or
     *  "line:N").  A field that is not shown (its eye is off) has neither icon nor viewer  */
    void setOpenFields(QStringList keys);
    /*  The field the card is about (one of the open ones)  */
    void setActiveField(QString key);
    QString activeField() const { return m_activeField; }
    /*  The open viewers that are in the scene, by key  */
    QStringList openFieldKeys() const;
    /*  A viewer's settings and the render region (what the card's radius ranges over); false when there is none  */
    bool fieldViewer(const QString& key, FieldViewSettings& s, QVector3D& regionMin, QVector3D& regionMax) const;
    /*  The card changed a viewer: its axis, its radius, its opacity  */
    void setFieldView(QString key, FieldViewSettings s);
    /*  Where a field's icon is on the screen (for scripted tests); false when it is not in the scene  */
    bool fieldIconPoint(const QString& key, QPoint& out) const;
    /*  Where a part of the gizmo of an open viewer's disc is on the screen (for scripted tests: 1, 2 the arrows in its plane, 3 the dot, 4 the arrow along its normal); false when there is none  */
    bool fieldGripPoint(const QString& key, int part, QPoint& out) const;

    void toOrthographic() { camera.toOrthographic();  }
    void toPerspective()  { camera.toPerspective();   }
    void toTurnZ() { y_up = false; camera.toTurnZ();  }
    void toTurnY()  { y_up = true; camera.toTurnY();   }
    /*  Which axis is up, at once (the start of a session: no turn to animate)  */
    void setUpAxis(bool y) { y_up = y; camera.setUpAxis(y); update(); }
    void setLowRotSensitivity()  { camera.setRotationSensitivity(240); }
    void setMedRotSensitivity()  { camera.setRotationSensitivity(360); }
    void setHighRotSensitivity() { camera.setRotationSensitivity(720); }
    /*  Mouse gestures for other hands, an OPTION beside the ones everyone has (right drag pans, the wheel zooms, Shift + left drag and the middle button turn): the Blender style
        also pans with Shift + the middle button, the touchpad style with Alt + the left button.  The default is NAV_DEFAULT, and stays it  */
    void setNavigationPreset(int preset);
    /*  The gestures of the view, by name: for turning it, panning it and zooming it by dragging, a mouse button and the keys held with it ("none", "left", "shift+left", "ctrl+left",
     *  "alt+left", "middle", "shift+middle", "ctrl+middle", "alt+middle", "shift+right", "ctrl+right", "alt+right").  A gesture that is given to an action is that action's whatever it did
     *  before; the ones that are not given stay as everyone has them.  A plain left drag turns (or pans, or zooms) on empty space, and a click that does not move still selects  */
    void setNavigation(const QString& rotate, const QString& pan, const QString& zoom);
    QString navigationName(int action) const { return m_navKey[action]; }          // (0 turn, 1 pan, 2 zoom)
    /*  The gestures a dropdown offers: the name and the words  */
    static QList<QPair<QString, QString>> navigationGestures();
    void setZoomCursorCentric() { zoom_cursor_centric = true; }
    void setZoomSceneCentric() { zoom_cursor_centric = false; }
    void zoomTo() { camera.zoomTo(settings.min, settings.max); }
    void lookFrom(const QVector3D& dir) { camera.lookFrom(dir); }          // (the camera looks at the model from that side: a standard view, but of any side)
    void zoomToBox(QVector3D min, QVector3D max) { camera.zoomTo(min, max); }       // (the camera on a box, before the script that sets it has run)

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
    /*  The work plane changed (its view opened or closed, a new plane, its options): for its card ...  */
    void workPlaneStateChanged(FielDes::WorkPlane plane, float step);
    /*  Turning the view (Shift + left drag, the middle button) closed the work plane view that was open: the window says so, and how to get it back  */
    void workPlaneTurnedAway();
    /*  ... for the scene, which writes what is made on it in its axes: whether new models go on it, where it is, its axes, and the step the places snap to (0: none)  */
    void workPlaneChanged(bool active, QVector3D origin, QVector3D u, QVector3D v, float step);
    /*  The symmetry that is in use changed (its planes, its centre, in use or not), and how many images it makes: for the scene, which makes the images of what is made  */
    void symmetryChanged(FielDes::Symmetry symmetry, int images);
    /*  The symmetry that is selected, for its card: whether there is one, and whether it is in use  */
    void symmetryCard(FielDes::Symmetry symmetry, int images, bool exists, bool inUse);
    /*  A symmetry is to be made (Shift+S with none in the script): the scene writes it  */
    void symmetryCreateRequested(FielDes::Symmetry symmetry);
    /*  The arrangement of the symmetry that is selected is changed: the arguments of its call (`planes`, `kind`) and the text each gets  */
    void symmetryArguments(QStringList names, QStringList values);
    void symmetryNotice(QString text);
    /*  Where the cursor is on the plane, in its coordinates (empty: not on it)  */
    void workPlaneReadout(QString text);
    /*  The snapping mode changed (on or off, its options): for its card and its menu entry  */
    void snapChanged();
    /*  A line or a spline was put down (or a profile: closed): its name in the library (`profile`, `polyline`, `spline`), its points, and whether the outline is smooth  */
    void placementFinished(QString name, QList<QVector3D> points, bool smooth);

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
    /*  A model that was being dragged reached out of the render region and the region grew to hold it, at once, as it was dragged (auto scale): the region it grew to, said when
     *  the drag ends, so that it is written into the script with the numbers of the drag  */
    void regionGrown(QVector3D min, QVector3D max);

    /*  Meshing started (true) or every shape finished (false)  */
    void renderBusy(bool running);

    /*  A shape's render cache read or kept its mesh (see cacheStates)  */
    void cacheStatesChanged();

    /*  The error banner was clicked: the (0-based) line it is about, -1 when none is known  */
    void errorClicked(int line0);

    /*  The section plane was dragged along its normal by its handle in the viewport  */
    void sectionOffsetDragged(float offset);
    /*  The icon of a field was clicked (without dragging): the window selects the field, which opens its viewer; with Ctrl it joins the
     *  selection or leaves it  */
    void fieldIconClicked(QString key, bool ctrl);
    /*  The icon of a field was dragged: where it is now, and the viewer with it  */
    void fieldMoved(QString key, FieldViewSettings s);
    /*  Which fields are open, shown or the card's changed (the card follows)  */
    void fieldViewersChanged();

    /*  The distance field under the mouse on the section plane ("" = none)  */
    void sectionReadout(QString text);
    /*  The value of the field under the mouse on a field viewer's plane ("" = none)  */
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
     *  The same rectangle, for the fields whose icon lies inside it (a field is not a shape: it has no line that displays it, so it is named by its
     *  key, as in fieldIconClicked); with `add` they join the selection instead of replacing it
     */
    void fieldsRectSelected(QStringList keys, bool add);

    /*
     *  A point was pulled out of the outline of a profile or the line of a curve and let go: it is to be put in the script (the 0-based line that displays
     *  the model, where the outline was pressed, where the point was let go).  While it was pulled it was drawn by the view, from numbers, without the script
     */
    void outlineInsertRequested(int line0, QVector3D press, QVector3D at);
    /*
     *  The arc of a round corner was pressed, and the radius of its point is a plain number (`radius=5`): it is to be made a number that can be dragged (`radius=var(5)`) -- the
     *  0-based line that displays the profile, and the number of the point in its list.  The press does nothing else: the arc is not an edge to pull a point out of
     */
    void radiusVarRequested(int line0, int control);

    /*  A double click on the dot of a point of a profile or a curve: it is to change between a corner and a spline point (the 0-based line that displays it)  */
    void pointToggleRequested(int line0);

    /*
     *  The menu of a right-click on a shape was confirmed: select the surface of the shape displayed by
     *  the (0-based) line `line0` around `point` (a flood fill: mode "flat" or "smooth", the angle in
     *  degrees, the radius in mm, 0 = no limit)
     */
    void surfaceSelectRequested(int line0, QVector3D point, QString mode, double angle, double radius);
    /*  The menu of a right click on a body near an edge or a corner: a point at the corner ("corner"), the edge as a line ("edge") or a point on the edge ("edge_point")  */
    void featureRequested(int line0, QVector3D point, QString kind);

    /*
     *  An entry of a context menu was chosen: a new primitive (kind "primitive") placed at `point`, or an
     *  operation (kind "operation") on the model displayed by the (0-based) line `line0`, or, for -1, the
     *  selected one; `scale` is how many mm about a hundred pixels span at that place
     */
    void createRequested(QString kind, QString name, QVector3D point, double scale, int line0, int generation);

    /*  "Import model..." was chosen in the menu of empty space: the window asks for the file  */
    void importRequested();

protected slots:
    void update() { QOpenGLWidget::update(); }
    void redrawPicker();

    /*  Starts (debounced) re-sampling of the section field, and of the field viewer's disc  */
    void requestSlice();

protected:
    void initializeGL() override;
    /*  The graphics context of the viewport is ending (it is pulled out into a window of its own, or put back: a new window is a new context): what the graphics card held for it --
     *  the buffers of every shape, the textures of the planes, the picking buffer, the programs -- is let go of, and made again at the next paint  */
    void releaseGL();
    bool m_gl_ending = false;           // (the viewport is being destroyed: the context ending then is the destructor's to deal with)
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

    /*  The dot of a point (the middle point of a gizmo, always drawn), the point whose dot is at a widget position, and the names of an app  */
    void drawPointDots(QPainter& painter);
    Shape* pointDotAt(QPoint pos) const;
    /*  The outlines of profiles and the lines of curves, drawn blue: the one the mouse is over is brighter  */
    void drawOutlines(QPainter& painter);
    /*  What a click or a double click does where the mouse is, in a few words beside it (on the outline of a profile or a curve, on the dot of one of
        their points)  */
    void drawHints(QPainter& painter);
    /*  The editable outline (of a profile, a curve) that passes under a widget position (within a few pixels): its shape, the point of it that is
        there, and the number of the piece of the line (counted over all its polylines)  */
    bool outlineAt(QPoint pos, Shape*& shape, QVector3D& hit, int& piece) const;
    /*  One line of an outline as it is drawn and as a press is looked for on it: its vertices, and between the ends of the arc of a round corner not the pieces the face is cut
        along but points of the arc itself, as many as the screen tells apart (so the arc is the circle to a twentieth of a pixel at any zoom).  `piece` is the number of the piece
        of the line that starts at the point (all of an arc has the number of its first piece); `pieceBase` is the number of the first piece of this line  */
    struct OutlinePoint
    {
        QVector3D world;
        QPointF screen;
        bool front = false;                     // (in front of the camera: `screen` is only there then)
        int piece = 0;
    };
    void outlineLine(const Shape* s, size_t line, int pieceBase, std::vector<OutlinePoint>& out) const;
    Shape* m_outlineHover = nullptr;
    bool m_outlineArcHover = false;             // (what is hovered is the arc of a round corner: a handle for its radius, not an edge to pull a point out of)
    /*
     *  The arc of a round corner is the HANDLE of its radius: pressed and dragged along the way the corner looks, it sets the point's radius (a var(): the number the point itself
     *  gives) -- pulled in it is rounder, pushed out towards the corner it is sharper -- and it is limited by what the geometry allows.  It runs as a gizmo drag of kind 7 (the
     *  numbers are written into the script as they are dragged, the script runs when the mouse is let go).  `s0` is where the press was, along the bisector from the corner
     */
    struct FilletDrag
    {
        Shape::FilletHandle h;
        Shape* shape = nullptr;
        double s0 = 0, apex0 = 0;
    } m_fillet;
    void beginFilletDrag(Shape* shape, const Shape::FilletHandle& h, const QVector3D& hit);
    void dragFillet(QPoint pos);
    /*  A press on the outline of a profile or the line of a curve: the point that is pulled out of it (a click on a curve selects the curve: its line is all there is
        of it, so a drag is what pulls a point out), and a press on the face of a profile that drags all its points in its plane  */
    Shape* m_pullShape = nullptr;
    bool m_pullActive = false;
    int m_pullAfter = 0, m_pullPiece = 0;
    QVector3D m_pullHit, m_pullNormal, m_pullLast;
    QPoint m_pullPressPos;
    Shape* m_faceShape = nullptr;
    bool m_faceActive = false;
    QVector3D m_faceNormal, m_faceStart;
    QVector3D m_faceWallDir;                    // (a wall of a prism pulled by its two points: the direction in the plane that it moves along; null for the face of a profile, which moves every point as the mouse does)
    QPoint m_facePressPos;
    /*  A press on the dot of a point that has its numbers (its gizmo) begins its drag at once, with no selection and no gizmo needed.  Until the mouse has moved a few pixels
        it is still a click, which selects the point  */
    int m_dotPressLine = -1;
    bool m_dotMoved = false;
    QPoint m_dotPressPos;
    /*  The copy of a point that was pressed (the dot of a placed copy, of an image in a symmetry): the row of the point it is a copy of, and which of the copies of that point it is, in
        the order they are drawn.  The point is selected by the press (it is the original's row that is selected), and that copy -- and only that one -- shows the point's gizmo (its arrows,
        its ring, the ring that sizes its influence), at the copy, taken through the map of the copy; dragging it changes the original's numbers  */
    int m_copyLine = -1;
    int m_copyNth = 0;
    void applyActiveCopy();
    int copyIndexOf(const Shape* dot) const;
    /*  Auto scale while a model is dragged: when the picture of it (the mesh, moved by the numbers of the drag) reaches out of the render region, the region grows to hold it
     *  -- with the room and the whole numbers the script's region has -- and what is shown is meshed again in it at once; the region it has grown to is what is written into the
     *  script when the drag ends (regionGrown)  */
    void growRegionForPreview();
    /*  The same for a surface that is pulled (the picture of it is meshed again from its numbers at every move, over the region): the region holds the point it is pulled to  */
    void growRegionForSurfaceDrag(const QVector3D& at, const std::map<libfive::Tree::Id, float>& vs);
    /*  The region grown to hold a box of the models (with the room and the whole numbers of the script's region), and what is shown meshed again in it  */
    void growRegionToHold(const double nlo[3], const double nhi[3]);
    bool m_regionGrown = false;
    QVector3D m_grownMin, m_grownMax;
    struct FaceVar { libfive::Tree::Id id; int axis; float value0; };
    QList<FaceVar> m_faceVars;
    void beginPull(QPoint pos);
    /*  The point that is pulled out of an outline, drawn at once on every shape that stands for that outline (the outline, its placed copies, its images in a symmetry)  */
    void setPullVirtual(const QVector3D& at);
    Shape* controlOwner(const Shape* point, int& index) const;
    void drawNameLabels(QPainter& painter);
    bool m_appMode = false;
    Shape* m_dotHover = nullptr;

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
    void keyPressEvent(QKeyEvent* event) override;

    /*  A right-click (not a pan) on a shape, or on its row in the model tree or a line of the text editor (`onSurface` false: no place on the surface was
     *  clicked): Primitives, Parametric shapes, Operations (the operations on that shape, Lattice, Simulation -- the same as the menu of empty space has), the quick
     *  operations (center, bounding_box), Import model... and Select Surface, which opens the menu of the surface selection (it needs the place that was clicked)  */
    void showSurfaceMenu(QPoint globalPos, int line, const QVector3D& point, double scale, bool onSurface = true);
    void showSelectMenu(QPoint globalPos, int line, const QVector3D& point);
    /*  A right-click (not a pan) on empty space: Primitives (a 3D shape, a 2D shape, a line, a point, a surface, a field: the most basic of each), Parametric shapes (the
     *  more complicated ones, made with parameters), Operations (the operations and Lattice), Engineering (a category for each domain -- Structural, Thermal, Fluid -- with
     *  what its analyses are given and the analyses, and the materials) and Import model...  `atLine` (from the text editor): what is made is written under that line  */
    void showEmptyMenu(QPoint globalPos, QPoint pos, int atLine = -1);
    /*  What the Operations menu holds, on the model at `line` or on empty space (-1): the operations and Lattice  */
    void fillOperationsMenu(QMenu* ops, int line, const QVector3D& point, double scale, int atLine);
    /*  What the Engineering menu holds: a category for each domain (Structural, Thermal, Fluid, each with the icon and the colour of its domain), each with what an analysis of
     *  it is given and its analyses and optimisations; then the materials, which an analysis of any domain takes  */
    void fillEngineeringMenu(QMenu* menu, int line, const QVector3D& point, double scale, int atLine);
    /*  The "New ..." entries of the menus of empty space and of a model: `basic` the primitives, else the Parametric shapes (all the rest).  `point` is where a shape is made, `exact` where a point is
     *  (under the cursor, as it is)  */
    void addCreateEntries(QMenu* menu, const QVector3D& point, const QVector3D& exact, const QVector3D& flat, double scale, double flatScale, int atLine, bool basic);
    /*  The menu that is being built: the line of the model it is about (-1: the menu of empty space).  On a model, a new model of the menu is greyed out when it does not fit the model
     *  (a line is made of points, a material is something a simulation takes ...: the library says -- see ScenePanel::entryAllowed); a thing made by number is placed anywhere  */
    int m_menuModelLine = -1;
    void checkPrimitive(QAction* action, const QString& name, bool withBodies, int atLine) const;
    QVector3D placementExact(QPoint pos) const;
    /*  Where a 2D shape that is written with an x and a y only (it lies in the plane z = 0) is made by a click: where the ray under the cursor meets that plane, whichever way the view
        looks (on the work plane, while its view is open)  */
    QVector3D placementFlat(QPoint pos, double& scale) const;
    bool flatPrimitive(const QString& name) const;
    /*  The operations as a menu's entries (the combining ones greyed when there is no second model); the analyses are
     *  in the Engineering menu: `simulations` says which of the two is listed, and `domain` (structural, thermal, fluid) which analyses of them  */
    void fillOperations(QMenu* menu, int line, const QVector3D& point, double scale, bool simulations = false, const QString& domain = QString());
    /*  The lattice section of a menu: the operations that fill a model with a lattice (and make points for it), then a menu for each family of
     *  cells -- struts, TPMS, planar patterns, non-periodic, custom -- each entry making that cell at once (no model is needed)  */
    void fillLatticeMenu(QMenu* menu, const QVector3D& point, double scale, int line, const QString& section);
    /*  The Python menu: the most basic features of the language -- a variable, if, loops, a function, a class ... -- as blocks of plain code written at the insertion point of the
     *  model tree (the catalog's `python` list; no model is made of them)  */
    void fillPythonMenu(QMenu* menu, const QVector3D& point, double scale, int line);
    /*  The entry that makes a body from the field of the model at `line` (-1: the selected field the viewer shows)  */
    void addBodyFromField(QMenu* menu, int line, const QVector3D& point, double scale);
    /*  New material, New fluid, New support or load, New thermal condition and New flow condition, as the menus of the Engineering
     *  menu: what an analysis is given, made at a place like any new model.  `domain`: the ones of that domain (empty: those of no domain, the materials).  `only`: just
     *  those groups ("Fluids", "Flow conditions" ...); `skip`: all but those  */
    void fillSimulationPrimitives(QMenu* menu, const QVector3D& point, double scale, int atLine, const QString& domain,
                                  const QStringList& only = QStringList(), const QStringList& skip = QStringList());
    /*  Where a primitive goes: the point of the ray under a widget position that is closest to the origin  */
    QVector3D placeOnRay(QPoint pos) const;
    /*  The work plane's state and its pieces in the viewport  */
    WorkPlane m_wp;
    int m_wpLastMode = 1;                   // (the kind of drawing that was on last: E brings it back after a right click has stopped the drawing)
    QVector3D m_wpCursor;                    // (where the cursor is on the plane, snapped)
    QList<QVector3D> m_wpPlaced;             // (the points of the line or spline that is being put down: not in the script yet)
    // The symmetry (see setActiveSymmetry)
    bool symmetric() const { return m_sym.on; }
    Shape* symmetryShape() const;                               // (the model that is selected and is a symmetry)
    void refreshSymmetry();                                     // (what the model says now: its planes, the card, the scene)
    void linkFollowers();                                       // (the images and linked copies that are shown follow the mesh of the model they are of, when that is shown)
    /*  The images of a point: the point, then one for each element of the symmetry  */
    QList<QVector3D> symmetryImages(const QVector3D& p) const;
    bool onSymmetryPlane(const QVector3D& p) const;             // (the point is on one of the planes of the symmetry)
    /*  The strokes a stroke has in the symmetry: the stroke itself, then its images (the mirror ones in the reverse order, so that they face the same way); an image
        that is the stroke (or another image) over again is left out  */
    QList<QList<QVector3D>> symmetryStrokes(const QList<QVector3D>& stroke) const;
    void drawSymmetry(QPainter& painter);
    Symmetry m_sym;                                             // (the symmetry that is in use: `on` while it is)
    QList<SymmetryElement> m_symElements;                       // (every image but the identity, of the symmetry in use)
    QList<int> m_symLines;                                      // (the lines that show the symmetry model that is selected)
    bool m_symExists = false;
    bool m_symInUse = true;
    QVector3D m_faceRef;                     // (the point of a profile that its drag keeps on the grid)
    bool m_faceHasRef = false;
    bool m_wpCursorValid = false;
    bool m_wpCursorSnapped = false;
    void makeWorkPlane();
    void announceWorkPlane();
    QRectF m_renderBarArea;                  // (where the render's progress bar and its label were drawn last, in the viewport's pixels; empty when no render is going on: the tour frames them while a calculation goes on)
    SnapGrid m_snap;                         // (the snapping mode: see snapGrid)
    bool m_snapResume = false;               // (the mode was on when the work plane view opened: it comes back when that is left)
    void announceSnap();
    void syncSnapWithWorkPlane();
    void drawSnapGrid(QPainter& painter);
    void drawSnapMark(QPainter& painter);
    /*  The lattice of the snapping mode as a picture of the viewport (transparent, in device pixels): false when its lines are too fine to draw from here.  The lattice is seen out to the
     *  viewing distance of the card -- measured from the viewer, in the units of the view, so that it is the same picture whatever the zoom and wherever the view is panned  */
    bool renderSnapLattice(QImage& image, qreal ratio, int& stride) const;
    QImage m_snapImage;                      // (the lattice as it was drawn last: the cursor moves over it all the time, the camera does not)
    QMatrix4x4 m_snapImageM;                 // (the camera it was drawn for)
    float m_snapImageStep = 0;               // (and the step, the viewing distance, the size of the viewport and its pixel ratio)
    int m_snapImageDistance = 0;
    QSize m_snapImageSize;
    qreal m_snapImageRatio = 0;
    bool m_snapImageKept = false;            // (the numbers above are those of a picture that was made: it is made again only when one of them changes)
    bool m_snapImageDrawn = false;           // (and the lattice was drawn in it)
    // The nearest and the farthest rows of the lattice that was drawn, as shares of the way from the viewer to the place looked at (a click goes to a node between them), and the mark of where it snaps to
    mutable float m_snapSlabNear = 0.25f, m_snapSlabFar = 1.15f;
    mutable bool m_snapSlabValid = false;
    mutable QVector3D m_snapStick;                  // (the node that was marked last: it stays marked while the cursor is near it)
    mutable bool m_snapStickValid = false;
    struct SnapMark { bool valid = false; QVector3D at; bool onPoint = false; };
    SnapMark m_snapMark;
    void setSnapMark(const QVector3D& at, bool onPoint);
    void clearSnapMark();
    void updateSnapHover(QPoint pos);
    int m_snapImageStride = 1;               // (every how manieth line of the lattice was drawn: 1 all of them, 5 every fifth -- when the lines would be too close to see -- the label says so)
    bool altHeld() const;
    /*  The dot of a point that is within a few pixels of `p` on the screen (not `exclude`, the one that is moved): where it is  */
    bool pointNear(const QVector3D& p, const Shape* exclude, QVector3D& where) const;
    /*  A move of `distance` along the unit vector `dir` from `from`, snapped: along an axis of the world the place it lands on is a line of the grid, any other way the distance is whole steps  */
    float snappedAlong(const QVector3D& from, const QVector3D& dir, float distance) const;
    bool planeOfSelection(QVector3D& origin, QVector3D& u, QVector3D& v) const;
    void planeOfView(QVector3D& origin, QVector3D& u, QVector3D& v) const;
    void updateWorkPlaneCursor(QPoint pos);
    void drawWorkPlane(QPainter& painter);
    void wpMarker(QPainter& painter, const QPointF& q, int mode, double alpha, bool lit) const;
    /*  The grid's step as a round number of about `want` mm  */
    static float roundStep(double want);
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
        if (m_wpCursorValid)
        {
            m_wpCursorValid = false;
            emit(workPlaneReadout(QString()));
            m_overlay->update();
        }
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
        enum { RELEASED, DRAG_ROT, DRAG_PAN, DRAG_EVAL, DRAG_RECT, DRAG_ZOOM } state = RELEASED;
        QPoint pos;
    } mouse;

    /*  The selection rectangle (left drag; Shift + left drag and the middle button turn the view): where it began and where the mouse is now, in widget pixels  */
    QPoint rect_start, rect_now;
    bool rect_add = false;
    /*  A Ctrl+click toggles the selection instead of replacing it  */
    bool press_ctrl = false;
    /*  The 0-based lines of the shapes whose whole mesh lies inside a rectangle of the widget  */
    QList<int> linesInside(const QRect& r) const;
    /*  The keys of the fields whose icon is inside a rectangle of the widget  */
    QStringList fieldKeysInside(const QRect& r) const;

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
    int m_navPreset = NAV_DEFAULT;
    struct NavBinding { int button = 0; int modifiers = 0; };       // (a mouse button and the keys held with it: none is button 0)
    NavBinding m_navBind[3];                                        // (turn, pan, zoom)
    QString m_navKey[3] = {QStringLiteral("none"), QStringLiteral("none"), QStringLiteral("none")};
    int m_navLeft = 0;                                              // (what a plain left drag does on empty space: 0 nothing of the kind, 1 turn, 2 pan, 3 zoom)
    bool navigationPress(QMouseEvent* event);
    bool zoom_on_next_settings = false;

    /*  Framebuffer to render pick data  */
    QScopedPointer<QOpenGLFramebufferObject> pick_fbo;
    QTimer pick_timer;
    QImage pick_img;
    QVector<float> pick_depth;
    // (the same without the 2D shapes, which are drawn over the bodies: what is under a 2D shape where a press is not at its edge, and how deep -- the end face of an extrusion at its base, under the shape it was made of.
    // Empty when no 2D shape is shown)
    QImage pick_img_bodies;
    QVector<float> pick_depth_bodies;

    /*  Data to handle direct modification of shapes */
    QVector3D drag_start;
    QVector3D drag_dir;
    std::map<libfive::Tree::Id, float> drag_weights;       // (what resists the drag of the surface that is held: see Shape::dragWeights)
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
    /*  An optimisation that is running: the kernel puts a picture of the design after every iteration (run_progress::liveView) -- the
     *  shapes its result is made of (the part cut from the density, the fluid and the body ...) -- and the view shows them as it shows
     *  the result: they are made into shapes, rendered like any others, and put in place of the ones shown when they have their
     *  meshes (what is shown stays until then).  A card says how it goes: the iteration, the objective (is it falling?), the material
     *  against what is asked for, and how much the design still moves.  What the run makes replaces them when it is done  */
    QTimer m_liveTimer;
    std::shared_ptr<const libfive::run_progress::LiveView> m_live;
    QList<Shape*> m_livePending;                // the shapes of the picture being meshed
    bool m_liveDrawn = false;                   // (the shapes shown are the live picture's)
    bool m_liveWaiting = false;                 // a newer picture has been published while those were meshing: it is next
    QVector3D m_liveMin, m_liveMax;             // the region they are rendered over
    std::function<QString()> m_liveSettings;    // the render settings the script has given so far (JSON), asked for while it runs
    std::function<libfive::Tree(const QString&)> m_exactResolver;
    std::shared_ptr<QMutex> m_exactMutex = std::make_shared<QMutex>();
    std::shared_ptr<QHash<QString, libfive::Tree>> m_exactTrees = std::make_shared<QHash<QString, libfive::Tree>>();
    void pollLive();
    void startLive();
    void swapLive();
    LivePanel* m_livePanel = nullptr;           // (the card of the optimisation that runs: a card like the others)
    void placeLivePanel();
    /*  The entries of the first menu of a body that are used all the time -- center, bounding_box: not under a group  */
    void addQuickOperations(QMenu* menu, int line, const QVector3D& point, double scale);
    /*  The Export submenu of the menu of a model: its mesh in every mesh format, a field on a grid, an analysis as a finite-element model and as results
     *  -- what the model can be exported as; the window asks where the file goes  */
public:
    void setLiveSettings(std::function<QString()> probe) { m_liveSettings = std::move(probe); }
    /*  Where a field viewer gets the exact distance of an imported part (asked for by FieldSource::exactKey, from the sampling's own thread)  */
    void setExactResolver(std::function<libfive::Tree(const QString&)> resolver) { m_exactResolver = std::move(resolver); }
private:
    int m_playMode = 0;                 // 0 loop, 1 back and forth, 2 once (stop at the end)
    float m_playSpeed = 1.0f;           // steps per second = 10 x this
    float m_flowOpacity = 1.0f;         // how opaque a flow is drawn (the result card's slider): the body in it shows through a lower one
    int m_playDir = 1;                  // (back and forth: which way)
    /*  What was chosen on the result card, kept by the view and not by the shapes: every new result shape -- the result of the next run,
     *  and every picture of an optimisation that runs (a new one after every iteration) -- is made to look as the one before it did, so that
     *  what you set (the field shown, the magnification, the opacity of a flow, the elements, the streamlines) stays while the design
     *  changes.  -1: not chosen (the shape's own)  */
    QString m_choiceChannel;            // the name of the field (not its position: another result has other fields)
    float m_choiceDeform = -1.0f;       // the magnification, as a multiple of the result's own automatic one
    int m_choiceElements = -1;
    int m_choiceLines = -1;
    void applyViewerChoices(Shape* s) const;

    /*  For click-to-select: where the press happened and what it hit  */
    QPoint press_pos;
    Shape* press_target=nullptr;

public:
    ScenePanel* scenePanel() const { return m_scene; }
    /*  The entries of File > Export model: what can be written of the model displayed on `line` (0-based) to a file -- a mesh of its surface, a field on a grid, the finite-element
     *  model and the results of an analysis, a mesh for another solver.  A file is not made by a menu of the viewport: those write code  */
    void addExportEntries(QMenu* into, int line);
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
        QScopedPointer<QOpenGLTexture> lutTex;   // the colours of the model's own colour map, a table of 1024 made from colormapColor (the plane shows what the surface shows)
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
        bool runningFine = false;          // (the pass in flight is a fine one)
    };

    /*  Section view state: the plane that cuts the models and shows the distance field on them  */
    SectionSettings section;
    Plane m_sec;

    /*  A shape's fields as the plane samples them (copied for the worker thread)  */
    FieldSource sourceOf(Shape* s) const;

    /*  A field's viewer: its icon, and its plane while it is open.  Every field has its own, with its own plane and sampling.  */
    struct FieldViewer
    {
        QString key;
        FieldSource source;
        bool shown = true;                       // its eye: icon and viewer are in the scene
        bool hasAnchor = false;
        QVector3D anchor;                        // where the icon starts: the place of the object the field was made from
        bool open = false;                       // the viewer's disc is open
        bool moved = false;
        QVector3D centre;                        // the middle of the disc, once it was moved by hand (else the field's anchor); the disc is at centre[axis]
        int axis = 2;                            // the disc faces along: 0 = X, 1 = Y, 2 = Z
        float radius = 0;                        // 0: not chosen: half the render region
        float opacity = 0.9f;
        bool rangeKnown = false;
        QPair<float, float> range;               // the colours of the field, found when it is first shown and kept until the script runs again
        Plane plane;
    };
    /*  A part of the gizmo of an open viewer's disc: the viewer and the part -- 1, 2 the arrows in its plane, 3 the dot in the middle, 4 the arrow along its normal  */
    struct FieldGrip
    {
        FieldViewer* v = nullptr;
        int part = 0;
        bool operator==(const FieldGrip& o) const { return v == o.v && part == o.part; }
    };
    std::map<QString, std::unique_ptr<FieldViewer>> m_viewers;
    QString m_activeField;
    FieldViewer* viewer(const QString& key) const;
    QStringList m_wantedOpen;            // (the fields that are selected: their viewers open when the field is there)
    void settleActive();
    void initViewer(FieldViewer* v);
    void retireViewer(FieldViewer* v);
    /*  Where a viewer's disc is (its middle: its icon is beside it), and the disc as the settings of a plane  */
    QVector3D fieldPlace(const FieldViewer& v) const;
    SectionSettings fieldPlane(const FieldViewer& v) const;
    FieldViewSettings fieldSettings(const FieldViewer& v) const;
    void requestFieldSlice(FieldViewer* v);
    void emitActiveSlice();
    /*  The icons of the fields, with their names; the icon under a widget position; the drag of an icon (in the plane that faces the eye)  */
    void drawFieldIcons(QPainter& painter);
    FieldViewer* fieldIconAt(QPoint pos) const;
    QVector3D rayHitFacing(QPoint pos, const QVector3D& through) const;
    void dragFieldIcon(QPoint pos);
    FieldViewer* m_iconPress = nullptr;
    FieldViewer* m_iconHover = nullptr;
    bool m_iconDrag = false;
    bool m_iconCtrl = false;
    QPoint m_iconPressPos;
    QVector3D m_iconGrab;
    QPointF m_iconOffset;                 // (how far the icon that is held was drawn from its place when it was taken: it keeps that while it is dragged)
    /*  Where the icons are drawn: at the place of the field, moved aside when the dot of a point or another icon is there, so that the dot can be clicked  */
    QHash<QString, QPointF> iconLayout() const;
    /*  The radius of a viewer's disc: the one chosen, else half of the render region  */
    float fieldRadius(const FieldViewer& v) const;
    /*  The gizmo of an open viewer's disc, the arrow along its normal the section's has, two more in its plane, and a dot in the middle that moves it freely in the plane: the
     *  screen points of its middle and of the tips of the two arrows (along the two axes of the plane) and of the one along the normal; what part of it a widget position is on
     *  (0 nothing, 1 / 2 an arrow in the plane, 3 the dot, 4 the arrow along the normal); its drawing; the drag of a part -- nothing keeps the disc inside anything  */
    bool fieldGizmo(const FieldViewer& v, QPointF& middle, QPointF& tipU, QPointF& tipV, QPointF& tipN, float& length) const;
    int fieldGizmoHit(const FieldViewer& v, QPoint pos) const;
    void drawFieldGizmo(QPainter& painter, const FieldViewer& v);
    FieldGrip fieldGripAt(QPoint pos) const;
    void beginFieldGrip(const FieldGrip& g, QPoint pos);
    void dragFieldGrip(QPoint pos);
    FieldGrip m_fieldGrip;                // (the one under the mouse)
    FieldGrip m_fieldGripActive;          // (the one that is being dragged)
    bool m_fieldGripDrag = false;
    QPoint m_fgPress;
    QVector3D m_fgStart;
    QPointF m_fgDir;
    float m_fgLen = 0;
    QVector3D m_fgGrab;                   // (the dot: how far the middle of the disc was from where the line of sight met its plane when it was taken)
    bool activeFieldOpen() const;
    void startSliceAt(Plane& plane, bool fine, FieldViewer* viewer = nullptr);
    void onSliceFinished(Plane& plane, FieldViewer* viewer = nullptr);
    void setClipUniform(bool on);
    void drawSlicePlane(const QMatrix4x4& m, Plane& plane, const SectionSettings& at);

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

    QPointF toScreen(const QVector3D& p) const;

    /*  The gizmo of placed parts (Shape::gizmo, fieldes.stdlib.handles): move
     *  arrows along the axes, rotation rings and scale knobs at the part's pivot,
     *  dragged to change the script's numbers.  kind 0 = move, 1 = rotate,
     *  2 = scale (Shift: all three axes together)  */
    struct GizmoGrip
    {
        Shape* shape = nullptr;
        int kind = -1;              // 0 move arrow, 1 rotation ring, 2 scale knob, 3 the dot in the middle (free move), 4 a plane square (Shift: move along a plane), 5 the ring that scales a group of points, 6 the ring that turns it
        int axis = 0;
        bool group = false;         // the gizmo shared by all the selected shapes (kinds 0, 3 and 4 only)
        bool operator==(const GizmoGrip& o) const
        { return shape == o.shape && kind == o.kind && axis == o.axis && group == o.group; }
    };
    /*  Two or more shapes are selected (the multi-select state): one gizmo, at the middle of the pivots of the ones that
     *  have numbers to move them by, moves them all, whatever way of editing each of them is in when it is alone (its
     *  arrows and its dot; turning and scaling are one shape at a time).  Gives them and the gizmo's place; false when
     *  fewer than two are selected (every shape then has its own gizmo, as usual)  */
    bool groupGizmo(QList<Shape*>& members, QVector3D& center) const;
    /*  How many shapes are selected (lit up as the model tree's selection)  */
    int selectedShapeCount() const;
    /*  The numbers a gizmo drag changes: each shape's move number for an axis, with its value when the drag began  */
    struct GizmoVar
    {
        libfive::Tree::Id id;
        int axis;
        float value0;
        float weight = 1.0f;            // (how much of the move along the arrow this number takes)
        // (the number of the original of an image: it changes by `w` . what the image moved by -- the original moves as the image does, taken back through the symmetry)
        QVector3D w;
        bool mapped = false;
    };
    QVector3D gizmo_arrow;              // (the way a move arrow points, in the world)
    float gizmo_sign = 1.0f;            // (a ring of an image turns the original the other way when the image is a mirror image)
    QVector<GizmoVar> gizmo_vars;
    /*  Several points are selected: they share one gizmo, and two rings of it scale and turn them -- evenly, about the middle of them all, which moves each of them (it is the
        numbers that move each that change).  Each point's place and its move numbers when a drag of a ring began  */
    bool groupOfPoints(const QList<Shape*>& members) const;
    /*  Whether a profile or a line (a shape drawn from the numbers of its points) is selected as a whole: its points then join the shared gizmo, which has the arrows in all three
        directions and rings that scale and turn the ARRANGEMENT of the points about the middle of them (selected as points alone, the rings change each point's own numbers)  */
    bool outlineSelected() const;
    QList<const Shape*> outlinesWithGizmo() const;
    bool gizmo_group_outline = false;      // (the drag of a ring of the shared gizmo that is going on is the outline's: the places of the points change)
    QVector3D groupRingNormal(const QList<Shape*>& members) const;     // (the axis they turn about: the plane of their profile, else the work plane's, else the way the camera looks)
    // (the place of a point of a group and the numbers that move it; and the numbers of its own scale and rotation: the group's rings change those, point by point)
    struct GroupPoint { QVector3D pos0; libfive::Tree::Id id[3]; float value0[3]; libfive::Tree::Id scaleId[3]; float scale0[3]; libfive::Tree::Id rotId[3]; float rot0[3]; };
    QVector<GroupPoint> gizmo_group;
    QVector3D free_hit0, free_normal;   // a free move: where the cursor's ray met the plane at the start, and the plane's normal
    GizmoGrip gizmo_hover;
    // The provisional gizmo (see setProvisionalGizmo), and a press on it that waits for the real one
    bool m_prov = false;
    QVector3D m_provPivot;
    QList<int> m_provLines;
    struct WaitGrip { bool on = false; int kind = -1, axis = 0, ticks = 0; QList<int> lines; bool selected = false; };   // (selected: the one point that is selected, wherever its line is by then)
    WaitGrip m_wait;
    QTimer m_waitTimer;
    bool provisionalVisible() const;
    bool provisionalGripAt(QPoint pos, int* kind, int* axis) const;
    void drawProvisional(QPainter& painter);
    void startWaitingGrip();
    void cancelWaitingGrip();
    GizmoGrip gizmo_active;
    bool gizmo_drag = false;
    libfive::Tree::Id gizmo_id = nullptr;
    float gizmo_value0 = 0;            // the number when the drag began
    QPoint gizmo_press;
    QPointF gizmo_dir;                 // a move arrow on the screen
    float gizmo_len = 0;               // ...and its length in the model
    bool gizmo_uniform = false;        // the scale handle being dragged is the ring of a point or of the Shift gizmo: it scales all three numbers together
    float gizmo_factor = 1.0f;         // ...and the factor it has scaled by so far (shown beside it)
    QPointF gizmo_centre;              // ...the middle of the ring on the screen, and how far from it the ring was pressed
    double gizmo_press_r = 1.0;
    QVector3D gizmo_pivot, gizmo_axis, gizmo_vec;   // a ring's
    float gizmo_turn = 0;              // degrees turned so far
    float gizmo_scale0[3] = {1, 1, 1}; // the scale numbers when a scale drag began
    libfive::Tree::Id gizmo_scale_id[3] = {nullptr, nullptr, nullptr};
    float gizmoLength(const QVector3D& pivot) const;
    GizmoGrip gizmoAt(QPoint pos) const;
    /*  Shift held: the move arrows are replaced by flat squares (a drag moves along the plane of the square) and the scale knobs all scale together  */
    bool shiftHeld() const;
    /*  The turn of a ring (degrees) as it is applied: with Shift held, in steps of 5 degrees (`absolute`: the angle `start + turn` is a multiple of 5)  */
    float snappedTurn(float turn, float start = 0.0f, bool absolute = false) const;
    QPoint m_lastPos;                    // (where the last mouse event of the viewport was)
    bool m_leftDown = false;             // (the left button, as the mouse events of the viewport say: a script that sends events sets it as a real mouse does)
    /*  A point of a profile has ONE rotation ring, in the plane of the profile (about its normal): whether `s` is one, and that normal  */
    bool planarRingNormal(const Shape* s, QVector3D& normal) const;
    /*  The axes the arrows, the plane squares and the rings of the gizmo of `s` are along.  The world's, except for a point of a curve: its blue axis is the way the curve faces
        there and the red and green are across it, so that the ring about the blue axis twists what is swept along the curve and the other two tilt where the curve goes (the
        numbers are the same ones: a turn about these axes is written as the three angles of the gizmo).  True when it is turned.  While the gizmo is dragged the axes are the
        ones it was pressed with  */
    bool gizmoFrame(const Shape* s, QVector3D F[3]) const;
    bool gizmo_oriented = false;                // (the drag in progress is along the axes of a turned gizmo, noted at its start)
    const Shape* gizmo_frame_shape = nullptr;
    QVector3D gizmo_frame[3];
    QVector3D gizmo_turn_axis;                  // (the axis of a ring of a turned gizmo as the ORIGINAL has it: for a copy of a point, the axis taken back through the map of the copy)
    libfive::Tree::Id gizmo_rot_id[3] = {nullptr, nullptr, nullptr};          // ...and the rotation numbers of the shape, with the values they had at the start
    float gizmo_rot0[3] = {0, 0, 0};
    QPolygonF planeSquareIn(const QVector3D& pivot, float length, const QVector3D& eb, const QVector3D& ec) const;
    /*  Whether all of `members` are points of profiles that lie in one plane -- selected together they have the dot that moves them in that plane, and no arrows  */
    bool groupInProfilePlane(const QList<Shape*>& members, QVector3D& normal) const;
    /*  What a point's dot says: 1 a corner (a square), 2 a spline point (a round dot with a ring), 0 neither (a plain round dot: it belongs to no profile or curve).
        Taken from the profile or curve that owns the point -- what it IS, whenever it was made -- and, for a point that has no gizmo to be found by, from the mark
        the library made on it  */
    int pointStyleOf(const Shape* s) const;
    /*  Shift or Ctrl + the mouse wheel scales what the mouse is on, or what is selected: the point whose dot is under `at` (selected or not), else every selected shape or point that has
        scale numbers.  One gesture, written to the script when the wheel has rested a moment  */
    bool scaleSelectionByWheel(int delta, QPoint at);
    bool applyWheelFactor(float factor, Shape* only);      // (the scale numbers of `only`, or of every selected shape, times `factor`; false when none of them has numbers)
    void waitForWheelNumbers(float factor);                // (a notch over a point that has no numbers yet is kept, with those that follow, until the run that writes them is over)
    void applyPendingWheel();
    bool m_wheelScaling = false;
    QTimer m_wheelTimer;
    float m_wheelPending = 1.0f;
    int m_wheelPendingTries = 0;
    QTimer m_wheelPendingTimer;
    bool m_shiftSeen = false;            // (what the last key event said, to redraw the gizmo when it changes)
    bool m_shiftForced = false;          // (the automation holds Shift: synthetic events do not change the keyboard)
    bool m_shiftWas = false;             // (a real Shift key went down since the tour last asked: see shiftWasDown)
    /*  The square, on the screen, of the plane whose normal is the axis `normal`, at the gizmo's pivot (axes `normal`+1 and `normal`+2 span it)  */
    QPolygonF planeSquare(const QVector3D& pivot, float length, int normal) const;
    bool eventFilter(QObject* watched, QEvent* event) override;
    /*  Whether the cursor is over a surface that a drag takes (a shape that is not locked and has numbers to pull by), and nothing but one shape is
     *  selected: the gizmo's arrows, rings and knobs are then only hit where they are drawn, so that a drag of a face is not taken for one of them  */
    bool overDraggableSurface(QPoint pos) const;
    /*  What the pick image says is at `pos`: the number of the shape (1-based), 0 for nothing.  A flat shape -- a rectangle, a circle -- has pixels only inside its outline, and the
     *  rasteriser leaves out the pixels on some of its sides: a press ON the outline, which is where it is pulled, found nothing on those sides.  Where there is nothing, the nearest
     *  flat shape (not a face of points: it is dragged as a whole) within a few pixels is what is there  */
    unsigned int pickedNear(QPoint pos) const;
    /*  Whether a press at `pos` on `s` pulls one of its surfaces: it has numbers to pull, and where it is a flat 2D shape, `pos` is at its
        outline (`edge` is the point of the outline that is pulled) -- there is no surface inside a face  */
    bool pullableAt(const Shape* s, QPoint pos, QVector3D* edge = nullptr) const;
    void drawGizmo(QPainter& painter);
    /*  The points of a set of points (Shape::markers), each as the dot in the middle of a gizmo -- and no handles: they are data  */
    void drawMarkers(QPainter& painter);
    void beginGizmoDrag(const GizmoGrip& g, QPoint pos);
    void dragGizmo(QPoint pos);
    bool ringPoint(QPoint pos, const QVector3D& pivot, const QVector3D& axis, QVector3D& out) const;
    void applyGizmoNumber(libfive::Tree::Id id, float value);
    void applyGizmoNumbers(const std::map<libfive::Tree::Id, float>& numbers);

    QString m_errorText;
    int m_errorLine = -1;
    QRect m_errorRect;                  // (where the banner was drawn: a click on it goes to the line)
    void drawErrorBanner(QPainter& painter);

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
