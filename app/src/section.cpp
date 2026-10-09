/*
FielDes: field-driven design
Derived from Studio, a simple GUI for the libfive CAD kernel

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <thread>
#include <vector>

#include <QButtonGroup>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSignalBlocker>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

#include "libfive/eval/eval_array.hpp"

#include "fieldes/i18n.hpp"
#include "fieldes/carddrag.hpp"
#include "fieldes/section.hpp"
#include "fieldes/colormap.hpp"

namespace FielDes {

////////////////////////////////////////////////////////////////////////////////

bool SectionSettings::operator==(const SectionSettings& o) const
{
    return enabled == o.enabled && axis == o.axis && offset == o.offset &&
           flip == o.flip && clip == o.clip && field == o.field &&
           opacity == o.opacity && range == o.range && spacing == o.spacing &&
           wholeElements == o.wholeElements;
}

QVector4D SectionSettings::clipPlane() const
{
    QVector3D n(0, 0, 0);
    n[axis] = 1;
    // Default: keep the half below the plane (cut away x > offset), so the
    // exposed face looks back at a camera on the positive side
    if (!flip) return QVector4D(-n, offset);
    return QVector4D(n, -offset);
}

////////////////////////////////////////////////////////////////////////////////

float FieldSlice::sample(float u, float v) const
{
    const int ua = uAxis(axis), va = vAxis(axis);
    if (!valid()) return std::numeric_limits<float>::quiet_NaN();
    const float fu = (u - min[ua]) / (max[ua] - min[ua]) * w - 0.5f;
    const float fv = (v - min[va]) / (max[va] - min[va]) * h - 0.5f;
    if (fu < -0.5f || fv < -0.5f || fu > w - 0.5f || fv > h - 0.5f)
    {
        return std::numeric_limits<float>::quiet_NaN();
    }
    const int i0 = std::max(0, std::min(w - 1, int(std::floor(fu))));
    const int j0 = std::max(0, std::min(h - 1, int(std::floor(fv))));
    const int i1 = std::min(w - 1, i0 + 1), j1 = std::min(h - 1, j0 + 1);
    const float tu = std::max(0.f, std::min(1.f, fu - i0));
    const float tv = std::max(0.f, std::min(1.f, fv - j0));
    auto at = [&](int i, int j) { return values[j * w + i]; };
    return (at(i0, j0) * (1 - tu) + at(i1, j0) * tu) * (1 - tv) +
           (at(i0, j1) * (1 - tu) + at(i1, j1) * tu) * tv;
}

////////////////////////////////////////////////////////////////////////////////

namespace {

float niceStep(float x)
{
    if (!(x > 0)) return 1;
    const float p = std::pow(10.f, std::floor(std::log10(x)));
    const float m = x / p;
    return p * (m < 1.5f ? 1 : m < 3.5f ? 2 : m < 7.5f ? 5 : 10);
}

const QColor kText(0xee, 0xe8, 0xd5);
const QColor kDim(0x93, 0xa1, 0xa1);

/*  Runs `worker(next)` on all the cores but one when there is enough to do: each thread builds what it needs
 *  and then takes chunks of [0, total) with next(first, last) until none are left, so that the cores that
 *  got the cheap chunks take more of them (the pixels through a part cost far more than the ones far from
 *  it).  The worker returns false when it gave up (a newer plane was asked for), and then so does this.  */
struct Chunks
{
    int total = 0;
    int chunk = 2048;                            // (a whole number of evaluator batches)
    std::atomic<int> cursor{0};
    std::atomic<bool> ok{true};

    bool next(int& first, int& last)
    {
        if (!ok) return false;
        const int f = cursor.fetch_add(chunk);
        if (f >= total) return false;
        first = f;
        last = std::min(total, f + chunk);
        return true;
    }
};

template <class Worker>
bool inChunks(int total, Worker worker)
{
    Chunks chunks;
    chunks.total = total;
    const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
    const unsigned T = total < 6000 ? 1u : std::max(1u, std::min(hw > 2 ? hw - 1 : hw, 16u));
    std::vector<std::thread> threads;
    for (unsigned t = 1; t < T; ++t)
    {
        threads.emplace_back([&chunks, &worker] { if (!worker(chunks)) chunks.ok = false; });
    }
    if (!worker(chunks)) chunks.ok = false;
    for (auto& th : threads) th.join();
    return chunks.ok;
}

}   // anonymous namespace

// Diverging colour map: dark blue (deep inside) -> white (surface) ->
// dark orange (far outside).  The same stops are in gl/slice.frag.
QColor fieldColour(float t)
{
    t = std::max(-1.f, std::min(1.f, t));
    struct C { float r, g, b; };
    const C inside_far {0.06f, 0.20f, 0.48f};
    const C inside_near{0.60f, 0.78f, 0.93f};
    const C mid        {0.97f, 0.96f, 0.93f};
    const C out_near   {0.99f, 0.78f, 0.55f};
    const C out_far    {0.60f, 0.20f, 0.04f};
    auto lerp = [](C a, C b, float s) {
        return C{a.r + (b.r - a.r) * s, a.g + (b.g - a.g) * s, a.b + (b.b - a.b) * s};
    };
    C c;
    const float a = std::fabs(t);
    if (t < 0)
        c = a < 0.25f ? lerp(mid, inside_near, a / 0.25f)
                      : lerp(inside_near, inside_far, (a - 0.25f) / 0.75f);
    else
        c = a < 0.25f ? lerp(mid, out_near, a / 0.25f)
                      : lerp(out_near, out_far, (a - 0.25f) / 0.75f);
    return QColor::fromRgbF(c.r, c.g, c.b);
}

void autoColorRange(FieldSource& src, QVector3D lo, QVector3D hi)
{
    // The field on a coarse grid over the region: its range without the few extreme values (a field that blows up at one
    // point -- 1/x -- would otherwise paint everything else one colour)
    const int n = 16;
    const size_t N = libfive::ArrayEvaluator::N;
    libfive::ArrayEvaluator e(src.color, src.vars);
    std::vector<float> values;
    values.reserve(size_t(n) * n * n);
    int k = 0;
    auto flush = [&] {
        if (k == 0) return;
        const auto vs = e.values(k);
        for (int j = 0; j < k; ++j) if (std::isfinite(vs(j))) values.push_back(vs(j));
        k = 0;
    };
    for (int i = 0; i < n; ++i)
    for (int j = 0; j < n; ++j)
    for (int l = 0; l < n; ++l)
    {
        e.set(Eigen::Vector3f(lo.x() + (i + 0.5f) / n * (hi.x() - lo.x()), lo.y() + (j + 0.5f) / n * (hi.y() - lo.y()),
                              lo.z() + (l + 0.5f) / n * (hi.z() - lo.z())), k);
        if (++k == int(N)) flush();
    }
    flush();
    if (values.empty())
    {
        src.lo = 0;
        src.hi = 1;
        return;
    }
    std::sort(values.begin(), values.end());
    src.lo = values[size_t(0.01 * double(values.size() - 1))];
    src.hi = values[size_t(0.99 * double(values.size() - 1))];
    if (!(src.hi > src.lo)) src.hi = src.lo + 1.0f;
}

