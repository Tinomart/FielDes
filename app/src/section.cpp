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
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSignalBlocker>
#include <QSlider>
#include <QStandardPaths>
#include <QTimer>
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

void autoColorRange(FieldSource& src, QVector3D lo, QVector3D hi, const QVector3D* anchor)
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
    // The value where the field is about is in the range: a point with a radius -- `distance_to_point(p)` with the influence of p -- is negative only in a ball of that radius, a few of the samples
    // above, which the percentile cuts off: the range then began above zero, and the disc showed no radius (the user, 2026-10-28: "There is no radius created")
    if (anchor)
    {
        e.set(Eigen::Vector3f(anchor->x(), anchor->y(), anchor->z()), 0);
        const float at = e.values(1)(0);
        if (std::isfinite(at))
        {
            src.lo = std::min(src.lo, at);
            src.hi = std::max(src.hi, at);
            if (!(src.hi > src.lo)) src.hi = src.lo + 1.0f;
        }
    }
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
            out.colorManual = src.manual;
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
    // (a field of the field viewer: the lines are at round steps of ITS values)
    if (slice.hasColor && slice.colorMap == "section" && slice.colorHi > slice.colorLo)
        slice.spacing = niceStep((slice.colorHi - slice.colorLo) / 8);

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
            if (slice.hasColor && slice.colorMap == "section")
            {
                // A field of the field viewer: the section's colours over its range, its dark lines at round steps of its values (the
                // distance to the nearest in pixels, from the field's slope there), a heavier one at zero, the rim of the disc dark
                const float c = slice.color.value(j * w + i, std::numeric_limits<float>::quiet_NaN());
                if (!(v < 0) || !std::isfinite(c))
                {
                    img.setPixel(i, j, std::fabs(v) / pixel < 1.2f ? qRgba(20, 20, 20, 255) : qRgba(0, 0, 0, 0));
                    continue;
                }
                const float span = slice.colorHi - slice.colorLo;
                QColor col = fieldColour(span > 0 ? sectionLevel(c, slice.colorLo, slice.colorHi) : 0.f);
                auto at = [&](int ii, int jj) {
                    const float q = slice.color.value(std::max(0, std::min(h - 1, jj)) * w + std::max(0, std::min(w - 1, ii)), c);
                    return std::isfinite(q) ? q : c;
                };
                const float gx = 0.5f * (at(i + 1, j) - at(i - 1, j)), gy = 0.5f * (at(i, j + 1) - at(i, j - 1));
                const float slope = std::max(std::hypot(gx, gy), 1e-12f);
                if (std::fabs(c - slice.spacing * std::round(c / slice.spacing)) / slope < 0.8f) col = col.darker(135);
                if (slice.colorLo < 0 && slice.colorHi > 0 && std::fabs(c) / slope < 1.2f) col = QColor(20, 20, 20);
                if (std::fabs(v) / pixel < 1.2f) col = QColor(20, 20, 20);
                img.setPixel(i, j, col.rgba());
                continue;
            }
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

