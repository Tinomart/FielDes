/*
FielDes: field-driven design

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/
#include <algorithm>

#include <QAbstractButton>
#include <QEvent>
#include <QMouseEvent>
#include <QSettings>
#include <QWidget>

#include "fieldes/carddrag.hpp"

namespace FielDes {

namespace {
const int kEdge = 6;                // (the margin of a card that resizes it)
}

CardController::CardController(QWidget* card, const QString& key, QWidget* handle, QSize minimum)
    : QObject(card), m_card(card), m_handle(handle), m_key(key), m_min(minimum)
{
    card->setMouseTracking(true);
    card->installEventFilter(this);
    if (handle) handle->installEventFilter(this);
    if (card->parentWidget()) card->parentWidget()->installEventFilter(this);
}

bool CardController::moved(const QWidget* card) { return card->property("userMoved").toBool(); }
bool CardController::sized(const QWidget* card) { return card->property("userSized").toBool(); }

void CardController::collapse(QWidget* card, bool collapsed, int collapsedHeight)
{
    if (!sized(card)) return;                           // (a card that was never sized fits its contents by itself)
    if (collapsed)
    {
        card->setProperty("expandedHeight", card->height());
        card->resize(card->width(), collapsedHeight);
    }
    else
    {
        const int h = card->property("expandedHeight").toInt();
        if (h > 0) card->resize(card->width(), h);
    }
}

int CardController::edgeAt(const QPoint& pos) const
{
    int e = None;
    if (pos.x() < kEdge) e |= Left;
    if (pos.x() >= m_card->width() - kEdge) e |= Right;
    if (pos.y() < kEdge) e |= Top;
    if (pos.y() >= m_card->height() - kEdge) e |= Bottom;
    return e;
}

void CardController::updateCursor(int edges)
{
    Qt::CursorShape shape = Qt::ArrowCursor;
    if ((edges & Left && edges & Top) || (edges & Right && edges & Bottom)) shape = Qt::SizeFDiagCursor;
    else if ((edges & Right && edges & Top) || (edges & Left && edges & Bottom)) shape = Qt::SizeBDiagCursor;
    else if (edges & (Left | Right)) shape = Qt::SizeHorCursor;
    else if (edges & (Top | Bottom)) shape = Qt::SizeVerCursor;
    if (shape == Qt::ArrowCursor) m_card->unsetCursor();
    else m_card->setCursor(shape);
}

void CardController::drag(const QPoint& global)
{
    QWidget* parent = m_card->parentWidget();
    if (!parent) return;
    const QPoint d = global - m_press;
    const QRect room = parent->rect();
    if (m_mode == 1)
    {
        QRect r = m_start.translated(d);
        r.moveLeft(std::max(0, std::min(room.width() - r.width(), r.left())));
        r.moveTop(std::max(0, std::min(room.height() - r.height(), r.top())));
        m_card->move(r.topLeft());
    }
    else if (m_mode == 2)
    {
        QRect r = m_start;
        if (m_edges & Left) r.setLeft(std::min(r.left() + d.x(), r.right() - m_min.width() + 1));
        if (m_edges & Right) r.setRight(std::max(r.right() + d.x(), r.left() + m_min.width() - 1));
        if (m_edges & Top) r.setTop(std::min(r.top() + d.y(), r.bottom() - m_min.height() + 1));
        if (m_edges & Bottom) r.setBottom(std::max(r.bottom() + d.y(), r.top() + m_min.height() - 1));
        r = r.intersected(room);
        m_card->setGeometry(r);
    }
}

void CardController::remember()
{
    QWidget* parent = m_card->parentWidget();
    if (!parent) return;
    if (qEnvironmentVariableIsSet("FIELDES_AUTOMATION")) return;       // (a scripted test never changes the user's settings)
    const int fw = std::max(1, parent->width() - m_card->width()), fh = std::max(1, parent->height() - m_card->height());
    m_fx = std::max(0.0, std::min(1.0, double(m_card->x()) / fw));
    m_fy = std::max(0.0, std::min(1.0, double(m_card->y()) / fh));
    QSettings s;
    s.beginGroup("cards/" + m_key);
    s.setValue("fx", m_fx);
    s.setValue("fy", m_fy);
    s.setValue("sized", sized(m_card));
    if (sized(m_card))
    {
        s.setValue("w", m_card->width());
        s.setValue("h", m_card->height());
    }
    s.endGroup();
}

void CardController::finish()
{
    if (m_dragged)
    {
        m_card->setProperty("userMoved", true);
        if (m_mode == 2) m_card->setProperty("userSized", true);
        remember();
    }
    m_mode = 0;
    m_edges = 0;
    m_dragged = false;
    m_handlePressed = false;
}

// A card that the user placed keeps its place along the free space of the viewport when the viewport changes size
void CardController::layoutInParent()
{
    QWidget* parent = m_card->parentWidget();
    if (!parent || !moved(m_card)) return;
    int w = m_card->width(), h = m_card->height();
    w = std::min(w, parent->width());
    h = std::min(h, parent->height());
    const int x = int(m_fx * std::max(0, parent->width() - w) + 0.5), y = int(m_fy * std::max(0, parent->height() - h) + 0.5);
    m_card->setGeometry(x, y, w, h);
}

void CardController::restore()
{
    if (m_restored) return;
    m_restored = true;
    if (qEnvironmentVariableIsSet("FIELDES_AUTOMATION")) return;
    QSettings s;
    s.beginGroup("cards/" + m_key);
    if (!s.contains("fx")) return;
    m_fx = s.value("fx").toDouble();
    m_fy = s.value("fy").toDouble();
    if (s.value("sized").toBool())
    {
        m_card->resize(std::max(m_min.width(), s.value("w").toInt()), std::max(m_min.height(), s.value("h").toInt()));
        m_card->setProperty("userSized", true);
    }
    m_card->setProperty("userMoved", true);
    layoutInParent();
}

bool CardController::eventFilter(QObject* obj, QEvent* e)
{
    if (obj == m_card->parentWidget())
    {
        if (e->type() == QEvent::Resize) layoutInParent();
        return false;
    }
    if (obj == m_card && e->type() == QEvent::Show)
    {
        restore();
        return false;
    }
    switch (e->type())
    {
        case QEvent::MouseMove:
        {
            auto me = static_cast<QMouseEvent*>(e);
            if (m_mode)
            {
                if ((me->globalPos() - m_press).manhattanLength() >= 3) m_dragged = true;
                if (m_dragged) drag(me->globalPos());
                return true;
            }
            if (obj == m_handle && m_handlePressed && (me->buttons() & Qt::LeftButton) &&
                (me->globalPos() - m_press).manhattanLength() >= 4)
            {
                // (the header was pressed and pulled: it is a drag, not a click)
                m_mode = 1;
                m_start = m_card->geometry();
                m_dragged = true;
                if (auto b = qobject_cast<QAbstractButton*>(m_handle)) b->setDown(false);
                drag(me->globalPos());
                return true;
            }
            if (obj == m_card && !(me->buttons() & Qt::LeftButton)) updateCursor(edgeAt(me->pos()));
            return false;
        }
        case QEvent::MouseButtonPress:
        {
            auto me = static_cast<QMouseEvent*>(e);
            if (me->button() != Qt::LeftButton) return false;
            if (obj == m_handle)
            {
                m_handlePressed = true;
                m_press = me->globalPos();
                return false;
            }
            if (obj == m_card)
            {
                m_press = me->globalPos();
                m_start = m_card->geometry();
                m_edges = edgeAt(me->pos());
                m_mode = m_edges ? 2 : 1;
                m_dragged = false;
                return true;
            }
            return false;
        }
        case QEvent::MouseButtonRelease:
        {
            auto me = static_cast<QMouseEvent*>(e);
            if (me->button() != Qt::LeftButton) return false;
            const bool wasDrag = m_mode && m_dragged;
            if (m_mode || m_handlePressed) finish();
            return wasDrag;                 // (a click that was a drag is not a click on the header)
        }
        case QEvent::Leave:
            if (obj == m_card && !m_mode) m_card->unsetCursor();
            return false;
        default:
            return false;
    }
}

}   // namespace FielDes
