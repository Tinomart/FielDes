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
#include <QApplication>
#include <QCursor>
#include <QComboBox>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QJsonDocument>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QWidgetAction>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QMessageBox>
#include <QPainter>
#include <QtConcurrent>

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>

#include "fieldes/i18n.hpp"
#include "fieldes/carddrag.hpp"
#include "fieldes/color.hpp"
#include "fieldes/view.hpp"
#include "fieldes/typeicons.hpp"
#include "fieldes/colormap.hpp"
#include "fieldes/scenetree.hpp"
#include "fieldes/shader.hpp"

#include "libfive/solve/solver.hpp"

#ifndef GL_CLIP_DISTANCE0
#define GL_CLIP_DISTANCE0 0x3000
#endif

namespace FielDes {

namespace {
class ViewOverlay : public QWidget
{
public:
    ViewOverlay(QWidget* parent, std::function<void(QPainter&)> fn)
        : QWidget(parent), paint(fn)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_TranslucentBackground);
    }
protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        paint(p);
    }
    std::function<void(QPainter&)> paint;
};
}   // anonymous namespace

View::View(QWidget* parent)
    : QOpenGLWidget(parent), camera(size()),
      settings(Settings::defaultSettings())
{
    setMouseTracking(true);

    connect(&busy, &Busy::redraw, this, &View::update);
    connect(this, &View::startRender, &busy,
            [&](Settings){ if (shapes.size()) { busy.show(); emit(renderBusy(true)); } });
    connect(this, &View::meshesReady, &busy,
            [&](QList<const libfive::Mesh*>){ busy.hide(); emit(renderBusy(false)); });

    // Clicking the viewport focuses it, so its own keys (standard views)
    // work without stealing them from the editor
    setFocusPolicy(Qt::ClickFocus);
    connect(&camera, &Camera::changed, this, &View::update);

    connect(&camera, &Camera::animDone, this, &View::redrawPicker);

    m_waitTimer.setInterval(40);
    connect(&m_waitTimer, &QTimer::timeout, this, &View::startWaitingGrip);
    pick_timer.setSingleShot(true);
    pick_timer.setInterval(250);
    connect(&pick_timer, &QTimer::timeout, this, &View::redrawPicker);

    // The model tree floats over the top-left corner of the viewport
    m_scene = new ScenePanel(this);

    // 2D overlays, below the model tree
    m_overlay = new ViewOverlay(this, [this](QPainter& p) { paintOverlay(p); });
    m_overlay->setGeometry(rect());
    m_overlay->lower();

    // Analysis results: which field, how much deformation, elements
    m_resultPanel = new ResultPanel(this);
    m_resultPanel->hide();
    m_livePanel = new LivePanel(this);
    m_livePanel->hide();
    auto forResults = [this](auto fn, bool resample) {
        for (auto s : shapes) if (s->hasResult() || s->stepCount() > 0) fn(s);
        updateResultPanel();
        if (resample && section.enabled) requestSlice();
        pick_timer.start();
        update();
    };
    connect(m_resultPanel, &ResultPanel::channelChanged, this, [=](int i) {
        forResults([i](Shape* s) { s->setChannel(i); }, true);
    });
    connect(m_resultPanel, &ResultPanel::deformChanged, this, [=](float f) {
        forResults([f](Shape* s) { s->setDeformScale(f); }, false);
    });
    connect(m_resultPanel, &ResultPanel::elementsToggled, this, [=](bool on) {
        forResults([on](Shape* s) { s->setShowElements(on); }, false);
    });
    connect(m_resultPanel, &ResultPanel::stepChanged, this, [=](int k) {
        forResults([k](Shape* s) { s->setStep(k); }, true);
    });
    connect(m_resultPanel, &ResultPanel::flowToggled, this, [=](bool on) {
        forResults([on](Shape* s) { s->setShowFlowLines(on); }, false);
    });
    connect(m_resultPanel, &ResultPanel::opacityChanged, this, [=](float a) {
        m_flowOpacity = a;
        forResults([a](Shape* s) { if (s->hasFlowLines()) s->setOpacity(a); }, false);
    });
    connect(m_resultPanel, &ResultPanel::playToggled, this, [=](bool on) {
        if (on)
        {
            // (once: playing again from the end starts over)
            Shape* r = nullptr;
            for (auto s : shapes) if (s->hasResult() && s->stepCount() > 0) { r = s; break; }
            if (r && m_playMode == 2 && r->step() >= r->stepCount() - 1)
                forResults([](Shape* s) { s->setStep(0); }, true);
            m_playDir = 1;
            m_stepTimer.start();
        }
        else m_stepTimer.stop();
        updateResultPanel();
    });
    connect(m_resultPanel, &ResultPanel::playModeChanged, this, [=](int mode) {
        m_playMode = mode;
        m_playDir = 1;
        updateResultPanel();
    });
    connect(m_resultPanel, &ResultPanel::playSpeedChanged, this, [=](float speed) {
        m_playSpeed = std::max(0.1f, speed);
        m_stepTimer.setInterval(int(100.0f / m_playSpeed));
        updateResultPanel();
    });
    // Play: the next step ten times a second at speed 1 (a step with its own geometry waits until it is meshed);
    // round and round, back and forth, or once to the end
    m_stepTimer.setInterval(100);
    connect(&m_stepTimer, &QTimer::timeout, this, [=]() {
        Shape* r = nullptr;
        for (auto s : shapes) if (s->hasResult() && s->stepCount() > 0) { r = s; break; }
        if (!r)
        {
            m_stepTimer.stop();
            updateResultPanel();
            return;
        }
        // (every stepped shape must have finished meshing its step: a body and the fluid around it step together)
        for (auto s : shapes) if (s->stepCount() > 0 && !s->done()) return;
        const int n = r->stepCount();
        int k = r->step();
        if (m_playMode == 0) k = (k + 1) % n;
        else if (m_playMode == 1)
        {
            if (n < 2) return;
            if (k + m_playDir < 0 || k + m_playDir >= n) m_playDir = -m_playDir;
            k += m_playDir;
        }
        else
        {
            if (k >= n - 1)
            {
                m_stepTimer.stop();
                updateResultPanel();
                return;
            }
            k += 1;
        }
        forResults([k](Shape* s) { s->setStep(k); }, true);
    });
    // The particles on the streamlines move at thirty frames a second
    m_flowTimer.setInterval(33);
    connect(&m_flowTimer, &QTimer::timeout, this, [=]() {
        bool any = false;
        for (auto s : shapes)
            if (s->showsFlowLines())
            {
                s->advanceFlow(0.033f);
                any = true;
            }
        if (any) update(); else m_flowTimer.stop();
    });

    // An optimisation that runs shows its design as it goes (a few times a second: the picture is only looked at when it is new)
    m_liveTimer.setInterval(120);
    connect(&m_liveTimer, &QTimer::timeout, this, &View::pollLive);
    m_liveTimer.start();

    // Section field sampling, and the field viewer's disc: each debounced, in a worker thread of its own
    for (Plane* plane : {&m_sec, &m_fld})
    {
        plane->timer.setSingleShot(true);
        plane->timer.setInterval(16);          // (about a frame: a drag that moves the plane faster is still one pass)
        connect(&plane->timer, &QTimer::timeout, this, [this, plane]{ startSliceAt(*plane, false); });
        connect(&plane->watcher, &QFutureWatcher<FieldSlice>::finished, this, [this, plane]{ onSliceFinished(*plane); });
    }
}

View::~View()
{
    for (Plane* plane : {&m_sec, &m_fld})
    {
        plane->generation++;              // abandon any sampling in flight
        plane->watcher.waitForFinished();
    }
    makeCurrent();
    for (Plane* plane : {&m_sec, &m_fld})
    {
        plane->tex.reset();
        plane->colorTex.reset();
        if (plane->vao.isCreated()) plane->vao.destroy();
        if (plane->vbo.isCreated()) plane->vbo.destroy();
    }
    for (auto s : findChildren<Shape*>())
    {
        s->freeGL();
    }
    doneCurrent();
}

void View::setShapes(QList<Shape*> new_shapes)
{
    for (auto s : new_shapes)
    {
        if (s->hasFlowLines()) s->setOpacity(m_flowOpacity);        // (a flow keeps how opaque it was made, run after run)
    }
    // We're going to co-optimize every single new and old tree together,
    // so that we can deduplicate them.  This could be expensive; if we
    // notice the main thread lagging, we could do the new_shapes half of this
    // co-optimization on the worker thread, but would then need to pass
    // the map from the thread.
    std::unordered_map<libfive::TreeDataKey, libfive::Tree> canonical;

    // Pack tree IDs into a pair of sets for fast checking.  A shape is its geometry AND its colours: the same geometry coloured
    // differently is another shape (a body, and the same body tinted where a condition acts on it, are both drawn)
    using ShapeKey = std::pair<libfive::Tree::Id, QString>;
    std::map<ShapeKey, Shape*> new_shapes_map;
    std::map<Shape*, ShapeKey> new_shapes_key;
    for (auto& s : new_shapes)
    {
        const ShapeKey key{s->getUniqueId(canonical), s->colorKey()};
        new_shapes_key.insert({s, key});
        new_shapes_map.insert({key, s});
    }

    // Erase all existing shapes that aren't in the new_shapes list
    bool vars_changed = false;
    bool any_running = false;
    for (auto itr=shapes.begin(); itr != shapes.end(); /* no update */ )
    {
        // (same geometry coloured differently -- another field or range -- is a different shape: its colours are computed while meshing)
        auto n = new_shapes_map.find(ShapeKey{(*itr)->getUniqueId(canonical), (*itr)->colorKey()});
        if (n == new_shapes_map.end())
        {
            if (*itr == drag_target)
            {
                drag_target->setGrabbed(false);
                drag_target = nullptr;
                emit(dragEnd());
                mouse.state = mouse.RELEASED;
            }
            if (*itr == hover_target)
            {
                hover_target->setHover(false);
                hover_target = nullptr;
            }
            disconnect(*itr, &Shape::redraw, this, &View::update);
            (*itr)->deleteLater();
            itr = shapes.erase(itr);
            pick_timer.start();
        }
        else
        {
            vars_changed |= (*itr)->updateFrom(n->second);
            // The statement may have moved: keep the tree link current
            (*itr)->setSourceLine(n->second->sourceLine());
            any_running |= !(*itr)->done();
            new_shapes_map.erase(n);
            ++itr;
        }
    }

    // Start up the busy spinner
    if (new_shapes_map.size() || vars_changed)
    {
        busy.show();
        emit(renderBusy(true));
    }

    // Connect all new shapes
    for (auto s : new_shapes)
    {
        if (new_shapes_map.count(new_shapes_key[s]))
        {
            connect(s, &Shape::redraw, this, &View::update);
            connect(s, &Shape::gotMesh, this, &View::checkMeshes);
            connect(s, &Shape::gotMesh, this, &View::updateResultPanel);
            connect(s, &Shape::cacheStateChanged, this, &View::cacheStatesChanged);
            connect(s, &Shape::gotMesh, &pick_timer,
                    QOverload<>::of(&QTimer::start));
            connect(this, &View::startRender,
                    s, [=](Settings st) { s->startRender(st, this->alg); });
            s->startRender(settings, alg);
            s->setParent(this);
            any_running = true;

            shapes.push_back(s);
        }
        else
        {
            s->deleteLater();
        }
    }

    if (!any_running) {
        busy.hide();
        emit(renderBusy(false));
    }
    if (const char* log = std::getenv("FIELDES_SHAPE_LOG"))
    {
        // (for the tests: how many shapes of the run before went on, how many were new -- their render starts again -- and how many are gone)
        static int number = 0;
        size_t fresh = 0;
        for (auto s : new_shapes)
            if (new_shapes_map.count(new_shapes_key[s])) ++fresh;
        if (FILE* f = std::fopen(log, "a"))
        {
            std::fprintf(f, "run %d: %d shapes shown, %d new (render starts), %d kept\n", ++number, int(shapes.size()), int(fresh),
                         int(shapes.size()) - int(fresh));
            std::fclose(f);
        }
    }
    m_liveDrawn = false;
    updateResultPanel();            // (shapes that went take their legend with them; new ones bring theirs when they have meshes)
    update();
    requestSlice();
}

bool View::isRendering() const
{
    for (const auto* s : shapes) if (!s->done()) return true;
    return false;
}

double View::renderFraction() const
{
    if (shapes.isEmpty()) return 1.0;
    // Each shape counts as much as its render is work (Shape::renderWeight):
    // with every shape counting the same, the hundred small parts of a
    // machine put the bar at 97 % while its few big parts had barely begun.
    // A shape with no level done yet counts as the average of the others.
    double known = 0.0, nKnown = 0.0;
    for (const auto* s : shapes)
    {
        const double w = s->renderWeight();
        if (w > 0) { known += w; nKnown += 1.0; }
    }
    const double unknown = nKnown > 0 ? known / nKnown : 1.0;
    double sum = 0.0, total = 0.0;
    for (const auto* s : shapes)
    {
        const double w0 = s->renderWeight();
        const double w = w0 > 0 ? w0 : unknown;
        sum += w * s->renderFraction();
        total += w;
    }
    return total > 0 ? sum / total : 1.0;
}

void View::highlightLines(QList<int> lines0)
{
    for (auto& s : shapes)
    {
        s->setSelected(lines0.contains(s->sourceLine()));
    }
    update();
}

void View::moveSourceLines(QVector<int> moved)
{
    auto moveLine = [&](int line) { return (line >= 0 && line < moved.size()) ? moved[line] : line; };
    for (auto& s : shapes)
    {
        if (s->sourceLine() >= 0) s->setSourceLine(moveLine(s->sourceLine()));
    }
    // (what the view keeps by line as well: the provisional gizmo's models, the press that waits for a gizmo)
    for (int& l : m_provLines) l = moveLine(l);
    for (int& l : m_wait.lines) l = moveLine(l);
    update();
}

QList<int> View::highlightedLines() const
{
    QList<int> out;
    for (const auto* s : shapes)
    {
        if (s->isSelected() && !out.contains(s->sourceLine())) out << s->sourceLine();
    }
    return out;
}

QList<int> View::linesInside(const QRect& r) const
{
    // A shape is inside when every vertex of its mesh -- as drawn from this camera -- is in the rectangle
    QList<int> out;
    const QMatrix4x4 M = camera.M();
    const QRectF rect(r);
    const float W = float(width()), H = float(height());
    for (const auto* s : shapes)
    {
        if (!s->hasMesh() || s->sourceLine() < 0 || out.contains(s->sourceLine())) continue;
        const libfive::Mesh* m = s->getMesh();
        if (!m || m->verts.empty()) continue;
        // (the vertices the triangles use: a mesh keeps a first vertex at the origin that nothing refers to, which
        // would put the origin into every shape)
        bool inside = !m->branes.empty();
        for (const auto& t : m->branes)
        {
            for (int k = 0; k < 3 && inside; ++k)
            {
                const auto& v = m->verts[t[k]];
                const QVector3D n = M.map(QVector3D(v.x(), v.y(), v.z()));
                if (!rect.contains(QPointF((n.x() + 1) * 0.5f * W, (1 - n.y()) * 0.5f * H))) inside = false;
            }
            if (!inside) break;
        }
        if (inside) out << s->sourceLine();
    }
    return out;
}

void View::focusOn(QVector3D min, QVector3D max, QList<int> lines0)
{
    // Where the drawn models are: the meshes of those lines say it exactly, while the extents the model tree is given are
    // estimates (loose for a model with numbers of a gizmo in it: the camera would zoom out far beyond the model)
    bool any = false;
    QVector3D lo, hi;
    for (auto& s : shapes)
    {
        if (!lines0.contains(s->sourceLine()) || !s->hasMesh() || s->noPaint()) continue;
        const auto& b = s->getMeshBounds();
        const QVector3D l(b.lower.x(), b.lower.y(), b.lower.z());
        const QVector3D h(b.upper.x(), b.upper.y(), b.upper.z());
        if (!any) { lo = l; hi = h; any = true; }
        else
        {
            lo = QVector3D(std::min(lo.x(), l.x()), std::min(lo.y(), l.y()), std::min(lo.z(), l.z()));
            hi = QVector3D(std::max(hi.x(), h.x()), std::max(hi.y(), h.y()), std::max(hi.z(), h.z()));
        }
    }
    if (any && (hi - lo).length() > 0)
    {
        min = lo;
        max = hi;
    }
    if ((max - min).length() <= 0) return;
    camera.zoomTo(min, max);
}

bool View::meshBounds(QVector3D& min, QVector3D& max) const
{
    bool any = false;
    for (auto& s : shapes)
    {
        if (!s->hasMesh() || s->noPaint()) continue;
        const auto& b = s->getMeshBounds();
        const QVector3D lo(b.lower.x(), b.lower.y(), b.lower.z());
        const QVector3D hi(b.upper.x(), b.upper.y(), b.upper.z());
        if (!any) { min = lo; max = hi; any = true; }
        else
        {
            min = QVector3D(std::min(min.x(), lo.x()), std::min(min.y(), lo.y()), std::min(min.z(), lo.z()));
            max = QVector3D(std::max(max.x(), hi.x()), std::max(max.y(), hi.y()), std::max(max.z(), hi.z()));
        }
    }
    return any;
}

void View::frameAll()
{
    QList<int> all;
    for (auto& s : shapes) all << s->sourceLine();
    bool any = false;
    for (auto& s : shapes) any |= s->hasMesh() && !s->noPaint();
    if (any)
    {
        focusOn(QVector3D(), QVector3D(), all);
    }
    else
    {
        camera.zoomTo(settings.min, settings.max);
    }
}

void View::cancelShapes()
{
    for (auto& s : shapes)
    {
        s->deleteLater();
    }
    shapes.clear();
}

void View::onSettingsFromScript(Settings s, bool first)
{
    if (settings != s)
    {
        const bool bounds = settings.min != s.min || settings.max != s.max;
        settings = s;
        update();
        // A shape starts again only if its own render changes with the settings: the region it meshes, the resolution, the quality.
        // The parts of an import mesh over cubes of their own, at resolutions of their own, so a model added to the script (the
        // scene's region grows) leaves the ones that are meshing to go on, and the ones that are done as they are
        for (auto shape : shapes)
        {
            if (shape->wouldRenderDifferently(s))
            {
                shape->startRender(s, alg);
            }
        }
        if (bounds)
        {
            emit(boundsChanged(s.min, s.max));
            requestSlice();
        }
    }

    if ((first || zoom_on_next_settings) && shapes.size())
    {
        camera.zoomTo(s.min, s.max);
        zoom_on_next_settings = false;
    }
}

void View::initializeGL()
{
    initializeOpenGLFunctions();

    Shader::initializeGL();

    arrow.initializeGL();
    glyphs.initializeGL();
    axes.initializeGL();
    background.initializeGL();
    bbox.initializeGL();
    busy.initializeGL();
}

void View::redrawPicker()
{
    // Only begin redrawing the pick buffer once we've been drawn once
    // and confirmed that the OpenGL context is new enough to work.
    if (!gl_checked)
    {
        pick_timer.start();
        return;
    }

    // We may not have the OpenGL context, so we claim it here
    // (and release it at the bottom if it was claimed)
    const bool needs_gl = (context() != QOpenGLContext::currentContext());
    if (needs_gl)
    {
        makeCurrent();
    }

    // Rebuild buffer if it is not present or is the wrong size
    if (!pick_fbo.data() ||  pick_fbo->size() != camera.size)
    {
        pick_fbo.reset(new QOpenGLFramebufferObject(
                    camera.size, QOpenGLFramebufferObject::Depth));
    }

    pick_fbo->bind();

    glClearColor(0, 0, 0, 1);
    glClearDepthf(1);
    glClearStencil(0);
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glViewport(0, 0, camera.size.width(), camera.size.height());

    auto m = camera.M();
    QRgb color = 1;
    const bool clipping = section.enabled && section.clip;
    setClipUniform(clipping);
    Shader::basic->bind();
    glUniform1i(Shader::basic->uniformLocation("cut_mode"), 0);
    Shader::basic->release();
    for (auto& s : shapes)
    {
        if (!s->noPaint()) s->drawMonochrome(m, color);
        ++color;                // (a point is not painted, nor picked: the numbers of the others stay what they were)
    }
    if (clipping)
    {
        glDisable(GL_CLIP_DISTANCE0);
    }

    pick_img = pick_fbo->toImage();

    // There's no utility function to get the depth buffer, so we manually
    // read it with glReadPixels here.
    if (pick_depth.size() != camera.size.width() * camera.size.height())
    {
        pick_depth.resize(camera.size.width() * camera.size.height());
    }
    glReadPixels(0, 0, camera.size.width(), camera.size.height(),
                 GL_DEPTH_COMPONENT, GL_FLOAT, pick_depth.data());

    glDisable(GL_DEPTH_TEST);
    pick_fbo->release();

    if (needs_gl)
    {
        doneCurrent();
    }
}