// The colour bar of a plane with its numbers, in `bar` and the line of labels under it: what the card's legend draws and what a saved image carries.  `hasColor`: a model coloured by a field (its own map
// and range); else the distance scale, dark blue inside to orange outside with the surface in the middle
static void paintFieldLegend(QPainter& p, const QRectF& bar, const QRectF& labels, bool valid, bool hasColor, const QString& map, const QString& label, float lo, float hi, float in, float out)
{
    QLinearGradient g(bar.topLeft(), bar.topRight());
    for (int k = 0; k <= 20; ++k)
    {
        const float t = -1 + 2 * k / 20.f;
        g.setColorAt(k / 20.0, !hasColor ? fieldColour(t)
                               : map == "section" ? fieldColour(sectionLevel(lo + (hi - lo) * k / 20.f, lo, hi))
                                                  : colormapColor(map, k / 20.f));
    }
    QPainterPath path;
    path.addRoundedRect(bar, 3, 3);
    p.fillPath(path, g);
    if (!hasColor)
    {
        p.setPen(QPen(QColor(20, 20, 20), 1.5));
        p.drawLine(QPointF(bar.center().x(), bar.top() - 1), QPointF(bar.center().x(), bar.bottom() + 1));
    }
    p.setPen(kDim);
    if (hasColor)
    {
        p.drawText(labels, Qt::AlignLeft | Qt::AlignVCenter, QString::number(lo, 'g', 4));
        p.drawText(labels, Qt::AlignRight | Qt::AlignVCenter, QString::number(hi, 'g', 4));
        p.drawText(labels, Qt::AlignHCenter | Qt::AlignVCenter, label.isEmpty() ? QString() : label);
        return;
    }
    if (valid)
    {
        p.drawText(labels, Qt::AlignLeft | Qt::AlignVCenter, T("%1%2 inside").arg(QChar(0x2212)).arg(in, 0, 'g', 3));
        p.drawText(labels, Qt::AlignRight | Qt::AlignVCenter, T("+%1 outside").arg(out, 0, 'g', 3));
    }
    p.drawText(labels, Qt::AlignHCenter | Qt::AlignVCenter, "surface");
}

bool FieldView::saveImage(const QString& path) const
{
    if (!m_slice.valid() || m_slice.image.isNull()) return false;
    // (row 0 of the image is the low end of v: flipped so +v points up, as the card draws it)
    const QImage plot = m_slice.image.mirrored(false, true);
    // As the card shows it: the plot on the card's dark ground, no transparency (the first file the test saved was a few outlines on nothing, 260 pixels wide), and large enough to look at: a whole
    // multiple of the plot's pixels that makes its longer side at least 1000
    const int longer = std::max(1, std::max(plot.width(), plot.height()));
    const int factor = std::max(1, (1000 + longer - 1) / longer);
    const int pw = plot.width() * factor, ph = plot.height() * factor;
    // And what the colours mean (H187, the user, 2026-11-14: "the exported image should display a legend obviously, otherwise it means nothing"): the plane it is across and where, the extent of
    // each axis, the colour bar with its numbers -- the card's own, drawn by the same code -- and what the lines are
    static const char* names = "XYZ";
    const int ua = FieldSlice::uAxis(m_slice.axis), va = FieldSlice::vAxis(m_slice.axis);
    const int margin = std::max(24, pw / 40);
    QFont f;
    f.setPixelSize(std::max(15, pw / 52));
    const QFontMetrics fm(f);
    const int line = fm.height() + 8;
    const int barH = std::max(16, pw / 60);
    const bool distance = !m_slice.hasColor;
    const QString title = T("Section across %1 at %2 mm").arg(names[m_slice.axis]).arg(m_slice.offset, 0, 'g', 5);
    const QString axes = QString("%1 %2 %3 %4").arg(names[ua]).arg(m_slice.min[ua], 0, 'g', 4).arg(QChar(0x2192)).arg(m_slice.max[ua], 0, 'g', 4) + QString("   ") + QChar(0x00b7) + "   "
                         + QString("%1 %2 %3 %4").arg(names[va]).arg(m_slice.min[va], 0, 'g', 4).arg(QChar(0x2192)).arg(m_slice.max[va], 0, 'g', 4);
    QString note;
    if (m_slice.hasColor) note = T("Inside: %1").arg(m_slice.colorLabel.isEmpty() ? T("result") : m_slice.colorLabel);
    else if (m_slice.spacing > 0) note = T("%1 to %2, lines every %3").arg(m_slice.lo, 0, 'g', 3).arg(m_slice.hi, 0, 'g', 3).arg(m_slice.spacing, 0, 'g', 3);
    const int width = pw + 2 * margin;
    const int height = margin + line + 8 + ph + 10 + line + 12 + barH + line + (note.isEmpty() ? 0 : line) + margin;
    QImage out(width, height, QImage::Format_RGB32);
    out.fill(QColor(22, 76, 94));
    QPainter p(&out);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.setRenderHint(QPainter::Antialiasing);
    p.setFont(f);
    int y = margin;
    p.setPen(kText);
    p.drawText(QRect(margin, y, pw, line), Qt::AlignLeft | Qt::AlignVCenter, title);
    y += line + 8;
    const QRect where(margin, y, pw, ph);
    p.fillRect(where, QColor(0, 0, 0, 60));
    p.drawImage(where, plot);
    y += ph + 10;
    p.setPen(kDim);
    p.drawText(QRect(margin, y, pw, line), Qt::AlignHCenter | Qt::AlignVCenter, axes);
    y += line + 12;
    const QRectF bar(margin, y, pw, barH);
    paintFieldLegend(p, bar, QRectF(margin, y + barH + 2, pw, line), m_slice.valid(), m_slice.hasColor, m_slice.colorMap, m_slice.colorLabel, m_slice.colorLo, m_slice.colorHi, m_slice.rangeIn,
                     m_slice.rangeOut);
    y += barH + line;
    if (!note.isEmpty())
    {
        p.setPen(kText);
        p.drawText(QRect(margin, y, pw, line), Qt::AlignHCenter | Qt::AlignVCenter, note);
    }
    (void)distance;
    p.end();
    return out.save(path);
}

