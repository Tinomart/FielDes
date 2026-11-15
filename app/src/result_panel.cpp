/*
FielDes: field-driven design
Derived from Studio, a simple GUI for the libfive CAD kernel

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/
#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include <QComboBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

#include "fieldes/i18n.hpp"
#include "fieldes/carddrag.hpp"
#include "fieldes/result_panel.hpp"
#include "fieldes/colormap.hpp"

namespace FielDes {

ColorBar::ColorBar(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(120);
    setMouseTracking(true);
    setToolTip(T("Click the lowest or the highest number to type that end of the colour scale yourself; Auto gives the ends of the values back"));
}

// H191: the geometry of the scale, the same for the painting and for the clicks
QRect ColorBar::barRect() const
{
    QFont font = this->font();
    font.setPointSizeF(8);
    const QFontMetrics fm(font);
    return QRect(4, fm.height() / 2 + 2, 14, height() - fm.height() - 4);
}

QRect ColorBar::tickLabel(int k) const
{
    QFont font = this->font();
    font.setPointSizeF(8);
    const QFontMetrics fm(font);
    const QRect bar = barRect();
    const int ticks = 5;
    const int y = bar.bottom() - (bar.height() * k) / (ticks - 1);
    return QRect(bar.right() + 8, y - fm.height() / 2, width() - bar.right() - 8, fm.height());
}

QRect ColorBar::autoPill() const
{
    QFont font = this->font();
    font.setPointSizeF(8);
    const QFontMetrics fm(font);
    const int w = fm.horizontalAdvance(T("Auto")) + 14;
    return QRect(width() - w - 2, barRect().center().y() - fm.height() / 2 - 2, w, fm.height() + 4);
}

void ColorBar::setManual(bool manual)
{
    if (m_manual == manual) return;
    m_manual = manual;
    update();
}

void ColorBar::mouseMoveEvent(QMouseEvent* e)
{
    const bool onNumber = scaleShown() && (tickLabel(0).contains(e->pos()) || tickLabel(4).contains(e->pos()));
    const bool onAuto = scaleShown() && m_manual && autoPill().contains(e->pos());
    setCursor(onNumber || onAuto ? Qt::PointingHandCursor : Qt::ArrowCursor);
    e->ignore();
}

void ColorBar::leaveEvent(QEvent* e)
{
    unsetCursor();
    QWidget::leaveEvent(e);
}

void ColorBar::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton && scaleShown())
    {
        if (m_manual && autoPill().contains(e->pos()))
        {
            emit(rangeAuto());
            e->accept();
            return;
        }
        if (tickLabel(4).contains(e->pos())) { startEdit(true); e->accept(); return; }
        if (tickLabel(0).contains(e->pos())) { startEdit(false); e->accept(); return; }
    }
    e->ignore();            // (anywhere else a press drags the card)
}

void ColorBar::startEdit(bool high)
{
    if (!m_edit)
    {
        m_edit = new QLineEdit(this);
        m_edit->setObjectName("legendRangeEdit");
        // (a solid background: the number that is there under it is drawn by the bar, and shows through a see-through box behind the one that is typed -- the picture of H191)
        m_edit->setStyleSheet("QLineEdit { color: #eee8d5; background: #0b2230; border: 1px solid rgba(38, 139, 210, 230);"
                              " border-radius: 3px; padding: 0px 3px; font-size: 8pt; selection-background-color: rgba(38, 139, 210, 200); }");
        m_edit->installEventFilter(this);
        connect(m_edit, &QLineEdit::returnPressed, this, [this] { commitEdit(); });
    }
    m_editHigh = high;
    const QRect r = tickLabel(high ? 4 : 0).adjusted(-2, -1, 0, 1);
    m_edit->setGeometry(r.left(), r.top(), std::max(60, r.width()), r.height());
    m_edit->setText(QString::number(high ? m_hi : m_lo, 'g', 6));
    m_edit->show();
    m_edit->raise();
    m_edit->setFocus();
    m_edit->selectAll();
}

void ColorBar::commitEdit()
{
    if (!m_edit) return;
    bool ok = false;
    QString text = m_edit->text().trimmed();
    text.replace(',', '.');                 // (a decimal comma)
    const double v = text.toDouble(&ok);
    m_edit->hide();
    if (!ok || !std::isfinite(v)) return;
    const float lo = m_editHigh ? m_lo : float(v), hi = m_editHigh ? float(v) : m_hi;
    if (!(hi > lo)) return;                 // (the scale has to go up: the ends are not swapped, and an empty one is not taken)
    emit(rangeEdited(lo, hi));
}

bool ColorBar::eventFilter(QObject* o, QEvent* e)
{
    if (o == m_edit)
    {
        if (e->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(e)->key() == Qt::Key_Escape)
        {
            m_edit->hide();
            return true;
        }
        if (e->type() == QEvent::FocusOut) m_edit->hide();
    }
    return QWidget::eventFilter(o, e);
}

void ColorBar::setRange(float lo, float hi, const QString& map)
{
    m_lo = lo;
    m_hi = hi;
    m_map = map;
    update();
}

void ColorBar::setSupports(bool on)
{
    if (m_supports == on) return;
    m_supports = on;
    update();
}

void ColorBar::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QFont font = p.font();
    font.setPointSizeF(8);
    p.setFont(font);
    const QFontMetrics fm(font);
    const QRect bar = barRect();
    if (m_map == "change" || m_map == "violation")
    {
        // (the geometry change of a shell is three classes, and a rule check two: not a scale, but swatches, each with what it is.  The words are T() calls written out, so that the
        // translation tool finds them)
        std::vector<std::pair<float, QString>> classes;
        bool supports = false;      // (the last class is the supports of a print, in the amber of their own map: a body of the scene, and a line of this legend and not a card of its own)
        if (m_map == "violation") classes = {{1.f, T("broken")}, {0.f, T("kept")}};
        else
        {
            classes = {{1.f, T("added")}, {0.5f, T("unchanged")}, {0.f, T("removed")}};
            if (m_supports && m_map == "change")
            {
                classes.push_back({1.f, T("print supports")});          // ("supports" alone is a support of an analysis: a fixed face)
                supports = true;
            }
        }
        const int count = int(classes.size());
        const int h = bar.height() / count;
        for (int i = 0; i < count; i++)
        {
            const QRect swatch(bar.left(), bar.top() + i * h + (i ? 1 : 0), bar.width(), h - (i ? 1 : 0));
            p.setPen(QPen(QColor(0x93, 0xa1, 0xa1), 1));
            p.setBrush(colormapColor((supports && i == count - 1) ? QString("support") : m_map, classes[size_t(i)].first));
            p.drawRect(swatch);
            p.setPen(QColor(0xee, 0xe8, 0xd5));
            p.drawText(QRect(bar.right() + 8, swatch.center().y() - fm.height() / 2, width() - bar.right() - 8, fm.height()),
                       Qt::AlignLeft | Qt::AlignVCenter, classes[size_t(i)].second);
        }
        return;
    }
    QLinearGradient g(bar.bottomLeft(), bar.topLeft());
    for (int k = 0; k <= 16; k++) g.setColorAt(k / 16.0, colormapColor(m_map, k / 16.0f));
    p.setBrush(g);
    p.setPen(QPen(QColor(0x93, 0xa1, 0xa1), 1));
    p.drawRect(bar);
    const int ticks = 5;
    for (int k = 0; k < ticks; k++)
    {
        const int y = bar.bottom() - (bar.height() * k) / (ticks - 1);
        const float v = m_lo + (m_hi - m_lo) * k / (ticks - 1);
        // (H191: the ends typed in the legend are in the blue of the card's switches that are on)
        p.setPen(m_manual && (k == 0 || k == ticks - 1) ? QColor(0x7c, 0xc0, 0xff) : QColor(0xee, 0xe8, 0xd5));
        p.drawLine(bar.right(), y, bar.right() + 4, y);
        const QString text = QString::number(v, 'g', 4);
        const QRect labelRect(bar.right() + 8, y - fm.height() / 2, width() - bar.right() - 8, fm.height());
        if (k == 0 || k == ticks - 1)
        {
            // (H191, the user, 2026-11-14: "NOT COMMUNICATED TO THE USER AT ALL HOW TO EDIT IT. MAKE IT CLEAR WHERE THEY CAN EDIT."): the lowest and the highest number are in a box like a field to type in, with a
            // pencil after it: a click on them types that end.  The same on every legend
            const int w = fm.horizontalAdvance(text);
            const QRect field(labelRect.left() - 3, labelRect.top() - 1, std::min(w + 22, labelRect.width() + 3), labelRect.height() + 2);
            p.setPen(QPen(QColor(0x7c, 0xc0, 0xff, m_manual ? 230 : 150), 1));
            p.setBrush(QColor(255, 255, 255, 22));
            p.drawRoundedRect(field, 3, 3);
            p.setPen(m_manual ? QColor(0x7c, 0xc0, 0xff) : QColor(0xee, 0xe8, 0xd5));
            p.drawText(labelRect, Qt::AlignLeft | Qt::AlignVCenter, text);
            p.setPen(QColor(0x7c, 0xc0, 0xff, 200));
            p.drawText(QRect(field.right() - 15, field.top(), 14, field.height()), Qt::AlignCenter, QString(QChar(0x270E)));
        }
        else p.drawText(labelRect, Qt::AlignLeft | Qt::AlignVCenter, text);
    }
    {
        // (the Auto switch is always there: dim while the ends are the values' own, lit and clickable when they were typed)
        const QRect pill = autoPill();
        p.setPen(QPen(m_manual ? QColor(38, 139, 210, 230) : QColor(0x93, 0xa1, 0xa1, 160), 1));
        p.setBrush(m_manual ? QBrush(QColor(38, 139, 210, 120)) : QBrush(Qt::NoBrush));         // (QBrush on both sides: `QColor : Qt::NoBrush` is a QColor, and Qt::NoBrush a black one -- the Auto pill was drawn black)
        p.drawRoundedRect(pill, 4, 4);
        p.setPen(m_manual ? QColor(0xee, 0xe8, 0xd5) : QColor(0x93, 0xa1, 0xa1));
        p.drawText(pill, Qt::AlignCenter, T("Auto"));
    }
}

ResidualGraph::ResidualGraph(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);       // (a click on the card, anywhere, drags it)
}

void ResidualGraph::setHistory(const std::vector<float>& history)
{
    m_history = history;
    update();
}

void ResidualGraph::paintEvent(QPaintEvent*)
{
    if (m_history.size() < 2) return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QFont font = p.font();
    font.setPointSizeF(7);
    p.setFont(font);
    const QFontMetrics fm(font);
    // (the residual on a log scale, from the whole decade above the first value to the whole decade below the last)
    auto decades = [](float v) { return std::log10(std::max(1e-16, double(v))); };
    double top = decades(m_history.front()), bottom = top;
    for (float v : m_history)
    {
        top = std::max(top, decades(v));
        bottom = std::min(bottom, decades(v));
    }
    top = std::ceil(top - 1e-9);
    bottom = std::floor(bottom + 1e-9);
    if (top - bottom < 1.0) bottom = top - 1.0;
    const int labelWidth = fm.horizontalAdvance("1e-00") + 4;
    const QRectF g(labelWidth + 2, fm.height() / 2.0 + 2.0, width() - labelWidth - 6, height() - fm.height() - 4.0);          // (room above and below for the numbers of the first and the last decade)
    // (a line and a number at every decade; many decades: every second one, so that the numbers do not touch)
    const int span = int(top - bottom);
    const int every = std::max(1, int(std::ceil(span * double(fm.height()) * 1.2 / std::max(1.0, g.height()))));
    for (int e = int(bottom); e <= int(top); ++e)
    {
        const double y = g.bottom() - g.height() * (double(e) - bottom) / (top - bottom);
        p.setPen(QPen(QColor(0x93, 0xa1, 0xa1, 55), 1));
        p.drawLine(QPointF(g.left(), y), QPointF(g.right(), y));
        if ((e - int(bottom)) % every != 0) continue;
        p.setPen(QColor(0x93, 0xa1, 0xa1));
        p.drawText(QRectF(0, y - fm.height() / 2.0, labelWidth, fm.height()), Qt::AlignRight | Qt::AlignVCenter, e == 0 ? QString("1") : QString("1e%1").arg(e));
    }
    QPolygonF line;
    for (size_t i = 0; i < m_history.size(); ++i)
        line << QPointF(g.left() + g.width() * double(i) / double(m_history.size() - 1),
                        g.bottom() - g.height() * (decades(m_history[i]) - bottom) / (top - bottom));
    p.setPen(QPen(QColor(0x9a, 0xd0, 0xf0), 1.5));
    p.drawPolyline(line);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0xee, 0xe8, 0xd5));
    p.drawEllipse(line.last(), 2.5, 2.5);
}

ObjectiveGraph::ObjectiveGraph(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);       // (a click on the card, anywhere, drags it)
}

void ObjectiveGraph::setHistory(const std::vector<float>& history)
{
    m_history = history;
    update();
}

void ObjectiveGraph::paintEvent(QPaintEvent*)
{
    if (m_history.size() < 2) return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QFont font = p.font();
    font.setPointSizeF(7);
    p.setFont(font);
    const QFontMetrics fm(font);
    // (on a linear scale, from the lowest value of the curve to the highest: a line and a number at each end)
    double lo = double(m_history.front()), hi = lo;
    for (float v : m_history)
    {
        lo = std::min(lo, double(v));
        hi = std::max(hi, double(v));
    }
    if (!(hi > lo)) hi = lo + std::max(1e-12, std::abs(lo) * 0.01);         // (a flat curve)
    auto number = [](double v) { return QString::number(v, 'g', 3); };
    const int labelWidth = std::max(fm.horizontalAdvance(number(lo)), fm.horizontalAdvance(number(hi))) + 4;
    const QRectF g(labelWidth + 2, fm.height() / 2.0 + 2.0, width() - labelWidth - 6, height() - fm.height() - 4.0);          // (room above and below for the numbers of the highest and the lowest value)
    p.setPen(QPen(QColor(0x93, 0xa1, 0xa1, 55), 1));
    p.drawLine(QPointF(g.left(), g.top()), QPointF(g.right(), g.top()));
    p.drawLine(QPointF(g.left(), g.bottom()), QPointF(g.right(), g.bottom()));
    p.setPen(QColor(0x93, 0xa1, 0xa1));
    p.drawText(QRectF(0, g.top() - fm.height() / 2.0, labelWidth, fm.height()), Qt::AlignRight | Qt::AlignVCenter, number(hi));
    p.drawText(QRectF(0, g.bottom() - fm.height() / 2.0, labelWidth, fm.height()), Qt::AlignRight | Qt::AlignVCenter, number(lo));
    QPolygonF line;
    for (size_t i = 0; i < m_history.size(); ++i)
        line << QPointF(g.left() + g.width() * double(i) / double(m_history.size() - 1),
                        g.bottom() - g.height() * (double(m_history[i]) - lo) / (hi - lo));
    p.setPen(QPen(QColor(0x9a, 0xd0, 0xf0), 1.5));
    p.drawPolyline(line);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0xee, 0xe8, 0xd5));
    p.drawEllipse(line.last(), 2.5, 2.5);
}

////////////////////////////////////////////////////////////////////////////////

ResultPanel::ResultPanel(QWidget* parent)
    : QFrame(parent), m_fields(new QComboBox), m_bar(new ColorBar),
      m_deformRow(new QWidget), m_deform(new QSlider(Qt::Horizontal)), m_scaleLabel(new QLabel),
      m_trueScale(new QToolButton), m_elements(new QToolButton), m_flow(new QToolButton),
      m_stepRow(new QWidget), m_play(new QToolButton), m_stepBack(new QToolButton), m_stepForward(new QToolButton),
      m_playMode(new QToolButton), m_speed(new QComboBox),
      m_step(new QSlider(Qt::Horizontal)),
      m_stepLabel(new QLabel), m_note(new QLabel), m_info(new QLabel)
{
    setObjectName("ResultPanel");
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(
        "#ResultPanel { background: rgba(22, 76, 94, 222);"
        "  border: 1px solid rgba(147, 161, 161, 90); border-radius: 6px; }"
        "QLabel { color: #93a1a1; font-size: 8pt; }"
        "QLabel#resultNote { color: #b3bdbd; }"
        "QComboBox { color: #eee8d5; background: rgba(0, 0, 0, 70);"
        "  border: 1px solid rgba(147, 161, 161, 70); border-radius: 3px; padding: 2px 6px;"
        "  font-size: 9pt; font-weight: bold; }"
        "QComboBox QAbstractItemView { color: #eee8d5; background: #25607a;"
        "  selection-background-color: rgba(38, 139, 210, 200); }"
        "QToolButton { color: #eee8d5; background: rgba(147, 161, 161, 28);"
        "  border: 1px solid rgba(147, 161, 161, 60); border-radius: 4px;"
        "  padding: 2px 8px; font-size: 8pt; }"
        "QToolButton:hover { background: rgba(147, 161, 161, 60); }"
        "QToolButton:checked { background: rgba(38, 139, 210, 170);"
        "  border-color: rgba(38, 139, 210, 230); color: white; }"
        "QToolButton:disabled { color: rgba(147, 161, 161, 120); background: rgba(147, 161, 161, 14);"
        "  border-color: rgba(147, 161, 161, 30); }"            // (the Elements button of a result that has more elements than are drawn: H186)
        "QSlider::groove:horizontal { height: 4px; background: rgba(147, 161, 161, 70);"
        "  border-radius: 2px; }"
        "QSlider::sub-page:horizontal { background: rgba(38, 139, 210, 200); border-radius: 2px; }"
        "QSlider::handle:horizontal { width: 12px; margin: -5px 0; border-radius: 6px;"
        "  background: #eee8d5; }");

    m_fields->setObjectName("resultField");
    m_fields->setToolTip(T("Field shown"));
    m_deform->setObjectName("resultDeform");
    m_deform->setRange(0, 100);
    m_deform->setToolTip(T("Deformation magnification"));
    m_trueScale->setObjectName("resultTrueScale");
    m_trueScale->setText("1:1");
    m_trueScale->setToolTip(T("True size"));
    m_elements->setObjectName("resultElements");
    m_elements->setCheckable(true);
    m_elements->setText(T("Elements"));
    m_elements->setToolTip(T("Show the solver's elements, each with its own value"));
    m_flow->setObjectName("resultFlow");
    m_flow->setCheckable(true);
    m_flow->setText(T("Flow"));
    m_flow->setToolTip(T("Streamlines from the inlets with particles moving along them, drawn over the fluid"));
    m_play->setObjectName("resultPlay");
    m_play->setCheckable(true);
    m_play->setText(QString(QChar(0x25B6)));
    m_play->setToolTip(T("Play / pause the steps"));
    m_stepBack->setObjectName("resultStepBack");
    m_stepBack->setText(QString(QChar(0x25C2)));
    m_stepBack->setToolTip(T("One step back"));
    m_stepForward->setObjectName("resultStepForward");
    m_stepForward->setText(QString(QChar(0x25B8)));
    m_stepForward->setToolTip(T("One step forward"));
    m_playMode->setObjectName("resultPlayMode");
    m_playMode->setText(QString(QChar(0x21BB)));
    m_playMode->setToolTip(T("How play runs: round and round, back and forth, or once to the end (click to change)"));
    m_speed->setObjectName("resultPlaySpeed");
    m_speed->setToolTip(T("Play speed: x1 is ten steps a second"));
    {
        static const float speeds[] = {0.125f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f};
        static const ushort glyphs[] = {0x215B, 0x00BC, 0x00BD, '1', '2', '4'};
        for (int i = 0; i < 6; ++i)
            m_speed->addItem(QString::fromUtf8("\xC3\x97") + QString(QChar(glyphs[i])), speeds[i]);
        m_speed->setCurrentIndex(3);
    }
    m_step->setObjectName("resultStep");
    m_step->setRange(0, 0);
    m_step->setToolTip(T("Step shown"));
    m_stepLabel->setObjectName("resultStepLabel");
    m_stepLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    // play, how it runs, how fast, the step's name; the slider below, the card's whole width
    auto stepLayout = new QVBoxLayout(m_stepRow);
    stepLayout->setContentsMargins(0, 0, 0, 0);
    stepLayout->setSpacing(2);
    auto stepHead = new QHBoxLayout;
    stepHead->setSpacing(4);
    // three rows: the buttons (back, play, forward; how it runs, how fast), the step's name, the slider
    stepHead->addWidget(m_stepBack);
    stepHead->addWidget(m_play);
    stepHead->addWidget(m_stepForward);
    stepHead->addStretch();
    stepHead->addWidget(m_playMode);
    stepHead->addWidget(m_speed);
    stepLayout->addLayout(stepHead);
    stepLayout->addWidget(m_stepLabel);
    stepLayout->addWidget(m_step);
    m_note->setObjectName("resultNote");
    m_note->setWordWrap(true);
    m_info->setObjectName("resultElementInfo");
    m_info->setWordWrap(true);

    auto deformLayout = new QVBoxLayout(m_deformRow);
    deformLayout->setContentsMargins(0, 0, 0, 0);
    deformLayout->setSpacing(2);
    auto deformHead = new QHBoxLayout;
    deformHead->addWidget(new QLabel(T("Deformation")));
    deformHead->addStretch();
    deformHead->addWidget(m_scaleLabel);
    deformLayout->addLayout(deformHead);
    auto deformSlider = new QHBoxLayout;
    deformSlider->addWidget(m_deform, 1);
    deformSlider->addWidget(m_trueScale);
    deformLayout->addLayout(deformSlider);

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(5);
    layout->addWidget(m_fields);
    m_keys = new QLabel;
    m_keys->setObjectName("resultKeys");
    m_keys->setTextFormat(Qt::RichText);
    m_keys->setToolTip(T("The key numbers of the result.  Right-click its row in the model tree, Export, to write them as text (the arrow at the end of "
                         "the row lists more)."));
    layout->addWidget(m_keys);
    layout->addWidget(m_bar, 1);
    layout->addWidget(m_note);
    layout->addWidget(m_stepRow);
    layout->addWidget(m_deformRow);
    auto bottom = new QHBoxLayout;
    bottom->addWidget(m_elements);
    bottom->addWidget(m_flow);
    bottom->addStretch();
    layout->addLayout(bottom);
    {
        // (a flow is drawn translucent when the body in it is to be seen: the same slider as the field viewer's)
        m_opacityRow = new QWidget;
        m_opacity = new QSlider(Qt::Horizontal);
        m_opacity->setObjectName("resultOpacity");
        m_opacity->setRange(5, 100);
        m_opacity->setValue(100);
        m_opacity->setToolTip(T("How opaque the fluid is drawn: lower it to see the body in the flow"));
        auto row = new QHBoxLayout(m_opacityRow);
        row->setContentsMargins(0, 0, 0, 0);
        row->addWidget(new QLabel(T("Opacity")));
        row->addWidget(m_opacity, 1);
        m_opacityRow->hide();
        layout->addWidget(m_opacityRow);
        connect(m_opacity, &QSlider::valueChanged, this, [this](int v) {
            if (!m_updating) emit(opacityChanged(v / 100.0f));
        });
    }
    layout->addWidget(m_info);
    {
        // The residual of the solve after every iteration, as a curve on a log scale (the tester of version 1.2.0: "residual plot missing"); only a static analysis has one
        m_residualRow = new QWidget;
        m_residualGraph = new ResidualGraph;
        auto title = new QLabel(T("Residual"));
        title->setToolTip(T("How far the solver was from balancing the loads, as a share of the load, after each iteration, on a log scale. It falls until it is below the tolerance, and then the solve stops."));
        auto column = new QVBoxLayout(m_residualRow);
        column->setContentsMargins(0, 0, 0, 0);
        column->setSpacing(2);
        column->addWidget(title);
        column->addWidget(m_residualGraph);
        m_residualRow->setToolTip(title->toolTip());
        m_residualRow->hide();
        layout->addWidget(m_residualRow);
    }
    {
        // What an optimisation made best at every iteration, as a curve from the first iteration to the last: it stays on the card after the run has ended, as the residual of a static analysis does
        // (the user, 2026-11-15: "Once the optimization is over the optimization curve disappears. That should be shows in the legend, just like the convergence is shown for the static analysis")
        m_objectiveRow = new QWidget;
        m_objectiveGraph = new ObjectiveGraph;
        m_objectiveTitle = new QLabel(T("Optimisation"));
        m_objectiveTitle->setToolTip(T("What the optimisation made best, at every iteration it made, from the first to the last. The curve falls (or rises, when the aim is the highest value) until the design stops changing. The numbers at the ends are the lowest and the highest value of the curve."));
        auto column = new QVBoxLayout(m_objectiveRow);
        column->setContentsMargins(0, 0, 0, 0);
        column->setSpacing(2);
        column->addWidget(m_objectiveTitle);
        column->addWidget(m_objectiveGraph);
        m_objectiveRow->setToolTip(m_objectiveTitle->toolTip());
        m_objectiveRow->hide();
        layout->addWidget(m_objectiveRow);
    }
    {   // (a design tool, not a certified solver: said on the card that shows the numbers, where they are read -- and where the checks of the program are)
        auto notice = new QLabel(T("Not validated for safety-critical use"));
        notice->setObjectName("resultDisclaimer");
        notice->setWordWrap(true);
        notice->setToolTip(T("FielDes is a design tool, not a certified solver. The program is checked against four known answers (the validation folder: a beam, a plate with a hole, "
                             "a channel flow, the conduction through a slab); compare any result with a hand calculation and a test before you rely on it."));
        layout->addWidget(notice);
    }
    setMinimumWidth(220);
    resize(240, 100);
    new CardController(this, "result", nullptr, QSize(220, 120));      // (dragged by any empty place, resized from its edges)

    connect(m_fields, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
        if (!m_updating && i >= 0) emit(channelChanged(i));
    });
    connect(m_bar, &ColorBar::rangeEdited, this, [this](float lo, float hi) {         // (H191: the ends typed over in the legend)
        if (!m_updating) emit(rangeEdited(lo, hi));
    });
    connect(m_bar, &ColorBar::rangeAuto, this, [this]() {
        if (!m_updating) emit(rangeAuto());
    });
    connect(m_deform, &QSlider::valueChanged, this, [this](int v) {
        if (m_updating) return;
        const float s = scaleFor(v, m_state.autoScale);
        updateScaleLabel(s);
        emit(deformChanged(s));
    });
    connect(m_trueScale, &QToolButton::clicked, this, [this]() {
        m_updating = true;
        m_deform->setValue(sliderFor(1.0f, m_state.autoScale));
        m_updating = false;
        updateScaleLabel(1.0f);
        emit(deformChanged(1.0f));
    });
    connect(m_elements, &QToolButton::toggled, this, [this](bool on) {
        if (!m_updating) emit(elementsToggled(on));
    });
    connect(m_flow, &QToolButton::toggled, this, [this](bool on) {
        if (!m_updating) emit(flowToggled(on));
    });
    connect(m_step, &QSlider::valueChanged, this, [this](int v) {
        if (!m_updating) emit(stepChanged(v));
    });
    connect(m_play, &QToolButton::toggled, this, [this](bool on) {
        m_play->setText(on ? QString("II") : QString(QChar(0x25B6)));
        if (!m_updating) emit(playToggled(on));
    });
    // one step either way: the slider moves, and says so
    connect(m_stepBack, &QToolButton::clicked, this, [this]() {
        if (m_step->value() > m_step->minimum()) m_step->setValue(m_step->value() - 1);
    });
    connect(m_stepForward, &QToolButton::clicked, this, [this]() {
        if (m_step->value() < m_step->maximum()) m_step->setValue(m_step->value() + 1);
    });
    connect(m_playMode, &QToolButton::clicked, this, [this]() {
        m_state.playMode = (m_state.playMode + 1) % 3;
        updatePlayButtons();
        emit(playModeChanged(m_state.playMode));
    });
    connect(m_speed, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
        if (m_updating || i < 0) return;
        m_state.playSpeed = m_speed->itemData(i).toFloat();
        emit(playSpeedChanged(m_state.playSpeed));
    });
}

void ResultPanel::updatePlayButtons()
{
    // round and round, back and forth, once
    static const ushort glyphs[3] = {0x21BB, 0x21C4, 0x2192};
    m_playMode->setText(QString(QChar(glyphs[std::max(0, std::min(2, m_state.playMode))])));
    int best = 3;
    for (int i = 0; i < m_speed->count(); ++i)
        if (std::abs(m_speed->itemData(i).toFloat() - m_state.playSpeed) < 1e-3f) best = i;
    const bool was = m_updating;
    m_updating = true;
    m_speed->setCurrentIndex(best);
    m_updating = was;
}

float ResultPanel::scaleFor(int slider, float autoScale)
{
    const float t = slider / 50.0f;
    return autoScale * t * t;
}

int ResultPanel::sliderFor(float scale, float autoScale)
{
    if (!(autoScale > 0) || !(scale > 0)) return 0;
    return std::max(0, std::min(100, int(std::lround(50.0 * std::sqrt(scale / autoScale)))));
}

void ResultPanel::updateScaleLabel(float s)
{
    m_scaleLabel->setText(s == 0 ? T("off")
                                 : QString::fromUtf8("×") + QString::number(s, 'g', 3));
}

QRect ResultPanel::endLabelGlobal(bool high) const
{
    const QRect r = m_bar->endLabel(high);
    return QRect(m_bar->mapToGlobal(r.topLeft()), r.size());
}

int ResultPanel::labelsWidth() const
{
    QFont bold = m_fields->font();
    bold.setPointSize(9);
    bold.setBold(true);
    const QFontMetrics metrics(bold);
    int width = metrics.horizontalAdvance(m_fields->currentText()) + 64;     // (the card's margins, the combo's padding and its arrow)
    if (m_keys && m_keys->isVisibleTo(this)) width = std::max(width, m_keys->sizeHint().width() + 28);        // (and the key numbers, whole)
    return width;
}

void ResultPanel::setState(const State& s)
{
    m_updating = true;
    m_state = s;
    if (m_fields->count() != s.labels.size() ||
        (m_fields->count() && m_fields->itemText(0) != s.labels.value(0)))
    {
        m_fields->clear();
        m_fields->addItems(s.labels);
    }
    m_fields->setCurrentIndex(s.current);
    m_fields->setToolTip(s.noteHelp.isEmpty() ? T("Field shown") : T("Field shown") + QString("\n") + s.noteHelp);        // (what the values are, and for a principal stress why it can be below zero)
    m_bar->setSupports(s.supports);
    m_bar->setRange(s.lo, s.hi, s.map);
    m_bar->setManual(s.rangeManual);
    m_deformRow->setVisible(s.hasDeform);
    m_deform->setValue(sliderFor(s.scale, s.autoScale));
    updateScaleLabel(s.scale);
    // (H186, the user, 2026-11-12: "the element button should also still remain there, just not be greyed out [sic], instead of disappearing completely": a result with more elements than can be
    // drawn keeps the button, greyed out, and its tooltip says why)
    m_elements->setVisible(s.hasElements);
    m_elements->setEnabled(s.hasElements);
    m_elements->setToolTip(T("Show the solver's elements, each with its own value"));
    m_elements->setChecked(s.hasElements && s.showElements);
    m_flow->setVisible(s.hasFlow);
    m_flow->setChecked(s.showFlow);
    m_opacityRow->setVisible(s.hasFlow);
    m_opacity->setValue(int(std::lround(s.opacity * 100.0f)));
    m_stepRow->setVisible(s.steps > 0);
    m_step->setRange(0, std::max(0, s.steps - 1));
    m_step->setValue(s.step);
    m_stepLabel->setText(s.stepLabel);
    m_play->setChecked(s.playing);
    m_play->setText(s.playing ? QString("II") : QString(QChar(0x25B6)));
    updatePlayButtons();
    m_note->setText(s.note);
    m_note->setToolTip(s.noteHelp);
    m_note->setVisible(!s.note.isEmpty());
    {
        QString html;
        if (!s.keyResults.isEmpty())
        {
            html = "<table cellspacing='0' cellpadding='1'>";
            for (const QStringList& r : s.keyResults)
            {
                if (r.size() < 3) continue;
                html += "<tr><td style='color:#93a1a1'>" + r[0].toHtmlEscaped() + "&nbsp;&nbsp;</td>"
                        "<td align='right' style='color:#eee8d5; font-weight:bold'>" + r[1].toHtmlEscaped() + "</td>"
                        "<td style='color:#93a1a1'>&nbsp;" + r[2].toHtmlEscaped() + "</td></tr>";
            }
            html += "</table>";
        }
        m_keys->setText(html);
        m_keys->setVisible(!html.isEmpty());
    }
    m_info->setText(s.elementInfo);
    m_info->setVisible(!s.elementInfo.isEmpty());
    m_residualGraph->setHistory(s.residuals);
    m_residualRow->setVisible(s.residuals.size() > 1);
    m_objectiveGraph->setHistory(s.objective);
    if (s.objective.size() > 1)
    {
        // (what was made best, and how far it went from the first iteration to the last)
        const double first = double(s.objective.front()), last = double(s.objective.back());
        const QString change = first != 0 ? QString::number(100.0 * (last - first) / std::abs(first), 'f', 1) + " %" : QString();
        QString title = T("Optimisation");
        if (!s.objectiveLabel.isEmpty()) title += ": " + s.objectiveLabel;
        if (!change.isEmpty()) title += "  (" + (last >= first ? QString("+") : QString()) + change + ")";
        m_objectiveTitle->setText(title);
    }
    m_objectiveRow->setVisible(s.objective.size() > 1);
    m_updating = false;
    adjustSize();
}

}   // namespace FielDes