void View::paintGL()
{
    if (!gl_checked)
    {
        auto def = QSurfaceFormat::defaultFormat();
        auto fmt = context()->format();
        if (fmt.majorVersion() < def.majorVersion() ||
                (fmt.majorVersion() == def.majorVersion() &&
                 fmt.minorVersion() < def.minorVersion()))
        {
            auto err = T("Error:<br><br>"
                    "OpenGL context is too old<br>"
                    "(got %1.%2, need %3.%4)<br><br>"
                    "The application will now exit.<br>")
                    .arg(fmt.majorVersion())
                    .arg(fmt.minorVersion())
                    .arg(def.majorVersion())
                    .arg(def.minorVersion());

            QMessageBox::critical(this, "FielDes", err);
            exit(1);
        }
        gl_checked = true;
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    painter.beginNativePainting();
    background.draw();

    auto m = camera.M();
    glEnable(GL_DEPTH_TEST);

    const bool clipping = section.enabled && section.clip;
    setClipUniform(clipping);
    // (the opaque shapes first; then the translucent ones -- a flow with a body in it -- blended over them.  A translucent shape is drawn
    // twice: first for its depth alone, so that only its nearest layer is left, then in colour over that layer.  Blending every triangle in
    // the order the mesh has them would paint the far side and the inner surfaces over the near ones, in patches)
    for (int pass = 0; pass < 3; ++pass)
    {
        if (pass == 1)
        {
            glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);        // (the nearest layer of the translucent shapes: depth only)
        }
        if (pass == 2)
        {
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            glDepthMask(GL_FALSE);
            glDepthFunc(GL_LEQUAL);
            glEnable(GL_BLEND);
            glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
        }
        for (auto& s : shapes)
        {
            if ((s->opacityValue() < 0.999f) != (pass >= 1)) continue;
            // The boundary conditions are drawn on the part's own surface: pulled a hair towards the eye, they win over the part when both are shown
            // (a patch of a surface is on the part's surface too)
            const bool onTheSurface = !s->boundaryGlyphs().empty() || s->isSurfacePatch();
            if (onTheSurface)
            {
                glEnable(GL_POLYGON_OFFSET_FILL);
                glPolygonOffset(-1.0f, -1.0f);
            }
            if (!s->noPaint()) s->draw(m);
            if (onTheSurface) glDisable(GL_POLYGON_OFFSET_FILL);
        }
        if (pass == 2)
        {
            glDepthMask(GL_TRUE);
            glDepthFunc(GL_LESS);
            glDisable(GL_BLEND);
        }
    }
    if (clipping)
    {
        setClipUniform(false);           // back to normal shading
        glDisable(GL_CLIP_DISTANCE0);
    }
    if (section.enabled && section.field)
    {
        drawSlicePlane(m, m_sec, section);
    }
    if (fieldShown())
    {
        drawSlicePlane(m, m_fld, fieldPlane());
    }

    // The streamlines of a flow and the particles on them, over everything (seen through the fluid)
    {
        bool any = false;
        for (auto& s : shapes) any = any || s->showsFlowLines();
        if (any)
        {
            glDisable(GL_DEPTH_TEST);
            for (auto& s : shapes)
                if (s->showsFlowLines()) s->drawFlowLines(m);
            glEnable(GL_DEPTH_TEST);
        }
    }

    // The arrows and pads of boundary conditions, over the model they belong to
    for (auto& s : shapes)
        for (const auto& g : s->boundaryGlyphs())
        {
            float r = 0.8f, gr = 0.8f, b = 0.8f;
            bcCategoryRGB(g.kind, r, gr, b);
            const QColor color = QColor::fromRgbF(r, gr, b);
            // (a pad lies on the surface: a support, a wall, a slip, a fixed temperature, a convection; the rest are arrows)
            if (g.kind == 1 || g.kind == 2 || g.kind == 9 || g.kind == 10 || g.kind == 11 || g.kind == 12)
                glyphs.drawPad(m, g.pos, g.size, g.dir, color);
            else
                glyphs.drawArrow(m, g.pos, g.size, g.dir, color, g.tip);
        }

    if (show_axes)
    {
        // A small, fixed on-screen size origin marker (thin lines that
        // models hide), instead of unit-length arrows drawn on top of
        // everything, which could cover a small model entirely
        const float px = 34;
        const float len = 2 * px / (std::max(1, std::min(camera.size.width(),
                                                         camera.size.height())) *
                                    camera.getScale());
        QMatrix4x4 mm = m;
        mm.scale(len);
        axes.drawMarker(mm);
    }

    if (show_bbox)
    {
        // This is intentionally accidentally quadratic, as we won't
        // be drawing too many bounding boxes and QVector3D doesn't
        // come with operator< or qHash overloads.
        QList<QPair<QVector3D, QVector3D>> shown;
        auto draw_bbox = [&](QVector3D min, QVector3D max){
            if (!shown.contains({min, max}))
            {
                bbox.draw(min, max, camera);
                shown.push_back({min, max});
            }
        };
        for (auto& s : shapes)
        {
            auto b = s->getRenderBounds();
            draw_bbox(QVector3D(b.lower.x(), b.lower.y(), b.lower.z()),
                      QVector3D(b.upper.x(), b.upper.y(), b.upper.z()));
        }
        if (shapes.size() == 0)
        {
            draw_bbox(settings.min, settings.max);
        }
    }

    if (drag_target)
    {
        arrow.draw(m, cursor_pos, 0.1 / camera.getScale(),
                   drag_dir.normalized(),
                   drag_valid ? Color::green : Color::red);
    }

    // (the spinner's place shows the render's progress bar: paintOverlay)

    glDisable(GL_DEPTH_TEST);
    painter.endNativePainting();

    if (m_overlay)
    {
        m_overlay->update();
    }
}

void View::paintOverlay(QPainter& painter)
{
    if (show_triad)
    {
        drawTriad(painter);
    }
    if (section.enabled)
    {
        drawSectionHandle(painter);
    }
    if (fieldShown())
    {
        drawFieldGizmo(painter);
    }
    drawHandles(painter);
    if (!section_readout.isEmpty() && !section_drag)
    {
        QFont font = painter.font();
        font.setPointSizeF(8.5);
        painter.setFont(font);
        const QFontMetrics fm(font);
        const QRectF box(section_readout_pos + QPointF(14, 10),
                         QSizeF(fm.horizontalAdvance(section_readout) + 12, fm.height() + 6));
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(22, 76, 94, 215));
        painter.drawRoundedRect(box, 4, 4);
        painter.setPen(QColor(0xee, 0xe8, 0xd5));
        painter.drawText(box, Qt::AlignCenter, section_readout);
    }

    drawBoundaryLabels(painter);
    legend_close.clear();
    bottom_items.clear();
    drawLegends(painter);
    drawScaleBar(painter);
    drawRenderProgress(painter);
    drawErrorBanner(painter);

    if (mouse.state == mouse.DRAG_RECT && (rect_now - rect_start).manhattanLength() >= 4)
    {
        // The selection rectangle: a thin outline round a light fill, in the colour of the selection
        const QRect r = QRect(rect_start, rect_now).normalized();
        const QColor blue(0x26, 0x8b, 0xd2);
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setPen(QPen(blue, 1));
        painter.setBrush(QColor(blue.red(), blue.green(), blue.blue(), 45));
        painter.drawRect(r.adjusted(0, 0, -1, -1));
    }

    if (probe_valid && cursor_pos_valid)
    {
        // The field's value at the surface point under the cursor
        QFont font = painter.font();
        font.setPointSizeF(9);
        painter.setFont(font);
        const QString text = (probe_label.isEmpty() ? T("value") : probe_label) +
                             ":  " + QString::number(probe_value, 'g', 5);
        const QFontMetrics fm(font);
        const QRectF box(probe_pos + QPointF(16, 12),
                         QSizeF(fm.horizontalAdvance(text) + 14, fm.height() + 8));
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(22, 76, 94, 225));
        painter.drawRoundedRect(box, 4, 4);
        painter.setPen(QColor(0xee, 0xe8, 0xd5));
        painter.drawText(box, Qt::AlignCenter, text);
    }

    if (cursor_pos_valid)
    {
        // Surface point under the cursor, one line in the bottom-left corner
        QFont font = painter.font();
        font.setFamily("Inconsolata");
        font.setPointSizeF(10.5);
        painter.setFont(font);
        auto num = [](float v) { return QString::number(v, 'f', 3); };
        const QString text = QString("X %1   Y %2   Z %3")
            .arg(num(cursor_pos.x()), num(cursor_pos.y()), num(cursor_pos.z()));
        const QFontMetrics fm(font);
        const QRectF box(8, height() - fm.height() - 14,
                         fm.horizontalAdvance(text) + 16, fm.height() + 8);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 70));
        painter.drawRoundedRect(box, 4, 4);
        painter.setPen(Color::base1);
        painter.drawText(box, Qt::AlignCenter, text);
    }
}

void View::pollLive()
{
    swapLive();                 // (a picture whose meshes came in between two looks)
    const auto now = libfive::run_progress::liveView();
    if (!now && !m_live) return;
    if (now && m_live && now->serial == m_live->serial) return;
    m_live = now;
    if (!now)
    {
        // (the optimisation is over: what is shown stays until the run makes its result, which replaces it)
        for (auto s : m_livePending) s->deleteLater();
        m_livePending.clear();
        m_liveWaiting = false;
        m_livePanel->hide();
        update();
        return;
    }
    m_livePanel->setView(*now);
    if (!m_livePanel->isVisible())
    {
        m_livePanel->show();
        placeLivePanel();
    }
    // The picture is of the model the statement that runs makes: when that model is not shown (its eye is off) none of it is drawn, and its
    // legend is not there -- only the card says how the optimisation is doing
    const int running = libfive::run_progress::current().line;
    if (m_scene && running > 0 && !m_scene->shownAtLine(running))
    {
        for (auto s : m_livePending) s->deleteLater();
        m_livePending.clear();
        m_liveWaiting = false;
        if (m_liveDrawn)
        {
            // (it was shown until its eye was turned off)
            for (auto s : shapes)
            {
                disconnect(s, &Shape::redraw, this, &View::update);
                s->deleteLater();
            }
            shapes.clear();
            m_liveDrawn = false;
            updateResultPanel();
        }
        update();
        return;
    }
    // A picture that is still being meshed is not thrown away for the newer one: a part whose picture takes longer to mesh than an iteration
    // takes would never be shown at all.  It is shown when it is done, and the newest picture is meshed next
    bool meshing = false;
    for (auto s : m_livePending)
        if (!s->done()) meshing = true;
    if (meshing)
    {
        m_liveWaiting = true;
        update();
        return;
    }
    startLive();
    update();
}

// The shapes of a picture of an optimisation, made as the shapes of a result are (see Interpreter::recordShape) and rendered as any:
// they replace the ones shown (swapLive) when every one of them has its mesh
void View::startLive()
{
    for (auto s : m_livePending) s->deleteLater();
    m_livePending.clear();
    // The render settings the script has given by now (it is still running: they are read from it), else those of the run before, over
    // what the picture covers
    Settings live = settings;
    if (m_liveSettings)
    {
        const auto o = QJsonDocument::fromJson(m_liveSettings().toUtf8()).object();
        const auto lo = o["min"].toArray(), hi = o["max"].toArray();
        if (lo.size() == 3 && hi.size() == 3)
        {
            live.min = QVector3D(float(lo[0].toDouble()), float(lo[1].toDouble()), float(lo[2].toDouble()));
            live.max = QVector3D(float(hi[0].toDouble()), float(hi[1].toDouble()), float(hi[2].toDouble()));
        }
        if (o["res"].isDouble()) live.res = float(o["res"].toDouble());
        if (o["quality"].isDouble()) live.quality = float(o["quality"].toDouble());
    }
    m_liveMin = live.min;
    m_liveMax = live.max;
    // (the numbers of the script -- var() -- the trees may hold: those of the shapes shown)
    std::map<libfive::Tree::Id, float> vars;
    for (auto s : shapes)
        for (const auto& kv : s->getVars()) vars.insert(kv);
    for (const auto& ls : m_live->shapes)
    {
        auto* s = new Shape(ls.tree, vars);
        if (ls.surfaceTris.size() >= 3)
        {
            // (the surface of the design, drawn as it is: see LiveShape)
            auto surface = std::make_shared<Shape::ExactMesh>();
            surface->key = "live:" + std::to_string(m_live->serial);
            surface->verts = ls.surfaceVerts;
            surface->tris = ls.surfaceTris;
            s->setSurfaceMesh(surface);
        }
        if (!ls.channels.empty())
        {
            std::vector<Shape::FieldChannel> channels;
            for (const auto& c : ls.channels)
            {
                Shape::FieldChannel fc;
                fc.name = QString::fromStdString(c.name);
                fc.label = T(QString::fromStdString(c.label));
                fc.tree = c.tree;
                fc.lo = c.lo;
                fc.hi = c.hi;
                channels.push_back(fc);
            }
            const auto& first = channels.front();
            s->setColorField(first.tree, first.lo, first.hi, false, first.label, "turbo");
            if (!ls.lines.empty())
            {
                std::vector<Shape::FlowLine> lines;
                for (const auto& l : ls.lines) lines.push_back(Shape::FlowLine(l.begin(), l.end()));
                s->setFlowLines(std::move(lines), ls.linesLo, ls.linesHi);
            }
            s->setResult(channels, 0, libfive::Tree::invalid(), libfive::Tree::invalid(), libfive::Tree::invalid(), 0.0f, 1.0f,
                         Shape::ElementGrid());
            s->setColorDetail(ls.detail);
        }
        s->setParent(this);
        connect(s, &Shape::gotMesh, this, &View::swapLive);
        m_livePending.push_back(s);
    }
    for (auto s : m_livePending) s->startRender(live, alg);
}

void View::swapLive()
{
    if (m_livePending.isEmpty()) return;
    for (auto s : m_livePending)
        if (!s->done() || !s->getMesh()) return;
    // The picture is meshed: it takes the place of what is shown
    const bool framed = !shapes.isEmpty();
    for (auto s : shapes)
    {
        if (s == drag_target)
        {
            drag_target->setGrabbed(false);
            drag_target = nullptr;
            emit(dragEnd());
            mouse.state = mouse.RELEASED;
        }
        if (s == hover_target)
        {
            hover_target->setHover(false);
            hover_target = nullptr;
        }
        disconnect(s, &Shape::redraw, this, &View::update);
        s->deleteLater();
    }
    shapes.clear();
    m_liveDrawn = true;
    for (auto s : m_livePending)
    {
        disconnect(s, &Shape::gotMesh, this, &View::swapLive);
        connect(s, &Shape::redraw, this, &View::update);
        connect(s, &Shape::gotMesh, this, &View::checkMeshes);
        connect(s, &Shape::gotMesh, this, &View::updateResultPanel);
        connect(s, &Shape::gotMesh, &pick_timer, QOverload<>::of(&QTimer::start));
        connect(this, &View::startRender, s, [=](Settings st) { s->startRender(st, this->alg); });
        shapes.push_back(s);
    }
    m_livePending.clear();
    // (the first picture of a run that had nothing shown: the camera looks at it)
    if (!framed) camera.zoomTo(m_liveMin, m_liveMax);
    updateResultPanel();
    pick_timer.start();
    update();
    // (a newer picture came while this one was meshing: its turn)
    if (m_liveWaiting && m_live)
    {
        m_liveWaiting = false;
        startLive();
    }
}

void View::drawRenderProgress(QPainter& painter)
{
    // The render's progress, small, bottom right (where the busy spinner
    // was): counted by the mesher, not predicted (see Shape::renderFraction);
    // it only moves forward
    if (!isRendering())
    {
        if (m_renderActive && qEnvironmentVariableIsSet("FIELDES_TIMING"))
            std::cerr << "[bar-render] " << std::fixed << QDateTime::currentMSecsSinceEpoch() / 1000.0
                      << " done after " << m_renderClock.elapsed() / 1000.0 << " s\n";
        m_renderActive = false;
        return;
    }
    if (!m_renderActive)
    {
        m_renderActive = true;
        m_renderClock.start();
        m_renderShown = 0.0;
        m_renderLogged = -1;
    }
    const double e = m_renderClock.elapsed() / 1000.0;
    m_renderShown = std::max(m_renderShown, std::min(0.99, renderFraction()));
    const QString text = T("Rendering");
    if (qEnvironmentVariableIsSet("FIELDES_TIMING") && int(e) != m_renderLogged)
    {
        m_renderLogged = int(e);
        std::cerr << "[bar-render] " << std::fixed << QDateTime::currentMSecsSinceEpoch() / 1000.0 << " "
                  << e << " s: " << int(100 * m_renderShown) << " %\n";
        {   // the totals the bar is made of
            double W = 0, D = 0, unknownWeight = 0; int nUnknown = 0, nDone = 0;
            for (const auto* s : shapes)
            {
                const double w = s->renderWeight();
                if (w > 0) { W += w; D += w * s->renderFraction(); } else nUnknown++;
                if (s->done()) nDone++;
                unknownWeight += 0;
            }
            std::cerr << "[bar-render]   shapes " << shapes.size() << " done " << nDone << " unknown " << nUnknown
                      << " weight " << std::fixed << W << " done " << D << " instant " << renderFraction() << "\n";
        }
        // the two shapes with the most render left, as the bar sees them
        QList<QPair<double, const Shape*>> left;
        for (const auto* s : shapes)
        {
            const double w = std::max(s->renderWeight(), 1.0);
            left.append({w * (1.0 - s->renderFraction()), s});
        }
        std::sort(left.begin(), left.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        for (int q = 0; q < std::min(2, int(left.size())); ++q)
            std::cerr << "[bar-render]   left " << std::fixed << left[q].first << ": "
                      << left[q].second->renderDebug().toStdString() << "\n";
    }

    painter.save();
    QFont font = painter.font();
    font.setPointSizeF(8.0);
    painter.setFont(font);
    const QFontMetrics fm(font);
    const double w = 140, h = 5, margin = 14;
    // Bottom right, moved up above any legend or the scale bar there
    QRectF bar(width() - margin - w, height() - margin - h, w, h);
    const double textW = fm.horizontalAdvance(text) + 4;
    for (int moves = 0; moves < 8; moves++)
    {
        const QRectF area(std::min(bar.left(), bar.right() - textW), bar.top() - fm.height() - 3,
                          std::max(w, textW), fm.height() + 3 + h);
        double top = area.bottom();
        for (const QRectF& o : bottom_items)
            if (o.intersects(area.adjusted(-4, -4, 4, 4))) top = std::min(top, o.top());
        if (top >= area.bottom()) break;
        bar.moveBottom(top - 8);
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 255, 255, 50));
    painter.drawRoundedRect(bar, 2, 2);
    painter.setBrush(QColor(0x2a, 0xa1, 0x98, 230));
    painter.drawRoundedRect(QRectF(bar.left(), bar.top(), w * m_renderShown, h), 2, 2);
    painter.setPen(QColor(255, 255, 255, 170));
    painter.drawText(QRectF(bar.left() - 200, bar.top() - fm.height() - 3, w + 200, fm.height()),
                     Qt::AlignRight | Qt::AlignVCenter, text);
    painter.restore();
}

