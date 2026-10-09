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
#include <functional>

#include <QApplication>
#include <QEasingCurve>
#include <QElapsedTimer>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPair>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QRegion>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QVBoxLayout>

#include "fieldes/i18n.hpp"
#include "fieldes/editor.hpp"
#include "fieldes/scenetree.hpp"
#include "fieldes/script.hpp"
#include "fieldes/tutorial.hpp"
#include "fieldes/view.hpp"

namespace FielDes {

static const QColor kAmber(0xff, 0xcc, 0x33);

// The text of a menu of the menu bar without its mnemonic, in the language of the program (the menus are made with T("&View"))
static QString menuBarText(const QString& english)
{
    QString t = T(QString("&") + english);
    t.remove('&');
    return t;
}

// What stops the user from touching what the step is not about: it covers the window, and has holes where the step
// is about something.  It paints nothing (the layer above it does), and takes every mouse event that is not in a hole
class TourBlocker : public QWidget
{
public:
    explicit TourBlocker(QWidget* parent) : QWidget(parent)
    {
        setAttribute(Qt::WA_NoSystemBackground);
        setMouseTracking(true);
    }
    std::function<void()> blocked;                      // (a click that went nowhere)
protected:
    void paintEvent(QPaintEvent*) override {}
    void mousePressEvent(QMouseEvent* e) override { e->accept(); if (blocked) blocked(); }
    void mouseReleaseEvent(QMouseEvent* e) override { e->accept(); }
    void mouseMoveEvent(QMouseEvent* e) override { e->accept(); }
    void mouseDoubleClickEvent(QMouseEvent* e) override { e->accept(); }
    void wheelEvent(QWheelEvent* e) override { e->accept(); }
    void contextMenuEvent(QContextMenuEvent* e) override { e->accept(); }
};

// What the tour draws over the window: the dimming with the glowing frames round what it is about, the arrow from the card to it,
// and the pretend cursor of "Show me".  It lets every mouse event through (the blocker below it decides which are used)
class TourLayer : public QWidget
{
public:
    explicit TourLayer(QWidget* parent) : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        m_clock.start();
    }

    QVector<QRect> holes;
    QVector<QRect> frames;                              // (glowing frames without dimming: the line, the field, the row)
    QVector<QRect> lit;                                 // (not dimmed, and nothing to click: the lines of the script the step is about)
    QVector<QPair<QRect, double>> changes;              // (lines of the script that were just changed, and how much they still glow)
    QPixmap menuPic;                                    // (a menu the step is about, drawn as it opens)
    QRect menuRect;
    bool dim = true;
    bool arrow = false;
    QPoint arrowFrom, arrowTo;
    bool cursorShown = false, cursorPressed = false;
    QString cursorKey;                                  // (a key the pretend hand holds down: "Ctrl")
    QPoint cursor;
    double flash = 0;                                   // (a click outside what the step is about: the frames flare up)

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const double t = m_clock.elapsed() / 1000.0;
        const double pulse = 0.5 + 0.5 * std::sin(t * 4.2);

        if (dim)
        {
            QPainterPath path;
            path.addRect(rect());
            for (const QRect& h : holes)
            {
                QPainterPath hole;
                hole.addRoundedRect(QRectF(h).adjusted(-4, -4, 4, 4), 7, 7);
                path = path.subtracted(hole);
            }
            for (const QRect& l : lit)
            {
                QPainterPath line;
                line.addRoundedRect(QRectF(l).adjusted(-2, -1, 2, 1), 3, 3);
                path = path.subtracted(line);
            }
            p.fillPath(path, QColor(3, 20, 28, 168));
        }
        // The lines of the script that were just changed (by the user's hand anywhere: the number, the tree, the viewport) glow
        // and fade: the script and what is drawn are one thing
        for (const auto& c : changes)
        {
            QColor fill = kAmber;
            fill.setAlpha(int(95 * c.second));
            p.setPen(Qt::NoPen);
            p.setBrush(fill);
            p.drawRoundedRect(QRectF(c.first).adjusted(-2, -1, 2, 1), 3, 3);
            QColor edge = kAmber;
            edge.setAlpha(int(255 * c.second));
            p.setPen(QPen(edge, 1.6));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(QRectF(c.first).adjusted(-2, -1, 2, 1), 3, 3);
        }
        for (const QRect& h : holes)
        {
            const QRectF r = QRectF(h).adjusted(-5, -5, 5, 5);
            const double f = std::min(1.0, flash);
            QColor glow = kAmber;
            glow.setAlpha(int(40 + 50 * pulse + 120 * f));
            p.setPen(QPen(glow, 7 + 4 * f));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(r, 8, 8);
            QColor line = kAmber;
            line.setAlpha(int(170 + 85 * pulse));
            p.setPen(QPen(line, 2.2));
            p.drawRoundedRect(r, 8, 8);
        }
        if (!menuPic.isNull() && !menuRect.isNull())
        {
            p.setOpacity(1);
            p.drawPixmap(menuRect.topLeft(), menuPic);
            p.setPen(QPen(QColor(147, 161, 161, 170), 1));
            p.setBrush(Qt::NoBrush);
            p.drawRect(menuRect.adjusted(0, 0, -1, -1));
        }
        for (const QRect& h : frames)
        {
            const QRectF r = QRectF(h).adjusted(-2, -1, 2, 1);
            QColor glow = kAmber;
            glow.setAlpha(int(50 + 60 * pulse));
            p.setPen(QPen(glow, 6));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(r, 4, 4);
            QColor line = kAmber;
            line.setAlpha(int(190 + 65 * pulse));
            p.setPen(QPen(line, 2));
            p.drawRoundedRect(r, 4, 4);
        }
        flash = std::max(0.0, flash - 0.06);

        if (arrow)
        {
            // from the card to what it is about, with a head; the line moves a little so that the eye follows it
            const QPointF a(arrowFrom), b(arrowTo);
            const QPointF d = b - a;
            const double len = std::sqrt(d.x() * d.x() + d.y() * d.y());
            if (len > 18)
            {
                const QPointF u = d / len, n(-u.y(), u.x());
                const QPointF mid = (a + b) / 2 + n * std::min(40.0, len * 0.14);
                QPainterPath curve(a);
                curve.quadTo(mid, b - u * 6);
                QPen pen(kAmber, 3.2, Qt::SolidLine, Qt::RoundCap);
                p.setPen(pen);
                p.setBrush(Qt::NoBrush);
                p.drawPath(curve);
                // (the direction at the end of the curve)
                QPointF end = b - u * 4, before = curve.pointAtPercent(0.94);
                QPointF dir = end - before;
                const double dl = std::sqrt(dir.x() * dir.x() + dir.y() * dir.y());
                if (dl > 0) dir /= dl; else dir = u;
                const QPointF dn(-dir.y(), dir.x());
                const double head = 13 + 2 * pulse;
                QPolygonF tri;
                tri << end << end - dir * head + dn * (head * 0.55) << end - dir * head - dn * (head * 0.55);
                p.setPen(Qt::NoPen);
                p.setBrush(kAmber);
                p.drawPolygon(tri);
                p.drawEllipse(a, 4.0, 4.0);
            }
        }
        if (cursorShown)
        {
            // the pretend cursor: an arrow, with a ring when a button is down
            QPolygonF arrowShape;
            arrowShape << QPointF(0, 0) << QPointF(0, 17) << QPointF(4.4, 13) << QPointF(7.6, 20.5) << QPointF(10.2, 19.4)
                       << QPointF(7.1, 12.2) << QPointF(12.6, 12.2);
            p.save();
            p.translate(cursor);
            if (cursorPressed)
            {
                p.setPen(QPen(QColor(255, 255, 255, 220), 2));
                p.setBrush(QColor(255, 204, 51, 90));
                p.drawEllipse(QPointF(0, 0), 13, 13);
            }
            p.setPen(QPen(QColor(10, 10, 10), 1.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(Qt::white);
            p.drawPolygon(arrowShape);
            if (!cursorKey.isEmpty())
            {
                // the key held down: a small key cap beside the cursor
                QFont f = p.font();
                f.setPointSizeF(8.5);
                f.setBold(true);
                p.setFont(f);
                const QRectF cap(15, 14, p.fontMetrics().horizontalAdvance(cursorKey) + 12, 18);
                p.setPen(QPen(QColor(10, 10, 10), 1.2));
                p.setBrush(QColor(255, 204, 51));
                p.drawRoundedRect(cap, 4, 4);
                p.drawText(cap, Qt::AlignCenter, cursorKey);
            }
            p.restore();
        }
    }

private:
    QElapsedTimer m_clock;
};

static QPoint clampInto(QPoint p, const QRect& r)
{
    return QPoint(std::max(r.left(), std::min(r.right(), p.x())), std::max(r.top(), std::min(r.bottom(), p.y())));
}

static void sendMouse(QWidget* target, QEvent::Type type, const QPoint& pos, Qt::MouseButton button, Qt::MouseButtons buttons,
                      Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    QMouseEvent e(type, pos, target->mapToGlobal(pos), button, buttons, mods);
    QApplication::sendEvent(target, &e);
}

static void sendKey(QWidget* target, int key)
{
    QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier), release(QEvent::KeyRelease, key, Qt::NoModifier);
    QApplication::sendEvent(target, &press);
    QApplication::sendEvent(target, &release);
}

