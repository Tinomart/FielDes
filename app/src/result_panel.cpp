/*
FielDes: field-driven design
Derived from Studio, a simple GUI for the libfive CAD kernel

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/
#include <cmath>

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

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
      m_trueScale(new QToolButton), m_elements(new QToolButton), m_note(new QLabel), m_info(new QLabel)
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
    m_fields->setToolTip("Field shown");
    m_deform->setObjectName("resultDeform");
    m_deform->setRange(0, 100);
    m_deform->setToolTip("Deformation magnification");
    m_trueScale->setObjectName("resultTrueScale");
    m_trueScale->setText("1:1");
    m_trueScale->setToolTip("True size");
    m_elements->setObjectName("resultElements");
    m_elements->setCheckable(true);
    m_elements->setText("Elements");
    m_elements->setToolTip("Show the solver's elements, each with its own value");
    m_note->setObjectName("resultNote");
    m_note->setWordWrap(true);
    m_info->setObjectName("resultElementInfo");
    m_info->setWordWrap(true);

    auto deformLayout = new QVBoxLayout(m_deformRow);
    deformLayout->setContentsMargins(0, 0, 0, 0);
    deformLayout->setSpacing(2);
    auto deformHead = new QHBoxLayout;
    deformHead->addWidget(new QLabel("Deformation"));
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
    layout->addWidget(m_deformRow);
    auto bottom = new QHBoxLayout;
    bottom->addWidget(m_elements);
    bottom->addStretch();
    layout->addLayout(bottom);
    layout->addWidget(m_info);
    setFixedWidth(240);

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
    m_scaleLabel->setText(s == 0 ? QString("off")
                                 : QString::fromUtf8("×") + QString::number(s, 'g', 3));
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
    m_note->setText(s.note);
    m_note->setToolTip(s.noteHelp);
    m_note->setVisible(!s.note.isEmpty());
    m_info->setText(s.elementInfo);
    m_info->setVisible(!s.elementInfo.isEmpty());
    m_updating = false;
    adjustSize();
}

}   // namespace FielDes