void View::drawScaleBar(QPainter& painter)
{
    // Length of one model unit in pixels, at the depth of the view's centre
    // (the view's centre is where the camera's centre maps, with w = 1, so
    // one model unit spans scale * sx NDC units there; see Camera::proj)
    if (width() < 50 || height() < 50) return;
    const float frac = width() / float(height());
    const float sx = frac > 1 ? 1 / frac : 1;
    const float units = 100.0f / (camera.getScale() * sx * width() / 2.0f);   // model units per 100 px
    if (!(units > 0) || !std::isfinite(units)) return;
    // A round length that is 80..200 px long
    const float target = units * 1.4f;               // ~140 px
    const float p10 = std::pow(10.0f, std::floor(std::log10(target)));
    float len = p10;
    for (float m : {1.0f, 2.0f, 5.0f, 10.0f}) if (p10 * m <= target) len = p10 * m;
    const float px = 100.0f * len / units;
    QFont font = painter.font();
    font.setPointSizeF(8.5);
    painter.setFont(font);
    const QFontMetrics fm(font);
    const float x0 = width() / 2.0f - px / 2, y = height() - 16.0f;
    painter.setRenderHint(QPainter::Antialiasing, false);
    const QColor ink(0xee, 0xe8, 0xd5, 220), shade(0, 0, 0, 90);
    // A two-tone bar with ticks at the ends and the middle, like a ruler
    painter.fillRect(QRectF(x0 - 1, y - 4, px + 2, 7), shade);
    painter.fillRect(QRectF(x0, y - 3, px / 2, 5), ink);
    painter.setPen(QPen(ink, 1));
    painter.drawRect(QRectF(x0 + px / 2, y - 3, px / 2, 5));
    for (float t : {0.0f, 0.5f, 1.0f}) painter.drawLine(QPointF(x0 + t * px, y - 7), QPointF(x0 + t * px, y + 2));
    auto fmt = [](float v) {
        QString s = QString::number(v, 'g', 3);
        return s;
    };
    const QString label = fmt(len) + " mm";
    painter.setPen(ink);
    painter.drawText(QRectF(x0 + px + 6, y - fm.height() / 2.0 - 1, fm.horizontalAdvance(label) + 4, fm.height()),
                     Qt::AlignLeft | Qt::AlignVCenter, label);
    painter.drawText(QRectF(x0 - fm.horizontalAdvance("0") - 6, y - fm.height() / 2.0 - 1,
                            fm.horizontalAdvance("0") + 2, fm.height()), Qt::AlignRight | Qt::AlignVCenter, "0");
    bottom_items.push_back(QRectF(x0 - fm.horizontalAdvance("0") - 8, y - fm.height() / 2.0 - 4,
                                  px + fm.horizontalAdvance("0") + fm.horizontalAdvance(label) + 20, fm.height() + 6));
    painter.setRenderHint(QPainter::Antialiasing, true);
}

// The names and values beside the arrows and pads of boundary conditions: a small card in the colour of the symbol
void View::drawBoundaryLabels(QPainter& painter)
{
    QFont font = painter.font();
    font.setPointSizeF(9);
    font.setBold(true);
    painter.setFont(font);
    const QFontMetrics fm(font);
    for (auto& s : shapes)
        for (const auto& l : s->boundaryLabels())
        {
            const QPointF at = toScreen(l.pos);
            if (!(at.x() > -50 && at.y() > -50 && at.x() < width() + 50 && at.y() < height() + 50)) continue;
            float r = 0.8f, g = 0.8f, b = 0.8f;
            bcCategoryRGB(l.kind, r, g, b);
            const QSizeF sz(fm.horizontalAdvance(l.text) + 14, fm.height() + 6);
            const QRectF box(at.x() - sz.width() / 2, at.y() - sz.height() / 2, sz.width(), sz.height());
            painter.setPen(QPen(QColor::fromRgbF(r, g, b), 1.5));
            painter.setBrush(QColor(22, 76, 94, 225));
            painter.drawRoundedRect(box, 4, 4);
            painter.setPen(QColor(0xee, 0xe8, 0xd5));
            painter.drawText(box, Qt::AlignCenter, l.text);
        }
}

void View::drawLegends(QPainter& painter)
{
    // The boundary conditions: a key of the colours the pictures use ("bc:1,3" is categories 1 and 3)
    QSet<int> bcCategories;
    for (auto s : shapes)
    {
        if (!s->hasColorField() || !s->hasMesh() || s->colorMap() != "bc") continue;
        for (const QString& c : s->colorLabel().section(':', 1).split(',', Qt::SkipEmptyParts))
        {
            bcCategories.insert(c.toInt());
        }
    }

    // One bar per distinct colouring (label, map, range), right to left
    struct Bar { QString label, map; float lo, hi; };
    QVector<Bar> bars;
    for (auto s : shapes)
    {
        // (analysis results have their own card; the shading of a fit that is off -- an imported part lit up from grey to red -- is its
        // colour alone: no bar of percentages, and nothing read under the cursor)
        if (!s->hasColorField() || !s->hasMesh() || s->hasResult() || s->colorMap() == "bc" || s->colorMap() == "fit") continue;
        Bar b{s->colorLabel(), s->colorMap(), s->colorLo(), s->colorHi()};
        bool dup = false;
        for (auto& o : bars)
        {
            if (o.label == b.label && o.map == b.map)
            {   // same quantity on several parts: one bar over all of them
                o.lo = std::min(o.lo, b.lo);
                o.hi = std::max(o.hi, b.hi);
                dup = true;
                break;
            }
        }
        if (!dup && !hidden_legends.contains(b.label)) bars.push_back(b);
    }
    if ((bars.isEmpty() && bcCategories.isEmpty()) || !show_legends) return;

    QFont font = painter.font();
    font.setPointSizeF(8.5);
    painter.setFont(font);
    const QFontMetrics fm(font);
    const int barH = std::min(220, std::max(90, height() / 3));
    const int barW = 14;
    // (to the left of the result card, which has the bottom-right corner)
    int right = (m_resultPanel && m_resultPanel->isVisible()) ? m_resultPanel->geometry().left() - 10 : width() - 14;

    // (only selected surfaces shown: the key is called so)
    const QString bcTitle = (bcCategories.size() == 1 && bcCategories.contains(6)) ? "Selection" : "Boundary conditions";     // (the key of the legend: shown through T())
    if (!bcCategories.isEmpty() && !hidden_legends.contains(bcTitle))
    {
        static const QString names[kBcCategories + 1] = {"", T("Fixed support"), T("Sliding support"), T("Force"), T("Gravity"), T("Heat"),
                                         T("Selected surface"), T("Inlet"), T("Outlet"), T("Wall"), T("Slip"), T("Fixed temperature"),
                                         T("Convection"), T("Heat generated")};
        QList<int> cats = bcCategories.values();
        std::sort(cats.begin(), cats.end());
        int textW = fm.horizontalAdvance(T(bcTitle)) + 16;
        for (int c : cats) textW = std::max(textW, 18 + fm.horizontalAdvance(names[std::max(0, std::min(kBcCategories, c))]));
        const int rowH = fm.height() + 4;
        const int boxW = textW + 20, boxH = fm.height() + 20 + rowH * cats.size();
        const QRect box(right - boxW, height() - boxH - 12, boxW, boxH);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(22, 76, 94, 200));
        painter.drawRoundedRect(box, 6, 6);
        painter.setPen(QColor(0xee, 0xe8, 0xd5));
        painter.drawText(QRect(box.left() + 10, box.top() + 6, boxW - 20, fm.height()),
                         Qt::AlignLeft | Qt::AlignVCenter, T(bcTitle));
        bottom_items.push_back(QRectF(box));
        const QRect close(box.right() - 18, box.top() + 4, 14, 14);
        painter.setPen(QColor(0xee, 0xe8, 0xd5, 170));
        painter.drawText(close, Qt::AlignCenter, QString(QChar(0x00d7)));
        legend_close.push_back({close, bcTitle});
        int y = box.top() + fm.height() + 14;
        for (int c : cats)
        {
            float r, g, b;
            bcCategoryRGB(c, r, g, b);
            painter.setPen(QPen(QColor(0x93, 0xa1, 0xa1), 1));
            painter.setBrush(QColor::fromRgbF(r, g, b));
            painter.drawRect(QRect(box.left() + 10, y + 2, 12, fm.height() - 2));
            painter.setPen(QColor(0xee, 0xe8, 0xd5));
            painter.drawText(QRect(box.left() + 28, y, textW, rowH), Qt::AlignLeft | Qt::AlignVCenter,
                             names[std::max(0, std::min(kBcCategories, c))]);
            y += rowH;
        }
        right = box.left() - 8;
    }
    for (const auto& b : bars)
    {
        auto num = [](float v) { return QString::number(v, 'g', 4); };
        const int ticks = 5;
        int textW = fm.horizontalAdvance(b.label) + 16;   // (+ the close button)
        for (int k = 0; k < ticks; k++)
        {
            textW = std::max(textW, barW + 8 + fm.horizontalAdvance(
                num(b.lo + (b.hi - b.lo) * k / (ticks - 1))));
        }
        const int boxW = textW + 20;
        const int boxH = barH + fm.height() * 2 + 18;
        const QRect box(right - boxW, height() - boxH - 12, boxW, boxH);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(22, 76, 94, 200));
        painter.drawRoundedRect(box, 6, 6);

        painter.setPen(QColor(0xee, 0xe8, 0xd5));
        painter.drawText(QRect(box.left() + 10, box.top() + 6, boxW - 20, fm.height()),
                         Qt::AlignLeft | Qt::AlignVCenter, b.label);
        bottom_items.push_back(QRectF(box));

        // Its close button (View > Show legends brings it back)
        const QRect close(box.right() - 18, box.top() + 4, 14, 14);
        painter.setPen(QColor(0xee, 0xe8, 0xd5, 170));
        painter.drawText(close, Qt::AlignCenter, QString(QChar(0x00d7)));
        legend_close.push_back({close, b.label});

        const QRect bar(box.left() + 10, box.top() + fm.height() + 14, barW, barH);
        QLinearGradient g(bar.bottomLeft(), bar.topLeft());
        for (int k = 0; k <= 16; k++)
        {
            g.setColorAt(k / 16.0, colormapColor(b.map, k / 16.0f));
        }
        painter.setBrush(g);
        painter.setPen(QPen(QColor(0x93, 0xa1, 0xa1), 1));
        painter.drawRect(bar);

        painter.setPen(QColor(0xee, 0xe8, 0xd5));
        for (int k = 0; k < ticks; k++)
        {
            const int y = bar.bottom() - (bar.height() * k) / (ticks - 1);
            painter.drawLine(bar.right(), y, bar.right() + 4, y);
            painter.drawText(QRect(bar.right() + 8, y - fm.height() / 2, textW, fm.height()),
                             Qt::AlignLeft | Qt::AlignVCenter,
                             num(b.lo + (b.hi - b.lo) * k / (ticks - 1)));
        }
        right = box.left() - 8;
    }
}

void View::resizeGL(int width, int height)
{
    QTimer::singleShot(0, this, [this] { placeLivePanel(); });
    camera.size = {width, height};
    pick_timer.start();
    if (m_overlay)
    {
        m_overlay->setGeometry(rect());
    }
    placeResultPanel();
}

void View::placeLivePanel()
{
    if (!m_livePanel || !m_livePanel->isVisible()) return;
    // (a card the user sized keeps its size, and one the user moved keeps its place: see CardController)
    if (!CardController::sized(m_livePanel))
        m_livePanel->resize(std::max(260, std::min(m_livePanel->textWidth(), width() - 40)), m_livePanel->sizeHint().height() + 24);
    if (!CardController::moved(m_livePanel))
        m_livePanel->move((width() - m_livePanel->width()) / 2, 10);
    m_livePanel->raise();
}

void View::placeResultPanel()
{
    if (!m_resultPanel || !m_resultPanel->isVisible()) return;
    // (a card the user sized keeps its size, and one the user moved keeps its place: see CardController)
    if (!CardController::sized(m_resultPanel))
    {
        const int h = std::min(m_resultPanel->sizeHint().height(), std::max(160, height() - 180));
        m_resultPanel->resize(std::max(240, std::min(m_resultPanel->labelsWidth(), width() / 2)), h);
    }
    if (!CardController::moved(m_resultPanel))
        m_resultPanel->move(width() - m_resultPanel->width() - 12, height() - m_resultPanel->height() - 44);
}

void View::updateResultPanel()
{
    // The legend of a result is there when the result is: a shape that has its mesh (what is drawn), not one that is still being meshed or
    // one that has gone
    Shape* r = nullptr;
    for (auto s : shapes) if (s->hasResult() && s->getMesh()) { r = s; break; }
    if (!r)
    {
        m_resultPanel->hide();
        m_stepTimer.stop();
        m_flowTimer.stop();
        return;
    }
    ResultPanel::State st;
    for (const auto& c : r->channels()) st.labels << c.label;
    st.current = r->channel();
    st.lo = r->displayLo();
    st.hi = r->displayHi();
    st.map = r->colorMap();
    st.note = r->valueNote();
    st.noteHelp = r->valueNoteHelp();
    st.elementInfo = r->hasElements() ? r->elementText() : QString();
    st.hasDeform = r->hasDeformation();
    st.scale = r->deformScale();
    st.autoScale = r->deformAuto();
    st.hasElements = r->hasElements();
    st.showElements = r->showElements();
    st.steps = r->stepCount();
    st.step = r->step();
    st.stepLabel = r->stepLabel();
    st.playing = m_stepTimer.isActive();
    st.playMode = m_playMode;
    st.playSpeed = m_playSpeed;
    st.hasFlow = r->hasFlowLines();
    st.showFlow = r->showFlowLines();
    st.opacity = r->opacityValue();
    bool flowing = false;
    for (auto s : shapes) if (s->showsFlowLines()) flowing = true;
    if (flowing && !m_flowTimer.isActive()) m_flowTimer.start();
    if (!flowing) m_flowTimer.stop();
    m_resultPanel->setState(st);
    m_resultPanel->show();
    m_resultPanel->raise();
    placeResultPanel();
    // The section's whole-element display follows the shapes
    for (auto s : shapes)
    {
        if (s->hasElements())
            s->setElementSection(section.enabled && section.clip, section.clipPlane(), section.wholeElements);
    }
    updateSectionElements();
}

void View::updateSectionElements()
{
    bool cuts = false;
    if (section.enabled && section.clip)
    {
        const QVector4D plane = section.clipPlane();
        for (auto s : shapes)
        {
            if (!s->hasResult() || !s->hasElements() || !s->showElements() || !s->hasMesh()) continue;
            // the plane a x + b y + c z + d = 0 cuts the part's box when the plane function takes both signs on it
            const auto& b = s->getMeshBounds();
            const double lo[3] = {b.lower.x(), b.lower.y(), b.lower.z()};
            const double hi[3] = {b.upper.x(), b.upper.y(), b.upper.z()};
            const double n[3] = {plane.x(), plane.y(), plane.z()};
            double least = plane.w(), most = plane.w();
            for (int k = 0; k < 3; ++k)
            {
                least += std::min(n[k] * lo[k], n[k] * hi[k]);
                most += std::max(n[k] * lo[k], n[k] * hi[k]);
            }
            if (least <= 0 && most >= 0) { cuts = true; break; }
        }
    }
    if (cuts != m_sectionElements)
    {
        m_sectionElements = cuts;
        emit sectionCutsElements(cuts);
    }
}

void View::syncPicker()
{
    if (pick_timer.isActive())
    {
        pick_timer.stop();
        redrawPicker();
    }
}

void View::mouseMoveEvent(QMouseEvent* event)
{
    QOpenGLWidget::mouseMoveEvent(event);
    event->accept();

    if (field_drag)
    {
        dragFieldGizmo(event->pos());
        mouse.pos = event->pos();
        return;
    }
    if (section_drag)
    {
        QPointF p, q;
        float len;
        if (sectionHandle(p, q, len))
        {
            // Project the mouse movement on the plane normal's screen direction
            const QPointF dir = q - p;
            const double l2 = QPointF::dotProduct(dir, dir);
            if (l2 > 1)
            {
                const double t = QPointF::dotProduct(QPointF(event->pos() - section_press), dir) / l2;
                const int a = section.axis;
                const float offset = std::max(settings.min[a], std::min(settings.max[a],
                                              float(section_press_offset + t * len)));
                emit(sectionOffsetDragged(offset));
            }
        }
        mouse.pos = event->pos();
        return;
    }
    if (handle_drag)
    {
        dragHandle(event->pos());
        mouse.pos = event->pos();
        return;
    }
    if (mouse.state == mouse.DRAG_RECT)
    {
        // (the selection rectangle grows with the mouse)
        rect_now = event->pos();
        m_overlay->update();
        mouse.pos = event->pos();
        return;
    }
    if (mouse.state == mouse.RELEASED)
    {
        const HandleGrip grip = handleAt(event->pos());
        if (!(grip == handle_hover))
        {
            handle_hover = grip;
            m_overlay->update();
        }
        const int part = fieldGizmoHit(event->pos());
        const bool hover = sectionHandleHit(event->pos());
        if (hover != section_hover || part != field_hover)
        {
            section_hover = hover;
            field_hover = part;
            setCursor(hover || part ? Qt::SizeAllCursor : Qt::ArrowCursor);
            m_overlay->update();
        }
        updateSectionReadout(event->pos());
    }
    if (show_triad && mouse.state == mouse.RELEASED)
    {
        const int h = triadHit(event->pos());
        if (h != triad_hover)
        {
            triad_hover = h;
            update();
        }
    }

    if (mouse.state == mouse.DRAG_ROT)
    {
        camera.rotateIncremental(event->pos() - mouse.pos);
        update();
    }
    else if (mouse.state == mouse.DRAG_PAN)
    {
        camera.panIncremental(event->pos() - mouse.pos);
        update();
    }
    else if (mouse.state == mouse.DRAG_EVAL)
    {
        // Convert to 3D coordinates, then find the 3D ray that's
        // represented by the mouse position + viewing angle
        cursor_pos = toModelPos(event->pos(), 0);
        auto ray = cursor_pos - toModelPos(event->pos(), 1);

        // Slide pos down the ray to minimize distance to drag start
        cursor_pos += ray * QVector3D::dotProduct(drag_start - cursor_pos, ray);

        // Solve for the point on the normal ray that is closest to the cursor ray
        // https://en.wikipedia.org/wiki/Skew_lines#Distance_between_two_skew_lines
        const auto n = QVector3D::crossProduct(drag_dir, ray);
        const auto n2 = QVector3D::crossProduct(ray, n);
        cursor_pos = drag_start +
            drag_dir * QVector3D::dotProduct(cursor_pos - drag_start, n2) /
            QVector3D::dotProduct(drag_dir, n2);

        // (the numbers of a gizmo on the same part stay where they are)
        auto sol = libfive::Solver::findRoot(
                *drag_eval.first, drag_eval.second,
                drag_target->getVars(),
                {cursor_pos.x(), cursor_pos.y(), cursor_pos.z()},
                drag_target->gizmoVars());
        emit(varsDragged(QMap<libfive::Tree::Id, float>(sol.second)));

        drag_valid = fabs(sol.first) < 1e-6;
        bool changed = false;
        for (auto& s : shapes)
        {
            changed |= s->updateVars(sol.second);
        }
        if (changed)
        {
            busy.show();
            emit(renderBusy(true));
            requestSlice();
        }
    }
    else
    {
        checkHoverTarget(event->pos());
        if (handle_hover.kind >= 0)
        {
            setCursor(handle_hover.kind == 3 ? Qt::SizeAllCursor : Qt::PointingHandCursor);
        }
    }
    mouse.pos = event->pos();
}

QVector3D View::toModelPos(QPoint pt) const
{
    float pick_z = 0;
    if (pick_img.valid(pt))
    {
        const int at = pt.x() + pick_img.width() * (pick_img.height() - pt.y() - 1);
        if (at >= 0 && at < pick_depth.size()) pick_z = 2 * pick_depth.at(at) - 1;      // (the depths are of the picture's own size)
    }
    return toModelPos(pt, pick_z);
}

QVector3D View::toModelPos(QPoint pt, float z) const
{
    return camera.M().inverted().map(QVector3D(
            (pt.x() * 2.0) / pick_img.width() - 1,
            1 - (pt.y() * 2.0) / pick_img.height(), z));
}