////////////////////////////////////////////////////////////////////////////////

struct Tutorial::Step
{
    QString title, text, hint;
    std::function<QVector<QRect>()> holes;              // where the user may act (window coordinates); none: nowhere
    std::function<QPoint()> point;                      // what the arrow points at (default: the nearest edge of a small hole)
    QString menuTitle, menuEntry;                       // a menu of the menu bar the step is about, and its entry: the tour draws the
                                                        // menu itself, as a picture the card stands beside -- a real popup is a window
                                                        // of its own, above the tour, that would hide the card and take clicks
    std::function<QRect()> focus;                       // a frame inside the hole: the line, the field
    std::function<QVector<int>()> code;                 // the lines of the script (0-based) the step is about: framed, and not dimmed
    Place place = Auto;
    bool keys = false;                                  // whether the keyboard works in this step
    std::function<void()> enter;
    std::function<bool()> done;                         // what the step asks of the user is done
    std::function<void(std::function<void()>)> show;    // "Show me": the program does it, once, and says when it is over
    QString showLabel = T("Show me");
    QString doneNote = T("Nice.");
    bool welcome = false;
    bool last = false;
};

Tutorial::Tutorial(QMainWindow* window, View* view, Editor* editor, std::function<bool()> loadTour, QObject* parent)
    : QObject(parent), m_window(window), m_view(view), m_editor(editor), m_loadTour(std::move(loadTour)),
      m_steps(new QVector<Step>)
{
    m_layer = new TourLayer(window);
    m_blocker = new TourBlocker(window);
    m_card = new QFrame(window);
    m_card->setObjectName("TourCard");
    m_card->setFixedWidth(440);
    m_card->setStyleSheet(
        "#TourCard { background: #0d3a4a; border: 2px solid #ffcc33; border-radius: 10px; }"
        "#TourCard QLabel { color: #eee8d5; background: transparent; }"
        "#TourCard QPushButton { color: #eee8d5; background: rgba(147, 161, 161, 40); border: 1px solid rgba(147, 161, 161, 110);"
        "  border-radius: 6px; padding: 5px 12px; font-size: 9.5pt; }"
        "#TourCard QPushButton:hover { background: rgba(147, 161, 161, 90); }"
        "#TourCard QPushButton:disabled { color: #6c8a93; border-color: rgba(147, 161, 161, 50); }"
        "#TourCard QPushButton#TourNext { background: #ffcc33; color: #10303b; border: 1px solid #ffcc33; font-weight: bold; }"
        "#TourCard QPushButton#TourNext:hover { background: #ffd966; }"
        "#TourCard QPushButton#TourSkip { background: transparent; border: none; color: #93a1a1; padding: 5px 4px; }"
        "#TourCard QPushButton#TourSkip:hover { color: #eee8d5; text-decoration: underline; }");
    auto col = new QVBoxLayout(m_card);
    col->setContentsMargins(16, 12, 16, 12);
    col->setSpacing(6);
    auto head = new QHBoxLayout;
    m_title = new QLabel;
    m_title->setStyleSheet("font-size: 12.5pt; font-weight: bold; color: #ffcc33;");
    m_counter = new QLabel;
    m_counter->setStyleSheet("font-size: 8.5pt; color: #93a1a1;");
    head->addWidget(m_title, 1);
    head->addWidget(m_counter);
    col->addLayout(head);
    m_text = new QLabel;
    m_text->setWordWrap(true);
    m_text->setStyleSheet("font-size: 10.5pt;");
    col->addWidget(m_text);
    m_note = new QLabel;
    m_note->setWordWrap(true);
    m_note->setStyleSheet("font-size: 9pt; color: #93a1a1;");
    col->addWidget(m_note);
    col->addSpacing(4);
    auto buttons = new QHBoxLayout;
    buttons->setSpacing(6);
    m_skipTour = new QPushButton(T("Skip the tour"));
    m_skipTour->setObjectName("TourSkip");
    m_skipTour->setCursor(Qt::PointingHandCursor);
    m_back = new QPushButton(T("Back"));
    m_show = new QPushButton(T("Show me"));
    m_next = new QPushButton;
    m_next->setObjectName("TourNext");
    m_next->setMinimumWidth(118);
    buttons->addWidget(m_skipTour);
    buttons->addStretch(1);
    buttons->addWidget(m_back);
    buttons->addWidget(m_show);
    buttons->addWidget(m_next);
    col->addLayout(buttons);
    for (auto b : {m_back, m_show, m_next}) b->setCursor(Qt::PointingHandCursor);
    connect(m_skipTour, &QPushButton::clicked, this, [this] { stop(false); });
    connect(m_back, &QPushButton::clicked, this, [this] { back(); });
    connect(m_show, &QPushButton::clicked, this, [this] { showMe(); });
    connect(m_next, &QPushButton::clicked, this, [this] { next(); });
    static_cast<TourBlocker*>(m_blocker)->blocked = [this] { static_cast<TourLayer*>(m_layer)->flash = 1.0; };

    m_layer->hide();
    m_blocker->hide();
    m_card->hide();
    m_poll.setInterval(180);
    connect(&m_poll, &QTimer::timeout, this, [this] {
        // (what a "Show me" does is not the user's work: the step is not done by it)
        if (!m_active || m_index < 0 || m_index >= m_steps->size() || m_done || m_demo) return;
        const Step& s = (*m_steps)[m_index];
        if (s.done && s.done())
        {
            m_done = true;
            m_note->setText(s.doneNote);
            m_note->setStyleSheet("font-size: 9.5pt; color: #82cc58; font-weight: bold;");
            m_show->hide();
            m_next->setText(m_index + 1 < m_steps->size() ? T("Next  ▸") : T("Finish"));
            // (a moment to see what the user did, then on; not when what the step asks was done already when it was opened)
            if (!m_doneOnArrival)
            {
                const int gen = m_generation;
                after(1300, [this, gen] { if (m_active && gen == m_generation) next(); });
            }
        }
    });
    m_tick.setInterval(40);
    connect(&m_tick, &QTimer::timeout, this, [this] {
        if (!m_active) return;
        refresh();
        m_layer->update();
    });
    buildSteps();
}

Tutorial::~Tutorial()
{
    delete m_steps;
}

int Tutorial::stepCount() const { return m_steps->size(); }
QString Tutorial::stepTitle() const { return m_index >= 0 && m_index < m_steps->size() ? (*m_steps)[m_index].title : QString(); }

////////////////////////////////////////////////////////////////////////////////
// Where things are

QRect Tutorial::widgetRect(QWidget* w) const
{
    if (!w || !w->isVisible()) return QRect();
    return QRect(w->mapTo(m_window, QPoint(0, 0)), w->size()).intersected(m_window->rect());
}

QRect Tutorial::scriptLineRect(int line0) const
{
    Script* sc = m_editor->scriptWidget();
    const QTextBlock b = sc->document()->findBlockByNumber(line0);
    if (!b.isValid()) return QRect();
    const QRect r = sc->cursorRect(QTextCursor(b));
    const QRect full(0, r.top(), sc->viewport()->width(), r.height());
    return QRect(sc->viewport()->mapTo(m_window, full.topLeft()), full.size()).intersected(widgetRect(sc->viewport()));
}

