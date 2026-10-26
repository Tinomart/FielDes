/*
FielDes: field-driven design

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/
#include <algorithm>
#include <cmath>

#include <QAction>
#include <QApplication>
#include <QEvent>
#include <QFrame>
#include <QIcon>
#include <QLabel>
#include <QLayout>
#include <QPainter>
#include <QPixmap>
#include <QPolygonF>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "fieldes/i18n.hpp"
#include "fieldes/popout.hpp"
#include "fieldes/theme.hpp"

namespace FielDes {

namespace {

QList<QPointer<PopOut>>& registry()
{
    static QList<QPointer<PopOut>> list;
    return list;
}

// (a scripted test never changes the user's settings)
bool automated() { return qEnvironmentVariableIsSet("FIELDES_AUTOMATION"); }

// The glyph of the button: a window with an arrow that leaves it (pull out) or goes back into it (put back), drawn in `ink`
QIcon glyph(bool floating, const QColor& ink)
{
    const qreal ratio = 2.0;
    QPixmap pm(int(14 * ratio), int(14 * ratio));
    pm.setDevicePixelRatio(ratio);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(ink, 1.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    // the window, open at the top right
    p.drawPolyline(QPolygonF({QPointF(6, 2.5), QPointF(2.5, 2.5), QPointF(2.5, 11.5), QPointF(11.5, 11.5), QPointF(11.5, 8)}));
    // the arrow: toward the top right (out), or from there into the window (back)
    const QPointF outer(11.5, 2.5), inner(6.5, 7.5);
    const QPointF tail = floating ? outer : inner, tip = floating ? inner : outer;
    p.drawLine(tail, tip);
    const QPointF dir = (tip - tail) / std::hypot(tip.x() - tail.x(), tip.y() - tail.y());
    const QPointF side(-dir.y(), dir.x());
    const QPointF back = tip - dir * 3.2;
    p.drawLine(tip, back + side * 2.0);
    p.drawLine(tip, back - side * 2.0);
    p.end();
    return QIcon(pm);
}

}   // namespace

PopOut* PopOut::attach(QWidget* target, const QString& title, const QString& key, QWidget* nextTo)
{
    return new PopOut(target, title, key, nextTo);
}

PopOut::PopOut(QWidget* target, const QString& title, const QString& key, QWidget* nextTo)
    : QObject(target), m_target(target), m_title(title), m_key(key), m_nextTo(nextTo)
{
    m_button = new QToolButton(target);
    m_button->setObjectName("PopOutButton");
    m_button->setFixedSize(20, 18);
    m_button->setIconSize(QSize(14, 14));
    m_button->setFocusPolicy(Qt::NoFocus);
    m_button->setCursor(Qt::ArrowCursor);
    // (a way out of a pane is not something to look at: no fill and no frame at rest -- the icon alone, quiet -- and a faint plate behind it under the mouse; see refreshButton for the ink)
    m_button->setStyleSheet(QString("QToolButton#PopOutButton { background: transparent; border: none; border-radius: 3px; padding: 0; }"
                                    "QToolButton#PopOutButton:hover { background: %1; }").arg(onPaper() ? "rgba(128, 128, 128, 48)" : "rgba(255, 255, 255, 40)"));
    connect(m_button, &QToolButton::clicked, this, [this] { toggle(); });
    m_button->installEventFilter(this);                      // (the ink comes forward under the mouse)
    target->installEventFilter(this);
    refreshButton();
    placeButton();
    m_button->show();
    registry().append(this);
}

PopOut* PopOut::find(const QString& key)
{
    for (const auto& p : registry())
        if (p && p->m_key == key) return p;
    return nullptr;
}

QStringList PopOut::keys()
{
    QStringList out;
    for (const auto& p : registry())
        if (p) out << p->m_key;
    return out;
}

QStringList PopOut::floatingKeys()
{
    QStringList out;
    for (const auto& p : registry())
        if (p && p->floating()) out << p->m_key;
    return out;
}

bool PopOut::isFloating(const QWidget* w)
{
    return w && w->property("floating").toBool();
}

void PopOut::dockAll(bool remember)
{
    // (the list is copied: docking changes nothing in it, but the habit is a safe one)
    const auto all = registry();
    for (const auto& p : all)
    {
        if (!p || !p->floating()) continue;
        if (remember && !automated()) QSettings().setValue("popout/" + p->m_key + "/floating", true);
        p->dock();
    }
}

void PopOut::restoreFloating()
{
    if (automated()) return;
    QSettings s;
    const auto all = registry();
    for (const auto& p : all)
    {
        if (!p) continue;
        const QString flag = "popout/" + p->m_key + "/floating";
        if (!s.value(flag).toBool()) continue;
        s.remove(flag);                  // (cleared as it is read: a start that crashes with one pulled out is not repeated)
        p->floatOut();
    }
}

void PopOut::toggle()
{
    if (floating()) dock();
    else floatOut();
}

// The elements that stand on paper -- the code editor and the console -- are light fields in the light theme: the button is the colour of the secondary text there, the colour of the text itself
// under the mouse.  The others are dark cards over the viewport: a pale grey, a pale cream under the mouse
bool PopOut::onPaper() const
{
    return m_key == "editor" || m_key == "terminal";
}

void PopOut::refreshButton()
{
    QColor ink;
    if (onPaper()) ink = m_hover ? Theme::text : Theme::muted;
    else ink = m_hover ? QColor(238, 232, 213) : QColor(158, 171, 171);
    m_button->setIcon(glyph(floating(), ink));
    m_button->setToolTip(floating() ? T("Put %1 back where it was").arg(m_title) : T("Pull %1 out into a window of its own").arg(m_title));
}

void PopOut::placeButton()
{
    int x = m_target->width() - m_button->width() - 4, y = 4;
    if (m_nextTo)
    {
        const QPoint at = m_nextTo->mapTo(m_target, QPoint(0, 0));
        x = at.x() - m_button->width() - 3;
        y = at.y() + (m_nextTo->height() - m_button->height()) / 2;
    }
    m_button->move(std::max(0, x), std::max(0, y));
    m_button->raise();
}

QWidget* PopOut::makePlaceholder()
{
    auto frame = new QFrame(m_parent);
    frame->setObjectName("PopOutPlaceholder");
    frame->setFrameShape(QFrame::StyledPanel);
    frame->setMinimumSize(140, 80);
    auto layout = new QVBoxLayout(frame);
    auto label = new QLabel(T("%1 is in a window of its own.").arg(m_title), frame);
    label->setAlignment(Qt::AlignCenter);
    label->setWordWrap(true);
    auto back = new QPushButton(T("Put it back"), frame);
    back->setObjectName("PopOutBack");
    layout->addStretch(1);
    layout->addWidget(label);
    layout->addWidget(back, 0, Qt::AlignHCenter);
    layout->addStretch(1);
    connect(back, &QPushButton::clicked, this, [this] { dock(); });
    return frame;
}

void PopOut::saveWindowGeometry() const
{
    if (!m_window || automated()) return;
    QSettings().setValue("popout/" + m_key + "/geometry", m_window->saveGeometry());
}

void PopOut::floatOut()
{
    if (floating() || !m_target->parentWidget()) return;
    QWidget* top = m_target->window();
    m_parent = m_target->parentWidget();
    m_geometry = m_target->geometry();
    const QSize size = m_target->size();
    const QPoint where = m_target->mapToGlobal(QPoint(24, 24));
    m_wantVisible = !m_target->isHidden();
    m_kind = 0;
    m_window = new QWidget(top, Qt::Window);
    m_window->setObjectName("PopOutWindow_" + m_key);
    m_window->setWindowTitle(m_title);
    m_window->setWindowIcon(top->windowIcon());
    auto layout = new QVBoxLayout(m_window);
    layout->setContentsMargins(0, 0, 0, 0);
    if (auto splitter = qobject_cast<QSplitter*>(m_parent))
    {
        m_kind = 1;
        m_index = splitter->indexOf(m_target);
        m_sizes = splitter->sizes();
        m_placeholder = makePlaceholder();
        m_target->setParent(m_window);                    // (it leaves the splitter)
        splitter->insertWidget(m_index, m_placeholder);
        if (m_sizes.size() == splitter->count()) splitter->setSizes(m_sizes);
    }
    else if (m_parent->layout())
    {
        m_placeholder = makePlaceholder();
        if (QLayoutItem* item = m_parent->layout()->replaceWidget(m_target, m_placeholder))
        {
            delete item;
            m_kind = 2;
        }
        else
        {
            delete m_placeholder;                         // (it is not in the layout: a card placed by hand)
        }
        m_target->setParent(m_window);
    }
    else
    {
        m_target->setParent(m_window);
    }
    layout->addWidget(m_target);
    m_target->setProperty("floating", true);
    // The keys: what works anywhere in the main window works here, and what worked where the element came from (the shortcuts of the viewport, for a card of it)
    for (QAction* a : top->findChildren<QAction*>())
        if (a->shortcutContext() == Qt::WindowShortcut) m_window->addAction(a);
    for (QWidget* w = m_parent; w && w != top; w = w->parentWidget())
        m_window->addActions(w->actions());
    m_window->installEventFilter(this);
    refreshButton();
    m_window->resize(std::max(size.width(), 260), std::max(size.height(), 160));
    bool restored = false;
    if (!automated())
    {
        const QByteArray saved = QSettings().value("popout/" + m_key + "/geometry").toByteArray();
        if (!saved.isEmpty()) restored = m_window->restoreGeometry(saved);
    }
    if (!restored) m_window->move(where);
    if (m_wantVisible)
    {
        m_target->show();
        m_window->show();
    }
    placeButton();
}

void PopOut::dock()
{
    if (!floating()) return;
    QWidget* window = m_window;
    const bool wantVisible = !m_target->isHidden();
    saveWindowGeometry();
    m_target->setProperty("floating", false);
    m_window = nullptr;                      // (from here on the element is not out: its Show and Hide, as it is moved back, must not touch the window that goes)
    window->hide();
    window->layout()->removeWidget(m_target);
    QWidget* parent = m_parent;
    if (parent)
    {
        auto splitter = m_kind == 1 ? qobject_cast<QSplitter*>(parent) : nullptr;
        if (splitter)
        {
            int at = m_placeholder ? splitter->indexOf(m_placeholder) : -1;
            if (at < 0) at = m_index;
            if (m_placeholder)
            {
                m_placeholder->hide();
                m_placeholder->setParent(nullptr);
                m_placeholder->deleteLater();
            }
            splitter->insertWidget(std::min(at, splitter->count()), m_target);
            if (m_sizes.size() == splitter->count()) splitter->setSizes(m_sizes);
        }
        else if (m_kind == 2 && m_placeholder && parent->layout())
        {
            if (QLayoutItem* item = parent->layout()->replaceWidget(m_placeholder, m_target)) delete item;
            m_placeholder->deleteLater();
        }
        else
        {
            m_target->setParent(parent);
            m_target->setGeometry(m_geometry);
        }
    }
    if (wantVisible) m_target->show();
    window->deleteLater();
    refreshButton();
    placeButton();
}

bool PopOut::eventFilter(QObject* obj, QEvent* e)
{
    if (obj == m_button)
    {
        if (e->type() == QEvent::Enter || e->type() == QEvent::Leave)
        {
            m_hover = e->type() == QEvent::Enter;
            refreshButton();
        }
        return false;
    }
    if (obj == m_target)
    {
        switch (e->type())
        {
            case QEvent::PaletteChange:
            case QEvent::ApplicationPaletteChange:
            case QEvent::StyleChange:
                refreshButton();                         // (the theme was changed: the ink follows it)
                break;
            case QEvent::Resize:
            case QEvent::LayoutRequest:
                placeButton();
                break;
            case QEvent::Show:
                placeButton();
                if (floating() && !m_window->isVisible()) m_window->show();          // (the element is shown by the program: its window with it)
                break;
            case QEvent::Hide:
                if (floating() && m_target->isHidden()) m_window->hide();           // (and hidden by it)
                break;
            default:
                break;
        }
        return false;
    }
    if (obj == m_window && e->type() == QEvent::Close)
    {
        // (a window that is closed puts its element back: there is nothing to close but the window)
        e->ignore();
        QTimer::singleShot(0, this, [this] { dock(); });
        return true;
    }
    return false;
}

}   // namespace FielDes