void View::mousePressEvent(QMouseEvent* event)
{
    QOpenGLWidget::mousePressEvent(event);
    event->accept();
    // A click in the viewport puts the keyboard in the viewport (from the editor too): the keys that work on the
    // selection (E, R, G, H, I) are not typed into the script
    if (!hasFocus()) setFocus(Qt::MouseFocusReason);

    // A legend's close button
    if (event->button() == Qt::LeftButton)
    {
        for (const auto& c : legend_close)
        {
            if (c.first.contains(event->pos()))
            {
                hidden_legends.insert(c.second);
                probe_valid = false;
                update();
                return;
            }
        }
    }

    if (mouse.state == mouse.RELEASED)
    {
        if (event->button() == Qt::LeftButton && !m_errorText.isEmpty() && m_errorRect.contains(event->pos()))
        {
            // The error banner: the editor goes to the line (the press is not a selection click)
            emit(errorClicked(m_errorLine));
            press_pos = QPoint(-100000, -100000);
            press_target = nullptr;
            return;
        }
        if (event->button() == Qt::LeftButton)
        {
            // A part's handle (a move arrow or a rotation ring)
            const HandleGrip grip = handleAt(event->pos());
            if (grip.kind >= 0)
            {
                beginHandleDrag(grip, event->pos());
                if (handle_drag)
                {
                    press_pos = QPoint(-100000, -100000);   // not a selection click
                    press_target = nullptr;
                    return;
                }
            }
            else
            {
                // The provisional gizmo (the real one is not there yet): the press is kept, and the model is made ready at once
                int kind = -1, axis = 0;
                if (provisionalGripAt(event->pos(), &kind, &axis))
                {
                    m_wait = WaitGrip{true, kind, axis, 0, m_provLines};
                    m_waitTimer.start();
                    setCursor(Qt::BusyCursor);
                    press_pos = QPoint(-100000, -100000);   // not a selection click
                    press_target = nullptr;
                    emit(provisionalPressed());
                    return;
                }
            }
        }
        if (event->button() == Qt::LeftButton && fieldGizmoHit(event->pos()))
        {
            // The field viewer's disc: an arrow in its plane moves it along that axis, the dot moves it freely in the plane
            field_drag = fieldGizmoHit(event->pos());
            field_press = event->pos();
            field_press_centre = m_fieldView.centre;
            QPointF m, u, v, n;
            float len;
            fieldGizmo(m, u, v, n, len);
            field_len = len;
            field_dir = field_drag == 1 ? u - m : field_drag == 2 ? v - m : n - m;
            if (field_drag == 3)
            {
                const int ax = m_fieldView.axis, ua = FieldSlice::uAxis(ax), va = FieldSlice::vAxis(ax);
                const QVector3D a = toModelPos(event->pos(), 0), b = toModelPos(event->pos(), 0.25);
                field_press_grab = QVector3D();
                if (b[ax] != a[ax])
                {
                    const QVector3D hit = a + (m_fieldView.offset() - a[ax]) / (b[ax] - a[ax]) * (b - a);
                    field_press_grab[ua] = m_fieldView.centre[ua] - hit[ua];
                    field_press_grab[va] = m_fieldView.centre[va] - hit[va];
                }
            }
            press_pos = QPoint(-100000, -100000);   // not a selection click
            press_target = nullptr;
            m_overlay->update();
            return;
        }
        if (event->button() == Qt::LeftButton && sectionHandleHit(event->pos()))
        {
            // Drag the section plane along its normal
            section_drag = true;
            section_press = event->pos();
            section_press_offset = section.offset;
            press_pos = QPoint(-100000, -100000);   // not a selection click
            press_target = nullptr;
            return;
        }
        const int hit = (event->button() == Qt::LeftButton && show_triad)
            ? triadHit(event->pos()) : -1;
        if (hit >= 0)
        {
            // Orientation triad: look at the model along that axis
            camera.lookFrom(triad_hits[hit].second);
            press_pos = QPoint(-100000, -100000);   // not a selection click
            press_target = nullptr;
            return;
        }
        if ((event->button() == Qt::LeftButton && (event->modifiers() & Qt::ShiftModifier)) ||
            event->button() == Qt::MiddleButton)
        {
            // Shift + left drag and the middle button's drag turn the view; such a press selects nothing
            press_pos = QPoint(-100000, -100000);   // not a selection click
            press_target = nullptr;
            press_ctrl = false;
            drag_target = nullptr;
            mouse.state = mouse.DRAG_ROT;
            return;
        }
        if (event->button() == Qt::LeftButton)
        {
            syncPicker();
            auto picked = pick_img.valid(event->pos())
                ? (pick_img.pixel(event->pos()) & 0xFFFFFF) : 0;
            drag_target = (picked && int(picked) <= shapes.size())
                ? shapes.at(picked - 1) : nullptr;
            press_pos = event->pos();
            press_target = drag_target;
            // (Ctrl + click selects: it never pulls the surface it is on)
            press_ctrl = bool(event->modifiers() & Qt::ControlModifier);
            // (a part in the gizmo mode is moved by the gizmo and a locked one not at all: only its
            // own surfaces, made of numbers that can change, are pulled)
            // (and with several shapes selected nothing is pulled by its surface: they only have the gizmo)
            if (picked && !press_ctrl && selectedShapeCount() < 2 && drag_target->nativeDragOk())
            {
                this->setCursor(Qt::ClosedHandCursor);
                emit(dragStart());
                drag_target->setGrabbed(true);
                drag_valid = true;

                drag_start = toModelPos(event->pos());
                auto df = drag_target->dragFrom(drag_start);
                drag_eval.first.reset(df.first);
                drag_eval.second = df.second;

                auto norm = drag_eval.first->deriv(
                        {drag_start.x(), drag_start.y(), drag_start.z()},
                        *df.second);
                drag_dir = {norm.x(), norm.y(), norm.z()};

                mouse.state = mouse.DRAG_EVAL;
            }
            else
            {
                // Nothing to pull: a drag draws the selection rectangle (with Ctrl held, the shapes inside join the
                // selection), and a click that does not move selects what is under the cursor, as it always did
                drag_target = nullptr;
                rect_start = rect_now = event->pos();
                rect_add = press_ctrl;
                mouse.state = mouse.DRAG_RECT;
            }
        }
        else if (event->button() == Qt::RightButton)
        {
            // (a right-click that does not move opens the menu of the surface selection: what is under it)
            syncPicker();
            const auto picked = pick_img.valid(event->pos())
                ? (pick_img.pixel(event->pos()) & 0xFFFFFF) : 0;
            right_press_pos = event->pos();
            right_press_target = (picked && int(picked) <= shapes.size()) ? shapes.at(picked - 1) : nullptr;
            right_press_empty = !right_press_target;       // (empty space: its own menu, if the click does not drag)
            right_press_point = right_press_target ? toModelPos(event->pos()) : QVector3D();
            mouse.state = mouse.DRAG_PAN;
        }
    }
}

void View::mouseReleaseEvent(QMouseEvent* event)
{
    QOpenGLWidget::mouseReleaseEvent(event);
    event->accept();
    if (field_drag)
    {
        field_drag = 0;
        m_overlay->update();
        return;
    }
    if (section_drag)
    {
        section_drag = false;
        m_overlay->update();
        return;
    }
    if (m_wait.on)
    {
        cancelWaitingGrip();        // (a press that waited for the real gizmo and was let go: nothing to do)
        return;
    }
    if (handle_drag)
    {
        handle_drag = false;
        handle_hover = handleAt(event->pos());
        setCursor(handle_hover.kind >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        redrawPicker();
        emit(dragEnd());
        m_overlay->update();
        return;
    }

    // The selection rectangle was let go: the shapes that lie wholly inside it are selected (a rectangle that holds
    // none leaves the selection as it is, so that a slip of the mouse does not lose it).  A press that did not move
    // was a click: it selects what was under the cursor (below)
    if (mouse.state == mouse.DRAG_RECT && event->button() == Qt::LeftButton)
    {
        const QRect r = QRect(rect_start, event->pos()).normalized();
        const bool dragged = (event->pos() - rect_start).manhattanLength() >= 4;
        mouse.state = mouse.RELEASED;
        m_overlay->update();
        if (dragged)
        {
            if (r.width() >= 4 && r.height() >= 4)
            {
                const QList<int> lines = linesInside(r);
                if (!lines.isEmpty()) emit(shapesRectSelected(lines, rect_add));
            }
            press_ctrl = false;
            press_target = nullptr;
            return;
        }
    }

    // A left click that didn't drag selects what was under the cursor (with Ctrl: adds it to the selection, or
    // takes it out when it is in)
    if (event->button() == Qt::LeftButton &&
        (event->pos() - press_pos).manhattanLength() < 4)
    {
        const int line = (press_target && shapes.contains(press_target))
            ? press_target->sourceLine() : -1;
        if (press_ctrl)
        {
            if (line >= 0) emit(shapeToggled(line));
        }
        else
        {
            emit(shapeClicked(line));
        }
    }
    if (event->button() == Qt::LeftButton) press_ctrl = false;
    press_target = nullptr;

    if (event->button() == Qt::RightButton && right_press_target &&
        (event->pos() - right_press_pos).manhattanLength() < 4 && shapes.contains(right_press_target))
    {
        Shape* target = right_press_target;
        right_press_target = nullptr;
        right_press_empty = false;
        mouse.state = mouse.RELEASED;
        const int line = target->sourceLine();
        if (line >= 0) showSurfaceMenu(event->globalPos(), line, right_press_point,
                                       scaleAt(event->pos(), right_press_point));
        return;
    }
    if (event->button() == Qt::RightButton && right_press_empty &&
        (event->pos() - right_press_pos).manhattanLength() < 4)
    {
        right_press_empty = false;
        mouse.state = mouse.RELEASED;
        showEmptyMenu(event->globalPos(), event->pos());
        return;
    }
    right_press_empty = false;
    right_press_target = nullptr;

    if (mouse.state != mouse.RELEASED)
    {
        redrawPicker();
        if (drag_target)
        {
            drag_target->setGrabbed(false);
            drag_target = nullptr;
            emit(dragEnd());
            checkHoverTarget(event->pos());
        }
    }
    mouse.state = mouse.RELEASED;
}

bool View::selectSurfaceAt(QPoint pos, const QString& mode, double angle, double radius)
{
    syncPicker();
    const auto picked = pick_img.valid(pos) ? (pick_img.pixel(pos) & 0xFFFFFF) : 0;
    if (!picked || int(picked) > shapes.size()) return false;
    Shape* target = shapes.at(picked - 1);
    if (target->sourceLine() < 0) return false;
    emit(surfaceSelectRequested(target->sourceLine(), toModelPos(pos), mode, angle, radius));
    return true;
}

QVector3D View::placeOnRay(QPoint pos) const
{
    // A cursor is two-dimensional, so the depth is chosen: the closest to the origin's, that is, the point of the ray
    // under the cursor in the plane through the origin that faces the viewer (in a front view, y = 0)
    const QVector3D a = toModelPos(pos, -1.0f), b = toModelPos(pos, 1.0f);
    const QVector3D d = b - a;
    const float len2 = d.lengthSquared();
    if (!(len2 > 1e-12f)) return a;
    const QVector3D n = camera.towardViewer().normalized();
    const float dn = QVector3D::dotProduct(d, n);
    if (n.lengthSquared() > 0.5f && std::abs(dn) > 1e-6f * std::sqrt(len2))
        return a - QVector3D::dotProduct(a, n) / dn * d;
    return a - QVector3D::dotProduct(a, d) / len2 * d;      // (looking along the plane: the point closest to the origin)
}

double View::scaleAt(QPoint pos, const QVector3D& point) const
{
    const float z = camera.M().map(point).z();
    return double((toModelPos(pos + QPoint(100, 0), z) - toModelPos(pos, z)).length());
}

void View::loadMenuCatalog()
{
    if (!m_catalog.isEmpty() || !m_catalogSource) return;
    m_catalog = QJsonDocument::fromJson(m_catalogSource().toUtf8()).object();
    TypeIcons::setKinds(m_catalog["kinds"].toObject());
}

namespace {

// What the simulation menu holds besides the analyses: the groups of primitives that are about a simulation (a material, a
// fluid, the supports and loads and the thermal and flow conditions an analysis is given), and the operation that makes
// the boundary conditions of a model
bool isSimulationPrimitive(const QString& group)
{
    return group == "Materials" || group == "Fluids" || group == "Supports and loads" ||
           group == "Thermal conditions" || group == "Flow conditions";
}

bool isSimulationOperation(const QString& group)
{
    return group == "Simulations" || group == "Conditions";
}

}   // anonymous namespace

void View::addBodyFromField(QMenu* menu, int line, const QVector3D& point, double scale)
{
    const int generation = m_scene ? m_scene->generation() : -1;
    auto action = menu->addAction(TypeIcons::icon("solid"), T("Body from field"), this,
                                  [=]{ emit(createRequested("operation", "body_from_field", point, scale, line, generation)); });
    action->setToolTip(T("A body where the field is below 0: body_from_field(field, level) -- change the level in the script to take "
                       "another surface of it.  (Operation > Field math has it too, with the rest of the field math.)"));
}

void View::fillSimulationPrimitives(QMenu* menu, const QVector3D& point, double scale, int atLine)
{
    // New material, New fluid, New support or load, New thermal condition, New flow condition: each a menu of its own, in the
    // order the catalog has them
    static const QHash<QString, QString> titles = {
        {"Materials", T("New material")}, {"Fluids", T("New fluid")}, {"Supports and loads", T("New support or load")},
        {"Thermal conditions", T("New thermal condition")}, {"Flow conditions", T("New flow condition")}};
    static const QStringList order = {"Materials", "Fluids", "Supports and loads", "Thermal conditions", "Flow conditions"};
    QHash<QString, QList<QJsonObject>> groups;
    for (const auto v : m_catalog["primitives"].toArray())
    {
        const auto p = v.toObject();
        const QString g = p["group"].toString();
        if (isSimulationPrimitive(g)) groups[g] << p;
    }
    for (const QString& g : order)
    {
        if (!groups.contains(g)) continue;
        const auto& entries = groups[g];
        const QString type = entries.first()["type"].toString("conditions");
        auto sub = menu->addMenu(TypeIcons::icon(type), titles.value(g, T(g)));
        sub->setToolTipsVisible(true);
        for (const auto& p : entries)
        {
            const QString name = p["name"].toString();
            auto action = sub->addAction(TypeIcons::icon(p["type"].toString(type)), name,
                                         this, [=]{ emit(createRequested("primitive", name, point, scale, atLine, -1)); });
            // (a condition made of the models selected: not offered for a selection the library cannot write it for)
            if (p["with_bodies"].toBool() && m_scene && !m_scene->entryAllowed("primitive", name, atLine)) action->setEnabled(false);
            if (p["with_bodies"].toBool())
            {
                action->setToolTip(T("With several models selected: the first is the body, the others are where it acts (bodies, fields, "
                                   "selected surfaces) -- written with the surface of the body there.  With one model, or none: a placeholder for "
                                   "where it acts, to drop a body, a field or a surface on."));
            }
        }
    }
}

void View::fillOperations(QMenu* menu, int line, const QVector3D& point, double scale, bool simulations)
{
    QJsonArray ops;
    for (const auto v : m_catalog["operations"].toArray())
    {
        if (v.toObject()["quick"].toBool()) continue;          // (in the first menu: see addQuickOperations)
        if (isSimulationOperation(v.toObject()["group"].toString()) == simulations) ops.append(v);
    }
    if (ops.isEmpty())
    {
        menu->addAction(T("(not loaded yet: run the script first)"))->setEnabled(false);
        return;
    }
    const bool haveOther = m_scene && m_scene->hasOtherModel(line);
    // (which run of the script the menu is of: the line it is opened on means that run's models only)
    const int generation = m_scene ? m_scene->generation() : -1;
    // (a submenu for each group: the whole list in one column would not fit the screen)
    QString group;
    QMenu* sub = nullptr;
    for (const auto v : ops)
    {
        const auto o = v.toObject();
        if (simulations)
        {
            // (the boundary conditions and the analyses are the entries of the simulation menu, apart by a line)
            if (sub && o["group"].toString() != group) menu->addSeparator();
            group = o["group"].toString();
            sub = menu;
        }
        else if (!sub || o["group"].toString() != group)
        {
            group = o["group"].toString();
            sub = group == "Custom blocks" ? menu->addMenu(TypeIcons::icon("block"), T(group)) : menu->addMenu(T(group));
            sub->setToolTipsVisible(true);
        }
        const QString name = o["name"].toString();
        auto action = sub->addAction(name, this, [=]{ emit(createRequested("operation", name, point, scale, line, generation)); });
        if (o["type"].toString() == "block") action->setIcon(TypeIcons::icon("block"));
        if (simulations) action->setIcon(TypeIcons::icon(group == "Conditions" ? "conditions" : "simulation"));
        if (!o["doc"].toString().isEmpty()) action->setToolTip(o["doc"].toString());
        else if (o["other"].toBool() && !o["needs_other"].toBool(true))
        {
            action->setToolTip(T("With several models selected: the first is the one this is made for, the others are what it uses -- where it "
                               "acts, its conditions, its material.  With one model: a first version to change in the script."));
        }
        if (o["needs_other"].toBool(o["other"].toBool()) && !haveOther)
        {
            action->setEnabled(false);
            action->setToolTip(T("Needs a second model"));
        }
        // (what is selected cannot make it: the library refuses the call for this selection -- a part with supports and loads for a set of
        // conditions, conditions with no part for a simulation ...)
        if (m_scene && action->isEnabled() && !m_scene->entryAllowed("operation", name, line)) action->setEnabled(false);
    }
}

void View::addQuickOperations(QMenu* menu, int line, const QVector3D& point, double scale)
{
    const int generation = m_scene ? m_scene->generation() : -1;
    for (const auto v : m_catalog["operations"].toArray())
    {
        const auto o = v.toObject();
        if (!o["quick"].toBool()) continue;
        const QString name = o["name"].toString();
        // (the distance from a point is made of a point: it is there for a point, and only then)
        const bool fromPoint = name == "distance_to_point";
        if (fromPoint && (!m_scene || !m_scene->pointsOnly(line))) continue;
        auto action = menu->addAction(TypeIcons::icon(fromPoint ? "field" : name == "center" ? "point" : "solid"), name, this,
                                      [=]{ emit(createRequested("operation", name, point, scale, line, generation)); });
        action->setToolTip(fromPoint ? T("A field: how far every place is from this point")
                           : name == "center" ? T("The middle of the model's bounding box, as a point")
                                              : T("The smallest box, aligned with the axes, that holds the model: a body of its own"));
        if (m_scene && !m_scene->entryAllowed("operation", name, line)) action->setEnabled(false);
    }
}

void View::showMenuForLine(int line0, QPoint globalPos)
{
    loadMenuCatalog();
    QVector3D centre;
    if (line0 >= 0 && m_scene && m_scene->modelAtLine(line0, &centre))
    {
        // (the place of a right-click is the middle of the model: operations that need a place or a size take them from it)
        showSurfaceMenu(globalPos, line0, centre, scaleAt(rect().center(), centre), false);
        return;
    }
    showEmptyMenu(globalPos, rect().center(), line0);
}

void View::showEmptyMenu(QPoint globalPos, QPoint pos, int atLine)
{
    loadMenuCatalog();
    QVector3D point = placeOnRay(pos);
    const double scale = scaleAt(pos, point);
    // Nothing outside the render region is drawn: a primitive whose place is outside it goes to the nearest place
    // that is inside (half its size in from the edge), so it is always seen
    {
        const QVector3D lo = settings.min, hi = settings.max;
        auto inside = [&](float v, float a, float b) {
            const float m = float(std::min(0.5 * scale, 0.5 * double(b - a)));
            return std::max(a + m, std::min(b - m, v));
        };
        if (hi.x() > lo.x() && hi.y() > lo.y() && hi.z() > lo.z())
            point = QVector3D(inside(point.x(), lo.x(), hi.x()), inside(point.y(), lo.y(), hi.y()),
                              inside(point.z(), lo.z(), hi.z()));
    }

    auto menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->setToolTipsVisible(true);
    // The field the field viewer shows (the disc is not a model that can be right-clicked: this is its menu): a body from it at once
    if (fieldShown() && m_scene)
    {
        addBodyFromField(menu, -1, point, scale);
        menu->addSeparator();
    }
    // What can be made, a menu for each kind of thing, each with its own icon: 3D shapes, 2D shapes, a point, surfaces,
    // fields, and the custom blocks that need no model.  (A kind with one entry is that entry.)  What a simulation is made of --
    // materials, fluids, supports and loads, thermal and flow conditions -- is in the simulation menu, not here
    const QJsonArray list = m_catalog["primitives"].toArray();
    if (list.isEmpty()) menu->addAction(T("(not loaded yet: run the script first)"))->setEnabled(false);
    {
        static const QHash<QString, QString> titles = {
            {"3D", T("New 3D shape")}, {"2D", T("New 2D shape")}, {"Points", T("New point")}, {"Surfaces", T("New surface")},
            {"Fields", T("New field")}, {"Custom blocks", T("New custom block")}};
        QStringList order;
        QHash<QString, QList<QJsonObject>> groups;
        for (const auto v : list)
        {
            const auto p = v.toObject();
            const QString g = p["group"].toString();
            if (isSimulationPrimitive(g)) continue;
            if (!groups.contains(g)) order << g;
            groups[g] << p;
        }
        for (const QString& g : order)
        {
            const auto& entries = groups[g];
            const QString type = entries.first()["type"].toString("solid");
            auto make = [&](QMenu* into, const QJsonObject& p) {
                const QString name = p["name"].toString();
                auto action = into->addAction(TypeIcons::icon(p["type"].toString(type)), name,
                                              this, [=]{ emit(createRequested("primitive", name, point, scale, atLine, -1)); });
                if (p.contains("doc") && !p["doc"].toString().isEmpty()) action->setToolTip(p["doc"].toString());
                return action;
            };
            if (entries.size() == 1 && g != "Custom blocks")
            {
                auto a = make(menu, entries.first());
                a->setText(titles.value(g, T("New %1").arg(entries.first()["name"].toString())));
                continue;
            }
            auto sub = menu->addMenu(TypeIcons::icon(type), titles.value(g, T(g)));
            sub->setToolTipsVisible(true);
            for (const auto& p : entries) make(sub, p);
        }
    }
    menu->addSeparator();
    {
        auto imp = menu->addAction(T("Import model..."), this, [=]{ emit(importRequested()); });
        imp->setToolTip(T("A STEP file or a mesh (STL, OBJ, PLY, 3MF, glTF): each part is reconstructed or tessellated, "
                        "whichever suits it."));
    }
    auto ops = menu->addMenu(T("Add operation"));
    auto sims = menu->addMenu(TypeIcons::icon("simulation"), T("Add simulation"));
    sims->setToolTipsVisible(true);
    // (a material, a fluid, a support or a condition is a model of its own: no other model is needed for it)
    fillSimulationPrimitives(sims, point, scale, atLine);
    sims->addSeparator();
    if (m_scene && !m_scene->hasModel())
    {
        ops->menuAction()->setEnabled(false);
        ops->menuAction()->setToolTip(T("There is no model to work on yet"));
    }
    else
    {
        addQuickOperations(ops, -1, point, scale);
        fillOperations(ops, -1, point, scale);
    }
    // (the analyses work on what is selected -- a part, its conditions, a material -- and write a placeholder for what is not: they are always there)
    fillOperations(sims, -1, point, scale, true);
    menu->popup(globalPos);
}

void View::showSurfaceMenu(QPoint globalPos, int line, const QVector3D& point, double scale, bool onSurface)
{
    loadMenuCatalog();
    auto menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->setToolTipsVisible(true);
    // A field is not drawn, so what is made from it is the first thing its menu offers: a body (field_from_body is the way back)
    if (m_scene && m_scene->typeAtLine(line) == "field")
    {
        addBodyFromField(menu, line, point, scale);
        menu->addSeparator();
    }
    addQuickOperations(menu, line, point, scale);
    fillOperations(menu->addMenu(T("Operation")), line, point, scale);
    auto sims = menu->addMenu(TypeIcons::icon("simulation"), T("Simulation"));
    sims->setToolTipsVisible(true);
    fillSimulationPrimitives(sims, point, scale, line);
    sims->addSeparator();
    fillOperations(sims, line, point, scale, true);
    {
        // A resolution of its own for this model: the line is written under its definition (and the scene's resolution is its number)
        const QString type = m_scene ? m_scene->typeAtLine(line) : QString();
        auto own = menu->addAction(T("Custom resolution"), this, [=]{ if (m_scene) m_scene->addCustomResolution(line); });
        own->setToolTip(T("Draw this model at a resolution of its own, whatever the scene's is: writes custom_resolution(x, number) under its "
                        "definition; the number is a field under the model in the tree"));
        if (type == "field" || type == "point" || type.isEmpty())
        {
            own->setEnabled(false);
            own->setToolTip(T("A resolution of its own is for a model that is drawn: a shape, a surface"));
        }
    }
    auto select = menu->addAction(T("Select Surface"), this, [=]{
        // (after this menu has closed: the menu of the surface selection takes its place)
        QTimer::singleShot(0, this, [=]{ showSelectMenu(globalPos, line, point); });
    });
    if (!onSurface)
    {
        // (opened from the text editor or the model tree: no place on the surface was clicked)
        select->setEnabled(false);
        select->setToolTip(T("The selection spreads from the place you click on the surface, so it is started by right-clicking "
                           "the surface in the viewport."));
    }
    if (m_scene)
    {
        menu->addSeparator();
        menu->addAction(T("Delete"), this, [=]{ m_scene->deleteByLine(line); });
    }
    menu->popup(globalPos);
}

void View::showSelectMenu(QPoint globalPos, int line, const QVector3D& point)
{
    if (line < 0) return;

    QSettings store;
    auto menuPtr = new QMenu(this);
    menuPtr->setAttribute(Qt::WA_DeleteOnClose);
    QMenu& menu = *menuPtr;

    // (the title and the form are widgets in the menu: they are styled like the cards of the viewport, bright
    // text on the dark menu -- they would otherwise take the default dark text)
    auto titleLabel = new QLabel(T("Select surface (flood fill)"));
    titleLabel->setStyleSheet("QLabel { color: #eee8d5; font-weight: bold; padding: 6px 12px 2px 12px; }");
    auto titleAction = new QWidgetAction(&menu);
    titleAction->setDefaultWidget(titleLabel);
    menu.addAction(titleAction);
    menu.addSeparator();

    auto holder = new QWidget;
    holder->setStyleSheet(
        "QLabel { color: #eee8d5; }"
        "QComboBox, QDoubleSpinBox { color: #eee8d5; background: rgba(0, 0, 0, 70);"
        "  border: 1px solid rgba(147, 161, 161, 90); border-radius: 3px; padding: 2px 6px;"
        "  selection-background-color: rgba(38, 139, 210, 200); }"
        "QComboBox QAbstractItemView { color: #eee8d5; background: #25607a;"
        "  selection-background-color: rgba(38, 139, 210, 200); }"
        "QPushButton { color: #eee8d5; background: rgba(147, 161, 161, 28);"
        "  border: 1px solid rgba(147, 161, 161, 70); border-radius: 4px; padding: 4px 16px; }"
        "QPushButton:hover { background: rgba(147, 161, 161, 60); }"
        "QPushButton:default { background: rgba(38, 139, 210, 170); border-color: rgba(38, 139, 210, 230);"
        "  color: white; }");
    auto form = new QFormLayout(holder);
    form->setContentsMargins(10, 4, 10, 6);
    auto mode = new QComboBox;
    mode->addItem(T("Flat face"), "flat");
    mode->addItem(T("Round / smooth faces"), "smooth");
    mode->setToolTip(T("Flat: spreads while the surface faces the way it does at the click.\n"
                     "Smooth: spreads over round faces (cylinders, fillets) until the surface bends tighter than the angle allows, or to a sharp edge."));
    mode->setCurrentIndex(store.value("select/mode", "flat").toString() == "smooth" ? 1 : 0);
    auto angle = new QDoubleSpinBox;
    angle->setRange(0.5, 90.0);
    angle->setSuffix(T(" deg"));
    angle->setDecimals(1);
    angle->setToolTip(T("Flat: how far from the click's direction the surface may face.\n"
                      "Smooth: how much the surface may turn within 10 mm: a tighter bend stops the selection."));
    // (the smooth angle is per 10 mm of the surface now, not per step of the walk: the value remembered from before means
    // something else, so it is kept under another name)
    auto defaultAngle = [](const QString& m) { return m == "smooth" ? 35.0 : 10.0; };
    auto angleKey = [](const QString& m) { return QString("select/angle-") + (m == "smooth" ? "smooth-10mm" : m); };
    angle->setValue(store.value(angleKey(mode->currentData().toString()), defaultAngle(mode->currentData().toString())).toDouble());
    connect(mode, QOverload<int>::of(&QComboBox::currentIndexChanged), angle, [=](int) {
        QSettings s;
        angle->setValue(s.value(angleKey(mode->currentData().toString()), defaultAngle(mode->currentData().toString())).toDouble());
    });
    auto radius = new QDoubleSpinBox;
    radius->setRange(0.0, 100000.0);
    radius->setDecimals(1);
    radius->setSuffix(T(" mm"));
    radius->setSpecialValueText(T("no limit"));
    radius->setValue(store.value("select/radius", 0.0).toDouble());
    radius->setToolTip(T("Stop this far from the click."));
    form->addRow(T("Spread"), mode);
    form->addRow(T("Angle"), angle);
    form->addRow(T("Radius"), radius);
    auto buttons = new QHBoxLayout;
    auto select = new QPushButton(T("Select"));
    select->setObjectName("selectSurfaceConfirm");
    select->setDefault(true);
    auto cancel = new QPushButton(T("Cancel"));
    cancel->setObjectName("selectSurfaceCancel");
    buttons->addStretch();
    buttons->addWidget(cancel);
    buttons->addWidget(select);
    form->addRow(buttons);

    // (the menu stays open until one of the buttons: Select remembers the values and asks for the selection)
    connect(select, &QPushButton::clicked, menuPtr, [=]{
        const QString m = mode->currentData().toString();
        const double a = angle->value(), r = radius->value();
        QSettings s;
        s.setValue("select/mode", m);
        s.setValue(angleKey(m), a);
        s.setValue("select/radius", r);
        menuPtr->close();
        emit(surfaceSelectRequested(line, point, m, a, r));
    });
    connect(cancel, &QPushButton::clicked, menuPtr, &QMenu::close);
    auto action = new QWidgetAction(&menu);
    action->setDefaultWidget(holder);
    menu.addAction(action);

    menu.popup(globalPos);
}

void View::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton ||
        (show_triad && triadHit(event->pos()) >= 0))
    {
        QOpenGLWidget::mouseDoubleClickEvent(event);
        return;
    }
    event->accept();

    // Double-click a shape to frame it, or the background to frame all
    syncPicker();
    const auto picked = pick_img.valid(event->pos())
        ? (pick_img.pixel(event->pos()) & 0xFFFFFF) : 0;
    if (picked && int(picked) <= shapes.size())
    {
        focusOn(QVector3D(), QVector3D(), {shapes.at(picked - 1)->sourceLine()});
    }
    else
    {
        frameAll();
    }
}