QPoint Tutorial::scriptPoint(int line0, int col) const
{
    Script* sc = m_editor->scriptWidget();
    const QTextBlock b = sc->document()->findBlockByNumber(line0);
    if (!b.isValid()) return QPoint();
    QTextCursor c(b);
    c.setPosition(b.position() + std::min(col, std::max(0, b.length() - 1)));
    return sc->viewport()->mapTo(m_window, sc->cursorRect(c).center());
}

int Tutorial::scriptLine(const QString& startsWith) const
{
    const QStringList lines = m_editor->getScript().split('\n');
    for (int i = 0; i < lines.size(); ++i)
    {
        if (lines[i].startsWith(startsWith)) return i;
    }
    return -1;
}

double Tutorial::plateHeight(bool* ok) const
{
    // The number of the plate's last `var(...)`: what the tour asks the user to change
    const int line = scriptLine("plate =");
    if (line < 0) { if (ok) *ok = false; return 0; }
    const QString text = m_editor->getScript().split('\n').value(line);
    static const QRegularExpression re(R"(var\(\s*(-?[\d.]+)\s*\))");
    double value = 0;
    bool found = false;
    for (auto m = re.globalMatch(text); m.hasNext();)
    {
        value = m.next().captured(1).toDouble();
        found = true;
    }
    if (ok) *ok = found;
    return value;
}

QWidget* Tutorial::openMenu() const
{
    QWidget* root = nullptr;
    for (QWidget* w : QApplication::topLevelWidgets())
    {
        auto m = qobject_cast<QMenu*>(w);
        if (m && m->isVisible() && !qobject_cast<QMenu*>(m->parentWidget())) root = m;
    }
    return root;
}

QAction* Tutorial::sectionAction() const
{
    for (QAction* top : m_window->menuBar()->actions())
    {
        QString t = top->text();
        t.remove('&');
        if (t != menuBarText("View") || !top->menu()) continue;
        for (QAction* a : top->menu()->actions())
            if (a->text() == T("Section view")) return a;
    }
    return nullptr;
}

////////////////////////////////////////////////////////////////////////////////
// The state of a step

Tutorial::State Tutorial::capture() const
{
    State state;
    state.script = m_editor->getScript();
    for (const QString& key : m_view->scenePanel()->selectionKeys())
        if (key.startsWith("shape:")) state.selected << key.mid(6);
    if (QAction* a = sectionAction()) state.section = a->isChecked();
    return state;
}

void Tutorial::restore(const State& state)
{
    const QString now = m_editor->getScript();
    if (now != state.script)
    {
        const QStringList lines = now.split('\n');
        m_editor->applyEdits({TextEdit{0, 0, int(lines.size()) - 1, int(lines.last().size()), state.script}}, "Tour");
    }
    // (the lines that are put back are not changes the user made: they do not glow)
    m_scriptSeen = state.script.split('\n');
    m_changed.clear();
    if (QAction* a = sectionAction()) a->setChecked(state.section);
    auto scene = m_view->scenePanel();
    scene->clearSelection();
    if (!state.selected.isEmpty()) selectAgain(state.selected, 12);
}

void Tutorial::selectAgain(QStringList names, int tries)
{
    // (the model is in the tree once the script has run again: until it is, the selection waits)
    auto scene = m_view->scenePanel();
    QStringList missing;
    for (const QString& name : names)
        if (!scene->selectionKeys().contains("shape:" + name) && !scene->selectModel(name)) missing << name;
    if (!missing.isEmpty() && tries > 0) after(200, [this, missing, tries] { selectAgain(missing, tries - 1); });
}

void Tutorial::endDemo()
{
    if (!m_demo) return;
    m_demo = false;
    m_busy = false;
    hideCursor();
    restore(m_demoBefore);
    m_show->setEnabled(true);
}

////////////////////////////////////////////////////////////////////////////////
// Time and the pretend hand

void Tutorial::after(int ms, std::function<void()> fn)
{
    const int gen = m_generation;
    QTimer::singleShot(ms, this, [this, gen, fn] {
        if (m_active && gen == m_generation) fn();
    });
}

void Tutorial::setCursor(const QPoint& windowPos, bool pressed)
{
    m_cursor = windowPos;
    m_cursorShown = true;
    auto layer = static_cast<TourLayer*>(m_layer);
    layer->cursor = windowPos;
    layer->cursorShown = true;
    layer->cursorPressed = pressed;
    layer->update();
}

void Tutorial::hideCursor()
{
    static_cast<TourLayer*>(m_layer)->cursorKey.clear();
    m_cursorShown = false;
    auto layer = static_cast<TourLayer*>(m_layer);
    layer->cursorShown = false;
    layer->update();
}

void Tutorial::glide(const QPoint& from, const QPoint& to, int ms, std::function<void(QPoint)> each, std::function<void()> done)
{
    auto anim = new QVariantAnimation(this);
    anim->setDuration(std::max(1, ms));
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->setEasingCurve(QEasingCurve::InOutQuad);
    const int gen = m_generation;
    connect(anim, &QVariantAnimation::valueChanged, this, [=](const QVariant& v) {
        if (!m_active || gen != m_generation) { anim->stop(); return; }
        const double f = v.toDouble();
        each(QPoint(int(from.x() + (to.x() - from.x()) * f), int(from.y() + (to.y() - from.y()) * f)));
    });
    connect(anim, &QVariantAnimation::finished, this, [=] {
        if (m_active && gen == m_generation) done();
        anim->deleteLater();
    });
    anim->start();
}

void Tutorial::fakeDrag(QWidget* target0, const QPoint& from, const QPoint& to, int ms, std::function<void()> done,
                       Qt::KeyboardModifiers mods)
{
    static_cast<TourLayer*>(m_layer)->cursorKey = (mods & Qt::ControlModifier) ? "Ctrl" : QString();
    // What a hand does: the cursor comes, rests on the thing, presses, drags, lets go
    QPointer<QWidget> target(target0);
    const QPoint wFrom = target->mapTo(m_window, from), wTo = target->mapTo(m_window, to);
    m_busy = true;
    setCursor(wFrom);
    sendMouse(target, QEvent::MouseMove, from, Qt::NoButton, Qt::NoButton, mods);
    after(420, [=] {
        if (!target) { m_busy = false; hideCursor(); return; }          // (what it was done on is gone: the demo ends)
        setCursor(wFrom, true);
        sendMouse(target, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton, mods);
        after(160, [=] {
            glide(wFrom, wTo, ms, [=](QPoint wp) {
                setCursor(wp, true);
                if (target) sendMouse(target, QEvent::MouseMove, target->mapFrom(m_window, wp), Qt::NoButton, Qt::LeftButton, mods);
            }, [=] {
                if (target) sendMouse(target, QEvent::MouseButtonRelease, target->mapFrom(m_window, wTo), Qt::LeftButton, Qt::NoButton, mods);
                setCursor(wTo, false);
                after(500, [=] { m_busy = false; hideCursor(); if (done) done(); });
            });
        });
    });
}

void Tutorial::fakeClick(QWidget* target0, const QPoint& at, Qt::MouseButton button, std::function<void()> done)
{
    QPointer<QWidget> target(target0);
    const QPoint w = target->mapTo(m_window, at);
    m_busy = true;
    setCursor(w);
    sendMouse(target, QEvent::MouseMove, at, Qt::NoButton, Qt::NoButton);
    after(500, [=] {
        if (!target) { m_busy = false; hideCursor(); return; }
        setCursor(w, true);
        sendMouse(target, QEvent::MouseButtonPress, at, button, button);
        after(140, [=] {
            if (target) sendMouse(target, QEvent::MouseButtonRelease, at, button, Qt::NoButton);
            setCursor(w, false);
            after(350, [=] { m_busy = false; if (done) done(); });
        });
    });
}