FieldSlice sampleField(const QVector<FieldSource>& sources,
                       const SectionSettings& s,
                       QVector3D bmin, QVector3D bmax, int longSide, float fade,
                       const std::atomic<int>* generation, int myGeneration)
{
    FieldSlice out;
    out.axis = s.axis;
    out.offset = s.offset;
    out.generation = myGeneration;
    out.fade = fade;
    const int ua = FieldSlice::uAxis(s.axis), va = FieldSlice::vAxis(s.axis);
    out.min = bmin;
    out.max = bmax;
    out.min[s.axis] = out.max[s.axis] = s.offset;

    const float du = bmax[ua] - bmin[ua], dv = bmax[va] - bmin[va];
    if (!(du > 0) || !(dv > 0) || sources.isEmpty())
    {
        return out;
    }
    if (du >= dv)
    {
        out.w = longSide;
        out.h = std::max(8, int(std::round(longSide * dv / du)));
    }
    else
    {
        out.h = longSide;
        out.w = std::max(8, int(std::round(longSide * du / dv)));
    }
    out.values.fill(std::numeric_limits<float>::infinity(), out.w * out.h);
    for (const auto& src : sources)
    {
        if (src.color.is_valid() && !out.hasColor)
        {
            out.hasColor = true;
            out.colorLo = src.lo;
            out.colorHi = src.hi;
            out.colorMap = src.map;
            out.colorLabel = src.label;
        }
    }
    if (out.hasColor) out.color.fill(std::numeric_limits<float>::quiet_NaN(), out.w * out.h);

    // Every pixel is the shape's own field evaluated at that point (on all the cores: the same numbers)
    const size_t N = libfive::ArrayEvaluator::N;
    const bool timing = std::getenv("FIELDES_TIMING") != nullptr;
    const auto t_start = std::chrono::steady_clock::now();
    std::atomic<int> built_ms{0};
    for (const auto& src : sources)
    {
        const int total = out.w * out.h;
        float* values = out.values.data();
        float* colors = out.hasColor ? out.color.data() : nullptr;
        const bool done = inChunks(total, [&](Chunks& chunks) {
            const auto t_build = std::chrono::steady_clock::now();
            libfive::ArrayEvaluator e(src.tree, src.vars);
            std::unique_ptr<libfive::ArrayEvaluator> ce;
            if (src.color.is_valid()) ce.reset(new libfive::ArrayEvaluator(src.color, src.vars));
            if (timing)
            {
                const int ms = int(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t_build).count());
                int seen = built_ms.load();
                while (ms > seen && !built_ms.compare_exchange_weak(seen, ms)) {}
            }
            auto pointOf = [&](int r) {
                Eigen::Vector3f p;
                p[s.axis] = s.offset;
                p[ua] = bmin[ua] + (r % out.w + 0.5f) / out.w * du;
                p[va] = bmin[va] + (r / out.w + 0.5f) / out.h * dv;
                return p;
            };
            int first = 0, last = 0;
            while (chunks.next(first, last))
            for (int start = first; start < last; start += int(N))
            {
                if (generation && generation->load() != myGeneration) return false;
                const int count = std::min(int(N), last - start);
                for (int k = 0; k < count; ++k) e.set(pointOf(start + k), k);
                const auto vs = e.values(count);
                // (this model's colour field where it is the nearest model)
                Eigen::Array<float, 1, Eigen::Dynamic> cs;
                std::vector<int> inside;
                if (ce)
                {
                    // (the colour is only looked at where the plane is inside the model -- the disc of the field viewer, the part
                    // of a result: outside it nothing is drawn but the outline, so nothing is evaluated there)
                    for (int k = 0; k < count; ++k)
                        if (vs(k) < 0 && vs(k) < values[start + k]) inside.push_back(k);
                    for (size_t m = 0; m < inside.size(); ++m) ce->set(pointOf(start + inside[m]), int(m));
                    if (!inside.empty()) cs = ce->values(int(inside.size()));
                }
                for (int k = 0; k < count; ++k)
                {
                    const int idx = start + k;
                    if (vs(k) < values[idx] && colors) colors[idx] = std::numeric_limits<float>::quiet_NaN();
                    values[idx] = std::min(values[idx], vs(k));
                }
                if (colors)
                    for (size_t m = 0; m < inside.size(); ++m) colors[start + inside[m]] = cs(int(m));
            }
            return true;
        });
        if (!done)
        {
            out.w = out.h = 0;       // superseded: abandon
            out.values.clear();
            return out;
        }
    }

    const auto t_pass1 = std::chrono::steady_clock::now();
    // Displacements on a vertex grid (every other sample), for drawing the
    // plane deformed with the model
    const FieldSource* moving = nullptr;
    for (const auto& src : sources)
    {
        if (src.disp[0].is_valid() && src.disp[1].is_valid() && src.disp[2].is_valid())
        {
            moving = &src;
            break;
        }
    }
    if (moving)
    {
        out.gw = std::max(2, out.w / 2 + 1);
        out.gh = std::max(2, out.h / 2 + 1);
        const int total = out.gw * out.gh;
        out.disp.fill(QVector3D(), total);
        QVector<char> known(total, 0);
        char* knownAt = known.data();
        QVector3D* dispAt = out.disp.data();
        const bool done = inChunks(total, [&](Chunks& chunks) {
            std::unique_ptr<libfive::ArrayEvaluator> de[3];
            for (int a = 0; a < 3; ++a) de[a].reset(new libfive::ArrayEvaluator(moving->disp[a]));
            int first = 0, last = 0;
            while (chunks.next(first, last))
            for (int start = first; start < last; start += int(N))
            {
                if (generation && generation->load() != myGeneration) return false;
                const int count = std::min(int(N), last - start);
                for (int a = 0; a < 3; ++a)
                {
                    for (int k = 0; k < count; ++k)
                    {
                        const int idx = start + k;
                        Eigen::Vector3f p;
                        p[s.axis] = s.offset;
                        p[ua] = bmin[ua] + float(idx % out.gw) / (out.gw - 1) * du;
                        p[va] = bmin[va] + float(idx / out.gw) / (out.gh - 1) * dv;
                        de[a]->set(p, k);
                    }
                    const auto ds = de[a]->values(count);
                    for (int k = 0; k < count; ++k) dispAt[start + k][a] = ds(k);
                }
                for (int k = 0; k < count; ++k)
                {
                    const int idx = start + k;
                    const float u = bmin[ua] + float(idx % out.gw) / (out.gw - 1) * du;
                    const float v = bmin[va] + float(idx / out.gw) / (out.gh - 1) * dv;
                    const float d = out.sample(u, v);
                    const QVector3D& q = dispAt[idx];
                    knownAt[idx] = (d < 0.5f * std::max(du / out.w, dv / out.h)) &&
                                   std::isfinite(q.x()) && std::isfinite(q.y()) && std::isfinite(q.z());
                }
            }
            return true;
        });
        if (!done)
        {
            out.w = out.h = 0;
            out.values.clear();
            return out;
        }
        // Outside the model, carry the nearest inside displacement outwards
        // (so cells across the outline don't stretch)
        QVector<int> frontier;
        for (int idx = 0; idx < total; ++idx) if (known[idx]) frontier.push_back(idx);
        if (frontier.isEmpty())
        {
            out.disp.clear();
            out.gw = out.gh = 0;
        }
        while (!frontier.isEmpty())
        {
            QVector<int> next;
            for (int idx : frontier)
            {
                const int i = idx % out.gw, j = idx / out.gw;
                const int nb[4][2] = {{i - 1, j}, {i + 1, j}, {i, j - 1}, {i, j + 1}};
                for (const auto& n : nb)
                {
                    if (n[0] < 0 || n[1] < 0 || n[0] >= out.gw || n[1] >= out.gh) continue;
                    const int m = n[1] * out.gw + n[0];
                    if (known[m]) continue;
                    known[m] = 1;
                    out.disp[m] = out.disp[idx];
                    next.push_back(m);
                }
            }
            frontier.swap(next);
        }
    }

    if (timing)
    {
        const auto t_end = std::chrono::steady_clock::now();
        std::fprintf(stderr, "[slice-detail] the plane's pixels %.0f ms (slowest evaluator set-up %d ms), displacements %.0f ms\n",
                     std::chrono::duration<double, std::milli>(t_pass1 - t_start).count(), built_ms.load(),
                     std::chrono::duration<double, std::milli>(t_end - t_pass1).count());
    }
    out.lo = std::numeric_limits<float>::infinity();
    out.hi = -std::numeric_limits<float>::infinity();
    for (float v : out.values)
    {
        if (std::isfinite(v))
        {
            out.lo = std::min(out.lo, v);
            out.hi = std::max(out.hi, v);
        }
    }
    colorizeField(out, s.range, s.spacing);
    return out;
}