void View::standardView(int which)
{
    // Z up: front looks from -Y; Y up: front looks from +Z
    QVector3D d;
    switch (which)
    {
        case VIEW_FRONT:  d = y_up ? QVector3D(0, 0, 1)  : QVector3D(0, -1, 0); break;
        case VIEW_BACK:   d = y_up ? QVector3D(0, 0, -1) : QVector3D(0, 1, 0); break;
        case VIEW_RIGHT:  d = QVector3D(1, 0, 0); break;
        case VIEW_LEFT:   d = QVector3D(-1, 0, 0); break;
        case VIEW_TOP:    d = y_up ? QVector3D(0, 1, 0)  : QVector3D(0, 0, 1); break;
        case VIEW_BOTTOM: d = y_up ? QVector3D(0, -1, 0) : QVector3D(0, 0, -1); break;
        default:          d = y_up ? QVector3D(1, 1, 1)  : QVector3D(1, -1, 1); break;
    }
    camera.lookFrom(d);
}

int View::triadHit(QPoint pos) const
{
    int best = -1;
    float best_d = 11;
    for (int i=0; i < triad_hits.size(); ++i)
    {
        const QPointF d = QPointF(pos) - triad_hits[i].first;
        const float dist = std::sqrt(d.x() * d.x() + d.y() * d.y());
        if (dist < best_d)
        {
            best_d = dist;
            best = i;
        }
    }
    return best;
}