void Tutorial::typeOver(int line0, int col0, int col1, const QString& text, std::function<void()> done)
{
    m_busy = true;
    const QPoint at = scriptPoint(line0, col0);
    setCursor(m_cursorShown ? m_cursor : at + QPoint(120, 40));
    glide(m_cursor, at, 600, [=](QPoint wp) { setCursor(wp); }, [=] {
        setCursor(at, true);
        after(220, [=] {
            setCursor(at, false);
            int shown = col1 - col0;
            for (int i = 1; i <= text.size(); ++i)
            {
                const int length = shown;
                const QString part = text.left(i);
                after(120 * i, [=] {
                    m_editor->applyEdits({TextEdit{line0, col0, line0, col0 + length, part}}, "Tour");
                });
                shown = i;
            }
            after(120 * text.size() + 500, [=] { m_busy = false; hideCursor(); if (done) done(); });
        });
    });
}

////////////////////////////////////////////////////////////////////////////////
// The steps

void Tutorial::buildSteps()
{
    auto scene = [this] { return m_view->scenePanel(); };
    auto holesOf = [this](std::function<QWidget*()> w) {
        return [this, w]() -> QVector<QRect> { const QRect r = widgetRect(w()); return r.isEmpty() ? QVector<QRect>() : QVector<QRect>{r}; };
    };
    auto scriptWidget = [this]() -> QWidget* { return m_editor->scriptWidget(); };
    auto viewWidget = [this]() -> QWidget* { return m_view; };
    auto panelWidget = [scene]() -> QWidget* { return scene(); };
    // (the arrow of a row ends just outside the panel, at the height of the row: it covers nothing)
    auto rowTip = [this, scene](const QString& prefix) {
        return [this, scene, prefix]() -> QPoint {
            const QRect r = scene()->rowRect(prefix, m_window);
            const QRect panel = widgetRect(scene());
            return r.isEmpty() || panel.isEmpty() ? QPoint() : QPoint(panel.right() + 3, r.center().y());
        };
    };
    auto plateLine = [this]() -> QRect { return scriptLineRect(std::max(0, scriptLine("plate ="))); };
    // (the lines of the script that match: what a step frames in the editor beside what it is about in the viewport or the tree)
    auto linesWith = [this](const QString& pattern) {
        return [this, pattern]() -> QVector<int> {
            QVector<int> out;
            const QRegularExpression re(pattern);
            const QStringList lines = m_editor->getScript().split('\n');
            for (int i = 0; i < lines.size(); ++i)
                if (re.match(lines[i]).hasMatch()) out << i;
            return out;
        };
    };

    QVector<Step>& steps = *m_steps;
    auto add = [&](const Step& s) { steps.push_back(s); };

    // 0
    {
        Step s;
        s.title = T("Welcome to FielDes");
        s.text = T("A short tour of the basics: two minutes. You can skip any step, or the whole tour.");
        s.hint = T("It starts again from Help ▸ Guided tour.");
        s.welcome = true;
        s.place = Center;
        add(s);
    }
    // 1
    {
        Step s;
        s.title = T("The script is the model");
        s.focus = plateLine;
        s.text = T("A FielDes model is a short script. Everything you see is made by these lines.");
        s.holes = holesOf(scriptWidget);
        s.point = [this] { const QRect r = scriptLineRect(std::max(0, scriptLine("plate ="))); return r.isEmpty() ? QPoint() : QPoint(r.right() - 20, r.center().y()); };
        s.place = Right;
        s.enter = [this] { m_editor->scriptWidget()->goToLine(std::max(0, scriptLine("plate =")), false); };
        add(s);
    }
    // 2
    {
        Step s;
        s.title = T("Change a number");
        s.focus = plateLine;
        s.text = T("In the plate line, change the 6 to 12.");
        s.hint = T("Click in the script and type. Or press Show me.");
        s.holes = holesOf(scriptWidget);
        s.point = [this] { const QRect r = scriptLineRect(std::max(0, scriptLine("plate ="))); return r.isEmpty() ? QPoint() : QPoint(r.right() - 20, r.center().y()); };
        s.place = Right;
        s.keys = true;
        s.enter = [this] {
            m_baseline = plateHeight();
            m_editor->scriptWidget()->goToLine(std::max(0, scriptLine("plate =")), false);
        };
        s.done = [this] { bool ok = false; const double v = plateHeight(&ok); return ok && std::abs(v - m_baseline) > 1e-9; };
        s.doneNote = T("Nice. The plate is thicker.");
        s.show = [this](std::function<void()> over) {
            const int line = scriptLine("plate =");
            if (line < 0) { over(); return; }
            const QString text = m_editor->getScript().split('\n').value(line);
            static const QRegularExpression re(R"(var\(\s*(-?[\d.]+)\s*\))");
            QRegularExpressionMatch last;
            for (auto m = re.globalMatch(text); m.hasNext();) last = m.next();
            if (!last.hasMatch()) { over(); return; }
            typeOver(line, last.capturedStart(1), last.capturedEnd(1), "12", over);
        };
        add(s);
    }
    // 3
    {
        Step s;
        s.title = T("The viewport follows");
        s.text = T("The model is drawn here. Shift + left mouse, or the middle mouse button, turns it; right mouse pans; the wheel zooms.");
        s.hint = T("The framed line of the script made the plate you see: the picture is that line, drawn.");
        s.code = linesWith("^plate =");
        s.holes = holesOf(viewWidget);
        s.place = InsideBottom;
        add(s);
    }
    // 4
    {
        Step s;
        s.title = T("Drag a surface");
        s.text = T("Drag the top face of the plate up or down. The number in the script follows.");
        s.hint = T("Watch the framed line: it lights up each time it changes. Or press Show me.");
        s.code = linesWith("^plate =");
        s.holes = holesOf(viewWidget);
        s.place = InsideBottom;
        s.point = [this] {
            bool ok = false;
            const double z = plateHeight(&ok);
            return m_view->mapTo(m_window, m_view->screenPoint(QVector3D(15, 0, float(ok ? z : 6))));
        };
        s.enter = [this, scene] {
            scene()->clearSelection();
            m_view->standardView(View::VIEW_ISO);       // (the top face of the plate faces the camera, whatever was done to it in the step before)
            m_baseline = plateHeight();
        };
        s.done = [this] { bool ok = false; const double v = plateHeight(&ok); return ok && std::abs(v - m_baseline) > 1e-6; };
        s.doneNote = T("Nice. The script follows your hand.");
        s.show = [this](std::function<void()> over) {
            bool ok = false;
            const double z = plateHeight(&ok);
            const QPoint from = m_view->screenPoint(QVector3D(15, 0, float(ok ? z : 6)));
            if (!m_view->isVisible() || from.isNull()) { over(); return; }
            fakeDrag(m_view, from, from + QPoint(0, -46), 900, over);
        };
        add(s);
    }
    // 5
    {
        Step s;
        s.title = T("The model tree");
        s.text = T("Every model the script makes is listed here. Each kind has its own icon.");
        s.holes = holesOf(panelWidget);
        s.place = Right;
        s.enter = [this, scene] { scene()->showSettings(false); };
        add(s);
    }
    // 6
    {
        Step s;
        s.title = T("Models inside models");
        s.text = T("drilled is made of plate and hole. They are its arguments, shown as its children.");
        s.hint = T("Drag them in and out of an operation and its call changes: the framed lines are the same models, as text.");
        s.code = linesWith("^(plate|hole|drilled) =");
        s.holes = holesOf(panelWidget);
        s.point = rowTip("plate");
        s.place = Right;
        add(s);
    }
    // 6b: what a placeholder is (it turns up on its own when a model is taken out of a call that cannot do without it)
    {
        Step s;
        s.title = T("Placeholders");
        s.text = T("A call that is missing something has a placeholder (...) in its place, after the name of the argument.");
        s.hint = T("Amber, not red: it is no error, it is what has to be done for the script to run. The note under the model tree lists them: "
                   "click one to select it in the code editor and write what goes there. Hover an argument's name to read what it is.");
        s.code = linesWith("^drilled\\b");
        s.holes = holesOf(panelWidget);
        s.point = rowTip("drilled");
        s.place = Right;
        add(s);
    }
    // 7
    {
        Step s;
        s.title = T("Select a model");
        s.text = T("Click drilled in the tree.");
        s.code = linesWith("^drilled\\b");
        s.holes = holesOf(panelWidget);
        s.point = rowTip("drilled");
        s.place = Right;
        s.enter = [scene] { scene()->clearSelection(); };
        s.done = [scene] { return scene()->selectionKeys().contains("shape:drilled"); };
        s.doneNote = T("Nice. It is selected in the viewport too.");
        s.show = [this, scene](std::function<void()> over) {
            const QRect r = scene()->rowRect("drilled", m_window);
            if (r.isEmpty()) { over(); return; }
            QWidget* vp = scene()->treeViewport();
            fakeClick(vp, vp->mapFrom(m_window, QPoint(r.left() + 70, r.center().y())), Qt::LeftButton, over);
        };
        add(s);
    }
    // 8
    {
        Step s;
        s.title = T("The gizmo");
        s.text = T("A selected model has a gizmo. Drag an arrow to move the model.");
        s.hint = T("The gizmo is a line of the script too (handles): its numbers change as you drag. Or press Show me.");
        s.code = linesWith("handles\\(");
        s.holes = holesOf(viewWidget);
        s.place = InsideBottom;
        s.enter = [this, scene] {
            if (!scene()->selectionKeys().contains("shape:drilled")) scene()->selectModel("drilled");
        };
        s.point = [this] { QPoint p; return m_view->handleGripPoint(0, 0, p) ? m_view->mapTo(m_window, p) : QPoint(); };
        s.show = [this](std::function<void()> over) {
            QPoint p;
            if (!m_view->handleGripPoint(0, 0, p)) { over(); return; }
            fakeDrag(m_view, p, p + QPoint(60, 0), 800, over);
        };
        add(s);
    }
    // 9
    {
        Step s;
        s.title = T("Make things");
        s.text = T("Right-click empty space, choose New 3D shape, then sphere.");
        s.hint = T("Or press Show me.");
        s.holes = holesOf(viewWidget);
        s.place = InsideTopRight;
        s.enter = [this, scene] {
            scene()->clearSelection();
            m_baseline = m_editor->getScript().count("sphere(");
        };
        s.done = [this] { return m_editor->getScript().count("sphere(") > m_baseline; };
        s.doneNote = T("Nice. The new line is in the script.");
        s.show = [this](std::function<void()> over) {
            const QPoint at(int(m_view->width() * 0.12), int(m_view->height() * 0.80));
            fakeClick(m_view, at, Qt::RightButton, [this, over] {
                // the menu is open: its entries lit one after the other
                QWidget* menu = openMenu();
                auto root = qobject_cast<QMenu*>(menu);
                if (!root) { over(); return; }
                QAction* found = nullptr;
                for (QAction* a : root->actions())
                    if (a->text() == T("New 3D shape")) found = a;
                if (!found || !found->menu()) { root->close(); over(); return; }
                root->setActiveAction(found);
                QPointer<QMenu> sub = found->menu();
                QPointer<QMenu> top = root;
                sub->popup(root->mapToGlobal(root->actionGeometry(found).topRight()));
                // (the entry is lit, then chosen)
                after(800, [this, sub, top, over] {
                    if (!sub) { over(); return; }
                    QPointer<QAction> sphere;
                    for (QAction* a : sub->actions())
                        if (a->text() == "sphere") sphere = a;
                    if (!sphere) { sub->close(); if (top) top->close(); over(); return; }
                    sub->setActiveAction(sphere);
                    after(900, [sub, top, sphere, over] {
                        // (the menu may have been closed meanwhile, and a closed menu is deleted with its entries)
                        if (sub && top && sphere)
                        {
                            sphere->trigger();
                            sub->close();
                            top->close();
                        }
                        over();
                    });
                });
            });
        };
        add(s);
    }
    // A section view (a viewer of its own, switched on by the user)
    {
        Step s;
        s.title = T("Cut a section");
        s.text = T("Press Ctrl+Shift+X (View ▸ Section view). A plane cuts the model open. Drag the arrow of the plane through the model.");
        s.hint = T("The section view is yours to switch on; its card has the plane's settings. The field viewer is another viewer, which "
                 "opens by itself when you select a field: both can be open together. Or press Show me.");
        s.holes = holesOf(viewWidget);
        s.place = InsideBottom;
        s.keys = true;
        s.enter = [this, scene] {
            if (QAction* a = sectionAction()) a->setChecked(false);
            scene()->clearSelection();
        };
        s.done = [this] {
            // (switched on, and the plane moved since: the plane starts where the section view puts it, through the model)
            QAction* a = sectionAction();
            if (!a || !a->isChecked()) { m_sectionSeen = false; return false; }
            const double offset = m_view->sectionSettings().offset;
            if (!m_sectionSeen) { m_sectionSeen = true; m_sectionBase = offset; return false; }
            return std::abs(offset - m_sectionBase) > 1e-3;
        };
        s.doneNote = T("Nice. The plane cuts the model open; its card has the rest of the settings.");
        s.show = [this](std::function<void()> over) {
            QAction* a = sectionAction();
            if (!a) { over(); return; }
            m_busy = true;
            // (the shortcut is pressed: a key cap by the cursor says so)
            static_cast<TourLayer*>(m_layer)->cursorKey = "Ctrl+Shift+X";
            setCursor(m_view->mapTo(m_window, QPoint(m_view->width() / 2, m_view->height() / 2)));
            after(900, [this, a, over] {
                a->setChecked(true);
                after(1000, [this, over] {
                    QPointF knob, tip;
                    m_busy = false;
                    hideCursor();
                    if (!m_view->sectionHandlePoints(knob, tip)) { over(); return; }
                    // (along the arrow, and past its end: the plane goes right through the model)
                    const QPoint from = knob.toPoint();
                    fakeDrag(m_view, from, from + 2 * (tip.toPoint() - from), 1200, over);
                });
            });
        };
        add(s);
    }
    // 10
    {
        Step s;
        s.title = T("A field instead of a number");
        s.text = T("swell is a field. Drag it onto swollen: it takes the place of the 1.0.");
        s.hint = T("Anywhere a number goes, a field goes. The framed lines show the call change. Or press Show me.");
        s.code = linesWith("^(anchor|swell|swollen) =");
        s.holes = holesOf(panelWidget);
        s.point = rowTip("swell");
        s.place = Right;
        s.enter = [this, scene] {
            while (auto m = QApplication::activePopupWidget()) m->close();
            if (QAction* a = sectionAction()) a->setChecked(false);       // (the section view of the step before is put away)
            // (the lines the step needs: a point, a field made of it, and a model that grows drilled by 1; it is shown instead)
            if (scriptLine("swollen =") < 0)
            {
                const QStringList lines = m_editor->getScript().split('\n');
                for (int i = int(lines.size()) - 1; i >= 0; --i)
                {
                    if (lines[i].trimmed() != "drilled") continue;
                    m_editor->applyEdits({TextEdit{i, 0, i, int(lines[i].size()),
                        "anchor = point(24, 12, 6)\n"
                        "swell = ramp(distance_to_point(anchor), (0, 50), (4.0, 0.3))\n"
                        "swollen = offset(drilled, 1.0)\n"
                        "swollen"}}, "Tour");
                    break;
                }
            }
            scene()->clearSelection();
        };
        s.done = [this] { return m_editor->getScript().contains("offset(drilled, swell)"); };
        s.doneNote = T("Nice. The growth now follows the field.");
        s.show = [this, scene](std::function<void()> over) {
            const QRect a = scene()->rowRect("swell", m_window), b = scene()->rowRect("swollen", m_window);
            if (a.isEmpty() || b.isEmpty()) { over(); return; }
            QWidget* vp = scene()->treeViewport();
            fakeDrag(vp, vp->mapFrom(m_window, QPoint(a.left() + 60, a.center().y())),
                     vp->mapFrom(m_window, QPoint(b.left() + 90, b.center().y())), 1100, over);
        };
        add(s);
    }
    // A reference: Ctrl+drag
    {
        Step s;
        s.title = T("A reference with Ctrl");
        s.text = T("Hold Ctrl and drag knob onto capped: capped uses it, and knob stays where it is.");
        s.hint = T("Without Ctrl, a drag moves the model into the operation. With Ctrl, capped lists a faded shadow of knob, and its "
                 "line says # shadow: knob. Or press Show me.");
        s.code = linesWith("^(knob|capped) =|#\\s*shadow:");
        s.holes = holesOf(panelWidget);
        s.point = rowTip("knob");
        s.place = Right;
        s.enter = [this, scene] {
            while (auto m = QApplication::activePopupWidget()) m->close();
            // (the models the step needs, at the end of the script: a sphere on its own, and an operation it can be used in)
            if (scriptLine("capped =") < 0)
            {
                const QStringList lines = m_editor->getScript().split('\n');
                int last = int(lines.size()) - 1;
                while (last > 0 && lines[last].trimmed().isEmpty()) --last;
                m_editor->applyEdits({TextEdit{last, int(lines[last].size()), last, int(lines[last].size()),
                    "\nknob = sphere(4, (24, 0, 18))\n"
                    "knob\n"
                    "cap = box_exact((-8, -8, 14), (8, 8, 16))\n"
                    "ring = cylinder_z(9, 2, (0, 0, 13))\n"
                    "capped = union(cap, ring)\n"
                    "capped"}}, "Tour");
            }
            scene()->clearSelection();
        };
        s.done = [this] { return m_editor->getScript().contains(QRegularExpression("#\\s*shadow:\\s*knob")); };
        s.doneNote = T("Nice. knob did not move: capped only holds a reference to it.");
        s.show = [this, scene](std::function<void()> over) {
            const QRect a = scene()->rowRect("knob", m_window), b = scene()->rowRect("capped", m_window);
            if (a.isEmpty() || b.isEmpty()) { over(); return; }
            QWidget* vp = scene()->treeViewport();
            fakeDrag(vp, vp->mapFrom(m_window, QPoint(a.left() + 60, a.center().y())),
                     vp->mapFrom(m_window, QPoint(b.left() + 90, b.center().y())), 1100, over, Qt::ControlModifier);
        };
        add(s);
    }
    // 11
    {
        Step s;
        s.title = T("Rename");
        s.text = T("Double-click a name in the tree to rename it. Try swollen.");
        s.hint = T("Every line that uses the name is framed, and changes with it. Or press Show me.");
        s.code = linesWith("\\b(swollen|grown)\\b");
        s.holes = holesOf(panelWidget);
        s.point = rowTip("swollen");
        s.place = Right;
        s.keys = true;
        s.done = [this] { return !m_editor->getScript().contains(QRegularExpression("\\bswollen\\b")); };
        s.doneNote = T("Nice. Every use of the name changed.");
        s.show = [this, scene](std::function<void()> over) {
            const QRect r = scene()->rowRect("swollen", m_window);
            if (r.isEmpty()) { over(); return; }
            QWidget* vp = scene()->treeViewport();
            const QPoint at = vp->mapFrom(m_window, QPoint(r.left() + 70, r.center().y()));
            QPointer<QWidget> viewport(vp);
            fakeClick(vp, at, Qt::LeftButton, [this, viewport, at, over] {
                QWidget* vp = viewport;
                if (!vp) { over(); return; }
                // the double click: the name can be edited; the new name is typed, then Enter
                sendMouse(vp, QEvent::MouseButtonDblClick, at, Qt::LeftButton, Qt::LeftButton);
                sendMouse(vp, QEvent::MouseButtonRelease, at, Qt::LeftButton, Qt::NoButton);
                // (the name editor is the field that has the keyboard, in the tree; the tree is built again when the script has
                // run, and the editor with it: it is looked up each time, never kept)
                auto editor = [this]() -> QLineEdit* {
                    auto f = qobject_cast<QLineEdit*>(QApplication::focusWidget());
                    return f && f->parentWidget() == m_view->scenePanel()->treeViewport() ? f : nullptr;
                };
                after(500, [this, editor, over] {
                    if (!editor()) { over(); return; }
                    // (typed key by key, as a hand does: the script follows every letter)
                    const QString name = "grown";
                    for (int i = 1; i <= name.size(); ++i)
                        after(130 * i, [editor, name, i] {
                            QLineEdit* f = editor();
                            if (!f) return;
                            QKeyEvent press(QEvent::KeyPress, 0, Qt::NoModifier, name.mid(i - 1, 1));
                            QApplication::sendEvent(f, &press);
                        });
                    after(130 * name.size() + 450, [editor] { if (QLineEdit* f = editor()) sendKey(f, Qt::Key_Return); });
                    after(130 * name.size() + 700, over);
                });
            });
        };
        add(s);
    }
    // 12
    {
        Step s;
        s.title = T("Render settings");
        s.text = T("The region and the resolution are lines of the code. Change the resolution from 4 to 6 in the framed line.");
        s.hint = T("More samples per mm: a finer picture, slower. The row in the tree shows the numbers and goes to the line when clicked.");
        s.code = linesWith("set_resolution");
        s.holes = holesOf(panelWidget);
        s.point = rowTip(T("Render settings"));
        s.place = Right;
        s.keys = true;
        s.enter = [this, scene] {
            scene()->showSettings(true);
            static const QRegularExpression re(R"(set_resolution\(\s*([\d.]+)\s*\))");
            const auto m = re.match(m_editor->getScript());
            m_baseline = m.hasMatch() ? m.captured(1).toDouble() : 0;
        };
        s.done = [this] {
            static const QRegularExpression re(R"(set_resolution\(\s*([\d.]+)\s*\))");
            const auto m = re.match(m_editor->getScript());
            return m.hasMatch() && std::abs(m.captured(1).toDouble() - m_baseline) > 1e-9;
        };
        s.doneNote = T("Nice. The viewport meshes again at the new resolution.");
        add(s);
    }
    // 13
    {
        Step s;
        s.title = T("Custom function from block");
        s.text = T("A Python file in the blocks folder is a block: its functions work in every script, like the built-in ones.");
        s.hint = T("Settings ▸ Blocks folder chooses the folder. The right-click menu lists them as New custom block.");
        s.menuTitle = "Settings";
        s.menuEntry = "Blocks folder...";
        s.place = Right;
        add(s);
    }
    // 14
    {
        Step s;
        s.title = T("That is the tour");
        s.text = T("Shift+F1 opens the guide with every feature and shortcut. To see how the different features of FielDes work, open the examples: File ▸ Open example file.");
        s.hint = T("Each example is a small script to read, run and change. Help ▸ Guided tour starts this tour again.");
        s.last = true;
        // (the File menu is drawn beside the card, with the entry framed, as for the blocks folder: it lists the examples)
        s.menuTitle = "File";
        s.menuEntry = "Open example file...";
        s.place = Right;
        s.enter = [] { while (auto m = QApplication::activePopupWidget()) m->close(); };
        add(s);
    }
    m_entry.resize(steps.size());
    m_hasEntry = QVector<char>(steps.size(), 0);
}

