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
#include <chrono>
#include <cstdio>
#include "fieldes/regionformat.hpp"
#include <QKeyEvent>
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
#include <QClipboard>
#include <QGuiApplication>
#include <QElapsedTimer>
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
#include "fieldes/popout.hpp"
#include "fieldes/units.hpp"
#include "fieldes/view.hpp"
#include "fieldes/typeicons.hpp"
#include "fieldes/colormap.hpp"
#include "fieldes/scenetree.hpp"
#include "fieldes/shader.hpp"
#include "fieldes/typeicons.hpp"

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
    qApp->installEventFilter(this);             // (Shift swaps the gizmo's arrows for planes: the keys are seen wherever the focus is)

    connect(&busy, &Busy::redraw, this, &View::update);
    connect(this, &View::startRender, &busy,
            [&](Settings){ if (shapes.size()) { busy.show(); emit(renderBusy(true)); } });
    connect(this, &View::meshesReady, &busy,
            [&](QList<const libfive::Mesh*>){ busy.hide(); emit(renderBusy(false)); });

    // Clicking the viewport focuses it, so its own keys (standard views)
    // work without stealing them from the editor
    setFocusPolicy(Qt::ClickFocus);
    setObjectName("FielDesView");               // (the automation finds the viewport by it)
    connect(&camera, &Camera::changed, this, &View::update);

    connect(&camera, &Camera::animDone, this, &View::redrawPicker);

    m_wheelPendingTimer.setInterval(120);
    connect(&m_wheelPendingTimer, &QTimer::timeout, this, [this] { applyPendingWheel(); });
    m_wheelTimer.setSingleShot(true);
    m_wheelTimer.setInterval(450);
    connect(&m_wheelTimer, &QTimer::timeout, this, [this] {
        if (!m_wheelScaling) return;
        m_wheelScaling = false;
        emit(dragEnd());
        m_overlay->update();
    });
    m_waitTimer.setInterval(40);
    connect(&m_waitTimer, &QTimer::timeout, this, &View::startWaitingGrip);
    pick_timer.setSingleShot(true);
    pick_timer.setInterval(250);
    connect(&pick_timer, &QTimer::timeout, this, &View::redrawPicker);

    // The model tree floats over the top-left corner of the viewport
    m_scene = new ScenePanel(this);
    PopOut::attach(m_scene, T("Model tree"), "model-tree");

    // 2D overlays, below the model tree
    m_overlay = new ViewOverlay(this, [this](QPainter& p) { paintOverlay(p); });
    m_overlay->setGeometry(rect());
    m_overlay->lower();

    // Analysis results: which field, how much deformation, elements
    m_resultPanel = new ResultPanel(this);
    m_resultPanel->hide();
    PopOut::attach(m_resultPanel, T("Result"), "result");
    m_livePanel = new LivePanel(this);
    m_livePanel->hide();
    PopOut::attach(m_livePanel, T("Live view"), "live-view");
    auto forResults = [this](auto fn, bool resample) {
        for (auto s : shapes) if (s->hasResult() || s->stepCount() > 0) fn(s);
        updateResultPanel();
        if (resample && section.enabled) requestSlice();
        pick_timer.start();
        update();
    };
    connect(m_resultPanel, &ResultPanel::channelChanged, this, [=](int i) {
        for (auto s : shapes)
            if (s->hasResult() && i >= 0 && size_t(i) < s->channels().size()) { m_choiceChannel = s->channels()[size_t(i)].name; break; }
        forResults([i](Shape* s) { s->setChannel(i); }, true);
    });
    connect(m_resultPanel, &ResultPanel::deformChanged, this, [=](float f) {
        for (auto s : shapes)
            if (s->hasResult() && s->hasDeformation() && s->deformAuto() > 0) { m_choiceDeform = f / s->deformAuto(); break; }
        forResults([f](Shape* s) { s->setDeformScale(f); }, false);
    });
    connect(m_resultPanel, &ResultPanel::elementsToggled, this, [=](bool on) {
        m_choiceElements = on ? 1 : 0;
        forResults([on](Shape* s) { s->setShowElements(on); }, false);
    });
    connect(m_resultPanel, &ResultPanel::stepChanged, this, [=](int k) {
        forResults([k](Shape* s) { s->setStep(k); }, true);
    });
    connect(m_resultPanel, &ResultPanel::flowToggled, this, [=](bool on) {
        m_choiceLines = on ? 1 : 0;
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

    // Section field sampling: debounced, in a worker thread of its own (every field viewer has such a plane too: see initViewer)
    for (Plane* plane : {&m_sec})
    {
        plane->timer.setSingleShot(true);
        plane->timer.setInterval(16);          // (about a frame: a drag that moves the plane faster is still one pass)
        connect(&plane->timer, &QTimer::timeout, this, [this, plane]{ startSliceAt(*plane, false); });
        connect(&plane->watcher, &QFutureWatcher<FieldSlice>::finished, this, [this, plane]{ onSliceFinished(*plane); });
    }
}

View::~View()
{
    m_gl_ending = true;
    for (auto& kv : m_viewers)
    {
        kv.second->plane.generation++;
        kv.second->plane.watcher.waitForFinished();
    }
    for (Plane* plane : {&m_sec})
    {
        plane->generation++;              // abandon any sampling in flight
        plane->watcher.waitForFinished();
    }
    makeCurrent();
    for (auto& kv : m_viewers)
    {
        kv.second->plane.tex.reset();
        kv.second->plane.colorTex.reset();
        kv.second->plane.lutTex.reset();
        if (kv.second->plane.vao.isCreated()) kv.second->plane.vao.destroy();
        if (kv.second->plane.vbo.isCreated()) kv.second->plane.vbo.destroy();
    }
    for (Plane* plane : {&m_sec})
    {
        plane->tex.reset();
        plane->colorTex.reset();
        plane->lutTex.reset();
        if (plane->vao.isCreated()) plane->vao.destroy();
        if (plane->vbo.isCreated()) plane->vbo.destroy();
    }
    for (auto s : findChildren<Shape*>())
    {
        s->freeGL();
    }
    doneCurrent();
}

// A new result shape looks as the one before it did: what was chosen on the result card is kept, run after run and picture after picture
void View::refreshGizmoModes()
{
    for (auto& s : shapes) applyViewerChoices(s);
    update();
}

void View::applyViewerChoices(Shape* s) const
{
    // When the gizmo is shown is the state of the model tree's gizmo button (the script does not say): where the button was pressed it is that, else what the shape came with
    if (m_scene)
    {
        const int m = m_scene->gizmoModeOfLine(s->sourceLine());
        if (m >= 0)
        {
            const Shape::Gizmo::Mode mode = m == 1 ? Shape::Gizmo::NEVER : m == 2 ? Shape::Gizmo::ALWAYS : Shape::Gizmo::CLICK;
            s->setGizmoMode(mode);
            s->setOutlineMode(mode);
        }
    }
    if (s->hasFlowLines())
    {
        s->setOpacity(m_flowOpacity);
        if (m_choiceLines >= 0) s->setShowFlowLines(m_choiceLines != 0);
    }
    if (!s->hasResult()) return;
    if (!m_choiceChannel.isEmpty())
    {
        const auto& channels = s->channels();
        for (size_t i = 0; i < channels.size(); ++i)
            if (channels[i].name == m_choiceChannel) { s->setChannel(int(i)); break; }
    }
    if (m_choiceDeform >= 0 && s->hasDeformation() && s->deformAuto() > 0) s->setDeformScale(m_choiceDeform * s->deformAuto());
    if (m_choiceElements >= 0 && s->hasElements()) s->setShowElements(m_choiceElements != 0);
}

void View::setShapes(QList<Shape*> new_shapes, int keepFromLine)
{
    for (auto s : new_shapes) applyViewerChoices(s);
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
    const auto tKeys = std::chrono::steady_clock::now();
    for (auto& s : new_shapes)
    {
        const auto tOne = std::chrono::steady_clock::now();
        const ShapeKey key{s->getUniqueId(canonical), s->identityKey(canonical)};
        new_shapes_key.insert({s, key});
        new_shapes_map.insert({key, s});
        const double ms = 1000.0 * std::chrono::duration<double>(std::chrono::steady_clock::now() - tOne).count();
        if (ms > 20 && std::getenv("FIELDES_TIMING"))
            fprintf(stderr, "[view] setShapes: the key of the shape of line %d took %.0f ms\n", s->sourceLine() + 1, ms);
    }
    if (std::getenv("FIELDES_TIMING"))
        fprintf(stderr, "[view] setShapes: %d new shapes keyed in %.0f ms\n", int(new_shapes.size()),
                1000.0 * std::chrono::duration<double>(std::chrono::steady_clock::now() - tKeys).count());

    // Erase all existing shapes that aren't in the new_shapes list
    bool vars_changed = false;
    bool any_running = false;
    for (auto itr=shapes.begin(); itr != shapes.end(); /* no update */ )
    {
        // (same geometry coloured differently -- another field or range -- is a different shape: its colours are computed while meshing)
        auto n = new_shapes_map.find(ShapeKey{(*itr)->getUniqueId(canonical), (*itr)->identityKey(canonical)});
        if (n == new_shapes_map.end() && keepFromLine >= 0 && (*itr)->sourceLine() >= keepFromLine)
        {
            // (a statement that has not run yet in this run: its shape is the last run's, until the run makes it -- not gone)
            any_running |= !(*itr)->done();
            ++itr;
        }
        else if (n == new_shapes_map.end())
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
            if (*itr == m_dotHover) m_dotHover = nullptr;
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
    refreshSymmetry();              // (the symmetry model is another shape now)
    linkFollowers();
    applyActiveCopy();              // (the copies are linked to their points now: the one that was pressed shows the gizmo again)
}

void View::linkFollowers()
{
    std::map<libfive::Tree::Id, Shape*> made;
    for (Shape* s : shapes) made[s->sourceId()] = s;
    for (Shape* s : shapes)
    {
        Shape* model = nullptr;
        if (const auto of = s->followId())
        {
            const auto it = made.find(of);
            if (it != made.end() && it->second != s) model = it->second;
        }
        s->linkSource(model);
    }
}

bool View::isRendering() const
{
    for (const auto* s : shapes) if (!s->done()) return true;
    return false;
}

QString View::graphicsInfo()
{
    if (!context()) return QString();
    makeCurrent();
    auto text = [this](GLenum name) { return QString::fromLatin1(reinterpret_cast<const char*>(glGetString(name))); };
    const QString out = QString("%1 / %2, OpenGL %3").arg(text(GL_VENDOR), text(GL_RENDERER), text(GL_VERSION));
    doneCurrent();
    return out;
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
        bool on = lines0.contains(s->sourceLine());
        // (the dot of an image -- the point of a placed copy, of an image in a symmetry -- is the point it is the image of: selected with it)
        if (!on && s->gizmo().dotOnly && s->followedShape()) on = lines0.contains(s->followedShape()->sourceLine());
        s->setSelected(on);
    }
    if (m_copyLine >= 0 && !lines0.contains(m_copyLine)) m_copyLine = -1;          // (another row is selected: no copy of the point is the one that was pressed any more)
    applyActiveCopy();
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
    if (m_copyLine >= 0) m_copyLine = moveLine(m_copyLine);
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

QStringList View::fieldKeysInside(const QRect& r) const
{
    QStringList out;
    const QRectF rect(r);
    const auto layout = iconLayout();
    for (auto it = layout.constBegin(); it != layout.constEnd(); ++it)
    {
        if (rect.contains(it.value())) out << it.key();
    }
    return out;
}

QList<int> View::linesInside(const QRect& r) const
{
    // A shape is inside when every vertex of its mesh -- as drawn from this camera -- is in the rectangle; a point when its dot is, and a set of
    // points when all of its dots are (they are not painted: their dots are what there is of them on the screen)
    QList<int> out;
    const QMatrix4x4 M = camera.M();
    const QRectF rect(r);
    const float W = float(width()), H = float(height());
    auto inFront = [&](const QVector3D& p) {
        return M.row(3).x() * p.x() + M.row(3).y() * p.y() + M.row(3).z() * p.z() + M.row(3).w() > 1e-6f;
    };
    for (const auto* s : shapes)
    {
        if (s->sourceLine() < 0 || out.contains(s->sourceLine())) continue;
        QVector3D dot;
        if (s->isPoint() && s->dotPosition(dot))
        {
            if (inFront(dot) && rect.contains(toScreen(dot))) out << s->sourceLine();
            continue;
        }
        if (!s->markers().empty())
        {
            bool all = true;
            for (const QVector3D& p : s->markers())
            {
                if (!inFront(p) || !rect.contains(toScreen(p))) { all = false; break; }
            }
            if (all) out << s->sourceLine();
            continue;
        }
        if (s->isCurve())
        {
            bool all = !s->outlines().empty();
            for (const auto& line : s->outlines())
            {
                for (const auto& v : line)
                {
                    const QVector3D p(v.x(), v.y(), v.z());
                    if (!inFront(p) || !rect.contains(toScreen(p))) { all = false; break; }
                }
                if (!all) break;
            }
            if (all) out << s->sourceLine();
            continue;
        }
        if (!s->hasMesh()) continue;
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

void View::setAppMode(bool on)
{
    m_appMode = on;
    // (the model tree is not part of an app window: nothing of it is shown, and nothing writes the script)
    if (m_scene) m_scene->setVisible(!on);
    update();
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

    // A viewport that is pulled out into a window of its own, or put back, is given a new graphics context: this one lets go of what it held when it ends
    connect(context(), &QOpenGLContext::aboutToBeDestroyed, this, &View::releaseGL, Qt::DirectConnection);
}

void View::releaseGL()
{
    if (m_gl_ending) return;                           // (the destructor lets go of it all itself)
    const bool needs_gl = (context() != QOpenGLContext::currentContext());
    if (needs_gl) makeCurrent();
    pick_fbo.reset();
    auto release = [](Plane& plane) {
        plane.tex.reset();
        plane.colorTex.reset();
        plane.lutTex.reset();
        if (plane.vao.isCreated()) plane.vao.destroy();
        if (plane.vbo.isCreated()) plane.vbo.destroy();
        plane.dirty = true;                            // (sent to the graphics card again)
    };
    for (auto& kv : m_viewers) release(kv.second->plane);
    release(m_sec);
    for (auto s : findChildren<Shape*>()) s->freeGL();       // (their buffers are made again at the next draw)
    arrow.releaseGL();                                       // (and the buffers of what the viewport draws itself)
    glyphs.releaseGL();
    axes.releaseGL();
    background.releaseGL();
    bbox.releaseGL();
    busy.releaseGL();
    for (QOpenGLShaderProgram** program : {&Shader::basic, &Shader::busy, &Shader::line, &Shader::slice})
    {
        delete *program;
        *program = nullptr;
    }
    if (needs_gl) doneCurrent();
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
        if (!s->noPaint() && !s->is2D())
        {
            // (a patch of a surface lies ON the face of the body it was picked on, at the very same depth: it is drawn pulled a little towards the viewer, and so it is here --
            // else the body, drawn first, is what a click on the patch picks, and the menu is the body's, not the surface's)
            const bool onTheSurface = s->isSurfacePatch();
            if (onTheSurface)
            {
                glEnable(GL_POLYGON_OFFSET_FILL);
                glPolygonOffset(-1.0f, -1.0f);
            }
            s->drawMonochrome(m, color);
            if (onTheSurface) glDisable(GL_POLYGON_OFFSET_FILL);
        }
        ++color;                // (a point is not painted, nor picked: the numbers of the others stay what they were)
    }
    const int pickPixels = camera.size.width() * camera.size.height();
    if (pick_depth.size() != pickPixels)
    {
        pick_depth.resize(pickPixels);
    }
    bool depthRead = false;
    {
        // The 2D shapes are picked after the 3D ones, as they are drawn over them: where one covers a pixel, it is what is there.  The depth of the BODIES is read first -- a press on a body that no
        // 2D shape covers is at the depth of the body, not at the far plane that the buffer holds once it is cleared for the 2D shapes (that put the press of a surface of a body far away from it,
        // wherever a 2D shape was shown: a pulled face of an extrusion changed its radius to 119) -- and the depth of a 2D shape takes its place only where the shape was drawn
        bool any = false;
        for (auto& s : shapes) any = any || (s->is2D() && !s->noPaint());
        if (any)
        {
            glReadPixels(0, 0, camera.size.width(), camera.size.height(), GL_DEPTH_COMPONENT, GL_FLOAT, pick_depth.data());
            // (what the bodies alone have: a press inside a 2D shape that is not pulled by its edge is a press on the body under it)
            pick_img_bodies = pick_fbo->toImage();
            pick_depth_bodies = pick_depth;
            glClear(GL_DEPTH_BUFFER_BIT);
            color = 1;
            for (auto& s : shapes)
            {
                if (!s->noPaint() && s->is2D()) s->drawMonochrome(m, color);
                ++color;
            }
            QVector<float> flat(pickPixels);
            glReadPixels(0, 0, camera.size.width(), camera.size.height(), GL_DEPTH_COMPONENT, GL_FLOAT, flat.data());
            for (int i = 0; i < pickPixels; ++i)
            {
                if (flat[i] < 1.0f) pick_depth[i] = flat[i];
            }
            depthRead = true;
        }
    }
    if (clipping)
    {
        glDisable(GL_CLIP_DISTANCE0);
    }

    pick_img = pick_fbo->toImage();
    if (!depthRead)
    {
        // (no 2D shape is shown: the bodies are all there is, nothing is under anything)
        pick_img_bodies = QImage();
        pick_depth_bodies.clear();
    }

    // There's no utility function to get the depth buffer, so we manually
    // read it with glReadPixels here.
    if (!depthRead)
    {
        glReadPixels(0, 0, camera.size.width(), camera.size.height(),
                     GL_DEPTH_COMPONENT, GL_FLOAT, pick_depth.data());
    }

    glDisable(GL_DEPTH_TEST);
    pick_fbo->release();

    if (needs_gl)
    {
        doneCurrent();
    }
}

void View::paintGL()
{
    // (for the timing logs: what a frame costs while a gizmo is dragged)
    struct FrameClock
    {
        QElapsedTimer t;
        bool on = false;
        ~FrameClock() { if (on) std::fprintf(stderr, "[drag] frame drawn in %.1f ms\n", double(t.nsecsElapsed()) / 1e6); }
    } frameClock;
    if (gizmo_drag && std::getenv("FIELDES_TIMING"))
    {
        frameClock.on = true;
        frameClock.t.start();
    }
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
    // (the backdrop, with a shader of its own; the depth of the frame is cleared after it, so that nothing of the last one -- or of a driver that does not keep it -- is in it.  Measured in a real window
    // on Windows and under WSLg, with five ways of putting the backdrop on the screen: the shapes' shader drew it black under Mesa's software renderer, a gradient painted by the painter was lost on both,
    // a quad with a shader of its own drew the same gradient on both)
    background.draw();
    glClearDepthf(1.0f);
    glClear(GL_DEPTH_BUFFER_BIT);

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
            if (s->is2D()) continue;                // (the 2D shapes are drawn last, over everything)
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
    for (auto& kv : m_viewers)
    {
        FieldViewer* v = kv.second.get();
        if (v->open && v->shown) drawSlicePlane(m, v->plane, fieldPlane(*v));
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

    // The 2D shapes, over everything the 3D shapes drew: a shape that is drawn on geometry (or in it) is always seen whole -- it never hides behind a body, nor hides
    // what it was drawn for under a body's surface -- and they are in front of each other as their places say.  The picking buffer is made the same way
    {
        bool any = false;
        for (auto& s : shapes) any = any || (s->is2D() && !s->noPaint());
        if (any)
        {
            glClear(GL_DEPTH_BUFFER_BIT);
            setClipUniform(clipping);
            for (auto& s : shapes)
                if (s->is2D() && !s->noPaint()) s->draw(m);
            if (clipping)
            {
                setClipUniform(false);
                glDisable(GL_CLIP_DISTANCE0);
            }
        }
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
    drawWorkPlane(painter);
    drawSymmetry(painter);
    drawFieldIcons(painter);
    drawOutlines(painter);
    drawMarkers(painter);
    drawPointDots(painter);
    drawGizmo(painter);
    drawHints(painter);
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
    if (m_appMode) drawNameLabels(painter);
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
        const QString text = (probe_label.isEmpty() ? T("value") : Units::shownLabel(probe_label)) +
                             ":  " + QString::number(Units::shownValue(probe_label, probe_value), 'g', 5);
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
        auto num = [](float v) { return QString::number(v * float(Units::lengthScale()), 'f', Units::lengthDecimals(3)); };
        const QString text = QString("X %1   Y %2   Z %3  %4")
            .arg(num(cursor_pos.x()), num(cursor_pos.y()), num(cursor_pos.z()), Units::lengthLabel());
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
        s->settleField();
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
        applyViewerChoices(s);          // (a picture after every iteration: it looks as the one before it did)
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
    const float shown = units * float(Units::lengthScale());      // (the length is a round number of the unit chosen in Settings > Units: shown units per 100 px)
    const float target = shown * 1.4f;               // ~140 px
    const float p10 = std::pow(10.0f, std::floor(std::log10(target)));
    float len = p10;
    for (float m : {1.0f, 2.0f, 5.0f, 10.0f}) if (p10 * m <= target) len = p10 * m;
    const float px = 100.0f * len / shown;
    QFont font = painter.font();
    font.setPointSizeF(8.5);
    painter.setFont(font);
    const QFontMetrics fm(font);
    const float x0 = width() / 2.0f - px / 2, y = height() - 16.0f;
    // (the ruler has no fill of its own for its second half: it took the brush the last thing drawn before it left on the painter -- the pink
    // of a selected surface's key, a boundary condition's colour -- and was drawn in it)
    painter.save();
    painter.setBrush(Qt::NoBrush);
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
    const QString label = fmt(len) + " " + Units::lengthLabel();
    painter.setPen(ink);
    painter.drawText(QRectF(x0 + px + 6, y - fm.height() / 2.0 - 1, fm.horizontalAdvance(label) + 4, fm.height()),
                     Qt::AlignLeft | Qt::AlignVCenter, label);
    painter.drawText(QRectF(x0 - fm.horizontalAdvance("0") - 6, y - fm.height() / 2.0 - 1,
                            fm.horizontalAdvance("0") + 2, fm.height()), Qt::AlignRight | Qt::AlignVCenter, "0");
    bottom_items.push_back(QRectF(x0 - fm.horizontalAdvance("0") - 8, y - fm.height() / 2.0 - 4,
                                  px + fm.horizontalAdvance("0") + fm.horizontalAdvance(label) + 20, fm.height() + 6));
    painter.restore();
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
        // colour alone: no bar of percentages, and nothing read under the cursor; the supports of a print are amber, and a line of the legend of the result: see ResultPanel)
        if (!s->hasColorField() || !s->hasMesh() || s->hasResult() || s->colorMap() == "bc" || s->colorMap() == "fit" || s->colorMap() == "support") continue;
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
    int right = (m_resultPanel && m_resultPanel->isVisible() && m_resultPanel->parentWidget() == this) ? m_resultPanel->geometry().left() - 10 : width() - 14;

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
        auto num = [&b](float v) { return QString::number(Units::shownValue(b.label, v), 'g', 4); };      // (in the units chosen in Settings > Units, as the label names them)
        const int ticks = 5;
        const QString shownTitle = Units::shownLabel(b.label);
        int textW = fm.horizontalAdvance(shownTitle) + 16;   // (+ the close button)
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
                         Qt::AlignLeft | Qt::AlignVCenter, shownTitle);
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
    for (const auto& c : r->channels()) st.labels << Units::shownLabel(c.label);
    st.current = r->channel();
    st.lo = float(Units::shownValue(r->channelLabel(), r->displayLo()));      // (the colour bar in the units chosen in Settings > Units, as the label of the field names them)
    st.hi = float(Units::shownValue(r->channelLabel(), r->displayHi()));
    st.map = r->colorMap();
    st.note = r->valueNote();
    st.noteHelp = r->valueNoteHelp();
    st.elementInfo = r->hasElements() ? r->elementText() : r->elementNote();
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
    for (const QStringList& row : r->keyResults()) st.keyResults << Units::shownRow(row);
    // (the supports of a print are a body of the scene in amber: one more line of the legend of the geometry change, and no card of their own)
    // (its mesh, and not hasMesh(): that is true once the mesh is on the graphics card, at the next paint, and this runs when the mesh arrives)
    for (auto s : shapes) if (s->hasColorField() && s->getMesh() && s->colorMap() == "support") st.supports = true;
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

void View::unitsChanged()
{
    updateResultPanel();          // (the labels and the numbers of the card and of its colour bar are made again in the units chosen)
    if (m_overlay) m_overlay->update();
    update();
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
    m_lastPos = event->pos();

    if (m_pullShape)
    {
        if (!m_pullActive)
        {
            // (a press on a curve that moves: a point is pulled out of it from here)
            if ((event->pos() - m_pullPressPos).manhattanLength() >= 4 && m_leftDown) beginPull(event->pos());
        }
        if (m_pullActive)
        {
            QVector3D at;
            if (ringPoint(event->pos(), m_pullHit, m_pullNormal, at))
            {
                if (m_wp.open && m_wp.exists && std::fabs(QVector3D::dotProduct(m_pullNormal, m_wp.normal())) > 0.999f) at = snapToWorkPlane(at);
                m_pullLast = at;
                setPullVirtual(at);
                m_overlay->update();
            }
        }
        mouse.pos = event->pos();
        return;
    }
    if (m_faceShape)
    {
        if (!m_faceActive && (event->pos() - m_facePressPos).manhattanLength() >= 4 && m_leftDown)
        {
            // (a press on a face that moves: all its points are dragged, in its plane)
            m_faceActive = true;
            setCursor(Qt::SizeAllCursor);
            emit(dragStart());
        }
        if (m_faceActive)
        {
            QVector3D at;
            if (ringPoint(event->pos(), m_faceStart, m_faceNormal, at))
            {
                QVector3D d = at - m_faceStart;
                if (!m_faceWallDir.isNull()) d = m_faceWallDir * QVector3D::dotProduct(d, m_faceWallDir);       // (a wall moves along what it faces, and nowhere else)
                if (m_wp.open && m_wp.exists && m_faceHasRef && std::fabs(QVector3D::dotProduct(m_faceNormal, m_wp.normal())) > 0.999f)
                {
                    d = snapToWorkPlane(m_faceRef + d, nullptr, nullptr, false) - m_faceRef;        // (the grid only: its own points are what is moving)
                    d -= m_wp.normal() * QVector3D::dotProduct(d, m_wp.normal());                    // (in the plane: the noise of the ray is not a move out of it)
                }
                std::map<libfive::Tree::Id, float> numbers;
                for (const FaceVar& v : m_faceVars) numbers[v.id] = v.value0 + d[v.axis];
                if (!numbers.empty()) applyGizmoNumbers(numbers);
            }
        }
        mouse.pos = event->pos();
        return;
    }

    if (m_iconPress)
    {
        // (an icon that is held: a click that moves is a drag of it)
        if (!m_iconDrag && (event->pos() - m_iconPressPos).manhattanLength() >= 4) { m_iconDrag = true; setCursor(Qt::SizeAllCursor); }
        if (m_iconDrag) dragFieldIcon(event->pos());
        mouse.pos = event->pos();
        return;
    }
    if (m_fieldGripDrag)
    {
        // (a move arrow of an open viewer that is held)
        dragFieldGrip(event->pos());
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
    if (gizmo_drag)
    {
        // (the dot of a point that was pressed: a click until the mouse has moved a few pixels -- nothing moves for it)
        if (m_dotPressLine >= 0 && !m_dotMoved)
        {
            if ((event->pos() - m_dotPressPos).manhattanLength() < 4)
            {
                mouse.pos = event->pos();
                return;
            }
            m_dotMoved = true;
        }
        dragGizmo(event->pos());
        mouse.pos = event->pos();
        return;
    }
    if (mouse.state == mouse.DRAG_RECT && m_navLeft != 0 && !rect_add && (event->pos() - rect_start).manhattanLength() >= 4)
    {
        // A plain left drag is a gesture of the view when the user gave it one (Settings ▸ Navigation): on empty space the rectangle becomes the turn, the pan or the zoom once the mouse has
        // moved -- a click that does not move still selects, and Ctrl keeps the rectangle
        if (m_navLeft == 1 && m_wp.open)
        {
            setWorkPlaneOpen(false);
            emit(workPlaneTurnedAway());
        }
        mouse.state = m_navLeft == 1 ? mouse.DRAG_ROT : m_navLeft == 2 ? mouse.DRAG_PAN : mouse.DRAG_ZOOM;
        press_pos = QPoint(-100000, -100000);       // (not a selection click)
        press_target = nullptr;
        m_overlay->update();
        mouse.pos = rect_start;
    }
    if (mouse.state == mouse.DRAG_RECT)
    {
        // (the selection rectangle grows with the mouse)
        rect_now = event->pos();
        m_overlay->update();
        mouse.pos = event->pos();
        return;
    }
    if (m_wp.open) updateWorkPlaneCursor(event->pos());
    if (mouse.state == mouse.RELEASED)
    {
        // (the icon of a field and the move arrows of an open viewer are on top of everything: nothing behind them is hovered)
        const FieldGrip fgrip = fieldGripAt(event->pos());
        FieldViewer* icon = fgrip.v ? nullptr : fieldIconAt(event->pos());
        const bool onField = fgrip.v || icon;
        const GizmoGrip grip = onField ? GizmoGrip() : gizmoAt(event->pos());
        const bool gripChanged = !(grip == gizmo_hover);
        if (gripChanged)
        {
            gizmo_hover = grip;
            m_overlay->update();
        }
        // (the dot of a point lights up under the mouse: it can be clicked)
        Shape* dot = (grip.kind < 0 && !onField) ? pointDotAt(event->pos()) : nullptr;
        // (and the outline of a profile or a curve: a press on it puts a point there)
        Shape* lineShape = nullptr;
        QVector3D lineHit;
        int linePiece = 0;
        if (grip.kind < 0 && !onField && !dot && !outlineAt(event->pos(), lineShape, lineHit, linePiece)) lineShape = nullptr;
        const bool hover = sectionHandleHit(event->pos());
        // (the arc of a round corner is a handle for its radius, not an edge to pull a point out of: the hand that moves it says so)
        const bool arcHover = lineShape && lineShape->isFace() && !lineShape->isLocked() && lineShape->arcControlOfPiece(linePiece) >= 0;
        if (lineShape != m_outlineHover || arcHover != m_outlineArcHover)
        {
            m_outlineHover = lineShape;
            m_outlineArcHover = arcHover;
            setCursor(arcHover ? Qt::SizeAllCursor : lineShape ? Qt::PointingHandCursor : Qt::ArrowCursor);
            m_overlay->update();
        }
        if (gripChanged || dot != m_dotHover || hover != section_hover || icon != m_iconHover || !(fgrip == m_fieldGrip))
        {
            m_dotHover = dot;
            section_hover = hover;
            m_iconHover = icon;
            m_fieldGrip = fgrip;
            // (drawing -- the work plane's line, spline or point -- every click puts a point down, so the crosshair stays whatever is under it)
            const bool drawing = m_wp.open && m_wp.exists && m_wp.mode != 0;
            setCursor(hover ? Qt::SizeAllCursor : drawing ? Qt::CrossCursor
                      : (dot || icon || fgrip.v || grip.kind >= 0 || m_outlineHover) ? Qt::PointingHandCursor : Qt::ArrowCursor);
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
    else if (mouse.state == mouse.DRAG_ZOOM)
    {
        // (up zooms in, as the wheel's forward does)
        camera.zoomIncremental(float(mouse.pos.y() - event->pos().y()) * 4.0f, rect().center());
        update();
        pick_timer.start();
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

        static const bool timing = std::getenv("FIELDES_TIMING") != nullptr;
        QElapsedTimer clock;
        if (timing) clock.start();
        // (the numbers of a gizmo on the same part stay where they are)
        auto sol = libfive::Solver::findRoot(
                *drag_eval.first, drag_eval.second,
                drag_target->getVars(),
                {cursor_pos.x(), cursor_pos.y(), cursor_pos.z()},
                drag_target->pullMask(), 25000, drag_weights);
        const qint64 tSolved = timing ? clock.nsecsElapsed() : 0;
        emit(varsDragged(QMap<libfive::Tree::Id, float>(sol.second)));
        const qint64 tWritten = timing ? clock.nsecsElapsed() : 0;

        drag_valid = fabs(sol.first) < 1e-6;
        // The region holds what is pulled -- now: the surface is meshed again from its new numbers over the region, and out of it is cut off until the script has run.  The region
        // it has grown to goes into the script with the numbers when the mouse is let go (regionGrown)
        if (drag_valid) growRegionForSurfaceDrag(cursor_pos, sol.second);
        const qint64 tGrown = timing ? clock.nsecsElapsed() : 0;
        bool changed = false;
        for (auto& s : shapes)
        {
            changed |= s->updateVars(sol.second);
        }
        if (timing)
        {
            std::fprintf(stderr, "[at %lld ms] [drag] surface moved: %.1f ms in all (solving %.1f, writing the numbers %.1f, region %.1f, shapes given them %.1f); target (%.3f, %.3f, %.3f), residual %.2g, numbers:",
                         static_cast<long long>(QDateTime::currentMSecsSinceEpoch() % 100000000), double(clock.nsecsElapsed()) / 1e6,
                         double(tSolved) / 1e6, double(tWritten - tSolved) / 1e6, double(tGrown - tWritten) / 1e6, double(clock.nsecsElapsed() - tGrown) / 1e6,
                         double(cursor_pos.x()), double(cursor_pos.y()), double(cursor_pos.z()), double(sol.first));
            for (const auto& kv : sol.second) std::fprintf(stderr, " %.4g", double(kv.second));
            std::fprintf(stderr, "\n");
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
        if (gizmo_hover.kind >= 0)
        {
            setCursor(gizmo_hover.kind == 3 || gizmo_hover.kind == 4 ? Qt::SizeAllCursor : Qt::PointingHandCursor);
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

// The row a click on a dot selects: the point's own -- and for the dot of an image (the point of a placed copy, of an image in a symmetry) the row of the point it is the image of,
// which is what a drag and a double click on it reach as well: so that the point can be selected, deleted and edited like any other, whatever copy it is clicked on
static int dotLine(const Shape* shape)
{
    return (shape->gizmo().dotOnly && shape->followedShape()) ? shape->followedShape()->sourceLine() : shape->sourceLine();
}

// Whether this is the dot of a copy of a point that is shown (its point is shown, and it is what it is the copy of)
static bool isPointCopy(const Shape* shape)
{
    return shape->gizmo().dotOnly && shape->followedShape();
}

// Which of the copies of its point this dot is, in the order they are drawn
int View::copyIndexOf(const Shape* dot) const
{
    const int line = dotLine(dot);
    int n = 0;
    for (const Shape* s : shapes)
    {
        if (!isPointCopy(s) || dotLine(s) != line) continue;
        if (s == dot) return n;
        ++n;
    }
    return 0;
}

// The copy of a point that was pressed shows the point's gizmo -- its arrows, its ring, the ring that sizes its influence -- at the copy, while the point is selected: the others show
// their dots only (every copy of it with a gizmo would be a gizmo for each leg of a symmetry).  What is dragged on it changes the numbers of the original, taken back through the
// map of the copy (Shape::gizmoMapped)
void View::applyActiveCopy()
{
    int n = 0;
    for (Shape* s : shapes)
    {
        bool on = false;
        if (m_copyLine >= 0 && isPointCopy(s) && dotLine(s) == m_copyLine)
        {
            on = n == m_copyNth && s->isSelected();
            ++n;
        }
        s->setActiveCopy(on);
    }
}

void View::mousePressEvent(QMouseEvent* event)
{
    QOpenGLWidget::mousePressEvent(event);
    event->accept();
    m_lastPos = event->pos();
    if (event->button() == Qt::LeftButton) m_leftDown = true;
    // A click in the viewport puts the keyboard in the viewport (from the editor too): the keys that work on the
    // selection (E, R, G, H, I) are not typed into the script
    if (!hasFocus()) setFocus(Qt::MouseFocusReason);

    // The work plane view, in one of the three kinds of drawing: every click puts down a point (of a line, of a spline, or a point) -- a click on a point that is
    // there attaches to it, it does not select it; the first point of a line closes it.  Mode 0 (a right click) leaves clicks to what they do everywhere: things are selected, moved,
    // turned and scaled (Shift and Ctrl keep their meanings)
    if (event->button() == Qt::LeftButton && m_wp.open && m_wp.exists && m_wp.mode != 0 && !(event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier)))
    {
        press_pos = QPoint(-100000, -100000);       // (not a selection click)
        press_target = nullptr;
        placeAt(event->pos());
        return;
    }

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
        if (event->button() == Qt::RightButton && !m_errorText.isEmpty() && m_errorRect.contains(event->pos()))
        {
            // The error banner is painted, so its text cannot be selected: a right-click on it offers to copy it (the message of the interpreter, to look it up or to send it)
            QMenu menu(this);
            menu.addAction(T("Copy the error"), this, [this] { QGuiApplication::clipboard()->setText(m_errorText); });
            menu.exec(event->globalPos());
            press_pos = QPoint(-100000, -100000);
            press_target = nullptr;
            return;
        }
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
            // The icon of a field -- a click on it selects the field (its viewer opens), a drag takes it, and the viewer with it, elsewhere -- and
            // the move arrows of an open viewer are on top of everything: of a point's dot and of a gizmo too (the icon is moved aside from a dot)
            if (FieldViewer* icon = fieldIconAt(event->pos()))
            {
                const auto layout = iconLayout();
                m_iconOffset = layout.value(icon->key) - toScreen(fieldPlace(*icon));
                m_iconPress = icon;
                m_iconPressPos = event->pos();
                m_iconDrag = false;
                m_iconCtrl = bool(event->modifiers() & Qt::ControlModifier);
                m_iconGrab = fieldPlace(*icon) - rayHitFacing(event->pos() - m_iconOffset.toPoint(), fieldPlace(*icon));
                press_pos = QPoint(-100000, -100000);   // not a selection click
                press_target = nullptr;
                return;
            }
            const FieldGrip fgrip = fieldGripAt(event->pos());
            if (fgrip.v)
            {
                beginFieldGrip(fgrip, event->pos());
                press_pos = QPoint(-100000, -100000);   // not a selection click
                press_target = nullptr;
                if (m_fieldGripDrag) return;
            }
        }
        if (event->button() == Qt::LeftButton)
        {
            // A part's handle (a move arrow or a rotation ring)
            syncPicker();
            const GizmoGrip grip = gizmoAt(event->pos());
            if (grip.kind >= 0)
            {
                beginGizmoDrag(grip, event->pos());
                if (gizmo_drag)
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
        // The gestures the user chose (Settings ▸ Navigation): for each of turning, panning and zooming, a button and the keys held with it
        if (navigationPress(event)) return;
        if ((event->button() == Qt::LeftButton && (event->modifiers() & Qt::ShiftModifier)) ||
            event->button() == Qt::MiddleButton)
        {
            // Shift + left drag and the middle button's drag turn the view; such a press selects nothing -- and turning the view leaves the work plane view
            if (m_wp.open)
            {
                setWorkPlaneOpen(false);
                emit(workPlaneTurnedAway());        // (the grid and the drawing are gone without a word otherwise)
            }
            press_pos = QPoint(-100000, -100000);   // not a selection click
            press_target = nullptr;
            press_ctrl = false;
            drag_target = nullptr;
            mouse.state = mouse.DRAG_ROT;
            return;
        }
        if (event->button() == Qt::LeftButton)
        {
            // The dot of a point: it is not in the pick buffer (nothing of it is painted), and it wins over what is behind it
            if (Shape* dot = pointDotAt(event->pos()))
            {
                // A point is moved by pressing its dot and dragging -- a point of a profile in the plane of the profile, any other in the plane that faces the camera -- whether it is
                // selected or not and whatever gizmo is shown.  One that has its numbers already is moved at once (a click that does not move selects it); one that has none
                // is selected (which gives it the numbers it is moved by) and the drag begins as soon as they are there, if the button is still down.  Several selected points
                // are moved by the dot of their group
                // (the press on the dot of a COPY of a point remembers which copy: it is the one that shows the point's gizmo once the point is selected)
                if (!(event->modifiers() & Qt::ControlModifier))
                {
                    m_copyLine = isPointCopy(dot) ? dotLine(dot) : -1;
                    m_copyNth = isPointCopy(dot) ? copyIndexOf(dot) : 0;
                    applyActiveCopy();
                }
                const int selectedCount = selectedShapeCount();       // (the images of one row, and the dots drawn with them, are one selection)
                const bool direct = !(event->modifiers() & Qt::ControlModifier) && dot->sourceLine() >= 0 && !dot->isLocked() && !(dot->isSelected() && selectedCount > 1);
                // (a point has its numbers when its script has the gizmo's move numbers for it: whether it is selected -- which shows the gizmo -- does not matter)
                const bool numbers = dot->hasMoveVars();
                if (direct && numbers)
                {
                    press_pos = QPoint(-100000, -100000);   // (a click is made at the release)
                    press_target = nullptr;
                    drag_target = nullptr;
                    m_dotPressLine = dotLine(dot);
                    m_dotPressPos = event->pos();
                    m_dotMoved = false;
                    beginGizmoDrag(GizmoGrip{dot, 3, 0}, event->pos());
                    if (gizmo_drag) return;
                    m_dotPressLine = -1;
                }
                if (direct && !numbers)
                {
                    m_wait = WaitGrip{true, 3, 0, 0, QList<int>{}, true};
                    m_waitTimer.start();
                    setCursor(Qt::BusyCursor);
                    press_pos = QPoint(-100000, -100000);   // (the select is made here, not by the release)
                    press_target = nullptr;
                    emit(shapeClicked(dotLine(dot)));
                    return;
                }
                drag_target = nullptr;
                press_pos = event->pos();
                press_target = dot;
                press_ctrl = bool(event->modifiers() & Qt::ControlModifier);
                rect_start = rect_now = event->pos();
                rect_add = press_ctrl;
                mouse.state = mouse.DRAG_RECT;
                return;
            }
            syncPicker();
            auto picked = pickedNear(event->pos());
            // The outline of a profile, the line of a curve: a press on it pulls a point out of it (drawn at once from numbers, written into the script when
            // it is let go)
            if (!(event->modifiers() & Qt::ControlModifier))
            {
                Shape* lineShape = nullptr;
                QVector3D lineHit;
                int linePiece = 0;
                if (outlineAt(event->pos(), lineShape, lineHit, linePiece) && lineShape->sourceLine() >= 0 && !lineShape->isLocked())
                {
                    press_pos = QPoint(-100000, -100000);   // not a selection click
                    press_target = nullptr;
                    drag_target = nullptr;
                    // The arc of a round corner is the HANDLE of its radius: it is dragged to change it -- and never an edge to pull a point out of (that would break the corner).  When the
                    // radius is a plain number, the press makes it a number that can be dragged (`radius=var(5)`); the next press drags
                    if (lineShape->isFace())
                    {
                        const int arc = lineShape->arcControlOfPiece(linePiece);
                        if (arc >= 0)
                        {
                            Shape::FilletHandle handle;
                            if (lineShape->filletHandle(arc, handle)) beginFilletDrag(lineShape, handle, lineHit);
                            else if (lineShape->roundWithoutVar(arc)) emit(radiusVarRequested(lineShape->sourceLine(), arc));
                            return;
                        }
                    }
                    m_pullShape = lineShape;
                    m_pullHit = lineHit;
                    m_pullPiece = linePiece;
                    m_pullPressPos = event->pos();
                    m_pullActive = false;
                    // (an edge of a profile is pulled at once, so that a click puts a point there; the line of a curve waits for the mouse to move, so that
                    // a click selects the curve)
                    if (!lineShape->isCurve()) beginPull(event->pos());
                    return;
                }
                // The face of a profile: dragged, it takes all its points along in its plane; a click selects it
                // (the face of an image -- the profile a sweep's images bring along -- is not dragged: its points are the original's, and a click selects it)
                if (picked && int(picked) <= shapes.size() && shapes.at(picked - 1)->isFace() && !shapes.at(picked - 1)->isLocked() &&
                    !shapes.at(picked - 1)->isSymmetryImage() && shapes.at(picked - 1)->sourceLine() >= 0)
                {
                    Shape* face = shapes.at(picked - 1);
                    double frame[9];
                    if (face->planeNow(frame))
                    {
                        const QVector3D u{float(frame[3]), float(frame[4]), float(frame[5])}, w{float(frame[6]), float(frame[7]), float(frame[8])};
                        m_faceNormal = QVector3D::crossProduct(u, w).normalized();
                        m_faceStart = toModelPos(event->pos());
                        // (the point of it that is nearest the press: the one the grid holds while the face is dragged)
                        m_faceHasRef = false;
                        std::vector<libfive::pointshapes::Control> cs;
                        if (face->controlsNow(cs) && !cs.empty())
                        {
                            double best = 1e300;
                            for (const auto& c : cs)
                            {
                                const double d = (c.pos - Eigen::Vector3d(m_faceStart.x(), m_faceStart.y(), m_faceStart.z())).squaredNorm();
                                if (d < best) { best = d; m_faceRef = QVector3D(float(c.pos.x()), float(c.pos.y()), float(c.pos.z())); m_faceHasRef = true; }
                            }
                        }
                        m_faceVars.clear();
                        m_faceWallDir = QVector3D();                // (the face moves every point as the mouse does)
                        const auto& vs = face->getVars();
                        for (const auto& c : face->controlPoints())
                        {
                            for (int a = 0; a < 3; ++a)
                            {
                                const auto it = c.var[a] ? vs.find(c.var[a]) : vs.end();
                                if (it != vs.end()) m_faceVars.append({c.var[a], a, it->second});
                            }
                        }
                        // (the points that have no numbers to move them -- no gizmo yet -- stay: the face is only selected then)
                        if (!m_faceVars.isEmpty())
                        {
                            press_pos = QPoint(-100000, -100000);
                            press_target = nullptr;
                            drag_target = nullptr;
                            m_faceShape = face;
                            m_faceActive = false;
                            m_facePressPos = event->pos();
                            return;
                        }
                    }
                }
            }
            // A WALL of a prism -- the side of the extrusion of a profile of points: it is the side of the 2D shape, and it is pulled as the 2D shape's side is, by the two points of the outline it is
            // between, moved together in the plane of the profile (drawn at once from the numbers, written into the script when it is let go).  Its end faces are pulled by the handles (a, b)
            if (!(event->modifiers() & Qt::ControlModifier) && picked && int(picked) <= shapes.size() && selectedShapeCount() < 2)
            {
                Shape* prism = shapes.at(picked - 1);
                Shape::PrismWall wall;
                if (prism->hasPrism() && !prism->isLocked() && !prism->isSymmetryImage() && prism->sourceLine() >= 0 && prism->prismWallAt(toModelPos(event->pos()), wall))
                {
                    m_faceVars.clear();
                    const auto& vs = prism->getVars();
                    for (const auto& v : wall.vars)
                    {
                        const auto it = vs.find(v.first);
                        if (it != vs.end()) m_faceVars.append({v.first, v.second, it->second});
                    }
                    if (!m_faceVars.isEmpty())
                    {
                        press_pos = QPoint(-100000, -100000);
                        press_target = nullptr;
                        drag_target = nullptr;
                        m_faceNormal = wall.planeNormal;
                        m_faceWallDir = wall.direction;
                        m_faceStart = toModelPos(event->pos());
                        m_faceHasRef = false;
                        m_faceShape = prism;
                        m_faceActive = false;
                        m_facePressPos = event->pos();
                        return;
                    }
                }
            }
            // The work plane view: a 2D shape that lies in the plane (a circle, a rectangle, a shape from a surface) is moved about in it, in steps of the grid, by pressing it
            if (m_wp.open && m_wp.exists && m_wp.mode == 0 && picked && int(picked) <= shapes.size() && !(event->modifiers() & Qt::ControlModifier))
            {
                Shape* flat = shapes.at(picked - 1);
                double f[9];
                if (flat->isFlat() && !flat->isFace() && !flat->isLocked() && flat->sourceLine() >= 0 && flat->flatPlane(f))
                {
                    const QVector3D fn = QVector3D::crossProduct(QVector3D(float(f[3]), float(f[4]), float(f[5])), QVector3D(float(f[6]), float(f[7]), float(f[8]))).normalized();
                    if (std::fabs(QVector3D::dotProduct(fn, m_wp.normal())) > 0.999f && !pullableAt(flat, event->pos()))
                    {
                        press_pos = QPoint(-100000, -100000);
                        press_target = nullptr;
                        drag_target = nullptr;
                        if (flat->hasGizmo())
                        {
                            beginGizmoDrag(GizmoGrip{flat, 3, 0}, event->pos());
                            if (gizmo_drag) return;
                        }
                        else
                        {
                            // (it is selected, which gives it the numbers it is moved by, and the drag begins when they are there)
                            m_wait = WaitGrip{true, 3, 0, 0, QList<int>{}, true};
                            m_waitTimer.start();
                            setCursor(Qt::BusyCursor);
                            emit(shapeClicked(flat->sourceLine()));
                            return;
                        }
                    }
                }
            }
            drag_target = (picked && int(picked) <= shapes.size())
                ? shapes.at(picked - 1) : nullptr;
            // (a 2D shape is drawn over the body that was made of it, and a press inside it -- not at its edge, where it is pulled -- pulls nothing of the shape: the body that lies under it is what is
            // pressed.  The end face of an extrusion at its base is under the shape it was extruded from: pulled there, it changes the length of the extrusion from that end)
            if (drag_target && drag_target->is2D() && !pick_img_bodies.isNull() && !pullableAt(drag_target, event->pos()))
            {
                const auto under = pick_img_bodies.valid(event->pos()) ? (pick_img_bodies.pixel(event->pos()) & 0xFFFFFF) : 0;
                if (under && int(under) <= shapes.size() && !shapes.at(under - 1)->is2D())
                {
                    drag_target = shapes.at(under - 1);
                    picked = under;
                    std::swap(pick_img, pick_img_bodies);               // (the press is read at the depth of the body: the next picture of the picker puts them right)
                    std::swap(pick_depth, pick_depth_bodies);
                }
            }
            press_pos = event->pos();
            press_target = drag_target;
            // Ctrl + click on the LINE of a curve (a spline, a polyline) or the edge of a profile: a line has no pixel in the pick buffer, so the press found no model at all and
            // the click did nothing -- the line is what is toggled in the selection, as the click on a surface toggles its model (a plain click on it is handled above)
            if (event->modifiers() & Qt::ControlModifier)
            {
                Shape* lineShape = nullptr;
                QVector3D lineHit;
                int linePiece = 0;
                if (outlineAt(event->pos(), lineShape, lineHit, linePiece) && lineShape && lineShape->sourceLine() >= 0) press_target = lineShape;
            }
            // (Ctrl + click selects: it never pulls the surface it is on)
            press_ctrl = bool(event->modifiers() & Qt::ControlModifier);
            // (a part in the gizmo mode is moved by the gizmo and a locked one not at all: only its
            // own surfaces, made of numbers that can change, are pulled)
            // (and with several shapes selected nothing is pulled by its surface: they only have the gizmo)
            QVector3D flat_edge;
            if (picked && !press_ctrl && selectedShapeCount() < 2 && pullableAt(drag_target, event->pos(), &flat_edge))
            {
                static const bool timing = std::getenv("FIELDES_TIMING") != nullptr;
                QElapsedTimer clock;
                if (timing) clock.start();
                this->setCursor(Qt::ClosedHandCursor);
                emit(dragStart());
                const qint64 tStart = timing ? clock.nsecsElapsed() : 0;
                drag_target->setGrabbed(true);
                drag_valid = true;
                m_regionGrown = false;

                drag_start = drag_target->isFlat() ? flat_edge : toModelPos(event->pos());
                auto df = drag_target->dragFrom(drag_start);
                const qint64 tFrom = timing ? clock.nsecsElapsed() : 0;
                drag_eval.first.reset(df.first);
                drag_eval.second = df.second;
                drag_weights = drag_target->dragWeights();
                const qint64 tWeights = timing ? clock.nsecsElapsed() : 0;

                auto norm = drag_eval.first->deriv(
                        {drag_start.x(), drag_start.y(), drag_start.z()},
                        *df.second);
                drag_dir = {norm.x(), norm.y(), norm.z()};
                // A shape that has an axis (a threaded rod) is pulled along the radius when it is pressed on its side, whichever way the face under the hand looks: the flanks of a thread look up,
                // down and out, and a pull along each one's own normal would be a different pull on each.  The part of the normal that lies along the axis is not taken; an end (all of whose
                // normal is along the axis) keeps its own.  A flank is steep -- the normal of an ISO thread's flank is 63 degrees from the radius, only 0.45 of it across the axis -- so what
                // counts as a side is a normal with at least 0.2 of it across the axis (an end has none)
                if (drag_target->hasPullAxis())
                {
                    const QVector3D axis = drag_target->pullAxis().normalized();
                    const QVector3D radial = drag_dir - axis * QVector3D::dotProduct(drag_dir, axis);
                    if (radial.length() >= 0.2f * drag_dir.length()) drag_dir = radial;
                }

                mouse.state = mouse.DRAG_EVAL;
                if (timing)
                    std::fprintf(stderr, "[at %lld ms] [drag] surface pressed: %.1f ms in all (dragStart %.1f, field and evaluator %.1f, weights %.1f, direction %.1f); at (%.3f, %.3f, %.3f), direction (%.3f, %.3f, %.3f)\n",
                                 static_cast<long long>(QDateTime::currentMSecsSinceEpoch() % 100000000), double(clock.nsecsElapsed()) / 1e6,
                                 double(tStart) / 1e6, double(tFrom - tStart) / 1e6, double(tWeights - tFrom) / 1e6, double(clock.nsecsElapsed() - tWeights) / 1e6,
                                 double(drag_start.x()), double(drag_start.y()), double(drag_start.z()), double(drag_dir.x()), double(drag_dir.y()), double(drag_dir.z()));
            }
            else
            {
                // Nothing to pull: a drag draws the selection rectangle (with Ctrl held, the shapes inside join the
                // selection), and a click that does not move selects what is under the cursor, as it always did
                if (std::getenv("FIELDES_TIMING"))
                {
                    // (why a press on a surface did not take it: a face that does not move says so, instead of nothing)
                    std::fprintf(stderr, "[drag] nothing to pull at (%d, %d): picked %d (line %d), Ctrl %s, %d selected, editable %s, numbers to pull it by %s, %s\n", event->pos().x(), event->pos().y(),
                                 int(picked), drag_target ? drag_target->sourceLine() + 1 : -1, press_ctrl ? "yes" : "no", selectedShapeCount(),
                                 drag_target && drag_target->isEditable() ? "yes" : "no", drag_target && drag_target->nativeDragOk() ? "yes" : "no",
                                 drag_target && drag_target->isFlat() ? "a flat shape: only its edge is pulled" : "a solid");
                }
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

// The outline an outline stands for: its own line, or -- an image of another (a placed copy, an image in a symmetry) -- the line of the one it is an image of
static int editRootLine(const Shape* s)
{
    return s->editLine() >= 0 ? s->editLine() : s->sourceLine();
}

void View::mouseReleaseEvent(QMouseEvent* event)
{
    QOpenGLWidget::mouseReleaseEvent(event);
    event->accept();
    if (event->button() == Qt::LeftButton) m_leftDown = false;
    if (m_pullShape)
    {
        Shape* shape = m_pullShape;
        const bool active = m_pullActive;
        m_pullShape = nullptr;
        m_pullActive = false;
        unsetCursor();
        if (event->button() == Qt::LeftButton)
        {
            if (active)
            {
                // (the point stays drawn where it was let go until the script has it.)  Pressed on an image of an outline -- a placed copy, an image in a symmetry -- the point is put on the
                // outline it is an image of, where the press is on that: the place of the image taken back
                bool invertible = false;
                const QMatrix4x4 back = shape->editMap().inverted(&invertible);
                emit(outlineInsertRequested(editRootLine(shape), invertible ? back.map(m_pullHit) : m_pullHit, invertible ? back.map(m_pullLast) : m_pullLast));
            }
            else
            {
                emit(shapeClicked(shape->sourceLine()));        // (a click on the line of a curve: the curve is selected)
            }
        }
        return;
    }
    if (m_faceShape)
    {
        Shape* face = m_faceShape;
        const bool active = m_faceActive;
        m_faceShape = nullptr;
        m_faceActive = false;
        unsetCursor();
        if (event->button() == Qt::LeftButton)
        {
            if (active) emit(dragEnd());
            else emit(shapeClicked(face->sourceLine()));
        }
        return;
    }
    if (m_iconPress)
    {
        FieldViewer* held = m_iconPress;
        const bool dragged = m_iconDrag, ctrl = m_iconCtrl;
        m_iconPress = nullptr;
        m_iconDrag = false;
        setCursor(Qt::PointingHandCursor);
        if (!dragged) emit(fieldIconClicked(held->key, ctrl));
        m_overlay->update();
        return;
    }
    if (m_fieldGripDrag)
    {
        m_fieldGripDrag = false;
        m_fieldGripActive = FieldGrip();
        setCursor(Qt::ArrowCursor);
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
    if (gizmo_drag)
    {
        gizmo_drag = false;
        // (the press on the dot of a point that did not move was a click: it selects the point)
        const int dotClickLine = (m_dotPressLine >= 0 && !m_dotMoved) ? m_dotPressLine : -1;
        m_dotPressLine = -1;
        m_dotMoved = false;
        // (the shapes that were only drawn moved while it was dragged are made again at the numbers they have now: the one real render of the drag)
        for (Shape* s : shapes) s->endGizmoPreview();
        gizmo_hover = gizmoAt(event->pos());
        setCursor(gizmo_hover.kind >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        redrawPicker();
        // (the region the drag grew to goes into the script with the numbers of the drag: the run that this drag's end starts has both)
        if (m_regionGrown)
        {
            m_regionGrown = false;
            emit(regionGrown(m_grownMin, m_grownMax));
        }
        emit(dragEnd());
        if (dotClickLine >= 0) emit(shapeClicked(dotClickLine));
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
                const QStringList fields = fieldKeysInside(r);
                if (!lines.isEmpty()) emit(shapesRectSelected(lines, rect_add));
                if (!fields.isEmpty()) emit(fieldsRectSelected(fields, rect_add || !lines.isEmpty()));
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
            ? dotLine(press_target) : -1;
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

    // While a kind of drawing is on in the work plane view, a right click (not a drag: that pans) stops the drawing -- clicks select, move, turn and scale as usual again
    // -- instead of opening a menu; E brings the kind of drawing back
    if (event->button() == Qt::RightButton && (right_press_target || right_press_empty) && (event->pos() - right_press_pos).manhattanLength() < 4 &&
        m_wp.open && m_wp.exists && m_wp.mode != 0)
    {
        right_press_target = nullptr;
        right_press_empty = false;
        mouse.state = mouse.RELEASED;
        setWorkPlaneMode(0);
        return;
    }
    if (event->button() == Qt::RightButton && right_press_target &&
        (event->pos() - right_press_pos).manhattanLength() < 4 && shapes.contains(right_press_target))
    {
        Shape* target = right_press_target;
        right_press_target = nullptr;
        right_press_empty = false;
        mouse.state = mouse.RELEASED;
        const int line = target->sourceLine();
        if (line >= 0 && !m_appMode) showSurfaceMenu(event->globalPos(), line, right_press_point,
                                                     scaleAt(event->pos(), right_press_point));
        return;
    }
    if (event->button() == Qt::RightButton && right_press_empty &&
        (event->pos() - right_press_pos).manhattanLength() < 4)
    {
        right_press_empty = false;
        mouse.state = mouse.RELEASED;
        if (!m_appMode) showEmptyMenu(event->globalPos(), event->pos());
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
            // (the region a pulled surface grew to goes into the script with the numbers of the drag, as a gizmo's does: the run that this drag's end starts has both)
            if (m_regionGrown)
            {
                m_regionGrown = false;
                emit(regionGrown(m_grownMin, m_grownMax));
            }
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

bool View::featureAt(QPoint pos, const QString& kind)
{
    syncPicker();
    const auto picked = pick_img.valid(pos) ? (pick_img.pixel(pos) & 0xFFFFFF) : 0;
    if (!picked || int(picked) > shapes.size()) return false;
    Shape* target = shapes.at(picked - 1);
    if (target->sourceLine() < 0) return false;
    const QVector3D at = toModelPos(pos);
    const QJsonObject found = m_scene ? m_scene->featuresNear(target->sourceLine(), at, 0.3 * scaleAt(pos, at)) : QJsonObject();
    if (kind == "list")
    {
        std::cerr << "automation: features at " << pos.x() << " " << pos.y() << ": "
                  << QJsonDocument(found).toJson(QJsonDocument::Compact).toStdString() << std::endl;
        return true;
    }
    // (as the menu does: what is written is the place that was found, not the click)
    const QJsonValue where = kind == "corner" ? found["corner"] : found["edge"].toObject()["point"];
    if (!where.isArray()) return false;
    const QJsonArray a = where.toArray();
    emit(featureRequested(target->sourceLine(), QVector3D(float(a.at(0).toDouble()), float(a.at(1).toDouble()), float(a.at(2).toDouble())), kind));
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

// ---------------------------------------------------------------------------------------------------------------------------------
// The work plane (see workplane.hpp)

float View::roundStep(double want)
{
    if (!(want > 0)) return 1.0f;
    const double e = std::floor(std::log10(want)), f = want / std::pow(10.0, e);
    double best = 1.0, bestDistance = 1e300;
    for (const double c : {1.0, 2.0, 5.0, 10.0})
    {
        const double distance = std::fabs(std::log(f / c));
        if (distance < bestDistance) { bestDistance = distance; best = c; }
    }
    return float(best * std::pow(10.0, e));
}

float View::workPlaneStep() const
{
    if (m_wp.spacing > 0) return m_wp.spacing;
    // from how large the picture is: a line about every 25 pixels, at a round number
    const QVector3D at = m_wp.exists ? m_wp.origin() : QVector3D(0, 0, 0);
    return roundStep(scaleAt(rect().center(), at) / 4.0);
}

void View::planeOfView(QVector3D& origin, QVector3D& u, QVector3D& v) const
{
    // The plane through the origin that is parallel to the screen: its axes are the way the screen's right and up point in the model
    const QPoint c = rect().center();
    const float z = camera.M().map(QVector3D(0, 0, 0)).z();
    const QVector3D p0 = toModelPos(c, z);
    u = (toModelPos(c + QPoint(100, 0), z) - p0).normalized();
    v = toModelPos(c - QPoint(0, 100), z) - p0;
    v = (v - u * QVector3D::dotProduct(u, v)).normalized();
    origin = QVector3D(0, 0, 0);
}

bool View::planeOfSelection(QVector3D& origin, QVector3D& u, QVector3D& v) const
{
    // A 2D shape that is selected: its plane, where it is
    double f[9];
    for (const Shape* s : shapes)
    {
        if (!s->isSelected() || !s->flatPlane(f)) continue;
        origin = QVector3D(float(f[0]), float(f[1]), float(f[2]));
        u = QVector3D(float(f[3]), float(f[4]), float(f[5])).normalized();
        v = QVector3D(float(f[6]), float(f[7]), float(f[8])).normalized();
        return true;
    }
    // A surface that is selected (a patch on the face of a body): the plane that it covers the most area of, as CAD does it -- a flat face gives its own plane, a bent one the plane most of
    // it faces -- turned to face the viewer (what is outside the body is what the viewer sees of a face, and the plane's normal is the way to build on)
    for (const Shape* s : shapes)
    {
        if (!s->isSelected() || !s->isSurfacePatch() || !s->patchPlane(f)) continue;
        origin = QVector3D(float(f[0]), float(f[1]), float(f[2]));
        u = QVector3D(float(f[3]), float(f[4]), float(f[5])).normalized();
        v = QVector3D(float(f[6]), float(f[7]), float(f[8])).normalized();
        QVector3D o, viewU, viewV;
        planeOfView(o, viewU, viewV);
        if (QVector3D::dotProduct(QVector3D::crossProduct(u, v), QVector3D::crossProduct(viewU, viewV)) < 0) std::swap(u, v);     // (swapped, u x v turns round)
        return true;
    }
    // Three points or more: the plane a profile of them would lie in (through the first three that are not on a line)
    std::vector<Eigen::Vector3d> points;
    for (const Shape* s : shapes)
    {
        QVector3D p;
        if (s->isSelected() && s->isPoint() && s->dotPosition(p)) points.emplace_back(p.x(), p.y(), p.z());
    }
    double g[9];
    if (points.size() >= 3 && libfive::pointshapes::planeOf(points, g))
    {
        origin = QVector3D(float(g[0]), float(g[1]), float(g[2]));
        u = QVector3D(float(g[3]), float(g[4]), float(g[5])).normalized();
        v = QVector3D(float(g[6]), float(g[7]), float(g[8])).normalized();
        return true;
    }
    return false;
}

bool View::navigationPress(QMouseEvent* event)
{
    const int mods = int(event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier));
    for (int action = 0; action < 3; ++action)
    {
        const NavBinding& b = m_navBind[action];
        if (b.button == 0 || b.button != int(event->button()) || b.modifiers != mods) continue;
        if (action == 0 && m_wp.open)
        {
            setWorkPlaneOpen(false);
            emit(workPlaneTurnedAway());                // (the grid and the drawing are gone without a word otherwise)
        }
        press_pos = QPoint(-100000, -100000);           // not a selection click
        press_target = nullptr;
        press_ctrl = false;
        drag_target = nullptr;
        mouse.state = action == 0 ? mouse.DRAG_ROT : action == 1 ? mouse.DRAG_PAN : mouse.DRAG_ZOOM;
        return true;
    }
    return false;
}

QList<QPair<QString, QString>> View::navigationGestures()
{
    return {
        {"none", T("None (what everyone has)")}, {"left", T("Left button")}, {"shift+left", T("Shift + left button")}, {"ctrl+left", T("Ctrl + left button")},
        {"alt+left", T("Alt + left button")}, {"middle", T("Middle button")}, {"shift+middle", T("Shift + middle button")}, {"ctrl+middle", T("Ctrl + middle button")},
        {"alt+middle", T("Alt + middle button")}, {"shift+right", T("Shift + right button")}, {"ctrl+right", T("Ctrl + right button")}, {"alt+right", T("Alt + right button")}};
}

void View::setNavigation(const QString& rotate, const QString& pan, const QString& zoom)
{
    const QString keys[3] = {rotate, pan, zoom};
    m_navLeft = 0;
    for (int i = 0; i < 3; ++i)
    {
        m_navKey[i] = keys[i].isEmpty() ? QString("none") : keys[i].toLower();
        m_navBind[i] = NavBinding();
        if (m_navKey[i] == "none") continue;
        QStringList parts = m_navKey[i].split('+');
        const QString button = parts.takeLast();
        NavBinding b;
        b.button = button == "left" ? int(Qt::LeftButton) : button == "middle" ? int(Qt::MiddleButton) : button == "right" ? int(Qt::RightButton) : 0;
        for (const QString& p : parts)
            b.modifiers |= p == "shift" ? int(Qt::ShiftModifier) : p == "ctrl" ? int(Qt::ControlModifier) : p == "alt" ? int(Qt::AltModifier) : 0;
        if (b.button == int(Qt::LeftButton) && b.modifiers == 0)
        {
            m_navLeft = i + 1;                          // (a plain left drag: it is a click first, see mouseMoveEvent)
            continue;
        }
        if (b.button == int(Qt::RightButton) && b.modifiers == 0) continue;     // (a right click opens the menu: it needs a key beside it to be a gesture)
        m_navBind[i] = b;
    }
}

void View::setNavigationPreset(int preset)
{
    m_navPreset = preset;
    if (preset == NAV_BLENDER) setNavigation("middle", "shift+middle", "ctrl+middle");
    else if (preset == NAV_TOUCHPAD) setNavigation("none", "alt+left", "none");
    else if (preset == NAV_LEFT_TURN) setNavigation("left", "alt+left", "none");
    else if (preset == NAV_DEFAULT) setNavigation("none", "none", "none");
}

void View::makeWorkPlane()
{
    QVector3D o, u, v;
    if (!planeOfSelection(o, u, v)) planeOfView(o, u, v);
    m_wp.exists = true;
    m_wp.base = o;
    m_wp.offset = 0;
    m_wp.u = u;
    m_wp.v = v;
}

void View::announceWorkPlane()
{
    // Drawing shows the crosshair; not drawing (the select mode, or the view closed) the usual pointer, and nothing of the cursor on the plane
    const bool drawing = m_wp.open && m_wp.exists && m_wp.mode != 0;
    if (!drawing && m_wpCursorValid)
    {
        m_wpCursorValid = false;
        emit(workPlaneReadout(QString()));
    }
    if (mouse.state == mouse.RELEASED && !gizmo_drag) setCursor(drawing ? Qt::CrossCursor : Qt::ArrowCursor);
    emit(workPlaneStateChanged(m_wp, workPlaneStep()));
    emit(workPlaneChanged(m_wp.open && m_wp.exists, m_wp.origin(), m_wp.u, m_wp.v, m_wp.snap ? workPlaneStep() : 0.0f));
    m_overlay->update();
}

void View::setWorkPlaneOpen(bool open)
{
    if (open && !m_wp.exists)
    {
        QVector3D o, u, v;
        const bool fromSelection = planeOfSelection(o, u, v);
        makeWorkPlane();
        if (fromSelection) faceWorkPlane();                 // (a plane that is a surface's, a 2D shape's, the one of some points: the view turns to look at it)
    }
    if (!open) m_wpPlaced.clear();
    // Opening it starts in Line: a click draws at once -- a view that opens and does nothing leaves the first thing to do a guess (a right click stops drawing)
    if (open && !m_wp.open) { m_wp.mode = 1; m_wpLastMode = 1; }
    m_wp.open = open;
    m_wpCursorValid = false;
    if (!open) emit(workPlaneReadout(QString()));
    announceWorkPlane();
}

bool View::exitMode()
{
    if (!m_wpPlaced.isEmpty())
    {
        cancelPlacement();
        return true;
    }
    if (m_wp.open && m_wp.exists && m_wp.mode != 0)
    {
        setWorkPlaneMode(0);
        return true;
    }
    if (m_wp.open)
    {
        setWorkPlaneOpen(false);
        return true;
    }
    return false;
}

void View::newWorkPlane()
{
    m_wpPlaced.clear();
    QVector3D o, u, v;
    const bool fromSelection = planeOfSelection(o, u, v);
    makeWorkPlane();
    if (!m_wp.open) { m_wp.mode = 1; m_wpLastMode = 1; }                  // (opening it starts in Line, as setWorkPlaneOpen does)
    m_wp.open = true;
    // A plane made on a surface (or on a 2D shape, or on some points) is a plane to draw on: the view turns to look at it, as W does.  One made from the view is where the view already is
    if (fromSelection) faceWorkPlane();
    announceWorkPlane();
}

void View::workPlaneFromView()
{
    m_wpPlaced.clear();
    QVector3D o, u, v;
    planeOfView(o, u, v);
    m_wp.exists = true;
    m_wp.base = o;
    m_wp.offset = 0;
    m_wp.u = u;
    m_wp.v = v;
    announceWorkPlane();
}

void View::faceWorkPlane()
{
    if (!m_wp.exists) return;
    camera.lookFrom(m_wp.normal());
}

void View::setWorkPlaneOptions(bool grid, bool snap, bool snapPoints, float spacing)
{
    m_wp.grid = grid;
    m_wp.snap = snap;
    m_wp.snapPoints = snapPoints;
    m_wp.spacing = spacing > 0 ? spacing : 0;
    announceWorkPlane();
}

void View::setWorkPlaneOffset(float offset)
{
    m_wp.offset = offset;
    announceWorkPlane();
}

QVector3D View::snapToWorkPlane(const QVector3D& p, const Shape* exclude, bool* snapped, bool toPoints) const
{
    if (snapped) *snapped = false;
    if (!m_wp.open || !m_wp.exists) return p;
    if (toPoints && m_wp.snapPoints)
    {
        // The point that is there: its dot, when it is within a few pixels
        const QPointF at = toScreen(p);
        double best = 10.0 * 10.0;
        bool found = false;
        QVector3D where;
        for (const Shape* s : shapes)
        {
            QVector3D q;
            if (s == exclude || !s->isPoint() || !s->dotPosition(q)) continue;
            const QPointF d = toScreen(q) - at;
            const double distance = d.x() * d.x() + d.y() * d.y();
            if (distance < best) { best = distance; where = q; found = true; }
        }
        if (found)
        {
            if (snapped) *snapped = true;
            return where;
        }
    }
    if (toPoints && m_wp.snapPoints && symmetric())
    {
        // The planes of the symmetry, as they cut the plane the point is in (parallel to the work plane): a point near a line of them goes onto it -- along the line, to the
        // grid's steps from the centre when the plane snaps -- so that what is drawn on both sides of a plane meets on it
        const QVector3D wn = m_wp.normal();
        const float h = QVector3D::dotProduct(p - m_sym.centre, wn);                       // (a point that is off the plane stays as far off it)
        const QVector3D cHere = m_sym.centre + wn * h;                                    // (the centre's place in the plane the point is in)
        const QPointF at = toScreen(p);
        double best = 10.0;
        bool found = false;
        QVector3D where;
        for (const QVector3D& m : m_sym.planeNormals())
        {
            // (the line the plane makes in that plane: the points x with (x - centre) . m = 0 -- its normal in the plane is m without its part along the plane's normal)
            QVector3D inPlane = m - wn * QVector3D::dotProduct(m, wn);
            if (inPlane.length() < 1e-4f) continue;                                       // (parallel to the work plane: no line)
            const float scale = inPlane.length();
            inPlane /= scale;
            const QVector3D d = QVector3D::crossProduct(wn, inPlane);                      // (the line's direction)
            const float off = QVector3D::dotProduct(p - m_sym.centre, m) / scale;          // (how far the point is from the line, in the plane)
            const QVector3D foot = p - inPlane * off;
            const QPointF there = toScreen(foot);
            const double distance = std::hypot(there.x() - at.x(), there.y() - at.y());
            if (distance >= best) continue;
            best = distance;
            found = true;
            // (the point of the line nearest the centre is where the steps are counted from)
            const float lineOff = QVector3D::dotProduct(cHere - m_sym.centre, m) / scale;
            const QVector3D origin = cHere - inPlane * lineOff;
            float along = QVector3D::dotProduct(foot - origin, d);
            if (m_wp.snap)
            {
                const float step = workPlaneStep();
                along = std::round(along / step) * step;
            }
            where = origin + d * along;
        }
        if (found)
        {
            if (snapped) *snapped = true;
            return where;
        }
    }
    if (m_wp.snap)
    {
        const QVector3D o = m_wp.origin(), n = m_wp.normal();
        const float step = workPlaneStep();
        const QVector3D d = p - o;
        const float s = std::round(QVector3D::dotProduct(d, m_wp.u) / step) * step;
        const float t = std::round(QVector3D::dotProduct(d, m_wp.v) / step) * step;
        if (snapped) *snapped = true;
        return o + m_wp.u * s + m_wp.v * t + n * QVector3D::dotProduct(d, n);
    }
    return p;
}

void View::placementAt(QPoint pos, QVector3D& point, double& scale) const
{
    if (m_wp.open && m_wp.exists)
    {
        // On the work plane, snapped: whatever is made goes where the plane's grid says
        QVector3D hit;
        point = ringPoint(pos, m_wp.origin(), m_wp.normal(), hit) ? snapToWorkPlane(hit) : placeOnRay(pos);
        scale = scaleAt(pos, point);
        return;
    }
    point = placeOnRay(pos);
    scale = scaleAt(pos, point);
    // Nothing outside the render region is drawn: a primitive whose place is outside it goes to the nearest place
    // that is inside (half its size in from the edge), so it is always seen
    const QVector3D lo = settings.min, hi = settings.max;
    auto inside = [&](float v, float a, float b) {
        const float m = float(std::min(0.5 * scale, 0.5 * double(b - a)));
        return std::max(a + m, std::min(b - m, v));
    };
    if (hi.x() > lo.x() && hi.y() > lo.y() && hi.z() > lo.z())
        point = QVector3D(inside(point.x(), lo.x(), hi.x()), inside(point.y(), lo.y(), hi.y()), inside(point.z(), lo.z(), hi.z()));
}

void View::setWorkPlaneMode(int mode)
{
    m_wpPlaced.clear();
    m_wp.mode = std::max(0, std::min(3, mode));
    // (the kind of drawing that was on last: E brings it back after the drawing has been stopped)
    if (m_wp.mode != 0) m_wpLastMode = m_wp.mode;
    announceWorkPlane();
}

void View::cycleWorkPlaneMode()
{
    // Not drawing: E goes back to the kind of drawing there was before it was stopped (the line, when there was none); drawing: round the three kinds of what is put down
    if (m_wp.mode == 0) setWorkPlaneMode(m_wpLastMode);
    else setWorkPlaneMode(m_wp.mode % 3 + 1);
}

void View::setWorkPlaneFrame(const QVector3D& origin, const QVector3D& u, const QVector3D& v)
{
    m_wpPlaced.clear();
    m_wp.exists = true;
    m_wp.base = origin;
    m_wp.offset = 0;
    m_wp.u = u.normalized();
    m_wp.v = (v - m_wp.u * QVector3D::dotProduct(m_wp.u, v)).normalized();
    announceWorkPlane();
}

static void joinMeeting(QList<QList<QVector3D>>& strokes, QList<QList<QVector3D>>& closed, float tol);

void View::placeAt(QPoint pos)
{
    if (!m_wp.open || !m_wp.exists || m_wp.mode == 0) return;
    QVector3D hit;
    if (!ringPoint(pos, m_wp.origin(), m_wp.normal(), hit)) return;
    const QVector3D p = snapToWorkPlane(hit);
    // (a first point that is only being put down is not in the script yet, so it is not a point to snap to: it is where a click closes the outline.  The first point
    // was snapped, so a click on it lands on the same place -- near it on the screen, or the same spot once snapped -- as the hover target (the cursor, snapped) shows)
    const bool nearFirst = m_wp.mode != 3 && m_wpPlaced.size() >= 3 &&
                           ((toScreen(m_wpPlaced.first()) - QPointF(pos)).manhattanLength() < 12 || (m_wpPlaced.first() - p).length() < 1e-4f * std::max(1.0f, m_wpPlaced.first().length()));
    if (nearFirst)
    {
        finishPlacement(true);
        return;
    }
    if (m_wp.mode == 3)
    {
        // (the point; with the symmetry on its images are made with it, by the scene)
        emit(createRequested("primitive", "point", p, scaleAt(pos, p), -1, -1));
        return;
    }
    if (!m_wpPlaced.isEmpty() && (m_wpPlaced.last() - p).length() < 1e-5f) return;      // (the second click of a double click, or the same place again)
    m_wpPlaced << p;
    m_overlay->update();
    // A stroke that comes back to a line of the symmetry, from a first point that was on one, is closed through it: with its images it is a closed outline, and it is made at
    // once -- as it is when the first point is clicked again -- not when the stroke is ended by hand (half a circle from one point of the line to another)
    if (m_wp.mode != 3 && symmetric() && m_wpPlaced.size() >= 3 && onSymmetryPlane(m_wpPlaced.first()) && onSymmetryPlane(m_wpPlaced.last()))
    {
        QList<QList<QVector3D>> strokes = symmetryStrokes(m_wpPlaced);
        QList<QList<QVector3D>> loops;
        joinMeeting(strokes, loops, std::max(1e-4f, 1e-3f * workPlaneStep()));
        if (!loops.isEmpty()) finishPlacement(false);
    }
}

// Strokes that meet at their ends -- half a shape drawn on one side of the symmetry line and its image on the other, which meet on the line -- are one outline: they are joined
// end to end, and one that comes round to where it began is a closed shape (`closed`).  What is left is open
static void joinMeeting(QList<QList<QVector3D>>& strokes, QList<QList<QVector3D>>& closed, float tol)
{
    auto same = [tol](const QVector3D& a, const QVector3D& b) { return (a - b).length() < tol; };
    bool again = true;
    while (again)
    {
        again = false;
        for (int i = 0; i < strokes.size() && !again; ++i)
        {
            if (strokes[i].size() >= 4 && same(strokes[i].first(), strokes[i].last()))
            {
                QList<QVector3D> loop = strokes.takeAt(i);
                loop.removeLast();                                  // (the point it began at is the last one too)
                closed << loop;
                again = true;
                break;
            }
            for (int j = i + 1; j < strokes.size() && !again; ++j)
            {
                const QList<QVector3D> a = strokes[i];
                QList<QVector3D> b = strokes[j];
                QList<QVector3D> merged;
                if (same(a.last(), b.first())) merged = a + b.mid(1);
                else if (same(a.last(), b.last())) { std::reverse(b.begin(), b.end()); merged = a + b.mid(1); }
                else if (same(a.first(), b.last())) merged = b + a.mid(1);
                else if (same(a.first(), b.first())) { std::reverse(b.begin(), b.end()); merged = b + a.mid(1); }
                else continue;
                strokes[i] = merged;
                strokes.removeAt(j);
                again = true;
            }
        }
    }
    // (the same outline found twice -- a symmetry of several axes makes it from every side -- is one)
    auto sameSet = [&](const QList<QVector3D>& a, const QList<QVector3D>& b) {
        if (a.size() != b.size()) return false;
        for (const QVector3D& p : a)
        {
            bool found = false;
            for (const QVector3D& q : b) found = found || same(p, q);
            if (!found) return false;
        }
        return true;
    };
    for (QList<QList<QVector3D>>* list : {&closed, &strokes})
    {
        QList<QList<QVector3D>> unique;
        for (const auto& one : *list)
        {
            bool seen = false;
            for (const auto& other : unique) seen = seen || sameSet(one, other);
            if (!seen) unique << one;
        }
        *list = unique;
    }
}

void View::finishPlacement(bool close)
{
    const int n = m_wpPlaced.size();
    if ((close && n < 3) || (!close && n < 2))
    {
        cancelPlacement();
        return;
    }
    const QList<QVector3D> points = m_wpPlaced;
    m_wpPlaced.clear();
    m_overlay->update();
    const QString name = close ? QString("profile") : (m_wp.mode == 2 ? QString("spline") : QString("polyline"));
    // (the stroke; with the symmetry on, its images are made with it -- through the images of its points, the halves that meet on a plane joined into one outline -- by the scene)
    emit(placementFinished(name, points, m_wp.mode == 2));
}

void View::cancelPlacement()
{
    if (m_wpPlaced.isEmpty()) return;
    m_wpPlaced.clear();
    m_overlay->update();
}

void View::undoPlacedPoint()
{
    if (m_wpPlaced.isEmpty()) return;
    m_wpPlaced.removeLast();
    m_overlay->update();
}

void View::keyPressEvent(QKeyEvent* event)
{
    if (m_wp.open && m_wp.exists && !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier)))
    {
        switch (event->key())
        {
        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (!m_wpPlaced.isEmpty()) { finishPlacement(false); event->accept(); return; }
            break;
        case Qt::Key_Escape:
            if (exitMode()) { event->accept(); return; }
            break;
        case Qt::Key_Backspace:
            if (!m_wpPlaced.isEmpty()) { undoPlacedPoint(); event->accept(); return; }
            break;
        default:
            break;
        }
    }
    QOpenGLWidget::keyPressEvent(event);
}

void View::createAt(const QString& name, QPoint pos)
{
    QVector3D point;
    double scale = 10;
    placementAt(pos, point, scale);
    if (name == "point") point = placementExact(pos);          // (as the menu does: a point is exactly under the cursor)
    else if (flatPrimitive(name)) point = placementFlat(pos, scale);          // (and a 2D shape where the ray meets its plane)
    emit(createRequested("primitive", name, point, scale, -1, -1));
}

bool View::flatPrimitive(const QString& name) const
{
    for (const auto v : m_catalog["primitives"].toArray())
    {
        const auto p = v.toObject();
        if (p["name"].toString() == name) return p["flat"].toBool();
    }
    return false;
}

QVector3D View::placementFlat(QPoint pos, double& scale) const
{
    QVector3D point;
    // (the work plane, while its view is open: the shape goes where the plane's grid says, as everything made there does)
    if (!(m_wp.open && m_wp.exists))
    {
        const QVector3D a = toModelPos(pos, -1.0f), b = toModelPos(pos, 1.0f);
        const QVector3D d = b - a;
        // (a view that looks along the plane z = 0 has no place on it under the cursor: the plane that faces the viewer is used then)
        if (std::fabs(d.z()) > 1e-4f * d.length())
        {
            const float t = -a.z() / d.z();
            if (t > 0)
            {
                point = a + t * d;
                point.setZ(0.0f);
                scale = scaleAt(pos, point);
                // (nothing outside the render region is drawn: a shape whose place is outside it goes to the nearest place that is inside, so that it is seen)
                const QVector3D lo = settings.min, hi = settings.max;
                auto inside = [&](float v, float lo1, float hi1) {
                    const float m = float(std::min(0.5 * scale, 0.5 * double(hi1 - lo1)));
                    return std::max(lo1 + m, std::min(hi1 - m, v));
                };
                if (hi.x() > lo.x() && hi.y() > lo.y())
                    point = QVector3D(inside(point.x(), lo.x(), hi.x()), inside(point.y(), lo.y(), hi.y()), 0.0f);
                return point;
            }
        }
    }
    placementAt(pos, point, scale);
    return point;
}

void View::updateWorkPlaneCursor(QPoint pos)
{
    QVector3D hit;
    const bool was = m_wpCursorValid;
    // (only while drawing: the select mode puts nothing down, so there is no place a click would land to show or to read out)
    m_wpCursorValid = m_wp.open && m_wp.exists && m_wp.mode != 0 && ringPoint(pos, m_wp.origin(), m_wp.normal(), hit);
    if (m_wpCursorValid)
    {
        m_wpCursor = snapToWorkPlane(hit, nullptr, &m_wpCursorSnapped);
        const QVector3D d = m_wpCursor - m_wp.origin();
        auto number = [](double x) { return QString::number(std::fabs(x) < 5e-4 ? 0.0 : x * Units::lengthScale(), 'f', Units::lengthDecimals(3)); };
        emit(workPlaneReadout(T("On the plane: %1, %2").arg(number(QVector3D::dotProduct(d, m_wp.u)), number(QVector3D::dotProduct(d, m_wp.v)))
                              + QString("   (x %1, y %2, z %3)  %4").arg(number(m_wpCursor.x()), number(m_wpCursor.y()), number(m_wpCursor.z()), Units::lengthLabel())));
        m_overlay->update();
    }
    else if (was)
    {
        emit(workPlaneReadout(QString()));
        m_overlay->update();
    }
}

// A point of what is being drawn, as the points of the script are drawn: a corner of a line is a square, a spline point a circle with a ring round it, a point a
// plain dot (`mode` is the work plane's: 1 line, 2 spline, 3 point; `alpha` less than 1 for the one that only shows where a click would land)
void View::wpMarker(QPainter& painter, const QPointF& q, int mode, double alpha, bool lit) const
{
    const double r = 5.0;
    painter.setPen(QPen(lit ? QColor(133, 153, 0) : QColor(238, 232, 213, int(255 * alpha)), 1.5));
    painter.setBrush(QColor(38, 139, 210, int(255 * alpha)));
    if (mode == 1)
    {
        painter.drawRect(QRectF(q.x() - r, q.y() - r, 2 * r, 2 * r));
        return;
    }
    painter.drawEllipse(q, r, r);
    if (mode == 2)
    {
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(238, 232, 213, int(170 * alpha)), 1.0));
        painter.drawEllipse(q, r + 3.0, r + 3.0);
    }
}

// ---- Symmetry: the symmetry model that is selected (`mirror = symmetry_planes(...)`); what is made while it is in use is made at its images too

Shape* View::symmetryShape() const
{
    if (m_symLines.isEmpty()) return nullptr;
    for (Shape* s : shapes)
    {
        if (s->isSymmetry() && m_symLines.contains(s->sourceLine())) return s;
    }
    return nullptr;
}

void View::setActiveSymmetry(QList<int> lines0, bool chosen)
{
    // (a symmetry that is chosen is in use: selecting it is how it is turned on)
    if (chosen && !lines0.isEmpty()) m_symInUse = true;
    m_symLines = lines0;
    refreshSymmetry();
}

void View::setSymmetryInUse(bool on)
{
    m_symInUse = on;
    refreshSymmetry();
}

Symmetry View::newSymmetryHere() const
{
    // The symmetry of a new model: on the work plane while its view is open -- its axis the plane's normal, its first plane along the plane's u axis -- else the mirror x = 0
    Symmetry s;
    s.on = true;
    if (m_wp.open && m_wp.exists)
    {
        s.c = m_wp.normal();
        s.a = m_wp.u;
        s.centre = m_wp.origin();
    }
    s.normalise();
    return s;
}

void View::toggleSymmetry()
{
    // Shift+S: the symmetry that is selected is used, or not.  With none in the script a mirror is made (and selected, which is to use it)
    if (!m_symExists)
    {
        emit(symmetryCreateRequested(newSymmetryHere()));
        return;
    }
    setSymmetryInUse(!m_symInUse);
}

void View::refreshSymmetry()
{
    Symmetry now;
    const Shape* s = symmetryShape();
    const bool exists = s && s->symmetryNow(now);
    m_symExists = exists;
    const int images = exists ? int(now.elements().size()) : 0;
    m_sym = exists ? now : Symmetry();
    m_sym.on = exists && m_symInUse;
    m_symElements = m_sym.on ? m_sym.elements() : QList<SymmetryElement>();
    emit(symmetryChanged(m_sym, int(m_symElements.size())));
    emit(symmetryCard(exists ? now : Symmetry(), images, exists, m_symInUse));
    m_overlay->update();
}

void View::setSymmetryKind(int kind)
{
    kind = std::max(0, std::min(3, kind));
    QStringList names{"kind"}, values{QString("'%1'").arg(kind == 3 ? "turn" : kind == 2 ? "cube" : kind == 1 ? "across" : "axis")};
    // (the number is the planes of one kind and the copies of the other: it goes to the argument that kind has, and the other one is taken out of the call)
    const bool wasTurn = m_sym.kind == Symmetry::Turn;
    if (kind == 3 && !wasTurn)
    {
        names << "copies" << "planes";
        values << QString::number(std::max(1, std::min(24, m_sym.count))) << QString();
    }
    else if (kind != 3 && wasTurn)
    {
        names << "planes" << "copies";
        values << QString::number(std::max(1, std::min(8, m_sym.count))) << QString();
    }
    emit(symmetryArguments(names, values));
}

void View::setSymmetryCount(int count)
{
    if (m_sym.kind == Symmetry::Turn)
    {
        emit(symmetryArguments(QStringList{"copies"}, QStringList{QString::number(std::max(1, std::min(24, count)))}));
        return;
    }
    count = std::max(1, std::min(8, count));
    QStringList names{"planes"}, values{QString::number(count)};
    if (m_sym.kind == Symmetry::Cube)
    {
        names << "kind";
        values << "'axis'";
    }
    emit(symmetryArguments(names, values));
}

// The symmetry the model has now, whether it is in use or not
static Symmetry symmetryOf(const Shape* s)
{
    Symmetry now;
    if (s) s->symmetryNow(now);
    return now;
}

void View::setSymmetryAxis(int which)
{
    Symmetry t = symmetryOf(symmetryShape());
    QVector3D axis, hint = t.a;
    switch (which)
    {
    case 0: axis = QVector3D(1, 0, 0); break;
    case 1: axis = QVector3D(0, 1, 0); break;
    case 2: axis = QVector3D(0, 0, 1); break;
    case 3:
        if (!m_wp.exists) return;
        axis = m_wp.normal();
        hint = m_wp.u;
        break;
    case 4:
    {
        // Up in the view; the first plane holds the way the eye looks, so that it is seen edge on: a line that splits the picture into left and right
        QVector3D o, u, v;
        planeOfView(o, u, v);
        axis = v;
        hint = towardViewer();
        break;
    }
    case 5:
    {
        QVector3D o, u, v;
        if (!planeOfSelection(o, u, v)) return;
        axis = QVector3D::crossProduct(u, v).normalized();
        hint = u;
        break;
    }
    default: return;
    }
    Symmetry::frameFrom(axis, hint, t.a, t.c);
    placeSymmetry(t);
}

void View::setSymmetryAngle(float degrees)
{
    Symmetry t = symmetryOf(symmetryShape());
    t.setAngleOfFirst(degrees);
    placeSymmetry(t);
}

void View::setSymmetryCentre(const QVector3D& centre)
{
    Symmetry t = symmetryOf(symmetryShape());
    t.centre = centre;
    placeSymmetry(t);
}

void View::setSymmetryCentreFrom(int which)
{
    Symmetry t = symmetryOf(symmetryShape());
    if (which == 0)
    {
        t.centre = QVector3D(0, 0, 0);
    }
    else if (which == 2)
    {
        if (!m_wp.exists) return;
        t.centre = m_wp.origin();
    }
    else
    {
        // The middle of what is selected: the middle of the boxes of the selected models (a point is its dot)
        QVector3D lo, hi;
        bool any = false;
        for (const Shape* s : shapes)
        {
            if (!s->isSelected() || s->isSymmetry()) continue;
            QVector3D a, b;
            if (s->isPoint() && s->dotPosition(a)) b = a;
            else if (!s->meshBox(a, b)) continue;
            lo = any ? QVector3D(std::min(lo.x(), a.x()), std::min(lo.y(), a.y()), std::min(lo.z(), a.z())) : a;
            hi = any ? QVector3D(std::max(hi.x(), b.x()), std::max(hi.y(), b.y()), std::max(hi.z(), b.z())) : b;
            any = true;
        }
        if (!any) return;
        t.centre = (lo + hi) * 0.5f;
    }
    placeSymmetry(t);
}

bool View::onSymmetryPlane(const QVector3D& p) const
{
    return symmetric() && m_sym.onPlane(p, std::max(1e-4f, 1e-3f * workPlaneStep()));
}

QList<QVector3D> View::symmetryImages(const QVector3D& p) const
{
    QList<QVector3D> out;
    out << p;
    if (!symmetric()) return out;
    for (const SymmetryElement& e : m_symElements) out << e.map.map(p);
    return out;
}

QList<QList<QVector3D>> View::symmetryStrokes(const QList<QVector3D>& stroke) const
{
    QList<QList<QVector3D>> all;
    all << stroke;
    if (!symmetric() || stroke.isEmpty()) return all;
    std::vector<QList<QVector3D>> images;
    images.resize(size_t(m_symElements.size()) + 1);
    for (const QVector3D& p : stroke)
    {
        const auto each = symmetryImages(p);
        for (int e = 1; e < each.size(); ++e) images[size_t(e)] << each[e];
    }
    const float tol = std::max(1e-4f, 1e-3f * workPlaneStep());
    // (the same points -- in any order -- are the same shape)
    auto same = [&](const QList<QVector3D>& a, const QList<QVector3D>& b) {
        if (a.size() != b.size()) return false;
        for (const QVector3D& p : a)
        {
            bool found = false;
            for (const QVector3D& q : b) found = found || (p - q).length() < tol;
            if (!found) return false;
        }
        return true;
    };
    for (int e = 1; e <= m_symElements.size(); ++e)
    {
        QList<QVector3D> image = images[size_t(e)];
        if (m_symElements[e - 1].improper) std::reverse(image.begin(), image.end());          // (a mirror image turns the way round: reversed, it faces as the stroke does)
        bool seen = false;
        for (const auto& other : all) seen = seen || same(other, image);
        if (!seen) all << image;
    }
    return all;
}

// The planes of a symmetry, drawn as translucent quads; `ink`, `strong` how much it shows
static void drawPlanes(QPainter& painter, const std::function<QPointF(const QVector3D&)>& toScreen, const Symmetry& s, float reach, const QColor& ink, double strength, bool dashed)
{
    const QVector3D c = s.centre;
    const QList<QVector3D> normals = s.planeNormals();
    for (int j = 0; j < normals.size(); ++j)
    {
        const QVector3D n = normals[j];
        // (two axes in the plane: one of the symmetry's own, so that the plane's quad is turned with it)
        QVector3D p = std::fabs(QVector3D::dotProduct(n, s.c)) < 0.99f ? s.c - n * QVector3D::dotProduct(s.c, n) : s.a - n * QVector3D::dotProduct(s.a, n);
        p.normalize();
        const QVector3D q = QVector3D::crossProduct(n, p);
        QPolygonF quad;
        quad << toScreen(c - p * reach - q * reach) << toScreen(c + p * reach - q * reach) << toScreen(c + p * reach + q * reach) << toScreen(c - p * reach + q * reach);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(ink.red(), ink.green(), ink.blue(), int((j == 0 ? 30 : 20) * strength)));
        painter.drawPolygon(quad);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(ink.red(), ink.green(), ink.blue(), int((j == 0 ? 230 : 170) * strength)), 1.5, dashed ? Qt::DashLine : Qt::SolidLine));
        painter.drawPolygon(quad);
    }
    // The axis the planes are round
    if (s.kind != Symmetry::Cube)
    {
        painter.setPen(QPen(QColor(ink.red(), ink.green(), ink.blue(), int(150 * strength)), 1.2));
        painter.drawLine(toScreen(c - s.c * reach), toScreen(c + s.c * reach));
    }
}

// The copies of a turn symmetry, drawn as a disc across the axis with a spoke where each copy's sector begins, and the axis
static void drawTurns(QPainter& painter, const std::function<QPointF(const QVector3D&)>& toScreen, const Symmetry& s, float reach, const QColor& ink, double strength, bool dashed)
{
    Symmetry n = s;
    n.normalise();
    const QVector3D c = n.centre, a = n.a, b = n.b();
    const int copies = std::max(1, std::min(24, s.count));
    const float radius = 0.55f * reach;
    auto at = [&](float t) { return toScreen(c + radius * (a * std::cos(t) + b * std::sin(t))); };
    QPolygonF disc;
    for (int k = 0; k < 72; ++k) disc << at(float(2.0 * M_PI * k / 72.0));
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(ink.red(), ink.green(), ink.blue(), int(24 * strength)));
    painter.drawPolygon(disc);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(ink.red(), ink.green(), ink.blue(), int(190 * strength)), 1.5, dashed ? Qt::DashLine : Qt::SolidLine));
    painter.drawPolygon(disc);
    painter.setPen(QPen(QColor(ink.red(), ink.green(), ink.blue(), int(230 * strength)), 1.5, dashed ? Qt::DashLine : Qt::SolidLine));
    for (int k = 0; k < copies; ++k) painter.drawLine(toScreen(c), at(float(2.0 * M_PI * k / copies)));
    painter.setPen(QPen(QColor(ink.red(), ink.green(), ink.blue(), int(150 * strength)), 1.2));
    painter.drawLine(toScreen(c - n.c * reach), toScreen(c + n.c * reach));
}

void View::drawSymmetry(QPainter& painter)
{
    if (!m_symExists) return;
    const QColor ink(211, 54, 130);            // (magenta: not the red and green of the plane's own axes, nor the blue of the grid)
    auto to = [this](const QVector3D& p) { return toScreen(p); };
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    if (m_symExists)
    {
        Symmetry now = symmetryOf(symmetryShape());
        // How far the planes reach: about five hundred pixels' worth of the world at the centre, so that they are as large as the picture whatever the zoom (a plane seen edge
        // on, in the work plane's view, is a line that spans the picture)
        const float reach = std::max(1e-3f, float(scaleAt(toScreen(now.centre).toPoint(), now.centre)) * 5.0f);
        if (now.kind == Symmetry::Turn) drawTurns(painter, to, now, reach, ink, m_symInUse ? 1.0 : 0.3, !m_symInUse);
        else drawPlanes(painter, to, now, reach, ink, m_symInUse ? 1.0 : 0.3, !m_symInUse);
        // (the middle: a cross, small -- the gizmo of the model is what moves it)
        const QPointF c0 = toScreen(now.centre);
        painter.setPen(QPen(ink, 1.6));
        painter.drawLine(c0 + QPointF(-5, 0), c0 + QPointF(5, 0));
        painter.drawLine(c0 + QPointF(0, -5), c0 + QPointF(0, 5));
    }
    painter.restore();
}

void View::drawWorkPlane(QPainter& painter)
{
    if (!m_wp.open || !m_wp.exists) return;
    const QVector3D o = m_wp.origin(), u = m_wp.u, v = m_wp.v, n = m_wp.normal();
    // The part of the plane the picture shows: where the rays through the corners of the viewport meet it
    float smin = 1e30f, smax = -1e30f, tmin = 1e30f, tmax = -1e30f;
    int found = 0;
    for (const QPoint corner : {QPoint(0, 0), QPoint(width(), 0), QPoint(width(), height()), QPoint(0, height())})
    {
        QVector3D hit;
        if (!ringPoint(corner, o, n, hit)) continue;
        const float s = QVector3D::dotProduct(hit - o, u), t = QVector3D::dotProduct(hit - o, v);
        smin = std::min(smin, s);
        smax = std::max(smax, s);
        tmin = std::min(tmin, t);
        tmax = std::max(tmax, t);
        ++found;
    }
    const QVector3D lo = settings.min, hi = settings.max;
    const float reach = (hi.x() > lo.x() ? (hi - lo).length() : 100.0f) * 0.75f;
    if (found < 4)
    {
        // (a plane that is seen edge on: round the origin, as far as the render region reaches)
        smin = tmin = -reach;
        smax = tmax = reach;
    }
    const float cap = 3.0f * reach;
    smin = std::max(smin, -cap);
    tmin = std::max(tmin, -cap);
    smax = std::min(smax, cap);
    tmax = std::min(tmax, cap);
    if (!(smax > smin) || !(tmax > tmin)) return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    // The plane, a little tinted
    QPolygonF quad;
    quad << toScreen(o + u * smin + v * tmin) << toScreen(o + u * smax + v * tmin) << toScreen(o + u * smax + v * tmax) << toScreen(o + u * smin + v * tmax);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(38, 139, 210, 26));
    painter.drawPolygon(quad);
    // The grid: every line of its step, a stronger one at every fifth; a coarser one when the step would be closer than a few pixels
    if (m_wp.grid)
    {
        const float step = workPlaneStep();
        float shown = step;
        const float span = std::max(smax - smin, tmax - tmin);
        while (span / shown > 150.0f) shown *= 2.0f;
        for (int pass = 0; pass < 2; ++pass)
        {
            const float lo0 = pass == 0 ? smin : tmin, hi0 = pass == 0 ? smax : tmax;
            const float a0 = pass == 0 ? tmin : smin, a1 = pass == 0 ? tmax : smax;
            for (int i = int(std::ceil(lo0 / shown)); i <= int(std::floor(hi0 / shown)); ++i)
            {
                const float c = float(i) * shown;
                const bool strong = i % 5 == 0;
                painter.setPen(QPen(QColor(147, 200, 235, strong ? 120 : 52), strong ? 1.2 : 0.8));
                const QVector3D from = pass == 0 ? o + u * c + v * a0 : o + u * a0 + v * c;
                const QVector3D to = pass == 0 ? o + u * c + v * a1 : o + u * a1 + v * c;
                painter.drawLine(toScreen(from), toScreen(to));
            }
        }
    }
    // The plane's own axes through its origin
    painter.setPen(QPen(QColor(220, 50, 47, 170), 1.6));
    painter.drawLine(toScreen(o + u * smin), toScreen(o + u * smax));
    painter.setPen(QPen(QColor(116, 184, 31, 170), 1.6));
    painter.drawLine(toScreen(o + v * tmin), toScreen(o + v * tmax));
    // The view is a mode: a frame round the viewport says so
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(38, 139, 210, 150), 2.0));
    painter.drawRect(rect().adjusted(1, 1, -2, -2));
    // The line or the spline that is being put down: its points, and the way through them to where the cursor is
    if (!m_wpPlaced.isEmpty())
    {
        QList<QVector3D> way = m_wpPlaced;
        if (m_wpCursorValid) way << m_wpCursor;
        QPolygonF line;
        if (m_wp.mode == 2 && way.size() >= 3)
        {
            std::vector<libfive::pointshapes::Control> controls;
            for (const QVector3D& p : way)
            {
                libfive::pointshapes::Control c;
                c.pos = Eigen::Vector3d(p.x(), p.y(), p.z());
                c.smooth = 1;
                controls.push_back(c);
            }
            for (const Eigen::Vector3d& q : libfive::pointshapes::polyline(controls, true, false, 14)) line << toScreen(QVector3D(float(q.x()), float(q.y()), float(q.z())));
        }
        else
        {
            for (const QVector3D& p : way) line << toScreen(p);
        }
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(238, 232, 213, 235), 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPolyline(line);
        // (a click on the first point closes it: it is shown as a target when there are enough points for a shape)
        const QPointF first = toScreen(m_wpPlaced.first());
        const bool closing = m_wpPlaced.size() >= 3 && m_wpCursorValid && (first - toScreen(m_wpCursor)).manhattanLength() < 12;
        for (int i = 0; i < m_wpPlaced.size(); ++i) wpMarker(painter, toScreen(m_wpPlaced[i]), m_wp.mode, 1.0, closing && i == 0);
        if (m_wpPlaced.size() >= 3)
        {
            painter.setPen(QPen(closing ? QColor(133, 153, 0) : QColor(238, 232, 213), 1.6));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(first, closing ? 13.0 : 11.0, closing ? 13.0 : 11.0);
        }
    }
    // The images of what is being put down, in the symmetry: the line through them (the way the real one will go) and their points, lighter than the stroke itself
    if (symmetric() && (!m_wpPlaced.isEmpty() || (m_wpCursorValid && m_wp.mode != 0)))
    {
        QList<QVector3D> way = m_wpPlaced;
        if (m_wpCursorValid && m_wp.mode != 0) way << m_wpCursor;
        const int count = int(m_symElements.size()) + 1;
        std::vector<QList<QVector3D>> images;
        images.resize(size_t(count));
        for (const QVector3D& p : way)
        {
            const auto each = symmetryImages(p);
            for (int e = 1; e < count && e < each.size(); ++e) images[e] << each[e];
        }
        for (int e = 1; e < count; ++e)
        {
            const QList<QVector3D>& image = images[e];
            if (image.isEmpty()) continue;
            if (m_wp.mode != 3 && image.size() >= 2)
            {
                QPolygonF line;
                if (m_wp.mode == 2 && image.size() >= 3)
                {
                    std::vector<libfive::pointshapes::Control> controls;
                    for (const QVector3D& p : image)
                    {
                        libfive::pointshapes::Control c;
                        c.pos = Eigen::Vector3d(p.x(), p.y(), p.z());
                        c.smooth = 1;
                        controls.push_back(c);
                    }
                    for (const Eigen::Vector3d& q : libfive::pointshapes::polyline(controls, true, false, 14)) line << toScreen(QVector3D(float(q.x()), float(q.y()), float(q.z())));
                }
                else
                {
                    for (const QVector3D& p : image) line << toScreen(p);
                }
                painter.setBrush(Qt::NoBrush);
                painter.setPen(QPen(QColor(211, 54, 130, 190), 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                painter.drawPolyline(line);
            }
            for (int i = 0; i < image.size(); ++i) wpMarker(painter, toScreen(image[i]), m_wp.mode, 0.6, false);
        }
    }
    // Where a click would land: the point it puts down, as it will be -- a corner (square), a spline point (round, with a ring), a point -- snapped.  Not drawing
    // (the select mode): nothing is put down, so there is no point to show
    if (m_wpCursorValid && m_wp.mode != 0) wpMarker(painter, toScreen(m_wpCursor), m_wp.mode, 0.75, false);
    painter.restore();
}

void View::loadMenuCatalog()
{
    if (!m_catalog.isEmpty() || !m_catalogSource) return;
    m_catalog = QJsonDocument::fromJson(m_catalogSource().toUtf8()).object();
    TypeIcons::setKinds(m_catalog["kinds"].toObject());
    TypeIcons::setDomains(m_catalog["domains"].toObject());
}

namespace {

// What the simulation menu holds besides the analyses: the groups of primitives that are about a simulation (a material, a
// fluid, the supports and loads and the thermal and flow conditions an analysis is given), and the operation that makes
// the boundary conditions of a model
bool isSimulationPrimitive(const QString& group)
{
    return group == "Materials" || group == "Fluids" || group == "Supports and loads" ||
           group == "Thermal conditions" || group == "Flow conditions" || group == "Manufacturing constraints";
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

// Greys out a new model of a menu that does not fit what the menu is about.  On a model: whatever the model(s) are, the library says whether the entry fits (a thing made by
// number is a placement: it always does); in the menu of empty space only a condition made of the models selected is checked
void View::checkPrimitive(QAction* action, const QString& name, bool withBodies, int atLine) const
{
    if (!m_scene) return;
    if (m_menuModelLine >= 0)
    {
        if (m_scene->entryAllowed("primitive", name, m_menuModelLine)) return;
        action->setEnabled(false);
        const QString own = action->toolTip();
        action->setToolTip(own.isEmpty() || own == action->text() ? T("Does not fit what is selected") : T("Does not fit what is selected") + "\n" + own);
        return;
    }
    if (withBodies && !m_scene->entryAllowed("primitive", name, atLine)) action->setEnabled(false);
}

void View::fillSimulationPrimitives(QMenu* menu, const QVector3D& point, double scale, int atLine, const QString& domain, const QStringList& only, const QStringList& skip)
{
    // New material, New fluid, New support or load, New thermal condition, New flow condition: each a menu of its own, in the
    // order the catalog has them
    static const QHash<QString, QString> titles = {
        {"Materials", T("New material")}, {"Fluids", T("New fluid")}, {"Supports and loads", T("New support or load")},
        {"Thermal conditions", T("New thermal condition")}, {"Flow conditions", T("New flow condition")},
        {"Manufacturing constraints", T("New manufacturing constraint")}};
    static const QStringList order = {"Materials", "Fluids", "Supports and loads", "Thermal conditions", "Flow conditions", "Manufacturing constraints"};
    QHash<QString, QList<QJsonObject>> groups;
    for (const auto v : m_catalog["primitives"].toArray())
    {
        const auto p = v.toObject();
        const QString g = p["group"].toString();
        if (isSimulationPrimitive(g) && p["domain"].toString() == domain && (only.isEmpty() || only.contains(g)) && !skip.contains(g)) groups[g] << p;
    }
    for (const QString& g : order)
    {
        if (!groups.contains(g)) continue;
        const auto& entries = groups[g];
        const QString type = entries.first()["type"].toString("conditions");
        auto sub = menu->addMenu(TypeIcons::icon(type, false, domain), titles.value(g, T(g)));
        sub->setToolTipsVisible(true);
        for (const auto& p : entries)
        {
            const QString name = p["name"].toString();
            auto action = sub->addAction(TypeIcons::icon(p["type"].toString(type), false, domain, name), name,
                                         this, [=]{ emit(createRequested("primitive", name, point, scale, atLine, -1)); });
            if (p["with_bodies"].toBool())
            {
                action->setToolTip(T("With several models selected: the first is the body, the others are where it acts (bodies, fields, "
                                   "selected surfaces) -- written with the surface of the body there.  With one model, or none: a placeholder for "
                                   "where it acts, to drop a body, a field or a surface on."));
            }
            // (made of the models selected, or something to give a simulation: not offered when the selection has no use for it)
            checkPrimitive(action, name, p["with_bodies"].toBool(), atLine);
        }
    }
}

void View::fillLatticeMenu(QMenu* menu, const QVector3D& point, double scale, int line, const QString& section)
{
    const int generation = m_scene ? m_scene->generation() : -1;
    // The operations that fill a model with a lattice, and the points a foam is made around (random_points, then voronoi_lattice).  With no model to work on -- an empty script, or
    // nothing selected that fits -- they are there like every other operation and write a placeholder (`...`) where the model goes
    int shown = 0;
    for (const auto v : m_catalog["operations"].toArray())
    {
        const auto o = v.toObject();
        if (o["section"].toString() != section) continue;
        const QString name = o["name"].toString();
        auto action = menu->addAction(TypeIcons::icon(o["type"].toString("lattice")), name, this,
                                      [=]{ emit(createRequested("operation", name, point, scale, line, generation)); });
        if (!o["hint"].toString().isEmpty()) action->setToolTip(o["hint"].toString());
        if (m_scene && !m_scene->entryAllowed("operation", name, line)) action->setEnabled(false);
        ++shown;
    }
    if (shown) menu->addSeparator();
    // The cells: a menu for each family, in the order the catalog lists them
    QStringList order;
    QHash<QString, QList<QJsonObject>> groups;
    for (const auto v : m_catalog["primitives"].toArray())
    {
        const auto p = v.toObject();
        if (p["section"].toString() != section) continue;
        const QString g = p["group"].toString();
        if (!groups.contains(g)) order << g;
        groups[g] << p;
    }
    for (const QString& g : order)
    {
        auto sub = menu->addMenu(TypeIcons::icon("cell"), T(g));
        sub->setToolTipsVisible(true);
        for (const auto& p : groups[g])
        {
            const QString name = p["name"].toString();
            const QString label = p["label"].toString(name);
            auto action = sub->addAction(TypeIcons::icon("cell", false, QString(), name), label, this,
                                         [=]{ emit(createRequested("primitive", name, point, scale, line, -1)); });
            action->setToolTip(T("A new cell: %1 -- a lattice operation takes it as its cell").arg(name));
            checkPrimitive(action, name, false, line);
        }
    }
}

void View::fillPythonMenu(QMenu* menu, const QVector3D& point, double scale, int line)
{
    const QJsonArray blocks = m_catalog["python"].toArray();
    if (blocks.isEmpty()) menu->addAction(T("(not loaded yet: run the script first)"))->setEnabled(false);
    for (const auto v : blocks)
    {
        const auto b = v.toObject();
        const QString name = b["name"].toString();
        auto action = menu->addAction(TypeIcons::icon("python"), b["label"].toString(name), this,
                                      [=]{ emit(createRequested("python", name, point, scale, line, -1)); });
        action->setToolTip(b["doc"].toString() + "\n" + T("Plain Python, written at the insertion point of the tree; edit it in the code editor."));
    }
}

void View::fillOperations(QMenu* menu, int line, const QVector3D& point, double scale, bool simulations, const QString& domain)
{
    QJsonArray ops;
    for (const auto v : m_catalog["operations"].toArray())
    {
        if (v.toObject()["quick"].toBool()) continue;          // (in the first menu: see addQuickOperations)
        if (!v.toObject()["section"].toString().isEmpty()) continue;      // (the entries that have a section have a category of their own: see fillLatticeMenu)
        if (isSimulationOperation(v.toObject()["group"].toString()) != simulations) continue;
        // (a category of the Engineering menu: its analyses; the domain "-" is what has no domain, what works over an analysis of any -- it is in the menu itself)
        // (what is first in the Engineering menu itself -- the domain "^": the optimisation that designs for an analysis of any domain -- is neither in a category nor among the rest)
        if (simulations && (domain == "^" ? !v.toObject()["top"].toBool() : v.toObject()["top"].toBool())) continue;
        if (simulations && !domain.isEmpty() && domain != "^" && (domain == "-" ? !v.toObject()["domain"].toString().isEmpty() : v.toObject()["domain"].toString() != domain)) continue;
        ops.append(v);
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
        if (simulations)
            action->setIcon(o["domain"].toString().isEmpty() && group != "Conditions" ? TypeIcons::icon("engineering")
                                                                                      : TypeIcons::icon(group == "Conditions" ? "conditions" : "simulation", false, o["domain"].toString()));
        if (!o["doc"].toString().isEmpty()) action->setToolTip(o["doc"].toString());
        else if (o["other"].toBool() && !o["needs_other"].toBool(true))
        {
            action->setToolTip(T("With several models selected: the first is the one this is made for, the others are what it uses -- where it "
                               "acts, its conditions, its material.  With one model: a first version to change in the script."));
        }
        if (o["needs_other"].toBool(o["other"].toBool()) && !haveOther)
        {
            // (a second model is what it combines the first with: with none in the script a placeholder stands for it, to drop a model on)
            const QString own = action->toolTip();
            const QString say = T("There is no second model yet: a placeholder (...) stands where it goes");
            action->setToolTip(own.isEmpty() || own == action->text() ? say : own + "\n" + say);
        }
        // (what is selected cannot make it: the library refuses the call for this selection -- a part with supports and loads for a set of
        // conditions, conditions with no part for a simulation ...)
        if (m_scene && action->isEnabled() && !m_scene->entryAllowed("operation", name, line)) action->setEnabled(false);
    }
}

void View::addExportEntries(QMenu* sub, int line)
{
    // What can be written of the model displayed on `line`: the entries of File > Export model (a file is not made by a menu of the viewport: that menu writes code)
    if (!m_scene) return;
    const QString type = m_scene->typeAtLine(line);
    const QStringList exports = m_scene->exportsAtLine(line);
    const bool shape = type == "solid" || type == "profile" || type == "surface" || type == "lattice";
    const bool simulation = type == "simulation";
    const bool field = type == "field";
    if (!shape && !simulation && !field)
    {
        sub->addAction(T("Nothing of this model can be written to a file"))->setEnabled(false);
        return;
    }
    const bool named = !m_scene->varAtLine(line).isEmpty();
    sub->setToolTipsVisible(true);
    auto add = [&](QMenu* into, const QString& text, const QString& kind, const QString& ext, bool enabled, const QString& why) {
        auto a = into->addAction(text, this, [=] { if (m_scene) m_scene->requestExport(line, kind, ext); });
        a->setEnabled(enabled && named);
        if (!named) a->setToolTip(T("Give the model a name first: only a variable of the script is exported."));
        else if (!enabled) a->setToolTip(why);
        return a;
    };
    if (shape || (simulation && exports.contains("mesh")))
    {
        auto mesh = sub->addMenu(T("Mesh"));
        mesh->setToolTipsVisible(true);
        mesh->setToolTip(T("The surface of the model as triangles, at the resolution of the scene"));
        static const QList<QPair<QString, QString>> formats = {
            {"STL (.stl)", ".stl"}, {"OBJ (.obj)", ".obj"}, {"PLY (.ply)", ".ply"}, {"3MF (.3mf)", ".3mf"}, {"glTF binary (.glb)", ".glb"},
            {"glTF (.gltf)", ".gltf"}, {"FBX (.fbx)", ".fbx"}, {"OFF (.off)", ".off"}, {"DXF (.dxf)", ".dxf"}};
        for (const auto& f : formats) add(mesh, f.first, "mesh", f.second, true, QString());
    }
    if (field)
    {
        auto grid = sub->addMenu(T("Field on a grid"));
        grid->setToolTipsVisible(true);
        grid->setToolTip(T("The field sampled on a regular grid at the resolution of the scene, for other programs"));
        add(grid, T("VTK image data (.vti)"), "field", ".vti", true, QString());
        add(grid, T("CSV table (.csv)"), "field", ".csv", true, QString());
    }
    if (simulation && exports.contains("meshfile"))
    {
        // A mesh made by mesh_volume or mesh_surface: the file of another program, by the extension
        auto file = sub->addMenu(T("Mesh file"));
        file->setToolTipsVisible(true);
        file->setToolTip(T("The mesh as it is, with its zones, for another program"));
        static const QList<QPair<QString, QString>> formats = {
            {"Gmsh (.msh)", ".msh"}, {"VTK (.vtu)", ".vtu"}, {"Abaqus / CalculiX (.inp)", ".inp"}, {"Nastran (.bdf)", ".bdf"}, {"I-DEAS (.unv)", ".unv"},
            {"STL (.stl)", ".stl"}, {"OBJ (.obj)", ".obj"}};
        for (const auto& f : formats) add(file, f.first, "meshfile", f.second, true, QString());
        return;
    }
    if (simulation)
    {
        const QString staticOnly = T("Only for a static analysis: the others do not keep a mesh to write");
        auto model = sub->addMenu(T("Finite-element model"));
        model->setToolTipsVisible(true);
        model->setToolTip(T("The mesh the analysis was solved on, with its material, supports and loads, for another solver"));
        add(model, T("Abaqus / CalculiX (.inp)"), "fea", ".inp", exports.contains("fea"), staticOnly);
        add(model, T("Nastran (.bdf)"), "fea", ".bdf", exports.contains("fea"), staticOnly);
        add(model, T("Gmsh (.msh)"), "fea", ".msh", exports.contains("fea"), staticOnly);
        auto results = sub->addMenu(T("Results"));
        results->setToolTipsVisible(true);
        add(results, T("VTK mesh with every field (.vtu)"), "results", ".vtu", exports.contains("results"), staticOnly);
        add(results, T("CSV tables (.csv)"), "results", ".csv", exports.contains("results"), staticOnly);
        add(results, T("Key numbers as text (.txt)"), "results", ".txt", exports.contains("summary"),
            T("This result has no key numbers to write"));
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

namespace {

// The categories of a menu come from what is in them, not from a list of them: a category -- a submenu, at any depth -- is in the menu when there is a valid entry
// somewhere in it, and is not in it (not greyed out: not there) when there is none.  Only categories are decided so; the entries of one are as they are.  A category
// that is added to a menu later is covered by this and needs no rule of its own.  Returns whether `menu` holds anything valid
bool keepCategoriesWithValidEntries(QMenu* menu)
{
    bool valid = false;
    for (QAction* act : menu->actions())
    {
        if (act->isSeparator()) continue;
        if (QMenu* sub = act->menu())
        {
            if (!act->isEnabled() || !keepCategoriesWithValidEntries(sub)) menu->removeAction(act);
            else valid = true;
        }
        else if (act->isEnabled())
        {
            valid = true;
        }
        else if (act->data().toString() == "category")
        {
            menu->removeAction(act);                      // (an entry that stands for its category, with nothing else in it: no valid entry, so no category)
        }
    }
    // (the lines that have nothing left to set apart: first, last and twice in a row)
    bool lineBefore = true;
    for (QAction* act : menu->actions())
    {
        if (!act->isSeparator()) { lineBefore = false; continue; }
        if (lineBefore) menu->removeAction(act);
        lineBefore = true;
    }
    const QList<QAction*> left = menu->actions();
    if (!left.isEmpty() && left.last()->isSeparator()) menu->removeAction(left.last());
    return valid;
}

}   // namespace

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
    m_menuModelLine = -1;                   // (empty space: what is made is made at the click)
    QVector3D point;
    double scale = 10;
    placementAt(pos, point, scale);

    auto menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->setToolTipsVisible(true);
    // The field the field viewer shows (the disc is not a model that can be right-clicked: this is its menu): a body from it at once
    if (activeFieldOpen() && m_scene)
    {
        addBodyFromField(menu, -1, point, scale);
        menu->addSeparator();
    }
    double flatScale = scale;
    const QVector3D flat = placementFlat(pos, flatScale);
    const QVector3D exact = placementExact(pos);
    // The basic things -- a 3D shape, a 2D shape, a line, a point, a surface, a field: the primitives -- are in a menu of their own, the more complicated shapes made with
    // parameters in another, what is done to a model -- the operations -- under Operations, and what is worked out about it or made of it -- the analyses, the lattices -- under Engineering
    auto primitives = menu->addMenu(TypeIcons::icon("solid"), T("Primitives"));
    primitives->setToolTipsVisible(true);
    addCreateEntries(primitives, point, exact, flat, scale, flatScale, atLine, true);
    auto shapes = menu->addMenu(TypeIcons::icon("shapes"), T("Parametric shapes"));
    shapes->setToolTipsVisible(true);
    addCreateEntries(shapes, point, exact, flat, scale, flatScale, atLine, false);
    auto ops = menu->addMenu(TypeIcons::icon("operation"), T("Operations"));
    ops->setToolTipsVisible(true);
    fillOperationsMenu(ops, -1, point, scale, atLine);
    auto engineering = menu->addMenu(TypeIcons::icon("engineering"), T("Engineering"));
    engineering->setToolTipsVisible(true);
    fillEngineeringMenu(engineering, -1, point, scale, atLine);
    auto python = menu->addMenu(TypeIcons::icon("python"), T("Python"));
    python->setToolTipsVisible(true);
    fillPythonMenu(python, point, scale, atLine);
    if (keepCategoriesWithValidEntries(menu)) menu->popup(globalPos);
    else menu->deleteLater();
}

// What the Operations menu holds, on a model (`line` >= 0) or on empty space: the operations -- offsets and walls, rounding, moving ... -- on the model that is selected
// (the lattices are in the Engineering menu)
void View::fillOperationsMenu(QMenu* ops, int line, const QVector3D& point, double scale, int atLine)
{
    Q_UNUSED(atLine);
    // Nothing is left out of it for want of a model: an operation with no model to work on -- the script has none, or nothing selected fits -- is written with a placeholder
    // (`...`) where the model goes, as the analyses of the Engineering menu are.  What has no meaning without a model -- center and bounding_box, which measure one -- is
    // in the menu of empty space only when the script has one
    if (line < 0 && (!m_scene || m_scene->hasModel())) addQuickOperations(ops, -1, point, scale);
    fillOperations(ops, line, point, scale);
}

// What the Engineering menu holds: first the optimisation that designs for an analysis of any domain, on its own; then a category for each domain -- Structural, Thermal, Fluid -- each with the icon and the colour of its domain, each with what an analysis of it is
// given (a support or a load, a thermal or a flow condition: a model of its own, no other model is needed for it) and the analyses and optimisations of it (they work on what is
// selected -- a part, its conditions, a material -- and write a placeholder for what is not: they are always there); a category for each kind of lattice -- periodic and
// non-periodic ones -- with what it makes and what it is made of; the materials -- a material, which an analysis of any domain takes, and a fluid, the material of a flow -- in
// one category; then what works over an analysis of any domain
void View::fillEngineeringMenu(QMenu* menu, int line, const QVector3D& point, double scale, int atLine)
{
    static const QList<QPair<QString, QString>> categories = {
        {"structural", T("Structural")}, {"thermal", T("Thermal")}, {"fluid", T("Fluid")}, {"manufacturing", T("Manufacturing")}, {"mesh", T("Mesh")}};
    const int at = line < 0 ? atLine : line;
    // First, on its own: what designs for an analysis of any domain (topology_optimization); then a line, then the categories
    fillOperations(menu, line, point, scale, true, "^");
    menu->addSeparator();
    for (const auto& c : categories)
    {
        auto sub = menu->addMenu(TypeIcons::icon("simulation", false, c.first), c.second);
        sub->setToolTipsVisible(true);
        fillSimulationPrimitives(sub, point, scale, at, c.first, QStringList(), QStringList{"Fluids"});      // (a fluid is with the materials)
        sub->addSeparator();
        fillOperations(sub, line, point, scale, true, c.first);
    }
    // What an analysis is made of, in one place: New material -- of any domain -- and New fluid, which is the material of a flow
    {
        auto materials = menu->addMenu(TypeIcons::icon("material"), T("Materials"));
        materials->setToolTipsVisible(true);
        fillSimulationPrimitives(materials, point, scale, at, QString(), QStringList{"Materials"});
        fillSimulationPrimitives(materials, point, scale, at, "fluid", QStringList{"Fluids"});
    }
    // (a category for each kind of lattice -- the catalog lists them in the order they are shown: the label is in the language already, it comes from the library)
    for (const auto s : m_catalog["sections"].toArray())
    {
        const auto section = s.toObject();
        auto lattices = menu->addMenu(TypeIcons::icon(section["type"].toString("lattice")), section["label"].toString());
        lattices->setToolTipsVisible(true);
        fillLatticeMenu(lattices, point, scale, at, section["name"].toString());
    }
    menu->addSeparator();
    fillOperations(menu, line, point, scale, true, "-");       // (what works over an analysis of any domain)
}

// What can be made, a menu for each kind of thing, each with its own icon: 3D shapes, 2D shapes, a point, surfaces, fields, and the custom blocks that need no model.
// (A kind with one entry is that entry.)  `basic`: the primitives -- the most basic representation of each kind -- or all the rest.  What a simulation is made of -- materials,
// fluids, supports and loads, thermal and flow conditions -- is in the simulation menu, not here.  `point` is where a shape is made, inside what is drawn; a POINT is made
// exactly at `exact`, where the cursor is -- a dot is drawn wherever it is
void View::addCreateEntries(QMenu* menu, const QVector3D& point, const QVector3D& exact, const QVector3D& flat, double scale, double flatScale, int atLine, bool basic)
{
    const QJsonArray list = m_catalog["primitives"].toArray();
    if (list.isEmpty()) menu->addAction(T("(not loaded yet: run the script first)"))->setEnabled(false);
    {
        static const QHash<QString, QString> titles = {
            {"1D", T("New 1D shape")}, {"3D", T("New 3D shape")}, {"2D", T("New 2D shape")}, {"Points", T("New point")}, {"Surfaces", T("New surface")},
            {"Fields", T("New field")}, {"Symmetry", T("New symmetry")}, {"Custom blocks", T("New custom block")}};
        QStringList order;
        QHash<QString, QList<QJsonObject>> groups;
        for (const auto v : list)
        {
            const auto p = v.toObject();
            const QString g = p["group"].toString();
            if (isSimulationPrimitive(g) || !p["section"].toString().isEmpty()) continue;      // (their own menus: Engineering, the lattice categories)
            if (p["primitive"].toBool() != basic) continue;
            if (!groups.contains(g)) order << g;
            groups[g] << p;
        }
        for (const QString& g : order)
        {
            const auto& entries = groups[g];
            const QString type = entries.first()["type"].toString("solid");
            auto make = [&](QMenu* into, const QJsonObject& p) {
                const QString name = p["name"].toString();
                // (a point is exactly under the cursor; a 2D shape that is written with an x and a y only is where the ray under the cursor meets its plane, z = 0)
                const bool isFlat = p["flat"].toBool();
                const QVector3D at = p["group"].toString() == "Points" ? exact : isFlat ? flat : point;
                const double size = isFlat ? flatScale : scale;
                auto action = into->addAction(TypeIcons::icon(p["type"].toString(type), false, QString(), name), p["label"].toString(name),
                                              this, [=]{ emit(createRequested("primitive", name, at, size, atLine, -1)); });
                if (p.contains("doc") && !p["doc"].toString().isEmpty()) action->setToolTip(p["doc"].toString());
                checkPrimitive(action, name, false, atLine);
                return action;
            };
            if (entries.size() == 1 && g != "Custom blocks")
            {
                auto a = make(menu, entries.first());
                a->setText(titles.value(g, T("New %1").arg(entries.first()["name"].toString())));
                a->setData(QStringLiteral("category"));         // (a category with one entry is shown as that entry: it goes when it is not valid, as a category does)
                continue;
            }
            auto sub = menu->addMenu(TypeIcons::icon(type), titles.value(g, T(g)));
            sub->setToolTipsVisible(true);
            for (const auto& p : entries) make(sub, p);
        }
    }
}

// Where a click puts a POINT: exactly under the cursor -- on the work plane, snapped, while its view is open; else in the plane through the origin that faces the
// viewer (the point of the ray there).  Not moved into the drawn region and not rounded: a dot is drawn wherever it is
QVector3D View::placementExact(QPoint pos) const
{
    if (m_wp.open && m_wp.exists)
    {
        QVector3D hit;
        if (ringPoint(pos, m_wp.origin(), m_wp.normal(), hit)) return snapToWorkPlane(hit);
    }
    return placeOnRay(pos);
}

void View::showSurfaceMenu(QPoint globalPos, int line, const QVector3D& point, double scale, bool onSurface)
{
    loadMenuCatalog();
    m_menuModelLine = line;                 // (a model: what is offered fits it, whether the menu came from its surface in the viewport, its row or its line)
    auto menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->setToolTipsVisible(true);
    // A field is not drawn, so what is made from it is the first thing its menu offers: a body (field_from_body is the way back)
    if (m_scene && m_scene->typeAtLine(line) == "field")
    {
        addBodyFromField(menu, line, point, scale);
        menu->addSeparator();
    }
    // What can be made is offered on a model too, and whichever way its menu was opened: by a right-click on its surface in the viewport (made at the place that
    // was clicked, a point exactly there), from its row in the model tree or from a line of the text editor (made at the middle of the model).  A menu has the
    // same categories wherever it comes from; what depends on the model is which of their entries are valid -- the selection decides that (a surface selected
    // makes `profile` the outline of that surface), not the way the menu was reached.  The primitives are in a menu of their own, the more complicated shapes made with
    // parameters in another, what is done to the model -- the operations on it -- under Operations, and what is worked out about it or made of it -- the analyses and the
    // lattices -- under Engineering; the quick ones (center, bounding_box) are below them
    auto primitives = menu->addMenu(TypeIcons::icon("solid"), T("Primitives"));
    primitives->setToolTipsVisible(true);
    addCreateEntries(primitives, point, point, point, scale, scale, -1, true);
    auto shapes = menu->addMenu(TypeIcons::icon("shapes"), T("Parametric shapes"));
    shapes->setToolTipsVisible(true);
    addCreateEntries(shapes, point, point, point, scale, scale, -1, false);
    auto ops = menu->addMenu(TypeIcons::icon("operation"), T("Operations"));
    ops->setToolTipsVisible(true);
    fillOperationsMenu(ops, line, point, scale, -1);
    auto engineering = menu->addMenu(TypeIcons::icon("engineering"), T("Engineering"));
    engineering->setToolTipsVisible(true);
    fillEngineeringMenu(engineering, line, point, scale, -1);
    auto python = menu->addMenu(TypeIcons::icon("python"), T("Python"));
    python->setToolTipsVisible(true);
    fillPythonMenu(python, point, scale, -1);
    menu->addSeparator();
    addQuickOperations(menu, line, point, scale);
    menu->addSeparator();
    {
        // A resolution of its own for this model: the line is written under its definition (and the scene's resolution is its number)
        const QString type = m_scene ? m_scene->typeAtLine(line) : QString();
        auto own = menu->addAction(T("Custom resolution"), this, [=]{ if (m_scene) m_scene->addCustomResolution(line); });
        own->setToolTip(T("Draw this model at a resolution of its own -- the number the scene's resolution is, for this model alone: writes custom_resolution(x, number) under its "
                        "definition; the number is a row under the model in the tree, and a click on it selects it in the code"));
        if (type == "field" || type == "point" || type.isEmpty())
        {
            own->setEnabled(false);
            own->setToolTip(T("A resolution of its own is for a model that is drawn: a shape, a surface"));
        }
    }
    // (what makes a point, an edge or a face of the body -- Select Surface and the points -- stands together, below the rest)
    menu->addSeparator();
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
    if (onSurface && m_scene)
    {
        // Near an edge or a corner of the body (the library looks at the surface round the click -- about thirty pixels -- and says what is there, so a click on a face or a smooth
        // surface offers nothing): a point exactly at the corner, a point on the edge, the edge as a line.  They are written under the body as `corner_at(...)`, `edge_point_at(...)`,
        // `edge_at(...)`, like a selected surface is
        const QJsonObject found = m_scene->featuresNear(line, point, 0.3 * scale);
        const bool corner = found["corner"].isArray(), edge = found["edge"].isObject();
        // (a circular edge has a centre, and a rounding has the corner or the edge it replaced: where the flat faces round it would meet)
        const bool circle = found["circle"].isObject();
        const QJsonObject rounded = found["rounded"].toObject();
        const bool roundedCorner = rounded["corner"].isArray(), roundedEdge = rounded["edge"].isArray();
        // What is written is the place the library found -- the corner, the point of the edge -- and not the click: the call looks only about 3 % of the body round the place it is given
        // (window t332: a click 1.1 mm from an edge wrote `edge_at(block, (18.878, ...))` and the call, looking 0.6 mm round it, found no edge)
        const auto place = [](const QJsonValue& v) {
            const QJsonArray a = v.toArray();
            return QVector3D(float(a.at(0).toDouble()), float(a.at(1).toDouble()), float(a.at(2).toDouble()));
        };
        const QVector3D cornerAt = corner ? place(found["corner"]) : point;
        const QVector3D edgeAt = edge ? place(found["edge"].toObject()["point"]) : point;
        // The middle of the face the click is on, as a point (the centre of a disc: the way to put two round parts on one axis)
        {
            auto f = menu->addAction(TypeIcons::icon("point"), T("Point at the centre of this face"), this, [=]{ emit(featureRequested(line, point, "face_center")); });
            f->setToolTip(T("A point at the middle of the face of this body that the click is on: face_center(select_surface(body, (x, y, z))) -- the centre of a round face is the centre of its circle"));
        }
        if (corner || edge || roundedCorner || roundedEdge)
        {
            if (circle)
            {
                auto c = menu->addAction(TypeIcons::icon("point"), T("Point at the centre of the circle"), this, [=]{ emit(featureRequested(line, edgeAt, "circle_center")); });
                c->setToolTip(T("A point at the centre of the circular edge of this body that is nearest the click (the rim of a cylinder or a hole, the base of a cone): circle_center_at(body, (x, y, z)) -- snap parts together by their axes with it"));
            }
            if (roundedCorner)
            {
                auto r = menu->addAction(TypeIcons::icon("point"), T("Point at the rounded corner"), this, [=]{ emit(featureRequested(line, point, "corner_rounded")); });
                r->setToolTip(T("A point at the corner that the rounding of this body replaced, where the flat faces round it would meet: corner_at(body, (x, y, z), rounded=True)"));
            }
            if (roundedEdge)
            {
                auto r = menu->addAction(TypeIcons::icon("point"), T("Point on the rounded edge"), this, [=]{ emit(featureRequested(line, point, "edge_rounded")); });
                r->setToolTip(T("A point on the sharp edge that the rounding of this body replaced, where the flat faces on both sides would meet: edge_point_at(body, (x, y, z), rounded=True)"));
            }
            if (corner)
            {
                auto a = menu->addAction(TypeIcons::icon("point"), T("Point at the corner"), this, [=]{ emit(featureRequested(line, cornerAt, "corner")); });
                a->setToolTip(T("A point exactly at the corner of this body that is nearest the click: corner_at(body, (x, y, z)) -- it is found from the body's field, so it follows the body when the body changes"));
            }
            if (edge)
            {
                auto p = menu->addAction(TypeIcons::icon("point"), T("Point on the edge"), this, [=]{ emit(featureRequested(line, edgeAt, "edge_point")); });
                p->setToolTip(T("A point exactly on the edge of this body that is nearest the click: edge_point_at(body, (x, y, z)) -- any edge, a circle too"));
                if (found["edge"].toObject()["straight"].isArray())
                {
                    auto l = menu->addAction(TypeIcons::icon("curve"), T("Line along the edge"), this, [=]{ emit(featureRequested(line, edgeAt, "edge")); });
                    l->setToolTip(T("The straight edge of this body that is nearest the click, from corner to corner: edge_at(body, (x, y, z))"));
                }
            }
        }
    }
    if (m_scene)
    {
        menu->addSeparator();
        menu->addAction(T("Delete"), this, [=]{ m_scene->deleteByLine(line); });
    }
    if (keepCategoriesWithValidEntries(menu)) menu->popup(globalPos);
    else menu->deleteLater();
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
    auto radius = new Units::LengthSpinBox;       // (a length in mm that is shown in the unit chosen in Settings > Units)
    radius->setLengthDecimals(1);
    radius->setRange(0.0, 100000.0);
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

Shape* View::controlOwner(const Shape* point, int& index) const
{
    for (Shape* s : shapes)
    {
        if (!s->hasControls()) continue;
        index = s->controlOf(point);
        if (index >= 0) return s;
    }
    // A point that has no gizmo yet has no numbers to be found by: it is the control that is where its dot is
    QVector3D at;
    if (point && point->isPoint() && point->dotPosition(at))
    {
        for (Shape* s : shapes)
        {
            if (!s->hasControls()) continue;
            index = s->controlAt(at);
            if (index >= 0) return s;
        }
    }
    index = -1;
    return nullptr;
}

void View::beginPull(QPoint pos)
{
    Q_UNUSED(pos);
    if (!m_pullShape) return;
    const int after = m_pullShape->controlAfterPiece(m_pullPiece);
    if (after < 0)
    {
        m_pullShape = nullptr;
        return;
    }
    m_pullAfter = after;
    m_pullNormal = camera.towardViewer().normalized();
    double frame[9];
    if (m_pullShape->planeNow(frame))
    {
        // (a point of a profile is pulled in the plane of the profile: that is where its outline is -- unless that plane is edge on to the eye)
        const QVector3D u{float(frame[3]), float(frame[4]), float(frame[5])}, w{float(frame[6]), float(frame[7]), float(frame[8])};
        const QVector3D n = QVector3D::crossProduct(u, w).normalized();
        if (std::fabs(QVector3D::dotProduct(n, m_pullNormal)) > 0.2f) m_pullNormal = n;
    }
    m_pullLast = m_pullHit;
    m_pullActive = true;
    setCursor(Qt::ClosedHandCursor);
    setPullVirtual(m_pullHit);
    m_overlay->update();
}

void View::setPullVirtual(const QVector3D& at)
{
    // The point that is pulled out of an outline is drawn at once on every shape that stands for the same outline -- the outline itself, its placed copies, its images in a symmetry --, not
    // only on the one that was pressed: each takes it to its own place (`editMap`) and counts its points the way it has them (a mirror image runs the other way round)
    if (!m_pullShape) return;
    bool invertible = false;
    const QMatrix4x4 back = m_pullShape->editMap().inverted(&invertible);
    const QVector3D root = invertible ? back.map(at) : at;
    const int n = m_pullShape->controlCount();
    auto flip = [n](int after) { return n > 0 ? ((n - 2 - after) % n + n) % n : after; };
    const int line = editRootLine(m_pullShape);
    const int afterRoot = m_pullShape->editReversed() ? flip(m_pullAfter) : m_pullAfter;
    for (Shape* s : shapes)
    {
        if (s != m_pullShape && (!s->isEditable() || !s->hasControls() || s->controlCount() != n || editRootLine(s) != line)) continue;
        s->setVirtualPoint(true, s->editReversed() ? flip(afterRoot) : afterRoot, s == m_pullShape ? at : s->editMap().map(root));
        s->refresh();
    }
}

void View::expectNewPoint(int line0)
{
    if (!m_wait.on) return;
    if (line0 < 0) { cancelWaitingGrip(); return; }
    m_wait.lines = QList<int>{line0};
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

    // A double click while a line or a spline is being put down ends it, open (the first click of it put the last point)
    if (m_wp.open && m_wp.mode != 0 && !m_wpPlaced.isEmpty())
    {
        // (a double click on the dot of a point of a profile or a line, whose first click only started a stroke there -- one point put down: it is dropped -- changes the point
        // between a corner and a spline point, as it does when not drawing)
        if (m_wpPlaced.size() == 1)
        {
            if (Shape* dot = pointDotAt(event->pos()))
            {
                const Shape* base = (dot->gizmo().dotOnly && dot->followedShape()) ? dot->followedShape() : dot;
                if (pointStyleOf(dot) != 0 && base->sourceLine() >= 0)
                {
                    cancelPlacement();
                    emit(pointToggleRequested(base->sourceLine()));
                    return;
                }
            }
        }
        finishPlacement(false);
        return;
    }

    // A double click on the dot of a point of a profile or a curve: a corner becomes a spline point, and the other way (on the dot of an image of the path of a sweep: the point it is
    // the image of changes)
    if (Shape* dot = pointDotAt(event->pos()))
    {
        const Shape* base = (dot->gizmo().dotOnly && dot->followedShape()) ? dot->followedShape() : dot;
        if (pointStyleOf(dot) != 0 && base->sourceLine() >= 0)
        {
            emit(pointToggleRequested(base->sourceLine()));
            return;
        }
    }

    // Double-click a shape to frame it, or the background to frame all
    syncPicker();
    const auto picked = pick_img.valid(event->pos())
        ? (pick_img.pixel(event->pos()) & 0xFFFFFF) : 0;
    if (picked && int(picked) <= shapes.size())
    {
        // (a profile: the camera turns to look at its face, from the side it is on)
        double frame[9];
        if (shapes.at(picked - 1)->isFace() && shapes.at(picked - 1)->planeNow(frame))
        {
            const QVector3D u{float(frame[3]), float(frame[4]), float(frame[5])}, w{float(frame[6]), float(frame[7]), float(frame[8])};
            QVector3D n = QVector3D::crossProduct(u, w).normalized();
            if (QVector3D::dotProduct(n, camera.towardViewer()) < 0) n = -n;
            camera.lookFrom(n);
        }
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

bool View::triadTip(const QVector3D& dir, QPointF& tip) const
{
    for (const auto& h : triad_hits)
        if ((h.second - dir).length() < 1e-3f)
        {
            tip = h.first;
            return true;
        }
    return false;
}

bool View::onStandardSide()
{
    const QMatrix4x4 r = camera.rotation();
    for (int i = 0; i < 3; ++i)
    {
        QVector3D d;
        d[i] = 1;
        if (std::fabs(r.map(d).z()) > 0.9995f) return true;
    }
    return false;
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

int View::pointStyleOf(const Shape* s) const
{
    int index = -1;
    const Shape* owner = s && s->isPoint() ? controlOwner(s, index) : nullptr;
    if (owner && index >= 0 && index < int(owner->controlPoints().size()))
    {
        const auto& c = owner->controlPoints()[size_t(index)];
        const bool smooth = c.flag < 0 ? owner->smoothDefault() : c.flag != 0;
        if (!smooth && c.hasRadius) return 3;                // (a round corner: a corner with a radius -- the rotation ring is not shown for it, as for any corner)
        return smooth ? 2 : 1;
    }
    return s ? s->pointStyle() : 0;
}

bool View::planarRingNormal(const Shape* s, QVector3D& normal) const
{
    if (!s || !s->isPoint()) return false;
    int index = -1;
    const Shape* owner = controlOwner(s, index);
    double frame[9];
    if (!owner || !owner->isFace() || !owner->planeNow(frame)) return false;
    const QVector3D u{float(frame[3]), float(frame[4]), float(frame[5])}, w{float(frame[6]), float(frame[7]), float(frame[8])};
    normal = QVector3D::crossProduct(u, w).normalized();
    // (the dot of a copy of a point is a point of the ORIGINAL profile: the plane it lies in, taken through the map of the copy, is the plane the copy's outline lies in)
    if (s->gizmoMapped()) normal = s->gizmoMapLinear().mapVector(normal).normalized();
    return true;
}

bool View::gizmoFrame(const Shape* s, QVector3D F[3]) const
{
    if (gizmo_drag && gizmo_oriented && gizmo_frame_shape == s)
    {
        for (int a = 0; a < 3; ++a) F[a] = gizmo_frame[a];
        return true;
    }
    if (s && s->isSymmetry())
    {
        // A symmetry's gizmo is along its own axes -- the line in its first plane, the normal of that plane, and its axis (on a work plane: the plane's u, v and normal) -- when
        // those are not the world's.  A symmetry made on a work plane that the camera has turned is turned with it: arrows and rings that stayed along the world's axes would
        // move and turn it across its own.  (Along the world's axes already, it keeps the world's gizmo.)  It needs all six numbers, as the gizmo of a curve's point does
        const auto& h = s->gizmo();
        const auto& vars = s->getVars();
        Symmetry now;
        if (!h.present || !s->symmetryNow(now)) return false;
        for (int a = 0; a < 3; ++a)
        {
            if (!h.move[a] || !vars.count(h.move[a]) || !h.rotate[a] || !vars.count(h.rotate[a])) return false;
        }
        const QVector3D own[3] = {now.a, now.b(), now.c};
        bool world = true;
        for (const QVector3D& v : own) world = world && std::max({std::fabs(v.x()), std::fabs(v.y()), std::fabs(v.z())}) > 0.99999f;
        if (world) return false;
        for (int a = 0; a < 3; ++a) F[a] = own[a].normalized();
        return true;
    }
    if (!s || !s->isPoint()) return false;
    int index = -1;
    const Shape* owner = controlOwner(s, index);
    if (!owner || owner->isFace() || index < 0) return false;
    QVector3D T;
    if (!owner->curveTangentAt(index, T)) return false;
    // (the axis of the world that the curve is least along, across it: the same rule as the first frame of a sweep, so the frame is steady along a curve)
    int axis = 0;
    for (int i = 1; i < 3; ++i) if (std::fabs(T[i]) < std::fabs(T[axis])) axis = i;
    QVector3D ref;
    ref[axis] = 1.0f;
    QVector3D N = ref - T * QVector3D::dotProduct(ref, T);
    if (N.length() < 1e-6f) return false;
    N.normalize();
    F[0] = N;
    F[1] = QVector3D::crossProduct(T, N).normalized();
    F[2] = T;
    // (the dot of a copy of a point: the frame is the original's, taken through the map of the copy -- where the copy's own curve faces there)
    if (s->gizmoMapped())
    {
        const QMatrix4x4 L = s->gizmoMapLinear();
        for (int a = 0; a < 3; ++a) F[a] = L.mapVector(F[a]).normalized();
    }
    return true;
}

// The scale numbers of `only` (or, with none given, of every selected shape) times `factor`: every shape that is scaled and has numbers to scale it by -- a point's are what a spline leaves it by
// and how thick a sweep is at it.  False when none of them has numbers
bool View::applyWheelFactor(float factor, Shape* only)
{
    std::map<libfive::Tree::Id, float> numbers;
    auto scaleOf = [&](Shape* s) {
        if (!s->hasGizmo() || s->isLocked()) return;
        const auto& h = s->gizmo();
        const auto& vars = s->getVars();
        for (int a = 0; a < 3; ++a)
        {
            const auto it = h.scale[a] ? vars.find(h.scale[a]) : vars.end();
            if (it != vars.end()) numbers[h.scale[a]] = std::max(0.01f, it->second * factor);
        }
    };
    if (only)
    {
        scaleOf(only);
    }
    else
    {
        for (Shape* s : shapes)
        {
            if (s->isSelected()) scaleOf(s);
        }
    }
    if (numbers.empty()) return false;
    if (!m_wheelScaling)
    {
        m_wheelScaling = true;
        emit(dragStart());
    }
    applyGizmoNumbers(numbers);
    m_wheelTimer.start();
    return true;
}

// A point has the numbers of its scale once it is selected (selecting it writes the gizmo into the script and the script runs again), which takes a moment: the notches that come over it meanwhile
// are kept, as one factor, and applied when it has them -- they neither zoom the view nor get lost
void View::waitForWheelNumbers(float factor)
{
    m_wheelPending *= factor;
    m_wheelPendingTries = 0;
    if (!m_wheelPendingTimer.isActive()) m_wheelPendingTimer.start();
}

void View::applyPendingWheel()
{
    if (m_wheelPending == 1.0f) { m_wheelPendingTimer.stop(); return; }
    if (applyWheelFactor(m_wheelPending, nullptr))
    {
        m_wheelPending = 1.0f;
        m_wheelPendingTimer.stop();
        return;
    }
    // (about five seconds: when the run that writes the numbers has not come by then it is not coming)
    if (++m_wheelPendingTries > 40)
    {
        m_wheelPending = 1.0f;
        m_wheelPendingTimer.stop();
    }
}

bool View::scaleSelectionByWheel(int delta, QPoint at)
{
    if (gizmo_drag || m_pullActive || m_faceActive) return false;
    const float factor = std::pow(1.1f, float(delta) / 120.0f);
    // The point whose dot the mouse is on is scaled whether it is selected or not -- the wheel acts on what the mouse is pointing at.  One of several selected points stands for them all
    // (as a drag of its dot moves them all), and a point that is not selected is scaled alone
    Shape* hovered = pointDotAt(at);
    if (hovered && !hovered->isSelected())
    {
        if (hovered->sourceLine() < 0 || hovered->isLocked()) return false;
        if (!hovered->hasGizmo())
        {
            // (it is selected, which gives it its numbers; this notch and the ones that follow are applied when it has them)
            waitForWheelNumbers(factor);
            emit(shapeClicked(hovered->sourceLine()));
            return true;
        }
        return applyWheelFactor(factor, hovered);
    }
    if (applyWheelFactor(factor, nullptr)) return true;
    // (a point that a notch has just selected has no numbers until its script has run again: the notches that come meanwhile wait for them)
    if (m_wheelPendingTimer.isActive())
    {
        waitForWheelNumbers(factor);
        return true;
    }
    return false;
}

void View::wheelEvent(QWheelEvent *event)
{
    QOpenGLWidget::wheelEvent(event);
    event->accept();
    // Shift + wheel, or Ctrl + wheel, scales what can be scaled: the point the mouse is on (selected or not), else the selection.  When there is nothing like that the wheel zooms, as it does
    if ((event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier)) &&
        scaleSelectionByWheel(event->angleDelta().y() != 0 ? event->angleDelta().y() : event->angleDelta().x(), event->pos())) return;
    const QPoint& center = zoom_cursor_centric ? mouse.pos : rect().center();
    camera.zoomIncremental(event->angleDelta().y(), center);
    update();
    pick_timer.start();
}

void View::checkHoverTarget(QPoint pos)
{
    static const bool timing = std::getenv("FIELDES_TIMING") != nullptr;
    QElapsedTimer clock;
    if (timing) clock.start();
    struct Report
    {
        bool on;
        QElapsedTimer& c;
        ~Report()
        {
            if (on && c.nsecsElapsed() > 3000000)
                std::fprintf(stderr, "[at %lld ms] [hover] looking at what is under the mouse took %.1f ms\n", static_cast<long long>(QDateTime::currentMSecsSinceEpoch() % 100000000),
                             double(c.nsecsElapsed()) / 1e6);
        }
    } report{timing, clock};
    syncPicker();

    auto picked = pickedNear(pos);
    // (a number that is not the place of a shape in the list -- the picture is of other shapes than there are, or a pixel that is not
    // one's colour -- is nothing: every other place that reads the picture says so, and this one read past the list and wrote to what
    // it found there, which corrupted the heap while the mouse moved over a model that was still being made)
    auto target = (picked && int(picked) <= shapes.size()) ? shapes.at(picked - 1) : nullptr;
    if (target && selectedShapeCount() < 2 && pullableAt(target, pos))
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
    // (the supports of a print are one class and carry no number to read: their card says what they are)
    probe_valid = target && target->hasColorField() && target->colorMap() != "bc" && target->colorMap() != "fit" && target->colorMap() != "support" && legend_on &&
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
    // (the warning sign is painted, not written: the character U+26A0, in the interface font, falls back to a colour emoji font through DirectWrite on Windows, and a window that had shown it ended with a
    // corrupted heap -- exit code 0xC0000374 -- every time a script had an error; the model tree's error note had the same character)
    const int icon = fmHead.height() + 8;
    const int w = std::max({fmHead.horizontalAdvance(title) + icon, fmBody.horizontalAdvance(message), fmHint.horizontalAdvance(hint)}) + 28;
    const int h = fmHead.height() + fmBody.height() + fmHint.height() + 22;
    // (centred, but not under the model tree card on the left)
    int x = (width() - w) / 2;
    if (m_scene && m_scene->isVisible() && m_scene->parentWidget() == this) x = std::max(x, m_scene->geometry().right() + 14);
    x = std::max(8, std::min(x, width() - w - 8));
    m_errorRect = QRect(x, 14, w, h);
    painter.setPen(QPen(red, 1.5));
    painter.setBrush(QColor(92, 24, 24, 235));
    painter.drawRoundedRect(m_errorRect, 8, 8);
    int y = m_errorRect.top() + 7;
    painter.setPen(QColor(255, 96, 96));
    painter.setFont(head);
    {
        const double th = fmHead.height();
        QPolygonF triangle;
        triangle << QPointF(x + 14 + th / 2.0, y + 2.0) << QPointF(x + 14, y + th - 1.0) << QPointF(x + 14 + th, y + th - 1.0);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(255, 96, 96));
        painter.drawPolygon(triangle);
        painter.setPen(QColor(92, 24, 24));
        painter.drawText(QRectF(x + 14, y + 4.0, th, th - 4.0), Qt::AlignHCenter | Qt::AlignVCenter, QStringLiteral("!"));
        painter.setPen(QColor(255, 96, 96));
        painter.drawText(QRect(x + 14 + int(icon), y, w - 28 - int(icon), fmHead.height()), Qt::AlignLeft | Qt::AlignVCenter, title);
    }
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

// ---------------------------------------------------------------------------------------------------------------------------------------
// The viewers of the fields: every field has an icon in the scene -- a card with its name over it, at the place of the object it was made
// from -- while it is shown (its eye), and a viewer while it is open: a DISC of the field (a plane of a radius), centred where the field is about
// until it is taken somewhere else, by the arrows of its gizmo or by its icon -- nothing keeps it inside anything

namespace {

QString fieldLabel(const QString& key)
{
    return key.startsWith("line:") ? T("field at line %1").arg(key.mid(5)) : key;
}

const libfive::Tree::Id idOf(const libfive::Tree& t)
{
    return t.is_valid() ? t.id() : nullptr;
}

}   // anonymous namespace

View::FieldViewer* View::viewer(const QString& key) const
{
    const auto it = m_viewers.find(key);
    return it == m_viewers.end() ? nullptr : it->second.get();
}

void View::initViewer(FieldViewer* v)
{
    Plane* plane = &v->plane;
    plane->timer.setSingleShot(true);
    plane->timer.setInterval(16);
    connect(&plane->timer, &QTimer::timeout, this, [this, plane, v]{ startSliceAt(*plane, false, v); });
    connect(&plane->watcher, &QFutureWatcher<FieldSlice>::finished, this, [this, plane, v]{ onSliceFinished(*plane, v); });
}

void View::retireViewer(FieldViewer* v)
{
    if (m_iconPress == v) { m_iconPress = nullptr; m_iconDrag = false; }
    if (m_iconHover == v) m_iconHover = nullptr;
    if (m_fieldGripActive.v == v) { m_fieldGripActive = FieldGrip(); m_fieldGripDrag = false; }
    if (m_fieldGrip.v == v) m_fieldGrip = FieldGrip();
    v->plane.generation++;
    v->plane.timer.stop();
    v->plane.watcher.waitForFinished();
    if (v->plane.tex || v->plane.colorTex || v->plane.vao.isCreated() || v->plane.vbo.isCreated())
    {
        makeCurrent();
        v->plane.tex.reset();
        v->plane.colorTex.reset();
        v->plane.lutTex.reset();
        if (v->plane.vao.isCreated()) v->plane.vao.destroy();
        if (v->plane.vbo.isCreated()) v->plane.vbo.destroy();
        doneCurrent();
    }
}

// An open viewer that is shown is the card's: the one it was about if that is still so, else the first
void View::settleActive()
{
    auto ok = [&](const QString& k) { const FieldViewer* v = viewer(k); return v && v->open && v->shown; };
    if (ok(m_activeField)) return;
    m_activeField.clear();
    for (const auto& kv : m_viewers)
        if (kv.second->open && kv.second->shown) { m_activeField = kv.first; break; }
}

bool View::activeFieldOpen() const
{
    const FieldViewer* v = viewer(m_activeField);
    return v && v->open && v->shown;
}

void View::setFieldSources(QList<FieldEntry> fields)
{
    QSet<QString> keep;
    for (const auto& f : fields)
    {
        keep.insert(f.key);
        auto& slot = m_viewers[f.key];
        if (!slot)
        {
            slot.reset(new FieldViewer);
            slot->key = f.key;
            slot->open = m_wantedOpen.contains(f.key);
            initViewer(slot.get());
        }
        FieldViewer* v = slot.get();
        // (the same field as the last run: what was sampled is still right)
        const bool same = v->source.tree.is_valid() && f.source.tree.is_valid() && idOf(v->source.tree) == idOf(f.source.tree) &&
                          idOf(v->source.color) == idOf(f.source.color) && v->source.vars == f.source.vars;
        v->source = f.source;
        v->shown = f.shown;
        v->hasAnchor = f.hasCentre;
        v->anchor = f.centre;
        if (!same)
        {
            v->plane.slice = FieldSlice();      // (the script ran again: the field may be another one)
            v->plane.dirty = true;
            v->plane.quickMs = 0;
            v->rangeKnown = false;
            if (v->open && v->shown) requestFieldSlice(v);
        }
    }
    for (auto it = m_viewers.begin(); it != m_viewers.end();)
    {
        if (keep.contains(it->first)) { ++it; continue; }
        retireViewer(it->second.get());
        it = m_viewers.erase(it);
    }
    settleActive();
    if (section.enabled && section.field)
    {
        m_sec.slice = FieldSlice();         // (the section view is not touched, but the field it shows may be another one)
        requestSlice();
    }
    emit(fieldViewersChanged());
    update();
}

void View::setOpenFields(QStringList keys)
{
    m_wantedOpen = keys;
    bool changed = false;
    for (auto& kv : m_viewers)
    {
        FieldViewer* v = kv.second.get();
        const bool want = keys.contains(kv.first);
        if (want == v->open) continue;
        v->open = want;
        if (want && v->shown) requestFieldSlice(v);
        if (!want) v->plane.generation++;       // (what is being sampled for a viewer that is closed is not wanted)
        changed = true;
    }
    // (the value read under the mouse was a viewer's, and it may be the one that went: it is read again at the next move)
    if (changed && !m_fieldText.isEmpty())
    {
        m_fieldText.clear();
        emit(fieldReadout(QString()));
    }
    settleActive();
    emit(fieldViewersChanged());
    update();
}

void View::setActiveField(QString key)
{
    const FieldViewer* v = viewer(key);
    if (!v || !v->open || !v->shown || key == m_activeField) return;
    m_activeField = key;
    emitActiveSlice();
    emit(fieldViewersChanged());
}

void View::emitActiveSlice()
{
    if (const FieldViewer* v = viewer(m_activeField)) emit(fieldSliceReady(v->plane.slice));
}

QStringList View::openFieldKeys() const
{
    QStringList out;
    for (const auto& kv : m_viewers)
        if (kv.second->open && kv.second->shown) out << kv.first;
    return out;
}

// The middle of the disc (and the place of the icon): where it was taken to, else where the field is about -- the place of the object it was made
// from, the origin when it was not made from one.  A field is about all of space: the way to see it is to take the disc to where it is interesting
QVector3D View::fieldPlace(const FieldViewer& v) const
{
    return v.moved ? v.centre : (v.hasAnchor ? v.anchor : QVector3D());
}

float View::fieldRadius(const FieldViewer& v) const
{
    if (v.radius > 0) return v.radius;
    const QVector3D size = settings.max - settings.min;
    return 0.5f * std::max({size.x(), size.y(), size.z(), 1e-3f});
}

SectionSettings View::fieldPlane(const FieldViewer& v) const
{
    // The viewer's plane is a plane like the section's, without the cut: it clips nothing and keeps or cuts no element
    SectionSettings s;
    s.enabled = true;
    s.axis = v.axis;
    s.offset = fieldPlace(v)[v.axis];
    s.flip = false;
    s.clip = false;
    s.field = true;
    s.opacity = v.opacity;
    return s;
}

FieldViewSettings View::fieldSettings(const FieldViewer& v) const
{
    FieldViewSettings s;
    s.axis = v.axis;
    s.centre = fieldPlace(v);
    s.radius = fieldRadius(v);
    s.opacity = v.opacity;
    return s;
}

// (the render region is what the radius slider of the card ranges over)
bool View::fieldViewer(const QString& key, FieldViewSettings& s, QVector3D& regionMin, QVector3D& regionMax) const
{
    const FieldViewer* v = viewer(key);
    if (!v) return false;
    s = fieldSettings(*v);
    regionMin = settings.min;
    regionMax = settings.max;
    return true;
}

void View::setFieldView(QString key, FieldViewSettings s)
{
    FieldViewer* v = viewer(key);
    if (!v) return;
    v->axis = std::max(0, std::min(2, s.axis));
    v->opacity = s.opacity;
    if (s.radius > 0) v->radius = s.radius;
    if (v->moved || !(s.centre == fieldPlace(*v)))
    {
        v->moved = true;
        v->centre = s.centre;
    }
    if (v->open && v->shown) requestFieldSlice(v);
    update();
}

bool View::fieldIconPoint(const QString& key, QPoint& out) const
{
    const FieldViewer* v = viewer(key);
    if (!v || !v->shown) return false;
    const auto layout = iconLayout();
    const auto it = layout.constFind(key);
    if (it == layout.constEnd()) return false;
    out = QPoint(int(std::lround(it.value().x())), int(std::lround(it.value().y())));
    return true;
}

// (for the automation: a point that grips part 1, 2 (the arrows in the plane), 3 (the dot) or 4 (the arrow along the normal) of the disc's gizmo)
bool View::fieldGripPoint(const QString& key, int part, QPoint& out) const
{
    const FieldViewer* v = viewer(key);
    if (!v || !v->shown || !v->open) return false;
    QPointF m, u, w, n;
    float len;
    if (!fieldGizmo(*v, m, u, w, n, len)) return false;
    const QPointF p = part == 1 ? m + 0.6 * (u - m) : part == 2 ? m + 0.6 * (w - m) : part == 4 ? m + 0.7 * (n - m) : m;
    out = QPoint(int(std::lround(p.x())), int(std::lround(p.y())));
    return fieldGizmoHit(*v, out) == part;
}

void View::requestSlice()
{
    if (section.enabled && section.field)
    {
        // (a throttle, not a debounce: see requestFieldSlice)
        if (!m_sec.timer.isActive()) m_sec.timer.start();
    }
}

void View::requestFieldSlice(FieldViewer* v)
{
    if (v && v->open && v->shown)
    {
        // The first request arms the timer, and the ones that come before it fires add nothing: the pass samples the state it finds
        // when it starts.  Starting the timer again at every request waited for the hand to rest, so a drag that moves faster than
        // the timer's interval showed nothing until it stopped -- the plane lagged behind the mouse
        if (!v->plane.timer.isActive()) v->plane.timer.start();
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
    src.exactKey = s->exactKey();
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

void View::startSliceAt(Plane& plane, bool fine, FieldViewer* viewer)
{
    const bool fieldMode = viewer != nullptr;
    if (fieldMode ? !(viewer->open && viewer->shown) : !(section.enabled && section.field)) return;      // (switched off while this waited)
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
        // (the field of this viewer is the only thing its plane shows)
        FieldSource src = viewer->source;
        src.map = "section";            // (a field is shown in the section's colours and dark lines: one look for what the planes show)
        if (viewer->rangeKnown)
        {
            src.lo = viewer->range.first;
            src.hi = viewer->range.second;
        }
        else rangeKnown = false;
        if (!viewer->key.startsWith("line:")) src.label = viewer->key;
        {
            // (the plane is a DISC: the "model" it shows the field in is the disc, its distance field -- negative inside it)
            const int ax = viewer->axis, ua = FieldSlice::uAxis(ax), va = FieldSlice::vAxis(ax);
            const QVector3D middle = fieldPlace(*viewer);
            const libfive::Tree axes[3] = {libfive::Tree::X(), libfive::Tree::Y(), libfive::Tree::Z()};
            using libfive::Tree;
            using libfive::Opcode::OP_ADD; using libfive::Opcode::OP_MUL; using libfive::Opcode::OP_SQRT; using libfive::Opcode::OP_SUB;
            const Tree du = Tree::binary(OP_SUB, axes[ua], Tree(double(middle[ua])));
            const Tree dv = Tree::binary(OP_SUB, axes[va], Tree(double(middle[va])));
            const Tree r2 = Tree::binary(OP_ADD, Tree::binary(OP_MUL, du, du), Tree::binary(OP_MUL, dv, dv));
            src.tree = Tree::binary(OP_SUB, Tree::unary(OP_SQRT, r2), Tree(double(fieldRadius(*viewer))));
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
    const SectionSettings sec = fieldMode ? fieldPlane(*viewer) : section;
    QVector3D lo, hi;
    float fade;
    if (fieldMode)
    {
        // The viewer's disc: the square round its middle (the disc is cut out of it by its own distance field)
        const float R = fieldRadius(*viewer);
        const QVector3D middle = fieldPlace(*viewer);
        lo = middle - QVector3D(R, R, R);
        hi = middle + QVector3D(R, R, R);
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
    const auto exactResolver = m_exactResolver;
    const auto exactMutex = m_exactMutex;
    const auto exactTrees = m_exactTrees;
    plane.watcher.setFuture(QtConcurrent::run([=]() {
        const auto t0 = std::chrono::steady_clock::now();
        QVector<FieldSource> used = sources;
        // An imported part's own field is exact in sign only: outside it is the boxy distance to the infinite surfaces of its faces, and
        // inside it has the ghosts of faces that end elsewhere.  What a plane shows is its distance, so the plane asks for the exact one
        // (made the first time, about a second a part, and kept)
        for (auto& u : used)
        {
            if (u.exactKey.isEmpty() || !exactResolver) continue;
            libfive::Tree t = libfive::Tree::invalid();
            {
                QMutexLocker lock(exactMutex.get());
                const auto it = exactTrees->constFind(u.exactKey);
                if (it != exactTrees->constEnd()) t = *it;
            }
            if (!t.is_valid())
            {
                t = exactResolver(u.exactKey);
                if (t.is_valid())
                {
                    QMutexLocker lock(exactMutex.get());
                    exactTrees->insert(u.exactKey, t);
                }
            }
            if (t.is_valid()) u.tree = t;
        }
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

void View::onSliceFinished(Plane& plane, FieldViewer* viewer)
{
    const bool fieldMode = viewer != nullptr;
    const FieldSlice s = plane.watcher.result();
    if (s.generation == plane.generation.load() && s.valid())
    {
        if (!s.fine && plane.clock.isValid()) plane.quickMs = double(plane.clock.elapsed());
        if (fieldMode && s.hasColor && !viewer->rangeKnown)
        {
            viewer->rangeKnown = true;
            viewer->range = qMakePair(s.colorLo, s.colorHi);       // (found once for the field, kept)
        }
        plane.slice = s;
        plane.dirty = true;
        if (fieldMode)
        {
            if (viewer->key == m_activeField) emit(fieldSliceReady(plane.slice));
        }
        else emit(sliceReady(plane.slice));
        update();
        if (!s.fine && !plane.timer.isActive())
        {
            startSliceAt(plane, true, viewer);
        }
    }
    else if ((fieldMode ? (viewer->open && viewer->shown) : (section.enabled && section.field)) && s.generation != plane.generation.load())
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
        plane.lutTex.reset();
        if (plane.slice.hasColor && plane.slice.color.size() == plane.slice.values.size())
        {
            // the colours of the model's own map as a table: the plane shows what the surface shows (the change map's three classes, a rule's two),
            // not a second copy of the maps in the shader, which painted every map it did not know in the rainbow
            constexpr int kLut = 1024;
            QImage lut(kLut, 1, QImage::Format_RGBA8888);
            for (int i = 0; i < kLut; ++i) lut.setPixelColor(i, 0, colormapColor(plane.slice.colorMap, float(i) / float(kLut - 1)));
            plane.lutTex.reset(new QOpenGLTexture(lut, QOpenGLTexture::DontGenerateMipMaps));
            plane.lutTex->setMinificationFilter(QOpenGLTexture::Nearest);
            plane.lutTex->setMagnificationFilter(QOpenGLTexture::Nearest);
            plane.lutTex->setWrapMode(QOpenGLTexture::ClampToEdge);
            plane.colorTex.reset(new QOpenGLTexture(QOpenGLTexture::Target2D));
            plane.colorTex->setFormat(QOpenGLTexture::R32F);
            plane.colorTex->setSize(plane.slice.w, plane.slice.h);
            plane.colorTex->allocateStorage(QOpenGLTexture::Red, QOpenGLTexture::Float32);
            // (nothing to colour there -- another model is nearer, or outside -- is a very large number and not NaN: the shader cannot be relied on to see a NaN, and on this GPU it read one as the
            // lowest colour of the map, so that the copy of a part beside a result was cut in the near-black of the rainbow, the blue of "removed" since)
            QVector<float> colours = plane.slice.color;
            for (auto& c : colours)
            {
                if (!std::isfinite(c)) c = 1e30f;
            }
            plane.colorTex->setData(QOpenGLTexture::Red, QOpenGLTexture::Float32, colours.constData());
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
        colorMode = plane.slice.colorMap == "section" ? 4 : 1;
        plane.colorTex->bind(1);
        glUniform1i(Shader::slice->uniformLocation("color_field"), 1);
        if (plane.lutTex) plane.lutTex->bind(2);
        glUniform1i(Shader::slice->uniformLocation("color_lut"), 2);
        glUniform1f(Shader::slice->uniformLocation("color_lo"), plane.slice.colorLo);
        glUniform1f(Shader::slice->uniformLocation("color_hi"), plane.slice.colorHi);
    }
    glUniform1i(Shader::slice->uniformLocation("color_mode"), colorMode);
    if (deformed) glDrawArrays(GL_TRIANGLES, 0, plane.gridVerts);
    else glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    if (plane.colorTex) plane.colorTex->release(1);
    if (plane.lutTex) plane.lutTex->release(2);
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

// The plane through the place of a point that faces the eye, and where the cursor's line of sight meets it
QVector3D View::rayHitFacing(QPoint pos, const QVector3D& through) const
{
    const QVector3D a = toModelPos(pos, 0), b = toModelPos(pos, 0.25);
    const QVector3D n = camera.towardViewer();
    const float denom = QVector3D::dotProduct(b - a, n);
    if (!std::isfinite(denom) || std::abs(denom) < 1e-9f) return through;
    return a + (QVector3D::dotProduct(through - a, n) / denom) * (b - a);
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
// Gizmo of placed parts

namespace {

const QColor kGizmoColor[3] = {QColor(0xdc, 0x32, 0x2f), QColor(0x74, 0xb8, 0x1f), QColor(0x26, 0x8b, 0xd2)};

// The axes the three rotation rings turn about (turning right-handed about
// each is a positive change of its angle).  The part turns by Rz * Ry * Rx, so
// the z ring is the world's z, the y ring's is z-turned, and the x ring's has
// both turns applied: a gimbal.  (The library's y rotation goes the other
// way about +y, hence the minus.)
void gizmoAxes(const QVector3D& turn, QVector3D ring[3])
{
    const QMatrix4x4 rz = Shape::gizmoRotation(QVector3D(0, 0, turn.z()));
    const QMatrix4x4 rzy = Shape::gizmoRotation(QVector3D(0, turn.y(), turn.z()));
    ring[2] = QVector3D(0, 0, 1);
    ring[1] = rz.mapVector(QVector3D(0, -1, 0));
    ring[0] = rzy.mapVector(QVector3D(1, 0, 0));
}

// The axes of the three rings of a shape: those of its angles, and for the image of a model those taken through the symmetry
void gizmoRingAxes(const Shape* s, QVector3D ring[3])
{
    gizmoAxes(s->gizmoRotate(), ring);
    if (!s->gizmoMapped()) return;
    const QMatrix4x4 L = s->gizmoMapLinear();
    for (int a = 0; a < 3; ++a) ring[a] = L.mapVector(ring[a]);
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
const float kPlaneFrom = 0.20f, kPlaneTo = 0.52f;     // (a plane square of the Shift gizmo: from and to, as shares of the arrow's length, along both its axes)
const QColor kUniformColor(0xe0, 0xa8, 0x20);          // (the one scale knob of a point and of the Shift gizmo: it scales all three together)
// The uniform scale handle -- of a point, and of the Shift gizmo of any shape -- is a RING round the middle of the gizmo, through the places the axis knobs sit (as the
// scale gizmo of Blender has: a circle round the middle that scales all three axes together; Maya, Unity and Unreal use a cube in the middle, which here is the dot that
// moves a point).  Dragged, the part is scaled by how far the cursor is from the middle against how far it was where the ring was pressed
const double kUniformRingPx = 80.0 * kKnobAt;           // (its radius on the screen: the arrows are about 80 px)

QVector3D unitAxis(int a)
{
    QVector3D e;
    e[a] = 1;
    return e;
}

// The three angles (degrees) the gizmo writes for a turn: the inverse of Shape::gizmoRotation, which turns by Rz * Ry * Rx (with the library's y turning the way it does)
QVector3D gizmoEuler(const QMatrix4x4& R)
{
    const double sy = std::max(-1.0, std::min(1.0, double(R(2, 0))));
    double ax, az;
    const double ay = std::asin(sy);
    if (std::fabs(sy) < 0.999999)
    {
        ax = std::atan2(double(R(2, 1)), double(R(2, 2)));
        az = std::atan2(double(R(1, 0)), double(R(0, 0)));
    }
    else
    {
        ax = 0;
        az = std::atan2(-double(R(0, 1)), double(R(1, 1)));
    }
    const double deg = 180.0 / M_PI;
    return QVector3D(float(ax * deg), float(ay * deg), float(az * deg));
}

}   // anonymous namespace

void View::placeSymmetry(const Symmetry& target)
{
    // The symmetry put where `target` says: the numbers of the gizmo of its model -- the turn that takes its own axes to the target's, and how far its centre moves
    Shape* s = symmetryShape();
    if (!s) return;
    const auto& d = s->symmetryDef();
    const auto& h = s->gizmo();
    if (!h.present)
    {
        emit(symmetryNotice(T("The symmetry has no gizmo yet: select it in the model tree, and try again.")));
        return;
    }
    Symmetry own;
    own.c = d.axis;
    own.a = d.first;
    own.normalise();
    Symmetry want = target;
    want.normalise();
    auto columns = [](const Symmetry& q) {
        const QVector3D b = q.b();
        return QMatrix4x4(q.a.x(), b.x(), q.c.x(), 0, q.a.y(), b.y(), q.c.y(), 0, q.a.z(), b.z(), q.c.z(), 0, 0, 0, 0, 1);
    };
    const QMatrix4x4 R = columns(want) * columns(own).transposed();
    const QVector3D angles = gizmoEuler(R);
    const auto& vars = s->getVars();
    std::map<libfive::Tree::Id, float> numbers;
    for (int a = 0; a < 3; ++a)
    {
        if (h.rotate[a] && vars.count(h.rotate[a])) numbers[h.rotate[a]] = angles[a];
        if (h.move[a] && vars.count(h.move[a])) numbers[h.move[a]] = want.centre[a] - d.centre[a];
    }
    if (numbers.empty()) return;
    emit(dragStart());
    applyGizmoNumbers(numbers);
    emit(dragEnd());
}

View::FieldViewer* View::fieldIconAt(QPoint pos) const
{
    FieldViewer* best = nullptr;
    double bestD = 15.0 * 15.0;
    const auto layout = iconLayout();
    for (const auto& kv : m_viewers)
    {
        FieldViewer* v = kv.second.get();
        const auto it = layout.constFind(kv.first);
        if (it == layout.constEnd()) continue;
        const double dx = it.value().x() - pos.x(), dy = it.value().y() - pos.y();
        const double d = dx * dx + dy * dy;
        if (d < bestD) { bestD = d; best = v; }
    }
    return best;
}

// The icon is taken where the cursor goes, in the plane through it that faces the eye -- and the disc with it: nothing keeps it inside anything
void View::dragFieldIcon(QPoint pos)
{
    if (!m_iconPress) return;
    FieldViewer* v = m_iconPress;
    const QVector3D c = rayHitFacing(pos - m_iconOffset.toPoint(), fieldPlace(*v)) + m_iconGrab;
    v->moved = true;
    v->centre = c;
    if (v->open) requestFieldSlice(v);
    emit(fieldMoved(v->key, fieldSettings(*v)));
    update();
}

// Where the icons are drawn: at the place of the field -- and moved aside, to the nearest of eight spots round it that is free, when the dot of a
// point or the icon of another field is there.  A field is made from a point more often than not and its icon starts on it: the dot must stay
// something that can be clicked.  (An icon that is being dragged keeps the distance it had when it was taken.)
QHash<QString, QPointF> View::iconLayout() const
{
    QHash<QString, QPointF> out;
    if (m_viewers.empty()) return out;
    const QMatrix4x4 M = camera.M();
    auto inFront = [&](const QVector3D& p) {
        return M.row(3).x() * p.x() + M.row(3).y() * p.y() + M.row(3).z() * p.z() + M.row(3).w() > 1e-6f;
    };
    std::vector<QPointF> taken;
    for (Shape* s : shapes)
    {
        QVector3D p;
        if (!s->isPoint() || !s->dotPosition(p) || !inFront(p)) continue;
        const QPointF q = toScreen(p);
        if (q.x() > -60 && q.y() > -60 && q.x() < width() + 60 && q.y() < height() + 60) taken.push_back(q);
    }
    auto isFree = [&](const QPointF& c) {
        for (const QPointF& t : taken)
            if (std::hypot(c.x() - t.x(), c.y() - t.y()) < 26.0) return false;
        return true;
    };
    // (the middle of the disc of an open viewer is the dot of its gizmo: that is something that can be pressed, and its icon stands aside)
    for (const auto& kv : m_viewers)
    {
        const FieldViewer* v = kv.second.get();
        if (!v->open || !v->shown) continue;
        const QVector3D place = fieldPlace(*v);
        if (!inFront(place)) continue;
        const QPointF q = toScreen(place);
        if (std::isfinite(q.x()) && std::isfinite(q.y())) taken.push_back(q);
    }
    static const QPointF steps[] = {{26, -26}, {-26, -26}, {26, 26}, {-26, 26}, {40, 0}, {-40, 0}, {0, -44}, {0, 44}};
    for (const auto& kv : m_viewers)
    {
        const FieldViewer* v = kv.second.get();
        if (!v->shown) continue;
        const QVector3D place = fieldPlace(*v);
        if (!inFront(place)) continue;
        QPointF q = toScreen(place);
        if (!std::isfinite(q.x()) || !std::isfinite(q.y())) continue;
        if (m_iconDrag && v == m_iconPress)
        {
            q += m_iconOffset;
        }
        else if (!isFree(q))
        {
            for (const QPointF& step : steps)
                if (isFree(q + step)) { q += step; break; }
        }
        out.insert(kv.first, q);
        taken.push_back(q);
    }
    return out;
}

bool View::fieldGizmo(const FieldViewer& v, QPointF& middle, QPointF& tipU, QPointF& tipV, QPointF& tipN, float& length) const
{
    const int ax = v.axis, ua = FieldSlice::uAxis(ax), va = FieldSlice::vAxis(ax);
    length = 0.6f * fieldRadius(v);
    const QVector3D c = fieldPlace(v);
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

int View::fieldGizmoHit(const FieldViewer& v, QPoint pos) const
{
    QPointF m, u, w, n;
    float len;
    if (!fieldGizmo(v, m, u, w, n, len)) return 0;
    const QPointF p(pos);
    if (std::hypot(p.x() - m.x(), p.y() - m.y()) < 9) return 3;                // (the dot in the middle)
    if (distanceToSegment(p, m, u) < 9) return 1;
    if (distanceToSegment(p, m, w) < 9) return 2;
    if (onArrow(pos, m, n)) return 4;                                          // (the double arrow along the normal)
    return 0;
}

View::FieldGrip View::fieldGripAt(QPoint pos) const
{
    FieldGrip best;
    for (const auto& kv : m_viewers)
    {
        FieldViewer* v = kv.second.get();
        if (!v->open || !v->shown) continue;
        const int part = fieldGizmoHit(*v, pos);
        if (part) { best.v = v; best.part = part; if (v->key == m_activeField) break; }
    }
    return best;
}

void View::beginFieldGrip(const FieldGrip& g, QPoint pos)
{
    FieldViewer* v = g.v;
    QPointF m, u, w, n;
    float len;
    if (!fieldGizmo(*v, m, u, w, n, len)) return;
    m_fieldGripActive = g;
    m_fgPress = pos;
    m_fgStart = fieldPlace(*v);
    m_fgLen = len;
    m_fgDir = g.part == 1 ? u - m : g.part == 2 ? w - m : n - m;
    m_fgGrab = QVector3D();
    if (g.part == 3)
    {
        // (the dot: the disc follows the cursor in its plane, and keeps the distance it was from the cursor when it was taken)
        const int ax = v->axis, ua = FieldSlice::uAxis(ax), va = FieldSlice::vAxis(ax);
        const QVector3D a = toModelPos(pos, 0), b = toModelPos(pos, 0.25);
        if (b[ax] != a[ax])
        {
            const QVector3D hit = a + (m_fgStart[ax] - a[ax]) / (b[ax] - a[ax]) * (b - a);
            m_fgGrab[ua] = m_fgStart[ua] - hit[ua];
            m_fgGrab[va] = m_fgStart[va] - hit[va];
        }
    }
    m_fieldGripDrag = true;
    setCursor(Qt::ClosedHandCursor);
    m_overlay->update();
}

// An arrow takes the disc along its axis by as much as the mouse went along the arrow on the screen (the one along the normal moves the disc through the
// field); the dot takes it where the cursor goes in its plane.  Nothing keeps it inside the render region or anywhere else
void View::dragFieldGrip(QPoint pos)
{
    FieldViewer* v = m_fieldGripActive.v;
    if (!v) return;
    const int ax = v->axis, ua = FieldSlice::uAxis(ax), va = FieldSlice::vAxis(ax);
    QVector3D c = m_fgStart;
    const int part = m_fieldGripActive.part;
    if (part == 1 || part == 2 || part == 4)
    {
        const double l2 = QPointF::dotProduct(m_fgDir, m_fgDir);
        if (l2 < 1) return;
        const double t = QPointF::dotProduct(QPointF(pos - m_fgPress), m_fgDir) / l2;
        const int a = part == 1 ? ua : part == 2 ? va : ax;
        c[a] = float(m_fgStart[a] + t * m_fgLen);
    }
    else
    {
        const QVector3D a = toModelPos(pos, 0), b = toModelPos(pos, 0.25);
        if (b[ax] == a[ax] || !std::isfinite(a[ax]) || !std::isfinite(b[ax])) return;
        const float t = (m_fgStart[ax] - a[ax]) / (b[ax] - a[ax]);
        const QVector3D hit = a + t * (b - a);
        c[ua] = hit[ua] + m_fgGrab[ua];
        c[va] = hit[va] + m_fgGrab[va];
    }
    v->moved = true;
    v->centre = c;
    if (v->open) requestFieldSlice(v);
    emit(fieldMoved(v->key, fieldSettings(*v)));
    update();
}

void View::drawFieldGizmo(QPainter& painter, const FieldViewer& v)
{
    QPointF m, u, w, n;
    float len;
    if (!fieldGizmo(v, m, u, w, n, len)) return;
    const int ax = v.axis, ua = FieldSlice::uAxis(ax), va = FieldSlice::vAxis(ax);
    int part = 0;                                                              // (the one that is held, else the one under the cursor)
    if (m_fieldGripDrag && m_fieldGripActive.v == &v) part = m_fieldGripActive.part;
    else if (m_fieldGrip.v == &v) part = m_fieldGrip.part;
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
        const QPointF d = dir / l, nrm(-d.y(), d.x());
        QPolygonF head;
        head << to + d * 4 << to - d * 9 + nrm * 6 << to - d * 9 - nrm * 6;
        painter.setPen(QPen(QColor(0, 0, 0, 90), 1));
        painter.setBrush(col);
        painter.drawPolygon(head);
    };
    arrow(m, u, kGizmoColor[ua], part == 1);
    arrow(m, w, kGizmoColor[va], part == 2);
    painter.setPen(QPen(part == 3 ? Qt::white : QColor(0xee, 0xe8, 0xd5), 1.5));
    painter.setBrush(part == 3 || part == 4 ? QColor(80, 170, 240) : QColor(38, 139, 210));
    painter.drawEllipse(m, 6.5, 6.5);
    painter.restore();
}

// The icons of the fields, each with its name over it as a card in the style of the labels of boundary conditions (in the green of fields):
// so that where there are several they are told apart at a glance.  An open viewer's icon is lit up, and its disc has a gizmo
void View::drawFieldIcons(QPainter& painter)
{
    if (m_viewers.empty()) return;
    const auto layout = iconLayout();
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    QFont font = painter.font();
    font.setPointSizeF(9);
    font.setBold(true);
    painter.setFont(font);
    const QFontMetrics fm(font);
    const QColor green = TypeIcons::color("field");
    const QIcon icon = TypeIcons::icon("field");
    for (const auto& kv : m_viewers)
    {
        const FieldViewer* v = kv.second.get();
        const auto where = layout.constFind(kv.first);
        if (where == layout.constEnd()) continue;
        const QPointF q = where.value();
        if (!(q.x() > -40 && q.y() > -40 && q.x() < width() + 40 && q.y() < height() + 40)) continue;
        const bool hot = v->open || v == m_iconHover || (m_iconDrag && v == m_iconPress);
        if (v->open)
        {
            // The gizmo of the viewer's disc, at its middle (the icon stands aside from it)
            drawFieldGizmo(painter, *v);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(green.red(), green.green(), green.blue(), 70));
            painter.drawEllipse(q, 19, 19);
        }
        painter.setPen(QPen(hot ? QColor(Qt::white) : QColor(0xee, 0xe8, 0xd5), 1.5));
        painter.setBrush(QColor(22, 76, 94, 235));
        painter.drawEllipse(q, 13, 13);
        icon.paint(&painter, QRect(int(std::lround(q.x())) - 9, int(std::lround(q.y())) - 9, 18, 18));
        // the name, over the icon
        const QString text = fieldLabel(v->key);
        const QSizeF sz(fm.horizontalAdvance(text) + 14, fm.height() + 6);
        QRectF box(q.x() - sz.width() / 2, q.y() - 17 - sz.height(), sz.width(), sz.height());
        box.moveLeft(std::max(2.0, std::min(double(width()) - sz.width() - 2, box.left())));
        box.moveTop(std::max(2.0, box.top()));
        painter.setPen(QPen(green, 1.5));
        painter.setBrush(QColor(22, 76, 94, 225));
        painter.drawRoundedRect(box, 4, 4);
        painter.setPen(QColor(0xee, 0xe8, 0xd5));
        painter.drawText(box, Qt::AlignCenter, text);
    }
    painter.restore();
}

float View::gizmoLength(const QVector3D& pivot) const
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
    // (the images of one statement -- `leg_images = symmetry_images(leg, mirror)` is drawn as a shape for each image, and selecting its row selects them all -- are one selection:
    // a surface of one of them can be pulled, which pulls the model they are the images of)
    int n = 0;
    QSet<int> imageLines;
    for (const auto* s : shapes)
    {
        if (!s->isSelected()) continue;
        if (s->isSymmetryImage() && s->sourceLine() >= 0)
        {
            if (imageLines.contains(s->sourceLine())) continue;
            imageLines.insert(s->sourceLine());
        }
        ++n;
    }
    return n;
}

QString View::hoverDescription(QPoint pos) const
{
    const auto picked = pick_img.valid(pos) ? pickedNear(pos) : 0;
    const Shape* s = (picked && int(picked) <= shapes.size()) ? shapes.at(int(picked) - 1) : nullptr;
    if (!s) return QString("nothing under (%1, %2); %3 selected; hover shape %4").arg(pos.x()).arg(pos.y()).arg(selectedShapeCount()).arg(hover_target ? "yes" : "none");
    return QString("shape line %1 under (%2, %3): pullable %4, symmetry image %5, gizmo %6; %7 selected; hover shape %8")
        .arg(s->sourceLine()).arg(pos.x()).arg(pos.y()).arg(pullableAt(s, pos) ? "yes" : "no").arg(s->isSymmetryImage() ? "yes" : "no").arg(s->hasGizmo() ? "yes" : "no")
        .arg(selectedShapeCount()).arg(hover_target == s ? "is it" : hover_target ? "is another" : "none");
}

bool View::groupGizmo(QList<Shape*>& members, QVector3D& center) const
{
    // The multi-select state: two or more shapes selected.  The ones that can be moved (they have move numbers) share
    // one gizmo, whatever gizmo mode each has when it is selected alone -- unless all of them say never
    members.clear();
    int selected = 0;
    bool anyShown = false, anyLocked = false;
    const QList<const Shape*> outlines = outlinesWithGizmo();           // (the profiles and lines whose gizmo is shown: shapes drawn from the numbers of their points)
    for (Shape* s : shapes)
    {
        // (the dots of the paths that are drawn with the images of a sweep are pressed and dragged, they are not part of a selection of the row)
        if (!s->isSelected() || s->gizmo().dotOnly) continue;
        ++selected;
        anyLocked |= s->isLocked();
        if (!s->hasMoveVars()) continue;
        members << s;
        anyShown |= s->gizmo().mode != Shape::Gizmo::NEVER;
    }
    // A profile or a line has no gizmo of its own -- its points have the numbers that place it -- so, where its gizmo is shown, the points it is made of join the shared gizmo,
    // selected themselves or not: the gizmo of the outline moves, turns and scales them all together.  The mode of the outline decides whether it is shown; the modes of its
    // points are for the points
    if (!outlines.isEmpty())
    {
        for (Shape* p : shapes)
        {
            if (!p->isPoint() || p->gizmo().dotOnly || members.contains(p)) continue;
            bool ofOutline = false;
            for (const Shape* o : outlines) ofOutline = ofOutline || o->controlOf(p) >= 0;
            if (!ofOutline) continue;
            anyLocked |= p->isLocked();
            if (!p->hasMoveVars()) continue;
            members << p;
        }
        anyShown = true;
        selected = std::max(selected, 2);
    }
    // (a locked shape in the selection: the selection is not moved or edited -- there is no gizmo for it)
    if (selected < 2 || anyLocked || members.isEmpty() || !anyShown) return false;
    QVector3D lo, hi;
    bool first = true;
    for (const Shape* s : members)
    {
        const QVector3D P = s->gizmoPivot();
        lo = first ? P : QVector3D(std::min(lo.x(), P.x()), std::min(lo.y(), P.y()), std::min(lo.z(), P.z()));
        hi = first ? P : QVector3D(std::max(hi.x(), P.x()), std::max(hi.y(), P.y()), std::max(hi.z(), P.z()));
        first = false;
    }
    center = 0.5f * (lo + hi);
    return true;
}

// Whether every one of these shapes is a point of a profile that lies in the same plane (and which, as the way its normal faces: the profile's own)
bool View::groupInProfilePlane(const QList<Shape*>& members, QVector3D& normal) const
{
    bool first = true;
    for (const Shape* s : members)
    {
        QVector3D n;
        if (!planarRingNormal(s, n)) return false;
        if (first) normal = n;
        else if (std::fabs(QVector3D::dotProduct(n, normal)) < 0.999f) return false;
        first = false;
    }
    return !first;
}

// Whether the selection is a group of points (two or more) that share one gizmo
bool View::groupOfPoints(const QList<Shape*>& members) const
{
    if (members.size() < 2) return false;
    for (const Shape* s : members)
    {
        if (!s->isPoint() || s->gizmoMapped()) return false;          // (the rings turn and scale the points by numbers of their own: an image's are its original's)
    }
    return true;
}

// The profiles and lines whose gizmo is shown (see groupGizmo), by the mode of the outline itself (the mode of its gizmo button, never until it says otherwise): click while it
// is selected as a whole, always.  An outline that is always shown but not selected has its gizmo only while nothing else is selected: the gizmo that its points share cannot be
// shown beside the gizmo of another selection
QList<const Shape*> View::outlinesWithGizmo() const
{
    QList<const Shape*> out;
    bool anySelected = false;
    for (const Shape* s : shapes) anySelected = anySelected || (s->isSelected() && !s->gizmo().dotOnly);
    for (const Shape* s : shapes)
    {
        if (!s->hasControls() || s->isPoint() || s->gizmo().dotOnly) continue;
        const Shape::Gizmo::Mode mode = s->outlineMode();
        if (mode == Shape::Gizmo::NEVER) continue;
        if (s->isSelected() || (mode == Shape::Gizmo::ALWAYS && !anySelected && !s->isLocked())) out << s;
    }
    return out;
}

// Whether the shared gizmo is that of a profile or a line (see groupGizmo): arrows in all three directions, and rings that turn and scale the arrangement of its points about
// the middle of them -- and not that of a few points, whose rings change each point's own numbers
bool View::outlineSelected() const
{
    return !outlinesWithGizmo().isEmpty();
}

QVector3D View::groupRingNormal(const QList<Shape*>& members) const
{
    QVector3D normal;
    if (groupInProfilePlane(members, normal)) return normal.normalized();
    if (m_wp.open && m_wp.exists) return m_wp.normal();
    return camera.towardViewer().normalized();
}

namespace {

// Whether any of these shapes has a number that moves it along an axis
bool anyMoveVar(const QList<Shape*>& shapes, int a)
{
    for (const Shape* s : shapes)
    {
        const auto& h = s->gizmo();
        if (h.move[a] && s->getVars().count(h.move[a])) return true;
    }
    return false;
}

}   // anonymous namespace

unsigned int View::pickedNear(QPoint pos) const
{
    if (!pick_img.valid(pos)) return 0;
    const unsigned int here = pick_img.pixel(pos) & 0xFFFFFF;
    if (here) return here;
    unsigned int best = 0;
    int bestD = 1 << 30;
    const int R = 12;
    for (int dy = -R; dy <= R; ++dy)
        for (int dx = -R; dx <= R; ++dx)
        {
            const QPoint q(pos.x() + dx, pos.y() + dy);
            if (!pick_img.valid(q)) continue;
            const unsigned int v = pick_img.pixel(q) & 0xFFFFFF;
            if (!v || int(v) > shapes.size()) continue;
            const Shape* s = shapes.at(int(v) - 1);
            if (!s->isFlat() || s->isFace() || s->isEditable()) continue;
            const int d = dx * dx + dy * dy;
            if (d < bestD) { bestD = d; best = v; }
        }
    return best;
}

bool View::overDraggableSurface(QPoint pos) const
{
    if (selectedShapeCount() >= 2 || !pick_img.valid(pos)) return false;
    const auto picked = pickedNear(pos);
    return picked && int(picked) <= shapes.size() && pullableAt(shapes.at(picked - 1), pos);
}

bool View::pullableAt(const Shape* s, QPoint pos, QVector3D* edge) const
{
    if (!s || s->isEditable() || !s->nativeDragOk()) return false;          // (the outline of a profile is where a point is put, not an edge to pull)
    if (!s->isFlat()) return true;
    // A flat face has no surface inside it: what is pulled is its nearest edge, when that is close to the cursor
    // (where the line of sight meets the plane of the shape: not the depth under the cursor, which is the background's where the press is just outside the face)
    QVector3D p;
    {
        QVector3D origin(0, 0, 0), normal(0, 0, 1);
        double plane[9];
        if (s->flatPlane(plane))
        {
            origin = QVector3D(float(plane[0]), float(plane[1]), float(plane[2]));
            normal = QVector3D::crossProduct(QVector3D(float(plane[3]), float(plane[4]), float(plane[5])), QVector3D(float(plane[6]), float(plane[7]), float(plane[8]))).normalized();
        }
        if (!ringPoint(pos, origin, normal, p)) return false;
    }
    const bool found = s->flatEdgeFrom(p);
    if (std::getenv("FIELDES_TIMING"))
    {
        const QPoint where = found ? screenPoint(p) : QPoint();
        fprintf(stderr, "[pull] flat edge at (%d, %d): %s, edge at (%d, %d) on the screen, %d px away\n", pos.x(), pos.y(), found ? "found" : "none", where.x(), where.y(),
                found ? (where - pos).manhattanLength() : -1);
    }
    if (!found) return false;
    const QPoint at = screenPoint(p);
    if ((at - pos).manhattanLength() > 14) return false;
    if (edge) *edge = p;
    return true;
}

void View::holdShift(bool on)
{
    m_shiftForced = on;
    m_shiftSeen = shiftHeld();
    if (!gizmo_drag) gizmo_hover = gizmoAt(mapFromGlobal(QCursor::pos()));
    m_overlay->update();
}

bool View::shiftHeld() const
{
    return m_shiftForced || (QGuiApplication::queryKeyboardModifiers() & Qt::ShiftModifier);
}

QPolygonF View::planeSquare(const QVector3D& pivot, float length, int normal) const
{
    const int b = (normal + 1) % 3, c = (normal + 2) % 3;
    return planeSquareIn(pivot, length, unitAxis(b), unitAxis(c));
}

QPolygonF View::planeSquareIn(const QVector3D& pivot, float length, const QVector3D& eb, const QVector3D& ec) const
{
    const float s0 = kPlaneFrom * length, s1 = kPlaneTo * length;
    QPolygonF out;
    out << toScreen(pivot + eb * s0 + ec * s0) << toScreen(pivot + eb * s1 + ec * s0)
        << toScreen(pivot + eb * s1 + ec * s1) << toScreen(pivot + eb * s0 + ec * s1);
    return out;
}

bool View::eventFilter(QObject* watched, QEvent* event)
{
    const QEvent::Type type = event->type();
    if (type == QEvent::KeyPress || type == QEvent::KeyRelease || type == QEvent::WindowDeactivate || type == QEvent::ApplicationStateChange)
    {
        const bool now = shiftHeld();
        if (now != m_shiftSeen)
        {
            m_shiftSeen = now;
            // (what is under the cursor is not the same grip any more: arrows and squares are in the same places)
            if (!gizmo_drag && underMouse()) gizmo_hover = gizmoAt(mapFromGlobal(QCursor::pos()));
            m_overlay->update();
        }
    }
    return QOpenGLWidget::eventFilter(watched, event);
}

View::GizmoGrip View::gizmoAt(QPoint pos) const
{
    GizmoGrip best;
    double bestDist = overDraggableSurface(pos) ? 4.0 : 9.0;
    const QPointF at(pos);
    // Several shapes selected, all in the gizmo mode: one gizmo for them all (arrows and the dot in the middle)
    QList<Shape*> members;
    QVector3D groupCenter;
    const bool group = groupGizmo(members, groupCenter);
    if (group)
    {
        const float L = gizmoLength(groupCenter);
        if (L > 0)
        {
            const QPointF p0 = toScreen(groupCenter);
            if (std::hypot(at.x() - p0.x(), at.y() - p0.y()) <= 8.0 &&
                (anyMoveVar(members, 0) || anyMoveVar(members, 1) || anyMoveVar(members, 2)))
            {
                return GizmoGrip{members[0], 3, 0, true};
            }
            const bool shift = shiftHeld();
            // (points of one profile have no arrows and no squares: the dot moves them, in the plane of the profile -- unless the profile itself is selected: all of it can move anywhere)
            QVector3D profileNormal;
            const bool inProfile = groupInProfilePlane(members, profileNormal) && !outlineSelected();
            for (int a = 0; a < 3 && !inProfile; ++a)
            {
                if (shift)
                {
                    // (Shift: the squares of the planes instead of the arrows)
                    if (anyMoveVar(members, (a + 1) % 3) && anyMoveVar(members, (a + 2) % 3) &&
                        planeSquare(groupCenter, L, a).containsPoint(at, Qt::OddEvenFill))
                    {
                        return GizmoGrip{members[0], 4, a, true};
                    }
                    continue;
                }
                if (!anyMoveVar(members, a)) continue;
                QVector3D e;
                e[a] = L;
                const double d = distanceToSegment(at, p0, toScreen(groupCenter + e));
                if (d < bestDist) { bestDist = d; best = GizmoGrip{members[0], 0, a, true}; }
            }
            if (groupOfPoints(members))
            {
                const bool outline = outlineSelected();
                if (outline && !shift)
                {
                    // The outline selected as a whole has the gizmo of any shape: a scale knob on each axis (it wins over the arrow behind it) and a ring about each axis
                    for (int a = 0; a < 3; ++a)
                    {
                        const QPointF q = toScreen(groupCenter + unitAxis(a) * (kKnobAt * L));
                        const double d = std::hypot(at.x() - q.x(), at.y() - q.y()) - 3.5;
                        if (d < bestDist) { bestDist = d; best = GizmoGrip{members[0], 2, a, true}; }
                    }
                    for (int a = 0; a < 3; ++a)
                    {
                        QVector3D ru, rv;
                        planeBasis(unitAxis(a), ru, rv);
                        QPointF rprev;
                        for (int k = 0; k <= kRingSegments; ++k)
                        {
                            const float t = float(2 * M_PI * k / kRingSegments);
                            const QPointF q = toScreen(groupCenter + (0.62f * L) * (std::cos(t) * ru + std::sin(t) * rv));
                            if (k > 0)
                            {
                                const double d = distanceToSegment(at, rprev, q) + 1.5;      // (the arrows and the knobs win ties)
                                if (d < bestDist) { bestDist = d; best = GizmoGrip{members[0], 1, a, true}; }
                            }
                            rprev = q;
                        }
                    }
                }
                else
                {
                    // The ring that scales the points evenly about the middle of them (with Shift, the outline's own: all of it, evenly), and the one that turns them about it
                    const double ds = std::fabs(std::hypot(at.x() - p0.x(), at.y() - p0.y()) - kUniformRingPx) - 3.5;
                    if (ds < bestDist) { bestDist = ds; best = GizmoGrip{members[0], 5, 0, true}; }
                    QVector3D u, v;
                    planeBasis(groupRingNormal(members), u, v);
                    QPointF prev;
                    for (int k = 0; k <= kRingSegments && !outline; ++k)
                    {
                        const float t = float(2 * M_PI * k / kRingSegments);
                        const QPointF q = toScreen(groupCenter + (0.62f * L) * (std::cos(t) * u + std::sin(t) * v));
                        if (k > 0)
                        {
                            const double d = distanceToSegment(at, prev, q) + 1.5;      // (the arrows and the scale ring win ties)
                            if (d < bestDist) { bestDist = d; best = GizmoGrip{members[0], 6, 2, true}; }
                        }
                        prev = q;
                    }
                }
            }
        }
    }
    for (Shape* s : shapes)
    {
        if (!s->hasGizmo() || (group && members.contains(s))) continue;
        const auto& h = s->gizmo();
        const auto& vars = s->getVars();
        const QVector3D P = s->gizmoPivot();
        const float L = gizmoLength(P);
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
        const bool shift = shiftHeld();
        // (a point of a profile has no arrows and no squares: it is moved by its dot, in the plane of the profile -- and has its one ring and its scale knobs)
        QVector3D facePlane;
        const bool inPlane = planarRingNormal(s, facePlane);
        // (a point of a curve: the arrows, the squares and the rings are along the way the curve faces there, see gizmoFrame -- and need all three numbers of a kind)
        QVector3D F[3] = {QVector3D(1, 0, 0), QVector3D(0, 1, 0), QVector3D(0, 0, 1)};
        const bool turned = !inPlane && gizmoFrame(s, F);
        auto moves = [&](int k) { return h.move[k] && vars.count(h.move[k]); };
        const bool movesAll = moves(0) && moves(1) && moves(2);
        for (int a = 0; a < 3 && !inPlane; ++a)
        {
            if (shift)
            {
                // (Shift: the squares of the planes instead of the arrows -- on a square, the grip is the square's, whatever is behind it)
                const int b = (a + 1) % 3, c = (a + 2) % 3;
                if ((turned ? movesAll : (moves(b) && moves(c))) && planeSquareIn(P, L, F[b], F[c]).containsPoint(at, Qt::OddEvenFill))
                {
                    best = {s, 4, a};
                    bestDist = -0.5;
                }
                continue;
            }
            if (turned ? !movesAll : !moves(a)) continue;
            const double d = distanceToSegment(at, p0, toScreen(P + F[a] * L));
            if (d < bestDist) { bestDist = d; best = {s, 0, a}; }
        }
        // (the scale knobs are on the part's own axes, turned with it: they win over the arrow behind them.  A point, and the Shift gizmo of any shape, have ONE
        // knob that scales all three together, on a line from the dot)
        const QMatrix4x4 R = s->gizmoOrientation();
        if (shift || s->isPoint())
        {
            bool any = false;
            for (int a = 0; a < 3; ++a) any = any || (h.scale[a] && vars.count(h.scale[a]));
            // (the hard corner of a profile has no ring, as it has no rotation ring: its scale is how strongly a spline leaves a spline point, or the radius of a round corner)
            if (any && !(inPlane && pointStyleOf(s) != 2 && pointStyleOf(s) != 3))             // (the radius of a round corner is the factor of this ring)
            {
                // (the ring: within a few pixels of the circle round the middle)
                const double d = std::fabs(std::hypot(at.x() - p0.x(), at.y() - p0.y()) - kUniformRingPx) - 3.5;
                if (d < bestDist) { bestDist = d; best = {s, 2, 0}; }
            }
        }
        else
        for (int a = 0; a < 3; ++a)
        {
            if (!h.scale[a] || !vars.count(h.scale[a])) continue;
            const QPointF q = toScreen(P + R.mapVector(unitAxis(a)) * (kKnobAt * L));
            const double d = std::hypot(at.x() - q.x(), at.y() - q.y()) - 3.5;
            if (d < bestDist) { bestDist = d; best = {s, 2, a}; }
        }
        QVector3D ring[3];
        gizmoRingAxes(s, ring);
        if (inPlane) ring[2] = facePlane;
        const bool rotatesAll = h.rotate[0] && vars.count(h.rotate[0]) && h.rotate[1] && vars.count(h.rotate[1]) && h.rotate[2] && vars.count(h.rotate[2]);
        if (turned) for (int a = 0; a < 3; ++a) ring[a] = F[a];
        for (int a = 0; a < 3; ++a)
        {
            if (!h.rotate[a] || !vars.count(h.rotate[a])) continue;
            if (turned && !rotatesAll) continue;
            // (a point of a profile has the one ring in the plane, and only a spline point: the rotation of a corner turns nothing)
            if (inPlane && (a != 2 || pointStyleOf(s) != 2)) continue;
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

bool View::gizmoGripPoint(int kind, int axis, QPoint& pos) const
{
    QList<Shape*> members;
    QVector3D groupCenter;
    if (groupGizmo(members, groupCenter))
    {
        // (the shared gizmo of several selected shapes has the arrows and the dot only)
        const float L = gizmoLength(groupCenter);
        if (!(L > 0) || (kind != 0 && kind != 1 && kind != 2 && kind != 3 && kind != 4 && kind != 5 && kind != 6)) return false;
        if (kind == 1 || kind == 2)
        {
            // (the rings and the knobs of an outline's own gizmo: a knob on its arrow, a ring at 45 degrees round, clear of the arrows)
            if (!groupOfPoints(members) || !outlineSelected()) return false;
            if (kind == 2)
            {
                pos = toScreen(groupCenter + unitAxis(axis) * (kKnobAt * L)).toPoint();
            }
            else
            {
                QVector3D u, v;
                planeBasis(unitAxis(axis), u, v);
                const float t = float(M_PI / 4);
                pos = toScreen(groupCenter + (0.62f * L) * (std::cos(t) * u + std::sin(t) * v)).toPoint();
            }
            return true;
        }
        if (kind == 5 || kind == 6)
        {
            if (!groupOfPoints(members)) return false;
            if (kind == 5)
            {
                pos = (toScreen(groupCenter) + QPointF(0.7071, -0.7071) * kUniformRingPx).toPoint();
            }
            else
            {
                QVector3D u, v;
                planeBasis(groupRingNormal(members), u, v);
                pos = toScreen(groupCenter + (0.62f * L) * u).toPoint();
            }
            return true;
        }
        if (kind == 4)
        {
            const QPolygonF q = planeSquare(groupCenter, L, axis);
            pos = ((q[0] + q[2]) / 2).toPoint();
            return true;
        }
        QVector3D e;
        if (kind == 0) e[axis] = 0.6f * L;
        pos = toScreen(groupCenter + e).toPoint();
        return true;
    }
    for (Shape* s : shapes)
    {
        if (!s->hasGizmo()) continue;
        const QVector3D P = s->gizmoPivot();
        const float L = gizmoLength(P);
        if (!(L > 0)) continue;
        if (kind == 3)
        {
            pos = toScreen(P).toPoint();
        }
        else if (kind == 0)
        {
            QVector3D F[3] = {QVector3D(1, 0, 0), QVector3D(0, 1, 0), QVector3D(0, 0, 1)};
            gizmoFrame(s, F);
            pos = toScreen(P + F[axis] * (0.6f * L)).toPoint();
        }
        else if (kind == 4)
        {
            QVector3D F[3] = {QVector3D(1, 0, 0), QVector3D(0, 1, 0), QVector3D(0, 0, 1)};
            gizmoFrame(s, F);
            const QPolygonF q = planeSquareIn(P, L, F[(axis + 1) % 3], F[(axis + 2) % 3]);
            pos = ((q[0] + q[2]) / 2).toPoint();
        }
        else if (kind == 2)
        {
            if (shiftHeld() || s->isPoint()) pos = (toScreen(P) + QPointF(0.7071, -0.7071) * kUniformRingPx).toPoint();         // (the ring of a point and of the Shift gizmo, up and to the right)
            else pos = toScreen(P + s->gizmoOrientation().mapVector(unitAxis(axis)) *
                                        (kKnobAt * L)).toPoint();
        }
        else
        {
            QVector3D ring[3], u, v, facePlane, F[3];
            gizmoRingAxes(s, ring);
            if (planarRingNormal(s, facePlane)) ring[2] = facePlane;
            else if (gizmoFrame(s, F)) for (int a = 0; a < 3; ++a) ring[a] = F[a];
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
        if (m_provLines.contains(s->sourceLine()) && s->hasGizmo()) return false;
    }
    return true;
}

bool View::provisionalGripAt(QPoint pos, int* kind, int* axis) const
{
    if (!provisionalVisible()) return false;
    const QVector3D P = m_provPivot;
    const float L = gizmoLength(P);
    if (!(L > 0)) return false;
    const QPointF at(pos), p0 = toScreen(P);
    if (std::hypot(at.x() - p0.x(), at.y() - p0.y()) <= 8.0)
    {
        *kind = 3;
        *axis = 0;
        return true;
    }
    double best = overDraggableSurface(pos) ? 4.0 : 9.0;
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
    const float L = gizmoLength(P);
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
        painter.setPen(QPen(kGizmoColor[a], 2.2));
        painter.drawPolyline(poly);
    }
    for (int a = 0; a < 3; ++a)
    {
        QVector3D e;
        e[a] = L;
        const QPointF q = toScreen(P + e);
        painter.setPen(QPen(QColor(0, 0, 0, 90), 5, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(p0, q);
        painter.setPen(QPen(kGizmoColor[a], 2.5, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(p0, q);
        const QPointF dir = q - p0;
        const double l = std::hypot(dir.x(), dir.y());
        if (l < 1) continue;
        const QPointF u = dir / l, w(-u.y(), u.x());
        QPolygonF head;
        head << q + u * 6 << q - u * 8 + w * 6 << q - u * 8 - w * 6;
        painter.setPen(QPen(QColor(0, 0, 0, 90), 1));
        painter.setBrush(kGizmoColor[a]);
        painter.drawPolygon(head);
    }
    for (int a = 0; a < 3; ++a)
    {
        const QPointF q = toScreen(P + unitAxis(a) * (kKnobAt * L));
        painter.setPen(QPen(QColor(0, 0, 0, 140), 1.5));
        painter.setBrush(kGizmoColor[a]);
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
    if (!m_leftDown || ++m_wait.ticks > 600)
    {
        cancelWaitingGrip();
        return;
    }
    for (Shape* s : shapes)
    {
        if (!s->hasGizmo()) continue;
        if (m_wait.selected ? !(s->isSelected() && (s->isPoint() || s->isFlat())) : !m_wait.lines.contains(s->sourceLine())) continue;
        const GizmoGrip g{s, m_wait.kind, m_wait.axis};
        const QPoint pos = m_lastPos;
        m_wait.on = false;
        m_waitTimer.stop();
        unsetCursor();
        beginGizmoDrag(g, pos);
        return;
    }
}

void View::drawMarkers(QPainter& painter)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    const QMatrix4x4 M = camera.M();
    for (Shape* s : shapes)
    {
        if (s->markers().empty()) continue;
        // The dot of a gizmo (see drawGizmo), the same colours; lit up while the set is the model tree's selection
        const bool on = s->isSelected();
        painter.setPen(QPen(on ? QColor(Qt::white) : QColor(0xee, 0xe8, 0xd5), 1.0));
        painter.setBrush(on ? QColor(80, 170, 235) : QColor(38, 139, 210));
        const double r = on ? 4.5 : 4.0;
        for (const QVector3D& p : s->markers())
        {
            if (!(M.row(3).x() * p.x() + M.row(3).y() * p.y() + M.row(3).z() * p.z() + M.row(3).w() > 1e-6f)) continue;     // (behind the eye)
            const QPointF q = toScreen(p);
            if (q.x() < -r || q.y() < -r || q.x() > width() + r || q.y() > height() + r) continue;
            painter.drawEllipse(q, r, r);
        }
    }
    painter.restore();
}

// A point is a dot: the middle point of a gizmo (the same blue, lit up while it is hovered or selected), drawn whatever the mode of its gizmo
void View::drawPointDots(QPainter& painter)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    const QMatrix4x4 M = camera.M();
    for (Shape* s : shapes)
    {
        QVector3D p;
        if (!s->isPoint() || !s->dotPosition(p)) continue;
        if (!(M.row(3).x() * p.x() + M.row(3).y() * p.y() + M.row(3).z() * p.z() + M.row(3).w() > 1e-6f)) continue;     // (behind the eye)
        const QPointF q = toScreen(p);
        if (q.x() < -8 || q.y() < -8 || q.x() > width() + 8 || q.y() > height() + 8) continue;
        const bool on = s->isSelected() || s == m_dotHover;
        painter.setPen(QPen(on ? QColor(Qt::white) : QColor(0xee, 0xe8, 0xd5), 1.5));
        painter.setBrush(on ? QColor(80, 170, 235) : QColor(38, 139, 210));
        const double r = on ? 6.0 : 5.0;
        const int style = pointStyleOf(s);
        if (style == 1)
        {
            // a corner of a profile or a curve: a square, a hard edge
            painter.drawRect(QRectF(q.x() - r, q.y() - r, 2 * r, 2 * r));
        }
        else if (style == 3)
        {
            // a round corner of a profile: a square with round corners -- an arc takes the place of the corner
            painter.drawRoundedRect(QRectF(q.x() - r, q.y() - r, 2 * r, 2 * r), 0.55 * r, 0.55 * r);
        }
        else if (style == 2)
        {
            // a spline point: a circle with a ring round it, the curve goes smoothly through it
            painter.drawEllipse(q, r, r);
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(on ? QColor(Qt::white) : QColor(0xee, 0xe8, 0xd5, 170), 1.0));
            painter.drawEllipse(q, r + 3.0, r + 3.0);
            // the direction the curve leaves it in, while it is selected (or the mouse is on it): a line through the dot, its rotation turns it
            QVector3D tg;
            int controlIndex = -1;
            Shape* owner = on ? controlOwner(s, controlIndex) : nullptr;
            if (owner && owner->tangentAt(controlIndex, tg))
            {
                const QPointF d = toScreen(p + tg.normalized() * gizmoLength(p)) - q;
                const double len = std::hypot(d.x(), d.y());
                if (len > 2.0)
                {
                    const QPointF u = d / len;
                    const QPointF tip = q + u * 46.0;
                    painter.setPen(QPen(QColor(120, 195, 255), 1.6, Qt::DashLine));
                    painter.drawLine(q - u * 46.0, tip);
                    painter.setPen(QPen(QColor(120, 195, 255), 1.8));
                    const QPointF n(-u.y(), u.x());
                    painter.drawLine(tip, tip - u * 8.0 + n * 4.0);
                    painter.drawLine(tip, tip - u * 8.0 - n * 4.0);
                }
            }
        }
        else
        {
            painter.drawEllipse(q, r, r);
        }
    }
    painter.restore();
}

// The outline of the face of a profile and the line of a curve are drawn blue: a place to press (a point is put where it is pressed), and to
// see where a 2D shape ends
void View::drawOutlines(QPainter& painter)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    for (Shape* s : shapes)
    {
        // (a face of points is drawn natively from its points, so it is not "flat" -- it has no field to mesh -- but its outline is drawn like a flat shape's)
        if (s->outlines().empty() || (!s->isFlat() && !s->isCurve() && !s->isFace())) continue;
        const bool hot = s == m_outlineHover || (s->isHovered() && (s->isFlat() || s->isFace()));
        const bool on = s->isSelected();
        QColor c = hot ? QColor(120, 195, 255) : on ? QColor(105, 180, 255) : QColor(52, 140, 245);
        painter.setPen(QPen(c, hot ? 3.4 : on ? 2.8 : 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        std::vector<OutlinePoint> points;
        for (size_t li = 0; li < s->outlines().size(); ++li)
        {
            points.clear();
            outlineLine(s, li, 0, points);
            QPolygonF poly;
            for (const auto& o : points)
            {
                if (!o.front)
                {
                    if (poly.size() > 1) painter.drawPolyline(poly);
                    poly.clear();
                    continue;
                }
                poly << o.screen;
            }
            if (poly.size() > 1) painter.drawPolyline(poly);
        }
    }
    painter.restore();
}

// The points of one line of an outline, for drawing it and for finding a press on it.  A straight piece is its two ends.  The arc of a round corner and the cubic of a spline are not
// the pieces the face is cut along: each is the curve itself (Shape::OutlineCurve), taken at as many points as the screen tells apart -- a piece is cut in two while its middle is
// more than a twentieth of a pixel off its chord -- so zooming in never shows a corner on it
void View::outlineLine(const Shape* s, size_t li, int pieceBase, std::vector<OutlinePoint>& out) const
{
    const auto& line = s->outlines()[li];
    const QMatrix4x4 M = camera.M();
    auto make = [&](const QVector3D& p, int piece) {
        OutlinePoint o;
        o.world = p;
        o.front = M.row(3).x() * p.x() + M.row(3).y() * p.y() + M.row(3).z() * p.z() + M.row(3).w() > 1e-6f;
        if (o.front) o.screen = toScreen(p);
        o.piece = piece;
        return o;
    };
    std::vector<const Shape::OutlineCurve*> curves;
    for (const auto& a : s->outlineCurves())
        if (a.line == int(li) && a.first >= 0 && a.last > a.first && a.last < int(line.size())) curves.push_back(&a);
    std::sort(curves.begin(), curves.end(), [](const Shape::OutlineCurve* a, const Shape::OutlineCurve* b) { return a->first < b->first; });
    size_t next = 0;
    for (size_t k = 0; k < line.size(); ++k)
    {
        out.push_back(make(QVector3D(line[k].x(), line[k].y(), line[k].z()), pieceBase + int(k)));
        while (next < curves.size() && curves[next]->first < int(k)) ++next;
        if (next >= curves.size() || curves[next]->first != int(k)) continue;
        const Shape::OutlineCurve& a = *curves[next++];
        const int piece = pieceBase + a.first;
        auto at = [&](double t) {
            const Eigen::Vector3d q = a.point(t);
            return make(QVector3D(float(q.x()), float(q.y()), float(q.z())), piece);
        };
        std::function<void(double, const OutlinePoint&, double, const OutlinePoint&, int)> cut =
            [&](double t0, const OutlinePoint& p0, double t1, const OutlinePoint& p1, int depth) {
            const double tm = 0.5 * (t0 + t1);
            const OutlinePoint pm = at(tm);
            bool split = false;
            if (p0.front && p1.front && pm.front)
            {
                const QPointF d = p1.screen - p0.screen, e = pm.screen - p0.screen;
                const double len = std::hypot(d.x(), d.y());
                const double off = len > 1e-9 ? std::abs(d.x() * e.y() - d.y() * e.x()) / len : std::hypot(e.x(), e.y());
                split = off > 0.05 && depth < (len > 2e4 ? 6 : 12);
            }
            else
            {
                split = depth < 4;                  // (partly behind the camera: only far enough to find where the arc goes behind it)
            }
            if (!split) return;
            cut(t0, p0, tm, pm, depth + 1);
            out.push_back(pm);
            cut(tm, pm, t1, p1, depth + 1);
        };
        const int parts = a.arc ? std::max(2, int(std::ceil(a.sweep / (M_PI / 8)))) : 8;
        OutlinePoint previous = out.back();         // (the start of the curve: the vertex that was just given)
        for (int i = 1; i <= parts; ++i)
        {
            const double t0 = double(i - 1) / parts, t1 = double(i) / parts;
            const OutlinePoint end = i == parts ? make(QVector3D(line[size_t(a.last)].x(), line[size_t(a.last)].y(), line[size_t(a.last)].z()), piece) : at(t1);
            cut(t0, previous, t1, end, 0);
            if (i < parts) out.push_back(end);
            previous = end;
        }
        k = size_t(a.last) - 1;                     // (the next vertex given is the curve's last one)
    }
}

void View::drawHints(QPainter& painter)
{
    if (mouse.state != mouse.RELEASED || m_wait.on) return;
    QString text;
    if (gizmo_hover.group && gizmo_hover.kind == 5)
        text = outlineSelected() ? T("Drag the ring to scale the whole outline about its middle") : T("Drag the ring to scale each point evenly: they stay where they are");
    else if (gizmo_hover.group && gizmo_hover.kind == 6)
        text = outlineSelected() ? T("Drag the ring to turn the whole outline about its middle") : T("Drag the ring to turn each point: they stay where they are");
    else if (gizmo_hover.kind == 2 && !gizmo_hover.group && gizmo_hover.shape && (gizmo_hover.shape->isPoint() || shiftHeld())) text = T("Drag the ring to scale evenly");
    else if (m_dotHover && pointStyleOf(m_dotHover) != 0) text = T("Double-click: corner, spline point or round corner");
    else if (m_outlineHover && m_outlineArcHover) text = T("Drag to change the radius of this corner");
    else if (m_outlineHover) text = m_outlineHover->isCurve() ? T("Drag to pull a point out of the line") : T("Drag to pull a point out of the edge");
    if (text.isEmpty()) return;
    painter.save();
    QFont font = painter.font();
    font.setPointSizeF(8.5);
    painter.setFont(font);
    const QFontMetrics fm(font);
    QPointF at = QPointF(mouse.pos) + QPointF(16, 18);
    QRectF box(at, QSizeF(fm.horizontalAdvance(text) + 12, fm.height() + 6));
    if (box.right() > width() - 4) box.moveRight(width() - 4);
    if (box.bottom() > height() - 4) box.moveBottom(mouse.pos.y() - 8);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(22, 76, 94, 225));
    painter.drawRoundedRect(box, 4, 4);
    painter.setPen(QColor(0xee, 0xe8, 0xd5));
    painter.drawText(box, Qt::AlignCenter, text);
    painter.restore();
}

// The editable outline that passes under a position: the nearest piece of any line of any editable shape
bool View::outlineAt(QPoint pos, Shape*& shape, QVector3D& hit, int& piece) const
{
    double best = 8.0 * 8.0;
    bool found = false;
    const QMatrix4x4 M = camera.M();
    const QPointF c(pos);
    std::vector<OutlinePoint> points;
    for (Shape* s : shapes)
    {
        if (!s->isEditable() || s->outlines().empty()) continue;
        int count = 0;
        for (size_t li = 0; li < s->outlines().size(); ++li)
        {
            // (the same points the line is drawn through: on the arc of a round corner a press is looked for on the circle, not on the pieces the face is cut along)
            points.clear();
            outlineLine(s, li, count, points);
            count += int(s->outlines()[li].size()) - 1;
            for (size_t i = 0; i + 1 < points.size(); ++i)
            {
                const OutlinePoint &p0 = points[i], &p1 = points[i + 1];
                if (!p0.front || !p1.front) continue;
                const QPointF a = p0.screen, b = p1.screen;
                const QPointF d = b - a;
                const double l2 = d.x() * d.x() + d.y() * d.y();
                double t = l2 > 1e-9 ? ((c.x() - a.x()) * d.x() + (c.y() - a.y()) * d.y()) / l2 : 0.0;
                t = std::max(0.0, std::min(1.0, t));
                const QPointF q = a + d * t;
                const double dist = (q.x() - c.x()) * (q.x() - c.x()) + (q.y() - c.y()) * (q.y() - c.y());
                if (dist < best)
                {
                    best = dist;
                    shape = s;
                    hit = p0.world + (p1.world - p0.world) * float(t);
                    piece = p0.piece;
                    found = true;
                }
            }
        }
    }
    if (found && pick_img.valid(pos))
    {
        // (something drawn nearer than the line, at that place, is what is under the mouse)
        const int at = pos.x() + pick_img.width() * (pick_img.height() - pos.y() - 1);
        if (at >= 0 && at < pick_depth.size())
        {
            const float pickZ = 2 * pick_depth.at(at) - 1;
            const QVector3D n = M.map(hit);
            const auto picked = pick_img.pixel(pos) & 0xFFFFFF;
            if (std::getenv("FIELDES_OUTLINE_LOG"))
                std::fprintf(stderr, "[outline] at %d,%d: piece %d, depth of the line %.5f, of what is drawn there %.5f, picked %u\n", pos.x(), pos.y(), piece,
                             double(n.z()), double(pickZ), unsigned(picked));
            // (the line of a curve is pressed through the body it runs in: the path of a sweep is drawn through the middle of it, so the sweep is always nearer than the line)
            if (picked && pickZ < n.z() - 0.003f && !shape->isCurve() && !shape->is2D()) found = false;     // (a 2D shape is drawn over what it is in)
        }
    }
    return found;
}

// The point whose dot is under a widget position (within a few pixels), the nearest one.  Two dots can be at ONE place (the corner of a profile and the start of a path that begin at the same
// point): the one that is selected is the one that is meant, since its gizmo is what the next press or the next notch of the wheel is for, and not whichever the list holds first
Shape* View::pointDotAt(QPoint pos) const
{
    Shape* best = nullptr;
    double bestD = 10.0 * 10.0;
    const QMatrix4x4 M = camera.M();
    for (Shape* s : shapes)
    {
        QVector3D p;
        if (!s->isPoint() || !s->dotPosition(p)) continue;
        if (!(M.row(3).x() * p.x() + M.row(3).y() * p.y() + M.row(3).z() * p.z() + M.row(3).w() > 1e-6f)) continue;
        const QPointF q = toScreen(p);
        const double dx = q.x() - pos.x(), dy = q.y() - pos.y();
        const double d = dx * dx + dy * dy;
        const bool nearer = d < bestD - 1e-6;
        const bool sameSpot = best && d <= bestD + 1e-6 && s->isSelected() && !best->isSelected();
        if (nearer || sameSpot) { bestD = d; best = s; }
    }
    return best;
}

// The names an app gives its models: a card above the model, in the style of the labels of boundary conditions (the same colours and the
// same corners), white round it while the model is selected
void View::drawNameLabels(QPainter& painter)
{
    QFont font = painter.font();
    font.setPointSizeF(9);
    font.setBold(true);
    painter.setFont(font);
    const QFontMetrics fm(font);
    const QMatrix4x4 M = camera.M();
    const int up = y_up ? 1 : 2;
    for (Shape* s : shapes)
    {
        if (s->appLabel().isEmpty()) continue;
        QVector3D anchor;
        double lift = 14;
        QVector3D p;
        if (s->isPoint() && s->dotPosition(p))
        {
            anchor = p;
            lift = 16;
        }
        else if (s->hasMesh() && !s->noPaint())
        {
            const auto& b = s->getMeshBounds();
            anchor = QVector3D(0.5f * (b.lower.x() + b.upper.x()), 0.5f * (b.lower.y() + b.upper.y()), 0.5f * (b.lower.z() + b.upper.z()));
            anchor[up] = b.upper[up];
        }
        else continue;
        if (!(M.row(3).x() * anchor.x() + M.row(3).y() * anchor.y() + M.row(3).z() * anchor.z() + M.row(3).w() > 1e-6f)) continue;
        QPointF at = toScreen(anchor);
        at.setY(at.y() - lift);
        const QSizeF sz(fm.horizontalAdvance(s->appLabel()) + 14, fm.height() + 6);
        QRectF box(at.x() - sz.width() / 2, at.y() - sz.height(), sz.width(), sz.height());
        // (kept on the picture)
        box.moveLeft(std::max(2.0, std::min(double(width()) - sz.width() - 2, box.left())));
        box.moveTop(std::max(2.0, box.top()));
        if (box.bottom() < 0 || box.top() > height()) continue;
        painter.setPen(QPen(s->isSelected() ? QColor(Qt::white) : QColor(0x93, 0xa1, 0xa1), 1.5));
        painter.setBrush(QColor(22, 76, 94, 225));
        painter.drawRoundedRect(box, 4, 4);
        painter.setPen(QColor(0xee, 0xe8, 0xd5));
        painter.drawText(box, Qt::AlignCenter, s->appLabel());
    }
}

void View::drawGizmo(QPainter& painter)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    // The dot in the middle of a gizmo: it drags the shape freely (in the plane facing the camera)
    auto dot = [&](const QPointF& p0, bool on) {
        painter.setPen(QPen(on ? QColor(Qt::white) : QColor(0xee, 0xe8, 0xd5), 1.5));
        painter.setBrush(on ? QColor(80, 170, 235) : QColor(38, 139, 210));
        painter.drawEllipse(p0, on ? 6.5 : 5.5, on ? 6.5 : 5.5);
    };
    // A plane square of the Shift gizmo: the colour of the axis that is the plane's normal, lit up while it is under the cursor or dragged
    auto squareOf = [&](const QPolygonF& poly, int normal, bool on) {
        QColor c = kGizmoColor[normal];
        painter.setPen(QPen(QColor(0, 0, 0, 120), 1.5));
        c.setAlpha(on ? 210 : 130);
        painter.setBrush(c);
        painter.drawPolygon(poly);
        painter.setPen(QPen(on ? c.lighter(150) : kGizmoColor[normal], on ? 2.5 : 1.8));
        painter.setBrush(Qt::NoBrush);
        painter.drawPolygon(poly);
    };
    auto square = [&](const QVector3D& pivot, float length, int normal, bool on) { squareOf(planeSquare(pivot, length, normal), normal, on); };
    const bool shift = shiftHeld();
    // The gizmo shared by several selected shapes, all in the gizmo mode: the move arrows and the dot, at the middle
    // of their pivots; they move them all
    QList<Shape*> members;
    QVector3D groupCenter;
    const bool group = groupGizmo(members, groupCenter);
    if (group)
    {
        const float L = gizmoLength(groupCenter);
        if (L > 0)
        {
            const QPointF p0 = toScreen(groupCenter);
            auto hotG = [&](int kind, int a) {
                const GizmoGrip g{members[0], kind, a, true};
                return g == gizmo_hover || (gizmo_drag && g == gizmo_active);
            };
            QVector3D profileNormal;
            const bool inProfile = groupInProfilePlane(members, profileNormal) && !outlineSelected();      // (points of one profile: the dot only, it moves them in the plane -- but the profile itself, selected, has the arrows)
            for (int a = 0; a < 3 && !inProfile; ++a)
            {
                if (shift)
                {
                    if (anyMoveVar(members, (a + 1) % 3) && anyMoveVar(members, (a + 2) % 3)) square(groupCenter, L, a, hotG(4, a));
                    continue;
                }
                if (!anyMoveVar(members, a)) continue;
                QVector3D e;
                e[a] = L;
                const QPointF q = toScreen(groupCenter + e);
                const bool on = hotG(0, a);
                QColor c = kGizmoColor[a];
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
            if (groupOfPoints(members))
            {
                // The rings of a group of points: the one in the plane turns them about the middle of them all, the gold one scales them evenly.  The outline selected as a whole has the
                // gizmo of any shape instead: a ring about each axis and a knob on each axis, and with Shift the one gold ring
                const bool outline = outlineSelected();
                if (outline && !shift)
                {
                    painter.setBrush(Qt::NoBrush);
                    for (int a = 0; a < 3; ++a)
                    {
                        QVector3D ru, rv;
                        planeBasis(unitAxis(a), ru, rv);
                        QPolygonF poly;
                        for (int k = 0; k <= kRingSegments; ++k)
                        {
                            const float t = float(2 * M_PI * k / kRingSegments);
                            poly << toScreen(groupCenter + (0.62f * L) * (std::cos(t) * ru + std::sin(t) * rv));
                        }
                        const bool onRing = hotG(1, a);
                        painter.setPen(QPen(QColor(0, 0, 0, 90), onRing ? 6 : 4.5));
                        painter.drawPolyline(poly);
                        painter.setPen(QPen(onRing ? kGizmoColor[a].lighter(140) : kGizmoColor[a], onRing ? 3.5 : 2.2));
                        painter.drawPolyline(poly);
                    }
                    for (int a = 0; a < 3; ++a)
                    {
                        const QPointF q = toScreen(groupCenter + unitAxis(a) * (kKnobAt * L));
                        const bool onKnob = hotG(2, a);
                        QColor kc = kGizmoColor[a];
                        if (onKnob) kc = kc.lighter(140);
                        const double r = onKnob ? 6.5 : 5.5;
                        painter.setPen(QPen(QColor(0, 0, 0, 140), 1.5));
                        painter.setBrush(kc);
                        painter.drawRect(QRectF(q.x() - r, q.y() - r, 2 * r, 2 * r));
                    }
                }
                QVector3D u, v;
                planeBasis(groupRingNormal(members), u, v);
                QPolygonF turnPoly;
                for (int k = 0; k <= kRingSegments; ++k)
                {
                    const float t = float(2 * M_PI * k / kRingSegments);
                    turnPoly << toScreen(groupCenter + (0.62f * L) * (std::cos(t) * u + std::sin(t) * v));
                }
                const bool turning = gizmo_drag && gizmo_active.group && gizmo_active.kind == 6;
                const bool onTurn = hotG(6, 2);
                if (!outline)
                {
                    painter.setBrush(Qt::NoBrush);
                    painter.setPen(QPen(QColor(0, 0, 0, 90), onTurn ? 6 : 4.5));
                    painter.drawPolyline(turnPoly);
                    painter.setPen(QPen(onTurn ? kGizmoColor[2].lighter(140) : kGizmoColor[2], onTurn ? 3.5 : 2.2));
                    painter.drawPolyline(turnPoly);
                }
                const bool scaling = gizmo_drag && gizmo_active.group && gizmo_active.kind == 5;
                const bool onScale = hotG(5, 0);
                QColor c = kUniformColor;
                if (onScale) c = c.lighter(140);
                const double radius = scaling ? std::max(6.0, kUniformRingPx * double(gizmo_factor)) : kUniformRingPx;
                if (!outline || shift)
                {
                    painter.setBrush(Qt::NoBrush);
                    if (scaling)
                    {
                        painter.setPen(QPen(QColor(kUniformColor.red(), kUniformColor.green(), kUniformColor.blue(), 110), 1.2, Qt::DashLine));
                        painter.drawEllipse(p0, kUniformRingPx, kUniformRingPx);
                    }
                    painter.setPen(QPen(QColor(0, 0, 0, 90), onScale ? 6.0 : 4.5));
                    painter.drawEllipse(p0, radius, radius);
                    painter.setPen(QPen(c, onScale ? 3.5 : 2.2));
                    painter.drawEllipse(p0, radius, radius);
                }
                if (scaling || turning)
                {
                    const QString text = scaling ? QString::fromUtf8("\xc3\x97") + QString::number(double(gizmo_factor), 'f', 2)
                                                 : QString::number(double(snappedTurn(gizmo_turn)), 'f', 1) + QString(QChar(0x00b0));
                    QFont font = painter.font();
                    font.setPointSizeF(8.5);
                    painter.setFont(font);
                    const QFontMetrics fm(font);
                    const QPointF at = scaling ? p0 + QPointF(0.7071, -0.7071) * radius : toScreen(groupCenter + (0.62f * L) * u);
                    const QRectF box(at + QPointF(8, -8 - fm.height()), QSizeF(fm.horizontalAdvance(text) + 10, fm.height() + 4));
                    painter.setPen(Qt::NoPen);
                    painter.setBrush(QColor(22, 76, 94, 225));
                    painter.drawRoundedRect(box, 4, 4);
                    painter.setPen(QColor(0xee, 0xe8, 0xd5));
                    painter.drawText(box, Qt::AlignCenter, text);
                }
            }
            dot(p0, hotG(3, 0));
        }
    }
    for (Shape* s : shapes)
    {
        if (!s->hasGizmo() || (group && members.contains(s))) continue;
        const auto& h = s->gizmo();
        const auto& vars = s->getVars();
        const QVector3D P = s->gizmoPivot();
        const float L = gizmoLength(P);
        if (!(L > 0)) continue;
        const QPointF p0 = toScreen(P);
        const bool uniform = shift || s->isPoint();         // (every knob scales all three: a point has one scale)
        auto hot = [&](int kind, int a) {
            const GizmoGrip g{s, kind, a};
            return g == gizmo_hover || (gizmo_drag && g == gizmo_active);
        };

        QVector3D ring[3];
        gizmoRingAxes(s, ring);
        QVector3D facePlane;
        const bool inPlane = planarRingNormal(s, facePlane);        // (a point of a profile: one ring, in the plane of the profile)
        if (inPlane) ring[2] = facePlane;
        // (a point of a curve: the gizmo is along the way the curve faces there, see gizmoFrame)
        QVector3D F[3] = {QVector3D(1, 0, 0), QVector3D(0, 1, 0), QVector3D(0, 0, 1)};
        const bool turned = !inPlane && gizmoFrame(s, F);
        const bool movesAll = h.move[0] && vars.count(h.move[0]) && h.move[1] && vars.count(h.move[1]) && h.move[2] && vars.count(h.move[2]);
        const bool rotatesAll = h.rotate[0] && vars.count(h.rotate[0]) && h.rotate[1] && vars.count(h.rotate[1]) && h.rotate[2] && vars.count(h.rotate[2]);
        if (turned) for (int a = 0; a < 3; ++a) ring[a] = F[a];
        for (int a = 0; a < 3; ++a)
        {
            if (!h.rotate[a] || !vars.count(h.rotate[a])) continue;
            if (turned && !rotatesAll) continue;
            if (inPlane && (a != 2 || pointStyleOf(s) != 2)) continue;       // (see gizmoAt: one ring in the plane, on a spline point only)
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
            QColor c = kGizmoColor[a];
            if (on) c = c.lighter(140);
            painter.setPen(QPen(c, on ? 3.5 : 2.2));
            painter.drawPolyline(poly);
        }
        for (int a = 0; a < 3 && !inPlane; ++a)             // (a point of a profile has no arrows and no squares: its dot moves it, in the plane of the profile)
        {
            if (shift)
            {
                const int b = (a + 1) % 3, c = (a + 2) % 3;
                if (turned ? movesAll : (h.move[b] && vars.count(h.move[b]) && h.move[c] && vars.count(h.move[c]))) squareOf(planeSquareIn(P, L, F[b], F[c]), a, hot(4, a));
                continue;
            }
            if (turned ? !movesAll : !(h.move[a] && vars.count(h.move[a]))) continue;
            const QPointF q = toScreen(P + F[a] * L);
            const bool on = hot(0, a);
            QColor c = kGizmoColor[a];
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
        // The scale knobs.  Normally a square on each of the part's own axes, each scaling along its axis.  A point and the Shift gizmo have ONE handle that scales all
        // three together: a ring round the middle (the way Blender's scale gizmo has it) -- pull it out to grow, push it in to shrink.  While it is dragged the ring
        // follows the cursor, a faint one stays where it was, and the factor is shown
        const QMatrix4x4 R = s->gizmoOrientation();
        if (uniform)
        {
            bool any = false;
            for (int a = 0; a < 3; ++a) any = any || (h.scale[a] && vars.count(h.scale[a]));
            if (any && !(inPlane && pointStyleOf(s) != 2 && pointStyleOf(s) != 3))              // (not on the hard corner of a profile: see gizmoAt)
            {
                const GizmoGrip& now = gizmo_drag ? gizmo_active : gizmo_hover;
                const bool on = now.kind == 2 && now.shape == s;
                const bool dragging = gizmo_drag && gizmo_active.kind == 2 && gizmo_active.shape == s;
                QColor c = kUniformColor;
                if (on) c = c.lighter(140);
                // (the ring as pressed, when it is being dragged)
                if (dragging)
                {
                    painter.setBrush(Qt::NoBrush);
                    painter.setPen(QPen(QColor(kUniformColor.red(), kUniformColor.green(), kUniformColor.blue(), 110), 1.2, Qt::DashLine));
                    painter.drawEllipse(p0, kUniformRingPx, kUniformRingPx);
                }
                const double radius = dragging ? std::max(6.0, kUniformRingPx * double(gizmo_factor)) : kUniformRingPx;
                painter.setBrush(Qt::NoBrush);
                painter.setPen(QPen(QColor(0, 0, 0, 90), on ? 6.0 : 4.5));
                painter.drawEllipse(p0, radius, radius);
                painter.setPen(QPen(c, on ? 3.5 : 2.2));
                painter.drawEllipse(p0, radius, radius);
                if (dragging)
                {
                    const QString text = QString::fromUtf8("\xc3\x97") + QString::number(double(gizmo_factor), 'f', 2);
                    QFont font = painter.font();
                    font.setPointSizeF(8.5);
                    painter.setFont(font);
                    const QFontMetrics fm(font);
                    const QPointF at = p0 + QPointF(0.7071, -0.7071) * radius;
                    const QRectF box(at + QPointF(8, -8 - fm.height()), QSizeF(fm.horizontalAdvance(text) + 10, fm.height() + 4));
                    painter.setPen(Qt::NoPen);
                    painter.setBrush(QColor(22, 76, 94, 225));
                    painter.drawRoundedRect(box, 4, 4);
                    painter.setPen(QColor(0xee, 0xe8, 0xd5));
                    painter.drawText(box, Qt::AlignCenter, text);
                }
            }
        }
        else
        for (int a = 0; a < 3; ++a)
        {
            if (!h.scale[a] || !vars.count(h.scale[a])) continue;
            const QPointF q = toScreen(P + R.mapVector(unitAxis(a)) * (kKnobAt * L));
            const bool on = hot(2, a);
            QColor c = kGizmoColor[a];
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

void View::applyGizmoNumber(libfive::Tree::Id id, float value)
{
    applyGizmoNumbers({{id, value}});
}

void View::applyGizmoNumbers(const std::map<libfive::Tree::Id, float>& m)
{
    static const bool timing = std::getenv("FIELDES_TIMING") != nullptr;
    QElapsedTimer clock;
    if (timing) clock.start();
    emit(varsDragged(QMap<libfive::Tree::Id, float>(m)));
    const qint64 written = timing ? clock.nsecsElapsed() : 0;
    bool changed = false, moved = false;
    // A drag of a gizmo's numbers moves a shape, it does not change it: its mesh is drawn where the numbers say, with nothing meshed, and made again when the drag ends
    // (Shape::previewGizmoVars).  Not with the section view cutting: the cut is made in the mesh's own places.  A shape that is made of what moves is made again as ever
    static const bool off = std::getenv("FIELDES_NO_DRAG_PREVIEW") != nullptr;          // (for comparing: the old way, every number meshes the shape again)
    const bool allowed = gizmo_drag && !off && !(section.enabled && section.clip);
    for (auto& s : shapes)
    {
        if (s->previewGizmoVars(m, allowed)) { moved = true; continue; }
        changed |= s->updateVars(m);
    }
    if (moved) update();
    // (the region holds what is dragged, drawn moved or meshed again: the one that is meshed again must not be meshed in a region that cuts it off)
    if (moved || changed) growRegionForPreview();
    if (changed)
    {
        busy.show();
        emit(renderBusy(true));
        requestSlice();
    }
    if (m_symExists) refreshSymmetry();            // (the numbers that are dragged may be the gizmo of the symmetry: its planes follow)
    // The field the field viewer shows is not a shape that is drawn, so nothing above reached its numbers: when the numbers that are
    // dragged are its own (the gizmo of the field, or a surface of it), the disc shows it from the new ones as they come, not
    // from the numbers of the last run of the script
    for (auto& kv : m_viewers)
    {
        FieldViewer* viewer = kv.second.get();
        bool fieldChanged = false;
        for (const auto& v : m)
        {
            const auto found = viewer->source.vars.find(v.first);
            if (found == viewer->source.vars.end() || found->second == v.second) continue;
            found->second = v.second;
            fieldChanged = true;
        }
        if (fieldChanged) requestFieldSlice(viewer);
    }
    m_overlay->update();
    if (timing)
        std::fprintf(stderr, "[drag] numbers: %.1f ms in all, %.1f ms of it writing the script; %s\n", double(clock.nsecsElapsed()) / 1e6, double(written) / 1e6,
                     moved ? (changed ? "some shapes moved, others meshed" : "shapes moved, nothing meshed") : (changed ? "meshed again" : "nothing changed"));
}

void View::growRegionForPreview()
{
    // Auto scale off: the region is what the script says.  The picture of a model that is dragged is its mesh moved: the box of it is where it will be, and the region
    // must hold that -- now, not after the drag is let go and the script has run
    if (!settings.autoScale) return;
    double nlo[3], nhi[3];
    bool any = false;
    for (auto& s : shapes)
    {
        QVector3D lo, hi;
        // (a shape drawn moved, or one that is meshed again as it is dragged -- both are where the numbers of its gizmo put them, not where the mesh it has is)
        if (!s->previewBox(lo, hi) && !s->movedBox(lo, hi))
        {
            if (!s->hasMesh() || s->noPaint()) continue;
            const auto& b = s->getMeshBounds();
            lo = QVector3D(b.lower.x(), b.lower.y(), b.lower.z());
            hi = QVector3D(b.upper.x(), b.upper.y(), b.upper.z());
        }
        for (int k = 0; k < 3; ++k)
        {
            nlo[k] = any ? std::min(nlo[k], double(lo[k])) : double(lo[k]);
            nhi[k] = any ? std::max(nhi[k], double(hi[k])) : double(hi[k]);
        }
        any = true;
    }
    if (!any) return;
    growRegionToHold(nlo, nhi);
}

void View::growRegionForSurfaceDrag(const QVector3D& at, const std::map<libfive::Tree::Id, float>& vs)
{
    // A surface that is pulled is meshed again from its numbers at every move, over the region: out of it, it is cut off.  The region must hold the model as it will be at these
    // numbers -- now, before the shapes are given them (they start meshing at once).  Where it will be is searched for in the field (a sphere pulled out on one side grows on the other
    // too); at the least the surface passes through the point the mouse has it at
    if (!settings.autoScale) return;
    double nlo[3] = {at.x(), at.y(), at.z()}, nhi[3] = {at.x(), at.y(), at.z()};
    auto hold = [&](const QVector3D& lo, const QVector3D& hi) {
        for (int k = 0; k < 3; ++k)
        {
            nlo[k] = std::min(nlo[k], double(lo[k]));
            nhi[k] = std::max(nhi[k], double(hi[k]));
        }
    };
    // (the search is over the region and as far again on every side, and as far round the point the mouse is at)
    const QVector3D size = settings.max - settings.min;
    const float reach = std::max({size.x(), size.y(), size.z(), 1.0f});
    QVector3D slo = settings.min - QVector3D(reach, reach, reach), shi = settings.max + QVector3D(reach, reach, reach);
    for (int k = 0; k < 3; ++k)
    {
        slo[k] = std::min(slo[k], float(at[k]) - reach);
        shi[k] = std::max(shi[k], float(at[k]) + reach);
    }
    for (auto& s : shapes)
    {
        if (s->hasMesh() && !s->noPaint())
        {
            const auto& b = s->getMeshBounds();
            hold(QVector3D(b.lower.x(), b.lower.y(), b.lower.z()), QVector3D(b.upper.x(), b.upper.y(), b.upper.z()));
        }
        bool uses = false;
        for (const auto& kv : vs) uses = uses || s->usesVar(kv.first);
        QVector3D lo, hi;
        if (uses && s->extentWith(vs, slo, shi, lo, hi)) hold(lo, hi);
    }
    growRegionToHold(nlo, nhi);
}

void View::growRegionToHold(const double nlo[3], const double nhi[3])
{
    bool outside = false;
    for (int k = 0; k < 3; ++k) outside = outside || nlo[k] < settings.min[k] - 1e-6 || nhi[k] > settings.max[k] + 1e-6;
    if (!outside) return;
    // (the room and the numbers the script's region gets: ScenePanel::fitRegion)
    const double longest = std::max(nhi[0] - nlo[0], std::max(nhi[1] - nlo[1], nhi[2] - nlo[2]));
    const double room = std::max(1.0, 0.1 * longest);
    Settings next = settings;
    for (int k = 0; k < 3; ++k)
    {
        next.min[k] = float(regionNumber(nlo[k] < settings.min[k] ? nlo[k] - room : double(settings.min[k]), -1).toDouble());
        next.max[k] = float(regionNumber(nhi[k] > settings.max[k] ? nhi[k] + room : double(settings.max[k]), 1).toDouble());
    }
    m_regionGrown = true;
    m_grownMin = next.min;
    m_grownMax = next.max;
    onSettingsFromScript(next, false);          // (what is shown starts meshing again, in the larger region)
}

void View::beginGizmoDrag(const GizmoGrip& g, QPoint pos)
{
    m_regionGrown = false;
    gizmo_oriented = false;
    gizmo_frame_shape = nullptr;
    gizmo_sign = 1.0f;
    if ((g.kind == 1 || g.kind == 2 || g.kind == 5 || g.kind == 6) && g.group)
    {
        // A ring or a knob of a group of points: it scales or turns them about the middle of them all.  The place of each and the numbers that move it are noted now
        QList<Shape*> members;
        QVector3D groupCenter;
        if (!groupGizmo(members, groupCenter) || !groupOfPoints(members)) return;
        gizmo_group.clear();
        gizmo_group_outline = outlineSelected();
        for (const Shape* s : members)
        {
            const auto& h = s->gizmo();
            const auto& vars = s->getVars();
            GroupPoint gp;
            gp.pos0 = s->gizmoPivot();
            for (int a = 0; a < 3; ++a)
            {
                const auto it = h.move[a] ? vars.find(h.move[a]) : vars.end();
                gp.id[a] = it != vars.end() ? h.move[a] : nullptr;
                gp.value0[a] = it != vars.end() ? it->second : 0.0f;
                const auto sc = h.scale[a] ? vars.find(h.scale[a]) : vars.end();
                gp.scaleId[a] = sc != vars.end() ? h.scale[a] : nullptr;
                gp.scale0[a] = sc != vars.end() ? sc->second : 1.0f;
                const auto ro = h.rotate[a] ? vars.find(h.rotate[a]) : vars.end();
                gp.rotId[a] = ro != vars.end() ? h.rotate[a] : nullptr;
                gp.rot0[a] = ro != vars.end() ? ro->second : 0.0f;
            }
            gizmo_group.append(gp);
        }
        gizmo_active = g;
        gizmo_press = pos;
        gizmo_pivot = groupCenter;
        if (g.kind == 5)
        {
            gizmo_factor = 1.0f;
            gizmo_centre = toScreen(gizmo_pivot);
            gizmo_press_r = std::max(8.0, std::hypot(double(pos.x()) - gizmo_centre.x(), double(pos.y()) - gizmo_centre.y()));
        }
        else if (g.kind == 2)
        {
            // (a scale knob of the outline's own gizmo: along its axis, as the knob of any shape is)
            gizmo_len = gizmoLength(gizmo_pivot);
            gizmo_factor = 1.0f;
            gizmo_dir = toScreen(gizmo_pivot + unitAxis(g.axis) * gizmo_len) - toScreen(gizmo_pivot);
            if (std::hypot(gizmo_dir.x(), gizmo_dir.y()) < 1.0) return;    // pointing at the camera
        }
        else
        {
            gizmo_axis = g.kind == 1 ? unitAxis(g.axis) : groupRingNormal(members);       // (a ring of the outline's own gizmo is about its axis)
            QVector3D at;
            if (!ringPoint(pos, gizmo_pivot, gizmo_axis, at)) return;
            gizmo_vec = at - gizmo_pivot;
            gizmo_turn = 0;
        }
        gizmo_drag = true;
        this->setCursor(Qt::ClosedHandCursor);
        emit(dragStart());
        m_overlay->update();
        return;
    }
    if (g.kind == 0 || g.kind == 3 || g.kind == 4)
    {
        // The move arrow (one axis), the plane square (two axes) and the dot (all three): the numbers that move the shape -- of every selected
        // shape, for the gizmo they share -- are noted with the values they have now
        QList<Shape*> members;
        QVector3D groupCenter;
        if (g.group) { if (!groupGizmo(members, groupCenter)) return; }
        else members << g.shape;
        gizmo_vars.clear();
        // (the gizmo of a point of a curve is along the way the curve faces there: an arrow or a square moves all three numbers, by how much of the move is along each)
        QVector3D F[3] = {QVector3D(1, 0, 0), QVector3D(0, 1, 0), QVector3D(0, 0, 1)};
        const bool turned = !g.group && (g.kind == 0 || g.kind == 4) && gizmoFrame(g.shape, F);
        gizmo_oriented = turned;
        gizmo_frame_shape = turned ? g.shape : nullptr;
        for (int a = 0; a < 3; ++a) gizmo_frame[a] = F[a];
        for (const Shape* s : members)
        {
            const auto& h = s->gizmo();
            const auto& vars = s->getVars();
            for (int a = 0; a < 3; ++a)
            {
                if (s->gizmoMapped())
                {
                    // (an image: the numbers are the original's, and each changes by the part of the image's move that is along it once it is taken back through the symmetry)
                    const QMatrix4x4 Lm = s->gizmoMapLinear();
                    const auto im = h.move[a] ? vars.find(h.move[a]) : vars.end();
                    if (im == vars.end()) continue;
                    GizmoVar mapped{h.move[a], a, im->second};
                    mapped.mapped = true;
                    mapped.w = QVector3D(Lm(0, a), Lm(1, a), Lm(2, a));
                    gizmo_vars.append(mapped);
                    continue;
                }
                if (!turned && g.kind == 0 && a != g.axis) continue;
                if (!turned && g.kind == 4 && a == g.axis) continue;       // (a plane square moves along the plane: not along its normal)
                const auto it = h.move[a] ? vars.find(h.move[a]) : vars.end();
                if (it == vars.end()) continue;
                GizmoVar var{h.move[a], a, it->second};
                if (turned && g.kind == 0) var.weight = F[g.axis][a];
                gizmo_vars.append(var);
            }
        }
        if (gizmo_vars.isEmpty()) return;
        gizmo_active = g;
        gizmo_press = pos;
        gizmo_pivot = g.group ? groupCenter : g.shape->gizmoPivot();
        if (g.kind == 0)
        {
            gizmo_len = gizmoLength(gizmo_pivot);
            // (a move arrow is along the world's axis -- along the way the curve faces, for a point of one)
            gizmo_arrow = turned ? F[g.axis] : unitAxis(g.axis);
            gizmo_dir = toScreen(gizmo_pivot + (turned ? F[g.axis] : unitAxis(g.axis)) * gizmo_len) - toScreen(gizmo_pivot);
            if (std::hypot(gizmo_dir.x(), gizmo_dir.y()) < 1.0) return;    // pointing at the camera
        }
        else if (g.kind == 4)
        {
            // The plane square: the shapes follow the cursor in that plane of the model's axes, unless it is edge on to the eye
            free_normal = turned ? F[g.axis] : unitAxis(g.axis);
            if (!ringPoint(pos, gizmo_pivot, free_normal, free_hit0)) return;
        }
        else
        {
            // The dot: the shapes follow the cursor in the plane through the pivot that faces the camera -- a point of a profile in the plane of the profile
            // (that is where its outline is, and a point dragged out of it is put back onto it), unless that plane is edge on to the eye
            free_normal = camera.towardViewer().normalized();
            int controlIndex = -1;
            Shape* owner = g.group ? nullptr : controlOwner(g.shape, controlIndex);
            double frame[9];
            QVector3D profileNormal;
            if (g.group && groupInProfilePlane(members, profileNormal))
            {
                if (std::fabs(QVector3D::dotProduct(profileNormal, free_normal)) > 0.2f) free_normal = profileNormal;      // (several points of one profile: in its plane)
            }
            else if (owner && owner->planeNow(frame))
            {
                const QVector3D u{float(frame[3]), float(frame[4]), float(frame[5])}, w{float(frame[6]), float(frame[7]), float(frame[8])};
                QVector3D planeNormal = QVector3D::crossProduct(u, w).normalized();
                if (g.shape->gizmoMapped()) planeNormal = g.shape->gizmoMapLinear().mapVector(planeNormal).normalized();      // (a copy of the point: the plane of the copy's outline)
                if (std::fabs(QVector3D::dotProduct(planeNormal, free_normal)) > 0.2f) free_normal = planeNormal;
            }
            else if (!owner && m_wp.open && m_wp.exists && std::fabs(QVector3D::dotProduct(m_wp.normal(), free_normal)) > 0.2f)
            {
                free_normal = m_wp.normal();          // (a point that is no profile's moves in the work plane while its view is open)
            }
            if (!ringPoint(pos, gizmo_pivot, free_normal, free_hit0)) return;
        }
        gizmo_drag = true;
        this->setCursor(g.kind == 3 || g.kind == 4 ? Qt::SizeAllCursor : Qt::ClosedHandCursor);
        emit(dragStart());
        m_overlay->update();
        return;
    }
    const auto& h = g.shape->gizmo();
    gizmo_id = g.kind == 1 ? h.rotate[g.axis] : h.scale[g.axis];
    const auto& vars = g.shape->getVars();
    const auto it = vars.find(gizmo_id);
    if (it == vars.end()) return;
    gizmo_value0 = it->second;
    gizmo_active = g;
    gizmo_press = pos;
    gizmo_pivot = g.shape->gizmoPivot();
    for (int a = 0; a < 3; ++a)
    {
        gizmo_scale_id[a] = h.scale[a];
        const auto sv = h.scale[a] ? vars.find(h.scale[a]) : vars.end();
        gizmo_scale0[a] = sv == vars.end() ? 1.0f : sv->second;
    }
    if (g.kind == 2)
    {
        gizmo_len = gizmoLength(gizmo_pivot);
        gizmo_factor = 1.0f;
        // The ring of a point and of the Shift gizmo scales all three numbers together, by how far the cursor is from the middle now against how far it was where the
        // ring was pressed: pulling it out to twice that distance doubles the part
        gizmo_uniform = shiftHeld() || g.shape->isPoint();
        if (gizmo_uniform)
        {
            gizmo_centre = toScreen(gizmo_pivot);
            gizmo_press_r = std::max(8.0, std::hypot(double(pos.x()) - gizmo_centre.x(), double(pos.y()) - gizmo_centre.y()));
        }
        else
        {
            // (a scale knob is along the part's own axis)
            const QVector3D e = g.shape->gizmoOrientation().mapVector(unitAxis(g.axis)) * gizmo_len;
            gizmo_dir = toScreen(gizmo_pivot + e) - toScreen(gizmo_pivot);
            if (std::hypot(gizmo_dir.x(), gizmo_dir.y()) < 1.0) return;    // pointing at the camera
        }
    }
    else
    {
        QVector3D ring[3], facePlane;
        gizmoRingAxes(g.shape, ring);
        if (planarRingNormal(g.shape, facePlane)) ring[2] = facePlane;
        gizmo_axis = ring[g.axis];
        gizmo_sign = g.shape->gizmoMapped() ? g.shape->gizmoMapSign() : 1.0f;      // (an image of a mirror image turns the other way: the original turns by the opposite)
        // (a point of a curve: the ring is about the way the curve faces, or across it -- the turn is written as the three angles of the gizmo)
        QVector3D F[3];
        const bool turned = !planarRingNormal(g.shape, facePlane) && h.rotate[0] && vars.count(h.rotate[0]) && h.rotate[1] && vars.count(h.rotate[1]) &&
                            h.rotate[2] && vars.count(h.rotate[2]) && gizmoFrame(g.shape, F);
        gizmo_oriented = turned;
        gizmo_frame_shape = turned ? g.shape : nullptr;
        if (turned)
        {
            gizmo_axis = F[g.axis];
            for (int a = 0; a < 3; ++a)
            {
                gizmo_frame[a] = F[a];
                gizmo_rot_id[a] = h.rotate[a];
                gizmo_rot0[a] = vars.at(h.rotate[a]);
            }
        }
        // (the turn is written in the original's numbers: about the axis the original has, which for a copy of a point is the copy's axis taken back through its map)
        gizmo_turn_axis = gizmo_axis;
        if (turned && g.shape->gizmoMapped())
        {
            bool ok = false;
            const QMatrix4x4 back = g.shape->gizmoMapLinear().inverted(&ok);
            if (ok) gizmo_turn_axis = back.mapVector(gizmo_axis).normalized();
        }
        QVector3D at;
        if (!ringPoint(pos, gizmo_pivot, gizmo_axis, at)) return;
        gizmo_vec = at - gizmo_pivot;
        gizmo_turn = 0;
    }
    gizmo_drag = true;
    this->setCursor(Qt::ClosedHandCursor);
    emit(dragStart());
    m_overlay->update();
}

// The turn of a ring as it is applied: with Shift held -- the Shift gizmo -- in whole steps of 5 degrees.  `absolute`: the angle that results is a multiple of the step (`start` is
// the number the ring turns from), else the turn itself is (a group of points, each with an angle of its own, keep their differences)
float View::snappedTurn(float turn, float start, bool absolute) const
{
    if (!shiftHeld()) return turn;
    const float step = 5.0f;
    return absolute ? std::round((start + turn) / step) * step - start : std::round(turn / step) * step;
}

void View::beginFilletDrag(Shape* shape, const Shape::FilletHandle& h, const QVector3D& hit)
{
    m_fillet.h = h;
    m_fillet.shape = shape;
    // (where the press was along the way the corner looks, and how far the arc's apex is from the corner now: the arc follows the hand by how far it moves, so that it does not jump)
    const QVector3D corner(float(h.corner[0]), float(h.corner[1]), float(h.corner[2])), toCentre(float(h.toCentre[0]), float(h.toCentre[1]), float(h.toCentre[2]));
    m_fillet.s0 = double(QVector3D::dotProduct(hit - corner, toCentre));
    m_fillet.apex0 = h.radius * (1.0 / h.sinHalf - 1.0);
    m_regionGrown = false;
    gizmo_oriented = false;
    gizmo_frame_shape = nullptr;
    gizmo_sign = 1.0f;
    gizmo_active = GizmoGrip();
    gizmo_active.shape = shape;
    gizmo_active.kind = 7;
    gizmo_active.axis = h.control;
    m_dotPressLine = -1;
    m_dotMoved = false;
    gizmo_drag = true;
    setCursor(Qt::ClosedHandCursor);
    emit(dragStart());
    m_overlay->update();
}

void View::dragFillet(QPoint pos)
{
    const auto& h = m_fillet.h;
    const QVector3D corner(float(h.corner[0]), float(h.corner[1]), float(h.corner[2])), toCentre(float(h.toCentre[0]), float(h.toCentre[1]), float(h.toCentre[2])),
        normal(float(h.normal[0]), float(h.normal[1]), float(h.normal[2]));
    QVector3D at;
    if (!ringPoint(pos, corner, normal, at)) return;
    // The arc's apex is `apex` from the corner along the way it looks: radius * (1 / sin(half the angle) - 1).  Pulled in, the apex goes further from the corner and the corner is rounder
    const double apex = std::max(0.0, m_fillet.apex0 + double(QVector3D::dotProduct(at - corner, toCentre)) - m_fillet.s0);
    double radius = apex * h.sinHalf / (1.0 - h.sinHalf);
    radius = std::min(radius, h.cap);                                     // (no more than the geometry allows: the arcs take no more of an edge than it is long)
    // (the point's own number is the radius before its scale: the radius is that times the scale)
    const float number = float(std::max(1e-3, radius / h.scale));
    applyGizmoNumbers({{h.var, number}});
}

void View::dragGizmo(QPoint pos)
{
    if (gizmo_active.kind == 7)
    {
        dragFillet(pos);
        return;
    }
    if ((gizmo_active.kind == 1 || gizmo_active.kind == 2 || gizmo_active.kind == 5 || gizmo_active.kind == 6) && gizmo_active.group)
    {
        // The points' places now: scaled about the middle by how far the cursor is from it against where it was pressed, or turned about it by the cursor's turn; the numbers
        // that move each point change by what it has moved.  (Kinds 1 and 2, a ring about an axis and a knob on it, are those of the outline's own gizmo; 5 and 6 those of a few points)
        std::map<libfive::Tree::Id, float> numbers;
        QMatrix4x4 turn;
        if (gizmo_active.kind == 5)
        {
            const double r = std::hypot(double(pos.x()) - gizmo_centre.x(), double(pos.y()) - gizmo_centre.y());
            gizmo_factor = std::max(0.01f, float(r / gizmo_press_r));
        }
        else if (gizmo_active.kind == 2)
        {
            // (a scale knob stretches the outline along its axis, as the knob of any shape does: pulled out by a share of its arrow, it grows by that share)
            const double l2 = QPointF::dotProduct(gizmo_dir, gizmo_dir);
            if (l2 < 1) return;
            const double t = QPointF::dotProduct(QPointF(pos - gizmo_press), gizmo_dir) / l2;
            gizmo_factor = std::max(0.01f, float(1.0 + t / kKnobAt));
        }
        else
        {
            QVector3D at;
            if (!ringPoint(pos, gizmo_pivot, gizmo_axis, at)) return;
            const QVector3D now = at - gizmo_pivot;
            if (now.length() < 1e-9f || gizmo_vec.length() < 1e-9f) return;
            const float s = QVector3D::dotProduct(gizmo_axis, QVector3D::crossProduct(gizmo_vec, now));
            const float c = QVector3D::dotProduct(gizmo_vec, now);
            gizmo_turn += float(std::atan2(s, c) * 180.0 / M_PI);
            gizmo_vec = now;
            turn.rotate(gizmo_turn, gizmo_axis);
        }
        if (!gizmo_group_outline && (gizmo_active.kind == 1 || gizmo_active.kind == 2)) return;       // (the rings and the knobs are the outline's own)
        if (gizmo_group_outline)
        {
            // A profile or a line selected as a whole: the ring or the knob scales, stretches or turns the arrangement of its points about the middle of them all -- the outline keeps
            // its shape, bigger, stretched or turned -- and the numbers that move each point change by what it has moved.  The points' own scale and rotation numbers are left as they are
            const float turnedBy = snappedTurn(gizmo_turn);
            QMatrix4x4 about;
            about.rotate(turnedBy, gizmo_axis.normalized());
            for (const GroupPoint& gp : gizmo_group)
            {
                QVector3D rel = gp.pos0 - gizmo_pivot;
                if (gizmo_active.kind == 5) rel *= gizmo_factor;
                else if (gizmo_active.kind == 2) rel[gizmo_active.axis] *= gizmo_factor;
                else rel = about.mapVector(rel);
                const QVector3D now = gizmo_pivot + rel;
                const QVector3D d = now - gp.pos0;
                for (int a = 0; a < 3; ++a)
                {
                    if (gp.id[a]) numbers[gp.id[a]] = gp.value0[a] + d[a];
                }
            }
            if (!numbers.empty()) applyGizmoNumbers(numbers);
            m_overlay->update();
            return;
        }
        // The points stay where they are: the ring scales every point by its own scale, or turns every point by its own rotation, each one alone -- it does not make the
        // arrangement of the points bigger or turn it about its middle
        const float turned = snappedTurn(gizmo_turn);
        const QVector3D axisN = gizmo_axis.normalized();
        for (const GroupPoint& gp : gizmo_group)
        {
            if (gizmo_active.kind == 5)
            {
                for (int a = 0; a < 3; ++a)
                    if (gp.scaleId[a]) numbers[gp.scaleId[a]] = gp.scale0[a] * gizmo_factor;
                continue;
            }
            // (the turn about an axis of the world that the point turns about by one of its own numbers: that number changes by the angle; else the three angles are made again)
            int main = -1;
            float sign = 1.0f;
            for (int a = 0; a < 3; ++a)
            {
                const float d = QVector3D::dotProduct(axisN, unitAxis(a));
                if (std::fabs(d) > 0.9999f) { main = a; sign = d > 0 ? 1.0f : -1.0f; }
            }
            if (main >= 0 && gp.rotId[main])
            {
                numbers[gp.rotId[main]] = gp.rot0[main] + sign * turned;
            }
            else if (gp.rotId[0] && gp.rotId[1] && gp.rotId[2])
            {
                QMatrix4x4 t;
                t.rotate(turned, axisN);
                const QVector3D angles = gizmoEuler(t * Shape::gizmoRotation(QVector3D(gp.rot0[0], gp.rot0[1], gp.rot0[2])));
                for (int a = 0; a < 3; ++a) numbers[gp.rotId[a]] = angles[a];
            }
        }
        if (!numbers.empty()) applyGizmoNumbers(numbers);
        m_overlay->update();
        return;
    }
    if (gizmo_active.kind == 0)
    {
        const double l2 = QPointF::dotProduct(gizmo_dir, gizmo_dir);
        if (l2 < 1) return;
        const double t = QPointF::dotProduct(QPointF(pos - gizmo_press), gizmo_dir) / l2;
        std::map<libfive::Tree::Id, float> numbers;          // (every shape that moves with the arrow)
        float distance = float(t * gizmo_len);
        // A symmetry, while the work plane's view is open and its grid snaps: its centre lands on the grid.  An arrow along one of the plane's own axes moves it along a line of
        // the grid -- the centre goes to the nearest of its crossings along the arrow, as a point put down does (the dot and the squares of the gizmo go through
        // snapToWorkPlane already)
        if (m_wp.open && m_wp.exists && m_wp.snap && gizmo_active.shape && gizmo_active.shape->isSymmetry())
        {
            const bool alongU = std::fabs(std::fabs(QVector3D::dotProduct(gizmo_arrow, m_wp.u)) - 1.0f) < 1e-3f;
            const bool alongV = std::fabs(std::fabs(QVector3D::dotProduct(gizmo_arrow, m_wp.v)) - 1.0f) < 1e-3f;
            if (alongU || alongV)
            {
                const float step = workPlaneStep();
                const float before = QVector3D::dotProduct(gizmo_pivot - m_wp.origin(), gizmo_arrow);
                distance = std::round((before + distance) / step) * step - before;
            }
        }
        for (const auto& v : gizmo_vars)
            numbers[v.id] = v.value0 + distance * (v.mapped ? QVector3D::dotProduct(v.w, gizmo_arrow) : v.weight);
        applyGizmoNumbers(numbers);
    }
    else if (gizmo_active.kind == 3 || gizmo_active.kind == 4)
    {
        // The dot (the plane facing the camera) and a plane square (that plane of the axes): where the cursor's ray meets the plane, against where it met it at the start
        QVector3D at;
        if (!ringPoint(pos, gizmo_pivot, free_normal, at)) return;
        QVector3D d = at - free_hit0;
        // In a plane that is parallel to the work plane's, while its view is open: the place it lands on is on the grid (or on a point that is there)
        if (m_wp.open && m_wp.exists && std::fabs(QVector3D::dotProduct(free_normal, m_wp.normal())) > 0.999f)
        {
            d = snapToWorkPlane(gizmo_pivot + d, gizmo_active.shape) - gizmo_pivot;
            d -= m_wp.normal() * QVector3D::dotProduct(d, m_wp.normal());                        // (in the plane: the noise of the ray is not a move out of it)
        }
        std::map<libfive::Tree::Id, float> numbers;
        for (const auto& v : gizmo_vars) numbers[v.id] = v.value0 + (v.mapped ? QVector3D::dotProduct(v.w, d) : d[v.axis]);
        applyGizmoNumbers(numbers);
    }
    else if (gizmo_active.kind == 2)
    {
        float factor;
        if (gizmo_uniform)
        {
            // (the ring: how far the cursor is from the middle, against where it was when the ring was pressed)
            factor = float(std::hypot(double(pos.x()) - gizmo_centre.x(), double(pos.y()) - gizmo_centre.y()) / gizmo_press_r);
        }
        else
        {
            const double l2 = QPointF::dotProduct(gizmo_dir, gizmo_dir);
            if (l2 < 1) return;
            // (the knob sits at kKnobAt of the arrow: pulling it out by a share of that arrow grows the part by it)
            const double t = QPointF::dotProduct(QPointF(pos - gizmo_press), gizmo_dir) / l2;
            factor = float(1.0 + t / kKnobAt);
        }
        gizmo_factor = factor;
        std::map<libfive::Tree::Id, float> numbers;
        const bool together = gizmo_uniform;            // (the one knob of a point and of the Shift gizmo: all three numbers)
        for (int a = 0; a < 3; ++a)
        {
            if (!gizmo_scale_id[a]) continue;
            if (!together && a != gizmo_active.axis) continue;
            numbers[gizmo_scale_id[a]] = std::max(0.01f, gizmo_scale0[a] * factor);
        }
        if (!numbers.empty()) applyGizmoNumbers(numbers);
    }
    else
    {
        QVector3D at;
        if (!ringPoint(pos, gizmo_pivot, gizmo_axis, at)) return;
        const QVector3D now = at - gizmo_pivot;
        if (now.length() < 1e-9f || gizmo_vec.length() < 1e-9f) return;
        // the turn since the last move (small, so never across +-180)
        const float s = QVector3D::dotProduct(gizmo_axis, QVector3D::crossProduct(gizmo_vec, now));
        const float c = QVector3D::dotProduct(gizmo_vec, now);
        gizmo_turn += float(std::atan2(s, c) * 180.0 / M_PI);
        gizmo_vec = now;
        if (gizmo_oriented)
        {
            // A turn about an axis of the curve's frame, on top of the turn the point had: the three angles of the sum
            QMatrix4x4 turn;
            turn.rotate(snappedTurn(gizmo_sign * gizmo_turn), gizmo_turn_axis);         // (a copy: the turn of the original that the copy's turn is, see gizmo_sign)
            const QVector3D angles = gizmoEuler(turn * Shape::gizmoRotation(QVector3D(gizmo_rot0[0], gizmo_rot0[1], gizmo_rot0[2])));
            std::map<libfive::Tree::Id, float> numbers;
            for (int a = 0; a < 3; ++a) numbers[gizmo_rot_id[a]] = angles[a];
            applyGizmoNumbers(numbers);
        }
        else
        {
            applyGizmoNumber(gizmo_id, gizmo_value0 + snappedTurn(gizmo_sign * gizmo_turn, gizmo_value0, true));
        }
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
    for (const auto& kv : m_viewers)
    {
        const FieldViewer* v = kv.second.get();
        if (!v->open || !v->shown || !v->plane.slice.valid() || v->plane.slice.axis != v->axis) continue;
        // The field's value under the cursor, on its plane (the sample nearest to it)
        const FieldSlice& slice = v->plane.slice;
        const int ax = v->axis, ua = FieldSlice::uAxis(ax), va = FieldSlice::vAxis(ax);
        QVector3D x;
        float t = 0;
        if (!meet(ax, fieldPlace(*v)[ax], t, x)) continue;
        const float d = slice.sample(x[ua], x[va]);
        const float du = slice.max[ua] - slice.min[ua], dv = slice.max[va] - slice.min[va];
        const int i = int((x[ua] - slice.min[ua]) / du * slice.w), j = int((x[va] - slice.min[va]) / dv * slice.h);
        if (std::isfinite(d) && d < 0 && slice.hasColor && slice.color.size() == slice.w * slice.h &&
            du > 0 && dv > 0 && i >= 0 && j >= 0 && i < slice.w && j < slice.h &&
            std::isfinite(slice.color[j * slice.w + i]) && (fieldText.isEmpty() || t < fieldT))
        {
            fieldT = t;
            fieldText = QString("%1 = %2").arg(fieldLabel(v->key)).arg(slice.color[j * slice.w + i], 0, 'g', 5);
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