void View::drawTriad(QPainter& p)
{
    struct Item {
        QVector3D v;       // direction in view space
        QVector3D dir;     // world direction
        QColor color;
        QString label;
        bool positive;
    };
    const QColor colors[3] = {QColor(232, 76, 61), QColor(126, 190, 58),
                              QColor(64, 144, 232)};
    const char* names[3] = {"X", "Y", "Z"};
    const QMatrix4x4 r = camera.rotation();

    QList<Item> items;
    for (int i=0; i < 3; ++i)
    {
        for (int sign : {1, -1})
        {
            QVector3D d;
            d[i] = sign;
            items.push_back({r.map(d), d, colors[i], names[i], sign > 0});
        }
    }
    // Far items first (larger view z is further from the camera)
    std::stable_sort(items.begin(), items.end(),
                     [](const Item& a, const Item& b) { return a.v.z() > b.v.z(); });

    const float R = 34;
    // Top-right corner (the busy spinner lives bottom-right)
    const QPointF c(width() - R - 24, R + 24);

    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 14));
    p.drawEllipse(c, R + 15, R + 15);

    QFont font = p.font();
    font.setPointSizeF(8);
    font.setBold(true);
    p.setFont(font);

    // Hit targets are stored in the order drawn; the hovered one is the
    // index into that list
    QVector<QPair<QPointF, QVector3D>> hits;
    for (const auto& it : items)
    {
        const QPointF tip = c + QPointF(it.v.x(), it.v.y()) * R;
        const bool hovered = (hits.size() == triad_hover);
        if (it.positive)
        {
            p.setPen(QPen(it.color, 2.0, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(c, tip);
            p.setPen(hovered ? QPen(Qt::white, 1.5) : Qt::NoPen);
            p.setBrush(it.color);
            p.drawEllipse(tip, 8.5, 8.5);
            p.setPen(QColor(24, 24, 24));
            p.drawText(QRectF(tip.x() - 9, tip.y() - 9, 18, 18),
                       Qt::AlignCenter, it.label);
        }
        else
        {
            QColor fill = it.color;
            fill.setAlpha(hovered ? 200 : 70);
            p.setPen(QPen(it.color, 1.2));
            p.setBrush(fill);
            p.drawEllipse(tip, 6.5, 6.5);
        }
        hits.push_back({tip, it.dir});
    }
    p.restore();
    triad_hits = hits;
}

void View::wheelEvent(QWheelEvent *event)
{
    QOpenGLWidget::wheelEvent(event);
    event->accept();
    const QPoint& center = zoom_cursor_centric ? mouse.pos : rect().center();
    camera.zoomIncremental(event->angleDelta().y(), center);
    update();
    pick_timer.start();
}

void View::checkHoverTarget(QPoint pos)
{
    syncPicker();

    auto picked = pick_img.valid(pos) ? (pick_img.pixel(pos) & 0xFFFFFF) : 0;
    // (a number that is not the place of a shape in the list -- the picture is of other shapes than there are, or a pixel that is not
    // one's colour -- is nothing: every other place that reads the picture says so, and this one read past the list and wrote to what
    // it found there, which corrupted the heap while the mouse moved over a model that was still being made)
    auto target = (picked && int(picked) <= shapes.size()) ? shapes.at(picked - 1) : nullptr;
    if (target && selectedShapeCount() < 2 && target->nativeDragOk())
    {
        if (hover_target)
        {
            hover_target->setHover(false);
        }

        hover_target = target;
        hover_target->setHover(true);
    }
    else if (hover_target)
    {
        hover_target->setHover(false);
        hover_target = nullptr;
    }

    bool changed = (cursor_pos_valid != (bool)target);
    cursor_pos_valid = target;
    cursor_pos = toModelPos(pos);

    // Probe a field-coloured model at the surface point
    const bool had_probe = probe_valid;
    // (a model coloured by a field is probed while its legend is shown: closing the legend ends
    // the probing; an analysis result has its own card and is always probed)
    const bool legend_on = target && (target->hasResult() ||
                                      (show_legends && !hidden_legends.contains(target->colorLabel())));
    probe_valid = target && target->hasColorField() && target->colorMap() != "bc" && target->colorMap() != "fit" && legend_on &&
                  target->probe(cursor_pos, probe_value);
    if (probe_valid)
    {
        probe_label = target->probeLabel();
        probe_pos = pos;
    }
    changed |= (had_probe != probe_valid);

    if (cursor_pos_valid || changed)
    {
        update();
    }

    this->setCursor(hover_target ? Qt::OpenHandCursor : Qt::ArrowCursor);
}

void View::setSection(SectionSettings s)
{
    const bool resample = s.enabled && s.field &&
        (!section.enabled || !section.field || s.axis != section.axis ||
         s.offset != section.offset || s.range != section.range ||
         s.spacing != section.spacing);
    section = s;
    for (auto shape : shapes)
    {
        if (shape->hasElements())
            shape->setElementSection(s.enabled && s.clip, s.clipPlane(), s.wholeElements);
    }
    updateSectionElements();
    if (resample)
    {
        requestSlice();
    }
    pick_timer.start();
    update();
}

void View::setError(const QString& text, int line0)
{
    if (text == m_errorText && line0 == m_errorLine) return;
    m_errorText = text;
    m_errorLine = line0;
    if (text.isEmpty()) m_errorRect = QRect();
    if (m_overlay) m_overlay->update();
    update();
}

void View::drawErrorBanner(QPainter& painter)
{
    if (m_errorText.isEmpty())
    {
        m_errorRect = QRect();
        return;
    }
    const QColor red(255, 56, 56);          // (red is for what is wrong: what only waits for the user is orange)
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    // A frame round the whole viewport: it is seen with the eyes on the model, which is where the work is
    painter.setPen(QPen(QColor(red.red(), red.green(), red.blue(), 190), 3));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(rect().adjusted(1, 1, -2, -2));

    QFont head = painter.font();
    head.setPointSizeF(10.5);
    head.setBold(true);
    QFont body = painter.font();
    body.setPointSizeF(9.0);
    QFont hintFont = body;
    hintFont.setPointSizeF(8.0);
    const QFontMetrics fmHead(head), fmBody(body), fmHint(hintFont);
    const QString title = (m_errorLine >= 0 ? T("Error in line %1").arg(m_errorLine + 1) : T("Error in the script"));
    const QString hint = T("The picture shows what ran before the error, or the last one that worked.  Click to go to the error.");
    const int maxWidth = std::max(260, std::min(width() - 40, 640));
    const QString message = fmBody.elidedText(m_errorText, Qt::ElideRight, maxWidth - 28);
    const int w = std::max({fmHead.horizontalAdvance(title), fmBody.horizontalAdvance(message), fmHint.horizontalAdvance(hint)}) + 28;
    const int h = fmHead.height() + fmBody.height() + fmHint.height() + 22;
    // (centred, but not under the model tree card on the left)
    int x = (width() - w) / 2;
    if (m_scene && m_scene->isVisible()) x = std::max(x, m_scene->geometry().right() + 14);
    x = std::max(8, std::min(x, width() - w - 8));
    m_errorRect = QRect(x, 14, w, h);
    painter.setPen(QPen(red, 1.5));
    painter.setBrush(QColor(92, 24, 24, 235));
    painter.drawRoundedRect(m_errorRect, 8, 8);
    int y = m_errorRect.top() + 7;
    painter.setPen(QColor(255, 96, 96));
    painter.setFont(head);
    painter.drawText(QRect(x + 14, y, w - 28, fmHead.height()), Qt::AlignLeft | Qt::AlignVCenter, QString(QChar(0x26a0)) + "  " + title);
    y += fmHead.height();
    painter.setPen(QColor(0xee, 0xe8, 0xd5));
    painter.setFont(body);
    painter.drawText(QRect(x + 14, y, w - 28, fmBody.height()), Qt::AlignLeft | Qt::AlignVCenter, message);
    y += fmBody.height() + 1;
    painter.setPen(QColor(0xc8, 0xb4, 0xb0));
    painter.setFont(hintFont);
    painter.drawText(QRect(x + 14, y, w - 28, fmHint.height()), Qt::AlignLeft | Qt::AlignVCenter, hint);
    painter.restore();
}

void View::setFieldSources(QList<FieldEntry> fields)
{
    const bool was = fieldShown();
    m_fieldSources.clear();
    m_fieldCentres.clear();
    for (const auto& f : fields)
    {
        m_fieldSources.insert(f.key, f.source);
        m_fieldCentres.insert(f.key, qMakePair(f.centre, f.hasCentre));
    }
    m_fieldRanges.clear();                 // (the script ran again: the fields may be other ones)
    if (!m_fieldKey.isEmpty() && !m_fieldSources.contains(m_fieldKey)) m_fieldKey.clear();
    if (was && !fieldShown())
    {
        m_fld.slice = FieldSlice();        // (the field is gone: the disc goes with it; the section view is not touched)
        m_fld.dirty = true;
        update();
    }
    else if (fieldShown())
    {
        m_fld.slice = FieldSlice();        // (the script ran again: the field may be another one)
        requestFieldSlice();
        update();
    }
    if (section.enabled && section.field)
    {
        m_sec.slice = FieldSlice();
        requestSlice();
        update();
    }
}

QVector3D View::fieldCentre(bool* known) const
{
    const auto c = m_fieldCentres.value(m_fieldKey, qMakePair(QVector3D(), false));
    if (known) *known = c.second;
    return c.second ? c.first : QVector3D();
}

float View::fieldRadius() const
{
    if (m_fieldView.radius > 0) return m_fieldView.radius;
    const QVector3D size = settings.max - settings.min;
    return 0.5f * std::max({size.x(), size.y(), size.z(), 1e-3f});
}

void View::showField(QString key)
{
    if (!m_fieldSources.contains(key)) key.clear();
    if (key == m_fieldKey) return;
    m_fieldKey = key;
    m_fld.quickMs = 0;                     // (how slow this field is to evaluate is not known yet)
    m_fld.slice = FieldSlice();            // (the plane of what was shown before is not the plane of this)
    m_fld.dirty = true;
    if (key.isEmpty())
    {
        update();
        return;
    }
    // (a field: its disc is where the field is about, facing as it did, at the size it was)
    m_fieldView.centre = fieldCentre();
    m_fieldView.radius = 0;
    applyFieldView();
}

void View::setFieldView(FieldViewSettings s)
{
    m_fieldView = s;
    if (fieldShown()) applyFieldView();
}

SectionSettings View::fieldPlane() const
{
    // The disc is a plane like the section's, without the cut: it clips nothing and keeps or cuts no element
    SectionSettings s;
    s.enabled = true;
    s.axis = m_fieldView.axis;
    s.offset = m_fieldView.offset();
    s.flip = false;
    s.clip = false;
    s.field = true;
    s.opacity = m_fieldView.opacity;
    return s;
}

void View::applyFieldView()
{
    requestFieldSlice();
    update();
}

void View::requestSlice()
{
    if (section.enabled && section.field)
    {
        // (a throttle, not a debounce: see requestFieldSlice)
        if (!m_sec.timer.isActive()) m_sec.timer.start();
    }
}

void View::requestFieldSlice()
{
    if (fieldShown())
    {
        // The first request arms the timer, and the ones that come before it fires add nothing: the pass samples the state it finds
        // when it starts.  Starting the timer again at every request waited for the hand to rest, so a drag that moves faster than
        // the timer's interval showed nothing until it stopped -- the plane lagged behind the mouse
        if (!m_fld.timer.isActive()) m_fld.timer.start();
    }
}

void View::sliceRegion(QVector3D& lo, QVector3D& hi, float& fade) const
{
    QVector3D mlo, mhi;
    if (!meshBounds(mlo, mhi))
    {
        mlo = settings.min;
        mhi = settings.max;
    }
    const QVector3D size = mhi - mlo;
    const float extent = std::max({size.x(), size.y(), size.z(), 1e-6f});
    // The plane shows the field within this distance of the models
    fade = 0.15f * extent;
    lo = mlo - QVector3D(fade, fade, fade);
    hi = mhi + QVector3D(fade, fade, fade);
    // ...but not far beyond the render region
    const QVector3D rlo = settings.min - QVector3D(fade, fade, fade);
    const QVector3D rhi = settings.max + QVector3D(fade, fade, fade);
    for (int i=0; i < 3; ++i)
    {
        lo[i] = std::max(lo[i], rlo[i]);
        hi[i] = std::min(hi[i], rhi[i]);
        if (!(hi[i] > lo[i]))
        {
            lo[i] = rlo[i];
            hi[i] = rhi[i];
        }
    }
}

FieldSource View::sourceOf(Shape* s) const
{
    FieldSource src{s->getTree(), s->getVars()};
    if (s->hasColorField())
    {   // (the field it is shown coloured by, e.g. an analysis result)
        src.color = s->colorFieldTree();
        src.lo = s->colorLo();
        src.hi = s->colorHi();
        src.map = s->colorMap();
        src.label = s->hasResult() ? s->channelLabel() : s->colorLabel();
    }
    if (s->hasDeformation())
    {
        for (int a = 0; a < 3; ++a) src.disp[a] = s->deformTree(a);
    }
    return src;
}

void View::startSliceAt(Plane& plane, bool fine)
{
    const bool fieldMode = &plane == &m_fld;
    if (fieldMode ? !fieldShown() : !(section.enabled && section.field)) return;      // (switched off while this waited)
    if (plane.watcher.isRunning())
    {
        // A quick pass that is on its way is let finish -- its picture is shown, and the next pass follows at its end.  Abandoning it
        // at every tick of a drag meant that no pass lived long enough to be shown.  A fine pass, or a quick one that takes long, is
        // abandoned (what it samples is out of date); we start again once it returns
        if (plane.runningFine || plane.clock.elapsed() > 400) plane.generation++;
        plane.timer.start();
        return;
    }
    QVector<FieldSource> sources;
    bool rangeKnown = true;
    if (fieldMode)
    {
        // (the field model that is selected is the only thing the plane shows)
        FieldSource src = m_fieldSources.value(m_fieldKey);
        src.map = "section";            // (a field is shown in the section's colours and dark lines: one look for what the planes show)
        if (m_fieldRanges.contains(m_fieldKey))
        {
            src.lo = m_fieldRanges[m_fieldKey].first;
            src.hi = m_fieldRanges[m_fieldKey].second;
        }
        else rangeKnown = false;
        if (!m_fieldKey.startsWith("line:")) src.label = m_fieldKey;
        {
            // (the plane is a DISC: the "model" it shows the field in is the disc, its distance field -- negative inside it)
            const int ax = m_fieldView.axis, ua = FieldSlice::uAxis(ax), va = FieldSlice::vAxis(ax);
            const libfive::Tree axes[3] = {libfive::Tree::X(), libfive::Tree::Y(), libfive::Tree::Z()};
            using libfive::Tree;
            using libfive::Opcode::OP_ADD; using libfive::Opcode::OP_MUL; using libfive::Opcode::OP_SQRT; using libfive::Opcode::OP_SUB;
            const Tree du = Tree::binary(OP_SUB, axes[ua], Tree(double(m_fieldView.centre[ua])));
            const Tree dv = Tree::binary(OP_SUB, axes[va], Tree(double(m_fieldView.centre[va])));
            const Tree r2 = Tree::binary(OP_ADD, Tree::binary(OP_MUL, du, du), Tree::binary(OP_MUL, dv, dv));
            src.tree = Tree::binary(OP_SUB, Tree::unary(OP_SQRT, r2), Tree(double(fieldRadius())));
        }
        sources.push_back(src);
    }
    else
    {
        for (auto& s : shapes)
        {
            sources.push_back(sourceOf(s));
        }
    }
    const int gen = ++plane.generation;
    const SectionSettings sec = fieldMode ? fieldPlane() : section;
    QVector3D lo, hi;
    float fade;
    if (fieldMode)
    {
        // The field viewer's disc: the square round its middle (the disc is cut out of it by its own distance field)
        const float R = fieldRadius();
        lo = m_fieldView.centre - QVector3D(R, R, R);
        hi = m_fieldView.centre + QVector3D(R, R, R);
        fade = 0.15f * R;
    }
    else sliceRegion(lo, hi, fade);
    const QVector3D rangeLo = settings.min, rangeHi = settings.max;      // (a field's colours are found over the whole render region)
    // A quick preview first (for dragging), then the full resolution -- less of it when the field is slow to evaluate (the fine pass
    // has sixteen times the samples of the quick one: it should not take much more than a third of a second)
    int fineSamples = 720;
    const double estimate = plane.quickMs * 16.0;
    if (estimate > 350.0) fineSamples = std::max(256, int(720.0 * std::sqrt(350.0 / estimate)));
    const int samples = fine ? fineSamples : 180;
    plane.clock.start();
    plane.runningFine = fine;
    std::atomic<int>* g = &plane.generation;
    plane.watcher.setFuture(QtConcurrent::run([=]() {
        const auto t0 = std::chrono::steady_clock::now();
        QVector<FieldSource> used = sources;
        if (!rangeKnown && !used.isEmpty()) autoColorRange(used[0], fieldMode ? rangeLo : lo, fieldMode ? rangeHi : hi);
        FieldSlice s = sampleField(used, sec, lo, hi, samples, fade, g, gen);
        s.fine = fine;
        if (std::getenv("FIELDES_TIMING") && s.valid())
        {
            std::cerr << "[slice] " << (fieldMode ? "field " : "") << (fine ? "fine" : "quick") << " plane at " << sec.offset << ": "
                      << std::fixed << 1000.0 * std::chrono::duration<double>(
                             std::chrono::steady_clock::now() - t0).count()
                      << " ms (" << sources.size() << " shapes evaluated)" << std::endl;
        }
        return s;
    }));
}

void View::onSliceFinished(Plane& plane)
{
    const bool fieldMode = &plane == &m_fld;
    const FieldSlice s = plane.watcher.result();
    if (s.generation == plane.generation.load() && s.valid())
    {
        if (!s.fine && plane.clock.isValid()) plane.quickMs = double(plane.clock.elapsed());
        if (fieldMode && fieldShown() && s.hasColor && !m_fieldRanges.contains(m_fieldKey))
            m_fieldRanges.insert(m_fieldKey, qMakePair(s.colorLo, s.colorHi));       // (found once for the field, kept)
        plane.slice = s;
        plane.dirty = true;
        if (fieldMode) emit(fieldSliceReady(plane.slice));
        else emit(sliceReady(plane.slice));
        update();
        if (!s.fine && !plane.timer.isActive())
        {
            startSliceAt(plane, true);
        }
    }
    else if ((fieldMode ? fieldShown() : (section.enabled && section.field)) && s.generation != plane.generation.load())
    {
        plane.timer.start();   // settings moved on while this one ran
    }
}

void View::setClipUniform(bool on)
{
    Shader::basic->bind();
    const QVector4D p = section.clipPlane();
    glUniform4f(Shader::basic->uniformLocation("clip_plane"), p.x(), p.y(), p.z(), p.w());
    // Inside faces exposed by the cut are drawn flat (see basic.frag);
    // FIELDES_CUT_MODE flips the facing convention for debugging
    static const int mode = qEnvironmentVariableIsSet("FIELDES_CUT_MODE")
        ? qEnvironmentVariableIntValue("FIELDES_CUT_MODE") : -1;
    glUniform1i(Shader::basic->uniformLocation("cut_mode"), on ? mode : 0);
    glUniform4f(Shader::basic->uniformLocation("cut_color"), 0.52f, 0.38f, 0.30f, 1.0f);
    Shader::basic->release();
    if (on)
    {
        glEnable(GL_CLIP_DISTANCE0);
    }
}

void View::drawSlicePlane(const QMatrix4x4& m, Plane& plane, const SectionSettings& at)
{
    if (!plane.slice.valid())
    {
        return;
    }
    if (plane.dirty)
    {
        // Raw distances (non-finite = nothing there, i.e. far outside)
        QVector<float> values = plane.slice.values;
        for (auto& v : values)
        {
            if (!std::isfinite(v)) v = 1e30f;
        }
        plane.tex.reset(new QOpenGLTexture(QOpenGLTexture::Target2D));
        plane.tex->setFormat(QOpenGLTexture::R32F);
        plane.tex->setSize(plane.slice.w, plane.slice.h);
        plane.tex->allocateStorage(QOpenGLTexture::Red, QOpenGLTexture::Float32);
        plane.tex->setData(QOpenGLTexture::Red, QOpenGLTexture::Float32, values.constData());
        plane.tex->setMinificationFilter(QOpenGLTexture::Linear);
        plane.tex->setMagnificationFilter(QOpenGLTexture::Linear);
        plane.tex->setWrapMode(QOpenGLTexture::ClampToEdge);
        plane.colorTex.reset();
        if (plane.slice.hasColor && plane.slice.color.size() == plane.slice.values.size())
        {
            plane.colorTex.reset(new QOpenGLTexture(QOpenGLTexture::Target2D));
            plane.colorTex->setFormat(QOpenGLTexture::R32F);
            plane.colorTex->setSize(plane.slice.w, plane.slice.h);
            plane.colorTex->allocateStorage(QOpenGLTexture::Red, QOpenGLTexture::Float32);
            plane.colorTex->setData(QOpenGLTexture::Red, QOpenGLTexture::Float32, plane.slice.color.constData());
            // nearest: NaN (outside) must not bleed into its neighbours -- except for a field of the field viewer, whose lines are drawn from
            // its values and need them smooth
            const auto filter = plane.slice.colorMap == "section" ? QOpenGLTexture::Linear : QOpenGLTexture::Nearest;
            plane.colorTex->setMinificationFilter(filter);
            plane.colorTex->setMagnificationFilter(filter);
            plane.colorTex->setWrapMode(QOpenGLTexture::ClampToEdge);
        }
        plane.gridVerts = 0;       // (the deformed grid is rebuilt)
        plane.dirty = false;
    }
    if (!plane.tex)
    {
        return;
    }

    // Elements shown whole: they are the section (the plane would cut
    // through them)
    for (auto s : shapes)
    {
        if (s->hasResult() && s->showElements() && at.wholeElements && plane.colorTex) return;
    }

    // The plane rectangle at the CURRENT offset (the texture may lag a
    // few milliseconds behind while dragging the slider)
    const int a = at.axis;
    const int ua = FieldSlice::uAxis(a), va = FieldSlice::vAxis(a);
    float deform = 0;
    for (auto s : shapes)
    {
        if (s->hasDeformation()) { deform = s->deformScale(); break; }
    }
    const bool deformed = deform != 0 && !plane.slice.disp.isEmpty() &&
                          plane.slice.disp.size() == plane.slice.gw * plane.slice.gh;
    auto corner = [&](float u, float v) {
        QVector3D p;
        p[a] = at.offset;
        p[ua] = u;
        p[va] = v;
        return p;
    };
    const QVector3D c[4] = {
        corner(plane.slice.min[ua], plane.slice.min[va]), corner(plane.slice.max[ua], plane.slice.min[va]),
        corner(plane.slice.max[ua], plane.slice.max[va]), corner(plane.slice.min[ua], plane.slice.max[va])};
    const float uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    GLfloat data[4 * 5];
    for (int i=0; i < 4; ++i)
    {
        data[i * 5 + 0] = c[i].x();
        data[i * 5 + 1] = c[i].y();
        data[i * 5 + 2] = c[i].z();
        data[i * 5 + 3] = uv[i][0];
        data[i * 5 + 4] = uv[i][1];
    }
    if (!plane.vao.isCreated())
    {
        plane.vao.create();
        plane.vbo.create();
    }
    plane.vao.bind();
    plane.vbo.bind();
    if (deformed)
    {
        if (plane.gridGeneration != plane.slice.generation || plane.gridScale != deform ||
            plane.gridOffset != at.offset || plane.gridVerts == 0)
        {
            // Each grid vertex moved by its (scaled) displacement
            std::vector<GLfloat> grid;
            grid.reserve(size_t(plane.slice.gw - 1) * (plane.slice.gh - 1) * 6 * 5);
            auto vert = [&](int i, int j) {
                const float s = float(i) / (plane.slice.gw - 1), t = float(j) / (plane.slice.gh - 1);
                QVector3D p;
                p[a] = at.offset;
                p[ua] = plane.slice.min[ua] + s * (plane.slice.max[ua] - plane.slice.min[ua]);
                p[va] = plane.slice.min[va] + t * (plane.slice.max[va] - plane.slice.min[va]);
                p += deform * plane.slice.disp[j * plane.slice.gw + i];
                grid.insert(grid.end(), {p.x(), p.y(), p.z(), s, t});
            };
            for (int j = 0; j + 1 < plane.slice.gh; ++j)
                for (int i = 0; i + 1 < plane.slice.gw; ++i)
                {
                    vert(i, j); vert(i + 1, j); vert(i + 1, j + 1);
                    vert(i, j); vert(i + 1, j + 1); vert(i, j + 1);
                }
            plane.vbo.allocate(grid.data(), int(grid.size() * sizeof(GLfloat)));
            plane.gridVerts = int(grid.size() / 5);
            plane.gridGeneration = plane.slice.generation;
            plane.gridScale = deform;
            plane.gridOffset = at.offset;
        }
    }
    else
    {
        plane.gridVerts = 0;
        plane.vbo.allocate(data, sizeof(data));
    }
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), nullptr);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat),
                          (GLvoid*)(3 * sizeof(GLfloat)));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);

    glEnable(GL_BLEND);
    // Colour blends; the framebuffer's alpha stays opaque (a translucent
    // alpha would make the widget composite oddly over the window)
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
    // Pull the plane slightly towards the camera so it never depth-fights
    // geometry clipped exactly at the plane
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-1.0f, -4.0f);
    Shader::slice->bind();
    glUniformMatrix4fv(Shader::slice->uniformLocation("M"), 1, GL_FALSE, m.data());
    glUniform1f(Shader::slice->uniformLocation("opacity"), at.opacity);
    glUniform1f(Shader::slice->uniformLocation("range_in"), std::max(plane.slice.rangeIn, 1e-9f));
    glUniform1f(Shader::slice->uniformLocation("range_out"), std::max(plane.slice.rangeOut, 1e-9f));
    glUniform1f(Shader::slice->uniformLocation("spacing"), std::max(plane.slice.spacing, 1e-9f));
    glUniform1f(Shader::slice->uniformLocation("fade"), std::max(plane.slice.fade, 1e-9f));
    glActiveTexture(GL_TEXTURE0);
    plane.tex->bind(0);
    glUniform1i(Shader::slice->uniformLocation("field"), 0);
    int colorMode = 0;
    if (plane.colorTex)
    {
        colorMode = plane.slice.colorMap == "section" ? 4 : plane.slice.colorMap == "viridis" ? 2
                    : (plane.slice.colorMap == "grey" || plane.slice.colorMap == "gray") ? 3 : 1;
        plane.colorTex->bind(1);
        glUniform1i(Shader::slice->uniformLocation("color_field"), 1);
        glUniform1f(Shader::slice->uniformLocation("color_lo"), plane.slice.colorLo);
        glUniform1f(Shader::slice->uniformLocation("color_hi"), plane.slice.colorHi);
    }
    glUniform1i(Shader::slice->uniformLocation("color_mode"), colorMode);
    if (deformed) glDrawArrays(GL_TRIANGLES, 0, plane.gridVerts);
    else glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    if (plane.colorTex) plane.colorTex->release(1);
    glActiveTexture(GL_TEXTURE0);
    plane.tex->release(0);
    Shader::slice->release();
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDisable(GL_BLEND);
    plane.vbo.release();
    plane.vao.release();
}

QPointF View::toScreen(const QVector3D& p) const
{
    const QVector3D n = camera.M().map(p);
    return QPointF((n.x() + 1) * 0.5 * width(), (1 - n.y()) * 0.5 * height());
}

bool View::sectionHandle(QPointF& p, QPointF& q, float& length) const
{
    QVector3D lo, hi;
    if (!meshBounds(lo, hi))
    {
        lo = settings.min;
        hi = settings.max;
    }
    const QVector3D size = hi - lo;
    length = 0.22f * std::max({size.x(), size.y(), size.z(), 1e-6f});
    QVector3D c = (lo + hi) / 2;
    c[section.axis] = section.offset;
    return normalArrow(section.axis, c, length, p, q);
}

bool View::normalArrow(int axis, const QVector3D& c, float length, QPointF& p, QPointF& q) const
{
    QVector3D n;
    n[axis] = 1;
    p = toScreen(c);
    q = toScreen(c + n * length);
    return std::isfinite(p.x()) && std::isfinite(q.x());
}

bool View::onArrow(QPoint pos, QPointF p, QPointF q)
{
    // The knob, or anywhere along the double arrow
    const QPointF a = p - (q - p), b = q;
    const QPointF ab = b - a, ap = QPointF(pos) - a;
    const double l2 = QPointF::dotProduct(ab, ab);
    const double t = l2 > 0 ? std::max(0.0, std::min(1.0, QPointF::dotProduct(ap, ab) / l2)) : 0.0;
    const QPointF d = QPointF(pos) - (a + t * ab);
    return std::hypot(d.x(), d.y()) < 9;
}

