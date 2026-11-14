/*
FielDes: field-driven design
Derived from Studio, a simple GUI for the libfive CAD kernel

The card that controls how an analysis result is shown on the model: which
field colours it, its colour bar, the step shown (a slider with play /
pause: a flow in time, the load growing, the iterations of an
optimisation), how much the displacements are magnified, and whether the
analysis elements, or the streamlines of a flow, are drawn.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/
#pragma once

#include <vector>

#include <QFrame>
#include <QStringList>

class QComboBox;
class QLabel;
class QLineEdit;
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
    // The geometry change of a shell has the supports of a print as one more class (amber), when the scene has them
    void setSupports(bool on);
    // H191 (the user, 2026-11-13: "Legend bounds should be adjustable in the legend"): the lowest and the highest number of the scale are typed over by a click on them; `manual` says the
    // ends are the ones typed (an "Auto" is drawn that gives the values' own ends back)
    void setManual(bool manual);
    /*  The rectangle of the lowest or the highest number, in the bar's own coordinates (for the scripted tests)  */
    QRect endLabel(bool high) const { return tickLabel(high ? 4 : 0); }
    QSize sizeHint() const override { return QSize(150, 150); }
signals:
    void rangeEdited(float lo, float hi);       // (the ends typed, in the unit the numbers are shown in)
    void rangeAuto();                           // (back to the ends the values have)
protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent* e) override;
    bool eventFilter(QObject* o, QEvent* e) override;
    bool scaleShown() const { return m_map != "change" && m_map != "violation"; }
    QRect barRect() const;
    QRect tickLabel(int k) const;               // (the rectangle of the number at tick k: 0 the lowest, 4 the highest)
    QRect autoPill() const;
    void startEdit(bool high);
    void commitEdit();
    float m_lo = 0, m_hi = 1;
    QString m_map = "turbo";
    bool m_supports = false;
    bool m_manual = false;
    bool m_editHigh = false;
    QLineEdit* m_edit = nullptr;
};

/*  The residual of a solve after every iteration, relative to the load, on a log scale: the curve of the convergence (the tester of version 1.2.0: "residual plot missing")  */
class ResidualGraph : public QWidget
{
public:
    ResidualGraph(QWidget* parent=nullptr);
    void setHistory(const std::vector<float>& history);
    QSize sizeHint() const override { return QSize(200, 88); }
    QSize minimumSizeHint() const override { return QSize(120, 64); }
protected:
    void paintEvent(QPaintEvent* e) override;
private:
    std::vector<float> m_history;
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
        bool rangeManual = false;    // the ends were typed in the legend (H191)
        QString map = "turbo";
        bool hasDeform = false;
        float scale = 0, autoScale = 1;
        bool hasElements = false;
        bool showElements = false;
        QString note;                // what the shown values are (a few words)
        QString noteHelp;            // ... and why (the note's tooltip)
        QString elementInfo;         // what the elements are, and how many
        int steps = 0, step = 0;     // the steps of the result (0: none), and the one shown
        QString stepLabel;           // what the shown step is ("t = 0.25 s", "load 35 %", "iteration 12 of 60")
        bool playing = false;
        int playMode = 0;            // 0 round and round, 1 back and forth, 2 once to the end
        float playSpeed = 1;         // 1 = ten steps a second
        bool hasFlow = false;        // a flow with streamlines...
        bool showFlow = true;        // ...drawn over it
        float opacity = 1;           // how opaque the flow is drawn (a lower one shows the body in it)
        QList<QStringList> keyResults;   // the key numbers of the result: label, text, unit of each
        std::vector<float> residuals;    // a static analysis: the residual of its solve after every iteration (a curve under the key numbers); none: no curve
        bool supports = false;           // the scene has the supports of a print (a body of its own, amber): one more class of the legend, and no card of its own
    };
    void setState(const State& s);
    // The lowest or the highest number of the colour scale, as a rectangle of the screen (for the scripted tests)
    QRect endLabelGlobal(bool high) const;
    // The width the name of the field shown needs, so that a long one (in another language) is not cut off at the card's usual width
    int labelsWidth() const;

    /*  Magnification <-> slider position (quadratic: fine control near
     *  zero; the middle is the automatic scale, the end four times it)  */
    static float scaleFor(int slider, float autoScale);
    static int sliderFor(float scale, float autoScale);

signals:
    void channelChanged(int index);
    void rangeEdited(float lo, float hi);       // (H191: the ends typed in the legend, in the unit shown)
    void rangeAuto();
    void deformChanged(float scale);
    void elementsToggled(bool on);
    void stepChanged(int index);
    void playToggled(bool on);
    void playModeChanged(int mode);
    void playSpeedChanged(float speed);
    void flowToggled(bool on);
    void opacityChanged(float opacity);

protected:
    void updateScaleLabel(float scale);
    void updatePlayButtons();

    QComboBox* m_fields;
    ColorBar* m_bar;
    QWidget* m_deformRow;
    QSlider* m_deform;
    QLabel* m_scaleLabel;
    QToolButton* m_trueScale;
    QToolButton* m_elements;
    QToolButton* m_flow;
    QWidget* m_opacityRow = nullptr;
    QSlider* m_opacity = nullptr;
    QWidget* m_stepRow;
    QToolButton* m_play;
    QToolButton* m_stepBack;
    QToolButton* m_stepForward;
    QToolButton* m_playMode;
    QComboBox* m_speed;
    QSlider* m_step;
    QLabel* m_stepLabel;
    QLabel* m_note;
    QLabel* m_info;
    QLabel* m_keys = nullptr;
    QWidget* m_residualRow = nullptr;
    ResidualGraph* m_residualGraph = nullptr;
    State m_state;
    bool m_updating = false;
};

}   // namespace FielDes