////////////////////////////////////////////////////////////////////////////////
// Running it

void Tutorial::offer()
{
    if (m_active) return;
    m_offering = true;
    m_active = true;
    m_hasEntry.fill(0);
    qApp->installEventFilter(this);
    m_window->installEventFilter(this);
    goTo(0);
    m_scriptSeen.clear();
    m_changed.clear();
    m_tick.start();
    m_poll.start();
}

void Tutorial::start()
{
    if (m_loadTour && !m_loadTour())
    {
        stop(false);
        return;
    }
    // (the tour's model has just replaced the script: its lines are not changes, and the steps are all to be opened for the first time)
    m_scriptSeen.clear();
    m_changed.clear();
    m_hasEntry.fill(0);
    if (!m_active)
    {
        m_active = true;
        qApp->installEventFilter(this);
        m_window->installEventFilter(this);
        m_tick.start();
        m_poll.start();
    }
    m_offering = false;
    m_view->standardView(View::VIEW_ISO);       // (the faces the steps point at are the ones that face the camera)
    // (the model is loaded and runs: the tour begins once it is drawn)
    ++m_generation;
    after(900, [this] { goTo(1); });
}

void Tutorial::stop(bool completed)
{
    const bool wasActive = m_active;
    endDemo();                              // (the tour ends with the program as the user had it, not as a demonstration left it)
    m_active = false;
    ++m_generation;
    m_poll.stop();
    m_tick.stop();
    m_layer->hide();
    m_blocker->hide();
    m_card->hide();
    hideCursor();
    while (auto m = QApplication::activePopupWidget()) m->close();
    qApp->removeEventFilter(this);
    m_window->removeEventFilter(this);
    m_busy = false;
    m_index = -1;
    if (wasActive) emit(finished(completed));
}