// H187 (the user, 2026-11-12: "when you right click that 2D image, I want there to be an option to save it as an image")
// A press and a release of the right button are the plot's too: a press that nobody took goes on to the card and the viewport behind it, which opens a menu of its own -- the entry stood over that one,
// "awkwardly placed together in the context menu as a different kind of button" (the user, 2026-11-14)
void FieldView::mousePressEvent(QMouseEvent* e)
{
    e->accept();
}

void FieldView::mouseReleaseEvent(QMouseEvent* e)
{
    e->accept();
}

void FieldView::contextMenuEvent(QContextMenuEvent* e)
{
    e->accept();
    if (!m_slice.valid()) return;
    QMenu menu(this);
    menu.addAction(T("Save as image..."), this, [this] {
        const QString start = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
        const QString path = QFileDialog::getSaveFileName(this, T("Save the 2D view as an image"), QDir(start.isEmpty() ? QDir::homePath() : start).filePath("section.png"),
                                                          T("PNG image (*.png);;JPEG image (*.jpg)"));
        if (path.isEmpty()) return;
        if (!saveImage(path)) QMessageBox::warning(this, T("Save as image"), T("The image could not be saved."));
    });
    menu.exec(e->globalPos());
}

////////////////////////////////////////////////////////////////////////////////

// H191 (the user, 2026-11-14: "The legend you made editable for H191 is NOT COMMUNICATED TO THE USER AT ALL HOW TO EDIT IT ... And with the legend I mean EVERY LEGEND. The legend in the section cut for
// example does not have editable bounds"): the numbers at the two ends of the bar are boxes with a pencil -- a click types the end -- and under the bar a line says so, with an Auto switch that is
// lit while the ends are the values' own and gives them back when they were typed
static const int kLegendLabelTop = 13, kLegendLabelH = 18, kLegendHintTop = 33, kLegendHintH = 14;

FieldLegend::FieldLegend(QWidget* parent) : QWidget(parent)
{
    setFixedHeight(kLegendHintTop + kLegendHintH + 2);
    setMouseTracking(true);
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
    if (s.hasColor) m_manual = s.colorManual;           // (the distance scale's own: the card sets it, setManual)
    update();
}

void FieldLegend::setManual(bool manual)
{
    if (m_manual == manual) return;
    m_manual = manual;
    update();
}

QString FieldLegend::endText(bool high) const
{
    if (m_hasColor) return QString::number(high ? m_hi : m_lo, 'g', 4);
    return high ? T("+%1 outside").arg(m_out, 0, 'g', 3) : T("%1%2 inside").arg(QChar(0x2212)).arg(m_in, 0, 'g', 3);
}

// The box of a number: its text, a little room round it and the pencil on the side towards the middle
QRectF FieldLegend::endRect(bool high) const
{
    QFont f = font();
    f.setPointSize(8);
    const int w = QFontMetrics(f).horizontalAdvance(endText(high)) + 26;
    const QRectF r(high ? width() - w : 0, kLegendLabelTop, w, kLegendLabelH);
    return r;
}

