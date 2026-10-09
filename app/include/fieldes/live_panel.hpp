/*
FielDes: field-driven design
Derived from Studio, a simple GUI for the libfive CAD kernel

The card that says how an optimisation is doing while it runs: the iteration it is at, the objective (the compliance, the drag) and how far
it has fallen from the first iteration, the material kept against what was asked for, how much the design still changes, and the objective of
every iteration so far as a curve.  A card like the others over the viewport: dragged by any empty place, resized from its edges (see
CardController).

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/
#pragma once

#include <vector>

#include <QFrame>

#include "libfive/run_progress.hpp"

class QLabel;

namespace FielDes {

/*  The curve of the objective of every iteration  */
class LiveGraph : public QWidget
{
public:
    LiveGraph(QWidget* parent=nullptr);
    void setHistory(std::vector<float> history, int iterations);
    QSize sizeHint() const override { return QSize(260, 60); }
    QSize minimumSizeHint() const override { return QSize(120, 36); }

protected:
    void paintEvent(QPaintEvent* e) override;

private:
    std::vector<float> m_history;
    int m_iterations = 2;
};

class LivePanel : public QFrame
{
public:
    LivePanel(QWidget* parent=nullptr);

    /*  What the optimisation looks like now  */
    void setView(const libfive::run_progress::LiveView& view);

    /*  The width the text of the card needs  */
    int textWidth() const;

private:
    QLabel* m_first;
    QLabel* m_second;
    QLabel* m_third;
    LiveGraph* m_graph;
};

}   // namespace FielDes
