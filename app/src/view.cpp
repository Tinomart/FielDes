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
#include <QComboBox>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
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

#include "fieldes/color.hpp"
#include "fieldes/view.hpp"
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
    auto forResults = [this](auto fn, bool resample) {
        for (auto s : shapes) if (s->hasResult()) fn(s);
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

    // Section field sampling: debounced, runs in a worker thread
    slice_timer.setSingleShot(true);
    slice_timer.setInterval(40);
    connect(&slice_timer, &QTimer::timeout, this, &View::startSlice);
    connect(&slice_watcher, &QFutureWatcher<FieldSlice>::finished,
            this, &View::onSliceFinished);
}

View::~View()
{
    slice_generation++;              // abandon any sampling in flight
    slice_watcher.waitForFinished();
    makeCurrent();
    slice_tex.reset();
    slice_color_tex.reset();
    if (slice_vao.isCreated()) slice_vao.destroy();
    if (slice_vbo.isCreated()) slice_vbo.destroy();
    for (auto s : findChildren<Shape*>())
    {
        s->freeGL();
    }
    doneCurrent();
}

void View::setShapes(QList<Shape*> new_shapes)
{
    // We're going to co-optimize every single new and old tree together,
    // so that we can deduplicate them.  This could be expensive; if we
    // notice the main thread lagging, we could do the new_shapes half of this
    // co-optimization on the worker thread, but would then need to pass
    // the map from the thread.
    std::unordered_map<libfive::TreeDataKey, libfive::Tree> canonical;

    // Pack tree IDs into a pair of sets for fast checking
    std::map<libfive::Tree::Id, Shape*> new_shapes_map;
    std::map<libfive::Tree::Id, libfive::Tree::Id> new_shapes_canonical;
    for (auto& s : new_shapes)
    {
        auto c = s->getUniqueId(canonical);
        new_shapes_canonical.insert({s->id(), c});
        new_shapes_map.insert({c, s});
    }

    // Erase all existing shapes that aren't in the new_shapes list
    bool vars_changed = false;
    bool any_running = false;
    for (auto itr=shapes.begin(); itr != shapes.end(); /* no update */ )
    {
        auto n = new_shapes_map.find((*itr)->getUniqueId(canonical));
        // Same geometry coloured differently (another field or range) is a
        // different shape: its colours are computed while meshing
        if (n != new_shapes_map.end() && n->second->colorKey() != (*itr)->colorKey())
        {
            n = new_shapes_map.end();
        }
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
        if (new_shapes_map.count(new_shapes_canonical[s->id()]))
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

void View::focusOn(QVector3D min, QVector3D max, QList<int> lines0)
{
    if ((max - min).length() <= 0)
    {
        bool any = false;
        for (auto& s : shapes)
        {
            if (!lines0.contains(s->sourceLine()) || !s->hasMesh()) continue;
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
        if (!any) return;
    }
    if ((max - min).length() <= 0) return;
    camera.zoomTo(min, max);
}

bool View::meshBounds(QVector3D& min, QVector3D& max) const
{
    bool any = false;
    for (auto& s : shapes)
    {
        if (!s->hasMesh()) continue;
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
    for (auto& s : shapes) any |= s->hasMesh();
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
        emit(startRender(s));
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
        s->drawMonochrome(m, color++);
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
            auto err = QString(
                    "Error:<br><br>"
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
    for (auto& s : shapes)
    {
        // The boundary conditions are drawn on the part's own surface: pulled a hair towards the eye, they win over the part when both are shown
        const bool onTheSurface = !s->boundaryGlyphs().empty();
        if (onTheSurface)
        {
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(-1.0f, -1.0f);
        }
        s->draw(m);
        if (onTheSurface) glDisable(GL_POLYGON_OFFSET_FILL);
    }
    if (clipping)
    {
        setClipUniform(false);           // back to normal shading
        glDisable(GL_CLIP_DISTANCE0);
    }
    if (section.enabled && section.field)
    {
        drawSlicePlane(m);
    }

    // The arrows and pads of boundary conditions, over the model they belong to
    for (auto& s : shapes)
        for (const auto& g : s->boundaryGlyphs())
        {
            float r = 0.8f, gr = 0.8f, b = 0.8f;
            bcCategoryRGB(g.kind, r, gr, b);
            const QColor color = QColor::fromRgbF(r, gr, b);
            if (g.kind == 1 || g.kind == 2)
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

    if (probe_valid && cursor_pos_valid)
    {
        // The field's value at the surface point under the cursor
        QFont font = painter.font();
        font.setPointSizeF(9);
        painter.setFont(font);
        const QString text = (probe_label.isEmpty() ? QString("value") : probe_label) +
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
    const QString text = "Rendering";
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
        // (analysis results have their own card)
        if (!s->hasColorField() || !s->hasMesh() || s->hasResult() || s->colorMap() == "bc") continue;
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
    int right = width() - 14;

    // (only selected surfaces shown: the key is called so)
    const QString bcTitle = (bcCategories.size() == 1 && bcCategories.contains(6)) ? "Selection" : "Boundary conditions";
    if (!bcCategories.isEmpty() && !hidden_legends.contains(bcTitle))
    {
        static const char* names[7] = {"", "Fixed support", "Sliding support", "Force", "Gravity", "Heat",
                                       "Selected surface"};
        QList<int> cats = bcCategories.values();
        std::sort(cats.begin(), cats.end());
        int textW = fm.horizontalAdvance(bcTitle) + 16;
        for (int c : cats) textW = std::max(textW, 18 + fm.horizontalAdvance(names[std::max(0, std::min(6, c))]));
        const int rowH = fm.height() + 4;
        const int boxW = textW + 20, boxH = fm.height() + 20 + rowH * cats.size();
        const QRect box(right - boxW, height() - boxH - 12, boxW, boxH);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(22, 76, 94, 200));
        painter.drawRoundedRect(box, 6, 6);
        painter.setPen(QColor(0xee, 0xe8, 0xd5));
        painter.drawText(QRect(box.left() + 10, box.top() + 6, boxW - 20, fm.height()),
                         Qt::AlignLeft | Qt::AlignVCenter, bcTitle);
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
                             names[std::max(0, std::min(6, c))]);
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
    camera.size = {width, height};
    pick_timer.start();
    if (m_overlay)
    {
        m_overlay->setGeometry(rect());
    }
    placeResultPanel();
}

void View::placeResultPanel()
{
    if (!m_resultPanel || !m_resultPanel->isVisible()) return;
    m_resultPanel->adjustSize();
    const int h = std::min(m_resultPanel->sizeHint().height(), std::max(160, height() - 180));
    m_resultPanel->setGeometry(width() - m_resultPanel->width() - 12, height() - h - 44,
                               m_resultPanel->width(), h);
}

void View::updateResultPanel()
{
    Shape* r = nullptr;
    for (auto s : shapes) if (s->hasResult()) { r = s; break; }
    if (!r)
    {
        m_resultPanel->hide();
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
    if (mouse.state == mouse.RELEASED)
    {
        const HandleGrip grip = handleAt(event->pos());
        if (!(grip == handle_hover))
        {
            handle_hover = grip;
            m_overlay->update();
        }
        const bool hover = sectionHandleHit(event->pos());
        if (hover != section_hover)
        {
            section_hover = hover;
            setCursor(hover ? Qt::SizeAllCursor : Qt::ArrowCursor);
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
            setCursor(Qt::PointingHandCursor);
        }
    }
    mouse.pos = event->pos();
}

QVector3D View::toModelPos(QPoint pt) const
{
    float pick_z = 0;
    if (pick_img.valid(pt))
    {
        pick_z = 2 * pick_depth.at(
                    pt.x() + pick_img.width() *
                    (pick_img.height() - pt.y() - 1)) - 1;
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
        if (event->button() == Qt::LeftButton)
        {
            syncPicker();
            auto picked = pick_img.valid(event->pos())
                ? (pick_img.pixel(event->pos()) & 0xFFFFFF) : 0;
            drag_target = (picked && int(picked) <= shapes.size())
                ? shapes.at(picked - 1) : nullptr;
            press_pos = event->pos();
            press_target = drag_target;
            // (a part in the gizmo mode is moved by the gizmo and a locked one not at all: only its
            // own surfaces, made of numbers that can change, are pulled)
            if (picked && drag_target->nativeDragOk())
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
                drag_target = nullptr;
                mouse.state = mouse.DRAG_ROT;
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
            right_press_point = right_press_target ? toModelPos(event->pos()) : QVector3D();
            mouse.state = mouse.DRAG_PAN;
        }
    }
}

void View::mouseReleaseEvent(QMouseEvent* event)
{
    QOpenGLWidget::mouseReleaseEvent(event);
    event->accept();
    if (section_drag)
    {
        section_drag = false;
        m_overlay->update();
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

    // A left click that didn't drag selects what was under the cursor
    if (event->button() == Qt::LeftButton &&
        (event->pos() - press_pos).manhattanLength() < 4)
    {
        const int line = (press_target && shapes.contains(press_target))
            ? press_target->sourceLine() : -1;
        emit(shapeClicked(line));
    }
    press_target = nullptr;

    if (event->button() == Qt::RightButton && right_press_target &&
        (event->pos() - right_press_pos).manhattanLength() < 4 && shapes.contains(right_press_target))
    {
        Shape* target = right_press_target;
        right_press_target = nullptr;
        mouse.state = mouse.RELEASED;
        showSelectMenu(event->globalPos(), target, right_press_point);
        return;
    }
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

bool View::selectSurfaceAt(QPoint pos, const QString& mode, double angle, double thickness, double radius)
{
    syncPicker();
    const auto picked = pick_img.valid(pos) ? (pick_img.pixel(pos) & 0xFFFFFF) : 0;
    if (!picked || int(picked) > shapes.size()) return false;
    Shape* target = shapes.at(picked - 1);
    if (target->sourceLine() < 0) return false;
    emit(surfaceSelectRequested(target->sourceLine(), toModelPos(pos), mode, angle, thickness, radius));
    return true;
}

void View::showSelectMenu(QPoint globalPos, Shape* target, const QVector3D& point)
{
    const int line = target->sourceLine();
    if (line < 0) return;

    QSettings store;
    QMenu menu(this);
    auto title = menu.addAction("Select surface (flood fill)");
    title->setEnabled(false);
    menu.addSeparator();

    auto holder = new QWidget;
    auto form = new QFormLayout(holder);
    form->setContentsMargins(10, 4, 10, 6);
    auto mode = new QComboBox;
    mode->addItem("Flat face", "flat");
    mode->addItem("Round / smooth faces", "smooth");
    mode->setToolTip("Flat: spreads while the surface faces the way it does at the click.\n"
                     "Smooth: spreads over round faces (cylinders, fillets) up to a sharp edge.");
    mode->setCurrentIndex(store.value("select/mode", "flat").toString() == "smooth" ? 1 : 0);
    auto angle = new QDoubleSpinBox;
    angle->setRange(0.5, 90.0);
    angle->setSuffix(" deg");
    angle->setDecimals(1);
    angle->setToolTip("Flat: how far from the click's direction the surface may face.\n"
                      "Smooth: how much it may turn from one triangle to the next.");
    auto defaultAngle = [mode]{ return mode->currentData().toString() == "smooth" ? 30.0 : 10.0; };
    angle->setValue(store.value("select/angle-" + mode->currentData().toString(), defaultAngle()).toDouble());
    connect(mode, QOverload<int>::of(&QComboBox::currentIndexChanged), angle, [=](int) {
        QSettings s;
        angle->setValue(s.value("select/angle-" + mode->currentData().toString(), defaultAngle()).toDouble());
    });
    auto thickness = new QDoubleSpinBox;
    thickness->setRange(0.0, 1000.0);
    thickness->setDecimals(2);
    thickness->setSuffix(" mm");
    thickness->setSpecialValueText("automatic");
    thickness->setValue(store.value("select/thickness", 0.0).toDouble());
    thickness->setToolTip("How thick the field across the patch is (automatic: a hundredth of the model).");
    auto radius = new QDoubleSpinBox;
    radius->setRange(0.0, 100000.0);
    radius->setDecimals(1);
    radius->setSuffix(" mm");
    radius->setSpecialValueText("no limit");
    radius->setValue(store.value("select/radius", 0.0).toDouble());
    radius->setToolTip("Stop this far from the click.");
    form->addRow("Spread", mode);
    form->addRow("Angle", angle);
    form->addRow("Thickness", thickness);
    form->addRow("Radius", radius);
    auto buttons = new QHBoxLayout;
    auto select = new QPushButton("Select");
    select->setDefault(true);
    auto cancel = new QPushButton("Cancel");
    buttons->addStretch();
    buttons->addWidget(cancel);
    buttons->addWidget(select);
    form->addRow(buttons);

    bool confirmed = false;
    connect(select, &QPushButton::clicked, &menu, [&]{ confirmed = true; menu.close(); });
    connect(cancel, &QPushButton::clicked, &menu, [&]{ menu.close(); });
    auto action = new QWidgetAction(&menu);
    action->setDefaultWidget(holder);
    menu.addAction(action);

    menu.exec(globalPos);
    if (!confirmed) return;
    const QString m = mode->currentData().toString();
    store.setValue("select/mode", m);
    store.setValue("select/angle-" + m, angle->value());
    store.setValue("select/thickness", thickness->value());
    store.setValue("select/radius", radius->value());
    emit(surfaceSelectRequested(line, point, m, angle->value(), thickness->value(), radius->value()));
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
    auto target = picked ? shapes.at(picked - 1) : nullptr;
    if (target && target->nativeDragOk())
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
    probe_valid = target && target->hasColorField() && target->colorMap() != "bc" && legend_on &&
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

void View::requestSlice()
{
    if (section.enabled && section.field)
    {
        slice_timer.start();
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

void View::startSliceAt(bool fine)
{
    if (slice_watcher.isRunning())
    {
        // Abandon the one in flight; we'll restart once it returns
        slice_generation++;
        slice_timer.start();
        return;
    }
    QVector<FieldSource> sources;
    for (auto& s : shapes)
    {
        sources.push_back(sourceOf(s));
    }
    const int gen = ++slice_generation;
    const SectionSettings sec = section;
    QVector3D lo, hi;
    float fade;
    sliceRegion(lo, hi, fade);
    // A quick preview first (for dragging), then the full resolution
    const int samples = fine ? 720 : 180;
    std::atomic<int>* g = &slice_generation;
    slice_watcher.setFuture(QtConcurrent::run([=]() {
        const auto t0 = std::chrono::steady_clock::now();
        FieldSlice s = sampleField(sources, sec, lo, hi, samples, fade, g, gen);
        s.fine = fine;
        if (std::getenv("FIELDES_TIMING") && s.valid())
        {
            std::cerr << "[slice] " << (fine ? "fine" : "quick") << " plane at " << sec.offset << ": "
                      << std::fixed << 1000.0 * std::chrono::duration<double>(
                             std::chrono::steady_clock::now() - t0).count()
                      << " ms (" << sources.size() << " shapes evaluated)" << std::endl;
        }
        return s;
    }));
}

void View::onSliceFinished()
{
    const FieldSlice s = slice_watcher.result();
    if (s.generation == slice_generation.load() && s.valid())
    {
        slice = s;
        slice_dirty = true;
        emit(sliceReady(slice));
        update();
        if (!s.fine && !slice_timer.isActive())
        {
            startSliceAt(true);
        }
    }
    else if (section.enabled && section.field && s.generation != slice_generation.load())
    {
        slice_timer.start();   // settings moved on while this one ran
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

void View::drawSlicePlane(const QMatrix4x4& m)
{
    if (!slice.valid())
    {
        return;
    }
    if (slice_dirty)
    {
        // Raw distances (non-finite = nothing there, i.e. far outside)
        QVector<float> values = slice.values;
        for (auto& v : values)
        {
            if (!std::isfinite(v)) v = 1e30f;
        }
        slice_tex.reset(new QOpenGLTexture(QOpenGLTexture::Target2D));
        slice_tex->setFormat(QOpenGLTexture::R32F);
        slice_tex->setSize(slice.w, slice.h);
        slice_tex->allocateStorage(QOpenGLTexture::Red, QOpenGLTexture::Float32);
        slice_tex->setData(QOpenGLTexture::Red, QOpenGLTexture::Float32, values.constData());
        slice_tex->setMinificationFilter(QOpenGLTexture::Linear);
        slice_tex->setMagnificationFilter(QOpenGLTexture::Linear);
        slice_tex->setWrapMode(QOpenGLTexture::ClampToEdge);
        slice_color_tex.reset();
        if (slice.hasColor && slice.color.size() == slice.values.size())
        {
            slice_color_tex.reset(new QOpenGLTexture(QOpenGLTexture::Target2D));
            slice_color_tex->setFormat(QOpenGLTexture::R32F);
            slice_color_tex->setSize(slice.w, slice.h);
            slice_color_tex->allocateStorage(QOpenGLTexture::Red, QOpenGLTexture::Float32);
            slice_color_tex->setData(QOpenGLTexture::Red, QOpenGLTexture::Float32, slice.color.constData());
            // nearest: NaN (outside) must not bleed into its neighbours
            slice_color_tex->setMinificationFilter(QOpenGLTexture::Nearest);
            slice_color_tex->setMagnificationFilter(QOpenGLTexture::Nearest);
            slice_color_tex->setWrapMode(QOpenGLTexture::ClampToEdge);
        }
        slice_grid_verts = 0;       // (the deformed grid is rebuilt)
        slice_dirty = false;
    }
    if (!slice_tex)
    {
        return;
    }

    // Elements shown whole: they are the section (the plane would cut
    // through them)
    for (auto s : shapes)
    {
        if (s->hasResult() && s->showElements() && section.wholeElements && slice_color_tex) return;
    }

    // The plane rectangle at the CURRENT offset (the texture may lag a
    // few milliseconds behind while dragging the slider)
    const int a = section.axis;
    const int ua = FieldSlice::uAxis(a), va = FieldSlice::vAxis(a);
    float deform = 0;
    for (auto s : shapes)
    {
        if (s->hasDeformation()) { deform = s->deformScale(); break; }
    }
    const bool deformed = deform != 0 && !slice.disp.isEmpty() &&
                          slice.disp.size() == slice.gw * slice.gh;
    auto corner = [&](float u, float v) {
        QVector3D p;
        p[a] = section.offset;
        p[ua] = u;
        p[va] = v;
        return p;
    };
    const QVector3D c[4] = {
        corner(slice.min[ua], slice.min[va]), corner(slice.max[ua], slice.min[va]),
        corner(slice.max[ua], slice.max[va]), corner(slice.min[ua], slice.max[va])};
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
    if (!slice_vao.isCreated())
    {
        slice_vao.create();
        slice_vbo.create();
    }
    slice_vao.bind();
    slice_vbo.bind();
    if (deformed)
    {
        if (slice_grid_generation != slice.generation || slice_grid_scale != deform ||
            slice_grid_offset != section.offset || slice_grid_verts == 0)
        {
            // Each grid vertex moved by its (scaled) displacement
            std::vector<GLfloat> grid;
            grid.reserve(size_t(slice.gw - 1) * (slice.gh - 1) * 6 * 5);
            auto vert = [&](int i, int j) {
                const float s = float(i) / (slice.gw - 1), t = float(j) / (slice.gh - 1);
                QVector3D p;
                p[a] = section.offset;
                p[ua] = slice.min[ua] + s * (slice.max[ua] - slice.min[ua]);
                p[va] = slice.min[va] + t * (slice.max[va] - slice.min[va]);
                p += deform * slice.disp[j * slice.gw + i];
                grid.insert(grid.end(), {p.x(), p.y(), p.z(), s, t});
            };
            for (int j = 0; j + 1 < slice.gh; ++j)
                for (int i = 0; i + 1 < slice.gw; ++i)
                {
                    vert(i, j); vert(i + 1, j); vert(i + 1, j + 1);
                    vert(i, j); vert(i + 1, j + 1); vert(i, j + 1);
                }
            slice_vbo.allocate(grid.data(), int(grid.size() * sizeof(GLfloat)));
            slice_grid_verts = int(grid.size() / 5);
            slice_grid_generation = slice.generation;
            slice_grid_scale = deform;
            slice_grid_offset = section.offset;
        }
    }
    else
    {
        slice_grid_verts = 0;
        slice_vbo.allocate(data, sizeof(data));
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
    glUniform1f(Shader::slice->uniformLocation("opacity"), section.opacity);
    glUniform1f(Shader::slice->uniformLocation("range_in"), std::max(slice.rangeIn, 1e-9f));
    glUniform1f(Shader::slice->uniformLocation("range_out"), std::max(slice.rangeOut, 1e-9f));
    glUniform1f(Shader::slice->uniformLocation("spacing"), std::max(slice.spacing, 1e-9f));
    glUniform1f(Shader::slice->uniformLocation("fade"), std::max(slice.fade, 1e-9f));
    glActiveTexture(GL_TEXTURE0);
    slice_tex->bind(0);
    glUniform1i(Shader::slice->uniformLocation("field"), 0);
    int colorMode = 0;
    if (slice_color_tex)
    {
        colorMode = slice.colorMap == "viridis" ? 2 : (slice.colorMap == "grey" || slice.colorMap == "gray") ? 3 : 1;
        slice_color_tex->bind(1);
        glUniform1i(Shader::slice->uniformLocation("color_field"), 1);
        glUniform1f(Shader::slice->uniformLocation("color_lo"), slice.colorLo);
        glUniform1f(Shader::slice->uniformLocation("color_hi"), slice.colorHi);
    }
    glUniform1i(Shader::slice->uniformLocation("color_mode"), colorMode);
    if (deformed) glDrawArrays(GL_TRIANGLES, 0, slice_grid_verts);
    else glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    if (slice_color_tex) slice_color_tex->release(1);
    glActiveTexture(GL_TEXTURE0);
    slice_tex->release(0);
    Shader::slice->release();
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDisable(GL_BLEND);
    slice_vbo.release();
    slice_vao.release();
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
    QVector3D n;
    n[section.axis] = 1;
    p = toScreen(c);
    q = toScreen(c + n * length);
    return std::isfinite(p.x()) && std::isfinite(q.x());
}

bool View::sectionHandleHit(QPoint pos) const
{
    if (!section.enabled) return false;
    QPointF p, q;
    float len;
    if (!sectionHandle(p, q, len)) return false;
    // The knob, or anywhere along the double arrow
    const QPointF a = p - (q - p), b = q;
    const QPointF ab = b - a, ap = QPointF(pos) - a;
    const double l2 = QPointF::dotProduct(ab, ab);
    const double t = l2 > 0 ? std::max(0.0, std::min(1.0, QPointF::dotProduct(ap, ab) / l2)) : 0.0;
    const QPointF d = QPointF(pos) - (a + t * ab);
    return std::hypot(d.x(), d.y()) < 9;
}

void View::drawSectionHandle(QPainter& painter)
{
    QPointF p, q;
    float len;
    if (!sectionHandle(p, q, len)) return;
    const QPointF back = p - (q - p);
    const bool hot = section_hover || section_drag;
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

View::HandleGrip View::handleAt(QPoint pos) const
{
    HandleGrip best;
    double bestDist = 9.0;
    const QPointF at(pos);
    for (Shape* s : shapes)
    {
        if (!s->hasHandles()) continue;
        const auto& h = s->handles();
        const auto& vars = s->getVars();
        const QVector3D P = s->handlePivot();
        const float L = handleLength(P);
        if (!(L > 0)) continue;
        const QPointF p0 = toScreen(P);
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
    for (Shape* s : shapes)
    {
        if (!s->hasHandles()) continue;
        const QVector3D P = s->handlePivot();
        const float L = handleLength(P);
        if (!(L > 0)) continue;
        if (kind == 0)
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

void View::drawHandles(QPainter& painter)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    for (Shape* s : shapes)
    {
        if (!s->hasHandles()) continue;
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
        painter.setPen(QPen(QColor(0xee, 0xe8, 0xd5), 1.5));
        painter.setBrush(QColor(38, 139, 210));
        painter.drawEllipse(p0, 4.5, 4.5);
    }
    painter.restore();
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
    m_overlay->update();
}

void View::beginHandleDrag(const HandleGrip& g, QPoint pos)
{
    const auto& h = g.shape->handles();
    handle_id = g.kind == 0 ? h.move[g.axis] : g.kind == 1 ? h.rotate[g.axis] : h.scale[g.axis];
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
    if (g.kind == 0 || g.kind == 2)
    {
        handle_len = handleLength(handle_pivot);
        // (a move arrow is along the world's axis, a scale knob along the part's own)
        const QVector3D e = (g.kind == 0 ? unitAxis(g.axis)
                                         : Shape::handleRotation(g.shape->handleRotate()).mapVector(unitAxis(g.axis)))
                            * handle_len;
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
        applyHandleNumber(handle_id, handle_value0 + float(t * handle_len));
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
    QString text;
    if (section.enabled && section.field && slice.valid() && slice.axis == section.axis)
    {
        // The pixel's line of sight, and where it meets the plane
        // (NDC depth 0.5 is infinitely far with this camera's perspective)
        const QVector3D a = toModelPos(pos, 0), b = toModelPos(pos, 0.25);
        const int ax = section.axis;
        if (b[ax] != a[ax] && std::isfinite(a[ax]) && std::isfinite(b[ax]))
        {
            const float t = (section.offset - a[ax]) / (b[ax] - a[ax]);
            {
                const QVector3D x = a + t * (b - a);
                const float d = slice.sample(x[FieldSlice::uAxis(ax)], x[FieldSlice::vAxis(ax)]);
                if (std::isfinite(d) && d < slice.fade)
                {
                    text = QString("d = %1  %2").arg(d, 0, 'g', 4)
                               .arg(d < 0 ? "inside" : "outside");
                }
            }
        }
    }
    section_readout_pos = pos;
    if (text != section_readout)
    {
        section_readout = text;
        emit(sectionReadout(text));
    }
    if (!text.isEmpty() || !section_readout.isEmpty())
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