QPoint FieldLegend::partCentre(int part) const
{
    if (part == 2) return autoPill().center();
    return endRect(part == 1).center().toPoint();
}

QRect FieldLegend::autoPill() const
{
    QFont f = font();
    f.setPointSize(7);
    const int w = QFontMetrics(f).horizontalAdvance(T("Auto")) + 14;
    return QRect(width() - w, kLegendHintTop, w, kLegendHintH);
}

void FieldLegend::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF bar(1, 2, width() - 2, 9);
    QFont f = font();
    f.setPointSize(8);
    p.setFont(f);
    // (the numbers are drawn by the one painter that the saved image uses too: inset, so that they stand in their boxes)
    paintFieldLegend(p, bar, QRectF(5, kLegendLabelTop, width() - 10, kLegendLabelH), m_valid, m_hasColor, m_map, m_label, m_lo, m_hi, m_in, m_out);
    if (!m_valid) return;
    const QColor accent(38, 139, 210);
    const QColor outline = m_manual ? accent : QColor(147, 161, 161, 150);
    for (int k = 0; k < 2; ++k)
    {
        const bool high = k == 1;
        const bool editing = m_edit && m_edit->isVisible() && m_editHigh == high;
        if (editing) continue;
        const QRectF r = endRect(high).adjusted(0.5, 0.5, -0.5, -0.5);
        p.setPen(QPen(outline, 1));
        p.setBrush(m_manual ? QBrush(QColor(38, 139, 210, 50)) : QBrush(Qt::NoBrush));      // (QBrush on both sides: `QColor : Qt::NoBrush` is a QColor, and Qt::NoBrush a black one: the boxes hid their numbers)
        p.drawRoundedRect(r, 3, 3);
        p.setPen(m_manual ? accent : kDim);
        QFont pf = f;
        pf.setPointSize(8);
        p.setFont(pf);
        const QRectF pencil = high ? QRectF(r.left() + 2, r.top(), 14, r.height()) : QRectF(r.right() - 16, r.top(), 14, r.height());
        p.drawText(pencil, Qt::AlignCenter, QString(QChar(0x270e)));
    }
    // The hint and the Auto switch under the bar
    QFont sf = f;
    sf.setPointSize(7);
    p.setFont(sf);
    p.setPen(kDim);
    const QRect pill = autoPill();
    p.drawText(QRect(0, kLegendHintTop, pill.left() - 6, kLegendHintH), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
               m_manual ? T("Ends typed by you") : T("Click a number to type the scale"));
    p.setPen(QPen(m_manual ? QColor(147, 161, 161, 150) : accent, 1));
    p.setBrush(m_manual ? QBrush(Qt::NoBrush) : QBrush(QColor(38, 139, 210, 90)));
    p.drawRoundedRect(QRectF(pill).adjusted(0.5, 0.5, -0.5, -0.5), 7, 7);
    p.setPen(m_manual ? kDim : QColor(255, 255, 255));
    p.drawText(pill, Qt::AlignCenter, T("Auto"));
}

void FieldLegend::mouseMoveEvent(QMouseEvent* e)
{
    const QPointF at = e->pos();
    const bool over = m_valid && (endRect(false).contains(at) || endRect(true).contains(at) || (m_manual && autoPill().contains(e->pos())));
    setCursor(over ? Qt::PointingHandCursor : Qt::ArrowCursor);
    QWidget::mouseMoveEvent(e);
}

void FieldLegend::leaveEvent(QEvent* e)
{
    unsetCursor();
    QWidget::leaveEvent(e);
}

void FieldLegend::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton || !m_valid)
    {
        QWidget::mousePressEvent(e);
        return;
    }
    const QPointF at = e->pos();
    if (endRect(false).contains(at)) startEdit(false);
    else if (endRect(true).contains(at)) startEdit(true);
    else if (autoPill().contains(e->pos()))
    {
        if (m_manual) emit autoClicked();
    }
    else
    {
        QWidget::mousePressEvent(e);
        return;
    }
    e->accept();
}