void colorizeField(FieldSlice& slice, float range, float spacing)
{
    if (!slice.valid()) return;

    // Outside, the colours run up to the fade distance; inside, down to the
    // deepest point on the plane.  A manual range sets both.
    if (range > 0)
    {
        slice.rangeIn = slice.rangeOut = range;
    }
    else
    {
        slice.rangeOut = std::max(slice.fade, 1e-6f);
        slice.rangeIn = std::max(std::isfinite(slice.lo) ? -slice.lo : 0.f, 0.05f * slice.rangeOut);
    }
    slice.spacing = spacing > 0 ? spacing
                                : niceStep(std::max(slice.rangeIn, slice.rangeOut) / 6);

    // (the image of the 2D view is made when that view is shown -- buildSliceImage: nobody looks at it most of the time, and
    // making it cost more than sampling the field)
    slice.image = QImage();
}

void buildSliceImage(FieldSlice& slice)
{
    if (!slice.valid()) return;
    // The image for the 2D view (the viewport colours in its shader)
    const int w = slice.w, h = slice.h;
    const float pixel = std::max((slice.max - slice.min).length() / std::max(w, h), 1e-9f);
    QImage img(w, h, QImage::Format_RGBA8888);
    for (int j = 0; j < h; ++j)
    {
        for (int i = 0; i < w; ++i)
        {
            const float v = slice.values[j * w + i];
            if (slice.hasColor)
            {
                // An analysis result: its field inside the model, the
                // outline dark, nothing outside
                const float c = slice.color.value(j * w + i, std::numeric_limits<float>::quiet_NaN());
                if (!(v < 0) || !std::isfinite(c))
                {
                    img.setPixel(i, j, std::fabs(v) / pixel < 1.2f ? qRgba(20, 20, 20, 255)
                                                                   : qRgba(0, 0, 0, 0));
                    continue;
                }
                const float span = slice.colorHi - slice.colorLo;
                QColor col = colormapColor(slice.colorMap, span > 0 ? (c - slice.colorLo) / span : 0.5f);
                if (std::fabs(v) / pixel < 1.2f) col = QColor(20, 20, 20);
                img.setPixel(i, j, col.rgba());
                continue;
            }
            if (!std::isfinite(v) || v > slice.fade)
            {
                img.setPixel(i, j, qRgba(0, 0, 0, 0));
                continue;
            }
            QColor c = fieldColour(v < 0 ? v / slice.rangeIn : v / slice.rangeOut);
            // Distance to the nearest iso-line / to the surface, in pixels
            const float iso = std::fabs(v - slice.spacing * std::round(v / slice.spacing)) / pixel;
            if (std::fabs(v) / pixel < 1.2f)
            {
                c = QColor(20, 20, 20);
            }
            else if (iso < 0.8f)
            {
                c = c.darker(135);
            }
            const float t = (slice.fade - v) / (0.45f * slice.fade);
            c.setAlphaF(std::max(0.f, std::min(1.f, t)));
            img.setPixel(i, j, c.rgba());
        }
    }
    slice.image = img;
}

////////////////////////////////////////////////////////////////////////////////

FieldView::FieldView(QWidget* parent) : QWidget(parent)
{
    setMouseTracking(true);
    setMinimumHeight(170);
}

void FieldView::setSlice(const FieldSlice& s)
{
    m_slice = s;
    if (m_slice.valid() && m_slice.image.isNull()) buildSliceImage(m_slice);       // (made when it is looked at)
    update();
}

QRect FieldView::imageRect() const
{
    QRect area = rect().adjusted(2, 2, -2, -18);
    if (!m_slice.valid() || area.width() < 10 || area.height() < 10) return area;
    const float aspect = float(m_slice.w) / m_slice.h;
    int w = area.width(), h = int(w / aspect);
    if (h > area.height())
    {
        h = area.height();
        w = int(h * aspect);
    }
    return QRect(area.left() + (area.width() - w) / 2, area.top() + (area.height() - h) / 2, w, h);
}