void Tutorial::goTo(int index, bool viaBack)
{
    if (index < 0) index = 0;
    if (index >= m_steps->size())
    {
        stop(true);
        return;
    }
    endDemo();                              // (what a "Show me" did is undone before the step changes)
    ++m_generation;
    m_index = index;
    m_done = false;
    m_viaBack = viaBack;
    m_busy = false;
    hideCursor();
    while (auto m = QApplication::activePopupWidget()) m->close();
    // The state of the step: what the steps before it left the first time it is opened, and the same again each time after
    // (Back, or forward again): the step asks for something to be done, and it can be done again
    if (index >= 1 && index < m_entry.size())
    {
        if (m_hasEntry[index]) restore(m_entry[index]);
        else
        {
            m_entry[index] = capture();
            m_hasEntry[index] = 1;
        }
    }
    showStep();
}

void Tutorial::next()
{
    if (!m_active) return;
    if (m_index < 0) { start(); return; }
    if (m_offering && m_index == 0) { start(); return; }
    if (m_index + 1 >= m_steps->size()) { stop(true); return; }
    goTo(m_index + 1);
}

void Tutorial::back()
{
    if (!m_active || m_index <= 1) return;
    goTo(m_index - 1, true);
}

void Tutorial::showMe()
{
    if (!m_active || m_busy || m_demo || m_index < 0) return;
    const Step& s = (*m_steps)[m_index];
    if (!s.show) return;
    // It is shown once, and then everything is put back as it was before the button was pressed: the step is the user's to do
    m_demo = true;
    m_demoBefore = capture();
    m_show->setEnabled(false);
    const int step = m_index;
    const int gen = m_generation;
    auto over = [this, step, gen] {
        // (a moment to look at what was done, then back)
        after(1500, [this, step, gen] {
            if (!m_demo || gen != m_generation) return;
            endDemo();
            const Step& s = (*m_steps)[step];
            if (s.enter) s.enter();
            m_note->setText(s.hint + (s.hint.isEmpty() ? "" : "\n") + T("Everything is back as it was. Now you try."));
            m_note->setStyleSheet("font-size: 9pt; color: #93a1a1;");
        });
    };
    s.show(over);
    // (a demonstration that never says it is over -- what it was to work on is not there -- does not hold the step)
    after(25000, [this, gen] { if (m_demo && gen == m_generation) { endDemo(); refresh(); } });
}