void FieldLegend::startEdit(bool high)
{
    if (!m_edit)
    {
        m_edit = new QLineEdit(this);
        m_edit->setStyleSheet("QLineEdit { color: #eee8d5; background: #0b2230; border: 1px solid rgba(38, 139, 210, 230);"
                              " border-radius: 3px; padding: 0px 3px; font-size: 8pt; selection-background-color: rgba(38, 139, 210, 200); }");
        m_edit->installEventFilter(this);
        connect(m_edit, &QLineEdit::returnPressed, this, [this] { commitEdit(); });
    }
    m_editHigh = high;
    const QRectF r = endRect(high);
    const int w = std::max(72, int(r.width()));
    m_edit->setGeometry(high ? width() - w : 0, int(r.top()), w, int(r.height()));
    // (the number alone: the distance scale is one number, ± about the surface)
    m_edit->setText(QString::number(m_hasColor ? (high ? m_hi : m_lo) : (high ? m_out : m_in), 'g', 6));
    m_edit->show();
    m_edit->raise();
    m_edit->setFocus();
    m_edit->selectAll();
    update();
}

void FieldLegend::commitEdit()
{
    if (!m_edit) return;
    QString text = m_edit->text().trimmed();
    m_edit->hide();
    update();
    text.replace(',', '.');                         // (a decimal comma)
    bool ok = false;
    const double v = text.toDouble(&ok);
    if (!ok || !std::isfinite(v)) return;
    emit rangeTyped(m_editHigh, float(v));
}

bool FieldLegend::eventFilter(QObject* o, QEvent* e)
{
    if (o == m_edit)
    {
        if (e->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(e)->key() == Qt::Key_Escape)
        {
            m_edit->hide();
            update();
            return true;
        }
        if (e->type() == QEvent::FocusOut)
        {
            m_edit->hide();
            update();
        }
    }
    return QWidget::eventFilter(o, e);
}

////////////////////////////////////////////////////////////////////////////////

KeyCard::KeyCard(const QString& cardKey, QWidget* parent) : QFrame(parent), m_close(new QToolButton(this))
{
    setObjectName("KeyCard");
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet("#KeyCard { background: rgba(22, 76, 94, 222); border: 1px solid rgba(147, 161, 161, 90); border-radius: 6px; }"
                  "QToolButton { color: #eee8d5; background: transparent; border: none; font-size: 10pt; padding: 0px; }"
                  "QToolButton:hover { background: rgba(147, 161, 161, 60); border-radius: 3px; }");
    m_close->setText(QString(QChar(0x00d7)));
    m_close->setToolTip(T("Close"));
    m_close->setCursor(Qt::ArrowCursor);
    connect(m_close, &QToolButton::clicked, this, [this] { if (onClose) onClose(); });
    new CardController(this, cardKey, nullptr, QSize(90, 40));          // (dragged by any empty place, resized from its edges)
}

QSize KeyCard::sizeHint() const
{
    QFont f = font();
    f.setPointSizeF(8.5);
    const QFontMetrics fm(f);
    int textW = fm.horizontalAdvance(m_title) + 24;
    for (const Row& r : m_rows) textW = std::max(textW, 18 + fm.horizontalAdvance(r.text));
    const int rowH = fm.height() + 4;
    return QSize(textW + 20, fm.height() + 20 + rowH * int(m_rows.size()));
}

void KeyCard::setKey(const QString& title, const QVector<Row>& rows)
{
    m_title = title;
    m_rows = rows;
    updateGeometry();
    update();
}

void KeyCard::resizeEvent(QResizeEvent* e)
{
    QFrame::resizeEvent(e);
    m_close->setGeometry(width() - 22, 4, 18, 18);
}