void FieldView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    QFont f = font();
    f.setPointSize(8);
    p.setFont(f);
    if (!m_slice.valid())
    {
        p.setPen(kDim);
        p.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap, T("No field yet"));
        return;
    }
    const QRect r = imageRect();
    p.fillRect(r, QColor(0, 0, 0, 60));
    // Row 0 of the image is the low end of v: flip so +v points up
    p.drawImage(r, m_slice.image.mirrored(false, true));

    static const char* names = "XYZ";
    const int ua = FieldSlice::uAxis(m_slice.axis), va = FieldSlice::vAxis(m_slice.axis);
    p.setPen(kDim);
    p.drawText(QRect(r.left(), r.bottom() + 2, r.width(), 14), Qt::AlignCenter,
               QString("%1 %2 %3 %4").arg(names[ua]).arg(m_slice.min[ua], 0, 'g', 4)
                   .arg(QChar(0x2192)).arg(m_slice.max[ua], 0, 'g', 4) +
               QString("   ") + QChar(0x00b7) + "   " +
               QString("%1 %2 %3 %4").arg(names[va]).arg(m_slice.min[va], 0, 'g', 4)
                   .arg(QChar(0x2192)).arg(m_slice.max[va], 0, 'g', 4));

    if (m_hover && r.contains(m_mouse))
    {
        const float u = m_slice.min[ua] + (m_mouse.x() - r.left() + 0.5f) / r.width() *
                                          (m_slice.max[ua] - m_slice.min[ua]);
        const float v = m_slice.max[va] - (m_mouse.y() - r.top() + 0.5f) / r.height() *
                                          (m_slice.max[va] - m_slice.min[va]);
        const float d = m_slice.sample(u, v);
        if (std::isfinite(d))
        {
            const QString txt = QString("d = %1").arg(d, 0, 'g', 4);
            const QRect box(r.left() + 4, r.top() + 4, p.fontMetrics().horizontalAdvance(txt) + 10, 16);
            p.fillRect(box, QColor(22, 76, 94, 220));
            p.setPen(kText);
            p.drawText(box, Qt::AlignCenter, txt);
        }
        p.setPen(QPen(QColor(220, 50, 47), 1));
        p.drawLine(m_mouse + QPoint(-4, 0), m_mouse + QPoint(4, 0));
        p.drawLine(m_mouse + QPoint(0, -4), m_mouse + QPoint(0, 4));
    }
}

void FieldView::mouseMoveEvent(QMouseEvent* e)
{
    m_mouse = e->pos();
    m_hover = true;
    update();
}

void FieldView::leaveEvent(QEvent*)
{
    m_hover = false;
    update();
}

////////////////////////////////////////////////////////////////////////////////

FieldLegend::FieldLegend(QWidget* parent) : QWidget(parent)
{
    setFixedHeight(30);
}

void FieldLegend::setSlice(const FieldSlice& s)
{
    m_valid = s.valid();
    m_in = s.rangeIn;
    m_out = s.rangeOut;
    m_hasColor = s.valid() && s.hasColor;
    m_map = s.colorMap;
    m_label = s.colorLabel;
    m_lo = s.colorLo;
    m_hi = s.colorHi;
    update();
}

void FieldLegend::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF bar(1, 2, width() - 2, 9);
    QLinearGradient g(bar.topLeft(), bar.topRight());
    for (int k = 0; k <= 20; ++k)
    {
        const float t = -1 + 2 * k / 20.f;
        g.setColorAt(k / 20.0, m_hasColor ? colormapColor(m_map, k / 20.f) : fieldColour(t));
    }
    QPainterPath path;
    path.addRoundedRect(bar, 3, 3);
    p.fillPath(path, g);
    if (!m_hasColor)
    {
        p.setPen(QPen(QColor(20, 20, 20), 1.5));
        p.drawLine(QPointF(bar.center().x(), bar.top() - 1), QPointF(bar.center().x(), bar.bottom() + 1));
    }

    QFont f = font();
    f.setPointSize(8);
    p.setFont(f);
    p.setPen(kDim);
    const QRectF labels(0, bar.bottom() + 2, width(), 14);
    if (m_hasColor)
    {
        p.drawText(labels, Qt::AlignLeft | Qt::AlignVCenter, QString::number(m_lo, 'g', 4));
        p.drawText(labels, Qt::AlignRight | Qt::AlignVCenter, QString::number(m_hi, 'g', 4));
        p.drawText(labels, Qt::AlignHCenter | Qt::AlignVCenter, m_label.isEmpty() ? QString() : m_label);
        return;
    }
    if (m_valid)
    {
        p.drawText(labels, Qt::AlignLeft | Qt::AlignVCenter,
                   T("%1%2 inside").arg(QChar(0x2212)).arg(m_in, 0, 'g', 3));
        p.drawText(labels, Qt::AlignRight | Qt::AlignVCenter,
                   T("+%1 outside").arg(m_out, 0, 'g', 3));
    }
    p.drawText(labels, Qt::AlignHCenter | Qt::AlignVCenter, "surface");
}

////////////////////////////////////////////////////////////////////////////////

void SectionPanel::setWholeElementsAvailable(bool available)
{
    if (!m_wholeBox || m_wholeBox->isVisibleTo(this) == available) return;
    m_wholeBox->setVisible(available);
    adjustSize();
    place();
}