void Tutorial::showStep()
{
    const Step& s = (*m_steps)[m_index];
    m_title->setText(s.title);
    m_text->setText(s.text);
    m_note->setText(s.hint);
    m_note->setStyleSheet("font-size: 9pt; color: #93a1a1;");
    m_note->setVisible(!s.hint.isEmpty());
    m_counter->setText(s.welcome ? QString() : QString("%1 / %2").arg(m_index).arg(m_steps->size() - 1));
    m_skipTour->setText(s.welcome ? T("No thanks") : (s.last ? QString() : T("Skip the tour")));
    m_skipTour->setVisible(!s.last);
    m_back->setVisible(!s.welcome && !s.last && m_index > 1);
    m_show->setVisible(bool(s.show));
    m_show->setText(s.showLabel);
    m_next->setText(s.welcome ? T("Start the tour") : s.last ? T("Finish") : (s.done ? T("Skip step  ▸") : T("Next  ▸")));
    // (the window is dimmed, the card is above it)
    m_blocker->setGeometry(m_window->rect());
    m_layer->setGeometry(m_window->rect());
    m_blocker->show();
    m_layer->show();
    m_blocker->raise();
    m_layer->raise();
    m_card->show();
    m_card->raise();
    // (a menu this step is about is drawn by the tour: it is grabbed here, once)
    m_menuPic = QPixmap();
    if (!s.menuTitle.isEmpty())
    {
        for (QAction* a : m_window->menuBar()->actions())
        {
            QString t = a->text();
            t.remove('&');
            if (t != menuBarText(s.menuTitle) || !a->menu()) continue;
            QMenu* menu = a->menu();
            menu->ensurePolished();
            menu->resize(menu->sizeHint());
            m_menuPic = menu->grab();
        }
    }
    m_sectionSeen = false;
    m_show->setEnabled(true);
    if (s.enter) s.enter();
    // (what the step asks is done already: this is a step that was done before and not put back, or a state the user is in --
    // it does not move on by itself)
    m_doneOnArrival = s.done && s.done();
    refresh();
}

namespace {

// The lines of `after` that are not in `before` as they were: written new or changed, or moved (a line that only moved down
// because another was put above it is the same line and is not one of them).  The longest common run of lines is what stayed
QVector<int> changedLines(const QStringList& before, const QStringList& after)
{
    const int n = int(before.size()), m = int(after.size());
    QVector<int> out;
    if (qint64(n) * m > 250000)
    {
        for (int j = 0; j < m; ++j)
            if ((j >= n || before[j] != after[j]) && !after[j].trimmed().isEmpty()) out << j;
        return out;
    }
    QVector<QVector<int>> best(n + 1, QVector<int>(m + 1, 0));
    for (int i = n - 1; i >= 0; --i)
        for (int j = m - 1; j >= 0; --j)
            best[i][j] = before[i] == after[j] ? best[i + 1][j + 1] + 1 : std::max(best[i + 1][j], best[i][j + 1]);
    QVector<char> kept(m, 0);
    int i = 0, j = 0;
    while (i < n && j < m)
    {
        if (before[i] == after[j]) { kept[j] = 1; ++i; ++j; }
        else if (best[i + 1][j] >= best[i][j + 1]) ++i;
        else ++j;
    }
    for (int k = 0; k < m; ++k)
        if (!kept[k] && !after[k].trimmed().isEmpty()) out << k;
    return out;
}

}   // namespace

void Tutorial::trackChanges()
{
    const QStringList now = m_editor->getScript().split('\n');
    if (m_scriptSeen.isEmpty())
    {
        m_scriptSeen = now;                 // (the first look: nothing was changed yet)
        return;
    }
    if (now == m_scriptSeen) return;
    m_changed = changedLines(m_scriptSeen, now);
    m_scriptSeen = now;
    m_changedClock.restart();
}