bool View::sectionHandleHit(QPoint pos) const
{
    if (!section.enabled) return false;
    QPointF p, q;
    float len;
    if (!sectionHandle(p, q, len)) return false;
    return onArrow(pos, p, q);
}

bool View::fieldGizmo(QPointF& middle, QPointF& tipU, QPointF& tipV, QPointF& tipN, float& length) const
{
    const int ax = m_fieldView.axis, ua = FieldSlice::uAxis(ax), va = FieldSlice::vAxis(ax);
    length = 0.6f * fieldRadius();
    const QVector3D c = m_fieldView.centre;
    QVector3D eu, ev, en;
    eu[ua] = 1;
    ev[va] = 1;
    en[ax] = 1;
    middle = toScreen(c);
    tipU = toScreen(c + eu * length);
    tipV = toScreen(c + ev * length);
    tipN = toScreen(c + en * length);
    return std::isfinite(middle.x()) && std::isfinite(tipU.x()) && std::isfinite(tipV.x()) && std::isfinite(tipN.x());
}

bool View::fieldGizmoPoint(int part, QPoint& out) const
{
    // (for the automation: a point that grips part 1, 2 (the arrows in the plane), 3 (the dot) or 4 (the arrow along the normal))
    if (!fieldShown()) return false;
    QPointF m, u, v, n;
    float len;
    if (!fieldGizmo(m, u, v, n, len)) return false;
    const QPointF p = part == 1 ? m + 0.6 * (u - m) : part == 2 ? m + 0.6 * (v - m) : part == 4 ? m + 0.7 * (n - m) : m;
    out = QPoint(int(std::lround(p.x())), int(std::lround(p.y())));
    return fieldGizmoHit(out) == part;
}

int View::fieldGizmoHit(QPoint pos) const
{
    if (!fieldShown()) return 0;
    QPointF m, u, v, n;
    float len;
    if (!fieldGizmo(m, u, v, n, len)) return 0;
    const QPointF p(pos);
    if (std::hypot(p.x() - m.x(), p.y() - m.y()) < 9) return 3;                // (the dot in the middle)
    auto closeTo = [&](QPointF a, QPointF b) {
        const QPointF ab = b - a, ap = p - a;
        const double l2 = QPointF::dotProduct(ab, ab);
        const double t = l2 > 0 ? std::max(0.0, std::min(1.0, QPointF::dotProduct(ap, ab) / l2)) : 0.0;
        const QPointF d = p - (a + t * ab);
        return std::hypot(d.x(), d.y()) < 9;
    };
    if (closeTo(m, u)) return 1;
    if (closeTo(m, v)) return 2;
    if (onArrow(pos, m, n)) return 4;                                          // (the double arrow along the normal)
    return 0;
}