SectionPanel::SectionPanel(QWidget* parent)
    : QFrame(parent),
      m_header(new QToolButton), m_enable(new QToolButton), m_close(new QToolButton),
      m_body(new QWidget),
      m_flip(new QToolButton),
      m_offsetSlider(new QSlider(Qt::Horizontal)),
      m_offsetSpin(new QDoubleSpinBox),
      m_clip(new QToolButton), m_field(new QToolButton),
      m_opacity(new QSlider(Qt::Horizontal)),
      m_autoRange(new QToolButton), m_range(new QDoubleSpinBox),
      m_legend(new FieldLegend), m_info(new QLabel),
      m_show2d(new QToolButton), m_view(new FieldView)
{
    setObjectName("SectionPanel");
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(
        "#SectionPanel { background: rgba(22, 76, 94, 222);"
        "  border: 1px solid rgba(147, 161, 161, 90); border-radius: 6px; }"
        "QLabel { color: #93a1a1; font-size: 8pt; }"
        "QLabel#SectionInfo { color: #eee8d5; }"
        "QToolButton { color: #eee8d5; background: rgba(147, 161, 161, 28);"
        "  border: 1px solid rgba(147, 161, 161, 60); border-radius: 4px;"
        "  padding: 2px 8px; font-size: 8pt; }"
        "QToolButton:hover { background: rgba(147, 161, 161, 60); }"
        "QToolButton:checked { background: rgba(38, 139, 210, 170);"
        "  border-color: rgba(38, 139, 210, 230); color: white; }"
        "QToolButton:disabled { color: rgba(147, 161, 161, 120); }"
        "QToolButton#SectionHeader { background: transparent; border: none; font-weight: bold;"
        "  font-size: 9pt; padding: 3px 4px; }"
        "QToolButton#SectionClose { background: transparent; border: none; color: #93a1a1;"
        "  padding: 2px 5px; font-size: 9pt; }"
        "QToolButton#SectionClose:hover { color: #eee8d5; }"
        "QSlider::groove:horizontal { height: 4px; background: rgba(147, 161, 161, 70);"
        "  border-radius: 2px; }"
        "QSlider::sub-page:horizontal { background: rgba(38, 139, 210, 200); border-radius: 2px; }"
        "QSlider::handle:horizontal { width: 12px; margin: -5px 0; border-radius: 6px;"
        "  background: #eee8d5; }"
        "QDoubleSpinBox { color: #eee8d5; background: rgba(0, 0, 0, 70);"
        "  border: 1px solid rgba(147, 161, 161, 70); border-radius: 3px; padding: 1px 2px;"
        "  font-size: 8pt; selection-background-color: rgba(38, 139, 210, 200); }"
        "QDoubleSpinBox:disabled { color: rgba(147, 161, 161, 140); }");

    // Header: collapse, on / off, close
    m_header->setObjectName("SectionHeader");
    m_header->setText(QString(QChar(0x25be)) + "  " + T("Section"));
    m_header->setCursor(Qt::PointingHandCursor);
    m_header->setToolTip(T("Collapse / expand"));
    m_enable->setObjectName("sectionEnable");
    m_enable->setCheckable(true);
    m_enable->setText(T("Off"));
    m_enable->setToolTip(T("Section on / off"));
    m_close->setObjectName("SectionClose");
    m_close->setText(QString(QChar(0x2715)));
    m_close->setToolTip(T("Close"));
    auto head = new QHBoxLayout;
    head->setContentsMargins(0, 0, 0, 0);
    head->setSpacing(4);
    head->addWidget(m_header);
    head->addStretch();
    head->addWidget(m_enable);
    head->addWidget(m_close);

    auto label = [](const QString& t) {
        auto l = new QLabel(t);
        l->setFixedWidth(52);
        return l;
    };

    // Plane normal (segmented X / Y / Z) and flip
    auto axisRow = new QHBoxLayout;
    axisRow->setSpacing(2);
    axisRow->addWidget(label(T("Plane")));
    auto group = new QButtonGroup(this);
    group->setExclusive(true);
    static const char* names[3] = {"X", "Y", "Z"};
    for (int i=0; i < 3; ++i)
    {
        m_axes[i] = new QToolButton;
        m_axes[i]->setText(names[i]);
        m_axes[i]->setCheckable(true);
        m_axes[i]->setFixedWidth(30);
        m_axes[i]->setObjectName(QString("sectionAxis") + names[i]);
        m_axes[i]->setToolTip(T("Plane normal along %1").arg(names[i]));
        group->addButton(m_axes[i], i);
        axisRow->addWidget(m_axes[i]);
    }
    m_axes[2]->setChecked(true);
    axisRow->addSpacing(8);
    m_flip->setObjectName("sectionFlip");
    m_flip->setCheckable(true);
    m_flip->setText(QString(QChar(0x21c5)) + " " + T("Flip"));
    m_flip->setToolTip(T("Keep the other side"));
    axisRow->addWidget(m_flip);
    axisRow->addStretch();

    // Position
    m_offsetSlider->setObjectName("sectionOffset");
    m_offsetSlider->setRange(0, 1000);
    m_offsetSlider->setValue(500);
    m_offsetSpin->setDecimals(3);
    m_offsetSpin->setRange(-1e6, 1e6);
    m_offsetSpin->setKeyboardTracking(false);
    m_offsetSpin->setFixedWidth(74);
    m_offsetSpin->setButtonSymbols(QDoubleSpinBox::NoButtons);
    m_offsetSpin->setToolTip(T("Plane position"));
    auto offsetRow = new QHBoxLayout;
    offsetRow->addWidget(label(T("Position")));
    offsetRow->addWidget(m_offsetSlider, 1);
    offsetRow->addWidget(m_offsetSpin);

    // What to show
    m_clip->setObjectName("sectionClip");
    m_clip->setCheckable(true);
    m_clip->setChecked(true);
    m_clip->setText(T("Cut model"));
    m_clip->setToolTip(T("Cut the model at the plane"));
    m_field->setObjectName("sectionField");
    m_field->setCheckable(true);
    m_field->setChecked(true);
    m_field->setText(T("Field"));
    m_field->setToolTip(T("Colour the plane: distance, or the analysis result"));
    m_whole = new QToolButton;
    m_whole->setObjectName("sectionWhole");
    m_whole->setCheckable(true);
    m_whole->setText(T("Whole elements"));
    m_whole->setToolTip(T("Keep elements whole at the plane"));
    auto showRow = new QHBoxLayout;
    showRow->addWidget(label(T("Show")));
    showRow->addWidget(m_clip);
    showRow->addWidget(m_field);
    showRow->addStretch();
    m_wholeBox = new QWidget;
    auto wholeRow = new QHBoxLayout(m_wholeBox);
    wholeRow->setContentsMargins(0, 0, 0, 0);
    wholeRow->addWidget(label(""));
    wholeRow->addWidget(m_whole);
    wholeRow->addStretch();
    m_wholeBox->hide();

    m_opacity->setRange(10, 100);
    m_opacity->setValue(90);
    auto opacityRow = new QHBoxLayout;
    opacityRow->addWidget(label(T("Opacity")));
    opacityRow->addWidget(m_opacity, 1);

    // Colour scale
    m_autoRange->setCheckable(true);
    m_autoRange->setChecked(true);
    m_autoRange->setText(T("Auto"));
    m_autoRange->setToolTip(T("Colour scale from the model"));
    m_range->setDecimals(4);
    m_range->setRange(1e-6, 1e6);
    m_range->setValue(1);
    m_range->setEnabled(false);
    m_range->setKeyboardTracking(false);
    m_range->setButtonSymbols(QDoubleSpinBox::NoButtons);
    m_range->setToolTip(T("Manual colour range (±)"));
    auto rangeRow = new QHBoxLayout;
    rangeRow->addWidget(label(T("Colours")));
    rangeRow->addWidget(m_autoRange);
    rangeRow->addWidget(m_range, 1);

    m_info->setObjectName("SectionInfo");
    m_info->setWordWrap(true);
    m_info->setMinimumHeight(16);

    m_show2d->setCheckable(true);
    m_show2d->setText(T("2D view"));
    m_show2d->setToolTip(T("Flat 2D plot of the plane"));
    m_view->hide();
    auto viewRow = new QHBoxLayout;
    viewRow->addWidget(m_info, 1);
    viewRow->addWidget(m_show2d, 0, Qt::AlignTop);

    auto body = new QVBoxLayout(m_body);
    body->setContentsMargins(6, 2, 6, 4);
    body->setSpacing(6);
    body->addLayout(axisRow);
    body->addLayout(offsetRow);
    body->addLayout(showRow);
    body->addWidget(m_wholeBox);
    body->addLayout(opacityRow);
    body->addLayout(rangeRow);
    body->addWidget(m_legend);
    body->addLayout(viewRow);
    body->addWidget(m_view);

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(7, 4, 7, 7);            // (the margin is where a card is resized from)
    layout->setSpacing(2);
    layout->addLayout(head);
    layout->addWidget(m_body);
    setMinimumWidth(260);
    resize(300, 100);

    new CardController(this, "section", m_header, QSize(260, 60));
    connect(m_header, &QToolButton::clicked, this, [this]{
        m_collapsed = !m_collapsed;
        m_body->setVisible(!m_collapsed);
        m_header->setText(QString(QChar(m_collapsed ? 0x25b8 : 0x25be)) + "  " + T("Section"));
        CardController::collapse(this, m_collapsed, m_header->sizeHint().height() + 14);
        place();
    });
    connect(m_close, &QToolButton::clicked, this, &SectionPanel::closeRequested);
    connect(m_enable, &QToolButton::toggled, this, [this](bool b){
        m_enable->setText(b ? T("On") : T("Off"));
        m_settings.enabled = b;
        emitChange();
    });
    connect(group, QOverload<int>::of(&QButtonGroup::idClicked), this, [this](int id){
        m_settings.axis = id;
        // Put the plane through the middle of the model along the new axis
        if (m_boundsKnown) m_settings.offset = preferredOffset(id);
        faceCamera();
        syncOffsetWidgets();
        emitChange();
    });
    connect(m_flip, &QToolButton::toggled, this, [this](bool b){ m_settings.flip = b; emitChange(); });
    connect(m_clip, &QToolButton::toggled, this, [this](bool b){ m_settings.clip = b; emitChange(); });
    connect(m_whole, &QToolButton::toggled, this, [this](bool b){ m_settings.wholeElements = b; emitChange(); });
    connect(m_field, &QToolButton::toggled, this, [this](bool b){ m_settings.field = b; emitChange(); });
    connect(m_opacity, &QSlider::valueChanged, this, [this](int v){
        m_settings.opacity = v / 100.f;
        emitChange();
    });
    connect(m_offsetSlider, &QSlider::valueChanged, this, [this](int v){
        if (m_updating || !m_boundsKnown) return;
        const int a = m_settings.axis;
        m_settings.offset = m_min[a] + (m_max[a] - m_min[a]) * v / 1000.f;
        m_updating = true;
        m_offsetSpin->setValue(m_settings.offset);
        m_updating = false;
        emitChange();
    });
    connect(m_offsetSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v){
        if (m_updating) return;
        m_settings.offset = v;
        syncOffsetWidgets();
        emitChange();
    });
    connect(m_autoRange, &QToolButton::toggled, this, [this](bool b){
        m_range->setEnabled(!b);
        if (!b && m_slice.valid()) m_range->setValue(std::max(m_slice.rangeIn, m_slice.rangeOut));
        m_settings.range = b ? 0 : float(m_range->value());
        emitChange();
    });
    connect(m_range, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v){
        if (!m_autoRange->isChecked())
        {
            m_settings.range = v;
            emitChange();
        }
    });
    connect(m_show2d, &QToolButton::toggled, this, [this](bool b){
        m_view->setVisible(b);
        if (b) m_view->setSlice(m_slice);
        place();
    });

    if (parent)
    {
        parent->installEventFilter(this);
    }
    place();
}

