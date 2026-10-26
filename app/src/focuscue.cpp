/*
FielDes: field-driven design

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/
#include "fieldes/focuscue.hpp"
#include "fieldes/popout.hpp"

#include <QApplication>
#include <QEvent>
#include <QPainter>

namespace FielDes {

FocusCue::FocusCue(QWidget* window)
    : QWidget(window)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setFocusPolicy(Qt::NoFocus);
    setGeometry(window->rect());
    window->installEventFilter(this);                      // (it follows the window's size)
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget*, QWidget* now) { focusMoved(now); });
    show();
    raise();
}

void FocusCue::addPane(QWidget* pane, const QString& name)
{
    m_panes.push_back({pane, name});
    pane->installEventFilter(this);                        // (a pane that is resized or moved -- the splitter -- takes its frame with it)
    focusMoved(QApplication::focusWidget());
}

QString FocusCue::focused() const
{
    return m_current >= 0 && m_current < m_panes.size() ? m_panes[m_current].name : QString();
}

void FocusCue::focusMoved(QWidget* now)
{
    int found = -1;
    for (int i = 0; now && i < m_panes.size(); ++i)
    {
        const QWidget* pane = m_panes[i].widget;
        if (pane && (pane == now || pane->isAncestorOf(now))) found = i;
    }
    if (found == m_current) return;
    m_current = found;
    update();
    emit focusedChanged(focused());
}

bool FocusCue::eventFilter(QObject* watched, QEvent* event)
{
    switch (event->type())
    {
    case QEvent::Resize:
        if (watched == parent()) setGeometry(static_cast<QWidget*>(parent())->rect());
        update();
        break;
    case QEvent::Move:
    case QEvent::Show:
    case QEvent::Hide:
        update();
        break;
    default:
        break;
    }
    return false;
}

void FocusCue::paintEvent(QPaintEvent*)
{
    if (m_current < 0 || m_current >= m_panes.size() || !m_panes[m_current].widget) return;
    const QWidget* pane = m_panes[m_current].widget;
    if (!pane->isVisible() || PopOut::isFloating(pane)) return;               // (a pane in a window of its own has no place in this one)
    const QRect r(pane->mapTo(parentWidget(), QPoint(0, 0)), pane->size());
    QPainter painter(this);
    // (two pixels, inside the pane: a colour that stands out on the light editor and on the dark viewport alike, and that the tour's yellow and green are not)
    painter.setPen(QPen(QColor(53, 167, 216, 235), 2));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(r.adjusted(1, 1, -1, -1));
}

}   // namespace FielDes