void KeyCard::paintEvent(QPaintEvent* e)
{
    QFrame::paintEvent(e);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QFont f = font();
    f.setPointSizeF(8.5);
    p.setFont(f);
    const QFontMetrics fm(f);
    p.setPen(kText);
    p.drawText(QRect(10, 6, width() - 36, fm.height()), Qt::AlignLeft | Qt::AlignVCenter, m_title);
    const int rowH = fm.height() + 4;
    int y = fm.height() + 14;
    for (const Row& r : m_rows)
    {
        p.setPen(QPen(kDim, 1));
        p.setBrush(r.colour);
        p.drawRect(QRect(10, y + 2, 12, fm.height() - 2));
        p.setPen(kText);
        p.drawText(QRect(28, y, width() - 36, rowH), Qt::AlignLeft | Qt::AlignVCenter, r.text);
        y += rowH;
    }
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
    m_info->setAlignment(Qt::AlignLeft | Qt::AlignTop);        // (H182, the user, 2026-11-14: the line is anchored at the top with the other elements, not centred in the room a taller card leaves)

    m_show2d->setCheckable(true);
    m_show2d->setText(T("2D view"));
    m_show2d->setToolTip(T("Flat 2D plot of the plane"));
    m_show2d->setObjectName("sectionShow2d");
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
        if (!m_slice.hasColor) m_legend->setManual(!b);
        emitChange();
    });
    // H191 (the user, 2026-11-14: "EVERY LEGEND ... Legend bound should inherently be editable"): the numbers of the card's own legend are typed over like those of the viewport's.  The distance scale is the
    // one number the card has in its range box (either end sets it); the colours of a model coloured by a field belong to that model's legend, so the typed ends go to the viewport, which keeps them
    connect(m_legend, &FieldLegend::rangeTyped, this, [this](bool high, float v) {
        if (!m_slice.hasColor)
        {
            const float r = std::fabs(v);
            if (!(r > 0)) return;
            m_autoRange->setChecked(false);
            m_range->setValue(r);
            return;
        }
        float lo = m_slice.colorLo, hi = m_slice.colorHi;
        (high ? hi : lo) = v;
        if (!(hi > lo)) return;             // (the scale goes up: the ends are not swapped)
        emit colourRangeSet(lo, hi);
    });
    connect(m_legend, &FieldLegend::autoClicked, this, [this] {
        if (!m_slice.hasColor) m_autoRange->setChecked(true);
        else emit colourRangeAuto();
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
        showPlotFully(b);
    });

    if (parent)
    {
        parent->installEventFilter(this);
    }
    place();
}

// H182 (the user, 2026-11-12: "When you click on it, it is not always visible immediately and the user has to manually extend the card"): the card has the room for the plot at once.  The layout has not
// seen that the plot is shown until it is asked to (the card was sized from the height it had without it), and a card that the user sized keeps its height unless it is made taller here
void SectionPanel::showPlotFully(bool shown)
{
    // And the card is the size it was when the plot is closed again (H182, the user, 2026-11-14: "the card should return to its original size as well after the 2D view has been closed").  A card that
    // was never sized fits its contents by itself (place()); one that was sized by hand, or grown for the plot, is put back to the height it had when the plot was opened -- unless it was
    // sized again while the plot was shown, which is the user's
    if (shown)
    {
        m_heightBeforePlot = height();
        m_heightWithPlot = 0;
    }
    else if (m_heightBeforePlot > 0)
    {
        if (CardController::sized(this) && height() == m_heightWithPlot)
        {
            QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
            if (layout()) layout()->activate();
            resize(width(), std::max(m_heightBeforePlot, minimumSizeHint().height()));
        }
        m_heightBeforePlot = m_heightWithPlot = 0;
    }
    auto fit = [this, shown] {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
        if (layout()) layout()->activate();
        if (shown && CardController::sized(this)) resize(width(), std::max(height(), minimumSizeHint().height()));
        place();
        if (shown) CardController::settle(this);
        if (shown) m_heightWithPlot = height();
    };
    fit();
    QTimer::singleShot(0, this, fit);          // (and once more when the layout has had its turn)
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
    if (!s.hasColor) m_legend->setManual(!m_autoRange->isChecked());
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

    // Header: collapse only (the card is there while a field is open: it has no close button -- its icon in the viewport closes it)
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

    // Which of the open fields (only when there are several)
    {
        auto row = new QHBoxLayout(m_comboRow);
        row->setContentsMargins(0, 0, 0, 0);
        row->addWidget(label(T("Field")));
        m_combo->setObjectName("fieldChoice");
        m_combo->setToolTip(T("The open fields: choose the one this card is about"));
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
    m_info->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_show2d->setCheckable(true);
    m_show2d->setText(T("2D view"));
    m_show2d->setToolTip(T("Flat 2D plot of the disc"));
    m_show2d->setObjectName("fieldShow2d");
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
        showPlotFully(b);
    });
    // H191: the ends of the viewer's colours, typed over in its legend (the viewer keeps them until Auto, or until the script runs again)
    connect(m_legend, &FieldLegend::rangeTyped, this, [this](bool high, float v) {
        float lo = m_slice.colorLo, hi = m_slice.colorHi;
        (high ? hi : lo) = v;
        if (!(hi > lo)) return;
        emit rangeSet(m_key, lo, hi);
    });
    connect(m_legend, &FieldLegend::autoClicked, this, [this] { emit rangeAuto(m_key); });

    if (parent) parent->installEventFilter(this);
    place();
}