bool SectionPanel::eventFilter(QObject* obj, QEvent* e)
{
    if (obj == parentWidget() && e->type() == QEvent::Resize)
    {
        place();
    }
    return QFrame::eventFilter(obj, e);
}

void SectionPanel::place()
{
    // (a card the user sized keeps its size, and one the user moved keeps its place: see CardController)
    if (!CardController::sized(this)) resize(300, std::max(minimumSizeHint().height(), sizeHint().height()));
    if (parentWidget() && !CardController::moved(this))
    {
        // Top-right, below the orientation triad
        move(parentWidget()->width() - width() - 8, 124);
    }
}

void SectionPanel::faceCamera()
{
    if (!m_cameraDir) return;
    // The default keeps the half below the plane, whose cut face looks
    // towards +axis: flip when the camera is on the negative side
    const bool flip = m_cameraDir()[m_settings.axis] < 0;
    m_settings.flip = flip;
    QSignalBlocker block(m_flip);
    m_flip->setChecked(flip);
}

void SectionPanel::setEnabledSection(bool on)
{
    if (on && !m_enable->isChecked())
    {
        faceCamera();
    }
    if (on && m_boundsKnown && m_modelBounds)
    {
        // Re-centre the plane if it no longer passes through the model
        QVector3D lo, hi;
        const int a = m_settings.axis;
        if (m_modelBounds(lo, hi) &&
            (m_settings.offset < lo[a] || m_settings.offset > hi[a]))
        {
            m_settings.offset = preferredOffset(a);
            syncOffsetWidgets();
        }
    }
    m_enable->setChecked(on);
}

void SectionPanel::setOffset(float offset)
{
    m_settings.offset = offset;
    syncOffsetWidgets();
    emitChange();
}

void SectionPanel::setReadout(const QString& text)
{
    if (text != m_readout)
    {
        m_readout = text;
        updateInfo();
    }
}

void SectionPanel::updateInfo()
{
    if (!m_readout.isEmpty())
    {
        m_info->setText(m_readout);
    }
    else if (m_slice.valid() && m_settings.field && m_slice.hasColor)
    {
        m_info->setText(T("Inside: %1")
                            .arg(m_slice.colorLabel.isEmpty() ? T("result") : m_slice.colorLabel));
    }
    else if (m_slice.valid() && m_settings.field)
    {
        m_info->setText(T("%1 to %2, lines every %3")
                            .arg(m_slice.lo, 0, 'g', 3).arg(m_slice.hi, 0, 'g', 3)
                            .arg(m_slice.spacing, 0, 'g', 3));
    }
    else
    {
        m_info->setText(m_settings.enabled ? T("Drag the arrow to move the plane") : T("Section off"));
    }
}

float SectionPanel::preferredOffset(int a) const
{
    float lo_a = m_min[a], hi_a = m_max[a];
    QVector3D lo, hi;
    if (m_modelBounds && m_modelBounds(lo, hi))
    {
        const float c = 0.5f * (lo[a] + hi[a]);
        if (!m_boundsKnown || (c >= lo_a && c <= hi_a))
        {
            return c;
        }
    }
    return 0.5f * (lo_a + hi_a);
}

void SectionPanel::emitChange()
{
    updateInfo();
    emit(settingsChanged(m_settings));
}