void View::drawFieldGizmo(QPainter& painter)
{
    QPointF m, u, v, n;
    float len;
    if (!fieldGizmo(m, u, v, n, len)) return;
    const int ax = m_fieldView.axis, ua = FieldSlice::uAxis(ax), va = FieldSlice::vAxis(ax);
    const int part = field_drag ? field_drag : field_hover;                    // (the one that is held, else the one under the cursor)
    static const QColor axisColour[3] = {QColor(225, 80, 70), QColor(110, 185, 70), QColor(60, 130, 230)};
    paintNormalArrow(painter, m, n, part == 4);
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    auto arrow = [&](QPointF from, QPointF to, QColor col, bool hot) {
        if (hot) col = col.lighter(130);
        painter.setPen(QPen(QColor(0, 0, 0, 90), 5, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(from, to);
        painter.setPen(QPen(col, 2.5, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(from, to);
        const QPointF dir = to - from;
        const double l = std::hypot(dir.x(), dir.y());
        if (l < 1) return;
        const QPointF d = dir / l, n(-d.y(), d.x());
        QPolygonF head;
        head << to + d * 4 << to - d * 9 + n * 6 << to - d * 9 - n * 6;
        painter.setPen(QPen(QColor(0, 0, 0, 90), 1));
        painter.setBrush(col);
        painter.drawPolygon(head);
    };
    arrow(m, u, axisColour[ua], part == 1);
    arrow(m, v, axisColour[va], part == 2);
    painter.setPen(QPen(part == 3 ? Qt::white : QColor(0xee, 0xe8, 0xd5), 1.5));
    painter.setBrush(part == 3 || part == 4 ? QColor(80, 170, 240) : QColor(38, 139, 210));
    painter.drawEllipse(m, 6.5, 6.5);
    painter.restore();
}

void View::dragFieldGizmo(QPoint pos)
{
    const int ax = m_fieldView.axis, ua = FieldSlice::uAxis(ax), va = FieldSlice::vAxis(ax);
    QVector3D c = field_press_centre;
    // (nothing keeps the disc inside the render region or anywhere else: a field is about all of space, and the way to see it is to
    // take the disc to where it is interesting)
    auto clamp = [&](int, float v) { return v; };
    if (field_drag == 1 || field_drag == 2 || field_drag == 4)
    {
        // An arrow: the mouse movement along its screen direction, in mm (the one along the normal moves the disc through the field)
        const double l2 = QPointF::dotProduct(field_dir, field_dir);
        if (l2 < 1) return;
        const double t = QPointF::dotProduct(QPointF(pos - field_press), field_dir) / l2;
        const int a = field_drag == 1 ? ua : field_drag == 2 ? va : ax;
        c[a] = clamp(a, float(field_press_centre[a] + t * field_len));
    }
    else
    {
        // The dot: the disc follows the cursor in its plane (the point where the cursor's line of sight meets the plane)
        const QVector3D a = toModelPos(pos, 0), b = toModelPos(pos, 0.25);
        if (b[ax] == a[ax] || !std::isfinite(a[ax]) || !std::isfinite(b[ax])) return;
        const float t = (m_fieldView.offset() - a[ax]) / (b[ax] - a[ax]);
        const QVector3D hit = a + t * (b - a);
        c[ua] = clamp(ua, hit[ua] + field_press_grab[ua]);
        c[va] = clamp(va, hit[va] + field_press_grab[va]);
    }
    emit(fieldCentreDragged(c));
}

void View::drawSectionHandle(QPainter& painter)
{
    QPointF p, q;
    float len;
    if (!sectionHandle(p, q, len)) return;
    paintNormalArrow(painter, p, q, section_hover || section_drag);
}

void View::paintNormalArrow(QPainter& painter, QPointF p, QPointF q, bool hot)
{
    const QPointF back = p - (q - p);
    const QColor col = hot ? QColor(80, 170, 240) : QColor(38, 139, 210);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    auto arrow = [&](QPointF from, QPointF to) {
        painter.setPen(QPen(QColor(0, 0, 0, 90), 5, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(from, to);
        painter.setPen(QPen(col, 2.5, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(from, to);
        const QPointF dir = to - from;
        const double l = std::hypot(dir.x(), dir.y());
        if (l < 1) return;
        const QPointF u = dir / l, v(-u.y(), u.x());
        QPolygonF head;
        head << to + u * 4 << to - u * 9 + v * 6 << to - u * 9 - v * 6;
        painter.setPen(QPen(QColor(0, 0, 0, 90), 1));
        painter.setBrush(col);
        painter.drawPolygon(head);
    };
    arrow(p, q);
    arrow(p, back);
    painter.setPen(QPen(hot ? Qt::white : QColor(0xee, 0xe8, 0xd5), 1.5));
    painter.setBrush(col);
    painter.drawEllipse(p, 6.5, 6.5);
    painter.restore();
}

////////////////////////////////////////////////////////////////////////////////
// Handles of placed parts

namespace {

const QColor kHandleColor[3] = {QColor(0xdc, 0x32, 0x2f), QColor(0x74, 0xb8, 0x1f), QColor(0x26, 0x8b, 0xd2)};

// The axes the three rotation rings turn about (turning right-handed about
// each is a positive change of its angle).  The part turns by Rz * Ry * Rx, so
// the z ring is the world's z, the y ring's is z-turned, and the x ring's has
// both turns applied: a gimbal.  (The library's y rotation goes the other
// way about +y, hence the minus.)
void handleAxes(const QVector3D& turn, QVector3D ring[3])
{
    const QMatrix4x4 rz = Shape::handleRotation(QVector3D(0, 0, turn.z()));
    const QMatrix4x4 rzy = Shape::handleRotation(QVector3D(0, turn.y(), turn.z()));
    ring[2] = QVector3D(0, 0, 1);
    ring[1] = rz.mapVector(QVector3D(0, -1, 0));
    ring[0] = rzy.mapVector(QVector3D(1, 0, 0));
}

// Two unit vectors spanning the plane normal to n
void planeBasis(const QVector3D& n, QVector3D& u, QVector3D& v)
{
    const QVector3D ref = std::fabs(n.z()) < 0.9f ? QVector3D(0, 0, 1) : QVector3D(1, 0, 0);
    u = QVector3D::crossProduct(n, ref).normalized();
    v = QVector3D::crossProduct(n, u).normalized();
}

double distanceToSegment(const QPointF& p, const QPointF& a, const QPointF& b)
{
    const QPointF ab = b - a, ap = p - a;
    const double l2 = QPointF::dotProduct(ab, ab);
    const double t = l2 > 0 ? std::max(0.0, std::min(1.0, QPointF::dotProduct(ap, ab) / l2)) : 0.0;
    const QPointF d = p - (a + t * ab);
    return std::hypot(d.x(), d.y());
}

const int kRingSegments = 64;

// Where along its axis a scale knob sits, as a share of the arrows' length
const float kKnobAt = 0.82f;

QVector3D unitAxis(int a)
{
    QVector3D e;
    e[a] = 1;
    return e;
}

}   // anonymous namespace

float View::handleLength(const QVector3D& pivot) const
{
    // A model length that spans about 80 pixels at this pivot
    const QVector3D size = settings.max - settings.min;
    const float eps = std::max({size.x(), size.y(), size.z(), 1e-6f}) * 0.01f;
    const QPointF p = toScreen(pivot);
    double best = 0;
    for (int a = 0; a < 3; ++a)
    {
        QVector3D d;
        d[a] = eps;
        const QPointF q = toScreen(pivot + d);
        best = std::max(best, std::hypot(q.x() - p.x(), q.y() - p.y()));
    }
    return best > 1e-6 ? float(eps * 80.0 / best) : 0.0f;
}

int View::selectedShapeCount() const
{
    int n = 0;
    for (const auto* s : shapes) if (s->isSelected()) ++n;
    return n;
}

bool View::groupHandles(QList<Shape*>& members, QVector3D& center) const
{
    // The multi-select state: two or more shapes selected.  The ones that can be moved (they have move numbers) share
    // one gizmo, whatever gizmo mode each has when it is selected alone -- unless all of them say never
    members.clear();
    int selected = 0;
    bool anyShown = false, anyLocked = false;
    for (Shape* s : shapes)
    {
        if (!s->isSelected()) continue;
        ++selected;
        anyLocked |= s->isLocked();
        if (!s->hasMoveVars()) continue;
        members << s;
        anyShown |= s->handles().mode != Shape::Handles::NEVER;
    }
    // (a locked shape in the selection: the selection is not moved or edited -- there is no gizmo for it)
    if (selected < 2 || anyLocked || members.isEmpty() || !anyShown) return false;
    QVector3D lo, hi;
    bool first = true;
    for (const Shape* s : members)
    {
        const QVector3D P = s->handlePivot();
        lo = first ? P : QVector3D(std::min(lo.x(), P.x()), std::min(lo.y(), P.y()), std::min(lo.z(), P.z()));
        hi = first ? P : QVector3D(std::max(hi.x(), P.x()), std::max(hi.y(), P.y()), std::max(hi.z(), P.z()));
        first = false;
    }
    center = 0.5f * (lo + hi);
    return true;
}

namespace {

// Whether any of these shapes has a number that moves it along an axis
bool anyMoveVar(const QList<Shape*>& shapes, int a)
{
    for (const Shape* s : shapes)
    {
        const auto& h = s->handles();
        if (h.move[a] && s->getVars().count(h.move[a])) return true;
    }
    return false;
}

}   // anonymous namespace

View::HandleGrip View::handleAt(QPoint pos) const
{
    HandleGrip best;
    double bestDist = 9.0;
    const QPointF at(pos);
    // Several shapes selected, all in the gizmo mode: one gizmo for them all (arrows and the dot in the middle)
    QList<Shape*> members;
    QVector3D groupCenter;
    const bool group = groupHandles(members, groupCenter);
    if (group)
    {
        const float L = handleLength(groupCenter);
        if (L > 0)
        {
            const QPointF p0 = toScreen(groupCenter);
            if (std::hypot(at.x() - p0.x(), at.y() - p0.y()) <= 8.0 &&
                (anyMoveVar(members, 0) || anyMoveVar(members, 1) || anyMoveVar(members, 2)))
            {
                return HandleGrip{members[0], 3, 0, true};
            }
            for (int a = 0; a < 3; ++a)
            {
                if (!anyMoveVar(members, a)) continue;
                QVector3D e;
                e[a] = L;
                const double d = distanceToSegment(at, p0, toScreen(groupCenter + e));
                if (d < bestDist) { bestDist = d; best = HandleGrip{members[0], 0, a, true}; }
            }
        }
    }
    for (Shape* s : shapes)
    {
        if (!s->hasHandles() || (group && members.contains(s))) continue;
        const auto& h = s->handles();
        const auto& vars = s->getVars();
        const QVector3D P = s->handlePivot();
        const float L = handleLength(P);
        if (!(L > 0)) continue;
        const QPointF p0 = toScreen(P);
        // (the dot in the middle drags the shape freely: it wins over everything near it)
        if (std::hypot(at.x() - p0.x(), at.y() - p0.y()) <= 8.0 &&
            (anyMoveVar({s}, 0) || anyMoveVar({s}, 1) || anyMoveVar({s}, 2)))
        {
            best = {s, 3, 0};
            bestDist = -1.0;
            continue;
        }
        for (int a = 0; a < 3; ++a)
        {
            if (!h.move[a] || !vars.count(h.move[a])) continue;
            QVector3D e;
            e[a] = L;
            const double d = distanceToSegment(at, p0, toScreen(P + e));
            if (d < bestDist) { bestDist = d; best = {s, 0, a}; }
        }
        // (the scale knobs are on the part's own axes, turned with it: they win over the arrow behind them)
        const QMatrix4x4 R = Shape::handleRotation(s->handleRotate());
        for (int a = 0; a < 3; ++a)
        {
            if (!h.scale[a] || !vars.count(h.scale[a])) continue;
            const QPointF q = toScreen(P + R.mapVector(unitAxis(a)) * (kKnobAt * L));
            const double d = std::hypot(at.x() - q.x(), at.y() - q.y()) - 3.5;
            if (d < bestDist) { bestDist = d; best = {s, 2, a}; }
        }
        QVector3D ring[3];
        handleAxes(s->handleRotate(), ring);
        for (int a = 0; a < 3; ++a)
        {
            if (!h.rotate[a] || !vars.count(h.rotate[a])) continue;
            QVector3D u, v;
            planeBasis(ring[a], u, v);
            QPointF prev;
            for (int k = 0; k <= kRingSegments; ++k)
            {
                const float t = float(2 * M_PI * k / kRingSegments);
                const QPointF q = toScreen(P + (0.62f * L) * (std::cos(t) * u + std::sin(t) * v));
                if (k > 0)
                {
                    const double d = distanceToSegment(at, prev, q) + 1.5;   // (arrows win ties)
                    if (d < bestDist) { bestDist = d; best = {s, 1, a}; }
                }
                prev = q;
            }
        }
    }
    return best;
}

bool View::handleGripPoint(int kind, int axis, QPoint& pos) const
{
    QList<Shape*> members;
    QVector3D groupCenter;
    if (groupHandles(members, groupCenter))
    {
        // (the shared gizmo of several selected shapes has the arrows and the dot only)
        const float L = handleLength(groupCenter);
        if (!(L > 0) || (kind != 0 && kind != 3)) return false;
        QVector3D e;
        if (kind == 0) e[axis] = 0.6f * L;
        pos = toScreen(groupCenter + e).toPoint();
        return true;
    }
    for (Shape* s : shapes)
    {
        if (!s->hasHandles()) continue;
        const QVector3D P = s->handlePivot();
        const float L = handleLength(P);
        if (!(L > 0)) continue;
        if (kind == 3)
        {
            pos = toScreen(P).toPoint();
        }
        else if (kind == 0)
        {
            QVector3D e;
            e[axis] = 0.6f * L;
            pos = toScreen(P + e).toPoint();
        }
        else if (kind == 2)
        {
            pos = toScreen(P + Shape::handleRotation(s->handleRotate()).mapVector(unitAxis(axis)) *
                                   (kKnobAt * L)).toPoint();
        }
        else
        {
            QVector3D ring[3], u, v;
            handleAxes(s->handleRotate(), ring);
            planeBasis(ring[axis], u, v);
            const float t = float(M_PI / 4);
            pos = toScreen(P + (0.62f * L) * (std::cos(t) * u + std::sin(t) * v)).toPoint();
        }
        return true;
    }
    return false;
}

void View::setProvisionalGizmo(bool on, QVector3D pivot, QList<int> lines0)
{
    m_prov = on;
    m_provPivot = pivot;
    m_provLines = lines0;
    m_overlay->update();
}

bool View::provisionalVisible() const
{
    if (!m_prov || m_provLines.isEmpty()) return false;
    for (const Shape* s : shapes)
    {
        // (the real gizmo is there: it is the one that is drawn, and dragged)
        if (m_provLines.contains(s->sourceLine()) && s->hasHandles()) return false;
    }
    return true;
}

bool View::provisionalGripAt(QPoint pos, int* kind, int* axis) const
{
    if (!provisionalVisible()) return false;
    const QVector3D P = m_provPivot;
    const float L = handleLength(P);
    if (!(L > 0)) return false;
    const QPointF at(pos), p0 = toScreen(P);
    if (std::hypot(at.x() - p0.x(), at.y() - p0.y()) <= 8.0)
    {
        *kind = 3;
        *axis = 0;
        return true;
    }
    double best = 9.0;
    bool found = false;
    for (int a = 0; a < 3; ++a)
    {
        QVector3D e;
        e[a] = L;
        const double d = distanceToSegment(at, p0, toScreen(P + e));
        if (d < best) { best = d; *kind = 0; *axis = a; found = true; }
    }
    for (int a = 0; a < 3; ++a)
    {
        const QPointF q = toScreen(P + unitAxis(a) * (kKnobAt * L));
        const double d = std::hypot(at.x() - q.x(), at.y() - q.y()) - 3.5;
        if (d < best) { best = d; *kind = 2; *axis = a; found = true; }
    }
    for (int a = 0; a < 3; ++a)
    {
        QVector3D u, v;
        planeBasis(unitAxis(a), u, v);
        QPointF prev;
        for (int k = 0; k <= kRingSegments; ++k)
        {
            const float t = float(2 * M_PI * k / kRingSegments);
            const QPointF q = toScreen(P + (0.62f * L) * (std::cos(t) * u + std::sin(t) * v));
            if (k > 0)
            {
                const double d = distanceToSegment(at, prev, q) + 1.5;
                if (d < best) { best = d; *kind = 1; *axis = a; found = true; }
            }
            prev = q;
        }
    }
    return found;
}

void View::drawProvisional(QPainter& painter)
{
    if (!provisionalVisible()) return;
    const QVector3D P = m_provPivot;
    const float L = handleLength(P);
    if (!(L > 0)) return;
    const QPointF p0 = toScreen(P);
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setOpacity(0.6);             // (a little transparent: not draggable yet, and the real one is solid)
    for (int a = 0; a < 3; ++a)
    {
        QVector3D u, v;
        planeBasis(unitAxis(a), u, v);
        QPolygonF poly;
        for (int k = 0; k <= kRingSegments; ++k)
        {
            const float t = float(2 * M_PI * k / kRingSegments);
            poly << toScreen(P + (0.62f * L) * (std::cos(t) * u + std::sin(t) * v));
        }
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(0, 0, 0, 90), 4.5));
        painter.drawPolyline(poly);
        painter.setPen(QPen(kHandleColor[a], 2.2));
        painter.drawPolyline(poly);
    }
    for (int a = 0; a < 3; ++a)
    {
        QVector3D e;
        e[a] = L;
        const QPointF q = toScreen(P + e);
        painter.setPen(QPen(QColor(0, 0, 0, 90), 5, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(p0, q);
        painter.setPen(QPen(kHandleColor[a], 2.5, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(p0, q);
        const QPointF dir = q - p0;
        const double l = std::hypot(dir.x(), dir.y());
        if (l < 1) continue;
        const QPointF u = dir / l, w(-u.y(), u.x());
        QPolygonF head;
        head << q + u * 6 << q - u * 8 + w * 6 << q - u * 8 - w * 6;
        painter.setPen(QPen(QColor(0, 0, 0, 90), 1));
        painter.setBrush(kHandleColor[a]);
        painter.drawPolygon(head);
    }
    for (int a = 0; a < 3; ++a)
    {
        const QPointF q = toScreen(P + unitAxis(a) * (kKnobAt * L));
        painter.setPen(QPen(QColor(0, 0, 0, 140), 1.5));
        painter.setBrush(kHandleColor[a]);
        painter.drawRect(QRectF(q.x() - 5.5, q.y() - 5.5, 11, 11));
    }
    painter.setPen(QPen(QColor(0xee, 0xe8, 0xd5), 1.5));
    painter.setBrush(QColor(38, 139, 210));
    painter.drawEllipse(p0, 5.5, 5.5);
    painter.restore();
}

void View::cancelWaitingGrip()
{
    if (!m_wait.on) return;
    m_wait.on = false;
    m_waitTimer.stop();
    unsetCursor();
}

void View::startWaitingGrip()
{
    // A press on the provisional gizmo that is waiting for the real one: the drag begins when it is there, if the button is
    // still down (the numbers of the model are in the script by then)
    if (!m_wait.on) { m_waitTimer.stop(); return; }
    if (!(QApplication::mouseButtons() & Qt::LeftButton) || ++m_wait.ticks > 600)
    {
        cancelWaitingGrip();
        return;
    }
    for (Shape* s : shapes)
    {
        if (!m_wait.lines.contains(s->sourceLine()) || !s->hasHandles()) continue;
        const HandleGrip g{s, m_wait.kind, m_wait.axis};
        const QPoint pos = mapFromGlobal(QCursor::pos());
        m_wait.on = false;
        m_waitTimer.stop();
        unsetCursor();
        beginHandleDrag(g, pos);
        return;
    }
}

void View::drawHandles(QPainter& painter)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    // The dot in the middle of a gizmo: it drags the shape freely (in the plane facing the camera)
    auto dot = [&](const QPointF& p0, bool on) {
        painter.setPen(QPen(on ? QColor(Qt::white) : QColor(0xee, 0xe8, 0xd5), 1.5));
        painter.setBrush(on ? QColor(80, 170, 235) : QColor(38, 139, 210));
        painter.drawEllipse(p0, on ? 6.5 : 5.5, on ? 6.5 : 5.5);
    };
    // The gizmo shared by several selected shapes, all in the gizmo mode: the move arrows and the dot, at the middle
    // of their pivots; they move them all
    QList<Shape*> members;
    QVector3D groupCenter;
    const bool group = groupHandles(members, groupCenter);
    if (group)
    {
        const float L = handleLength(groupCenter);
        if (L > 0)
        {
            const QPointF p0 = toScreen(groupCenter);
            auto hotG = [&](int kind, int a) {
                const HandleGrip g{members[0], kind, a, true};
                return g == handle_hover || (handle_drag && g == handle_active);
            };
            for (int a = 0; a < 3; ++a)
            {
                if (!anyMoveVar(members, a)) continue;
                QVector3D e;
                e[a] = L;
                const QPointF q = toScreen(groupCenter + e);
                const bool on = hotG(0, a);
                QColor c = kHandleColor[a];
                if (on) c = c.lighter(140);
                painter.setPen(QPen(QColor(0, 0, 0, 90), on ? 6.5 : 5, Qt::SolidLine, Qt::RoundCap));
                painter.drawLine(p0, q);
                painter.setPen(QPen(c, on ? 3.5 : 2.5, Qt::SolidLine, Qt::RoundCap));
                painter.drawLine(p0, q);
                const QPointF dir = q - p0;
                const double l = std::hypot(dir.x(), dir.y());
                if (l < 1) continue;
                const QPointF u = dir / l, w(-u.y(), u.x());
                QPolygonF head;
                head << q + u * 6 << q - u * 8 + w * 6 << q - u * 8 - w * 6;
                painter.setPen(QPen(QColor(0, 0, 0, 90), 1));
                painter.setBrush(c);
                painter.drawPolygon(head);
            }
            dot(p0, hotG(3, 0));
        }
    }
    for (Shape* s : shapes)
    {
        if (!s->hasHandles() || (group && members.contains(s))) continue;
        const auto& h = s->handles();
        const auto& vars = s->getVars();
        const QVector3D P = s->handlePivot();
        const float L = handleLength(P);
        if (!(L > 0)) continue;
        const QPointF p0 = toScreen(P);
        auto hot = [&](int kind, int a) {
            const HandleGrip g{s, kind, a};
            return g == handle_hover || (handle_drag && g == handle_active);
        };

        QVector3D ring[3];
        handleAxes(s->handleRotate(), ring);
        for (int a = 0; a < 3; ++a)
        {
            if (!h.rotate[a] || !vars.count(h.rotate[a])) continue;
            QVector3D u, v;
            planeBasis(ring[a], u, v);
            QPolygonF poly;
            for (int k = 0; k <= kRingSegments; ++k)
            {
                const float t = float(2 * M_PI * k / kRingSegments);
                poly << toScreen(P + (0.62f * L) * (std::cos(t) * u + std::sin(t) * v));
            }
            const bool on = hot(1, a);
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(QColor(0, 0, 0, 90), on ? 6 : 4.5));
            painter.drawPolyline(poly);
            QColor c = kHandleColor[a];
            if (on) c = c.lighter(140);
            painter.setPen(QPen(c, on ? 3.5 : 2.2));
            painter.drawPolyline(poly);
        }
        for (int a = 0; a < 3; ++a)
        {
            if (!h.move[a] || !vars.count(h.move[a])) continue;
            QVector3D e;
            e[a] = L;
            const QPointF q = toScreen(P + e);
            const bool on = hot(0, a);
            QColor c = kHandleColor[a];
            if (on) c = c.lighter(140);
            painter.setPen(QPen(QColor(0, 0, 0, 90), on ? 6.5 : 5, Qt::SolidLine, Qt::RoundCap));
            painter.drawLine(p0, q);
            painter.setPen(QPen(c, on ? 3.5 : 2.5, Qt::SolidLine, Qt::RoundCap));
            painter.drawLine(p0, q);
            const QPointF dir = q - p0;
            const double l = std::hypot(dir.x(), dir.y());
            if (l < 1) continue;
            const QPointF u = dir / l, w(-u.y(), u.x());
            QPolygonF head;
            head << q + u * 6 << q - u * 8 + w * 6 << q - u * 8 - w * 6;
            painter.setPen(QPen(QColor(0, 0, 0, 90), 1));
            painter.setBrush(c);
            painter.drawPolygon(head);
        }
        // The scale knobs: a square on each of the part's own axes
        const QMatrix4x4 R = Shape::handleRotation(s->handleRotate());
        for (int a = 0; a < 3; ++a)
        {
            if (!h.scale[a] || !vars.count(h.scale[a])) continue;
            const QPointF q = toScreen(P + R.mapVector(unitAxis(a)) * (kKnobAt * L));
            const bool on = hot(2, a);
            QColor c = kHandleColor[a];
            if (on) c = c.lighter(140);
            const double r = on ? 6.5 : 5.5;
            painter.setPen(QPen(QColor(0, 0, 0, 140), 1.5));
            painter.setBrush(c);
            painter.drawRect(QRectF(q.x() - r, q.y() - r, 2 * r, 2 * r));
        }
        dot(p0, hot(3, 0));
    }
    painter.restore();
    drawProvisional(painter);
}

bool View::ringPoint(QPoint pos, const QVector3D& pivot, const QVector3D& axis, QVector3D& out) const
{
    // Where the pixel's line of sight meets the plane through the pivot
    // normal to the axis (NDC depth 0.5 is infinitely far: use 0 and 0.25)
    const QVector3D a = toModelPos(pos, 0), b = toModelPos(pos, 0.25);
    const QVector3D d = b - a;
    const float denom = QVector3D::dotProduct(axis, d);
    if (std::fabs(denom) < 1e-7f * std::max(1e-9f, d.length())) return false;   // seen edge-on
    const float t = QVector3D::dotProduct(axis, pivot - a) / denom;
    out = a + t * d;
    return std::isfinite(out.x()) && std::isfinite(out.y()) && std::isfinite(out.z());
}

void View::applyHandleNumber(libfive::Tree::Id id, float value)
{
    applyHandleNumbers({{id, value}});
}

void View::applyHandleNumbers(const std::map<libfive::Tree::Id, float>& m)
{
    emit(varsDragged(QMap<libfive::Tree::Id, float>(m)));
    bool changed = false;
    for (auto& s : shapes)
    {
        changed |= s->updateVars(m);
    }
    if (changed)
    {
        busy.show();
        emit(renderBusy(true));
        requestSlice();
    }
    // The field the field viewer shows is not a shape that is drawn, so nothing above reached its numbers: when the numbers that are
    // dragged are its own (the gizmo of the field, or a surface of it), the disc shows it from the new ones as they come, not
    // from the numbers of the last run of the script
    bool fieldChanged = false;
    for (auto source = m_fieldSources.begin(); source != m_fieldSources.end(); ++source)
    {
        for (const auto& v : m)
        {
            const auto found = source->vars.find(v.first);
            if (found == source->vars.end() || found->second == v.second) continue;
            found->second = v.second;
            if (source.key() == m_fieldKey) fieldChanged = true;
        }
    }
    if (fieldChanged && fieldShown()) requestFieldSlice();
    m_overlay->update();
}

void View::beginHandleDrag(const HandleGrip& g, QPoint pos)
{
    if (g.kind == 0 || g.kind == 3)
    {
        // The move arrow (one axis) and the dot (all three): the numbers that move the shape -- of every selected
        // shape, for the gizmo they share -- are noted with the values they have now
        QList<Shape*> members;
        QVector3D groupCenter;
        if (g.group) { if (!groupHandles(members, groupCenter)) return; }
        else members << g.shape;
        handle_vars.clear();
        for (const Shape* s : members)
        {
            const auto& h = s->handles();
            const auto& vars = s->getVars();
            for (int a = 0; a < 3; ++a)
            {
                if (g.kind == 0 && a != g.axis) continue;
                const auto it = h.move[a] ? vars.find(h.move[a]) : vars.end();
                if (it != vars.end()) handle_vars.append({h.move[a], a, it->second});
            }
        }
        if (handle_vars.isEmpty()) return;
        handle_active = g;
        handle_press = pos;
        handle_pivot = g.group ? groupCenter : g.shape->handlePivot();
        if (g.kind == 0)
        {
            handle_len = handleLength(handle_pivot);
            // (a move arrow is along the world's axis)
            handle_dir = toScreen(handle_pivot + unitAxis(g.axis) * handle_len) - toScreen(handle_pivot);
            if (std::hypot(handle_dir.x(), handle_dir.y()) < 1.0) return;    // pointing at the camera
        }
        else
        {
            // The dot: the shapes follow the cursor in the plane through the pivot that faces the camera
            free_normal = camera.towardViewer().normalized();
            if (!ringPoint(pos, handle_pivot, free_normal, free_hit0)) return;
        }
        handle_drag = true;
        this->setCursor(g.kind == 3 ? Qt::SizeAllCursor : Qt::ClosedHandCursor);
        emit(dragStart());
        m_overlay->update();
        return;
    }
    const auto& h = g.shape->handles();
    handle_id = g.kind == 1 ? h.rotate[g.axis] : h.scale[g.axis];
    const auto& vars = g.shape->getVars();
    const auto it = vars.find(handle_id);
    if (it == vars.end()) return;
    handle_value0 = it->second;
    handle_active = g;
    handle_press = pos;
    handle_pivot = g.shape->handlePivot();
    for (int a = 0; a < 3; ++a)
    {
        handle_scale_id[a] = h.scale[a];
        const auto sv = h.scale[a] ? vars.find(h.scale[a]) : vars.end();
        handle_scale0[a] = sv == vars.end() ? 1.0f : sv->second;
    }
    if (g.kind == 2)
    {
        handle_len = handleLength(handle_pivot);
        // (a scale knob is along the part's own axis)
        const QVector3D e = Shape::handleRotation(g.shape->handleRotate()).mapVector(unitAxis(g.axis)) * handle_len;
        handle_dir = toScreen(handle_pivot + e) - toScreen(handle_pivot);
        if (std::hypot(handle_dir.x(), handle_dir.y()) < 1.0) return;    // pointing at the camera
    }
    else
    {
        QVector3D ring[3];
        handleAxes(g.shape->handleRotate(), ring);
        handle_axis = ring[g.axis];
        QVector3D at;
        if (!ringPoint(pos, handle_pivot, handle_axis, at)) return;
        handle_vec = at - handle_pivot;
        handle_turn = 0;
    }
    handle_drag = true;
    this->setCursor(Qt::ClosedHandCursor);
    emit(dragStart());
    m_overlay->update();
}

void View::dragHandle(QPoint pos)
{
    if (handle_active.kind == 0)
    {
        const double l2 = QPointF::dotProduct(handle_dir, handle_dir);
        if (l2 < 1) return;
        const double t = QPointF::dotProduct(QPointF(pos - handle_press), handle_dir) / l2;
        std::map<libfive::Tree::Id, float> numbers;          // (every shape that moves with the arrow)
        for (const auto& v : handle_vars) numbers[v.id] = v.value0 + float(t * handle_len);
        applyHandleNumbers(numbers);
    }
    else if (handle_active.kind == 3)
    {
        // The dot: where the cursor's ray meets the plane facing the camera, against where it met it at the start
        QVector3D at;
        if (!ringPoint(pos, handle_pivot, free_normal, at)) return;
        const QVector3D d = at - free_hit0;
        std::map<libfive::Tree::Id, float> numbers;
        for (const auto& v : handle_vars) numbers[v.id] = v.value0 + d[v.axis];
        applyHandleNumbers(numbers);
    }
    else if (handle_active.kind == 2)
    {
        const double l2 = QPointF::dotProduct(handle_dir, handle_dir);
        if (l2 < 1) return;
        // (the knob sits at kKnobAt of the arrow: pulling it out by a share of that grows the part by it)
        const double t = QPointF::dotProduct(QPointF(pos - handle_press), handle_dir) / l2;
        const float factor = float(1.0 + t / kKnobAt);
        std::map<libfive::Tree::Id, float> numbers;
        const bool together = QGuiApplication::keyboardModifiers() & Qt::ShiftModifier;
        for (int a = 0; a < 3; ++a)
        {
            if (!handle_scale_id[a]) continue;
            if (!together && a != handle_active.axis) continue;
            numbers[handle_scale_id[a]] = std::max(0.01f, handle_scale0[a] * factor);
        }
        if (!numbers.empty()) applyHandleNumbers(numbers);
    }
    else
    {
        QVector3D at;
        if (!ringPoint(pos, handle_pivot, handle_axis, at)) return;
        const QVector3D now = at - handle_pivot;
        if (now.length() < 1e-9f || handle_vec.length() < 1e-9f) return;
        // the turn since the last move (small, so never across +-180)
        const float s = QVector3D::dotProduct(handle_axis, QVector3D::crossProduct(handle_vec, now));
        const float c = QVector3D::dotProduct(handle_vec, now);
        handle_turn += float(std::atan2(s, c) * 180.0 / M_PI);
        handle_vec = now;
        applyHandleNumber(handle_id, handle_value0 + handle_turn);
    }
}

void View::updateSectionReadout(QPoint pos)
{
    // The pixel's line of sight, and where it meets a plane: how far along it (`t`, smaller = nearer the viewer) and what the plane
    // says there (NDC depth 0.5 is infinitely far with this camera's perspective)
    const QVector3D a = toModelPos(pos, 0), b = toModelPos(pos, 0.25);
    auto meet = [&](int ax, float offset, float& t, QVector3D& x) {
        if (b[ax] == a[ax] || !std::isfinite(a[ax]) || !std::isfinite(b[ax])) return false;
        t = (offset - a[ax]) / (b[ax] - a[ax]);
        x = a + t * (b - a);
        return true;
    };
    QString sectionText, fieldText;
    float sectionT = 0, fieldT = 0;
    if (section.enabled && section.field && m_sec.slice.valid() && m_sec.slice.axis == section.axis)
    {
        const FieldSlice& slice = m_sec.slice;
        const int ax = section.axis;
        QVector3D x;
        if (meet(ax, section.offset, sectionT, x))
        {
            const float d = slice.sample(x[FieldSlice::uAxis(ax)], x[FieldSlice::vAxis(ax)]);
            if (std::isfinite(d) && d < slice.fade)
            {
                sectionText = QString("d = %1  %2").arg(d, 0, 'g', 4)
                                  .arg(d < 0 ? "inside" : "outside");
            }
        }
    }
    if (fieldShown() && m_fld.slice.valid() && m_fld.slice.axis == m_fieldView.axis)
    {
        // The shown field's value under the cursor, on the disc (the sample nearest to it)
        const FieldSlice& slice = m_fld.slice;
        const int ax = m_fieldView.axis, ua = FieldSlice::uAxis(ax), va = FieldSlice::vAxis(ax);
        QVector3D x;
        if (meet(ax, m_fieldView.offset(), fieldT, x))
        {
            const float d = slice.sample(x[ua], x[va]);
            const float du = slice.max[ua] - slice.min[ua], dv = slice.max[va] - slice.min[va];
            const int i = int((x[ua] - slice.min[ua]) / du * slice.w), j = int((x[va] - slice.min[va]) / dv * slice.h);
            if (std::isfinite(d) && d < 0 && slice.hasColor && slice.color.size() == slice.w * slice.h &&
                du > 0 && dv > 0 && i >= 0 && j >= 0 && i < slice.w && j < slice.h &&
                std::isfinite(slice.color[j * slice.w + i]))
            {
                fieldText = QString("%1 = %2").arg(m_fieldKey.startsWith("line:") ? T("field") : m_fieldKey)
                                .arg(slice.color[j * slice.w + i], 0, 'g', 5);
            }
        }
    }
    // (the box at the cursor tells about the plane that is nearer the viewer where both are under it)
    const QString text = fieldText.isEmpty() ? sectionText
                       : sectionText.isEmpty() ? fieldText
                       : (fieldT < sectionT ? fieldText : sectionText);
    section_readout_pos = pos;
    if (sectionText != m_sectionText)
    {
        m_sectionText = sectionText;
        emit(sectionReadout(sectionText));
    }
    if (fieldText != m_fieldText)
    {
        m_fieldText = fieldText;
        emit(fieldReadout(fieldText));
    }
    const bool redraw = !text.isEmpty() || !section_readout.isEmpty();     // (the box moves with the cursor, and goes when it is empty)
    section_readout = text;
    if (redraw)
    {
        m_overlay->update();
    }
}

void View::showAxes(bool a)
{
    show_axes = a;
    update();
}

void View::showBBox(bool b)
{
    show_bbox = b;
    update();
}

void View::toDCMeshing()
{
    setAlgorithm(libfive::DUAL_CONTOURING);
}

void View::toIsoMeshing()
{
    setAlgorithm(libfive::ISO_SIMPLEX);
}

void View::toHybridMeshing()
{
    setAlgorithm(libfive::HYBRID);
}

void View::setAlgorithm(libfive::BRepAlgorithm a)
{
    if (a != alg) {
        alg = a;
        for (auto& s : shapes) {
            s->startRender(settings, alg);
        }
    }
}

void View::checkMeshes() const
{
    bool all_done = true;
    QList<const libfive::Mesh*> meshes;
    for (auto s : shapes)
    {
        if (s->done())
        {
            meshes.push_back(s->getMesh());
        }
        else
        {
            all_done = false;
        }
    }
    if (all_done)
    {
        emit(meshesReady(meshes));
    }
}

QHash<int, QString> View::cacheStates() const
{
    QHash<int, QString> out;
    for (const auto* s : shapes)
    {
        if (!s->renderCache() || s->sourceLine() < 0) continue;
        const QString kind = s->renderCacheKind();
        out[s->sourceLine()] = (kind == "wait" ? QString("on") : kind) + "|" + s->renderCacheState();
    }
    return out;
}

}   // namespace FielDes
