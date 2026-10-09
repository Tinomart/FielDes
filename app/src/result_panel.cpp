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

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
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
}

void ColorBar::setRange(float lo, float hi, const QString& map)
{
    m_lo = lo;
    m_hi = hi;
    m_map = map;
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
    const QRect bar(4, fm.height() / 2 + 2, 14, height() - fm.height() - 4);
    QLinearGradient g(bar.bottomLeft(), bar.topLeft());
    for (int k = 0; k <= 16; k++) g.setColorAt(k / 16.0, colormapColor(m_map, k / 16.0f));
    p.setBrush(g);
    p.setPen(QPen(QColor(0x93, 0xa1, 0xa1), 1));
    p.drawRect(bar);
    p.setPen(QColor(0xee, 0xe8, 0xd5));
    const int ticks = 5;
    for (int k = 0; k < ticks; k++)
    {
        const int y = bar.bottom() - (bar.height() * k) / (ticks - 1);
        const float v = m_lo + (m_hi - m_lo) * k / (ticks - 1);
        p.drawLine(bar.right(), y, bar.right() + 4, y);
        p.drawText(QRect(bar.right() + 8, y - fm.height() / 2, width() - bar.right() - 8, fm.height()),
                   Qt::AlignLeft | Qt::AlignVCenter, QString::number(v, 'g', 4));
    }
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
    setMinimumWidth(220);
    resize(240, 100);
    new CardController(this, "result", nullptr, QSize(220, 120));      // (dragged by any empty place, resized from its edges)

    connect(m_fields, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
        if (!m_updating && i >= 0) emit(channelChanged(i));
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

int ResultPanel::labelsWidth() const
{
    QFont bold = m_fields->font();
    bold.setPointSize(9);
    bold.setBold(true);
    const QFontMetrics metrics(bold);
    return metrics.horizontalAdvance(m_fields->currentText()) + 64;         // (the card's margins, the combo's padding and its arrow)
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
    m_bar->setRange(s.lo, s.hi, s.map);
    m_deformRow->setVisible(s.hasDeform);
    m_deform->setValue(sliderFor(s.scale, s.autoScale));
    updateScaleLabel(s.scale);
    m_elements->setVisible(s.hasElements);
    m_elements->setChecked(s.showElements);
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
    m_info->setText(s.elementInfo);
    m_info->setVisible(!s.elementInfo.isEmpty());
    m_updating = false;
    adjustSize();
}

}   // namespace FielDes
