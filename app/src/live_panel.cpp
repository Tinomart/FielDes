/*
FielDes: field-driven design
Derived from Studio, a simple GUI for the libfive CAD kernel

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/
#include "fieldes/live_panel.hpp"

#include <algorithm>
#include <cmath>

#include <QFontMetrics>
#include <QLabel>
#include <QPainter>
#include <QPolygonF>
#include <QVBoxLayout>

#include "fieldes/carddrag.hpp"
#include "fieldes/i18n.hpp"

namespace FielDes {

LiveGraph::LiveGraph(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);       // (a click on the card, anywhere, drags it)
}

void LiveGraph::setHistory(std::vector<float> history, int iterations)
{
    m_history = std::move(history);
    m_iterations = std::max(iterations, 2);
    update();
}

void LiveGraph::paintEvent(QPaintEvent*)
{
    if (m_history.size() < 2) return;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF g = QRectF(rect()).adjusted(2, 3, -2, -3);
    float lo = m_history.front(), hi = m_history.front();
    for (float c : m_history) { lo = std::min(lo, c); hi = std::max(hi, c); }
    const float span = hi > lo ? hi - lo : 1.0f;
    QPolygonF line;
    for (size_t i = 0; i < m_history.size(); ++i)
    {
        const double x = g.left() + g.width() * double(i) / std::max<size_t>(1, size_t(m_iterations) - 1);
        const double y = g.bottom() - g.height() * double(m_history[i] - lo) / span;
        line << QPointF(x, y);
    }
    painter.setPen(QPen(QColor(0x9a, 0xd0, 0xf0), 1.5));
    painter.drawPolyline(line);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0xee, 0xe8, 0xd5));
    painter.drawEllipse(line.last(), 2.5, 2.5);
}

LivePanel::LivePanel(QWidget* parent)
    : QFrame(parent), m_first(new QLabel), m_second(new QLabel), m_third(new QLabel), m_graph(new LiveGraph)
{
    setObjectName("LivePanel");
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(
        "#LivePanel { background: rgba(22, 76, 94, 225); border: 1px solid rgba(147, 161, 161, 90); border-radius: 6px; }"
        "QLabel { color: #eee8d5; font-size: 9pt; padding: 0px; background: transparent; }");
    QFont bold = m_first->font();
    bold.setBold(true);
    m_first->setFont(bold);
    for (auto l : {m_second, m_third})
    {
        l->setWordWrap(true);
        l->setAttribute(Qt::WA_TransparentForMouseEvents);
    }
    m_first->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 8, 10, 8);              // (the same on every side: the margin is where the card is resized from)
    layout->setSpacing(3);
    layout->addWidget(m_first);
    layout->addWidget(m_second);
    layout->addWidget(m_third);
    layout->addWidget(m_graph, 1);
    setMinimumWidth(220);
    new CardController(this, "live-view", nullptr, QSize(220, 110));      // (dragged by any empty place, resized from its edges)
}

int LivePanel::textWidth() const
{
    const QFontMetrics fm(m_second->font());
    int w = 0;
    for (auto l : {m_first, m_second, m_third}) w = std::max(w, QFontMetrics(l->font()).horizontalAdvance(l->text()));
    (void)fm;
    return w + 20;
}

void LivePanel::setView(const libfive::run_progress::LiveView& v)
{
    m_first->setText(T("Optimising · iteration %1 of %2").arg(v.iteration).arg(v.iterations));
    const double firstValue = v.history.empty() ? v.objective : double(v.history.front());
    const double fall = firstValue != 0 ? 100.0 * (v.objective - firstValue) / std::abs(firstValue) : 0.0;
    const QString value = QString::number(v.objective, 'g', 4), percent = QString::number(fall, 'f', 1);
    m_second->setText(v.quantity == "drag" ? T("drag %1 (%2 % from the first iteration)").arg(value, percent)
                    : v.quantity == "objective" ? T("objective %1 (%2 % from the first iteration)").arg(value, percent)
                                                : T("compliance %1 (%2 % from the first iteration)").arg(value, percent));
    const QString material = QString::number(100.0 * v.volume, 'f', 1);
    const QString low = QString::number(100.0 * v.volumeLow, 'f', 1), high = QString::number(100.0 * v.volumeHigh, 'f', 1);
    const QString move = QString::number(v.change, 'f', v.kind == "flow" ? 2 : 3);
    if (v.kind == "flow")
        m_third->setText(std::abs(v.volumeHigh - v.volumeLow) < 1e-6
                             ? T("body volume %1 % of the original, kept at %2 % · boundary moved %3 mm").arg(material, low, move)
                             : T("body volume %1 % of the original, allowed %2 % to %3 % · boundary moved %4 mm").arg(material, low, high, move));
    else
        m_third->setText(T("material %1 % of the part, asked for %2 % · biggest change %3").arg(material, low, move));
    m_graph->setHistory(std::vector<float>(v.history.begin(), v.history.end()), v.iterations);
}

}   // namespace FielDes