void SectionPanel::syncOffsetWidgets()
{
    m_updating = true;
    const int a = m_settings.axis;
    if (m_boundsKnown && m_max[a] > m_min[a])
    {
        m_offsetSlider->setValue(int(std::round(1000 * (m_settings.offset - m_min[a]) /
                                                (m_max[a] - m_min[a]))));
        m_offsetSpin->setSingleStep(std::max(1e-4, (m_max[a] - m_min[a]) / 100.0));
    }
    m_offsetSpin->setValue(m_settings.offset);
    m_updating = false;
}

void SectionPanel::setBounds(QVector3D min, QVector3D max)
{
    const bool first = !m_boundsKnown;
    const int a = m_settings.axis;
    const bool outside = m_settings.offset < min[a] || m_settings.offset > max[a];
    m_min = min;
    m_max = max;
    m_boundsKnown = true;
    if (first || outside)
    {
        m_settings.offset = preferredOffset(a);
        syncOffsetWidgets();
        emitChange();
    }
    else
    {
        syncOffsetWidgets();
    }
}

void SectionPanel::setSlice(FieldSlice s)
{
    m_slice = s;
    m_legend->setSlice(s);
    if (m_view->isVisible())
    {
        m_view->setSlice(s);
    }
    updateInfo();
}

////////////////////////////////////////////////////////////////////////////////
// The field viewer

FieldPanel::FieldPanel(QWidget* parent)
    : QFrame(parent),
      m_header(new QToolButton), m_combo(new QComboBox), m_comboRow(new QWidget), m_body(new QWidget),
      m_radiusSlider(new QSlider(Qt::Horizontal)), m_radiusSpin(new QDoubleSpinBox),
      m_opacity(new QSlider(Qt::Horizontal)),
      m_legend(new FieldLegend), m_info(new QLabel),
      m_show2d(new QToolButton), m_view(new FieldView)
{
    setObjectName("FieldPanel");
    setAttribute(Qt::WA_StyledBackground, true);
    // (the same look as the section card)
    setStyleSheet(
        "#FieldPanel { background: rgba(22, 76, 94, 222);"
        "  border: 1px solid rgba(147, 161, 161, 90); border-radius: 6px; }"
        "QLabel { color: #93a1a1; font-size: 8pt; }"
        "QLabel#FieldInfo { color: #eee8d5; }"
        "QToolButton { color: #eee8d5; background: rgba(147, 161, 161, 28);"
        "  border: 1px solid rgba(147, 161, 161, 60); border-radius: 4px;"
        "  padding: 2px 8px; font-size: 8pt; }"
        "QToolButton:hover { background: rgba(147, 161, 161, 60); }"
        "QToolButton:checked { background: rgba(38, 139, 210, 170);"
        "  border-color: rgba(38, 139, 210, 230); color: white; }"
        "QToolButton#FieldHeader { background: transparent; border: none; font-weight: bold;"
        "  font-size: 9pt; padding: 3px 4px; }"
        "QComboBox { color: #eee8d5; background: rgba(0, 0, 0, 70);"
        "  border: 1px solid rgba(147, 161, 161, 70); border-radius: 3px; padding: 1px 6px; font-size: 8pt; }"
        "QComboBox QAbstractItemView { color: #eee8d5; background: #25607a;"
        "  selection-background-color: rgba(38, 139, 210, 200); }"
        "QSlider::groove:horizontal { height: 4px; background: rgba(147, 161, 161, 70);"
        "  border-radius: 2px; }"
        "QSlider::sub-page:horizontal { background: rgba(38, 139, 210, 200); border-radius: 2px; }"
        "QSlider::handle:horizontal { width: 12px; margin: -5px 0; border-radius: 6px;"
        "  background: #eee8d5; }"
        "QDoubleSpinBox { color: #eee8d5; background: rgba(0, 0, 0, 70);"
        "  border: 1px solid rgba(147, 161, 161, 70); border-radius: 3px; padding: 1px 2px;"
        "  font-size: 8pt; selection-background-color: rgba(38, 139, 210, 200); }");

    // Header: collapse only (the card is there while a field is selected: it has no close button)
    m_header->setObjectName("FieldHeader");
    m_header->setText(QString(QChar(0x25be)) + "  " + T("Field viewer"));
    m_header->setCursor(Qt::PointingHandCursor);
    m_header->setToolTip(T("Collapse / expand"));
    auto head = new QHBoxLayout;
    head->setContentsMargins(0, 0, 0, 0);
    head->addWidget(m_header);
    head->addStretch();

    auto label = [](const QString& t) {
        auto l = new QLabel(t);
        l->setFixedWidth(52);
        return l;
    };

    // Which of the selected fields (only when there are several)
    {
        auto row = new QHBoxLayout(m_comboRow);
        row->setContentsMargins(0, 0, 0, 0);
        row->addWidget(label(T("Field")));
        m_combo->setObjectName("fieldChoice");
        m_combo->setToolTip(T("The selected fields: choose the one that is shown"));
        row->addWidget(m_combo, 1);
        m_comboRow->hide();
    }

    // The way the disc faces
    auto axisRow = new QHBoxLayout;
    axisRow->setSpacing(2);
    axisRow->addWidget(label(T("Plane")));
    auto group = new QButtonGroup(this);
    group->setExclusive(true);
    static const char* names[3] = {"X", "Y", "Z"};
    for (int i = 0; i < 3; ++i)
    {
        m_axes[i] = new QToolButton;
        m_axes[i]->setText(names[i]);
        m_axes[i]->setCheckable(true);
        m_axes[i]->setFixedWidth(30);
        m_axes[i]->setObjectName(QString("fieldAxis") + names[i]);
        m_axes[i]->setToolTip(T("The disc faces along %1").arg(names[i]));
        group->addButton(m_axes[i], i);
        axisRow->addWidget(m_axes[i]);
    }
    m_axes[2]->setChecked(true);
    axisRow->addStretch();

    // (no position slider: the disc is not kept inside anything -- the arrows in the viewport take it wherever it is to go, and a
    // slider that spans the render region is no way to say where in space that is)
    // Radius
    m_radiusSlider->setObjectName("fieldRadius");
    m_radiusSlider->setRange(0, 1000);
    m_radiusSpin->setDecimals(3);
    m_radiusSpin->setRange(1e-3, 1e6);
    m_radiusSpin->setKeyboardTracking(false);
    m_radiusSpin->setFixedWidth(74);
    m_radiusSpin->setButtonSymbols(QDoubleSpinBox::NoButtons);
    m_radiusSpin->setToolTip(T("Radius of the disc"));
    auto radiusRow = new QHBoxLayout;
    radiusRow->addWidget(label(T("Radius")));
    radiusRow->addWidget(m_radiusSlider, 1);
    radiusRow->addWidget(m_radiusSpin);

    m_opacity->setRange(10, 100);
    m_opacity->setValue(90);
    auto opacityRow = new QHBoxLayout;
    opacityRow->addWidget(label(T("Opacity")));
    opacityRow->addWidget(m_opacity, 1);

    m_info->setObjectName("FieldInfo");
    m_info->setWordWrap(true);
    m_info->setMinimumHeight(16);
    m_show2d->setCheckable(true);
    m_show2d->setText(T("2D view"));
    m_show2d->setToolTip(T("Flat 2D plot of the disc"));
    m_view->hide();
    auto viewRow = new QHBoxLayout;
    viewRow->addWidget(m_info, 1);
    viewRow->addWidget(m_show2d, 0, Qt::AlignTop);

    auto body = new QVBoxLayout(m_body);
    body->setContentsMargins(6, 2, 6, 4);
    body->setSpacing(6);
    body->addWidget(m_comboRow);
    body->addLayout(axisRow);
    body->addLayout(radiusRow);
    body->addLayout(opacityRow);
    body->addWidget(m_legend);
    body->addLayout(viewRow);
    body->addWidget(m_view);

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(7, 4, 7, 7);            // (the margin is where a card is resized from)
    layout->setSpacing(2);
    layout->addLayout(head);
    layout->addWidget(m_body);
    setMinimumWidth(260);
    resize(300, 100);

    new CardController(this, "field-viewer", m_header, QSize(260, 60));
    connect(m_header, &QToolButton::clicked, this, [this]{
        m_collapsed = !m_collapsed;
        m_body->setVisible(!m_collapsed);
        m_header->setText(QString(QChar(m_collapsed ? 0x25b8 : 0x25be)) + "  " + T("Field viewer"));
        CardController::collapse(this, m_collapsed, m_header->sizeHint().height() + 14);
        place();
    });
    connect(m_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
        if (m_updating || i < 0 || i >= m_keys.size()) return;
        m_name = m_combo->itemText(i);
        emit(fieldChosen(m_keys[i]));
    });
    connect(group, QOverload<int>::of(&QButtonGroup::idClicked), this, [this](int id) {
        m_settings.axis = id;
        syncWidgets();
        emitChange();
    });
    connect(m_opacity, &QSlider::valueChanged, this, [this](int v) {
        if (m_updating) return;
        m_settings.opacity = v / 100.f;
        emitChange();
    });
    connect(m_radiusSlider, &QSlider::valueChanged, this, [this](int v) {
        if (m_updating) return;
        m_settings.radius = 0.02f * radiusMax() / 1.5f + (radiusMax() - 0.02f * radiusMax() / 1.5f) * v / 1000.f;
        syncWidgets();
        emitChange();
    });
    connect(m_radiusSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v) {
        if (m_updating) return;
        m_settings.radius = float(v);
        syncWidgets();
        emitChange();
    });
    connect(m_show2d, &QToolButton::toggled, this, [this](bool b) {
        m_view->setVisible(b);
        if (b) m_view->setSlice(m_slice);
        place();
    });

    if (parent) parent->installEventFilter(this);
    place();
}