void Tutorial::refresh()
{
    if (!m_active || m_index < 0) return;
    const Step& s = (*m_steps)[m_index];
    auto layer = static_cast<TourLayer*>(m_layer);
    if (m_blocker->size() != m_window->size())
    {
        m_blocker->setGeometry(m_window->rect());
        m_layer->setGeometry(m_window->rect());
    }
    // The menu this step is about, drawn where it opens, with its entry framed; the button that opens it is framed too
    m_menuRect = QRect();
    QRect entryRect, buttonRect;
    if (!s.menuTitle.isEmpty() && !m_menuPic.isNull())
    {
        for (QAction* a : m_window->menuBar()->actions())
        {
            QString t = a->text();
            t.remove('&');
            if (t != menuBarText(s.menuTitle) || !a->menu()) continue;
            QMenu* menu = a->menu();
            const QRect g = m_window->menuBar()->actionGeometry(a);
            buttonRect = QRect(m_window->menuBar()->mapTo(m_window, g.topLeft()), g.size());
            m_menuRect = QRect(m_window->menuBar()->mapTo(m_window, g.bottomLeft()), menu->size());
            for (QAction* item : menu->actions())
            {
                if (item->text().remove('&') == T(s.menuEntry))
                    entryRect = menu->actionGeometry(item).translated(m_menuRect.topLeft());
            }
        }
    }
    {
        auto picture = static_cast<TourLayer*>(m_layer);
        picture->menuPic = m_menuRect.isNull() ? QPixmap() : m_menuPic;
        picture->menuRect = m_menuRect;
    }
    QVector<QRect> holes = s.holes ? s.holes() : QVector<QRect>();
    holes.erase(std::remove_if(holes.begin(), holes.end(), [](const QRect& r) { return r.isEmpty(); }), holes.end());
    if (holes != m_holes)
    {
        m_holes = holes;
        updateMask();
    }
    layer->holes = holes;
    layer->dim = true;
    QRect target;
    for (const QRect& h : holes) target = target.isNull() ? h : target.united(h);
    layoutCard(target);
    QPoint to;
    bool arrow = false;
    if (!s.welcome && (!s.last || !entryRect.isNull()) && s.place != Center)
    {
        if (!entryRect.isNull())
        {
            to = QPoint(entryRect.right() + 3, entryRect.center().y());         // (the arrow ends at the right edge of the entry)
            arrow = true;
        }
        else if (s.point)
        {
            to = s.point();
            arrow = !to.isNull();
        }
        else if (!target.isNull() && target.width() * target.height() < m_window->width() * m_window->height() / 5)
        {
            // (what it is about is small: the arrow ends at the nearest point of it)
            const QPoint c = m_card->geometry().center();
            to = clampInto(c, target);
            arrow = !target.contains(c);
        }
    }
    layer->arrow = arrow;
    layer->frames.clear();
    if (s.focus)
    {
        const QRect f = s.focus();
        if (!f.isEmpty()) layer->frames << f;
    }
    if (!buttonRect.isNull()) layer->frames << buttonRect;
    if (!entryRect.isNull()) layer->frames << entryRect;
    {
        // The script is the other half of what is drawn: the lines the step is about stay lit and framed, and the lines that
        // were just changed or moved -- by typing, by a drag in the viewport or in the tree -- glow for a moment and fade
        trackChanges();
        layer->lit.clear();
        layer->changes.clear();
        Script* script = m_editor->scriptWidget();
        if (script && script->isVisible())
        {
            const QVector<int> lines = s.code ? s.code() : QVector<int>();
            for (int line : lines)
            {
                const QRect r = scriptLineRect(line);
                if (r.isEmpty()) continue;
                layer->lit << r;
                layer->frames << r;
            }
            const qint64 age = m_changedClock.isValid() ? m_changedClock.elapsed() : 100000;
            const bool glowing = age < 2600 && !m_changed.isEmpty();
            if (glowing)
            {
                const double f = age < 1600 ? 1.0 : 1.0 - double(age - 1600) / 1000.0;
                for (int line : m_changed)
                {
                    const QRect r = scriptLineRect(line);
                    if (r.isEmpty()) continue;
                    layer->lit << r;
                    layer->changes.append(qMakePair(r, f));
                }
            }
            // (a line that is out of the editor's view is scrolled into it: once for each thing shown)
            const QVector<int>& want = glowing ? m_changed : lines;
            QString key = glowing ? QString("c%1").arg(m_changedClock.msecsSinceReference()) : QString("s%1").arg(m_index);
            if (!glowing) for (int line : lines) key += "," + QString::number(line);
            if (key != m_revealedFor)
            {
                m_revealedFor = key;
                if (!want.isEmpty() && scriptLineRect(want.first()).isEmpty()) script->scrollToLine(want.first());
            }
        }
    }
    if (arrow)
    {
        // from the edge of the card that is nearest to what it points at
        const QRect c = m_card->geometry();
        QPoint from;
        if (to.x() > c.right()) from = QPoint(c.right(), std::max(c.top() + 20, std::min(c.bottom() - 20, to.y())));
        else if (to.x() < c.left()) from = QPoint(c.left(), std::max(c.top() + 20, std::min(c.bottom() - 20, to.y())));
        else if (to.y() > c.bottom()) from = QPoint(std::max(c.left() + 20, std::min(c.right() - 20, to.x())), c.bottom());
        else from = QPoint(std::max(c.left() + 20, std::min(c.right() - 20, to.x())), c.top());
        layer->arrowFrom = from;
        layer->arrowTo = to;
    }
    m_blocker->raise();
    m_layer->raise();
    m_card->raise();
}

void Tutorial::updateMask()
{
    QRegion region(m_blocker->rect());
    for (const QRect& h : m_holes) region -= QRegion(h.adjusted(-3, -3, 3, 3));
    m_blocker->setMask(region);
}

void Tutorial::layoutCard(const QRect& hole)
{
    m_card->adjustSize();
    const QSize size = m_card->sizeHint();
    m_card->resize(440, size.height());
    const Step& s = (*m_steps)[m_index];
    const QRect win = m_window->rect();
    const int margin = (s.point || !s.menuTitle.isEmpty()) ? 46 : 24;
    QRect card(QPoint(0, 0), m_card->size());
    Place place = s.place;
    if (!s.menuTitle.isEmpty() && !m_menuRect.isNull() && place != Center)
    {
        // beside the menu: to its right, in line with its top; where there is no room, to its left
        QRect c = card;
        c.moveTopLeft(QPoint(m_menuRect.right() + margin, m_menuRect.top()));
        if (!win.adjusted(8, 8, -8, -8).contains(c)) c.moveTopRight(QPoint(m_menuRect.left() - margin, m_menuRect.top()));
        c.moveLeft(std::max(8, std::min(win.right() - c.width() - 8, c.left())));
        c.moveTop(std::max(8, std::min(win.bottom() - c.height() - 8, c.top())));
        m_card->move(c.topLeft());
        return;
    }
    if (hole.isNull() || place == Center)
    {
        card.moveCenter(win.center());
        m_card->move(card.topLeft());
        return;
    }
    if (place == InsideBottom)
    {
        card.moveCenter(QPoint(hole.center().x(), 0));
        card.moveBottom(hole.bottom() - 46);
    }
    else if (place == InsideTopRight)
    {
        card.moveTopRight(QPoint(hole.right() - margin, hole.top() + margin));
    }
    else
    {
        // beside what it is about, where there is room: right, left, below, above (the step may prefer one)
        QList<Place> order;
        if (place == Right) order << Right << Left << Below << Above;
        else if (place == Left) order << Left << Right << Below << Above;
        else if (place == Below) order << Below << Above << Right << Left;
        else if (place == Above) order << Above << Below << Right << Left;
        else order << Right << Left << Below << Above;
        bool placed = false;
        for (Place p : order)
        {
            QRect c = card;
            if (p == Right) c.moveTopLeft(QPoint(hole.right() + margin, hole.top()));
            else if (p == Left) c.moveTopRight(QPoint(hole.left() - margin, hole.top()));
            else if (p == Below) c.moveTopLeft(QPoint(hole.left(), hole.bottom() + margin));
            else c.moveBottomLeft(QPoint(hole.left(), hole.top() - margin));
            if (win.adjusted(8, 8, -8, -8).contains(c))
            {
                card = c;
                placed = true;
                break;
            }
        }
        if (!placed)
        {
            // (no room beside it: over the bottom of what it is about)
            card.moveCenter(QPoint(hole.center().x(), 0));
            card.moveBottom(std::min(hole.bottom() - 30, win.bottom() - 20));
        }
    }
    card.moveLeft(std::max(8, std::min(win.right() - card.width() - 8, card.left())));
    card.moveTop(std::max(8, std::min(win.bottom() - card.height() - 8, card.top())));
    m_card->move(card.topLeft());
}

bool Tutorial::eventFilter(QObject* obj, QEvent* e)
{
    if (!m_active) return false;
    if (obj == m_window && (e->type() == QEvent::Resize || e->type() == QEvent::Move))
    {
        QTimer::singleShot(0, this, [this] { refresh(); });
        return false;
    }
    // The keyboard works only where the step is about typing; the card's own buttons work, and the tour's own events do
    switch (e->type())
    {
    case QEvent::KeyPress:
    case QEvent::KeyRelease:
    case QEvent::ShortcutOverride:
    case QEvent::Shortcut:
    {
        if (m_index < 0 || m_index >= m_steps->size()) return false;
        if ((*m_steps)[m_index].keys) return false;
        auto w = qobject_cast<QWidget*>(obj);
        if (w && (w == m_card || m_card->isAncestorOf(w))) return false;
        if (e->type() == QEvent::Shortcut || e->type() == QEvent::ShortcutOverride)
        {
            e->accept();
            return true;
        }
        return qobject_cast<QWidget*>(obj) != nullptr;          // (eaten)
    }
    default:
        return false;
    }
}

}   // namespace FielDes
