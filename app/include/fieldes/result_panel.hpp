/*
FielDes: field-driven design
Derived from Studio, a simple GUI for the libfive CAD kernel

The card that controls how an analysis result is shown on the model: which
field colours it, its colour bar, how much the displacements are
magnified, and whether the analysis elements are drawn.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/
#pragma once

#include <QFrame>
#include <QStringList>

class QComboBox;
class QLabel;
class QSlider;
class QToolButton;

namespace FielDes {

/*  A vertical colour bar with tick labels  */
class ColorBar : public QWidget
{
    Q_OBJECT
public:
    ColorBar(QWidget* parent=nullptr);
    void setRange(float lo, float hi, const QString& map);
    QSize sizeHint() const override { return QSize(150, 150); }
protected:
    void paintEvent(QPaintEvent* e) override;
    float m_lo = 0, m_hi = 1;
    QString m_map = "turbo";
};

class ResultPanel : public QFrame
{
    Q_OBJECT
public:
    ResultPanel(QWidget* parent=nullptr);

    /*  What to show (from the result's model)  */
    struct State
    {
        QStringList labels;          // one per field
        int current = 0;
        float lo = 0, hi = 1;        // the current field's colour range
        QString map = "turbo";
        bool hasDeform = false;
        float scale = 0, autoScale = 1;
        bool hasElements = false;
        bool showElements = false;
        QString note;                // what the shown values are (a few words)
        QString noteHelp;            // ... and why (the note's tooltip)
        QString elementInfo;         // what the elements are, and how many
    };
    void setState(const State& s);

    /*  Magnification <-> slider position (quadratic: fine control near
     *  zero; the middle is the automatic scale, the end four times it)  */
    static float scaleFor(int slider, float autoScale);
    static int sliderFor(float scale, float autoScale);

signals:
    void channelChanged(int index);
    void deformChanged(float scale);
    void elementsToggled(bool on);

protected:
    void updateScaleLabel(float scale);

    QComboBox* m_fields;
    ColorBar* m_bar;
    QWidget* m_deformRow;
    QSlider* m_deform;
    QLabel* m_scaleLabel;
    QToolButton* m_trueScale;
    QToolButton* m_elements;
    QLabel* m_note;
    QLabel* m_info;
    State m_state;
    bool m_updating = false;
};

}   // namespace FielDes