bool FieldPanel::eventFilter(QObject* obj, QEvent* e)
{
    if (obj == parentWidget() && e->type() == QEvent::Resize) place();
    // (the section card is open beside it: where it is and how tall it is decides where this one goes)
    if (obj == m_sectionCard && (e->type() == QEvent::Show || e->type() == QEvent::Hide || e->type() == QEvent::Resize ||
                                 e->type() == QEvent::Move))
        place();
    return QFrame::eventFilter(obj, e);
}

void FieldPanel::place()
{
    if (!CardController::sized(this)) resize(300, std::max(minimumSizeHint().height(), sizeHint().height()));
    if (parentWidget() && !CardController::moved(this))
    {
        // Top-right, below the triad -- and below the section card when that is open too (the two views work together)
        int y = 124;
        if (m_sectionCard && m_sectionCard->isVisible()) y = m_sectionCard->y() + m_sectionCard->height() + 8;
        move(parentWidget()->width() - width() - 8, y);
    }
}

void FieldPanel::stackBelow(QWidget* sectionCard)
{
    m_sectionCard = sectionCard;
    sectionCard->installEventFilter(this);
    place();
}

float FieldPanel::radiusMax() const
{
    const QVector3D size = m_max - m_min;
    return 1.5f * std::max({size.x(), size.y(), size.z(), 1e-3f});
}

void FieldPanel::syncWidgets()
{
    m_updating = true;
    for (int i = 0; i < 3; ++i) m_axes[i]->setChecked(i == m_settings.axis);
    const float lo = 0.02f * radiusMax() / 1.5f, hi = radiusMax();
    m_radiusSlider->setValue(int(std::max(0.f, std::min(1.f, (m_settings.radius - lo) / std::max(hi - lo, 1e-6f))) * 1000));
    m_radiusSpin->setValue(m_settings.radius);
    m_opacity->setValue(int(m_settings.opacity * 100));
    m_updating = false;
}

void FieldPanel::emitChange()
{
    emit(settingsChanged(m_settings));
}

void FieldPanel::setFields(const QStringList& keys, const QStringList& names, int current)
{
    m_keys = keys;
    m_updating = true;
    m_combo->clear();
    m_combo->addItems(names);
    m_combo->setCurrentIndex(std::max(0, std::min(current, int(names.size()) - 1)));
    m_updating = false;
    m_name = m_combo->currentText();
    m_comboRow->setVisible(keys.size() >= 2);
    place();
    updateInfo();
}

void FieldPanel::setContext(QVector3D centre, bool known, QVector3D regionMin, QVector3D regionMax)
{
    Q_UNUSED(known);
    m_min = regionMin;
    m_max = regionMax;
    m_home = centre;
    m_settings.centre = centre;
    const QVector3D size = m_max - m_min;
    m_settings.radius = 0.5f * std::max({size.x(), size.y(), size.z(), 1e-3f});
    syncWidgets();
    emitChange();
}

void FieldPanel::setCentre(QVector3D centre)
{
    m_settings.centre = centre;
    syncWidgets();
    emitChange();
}

void FieldPanel::setSlice(FieldSlice s)
{
    m_slice = s;
    m_legend->setSlice(s);
    if (m_view->isVisible()) m_view->setSlice(s);
    updateInfo();
}

void FieldPanel::setReadout(const QString& text)
{
    m_readout = text;
    updateInfo();
}

void FieldPanel::updateInfo()
{
    if (!m_readout.isEmpty()) m_info->setText(m_readout);
    else m_info->setText(m_name.isEmpty() ? T("Drag the arrows to move the disc") : T("%1: drag the arrows to move the disc").arg(m_name));
}

}   // namespace FielDes