// (see SectionPanel::showPlotFully: H182)
void FieldPanel::showPlotFully(bool shown)
{
    if (shown)
    {
        m_heightBeforePlot = height();
        m_heightWithPlot = 0;
    }
    else if (m_heightBeforePlot > 0)
    {
        if (CardController::sized(this) && height() == m_heightWithPlot)
        {
            QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
            if (layout()) layout()->activate();
            resize(width(), std::max(m_heightBeforePlot, minimumSizeHint().height()));
        }
        m_heightBeforePlot = m_heightWithPlot = 0;
    }
    auto fit = [this, shown] {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
        if (layout()) layout()->activate();
        if (shown && CardController::sized(this)) resize(width(), std::max(height(), minimumSizeHint().height()));
        place();
        if (shown) CardController::settle(this);
        if (shown) m_heightWithPlot = height();
    };
    fit();
    QTimer::singleShot(0, this, fit);
}

bool FieldPanel::eventFilter(QObject* obj, QEvent* e)
{
    if (obj == parentWidget() && e->type() == QEvent::Resize) place();
    return QFrame::eventFilter(obj, e);
}

void FieldPanel::place()
{
    if (!CardController::sized(this)) resize(300, std::max(minimumSizeHint().height(), sizeHint().height()));
    // (top-right, below the triad; where another card is the card is put at the nearest free place: see CardController::settle -- no card goes where another one is, and none follows another)
    if (parentWidget() && !CardController::moved(this)) move(parentWidget()->width() - width() - 8, 124);
}

float FieldPanel::radiusMax() const
{
    const QVector3D size = m_max - m_min;
    return 1.5f * std::max({size.x(), size.y(), size.z(), 1e-3f});
}

void FieldPanel::syncWidgets()
{
    m_updating = true;
    const int a = std::max(0, std::min(2, m_settings.axis));
    for (int i = 0; i < 3; ++i) m_axes[i]->setChecked(i == a);
    const float lo = 0.02f * radiusMax() / 1.5f, hi = radiusMax();
    m_radiusSlider->setValue(int(std::max(0.f, std::min(1.f, (m_settings.radius - lo) / std::max(hi - lo, 1e-6f))) * 1000));
    m_radiusSpin->setValue(m_settings.radius);
    m_opacity->setValue(int(m_settings.opacity * 100));
    m_updating = false;
}

void FieldPanel::emitChange()
{
    emit(settingsChanged(m_key, m_settings));
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

void FieldPanel::setViewer(QString key, FieldViewSettings s, QVector3D regionMin, QVector3D regionMax)
{
    m_key = key;
    m_settings = s;
    m_min = regionMin;
    m_max = regionMax;
    syncWidgets();
    updateInfo();
}

void FieldPanel::setCentre(QString key, FieldViewSettings s)
{
    if (key != m_key) return;
    m_settings = s;
    syncWidgets();
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
