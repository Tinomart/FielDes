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
#include <QContextMenuEvent>
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
#include <QDialog>
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
#include <QFontMetrics>
#include <QSettings>
#include <QStyle>
#include <QTextBlock>
#include <QTextCursor>
#include <QToolButton>
#include <QVBoxLayout>

#include "fieldes/i18n.hpp"
#include "fieldes/editor.hpp"
#include "fieldes/scenetree.hpp"
#include "fieldes/script.hpp"
#include "fieldes/tutorial.hpp"
#include "fieldes/view.hpp"

namespace FielDes {

static const int kCardWidth = 500;                    // (the card: wide enough for the row of buttons, the chapter menu and the quit button among them)
static const QColor kAmber(0xff, 0xcc, 0x33);
static const QColor kDone(0x82, 0xcc, 0x58);          // (what a step that is done is outlined in: nothing is left to do in it)

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
    QColor accent = kAmber;                             // (the colour of every outline and of the arrow: green once the step is done)

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
            QColor fill = accent;
            fill.setAlpha(int(95 * c.second));
            p.setPen(Qt::NoPen);
            p.setBrush(fill);
            p.drawRoundedRect(QRectF(c.first).adjusted(-2, -1, 2, 1), 3, 3);
            QColor edge = accent;
            edge.setAlpha(int(255 * c.second));
            p.setPen(QPen(edge, 1.6));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(QRectF(c.first).adjusted(-2, -1, 2, 1), 3, 3);
        }
        for (const QRect& h : holes)
        {
            const QRectF r = QRectF(h).adjusted(-5, -5, 5, 5);
            const double f = std::min(1.0, flash);
            QColor glow = accent;
            glow.setAlpha(int(40 + 50 * pulse + 120 * f));
            p.setPen(QPen(glow, 7 + 4 * f));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(r, 8, 8);
            QColor line = accent;
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
            QColor glow = accent;
            glow.setAlpha(int(50 + 60 * pulse));
            p.setPen(QPen(glow, 6));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(r, 4, 4);
            QColor line = accent;
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
                QPen pen(accent, 3.2, Qt::SolidLine, Qt::RoundCap);
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
                p.setBrush(accent);
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

// A button of the card is at least as wide as its text needs: the text in its own font (the bold one for the button that goes on), the padding of the style sheet on both sides, the
// border, and a little more for the arrow that comes from another font.  The size a button reports before its style sheet is applied is of a narrower text, and at a larger font
// the first letter and the arrow of "Skip step ▸" were cut off at its edges
static void fitButton(QPushButton* b, bool bold, int padding)
{
    b->ensurePolished();
    QFont f = b->font();
    if (bold) f.setBold(true);
    b->setMinimumWidth(QFontMetrics(f).horizontalAdvance(b->text()) + 2 * padding + 2 + 10);
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
    std::function<QRect()> aim;                         // what the arrow ends at, and is framed: a row of the tree (the arrow ends at its edge that faces
                                                        // the card, so it never crosses what it points at)
    std::function<QRect()> cardNear;                    // the card stands beside this (default: everything the user may touch in the step)
    bool closeRows = false;                             // the tree shows only what the step is about: every row with rows under it is closed ...
    QStringList openRows;                               // ... except these models
    QString group;                                      // steps of a group work on models of their own, put back when the group is left
    bool undone = false;                                // what the step made goes when it is left
    std::function<QVector<int>()> code;                 // the lines of the script (0-based) the step is about: framed, and not dimmed
    Place place = Auto;
    bool keys = false;                                  // whether the keyboard works in this step
    bool showView = false;                              // the viewport is not dimmed (nothing in it to click): the step changes a model, and the change is to be seen
    std::function<void()> enter;
    std::function<void()> leave;                        // what the step wrote into the script for itself goes when it is left
    std::function<bool()> done;                         // what the step asks of the user is done
    std::function<void(std::function<void()>)> show;    // "Show me": the program does it, once, and says when it is over
    QString showLabel = T("Show me");
    QString doneNote = T("Nice.");
    bool welcome = false;
    bool last = false;                                  // the end: Finish ends the tour
    bool exitCard = false;                              // the card a skipped tour ends with: where the tour is found again (always the last step)
    int chapter = -1;                                   // the chapter the step is in (0-based; the welcome card and the exit card are in none)
    bool chapterEnd = false;                            // the card that ends a chapter: it congratulates, and asks whether to go on or to explore alone
    bool needsTourModel = false;                        // the step works on the model of the tour (plate, drilled, hole): it is put in the editor when it is not there
};

Tutorial::Tutorial(QMainWindow* window, View* view, Editor* editor, std::function<bool()> loadTour, QObject* parent)
    : QObject(parent), m_window(window), m_view(view), m_editor(editor), m_loadTour(std::move(loadTour)),
      m_steps(new QVector<Step>)
{
    m_layer = new TourLayer(window);
    m_blocker = new TourBlocker(window);
    m_card = new QFrame(window);
    m_card->setObjectName("TourCard");
    m_card->setFixedWidth(kCardWidth);
    m_card->setStyleSheet(
        "#TourCard { background: #0d3a4a; border: 2px solid #ffcc33; border-radius: 10px; }"
        "#TourCard[done=\"true\"] { border: 2px solid #82cc58; }"
        "#TourCard QLabel { color: #eee8d5; background: transparent; }"
        "#TourCard QPushButton { color: #eee8d5; background: rgba(147, 161, 161, 40); border: 1px solid rgba(147, 161, 161, 110);"
        "  border-radius: 6px; padding: 5px 12px; font-size: 9.5pt; }"
        "#TourCard QPushButton:hover { background: rgba(147, 161, 161, 90); }"
        "#TourCard QPushButton:disabled { color: #6c8a93; border-color: rgba(147, 161, 161, 50); }"
        "#TourCard QPushButton#TourNext { background: #ffcc33; color: #10303b; border: 1px solid #ffcc33; font-weight: bold; }"
        "#TourCard QPushButton#TourNext:hover { background: #ffd966; }"
        "#TourCard[done=\"true\"] QPushButton#TourNext { background: #82cc58; border: 1px solid #82cc58; }"
        "#TourCard[done=\"true\"] QPushButton#TourNext:hover { background: #9bd873; }"
        "#TourCard QPushButton#TourSkip { background: transparent; border: none; color: #93a1a1; padding: 5px 4px; }"
        "#TourCard QPushButton#TourSkip:hover { color: #eee8d5; text-decoration: underline; }"
        "#TourCard QPushButton#TourChapters { padding: 5px 10px; }");
    auto col = new QVBoxLayout(m_card);
    col->setContentsMargins(16, 12, 16, 12);
    col->setSpacing(6);
    // (the place in the tour is a small line of its own above the title: the title may be as long as it needs and breaks into lines, never clipped)
    m_counter = new QLabel;
    m_counter->setStyleSheet("font-size: 8.5pt; color: #93a1a1;");
    col->addWidget(m_counter);
    m_title = new QLabel;
    m_title->setWordWrap(true);
    m_title->setStyleSheet("font-size: 12.5pt; font-weight: bold; color: #ffcc33;");
    col->addWidget(m_title);
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
    m_chapters = new QPushButton(T("Chapters  ▾"));
    m_chapters->setObjectName("TourChapters");
    m_chapters->setCursor(Qt::PointingHandCursor);
    m_chapters->setToolTip(T("Every chapter of the tour, in order: choose one to go to it"));
    m_skipTour = new QPushButton(T("Quit the tour"));
    m_skipTour->setObjectName("TourSkip");
    m_skipTour->setCursor(Qt::PointingHandCursor);
    m_back = new QPushButton(T("Back"));
    m_show = new QPushButton(T("Show me"));
    m_next = new QPushButton;
    m_next->setObjectName("TourNext");
    m_next->setMinimumWidth(118);
    buttons->addWidget(m_chapters);
    buttons->addWidget(m_skipTour);
    buttons->addStretch(1);
    buttons->addWidget(m_back);
    buttons->addWidget(m_show);
    buttons->addWidget(m_next);
    col->addLayout(buttons);
    for (auto b : {m_back, m_show, m_next}) b->setCursor(Qt::PointingHandCursor);
    connect(m_chapters, &QPushButton::clicked, this, [this] { showChapters(); });
    connect(m_skipTour, &QPushButton::clicked, this, [this] { skip(); });
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
            showDoneLook(true);
            m_note->setText(s.doneNote);
            m_note->setStyleSheet("font-size: 9.5pt; color: #82cc58; font-weight: bold;");
            m_show->hide();
            m_next->setText(m_index + 1 < m_steps->size() ? T("Next  ▸") : T("Finish"));
            // (the note stays until the user goes on: it says what happened, and it is read at the user's pace)
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
    state.symmetry = m_view->scenePanel()->activeSymmetryName();
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
    symmetryAgain(state.symmetry, 12);
}

void Tutorial::symmetryAgain(const QString& var, int tries)
{
    // (a symmetry that is selected is the one in use, and it stays in use when nothing is selected: what a step showed is undone by putting the one that was back)
    auto scene = m_view->scenePanel();
    if (scene->activeSymmetryName() == var) return;
    scene->setActiveSymmetry(var);
    if (scene->activeSymmetryName() != var && tries > 0) after(200, [this, var, tries] { symmetryAgain(var, tries - 1); });      // (its model is not in the tree yet)
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

void Tutorial::fakeClick(QWidget* target0, const QPoint& at, Qt::MouseButton button, std::function<void()> done, Qt::KeyboardModifiers mods)
{
    QPointer<QWidget> target(target0);
    const QPoint w = target->mapTo(m_window, at);
    m_busy = true;
    static_cast<TourLayer*>(m_layer)->cursorKey = (mods & Qt::ControlModifier) ? "Ctrl" : QString();
    setCursor(w);
    sendMouse(target, QEvent::MouseMove, at, Qt::NoButton, Qt::NoButton, mods);
    after(500, [=] {
        if (!target) { m_busy = false; hideCursor(); return; }
        setCursor(w, true);
        sendMouse(target, QEvent::MouseButtonPress, at, button, button, mods);
        after(140, [=] {
            if (target) sendMouse(target, QEvent::MouseButtonRelease, at, button, Qt::NoButton, mods);
            setCursor(w, false);
            after(350, [=] { m_busy = false; if (done) done(); });
        });
    });
}

void Tutorial::typeOver(int line0, int col0, int col1, const QString& text, std::function<void()> done, int msPerCharacter)
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
                after(msPerCharacter * i, [=] {
                    m_editor->applyEdits({TextEdit{line0, col0, line0, col0 + length, part}}, "Tour");
                });
                shown = i;
            }
            after(msPerCharacter * text.size() + 500, [=] { m_busy = false; hideCursor(); if (done) done(); });
        });
    });
}

////////////////////////////////////////////////////////////////////////////////
// The steps

// The first lines of a model that a step makes for itself (the render region, how fine it is drawn, how smooth)
static QString tourHeader(const QString& bounds, int resolution = 4)
{
    return QString("# The guided tour's model\nfrom fieldes import *\n\nview.set_bounds(%1, auto_scale=True)\nview.set_resolution(%2)\nview.set_quality(8)\n\n")
        .arg(bounds.isEmpty() ? QString("[-35, -25, -8], [35, 25, 30]") : bounds)
        .arg(resolution);
}

// The same, with the region as it is written: it is not fitted to what is shown (a flow shows its post, but the water round it is what the region is to hold)
static QString tourHeaderFixed(const QString& bounds, int resolution = 4)
{
    return QString("# The guided tour's model\nfrom fieldes import *\n\nview.set_bounds(%1, auto_scale=False)\nview.set_resolution(%2)\nview.set_quality(8)\n\n").arg(bounds).arg(resolution);
}

void Tutorial::useScript(const QString& text, bool frame)
{
    // The camera is on the model's region at once (the region the script sets is known from its text): a step that puts its own model there starts zoomed out, not
    // zoomed in on what the step before had, and only later (below, once the script has run) framed more closely
    static const QRegularExpression bounds(R"(set_bounds\(\s*\[([^\]]*)\]\s*,\s*\[([^\]]*)\])");
    if (const auto m = bounds.match(text); frame && m.hasMatch())
    {
        const QStringList lo = m.captured(1).split(','), hi = m.captured(2).split(',');
        if (lo.size() == 3 && hi.size() == 3)
            m_view->zoomToBox(QVector3D(lo[0].toFloat(), lo[1].toFloat(), lo[2].toFloat()), QVector3D(hi[0].toFloat(), hi[1].toFloat(), hi[2].toFloat()));
    }
    const QString now = m_editor->getScript();
    if (now == text) return;
    const QStringList lines = now.split('\n');
    m_editor->applyEdits({TextEdit{0, 0, int(lines.size()) - 1, int(lines.last().size()), text}}, "Tour");
    // (the lines a step puts there are not changes the user made: they do not glow)
    m_scriptSeen = text.split('\n');
    m_changed.clear();
    // The model of another step is framed in the view -- the camera is where the step before left it, and a small model can stand half out of the picture -- once the
    // script has run and set the region it is in (again when the region has been fitted to the model); not when the user has taken the camera in hand since
    m_viewTouched = false;
    for (const int ms : {1200, 3500})
        after(ms, [this] { if (!m_viewTouched) m_view->zoomTo(); });
}

void Tutorial::buildSteps()
{
    auto scene = [this] { return m_view->scenePanel(); };
    auto holesOf = [this](std::function<QWidget*()> w) {
        return [this, w]() -> QVector<QRect> { const QRect r = widgetRect(w()); return r.isEmpty() ? QVector<QRect>() : QVector<QRect>{r}; };
    };
    auto scriptWidget = [this]() -> QWidget* { return m_editor->scriptWidget(); };
    auto viewWidget = [this]() -> QWidget* { return m_view; };
    auto panelWidget = [scene]() -> QWidget* { return scene(); };
    // (the row of the tree a step points at: where it is now, once it is in view -- the arrow ends at its edge, and it is framed)
    auto aimRow = [this, scene](QStringList prefixes) {
        return [this, scene, prefixes]() -> QRect {
            for (const QString& p : prefixes)
            {
                const QRect r = scene()->visibleRowRect(p, m_window);
                if (!r.isEmpty()) return r;
            }
            return QRect();
        };
    };
    auto treeNear = [this, scene]() -> QRect { return widgetRect(scene()); };
    // (the tree and the code editor together: a step that is about a row and the line it is in)
    auto treeAndScript = [this, scene, scriptWidget]() -> QVector<QRect> {
        QVector<QRect> out;
        for (QWidget* w : {static_cast<QWidget*>(scene()), scriptWidget()})
        {
            const QRect r = widgetRect(w);
            if (!r.isEmpty()) out << r;
        }
        return out;
    };
    // (the tree, and the tab of the properties that comes out of its right edge)
    auto treeWithTab = [this, scene]() -> QVector<QRect> {
        const QRect r = widgetRect(scene());
        return r.isEmpty() ? QVector<QRect>() : QVector<QRect>{r.adjusted(0, 0, 34, 0)};
    };
    // The model of the steps that work on a field: a plate, a point, a field made of the point -- how far every place is from it -- and the plate
    // grown by it (`linked`: by the field; else by 1)
    auto fieldModel = [](bool linked) {
        return QString("block = box((-30, -20, 0), (30, 20, 6))\n"
                       "anchor = point(24, 12, 6)\n"
                       "anchor\n"
                       "swell = ramp(distance_to_point(anchor), (0, 50), (4.0, 0.3))\n"
                       "grown = offset(block, ") + (linked ? "swell" : "1.0") + ")\ngrown";
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

    // A call is written one argument to a line, so a statement is several lines: where the one that starts with `prefix` ends (the line its parentheses are closed on), and
    // its text.  A step that asks for something in a call looks at all of it, not at its first line
    struct Statement { int first = -1, last = -1; QString text; };
    auto statementAt = [this](const QString& prefix) -> Statement {
        Statement out;
        const QStringList lines = m_editor->getScript().split('\n');
        for (int i = 0; i < lines.size() && out.first < 0; ++i)
            if (lines[i].startsWith(prefix)) out.first = i;
        if (out.first < 0) return out;
        int depth = 0;
        for (int i = out.first; i < lines.size(); ++i)
        {
            out.last = i;
            out.text += (i > out.first ? "\n" : "") + lines[i];
            for (const QChar c : lines[i])
            {
                if (c == '(' || c == '[') ++depth;
                else if (c == ')' || c == ']') --depth;
            }
            if (depth <= 0) break;
        }
        return out;
    };
    // (the lines of that statement, and of the line that shows the model under it: what a step frames in the code)
    auto statementLines = [statementAt](const QString& prefix) {
        return [statementAt, prefix]() -> QVector<int> {
            QVector<int> out;
            const Statement s = statementAt(prefix);
            for (int i = s.first; s.first >= 0 && i <= s.last; ++i) out << i;
            return out;
        };
    };
    // (the first number of a call that is written var(...): the one a step asks the user to change, and its place in the text of its line)
    auto firstVarOf = [](const QString& line, double* value, int* from, int* to) {
        static const QRegularExpression re(R"(var\(\s*(-?[\d.]+)\s*\))");
        const auto m = re.match(line);
        if (!m.hasMatch()) return false;
        if (value) *value = m.captured(1).toDouble();
        if (from) *from = m.capturedStart(1);
        if (to) *to = m.capturedEnd(1);
        return true;
    };
    // (the first number a call is given, written var(5) or 5 alike: typing a number over the row's var(5) leaves a plain number, which is the user's change all the same)
    auto firstNumberOf = [](const QString& line, double* value) {
        static const QRegularExpression re(R"(^\w+\s*=\s*\w+\(\s*(?:var\(\s*)?(-?[\d.]+))");
        const auto m = re.match(line);
        if (!m.hasMatch()) return false;
        if (value) *value = m.captured(1).toDouble();
        return true;
    };

    // (the tree and the viewport together: a menu is opened from either)
    auto bothAndScript = [this, scene, scriptWidget]() -> QVector<QRect> {
        QVector<QRect> out;
        for (QWidget* w : {static_cast<QWidget*>(scene()), static_cast<QWidget*>(m_view), scriptWidget()})
        {
            const QRect r = widgetRect(w);
            if (!r.isEmpty()) out << r;
        }
        return out;
    };
    auto both = [this, scene]() -> QVector<QRect> {
        QVector<QRect> out;
        for (QWidget* w : {static_cast<QWidget*>(scene()), static_cast<QWidget*>(m_view)})
        {
            const QRect r = widgetRect(w);
            if (!r.isEmpty()) out << r;
        }
        return out;
    };
    // (the entries of an open context menu chosen one after the other, each lit before its submenu opens: what a hand does; `top` is the menu
    // that was opened, closed when the last entry has been triggered)
    auto walk = std::make_shared<std::function<void(QPointer<QMenu>, QPointer<QMenu>, QStringList, std::function<void()>)>>();
    *walk = [this, walk](QPointer<QMenu> menu, QPointer<QMenu> top, QStringList path, std::function<void()> over) {
        if (!menu || path.isEmpty()) { if (top) top->close(); over(); return; }
        QAction* found = nullptr;
        for (QAction* a : menu->actions())
        {
            if (a->text().remove('&') == path.first()) found = a;
        }
        if (!found) { if (top) top->close(); over(); return; }
        menu->setActiveAction(found);
        if (path.size() == 1)
        {
            QPointer<QAction> act(found);
            after(900, [menu, top, act, over] {
                if (menu && act) act->trigger();
                if (top) top->close();
                over();
            });
            return;
        }
        if (!found->menu()) { if (top) top->close(); over(); return; }
        QPointer<QMenu> sub = found->menu();
        sub->popup(menu->mapToGlobal(menu->actionGeometry(found).topRight()));
        const QStringList rest = path.mid(1);
        after(800, [walk, sub, top, rest, over] { (*walk)(sub, top, rest, over); });
    };

    // ---- What the steps that build something with the right-click menus work with (chapters 3 and 7 to 12)
    // (the statement that makes a model with the call `fn`: `name = fn(` and what follows it, up to the bracket that closes it)
    auto callAt = [this](const QString& fn) -> Statement {
        Statement out;
        const QStringList lines = m_editor->getScript().split('\n');
        const QRegularExpression re("^\\w+\\s*=\\s*" + QRegularExpression::escape(fn) + "\\(");
        for (int i = 0; i < lines.size() && out.first < 0; ++i)
            if (re.match(lines[i]).hasMatch()) out.first = i;
        if (out.first < 0) return out;
        int depth = 0;
        for (int i = out.first; i < lines.size(); ++i)
        {
            out.last = i;
            out.text += (i > out.first ? "\n" : "") + lines[i];
            for (const QChar c : lines[i])
            {
                if (c == '(' || c == '[') ++depth;
                else if (c == ')' || c == ']') --depth;
            }
            if (depth <= 0) break;
        }
        return out;
    };
    auto callLines = [callAt](const QString& fn) {
        return [callAt, fn]() -> QVector<int> {
            QVector<int> out;
            const Statement st = callAt(fn);
            for (int i = st.first; st.first >= 0 && i <= st.last; ++i) out << i;
            return out;
        };
    };
    auto balanced = [](const QString& text) {
        int depth = 0;
        for (const QChar c : text)
        {
            if (c == '(' || c == '[') ++depth;
            else if (c == ')' || c == ']') --depth;
        }
        return depth == 0;
    };
    // (a call of that name is in the script; and: it is whole -- its brackets close and no placeholder is left in it)
    auto made = [callAt](const QString& fn) { return callAt(fn).first >= 0; };
    auto madeAndFilled = [callAt, balanced](const QString& fn) {
        const Statement st = callAt(fn);
        return st.first >= 0 && balanced(st.text) && !st.text.contains("...");
    };
    // (the text of a call with its spaces and line breaks taken out: what is compared, so that the way a call is laid out does not matter)
    auto flatCall = [callAt](const QString& fn) {
        QString flat = callAt(fn).text;
        flat.remove(QRegularExpression("\\s"));
        return flat;
    };
    // (the script has these lines: a step that starts from a model of its own does not put it there again when the user has it already)
    auto hasLines = [this](const QStringList& prefixes) {
        for (const QString& p : prefixes)
            if (scriptLine(p) < 0) return false;
        return true;
    };
    // (a model hidden as the eye of its row does it: the line that shows it, its name on a line of its own, becomes `# hidden: name` -- two results drawn in one place would fight)
    auto hideModel = [this](const QString& var) {
        const QStringList lines = m_editor->getScript().split('\n');
        const QRegularExpression bare("^(\\s*)" + QRegularExpression::escape(var) + "\\s*$");
        for (int i = 0; i < lines.size(); ++i)
        {
            const auto m = bare.match(lines[i]);
            if (!m.hasMatch()) continue;
            m_editor->applyEdits({TextEdit{i, 0, i, int(lines[i].size()), m.captured(1) + "# hidden: " + var}}, "Tour");
            m_scriptSeen = m_editor->getScript().split('\n');
            m_changed.clear();
            return;
        }
    };
    // (the models of the tree selected one after the other as a hand does it -- every one with Ctrl, which selects without the camera going to the row)
    auto selectRows = std::make_shared<std::function<void(QStringList, std::function<void()>)>>();
    *selectRows = [this, scene, selectRows](QStringList rows, std::function<void()> then) {
        if (rows.isEmpty()) { then(); return; }
        const QRect r = scene()->rowRect(rows.first(), m_window);
        QWidget* vp = scene()->treeViewport();
        if (r.isEmpty() || !vp) { then(); return; }
        const QStringList rest = rows.mid(1);
        fakeClick(vp, vp->mapFrom(m_window, QPoint(r.left() + 70, r.center().y())), Qt::LeftButton,
                  [selectRows, rest, then] { (*selectRows)(rest, then); }, Qt::ControlModifier);
    };
    // (the first row is right-clicked, and the entries of `path` are chosen in the menu that opens; no row: the empty space of the viewport is right-clicked, where the menu of what is new opens)
    auto rightClickAndChoose = [this, scene, walk](QString row, QStringList path, std::function<void()> over) {
        auto choose = [this, walk, path, over] {
            auto root = qobject_cast<QMenu*>(openMenu());
            if (!root) { over(); return; }
            (*walk)(root, root, path, over);
        };
        if (row.isEmpty())
        {
            fakeClick(m_view, QPoint(int(m_view->width() * 0.12), int(m_view->height() * 0.80)), Qt::RightButton, choose);
            return;
        }
        const QRect r = scene()->rowRect(row, m_window);
        QWidget* vp = scene()->treeViewport();
        if (r.isEmpty() || !vp) { over(); return; }
        const QPoint at = vp->mapFrom(m_window, QPoint(r.left() + 70, r.center().y()));
        const QPoint w = vp->mapTo(m_window, at);
        QPointer<QWidget> viewport(vp);
        m_busy = true;
        glide(m_cursorShown ? m_cursor : w + QPoint(80, 50), w, 600, [this](QPoint wp) { setCursor(wp); }, [this, viewport, at, w, choose, over] {
            if (!viewport) { m_busy = false; hideCursor(); over(); return; }
            setCursor(w, true);
            QContextMenuEvent ev(QContextMenuEvent::Mouse, at, viewport->mapToGlobal(at));
            QApplication::sendEvent(viewport, &ev);
            after(400, [this, w, choose] { setCursor(w, false); m_busy = false; hideCursor(); choose(); });
        });
    };
    // (what a user does to ask a menu for something: select the models it is about, right-click the first, choose the entry -- and the camera shows all of what was made)
    auto showPick = [this, selectRows, rightClickAndChoose](QStringList rows, QStringList path, std::function<void()> over) {
        auto choose = [this, rows, path, over, rightClickAndChoose] {
            rightClickAndChoose(rows.isEmpty() ? QString() : rows.first(), path, [this, over] {
                after(1200, [this] { m_view->zoomTo(); });
                over();
            });
        };
        (*selectRows)(rows, choose);
    };
    // (the three dots of a call that was made with a placeholder, typed over with `text`; the editor shows the line: a call that holds a placeholder is open)
    auto fillDots = [this, callAt](const QString& fn, const QString& text, std::function<void()> over, int msPerCharacter) {
        const Statement st = callAt(fn);
        if (st.first < 0) { over(); return; }
        const QStringList lines = st.text.split('\n');
        for (int k = 0; k < lines.size(); ++k)
        {
            const int dots = lines[k].indexOf("...");
            if (dots < 0) continue;
            typeOver(st.first + k, dots, dots + 3, text, over, msPerCharacter);
            return;
        }
        over();
    };
    // (the first group of `pattern` in the call `fn`, typed over with `text`)
    auto typeInCall = [this, callAt](const QString& fn, const QString& pattern, const QString& text, std::function<void()> over) {
        const Statement st = callAt(fn);
        if (st.first < 0) { over(); return; }
        const QStringList lines = st.text.split('\n');
        const QRegularExpression re(pattern);
        for (int k = 0; k < lines.size(); ++k)
        {
            const auto m = re.match(lines[k]);
            if (!m.hasMatch() || m.capturedStart(1) < 0) continue;
            typeOver(st.first + k, m.capturedStart(1), m.capturedEnd(1), text, over);
            return;
        }
        over();
    };
    // (what the steps that build something show: the tree and the viewport and the code, the card in the lower left of the viewport where the result is not)
    auto buildLook = [this, bothAndScript](Step& s) {
        s.holes = bothAndScript;
        s.cardNear = [this]() { return widgetRect(m_view); };
        s.place = InsideBottomLeft;
        s.showView = true;
        s.closeRows = true;
        s.keys = true;
    };

    // (the call is shown open in the code too, as its row is in the tree: a call the menu wrote has twenty lines of arguments and is folded, and the step is about one of them)
    auto openCall = [this, callAt](const QString& fn) {
        after(500, [this, callAt, fn] {
            const Statement st = callAt(fn);
            Script* sc = m_editor->scriptWidget();
            if (st.first >= 0 && sc && sc->isFolded(st.first)) sc->setFolded(st.first, false);
        });
    };

    QVector<Step>& steps = *m_steps;
    // The steps are written below in the order they were made in; each says which chapter it is in (`currentChapter`, set where the steps of another chapter begin), and
    // they are put in the order of the chapters at the end (a stable sort: the order inside a chapter is the order they are written in)
    int currentChapter = -1;
    auto add = [&](const Step& step) {
        Step s = step;
        s.chapter = currentChapter;
        // (the first two chapters work on the model of the tour -- plate, drilled, hole -- unless a step makes a model of its own)
        s.needsTourModel = s.needsTourModel || (s.group.isEmpty() && (currentChapter == 1 || currentChapter == 2));
        steps.push_back(s);
    };

    // 0
    {
        Step s;
        s.title = T("Thank you for downloading FielDes");
        s.text = T("FielDes stands for Field driven Design, which means that it wants you to design clean, organic geometry through the power of math. "
                   "It is most useful for post processing steps like simulations or topology optimizations of imported CAD files or geometries you modelled directly "
                   "in FielDes itself. The data from those steps creates fields that can directly improve your designs.")
                 + "\n\n" + T("Happy designing, and I hope FielDes can make your workflows faster and more intuitive.")
                 + "\n\n" + "-Tino";
        s.hint = T("The tour has twelve chapters. Each ends with a choice: go on, or explore on your own. Help ▸ Guided tour starts it again where you stopped.");
        s.welcome = true;
        s.place = Center;
        add(s);
    }
    // 1
    {
        currentChapter = 0;                 // (chapter 1: how FielDes works -- the script is the model, and fields)
        Step s;
        s.title = T("A model is a recipe");
        s.needsTourModel = true;            // (it works on the model of the tour: plate, drilled, hole)
        s.focus = plateLine;
        s.text = T("A FielDes model is not a pile of faces and edges. It is a short recipe, written as a script: a plate, a hole, one taken out of the other. Every number in it is a dimension of "
                   "the design, and everything you see in the program -- the picture, the model tree -- is made from these lines.");
        s.hint = T("Because the recipe stays in front of you, a design can be read, changed, shared and reused.");
        s.holes = holesOf(scriptWidget);
        s.point = [this] { const QRect r = scriptLineRect(std::max(0, scriptLine("plate ="))); return r.isEmpty() ? QPoint() : QPoint(r.right() - 20, r.center().y()); };
        s.place = Right;
        s.enter = [this] { m_editor->scriptWidget()->goToLine(std::max(0, scriptLine("plate =")), false); };
        add(s);
    }
    // 2
    {
        Step s;
        s.title = T("Change a number, change the design");
        s.needsTourModel = true;
        s.focus = plateLine;
        s.text = T("Because the model is a recipe, a design change is a change of a number. Make the plate thicker -- in the plate line, change the 6 to 12 -- and the whole model follows: "
                   "nothing is rebuilt by hand, and nothing breaks.");
        s.hint = T("Click in the script and type, or press Show me.");
        s.holes = holesOf(scriptWidget);
        s.point = [this] { const QRect r = scriptLineRect(std::max(0, scriptLine("plate ="))); return r.isEmpty() ? QPoint() : QPoint(r.right() - 20, r.center().y()); };
        s.place = Right;
        s.keys = true;
        s.enter = [this] {
            m_baseline = plateHeight();
            m_editor->scriptWidget()->goToLine(std::max(0, scriptLine("plate =")), false);
        };
        s.done = [this] { bool ok = false; const double v = plateHeight(&ok); return ok && std::abs(v - m_baseline) > 1e-9; };
        s.showView = true;
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
        currentChapter = 1;                 // (chapter 2: moving around)
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
        // (chapter 2 goes on: the standard views, the way the mouse moves the view, and the panes that can be pulled out into windows)
        Step s;
        s.title = T("Standard views");
        s.text = T("To look at a model from a fixed side, press 1 for the front, 3 for the right and 7 for the top (View ▸ Standard views), and Home to frame everything. "
                   "A view changes only where you look from: it is never part of the model.");
        s.hint = T("Clicking an axis of the little triad in the corner looks along it. Or press Show me.");
        s.holes = holesOf(viewWidget);
        s.place = InsideBottom;
        s.keys = true;
        s.showView = true;
        s.enter = [this] { m_view->standardView(View::VIEW_ISO); };
        s.show = [this](std::function<void()> over) {
            m_busy = true;
            auto* layer = static_cast<TourLayer*>(m_layer);
            layer->cursorKey = "1";
            setCursor(m_view->mapTo(m_window, QPoint(m_view->width() / 2, m_view->height() / 2)));
            after(600, [this, over, layer] {
                m_view->standardView(View::VIEW_FRONT);
                after(1500, [this, over, layer] {
                    layer->cursorKey = "7";
                    m_view->standardView(View::VIEW_TOP);
                    after(1500, [this, over, layer] {
                        layer->cursorKey.clear();
                        m_view->standardView(View::VIEW_ISO);
                        m_busy = false;
                        hideCursor();
                        over();
                    });
                });
            });
        };
        add(s);
    }
    {
        Step s;
        s.title = T("Your own navigation");
        s.text = T("Settings ▸ Navigation chooses how the mouse moves the view: the default (Shift and the left button turn it), a Blender-like set, or your own. "
                   "The tour keeps the default, so everything it says about the mouse is true for it.");
        s.hint = T("The choice is kept for you; changing it never changes a model.");
        s.menuTitle = "Settings";
        s.menuEntry = "Navigation";
        s.place = Right;
        s.enter = [] { while (auto m = QApplication::activePopupWidget()) m->close(); };
        add(s);
    }
    {
        Step s;
        s.title = T("Make room for your work");
        s.text = T("Every pane -- the code editor, the console, the model tree, the viewport -- has a small button in its top corner that pulls it out into a window of its own: the viewport "
                   "on a second screen, say, and the rest takes the room it left. The same button puts it back.");
        s.hint = T("The panes you pull out stay where you left them the next time FielDes opens.");
        s.showView = true;
        s.place = Right;
        // (the viewport's own button is framed, and the arrow ends at it: it is not to be pressed in the tour)
        s.aim = [this]() -> QRect {
            auto* b = m_view->findChild<QToolButton*>("PopOutButton");
            // (the button sits in the very corner of the window: the frame stays inside it, so no edge of the frame is cut off)
            return b && b->isVisible() ? widgetRect(b).adjusted(-4, -4, 4, 4).intersected(m_window->rect().adjusted(2, 2, -2, -2)) : QRect();
        };
        add(s);
    }
    {
        currentChapter = 2;
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
    // 5: the model tree, and selecting a model -- which comes before everything that is done to one
    {
        Step s;
        s.title = T("The model tree");
        s.text = T("Every model the script makes is listed here, each kind with its own icon. Click drilled to select it.");
        s.hint = T("A click selects a model: it is lit in the viewport, and its line in the code is framed. Everything you do next is done to what is selected.");
        s.code = linesWith("^drilled\\b");
        s.holes = holesOf(panelWidget);
        s.aim = aimRow({"drilled"});
        s.place = Right;
        s.closeRows = true;
        s.undone = true;                                // (the position, rotation and scale that selecting writes into the call go with the step)
        s.showView = true;
        s.enter = [scene] { scene()->showSettings(false); scene()->clearSelection(); };
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
    // 7
    {
        currentChapter = 1;                 // (chapter 2: the gizmo moves models, as the mouse moves the view)
        Step s;
        s.title = T("The gizmo");
        s.text = T("A selected model has a gizmo. Drag an arrow to move the model.");
        s.hint = T("The gizmo moves the model by its position, which is written in the call of the model (position=): its numbers change as you drag. Or press Show me.");
        s.code = linesWith("position=|rotation=|scale=");
        s.holes = holesOf(viewWidget);
        s.place = InsideBottom;
        s.closeRows = true;
        s.undone = true;
        s.enter = [this] {
            selectAgain({"drilled"}, 12);               // (once it is in the tree: a step reached by a jump or with Back may find the tree of another model)
        };
        s.point = [this] { QPoint p; return m_view->gizmoGripPoint(0, 0, p) ? m_view->mapTo(m_window, p) : QPoint(); };
        // (done when the numbers of the position are not the ones it was written with: the model was moved by its arrow, or a plane square)
        s.done = [this] {
            static const QRegularExpression move(R"(position=\(\s*var\(([^)]*)\)\s*,\s*var\(([^)]*)\)\s*,\s*var\(([^)]*)\))");
            const auto m = move.match(m_editor->getScript());
            if (!m.hasMatch()) return false;
            for (int i = 1; i <= 3; ++i)
                if (std::abs(m.captured(i).trimmed().toDouble()) > 0.05) return true;
            return false;
        };
        s.doneNote = T("Nice. The model moved, and the numbers of its position changed with it.");
        s.show = [this](std::function<void()> over) {
            QPoint p;
            if (!m_view->gizmoGripPoint(0, 0, p)) { over(); return; }
            fakeDrag(m_view, p, p + QPoint(60, 0), 800, over);
        };
        add(s);
    }
    // 7: the other gizmo, with Shift (right after the gizmo: it is the same tool)
    {
        Step s;
        s.title = T("Hold Shift");
        s.text = T("Hold Shift with a model selected: the arrows become flat squares that move it along a plane, and the three scale squares become one gold ring that scales it evenly.");
        s.hint = T("Release Shift for the normal gizmo. Or press Show me.");
        s.holes = holesOf(viewWidget);
        s.place = InsideBottom;
        s.closeRows = true;
        s.undone = true;
        s.enter = [this] {
            while (auto m = QApplication::activePopupWidget()) m->close();
            m_view->holdShift(false);
            m_view->standardView(View::VIEW_ISO);   // (seen from the side: a view dead on the z axis hides the squares that lie along it)
            selectAgain({"drilled"}, 12);           // (the model of the gizmo's step, selected again once it is in the tree)
        };
        s.show = [this](std::function<void()> over) {
            m_busy = true;
            static_cast<TourLayer*>(m_layer)->cursorKey = "Shift";
            setCursor(m_view->mapTo(m_window, QPoint(m_view->width() / 2, m_view->height() / 2)));
            after(600, [this, over] {
                m_view->holdShift(true);
                after(2600, [this, over] { m_view->holdShift(false); m_busy = false; hideCursor(); over(); });
            });
        };
        s.leave = [this] { m_view->holdShift(false); };
        add(s);
    }
    // 8: what a call is made of: its arguments are rows of the tree -- models (its children) and numbers; a number among them is typed over in the code, and the model is seen to change
    {
        currentChapter = 2;
        Step s;
        s.title = T("Models inside models");
        s.text = T("drilled is made of plate and hole. Everything a call is made of is an argument, and each one is a row under the model: the models are its children, the numbers are "
                   "the other rows. Click the r row under hole -- its number is selected in the code -- and type another number over it, 9 say, and watch the hole.");
        s.hint = T("The models a call is made of are its children; the other arguments are these dim rows. Or press Show me.");
        s.code = linesWith("^hole =");
        s.holes = holesOf(panelWidget);
        s.aim = aimRow({"r = "});
        s.place = Right;
        s.keys = true;
        s.showView = true;
        s.closeRows = true;
        s.openRows = QStringList{"drilled", "hole"};
        s.enter = [this, scene, statementAt, firstNumberOf] {
            scene()->clearSelection();
            const Statement st = statementAt("hole =");
            double v = 0;
            m_baseline = (st.first >= 0 && firstNumberOf(st.text.section('\n', 0, 0), &v)) ? v : 0;
            QTextCursor c = m_editor->scriptWidget()->textCursor();
            c.clearSelection();
            m_editor->scriptWidget()->setTextCursor(c);
        };
        s.done = [this, statementAt, firstNumberOf] {
            const Statement st = statementAt("hole =");
            double v = 0;
            return st.first >= 0 && firstNumberOf(st.text.section('\n', 0, 0), &v) && std::abs(v - m_baseline) > 1e-9;
        };
        s.doneNote = T("Nice. The hole is bigger: the number in the code is what the viewport draws.");
        s.show = [this, scene, statementAt, firstVarOf](std::function<void()> over) {
            const QRect r = scene()->rowRect("r = ", m_window);
            const Statement st = statementAt("hole =");
            int from = 0, to = 0;
            if (r.isEmpty() || st.first < 0 || !firstVarOf(st.text.section('\n', 0, 0), nullptr, &from, &to)) { over(); return; }
            QWidget* vp = scene()->treeViewport();
            const int line = st.first;
            fakeClick(vp, vp->mapFrom(m_window, QPoint(r.left() + 100, r.center().y())), Qt::LeftButton, [this, line, from, to, over] {
                typeOver(line, from, to, "9", over);
            });
        };
        add(s);
    }
    // 10: rename
    {
        Step s;
        s.title = T("Rename");
        s.text = T("Double-click a name in the tree to rename it. Try hole -- call it bore.");
        s.hint = T("Every line that uses the name is framed, and changes with it. Or press Show me.");
        s.code = linesWith("\\b(hole|bore)\\b");
        s.holes = holesOf(panelWidget);
        s.aim = aimRow({"hole", "bore"});
        s.place = Right;
        s.keys = true;
        s.closeRows = true;
        s.openRows = QStringList{"drilled"};
        s.done = [this] { return !m_editor->getScript().contains(QRegularExpression("\\bhole\\b")); };
        s.doneNote = T("Nice. Every use of the name changed.");
        s.show = [this, scene](std::function<void()> over) {
            const QRect r = scene()->rowRect("hole", m_window);
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
                    const QString name = "bore";
                    if (QLineEdit* f = editor()) f->selectAll();
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
    // 11
    {
        Step s;
        s.title = T("Make things");
        s.text = T("Right-click empty space, choose Primitives ▸ New 3D shape ▸ sphere.");
        s.hint = T("Or press Show me.");
        s.holes = holesOf(viewWidget);
        s.place = InsideTopRight;
        s.closeRows = true;
        s.undone = true;
        s.enter = [this, scene] {
            scene()->clearSelection();
            m_baseline = m_editor->getScript().count("sphere(");
        };
        s.done = [this] { return m_editor->getScript().count("sphere(") > m_baseline; };
        s.doneNote = T("Nice. The new line is in the script.");
        s.show = [this, walk](std::function<void()> over) {
            const QPoint at(int(m_view->width() * 0.12), int(m_view->height() * 0.80));
            fakeClick(m_view, at, Qt::RightButton, [this, walk, over] {
                // the menu is open: its entries lit one after the other, the last one chosen
                auto root = qobject_cast<QMenu*>(openMenu());
                if (!root) { over(); return; }
                (*walk)(root, root, QStringList{T("Primitives"), T("New 3D shape"), "sphere"}, over);
            });
        };
        add(s);
    }
    // 11a: the control of the primitives -- which shapes the Primitives menus offer first, a name of one's own for them, and one's own primitives
    {
        Step s;
        s.title = T("Your primitives");
        s.text = T("The Primitives menu in the menu bar is the control for the shapes the right-click menus offer first. Primitives ▸ Manage primitives... lists them kind by kind -- 3D shapes, 2D shapes, "
                   "lines, surfaces, fields: tick a shape to keep it under Primitives, untick it to move it to the Parametric shapes, and Rename gives the selected one another name. "
                   "With a model selected in the tree, Add selection makes it a primitive of your own, there in every script.");
        s.hint = T("The window opens on the kind of the model that is selected. Or press Show me.");
        s.menuTitle = "Primitives";
        s.menuEntry = "Manage primitives...";
        s.place = Right;
        s.enter = [] { while (auto m = QApplication::activePopupWidget()) m->close(); };
        s.leave = [this] { if (auto d = m_window->findChild<QDialog*>("PrimitivesDialog")) d->close(); };
        s.show = [this](std::function<void()> over) {
            // (the entry is chosen, and the window opens on the kind of what is selected; a moment later it is closed again)
            m_busy = true;
            for (QAction* a : m_window->findChildren<QAction*>())
            {
                if (a->text() == T("Manage primitives...")) { a->trigger(); break; }
            }
            after(2600, [this, over] {
                if (auto d = m_window->findChild<QDialog*>("PrimitivesDialog")) d->close();
                m_busy = false;
                over();
            });
        };
        add(s);
    }
    // 11: a placeholder -- what it is, made from the right-click menu, and filled in: asked and done in one step
    {
        // (the right-click menu of drilled, its entries chosen one after the other: Operations > Offsets and walls > offset)
        auto showMake = [this, scene, walk](std::function<void()> over) {
                const QRect r = scene()->rowRect("drilled", m_window);
                if (r.isEmpty()) { over(); return; }
                QWidget* vp = scene()->treeViewport();
                const QPoint at = vp->mapFrom(m_window, QPoint(r.left() + 70, r.center().y()));
                const QPoint w = vp->mapTo(m_window, at);
                QPointer<QWidget> viewport(vp);
                m_busy = true;
                setCursor(w);
                after(500, [this, viewport, at, w, walk, over] {
                    if (!viewport) { m_busy = false; hideCursor(); over(); return; }
                    setCursor(w, true);
                    QContextMenuEvent ev(QContextMenuEvent::Mouse, at, viewport->mapToGlobal(at));
                    QApplication::sendEvent(viewport, &ev);
                    after(400, [this, walk, w, over] {
                        setCursor(w, false);
                        m_busy = false;
                        hideCursor();
                        auto root = qobject_cast<QMenu*>(openMenu());
                        if (!root) { over(); return; }
                        (*walk)(root, root, QStringList{T("Operations"), T("Offsets and walls"), "offset"}, over);
                    });
                });
        };
        // (the three dots of the call it made, typed over with 1)
        auto showFill = [this, statementAt](std::function<void()> over) {
                const Statement st = statementAt("offset_1 =");
                if (st.first < 0) { over(); return; }
                const QStringList lines = st.text.split('\n');
                for (int k = 0; k < lines.size(); ++k)
                {
                    const int dots = lines[k].indexOf("...");
                    if (dots < 0) continue;
                    typeOver(st.first + k, dots, dots + 3, "1", over);
                    return;
                }
                over();
        };
        Step s;
        s.title = T("Placeholders");
        s.text = T("A call that is missing something has a placeholder (...) in its place. Right-click drilled and choose Operations ▸ Offsets and walls ▸ offset: an amber row marks "
                   "the placeholder. Click it and type a number over the three dots, 1 for instance.");
        s.hint = T("The note under the tree lists every placeholder. Nothing runs while one is left; a row that holds a placeholder stays open. Or press Show me.");
        s.code = statementLines("offset_1 =");
        s.holes = treeAndScript;
        s.cardNear = treeNear;
        // (the amber row once there is one; the row of drilled, which is right-clicked, before)
        auto drilledRow = aimRow({"drilled"});
        s.aim = [this, scene, drilledRow]() {
            const QRect r = scene()->placeholderRect(m_window);
            return r.isEmpty() ? drilledRow() : r;
        };
        s.place = Right;
        s.keys = true;
        s.showView = true;
        s.closeRows = true;
        s.enter = [this, scene] {
            while (auto m = QApplication::activePopupWidget()) m->close();
            scene()->clearSelection();
        };
        // (a call is written one argument to a line, so the whole statement is looked at: made, and no dots left)
        s.done = [statementAt] {
            const Statement st = statementAt("offset_1 =");
            return st.first >= 0 && !st.text.contains("...");
        };
        s.doneNote = T("Nice. The model is made.");
        // (made, a moment for the script to run and the amber row to come, and filled in)
        s.show = [this, showMake, showFill](std::function<void()> over) {
            showMake([this, showFill, over] { after(1800, [showFill, over] { showFill(over); }); });
        };
        // (what the step made is the step's own: it goes when the step is left -- the call and the line that shows it)
        s.leave = [this, statementAt] {
            for (int tries = 0; tries < 4; ++tries)
            {
                const Statement st = statementAt("offset_1");
                if (st.first < 0) return;
                const QStringList lines = m_editor->getScript().split('\n');
                if (st.last + 1 < lines.size()) m_editor->applyEdits({TextEdit{st.first, 0, st.last + 1, 0, QString()}}, "Tour");
                else if (st.first > 0) m_editor->applyEdits({TextEdit{st.first - 1, int(lines[st.first - 1].size()), st.last, int(lines[st.last].size()), QString()}}, "Tour");
                else return;
            }
        };
        add(s);
    }
    // 14: sections
    {
        Step s;
        s.title = T("Sections");
        s.text = T("Group the script in sections: move the mouse between two rows -- here, above drilled. A small + in a circle shows. Click the circle: a section starts there. Type its name, then press Enter.");
        s.hint = T("A section is a comment, # SECTION name: it folds in the code editor too. Double-click a section's name to rename it. Or press Show me.");
        s.code = linesWith("^\\s*#\\s*SECTION\\b");
        s.holes = holesOf(panelWidget);
        s.aim = aimRow({"drilled"});
        s.place = Right;
        s.keys = true;
        s.closeRows = true;
        s.openRows = QStringList{"drilled"};
        s.enter = [scene] {
            while (auto m = QApplication::activePopupWidget()) m->close();
            scene()->clearSelection();
        };
        s.done = [this, scene] {
            static const QRegularExpression re(R"(^\s*#\s*SECTION\b)", QRegularExpression::MultilineOption);
            return re.match(m_editor->getScript()).hasMatch() && !scene()->isNaming();
        };
        s.doneNote = T("Nice. The rows under it belong to the section.");
        s.show = [this, scene](std::function<void()> over) {
            const QRect r = scene()->rowRect("drilled", m_window);
            if (r.isEmpty()) { over(); return; }
            QWidget* vp = scene()->treeViewport();
            // (the hand goes to the circle that shows between the rows: hovering shows it, and a click on it does it)
            const QPoint circle = scene()->sectionGapPoint("drilled", m_window);
            if (circle.isNull()) { over(); return; }
            fakeClick(vp, vp->mapFrom(m_window, circle), Qt::LeftButton, [this, over] {
                // the name of the new section is typed in at once: a key at a time, then Enter
                auto editor = [this]() -> QLineEdit* {
                    auto f = qobject_cast<QLineEdit*>(QApplication::focusWidget());
                    return f && f->parentWidget() == m_view->scenePanel()->treeViewport() ? f : nullptr;
                };
                after(900, [this, editor, over] {
                    if (!editor()) { over(); return; }
                    const QString name = "Shape";
                    if (QLineEdit* f = editor()) f->selectAll();
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
    // 16: a section view (a viewer of its own, switched on by the user)
    {
        currentChapter = 0;                 // (chapter 1: the section view is how the idea of a field is seen)
        Step s;
        s.title = T("Every shape is a field");
        s.text = T("Underneath, FielDes does not store faces. A shape is a field: a number for every point in space -- negative inside the shape, zero exactly on its surface, positive outside. "
                   "Cut the model open (Shift+X) and the plane shows those numbers as colours; drag its arrow through the model.");
        s.hint = T("The surface of the shape is only the place where the field is zero. The section view is View ▸ Section view; its card has the plane's settings. Or press Show me.");
        s.holes = holesOf(viewWidget);
        s.place = InsideBottom;
        s.keys = true;
        s.closeRows = true;
        s.enter = [this, scene] {
            if (QAction* a = sectionAction()) a->setChecked(false);
            scene()->clearSelection();
            m_view->setFocus(Qt::OtherFocusReason);              // (Shift+X acts while the viewport has the keyboard)
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
            static_cast<TourLayer*>(m_layer)->cursorKey = "Shift+X";
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
        s.leave = [this] { if (QAction* a = sectionAction()) a->setChecked(false); };
        add(s);
    }
    // 17: a field instead of a number (a model of its own: a plate, a point, the field made of it, the plate grown by it)
    {
        currentChapter = 0;                 // (chapter 1: what a field is)
        Step s;
        s.title = T("A field instead of a number");
        s.text = T("swell is a field: a number at every place. grown is block grown by an even 1.0 -- the row o = 1.0 under it. Drag swell onto grown, and the field takes the place of the 1.0: "
                   "the skin is thick where swell is large and thin where it is not. The design intent is a field, not a dimension.");
        s.hint = T("Anywhere a number goes, a field goes. The framed lines show the call change. Or press Show me.");
        s.code = linesWith("^(swell|grown) =");
        s.holes = holesOf(panelWidget);
        s.focus = aimRow({"swell"});
        s.aim = aimRow({"grown"});
        s.place = Right;
        s.group = "field";
        s.closeRows = true;
        s.openRows = QStringList{"grown"};              // (what is dragged onto, with its arguments: the 1.0 that the field takes the place of is a row)
        s.showView = true;
        s.enter = [this, scene, fieldModel] {
            while (auto m = QApplication::activePopupWidget()) m->close();
            if (QAction* a = sectionAction()) a->setChecked(false);
            useScript(tourHeader(QString()) + fieldModel(false));
            scene()->clearSelection();
        };
        // (a call is written one argument to a line: the whole statement is looked at, without its blanks)
        s.done = [statementAt] {
            const Statement st = statementAt("grown =");
            QString flat = st.text;
            flat.remove(QRegularExpression("\\s"));
            return st.first >= 0 && flat.contains("offset(block,swell");
        };
        s.doneNote = T("Nice. The growth now follows the field.");
        s.show = [this, scene](std::function<void()> over) {
            const QRect a = scene()->rowRect("swell", m_window), b = scene()->rowRect("grown", m_window);
            if (a.isEmpty() || b.isEmpty()) { over(); return; }
            QWidget* vp = scene()->treeViewport();
            fakeDrag(vp, vp->mapFrom(m_window, QPoint(a.left() + 60, a.center().y())),
                     vp->mapFrom(m_window, QPoint(b.left() + 90, b.center().y())), 1100, over);
        };
        add(s);
    }
    // 18: the point of the field is moved by its gizmo, and the field follows
    {
        Step s;
        s.title = T("What that means for design");
        s.text = T("swell is the distance from the point anchor, so the skin is thick near it. Drag the arrows of anchor -- or press Show me -- and the thickness moves with the point: one point now "
                   "decides where the part is strong. You describe how the part should behave, and the geometry follows.");
        s.hint = T("A field is a number at every place, so it can be added, blended and graded like a number: a wall that thickens where a part is loaded, a round that grows along an edge, "
                   "a lattice that follows a shape. Click the point to see its gizmo.");
        s.code = linesWith("^anchor\\b|^swell =");
        s.holes = holesOf(viewWidget);
        s.place = InsideBottom;
        s.group = "field";
        s.closeRows = true;
        s.openRows = QStringList{"grown", "swell"};
        s.enter = [this, fieldModel] {
            // (the growth by the field is what is moved: it is there when the step before was skipped too)
            useScript(tourHeader(QString()) + fieldModel(true));
            // The point is selected once it is in the tree: when the step is reached with Back, or by a jump, the model of the step is not in the tree yet --
            // a selection asked for at once found nothing, and the step had no gizmo to drag
            selectAgain({"anchor"}, 12);
        };
        s.point = [this] { QPoint p; return m_view->gizmoGripPoint(0, 0, p) ? m_view->mapTo(m_window, p) : QPoint(); };
        s.done = [this] {
            static const QRegularExpression re(R"(^anchor = point\([^)]*?position=\(var\(\s*(-?[\d.e+-]+)\s*\), var\(\s*(-?[\d.e+-]+)\s*\), var\(\s*(-?[\d.e+-]+)\s*\))",
                                               QRegularExpression::MultilineOption);
            const auto m = re.match(m_editor->getScript());
            return m.hasMatch() && (std::abs(m.captured(1).toDouble()) + std::abs(m.captured(2).toDouble()) + std::abs(m.captured(3).toDouble())) > 1e-6;
        };
        s.doneNote = T("Nice. The field follows the point: the growth is where the point is.");
        s.show = [this](std::function<void()> over) {
            QPoint p;
            if (!m_view->gizmoGripPoint(0, 0, p)) { over(); return; }
            fakeDrag(m_view, p, p + QPoint(70, 0), 900, over);
        };
        add(s);
    }
    // 18b: a lattice whose wall thickness is a field (a model of its own): what a field is for, made visible -- one line changes every cell
    {
        Step s;
        s.title = T("A lattice that follows a field");
        s.text = T("foam fills a block with a gyroid lattice, and the thickness of its walls is walls: a field, thin at the left and thick at the right. In the walls line, change the 2.6 to 3.5 -- "
                   "one line, and every cell of the lattice follows.");
        s.hint = T("One line changes every cell: the lattice is made where the field says. A lattice takes a moment to draw. Or press Show me.");
        s.code = linesWith("^(walls|foam) =");
        s.holes = holesOf(scriptWidget);
        s.focus = [this]() -> QRect { return scriptLineRect(std::max(0, scriptLine("walls ="))); };
        s.point = [this] { const QRect r = scriptLineRect(std::max(0, scriptLine("walls ="))); return r.isEmpty() ? QPoint() : QPoint(r.right() - 20, r.center().y()); };
        s.place = Right;
        s.keys = true;
        s.group = "field";
        s.closeRows = true;
        s.showView = true;
        s.enter = [this, scene] {
            while (auto m = QApplication::activePopupWidget()) m->close();
            if (QAction* a = sectionAction()) a->setChecked(false);
            // (resolution 7: at less the walls are visibly faceted, and the lattice is what this step is to show well)
            useScript(tourHeader("[-30, -22, -8], [30, 22, 10]", 7) +
                      "block = box((-24, -16, -5), (24, 16, 5))\n"
                      "walls = ramp(x_field(), (-24, 24), (1.0, 2.6))\n"
                      "foam = lattice(block, cell_periodic('gyroid'), cell_size=12, thickness=walls)\n"
                      "foam");
            scene()->clearSelection();
        };
        // (the call is changed, and is still whole: a half-typed number is not the step done)
        s.done = [statementAt] {
            const Statement st = statementAt("walls =");
            QString flat = st.text;
            flat.remove(QRegularExpression("\\s"));
            return st.first >= 0 && flat.endsWith("))") && flat != "walls=ramp(x_field(),(-24,24),(1.0,2.6))";
        };
        s.doneNote = T("Nice. Every cell of the lattice follows the field.");
        s.show = [this](std::function<void()> over) {
            const int line = scriptLine("walls =");
            if (line < 0) { over(); return; }
            const QString text = m_editor->getScript().split('\n').value(line);
            static const QRegularExpression re(R"(-?[\d.]+)");
            QRegularExpressionMatch last;
            for (auto m = re.globalMatch(text); m.hasNext();) last = m.next();
            if (!last.hasMatch()) { over(); return; }
            typeOver(line, last.capturedStart(), last.capturedEnd(), "3.5", over);
        };
        add(s);
    }
    // 18c: the loop between analysis and shape: a text, with the lattice behind it
    {
        Step s;
        s.title = T("Simulations speak the same language");
        s.text = T("The result of an analysis -- the stress in a part, its temperature, the speed of a flow -- is a field too. So it can be fed back into the geometry: more material where the stress "
                   "is high, a lattice that follows the load. That loop between analysis and shape is what \"field driven design\" means, and it is why FielDes is built the way it is.");
        s.hint = T("The next chapters take you through the program, one piece at a time; the last ones build a small version of each of these loops yourself.");
        s.showView = true;
        s.place = Center;
        s.group = "field";
        s.closeRows = true;
        // (a small analysis of a beam, drawn with its stress colours: the stress is a field, and it is there to be seen)
        s.enter = [this, scene] {
            while (auto m = QApplication::activePopupWidget()) m->close();
            if (QAction* a = sectionAction()) a->setChecked(false);
            useScript(tourHeader("[-5, -12, -12], [65, 22, 20]", 4) +
                      "beam = box((0, 0, 0), (60, 10, 8))\n"
                      "wall = fixed(box((-1, -1, -1), (1, 11, 9)))\n"
                      "pull = force(box((59, -1, -1), (61, 11, 9)), (0, 0, -100))\n"
                      "stress = static_analysis(beam, supports=[wall], loads=[pull], material=aluminium, element_size=2)\n"
                      "stress");
            scene()->clearSelection();
        };
        add(s);
    }
    // 19: a reference: Ctrl+drag (what it is and why comes first)
    {
        currentChapter = 2;
        Step s;
        s.title = T("One model, two places");
        s.text = T("Sometimes a model is needed in two places. capped is a union of cap and ring: those two are its arguments, the rows under it. knob stands on its own, and "
                   "it should be one of the union's arguments too. A normal drag would move knob into the union, and it would be gone from where it was. A reference adds it "
                   "while knob stays where it is.")
                 + "\n\n" + T("Hold Ctrl and drag knob onto capped: it is added to its arguments.");
        s.hint = T("capped lists knob as a faded shadow row, and its line says # shadow: knob. Or press Show me.");
        s.code = linesWith("^(knob|capped) =|#\\s*shadow:");
        s.holes = holesOf(panelWidget);
        s.focus = aimRow({"knob"});
        s.aim = aimRow({"capped"});
        s.place = Right;
        s.group = "reference";
        s.closeRows = true;
        s.openRows = QStringList{"capped"};             // (the union with its arguments: what knob is added to)
        s.enter = [this, scene] {
            while (auto m = QApplication::activePopupWidget()) m->close();
            useScript(tourHeader(QString()) +
                      "knob = sphere(4, (15, 0, 4))\n"
                      "knob\n"
                      "cap = cylinder(7, 3, (-15, 0, 0))\n"
                      "ring = cylinder(9, 1, (-15, 0, 0))\n"
                      "capped = union(cap, ring)\n"
                      "capped");
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
    // 19b: snapping one body onto another (chapter 3: the S key moves a body so that the points, edges or faces selected on two bodies meet)
    {
        currentChapter = 2;
        Step s;
        s.title = T("Snap one body onto another");
        s.text = T("base and peg are two bodies, and each carries a point: corner on the base, tip at the foot of the peg. Select tip, then Ctrl+click corner, and press S. The peg moves until "
                   "its tip is on the corner. Nothing new is made: the peg's own position is written into its line, as the gizmo does.");
        s.hint = T("The first point selected is on the body that moves. Points, edges and faces of a body are made with right-click ▸ Select Surface and the corner and edge entries. "
                   "S is also View ▸ Snap. Or press Show me.");
        s.code = linesWith("^(base|peg|corner|tip) =");
        s.holes = both;
        s.aim = aimRow({"tip"});
        s.cardNear = [this]() { return widgetRect(m_view); };
        s.place = InsideBottomLeft;
        s.showView = true;
        s.keys = true;
        s.group = "snap";
        s.closeRows = true;
        s.enter = [this, scene] {
            while (auto m = QApplication::activePopupWidget()) m->close();
            useScript(tourHeader("[-30, -22, -12], [30, 22, 36]", 4) +
                      "base = box((-24, -14, 0), (24, 14, 6))\n"
                      "corner = corner_at(base, (24, 14, 6))\n"
                      "peg = cylinder(4, 14, (-10, 0, 20))\n"
                      "tip = circle_center_at(peg, (-6, 0, 20))\n"
                      "base\npeg\ncorner\ntip");
            scene()->clearSelection();
            m_view->standardView(View::VIEW_ISO);
        };
        // (the peg has its own position written once it has been moved: its numbers are not all zero)
        s.done = [this, statementAt] {
            QString flat = statementAt("peg =").text;
            flat.remove(QRegularExpression("\\s"));
            static const QRegularExpression re(R"(position=\((?:var\()?(-?[\d.e+-]+)\)?,(?:var\()?(-?[\d.e+-]+)\)?,(?:var\()?(-?[\d.e+-]+)\)?\))");
            const auto m = re.match(flat);
            return m.hasMatch() && (qAbs(m.captured(1).toDouble()) > 1e-6 || qAbs(m.captured(2).toDouble()) > 1e-6 || qAbs(m.captured(3).toDouble()) > 1e-6);
        };
        s.doneNote = T("Nice. The peg stands on the corner, and nothing but its position changed.");
        s.show = [this, selectRows](std::function<void()> over) {
            (*selectRows)(QStringList{"tip", "corner"}, [this, over] {
                // (S, a moment after the second click: selecting a point writes its placement into the script, and the tree has to read that before a snap can be worked out)
                after(1500, [this, over] {
                    m_view->scenePanel()->snapSelected();
                    over();
                });
            });
        };
        add(s);
    }
    currentChapter = 3;                     // (chapter 4: work planes, 2D shapes, paths and extrusions)
    // The model of the steps on profiles: four points and the profile through them (`smooth`: the corner at the top right is a spline point)
    auto profileModel = [](bool smooth) {
        return QString("a = point(-20, -12, 0)\na\nb = point(20, -12, 0)\nb\n") +
               (smooth ? "c = point(20, 12, 0, smooth=True)\n" : "c = point(20, 12, 0)\n") +
               "c\nd = point(-20, 12, 0)\nd\nplate = profile(\n    [a, b, c, d],\n    smooth=False,\n)\nplate";
    };
    // (the profile's points: how many the call lists)
    auto profilePoints = [this]() {
        static const QRegularExpression re(R"(plate\s*=\s*profile\(\s*\[([^\]]*)\])");
        const auto m = re.match(m_editor->getScript());
        return m.hasMatch() ? int(m.captured(1).count(',')) + 1 : 0;
    };

    // (the model of the steps on the work plane: a flat pad to draw on -- every one of them puts it back when it is opened, whatever was drawn or deleted before)
    auto padModel = [] { return QString("pad = box((-30, -22, -4), (30, 22, 0))\npad"); };

    // 20: the work plane: a grid to draw a shape on (Shift+W; a click puts a point down, the first one closes it)
    {
        Step s;
        s.title = T("Draw on a work plane");
        s.text = T("Press Shift+W in the viewport for a work plane: a grid to draw on. Click to put down the corners of a shape, then click the first one to close it.")
                 + "\n\n" + T("E picks line, spline or point.");
        s.hint = T("The plane opens in Line, so the first click draws. Nothing is written until the shape is closed. Or press Show me.");
        s.code = linesWith("profile\\(|^\\w+_p\\d+ = ");
        s.holes = holesOf(viewWidget);
        s.place = InsideBottom;
        s.keys = true;
        s.group = "workplane";
        s.closeRows = true;
        s.enter = [this, scene, padModel] {
            while (auto m = QApplication::activePopupWidget()) m->close();
            if (QAction* a = sectionAction()) a->setChecked(false);
            useScript(tourHeader(QString()) + padModel());
            scene()->clearSelection();
            m_view->setWorkPlaneOpen(false);
            m_view->standardView(View::VIEW_TOP);              // (the plane that opens is parallel to the view: the one the points go on)
            m_view->setFocus(Qt::OtherFocusReason);            // (Shift+W acts while the viewport has the keyboard)
        };
        s.done = [this] { return m_editor->getScript().contains("profile("); };
        s.doneNote = T("Nice. The points and the profile are in the script, as plain numbers.");
        s.show = [this](std::function<void()> over) {
            m_busy = true;
            static_cast<TourLayer*>(m_layer)->cursorKey = "Shift+W";                 // (the shortcut is pressed: a key cap by the cursor says so)
            setCursor(m_view->mapTo(m_window, QPoint(m_view->width() / 2, m_view->height() / 2)));
            after(900, [this, over] {
                m_view->setWorkPlaneOpen(true);
                m_busy = false;
                hideCursor();
                // (the corners of a rectangle, and the first one again: it closes the shape)
                const QVector<QVector3D> corners{{-18, -12, 0}, {18, -12, 0}, {18, 12, 0}, {-18, 12, 0}, {-18, -12, 0}};
                auto click = std::make_shared<std::function<void(int)>>();
                *click = [this, corners, click, over](int i) {
                    if (i >= corners.size()) { over(); return; }
                    fakeClick(m_view, m_view->screenPoint(corners[i]), Qt::LeftButton, [click, i] { (*click)(i + 1); });
                };
                after(700, [click] { (*click)(0); });
            });
        };
        s.leave = [this] { m_view->setWorkPlaneOpen(false); };
        add(s);
    }
    // 20b: stopping the drawing: a right click (the work plane has no way to be used without it)
    {
        Step s;
        s.title = T("Stop drawing, start again");
        s.text = T("While you draw, every click puts down a point. Right-click in the viewport to stop drawing: clicks select, move, turn and scale things as usual again. Press E to start "
                   "again where you were; E also goes on to the next kind of drawing: line, spline, point.");
        s.hint = T("The View menu has the same: Work plane: select, do not draw. A right click and drag still pans the view. Or press Show me.");
        s.holes = holesOf(viewWidget);
        s.place = InsideBottom;
        s.keys = true;
        s.group = "workplane";
        s.closeRows = true;
        s.enter = [this, scene, padModel] {
            while (auto m = QApplication::activePopupWidget()) m->close();
            useScript(tourHeader(QString()) + padModel());
            scene()->clearSelection();
            m_baseline = 0;                                     // (0: drawing is on; 1: it has been stopped -- the step is done when it is on again)
            m_view->setWorkPlaneOpen(false);
            m_view->setWorkPlaneOpen(true);                     // (it opens in Line: drawing is on)
            m_view->setWorkPlaneFrame(QVector3D(0, 0, 0), QVector3D(1, 0, 0), QVector3D(0, 1, 0));      // (the horizontal plane, seen from above: the camera turns, the plane stays)
            m_view->standardView(View::VIEW_TOP);
            m_view->setFocus(Qt::OtherFocusReason);
        };
        // (stopped with a right click, and then started again with E: the two halves, in that order)
        s.done = [this] {
            if (!m_view->workPlane().open) return false;
            if (m_baseline < 0.5)
            {
                if (m_view->workPlane().mode == 0) m_baseline = 1;
                return false;
            }
            return m_view->workPlane().mode != 0;
        };
        s.doneNote = T("Nice. Drawing is back on, where you left it.");
        s.show = [this](std::function<void()> over) {
            const QPoint at(m_view->width() / 2, m_view->height() / 2);
            fakeClick(m_view, at, Qt::RightButton, [this, over] {
                after(1200, [this, over] {
                    m_busy = true;
                    static_cast<TourLayer*>(m_layer)->cursorKey = "E";
                    setCursor(m_view->mapTo(m_window, QPoint(m_view->width() / 2, m_view->height() / 2)));
                    after(900, [this, over] {
                        m_view->cycleWorkPlaneMode();
                        after(900, [this, over] { m_busy = false; hideCursor(); static_cast<TourLayer*>(m_layer)->cursorKey.clear(); over(); });
                    });
                });
            });
        };
        s.leave = [this] { m_view->setWorkPlaneOpen(false); };
        add(s);
    }
    // 21: a point pulled out of an edge
    {
        Step s;
        s.title = T("Pull a point out of an edge");
        s.text = T("A 2D shape has a blue outline. Press an EDGE -- a blue line between two corners, not a corner -- and drag: a new point comes out of it and becomes part of the shape.");
        s.hint = T("The arrow points at an edge. The new point is a model of its own, put in the list of the profile. Or press Show me.");
        s.code = linesWith("^plate =|^\\s+\\[\\w+");
        s.holes = holesOf(viewWidget);
        s.point = [this] { return m_view->mapTo(m_window, m_view->screenPoint(QVector3D(0, 12, 0))); };       // (the middle of the top edge)
        s.place = InsideBottom;
        s.group = "profile";
        s.closeRows = true;
        s.enter = [this, scene, profileModel] {
            while (auto m = QApplication::activePopupWidget()) m->close();
            useScript(tourHeader(QString()) + profileModel(false));
            scene()->clearSelection();
            m_view->standardView(View::VIEW_TOP);
        };
        s.done = [profilePoints] { return profilePoints() >= 5; };
        s.doneNote = T("Nice. The profile has another point.");
        s.show = [this](std::function<void()> over) {
            const QPoint from = m_view->screenPoint(QVector3D(0, 12, 0));       // (the middle of the top edge)
            if (!m_view->isVisible() || from.isNull()) { over(); return; }
            fakeDrag(m_view, from, from + QPoint(0, -48), 900, over);
        };
        add(s);
    }
    // 22: a corner, a spline point and a round corner (a double click), and the gizmo of a point
    {
        Step s;
        s.title = T("Corner, spline point or round corner");
        s.text = T("Double-click a point: a corner becomes a spline point (a round dot: the outline goes smoothly through it), the next double click a round corner (an exact arc: drag the arc "
                   "to set its radius), and one more a corner again. A selected point has a gizmo: press its dot to move it in the plane of the profile; the gold ring scales it.");
        s.hint = T("The arrow points at a corner. The point says what it is: smooth=True, or radius=var(..). In a sweep along a line, the scale of a point is how thick the sweep is there. "
                   "Or press Show me.");
        s.code = linesWith("smooth=|radius=|^\\w+ = point\\(");
        s.holes = holesOf(viewWidget);
        s.point = [this] { return m_view->mapTo(m_window, m_view->screenPoint(QVector3D(20, 12, 0))); };       // (the corner at the top right)
        s.place = InsideBottom;
        s.group = "profile";
        s.closeRows = true;
        s.enter = [this, scene, profileModel] {
            while (auto m = QApplication::activePopupWidget()) m->close();
            useScript(tourHeader(QString()) + profileModel(false));
            scene()->clearSelection();
            m_view->standardView(View::VIEW_TOP);
        };
        s.done = [this] { return m_editor->getScript().contains(QRegularExpression(R"(point\([^)]*(smooth=True|radius=))")); };
        s.doneNote = T("Nice. The outline goes smoothly through that point.");
        s.show = [this](std::function<void()> over) {
            const QPoint at = m_view->screenPoint(QVector3D(20, 12, 0));
            m_busy = true;
            setCursor(m_view->mapTo(m_window, at));
            sendMouse(m_view, QEvent::MouseMove, at, Qt::NoButton, Qt::NoButton);
            after(700, [this, at, over] {
                for (const QEvent::Type t : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease, QEvent::MouseButtonDblClick, QEvent::MouseButtonRelease})
                    sendMouse(m_view, t, at, Qt::LeftButton, t == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton);
                after(900, [this, over] { m_busy = false; hideCursor(); over(); });
            });
        };
        add(s);
    }
    // The model of the steps on lines: four points, the spline through them, and (`tube`) a circle at the first point and the sweep of it along the line
    auto lineModel = [](bool tube) {
        return QString("q1 = point(-25, -12, 0)\nq1\nq2 = point(-8, 10, 8)\nq2\nq3 = point(10, -8, 14)\nq3\nq4 = point(26, 8, 6)\nq4\n"
                       "route = spline([q1, q2, q3, q4])\nroute") +
               (tube ? QString("\nprofile = circle(4, (-25, -12))\nprofile\npath_extrude_1 = path_extrude(profile, route)\npath_extrude_1") : QString());
    };
    // (the points q1 ... q4 as written in the script, with their gizmos: what a step compares to see that a point has been moved, turned or scaled)
    auto pointsSignature = [this]() {
        const QString text = m_editor->getScript();
        const int a = text.indexOf("q1 = "), b = text.indexOf("route = ");
        return (a >= 0 && b > a) ? text.mid(a, b - a) : QString();
    };
    // 24a: a line is a shape too (a 1D shape): it goes through points
    {
        Step s;
        s.title = T("A line through points");
        s.text = T("A line is a shape of one dimension, made of points: spline([q1, q2, q3, q4]) is a smooth line through them (polyline joins them with straight pieces). "
                   "Press the dot of a point and drag: the line follows.");
        s.hint = T("The line is the blue curve, and a model of its own, in the tree: the points are what it is made of. Or press Show me.");
        s.code = linesWith("^route =");
        s.holes = holesOf(viewWidget);
        s.place = InsideBottom;
        s.group = "line";
        s.closeRows = true;
        s.enter = [this, scene, lineModel, pointsSignature] {
            while (auto m = QApplication::activePopupWidget()) m->close();
            useScript(tourHeader(QString()) + lineModel(false));
            scene()->clearSelection();
            m_view->standardView(View::VIEW_ISO);
            m_baselineText = pointsSignature();
        };
        s.done = [this, pointsSignature] { return pointsSignature() != m_baselineText; };
        s.doneNote = T("Nice. The line goes through the points wherever you put them.");
        s.show = [this](std::function<void()> over) {
            const QPoint from = m_view->screenPoint(QVector3D(-8, 10, 8));
            if (!m_view->isVisible() || from.isNull()) { over(); return; }
            fakeDrag(m_view, from, from + QPoint(40, -36), 900, over);
        };
        add(s);
    }
    // 24b: a shape swept along it
    {
        Step s;
        s.title = T("Sweep a shape along a line");
        s.text = T("A line can guide a shape: the shape is swept along it. Select profile (the circle) and route (the line) -- Ctrl+click the second -- then right-click one of them and "
                   "choose Operations ▸ Extruding ▸ path_extrude. Then play: drag a point of the line and the tube bends; the gold ring of a point makes the tube thicker there.");
        s.hint = T("path_extrude(profile, route): the circle is the start of the tube, and the line runs through the middle of it. Or press Show me.");
        s.code = linesWith("^(profile|route|path_extrude_1) =");
        s.holes = both;
        s.cardNear = treeNear;
        s.place = Right;
        s.group = "line";
        s.closeRows = true;
        s.enter = [this, scene, lineModel] {
            while (auto m = QApplication::activePopupWidget()) m->close();
            useScript(tourHeader(QString()) + lineModel(false) + "\nprofile = circle(4, (-25, -12))\nprofile");
            scene()->clearSelection();
            m_view->standardView(View::VIEW_ISO);
        };
        s.done = [this] { return m_editor->getScript().contains("path_extrude("); };
        s.doneNote = T("Nice. The tube follows the line.");
        s.show = [this, scene, walk](std::function<void()> over) {
            const QRect a = scene()->rowRect("profile", m_window), b = scene()->rowRect("route", m_window);
            if (a.isEmpty() || b.isEmpty()) { over(); return; }
            QWidget* vp = scene()->treeViewport();
            const QPoint pa = vp->mapFrom(m_window, QPoint(a.left() + 70, a.center().y())), pb = vp->mapFrom(m_window, QPoint(b.left() + 70, b.center().y()));
            QPointer<QWidget> viewport(vp);
            m_busy = true;
            setCursor(vp->mapTo(m_window, pa));
            after(500, [this, viewport, pa, pb, walk, over] {
                if (!viewport) { m_busy = false; hideCursor(); over(); return; }
                sendMouse(viewport, QEvent::MouseButtonPress, pa, Qt::LeftButton, Qt::LeftButton);
                sendMouse(viewport, QEvent::MouseButtonRelease, pa, Qt::LeftButton, Qt::NoButton);
                after(600, [this, viewport, pb, walk, over] {
                    if (!viewport) { m_busy = false; hideCursor(); over(); return; }
                    static_cast<TourLayer*>(m_layer)->cursorKey = "Ctrl";
                    setCursor(viewport->mapTo(m_window, pb));
                    sendMouse(viewport, QEvent::MouseButtonPress, pb, Qt::LeftButton, Qt::LeftButton, Qt::ControlModifier);
                    sendMouse(viewport, QEvent::MouseButtonRelease, pb, Qt::LeftButton, Qt::NoButton, Qt::ControlModifier);
                    after(700, [this, viewport, pb, walk, over] {
                        static_cast<TourLayer*>(m_layer)->cursorKey.clear();
                        if (!viewport) { m_busy = false; hideCursor(); over(); return; }
                        QContextMenuEvent ev(QContextMenuEvent::Mouse, pb, viewport->mapToGlobal(pb));
                        QApplication::sendEvent(viewport, &ev);
                        after(400, [this, walk, over] {
                            m_busy = false;
                            hideCursor();
                            auto root = qobject_cast<QMenu*>(openMenu());
                            if (!root) { over(); return; }
                            (*walk)(root, root, QStringList{T("Operations"), T("Extruding"), "path_extrude"}, over);
                        });
                    });
                });
            });
        };
        add(s);
    }
    // 25: a copy, and a linked copy
    {
        currentChapter = 4;                 // (chapter 5: symmetry)
        Step s;
        s.title = T("Copy or link");
        s.text = T("Shift+D copies the selected model. Ctrl+D links it: the copy IS the model and follows every change of its shape, but its position and its rotation are its own -- "
                   "move or turn one and the other stays.")
                 + "\n\n" + T("Select bolt and press Ctrl+D, then move the copy.");
        s.hint = T("The copy is written bolt_2 = instance(bolt) with # shadow: bolt: bolt keeps its own row and the copy shows a faded shadow of it. Or press Show me.");
        s.code = linesWith("^bolt\\w* =|^bolt\\w*$|#\\s*shadow:");
        s.holes = both;
        s.place = Right;
        s.keys = true;
        s.group = "copies";
        s.closeRows = true;
        s.enter = [this] {
            while (auto m = QApplication::activePopupWidget()) m->close();
            useScript(tourHeader(QString()) + "bolt = cylinder(3, 14, (0, 0, 0))\nbolt");
            m_view->standardView(View::VIEW_ISO);
            selectAgain({"bolt"}, 12);              // (it is in the tree once the script has run)
            m_view->setFocus(Qt::OtherFocusReason);            // (Ctrl+D acts while the viewport has the keyboard)
        };
        s.done = [this] { return m_editor->getScript().contains("instance("); };
        s.doneNote = T("Nice. bolt_2 is bolt: change the shape of bolt and it follows.");
        s.show = [this, scene](std::function<void()> over) {
            m_busy = true;
            static_cast<TourLayer*>(m_layer)->cursorKey = "Ctrl+D";
            setCursor(m_view->mapTo(m_window, QPoint(m_view->width() / 2, m_view->height() / 2)));
            after(900, [this, scene, over] {
                scene()->duplicateLinkedSelected();
                after(900, [this, over] { m_busy = false; hideCursor(); over(); });
            });
        };
        add(s);
    }
    // 25b-25h: a symmetry (a model of its own: a table top, one leg, a mirror of two planes through the axis of the table, and the images of the leg written under it)
    auto symmetryModel = []() {
        return QString("mirror = symmetry_planes(\n    planes=2,\n    kind='axis',\n    centre=(0, 0, 0),\n    axis=(0, 0, 1),\n    first=(0, 1, 0),\n"
                       "    position=(var(0), var(0), var(0)),\n    rotation=(var(0), var(0), var(0)),\n)\n"
                       "mirror\n\n"
                       "top = box((-40, -26, 30), (40, 26, 36))\n"
                       "top\n\n"
                       "leg = cylinder(\n    4,\n    30,\n    (32, 18, 0),\n    position=(var(0), var(0), var(0)),\n    rotation=(var(0), var(0), var(0)),\n    scale=(var(1), var(1), var(1)),\n)\n"
                       "leg = handles(leg, [var(32), var(18), var(4), var(30), var(0)])\n"
                       "leg\n"
                       "leg_images = symmetry_images(leg, mirror)  # shadow: leg, mirror\n"
                       "leg_images");
    };
    // (what a step that edits the leg compares: its numbers -- the handles that pull its surfaces and the position, rotation and scale in its call that the gizmo drags)
    auto legSignature = [statementAt]() { return statementAt("leg = handles(").text + "\n" + statementAt("leg = cylinder(").text; };
    auto symmetryEnter = [this, scene, symmetryModel](const QStringList& select) {
        while (auto m = QApplication::activePopupWidget()) m->close();
        useScript(tourHeader("[-50, -35, -5], [50, 35, 45]", 3) + symmetryModel());
        scene()->clearSelection();
        m_view->standardView(View::VIEW_ISO);
        if (!select.isEmpty()) selectAgain(select, 12);       // (once it is in the tree)
    };
    // (a click on a row of the tree, as the hand does it)
    auto clickRow = [this, scene](const QString& prefix, std::function<void()> over) {
        const QRect r = scene()->rowRect(prefix, m_window);
        if (r.isEmpty()) { over(); return; }
        QWidget* vp = scene()->treeViewport();
        fakeClick(vp, vp->mapFrom(m_window, QPoint(r.left() + 70, r.center().y())), Qt::LeftButton, over);
    };
    // 25b: a symmetry is a model, and the one that is selected is the one in use
    {
        Step s;
        s.title = T("A mirror");
        s.text = T("A symmetry is a model: mirror planes through a centre. Click mirror in the tree: its planes are drawn, and it is the symmetry in use. Then right-click empty space in the "
                   "viewport and choose Primitives ▸ New 3D shape ▸ box: the box is made once, and one line under it holds all its images.");
        s.hint = T("box_1_images = symmetry_images(box_1, mirror): the images are not models of their own, so nothing is written twice. Or press Show me.");
        s.code = linesWith("^mirror\\b|^box_\\d+\\w* =|^box_\\d+_images");
        s.holes = both;
        s.aim = aimRow({"mirror"});
        s.cardNear = treeNear;
        s.place = Right;
        s.group = "symmetry";
        s.closeRows = true;
        s.showView = true;
        s.enter = [symmetryEnter] { symmetryEnter(QStringList()); };
        // (the box made while mirror is the one in use: it is written with its images under it)
        s.done = [this] {
            static const QRegularExpression re(R"(^box_\d+\w* = symmetry_images\()", QRegularExpression::MultilineOption);
            return re.match(m_editor->getScript()).hasMatch();
        };
        s.doneNote = T("Nice. One box, and its images under it.");
        s.show = [this, clickRow](std::function<void()> over) {
            // (mirror is clicked; then the box is made at a free place of the viewport, as the menu of empty space makes it, and its images with it)
            clickRow("mirror", [this, over] {
                after(1200, [this, over] {
                    const QPoint at(m_view->width() / 4, m_view->height() * 3 / 4);
                    m_busy = true;
                    setCursor(m_view->mapTo(m_window, at));
                    after(900, [this, at, over] {
                        m_view->createAt("box", at);
                        after(1500, [this, over] { m_busy = false; hideCursor(); over(); });
                    });
                });
            });
        };
        add(s);
    }
    // 25d: only the original has numbers: edit it and every image follows
    {
        Step s;
        s.title = T("Edit the original or an image");
        s.text = T("Only the original has numbers. Select leg and drag an arrow of its gizmo, or click leg_images and drag its gizmo: all the images follow, mirrored -- and an image moves "
                   "the original, the other way.");
        s.hint = T("leg_images is one line: the images are what the mirror makes of leg. Or press Show me.");
        s.code = linesWith("^leg\\b");
        s.holes = holesOf(viewWidget);
        s.point = [this] { QPoint p; return m_view->gizmoGripPoint(0, 0, p) ? m_view->mapTo(m_window, p) : QPoint(); };
        s.place = InsideBottom;
        s.group = "symmetry";
        s.closeRows = true;
        s.enter = [this, symmetryEnter, legSignature] {
            symmetryEnter(QStringList{"leg"});
            m_baselineText = legSignature();
        };
        s.done = [this, legSignature] { return legSignature() != m_baselineText; };
        s.doneNote = T("Nice. The images moved with the leg, mirrored.");
        s.show = [this](std::function<void()> over) {
            QPoint p;
            if (!m_view->gizmoGripPoint(0, 0, p)) { over(); return; }
            fakeDrag(m_view, p, p + QPoint(60, 0), 800, over);
        };
        add(s);
    }
    // 25f: it can be left off
    {
        currentChapter = 2;
        Step s;
        s.title = T("On and off");
        s.text = T("Shift+S stops using the symmetry, and starts it again. While it is off, what you make has no images, and its planes are drawn pale.");
        s.hint = T("The mirror is still there: it is a model like any other. Click the viewport and press Shift+S, or press Show me. Lines and sweeps drawn on the work plane with a mirror "
                   "selected are mirrored too (example 29_symmetry).");
        s.holes = both;
        s.cardNear = treeNear;
        s.place = Right;
        s.keys = true;
        s.group = "symmetry";
        s.closeRows = true;
        s.showView = true;
        s.enter = [this, symmetryEnter] {
            symmetryEnter(QStringList{"mirror"});
            m_view->setSymmetryInUse(true);
            m_view->setFocus(Qt::OtherFocusReason);            // (Shift+S acts while the viewport has the keyboard: in the script it is a letter)
        };
        s.leave = [this] { m_view->setSymmetryInUse(true); };
        s.done = [this] { return !m_view->symmetryInUse(); };
        s.doneNote = T("Nice. Shift+S again turns it back on.");
        s.show = [this](std::function<void()> over) {
            m_busy = true;
            static_cast<TourLayer*>(m_layer)->cursorKey = "Shift+S";
            setCursor(m_view->mapTo(m_window, QPoint(m_view->width() / 2, m_view->height() / 2)));
            after(900, [this, over] {
                m_view->toggleSymmetry();
                after(2000, [this, over] {
                    m_view->toggleSymmetry();
                    after(900, [this, over] { m_busy = false; hideCursor(); static_cast<TourLayer*>(m_layer)->cursorKey.clear(); over(); });
                });
            });
        };
        add(s);
    }
    // 26a: lattices, in short (chapter 6: what else FielDes does; chapter 7 builds one)
    {
        currentChapter = 5;
        Step s;
        s.title = T("Lattices");
        s.text = T("A lattice fills a body with a pattern that repeats: struts, like this octet lattice, or smooth surfaces such as the gyroid. It is light and still stiff, and the thickness "
                   "of its walls can be a field. There are cells of many kinds, and lattices that do not repeat.")
                 + "\n\n" + T("Chapter 7 builds one from the right-click menu.");
        s.hint = T("Operations ▸ Periodic lattices and Non-periodic lattices hold them. The example 23_lattice_gallery shows one of every kind.");
        s.holes = bothAndScript;
        s.cardNear = [this]() { return widgetRect(m_view); };
        s.place = InsideBottomLeft;
        s.showView = true;
        s.group = "showcase_lattice";
        s.closeRows = true;
        s.enter = [this, scene] {
            while (auto m = QApplication::activePopupWidget()) m->close();
            useScript(tourHeader("[-20, -20, -4], [20, 20, 26]", 6) +
                      "block = box((-14, -14, 0), (14, 14, 21))\n"
                      "foam = lattice(block, cell_periodic('octet'), cell_size=7, thickness=1.2)\n"
                      "foam");
            scene()->clearSelection();
            m_view->standardView(View::VIEW_ISO);
        };
        add(s);
    }
    // 26b: simulations and optimisations: what they are, shown running (a model of its own: a beam, held at one end and pulled at the other, being optimised)
    {
        currentChapter = 5;                 // (chapter 6: the special features, in short)
        Step s;
        s.title = T("Simulations and optimization");
        s.text = T("FielDes runs simulations -- static, modal, thermal, fluid -- and optimizations as lines of the script. Watch one: this beam is held at its left end and pulled down at the "
                   "right, and the optimization takes away the material that carries nothing, one step after the other.")
                 + "\n\n" + T("Chapters 8 to 10 build an analysis and an optimization of your own from the right-click menus; File ▸ Open example file has more.");
        s.hint = T("The bar under the code shows the step it is at, and the picture follows. 05_static_analysis, 06_modal_analysis, 07_thermal_analysis, 08_topology_optimization and 16_fluid_flow show how.");
        s.holes = bothAndScript;
        s.cardNear = [this]() { return widgetRect(m_view); };
        s.place = InsideBottomLeft;
        s.group = "optimization";
        s.closeRows = true;
        s.enter = [this, scene] {
            while (auto m = QApplication::activePopupWidget()) m->close();
            useScript(tourHeader("[-5, -10, -10], [65, 30, 20]", 2) +
                      "beam = box((0, 0, 0), (60, 20, 10))\n"
                      "support = fixed(box((-1, -1, -1), (1, 21, 11)))\n"
                      "load = force(box((59, -1, -1), (61, 21, 11)), (0, 0, -300))\n"
                      "stress = static_analysis(beam, supports=[support], loads=[load], material=steel, element_size=2.5)\n"
                      "opt = topology_optimization(stress, constraints=[minimize('compliance'), 'volume_fraction <= 0.4'])\n"
                      "opt");
            scene()->clearSelection();
            m_view->standardView(View::VIEW_ISO);
        };
        add(s);
    }
    // 26c: a flow, in short (the water of the model of chapter 9, solved)
    {
        currentChapter = 5;
        Step s;
        s.title = T("Flows");
        s.text = T("FielDes solves how a fluid moves, too. Here water comes in at the left, flows round a post and leaves at the right: the lines are the streams, the colours the speed -- "
                   "a field again. It is solved on a mesh that follows the surface of the water, in a few seconds.");
        s.hint = T("Chapter 9 builds this flow, step by step. The examples 16_fluid_flow and 17_flow_topology_optimization show more.");
        s.holes = bothAndScript;
        s.cardNear = [this]() { return widgetRect(m_view); };
        s.place = InsideBottomLeft;
        s.showView = true;
        s.group = "showcase_flow";
        s.closeRows = true;
        s.enter = [this, scene] {
            while (auto m = QApplication::activePopupWidget()) m->close();
            useScript(tourHeaderFixed("[-4, -4, -4], [44, 24, 14]") +
                      "post = cylinder(3, 12, (14, 10, -1))\n"
                      "slab = box((0, 0, 0), (40, 20, 10))\n"
                      "inlet_face = box((-1, -1, -1), (0.01, 21, 11))\n"
                      "outlet_face = box((39.99, -1, -1), (41, 21, 11))\n"
                      "faces = union(box((-1, -1, -1), (41, 21, 0.01)), box((-1, -1, 9.99), (41, 21, 11)))\n"
                      "sides = union(box((-1, -1, -1), (41, 0.01, 11)), box((-1, 19.99, -1), (41, 21, 11)))\n"
                      "flow = fluid_analysis(post, slab, inlets=[inlet(inlet_face, speed=5)], outlets=[outlet(outlet_face, pressure=0)], boundaries=[slip(faces), slip(sides)], fluid=water)\n"
                      "post\nflow");
            scene()->clearSelection();
            m_view->standardView(View::VIEW_ISO);
        };
        add(s);
    }
    // 26d: manufacturing, in short (a part that a printer cannot print, made printable)
    {
        currentChapter = 5;
        Step s;
        s.title = T("Manufacturing");
        s.text = T("A part is not finished when it is drawn: it has to be made. A 3D printer cannot print a cap that hangs 12 mm out over nothing. manufacturing_analysis colours the places where "
                   "the rules of a process are broken, and manufacturing_optimization changes the part until they are kept.")
                 + "\n\n" + T("Chapter 11 builds this.");
        s.hint = T("The example 33_manufacturing_optimization does it for a real bracket, for a printer, a mill and a mould.");
        s.holes = bothAndScript;
        s.cardNear = [this]() { return widgetRect(m_view); };
        s.place = InsideBottomLeft;
        s.showView = true;
        s.group = "showcase_manufacturing";
        s.closeRows = true;
        s.enter = [this, scene] {
            while (auto m = QApplication::activePopupWidget()) m->close();
            useScript(tourHeader("[-24, -12, -4], [24, 12, 28]", 5) +
                      "stem = box((-6, -6, 0), (6, 6, 16))\n"
                      "cap = rounded_box((-18, -6, 16), (18, 6, 22), 0.3)\n"
                      "part = union(stem, cap)\n"
                      "printer = fff_constraint(nozzle=0.4, overhang=45)\n"
                      "printable = manufacturing_optimization(part, constraints=[printer], max_change=0.25)\n"
                      "printable");
            scene()->clearSelection();
            m_view->standardView(View::VIEW_BOTTOM);            // (what is added is under the cap)
        };
        add(s);
    }
    // 26e: apps, in short (a script that someone who never opens the editor can run)
    {
        currentChapter = 5;
        Step s;
        s.title = T("Apps");
        s.text = T("A FielDes app is a script that someone who never opens the editor can run. You say which numbers, texts or files they may choose -- app.number and the like -- and FielDes shows "
                   "them as a form with a Run button. File ▸ Export as app... makes one file to send.")
                 + "\n\n" + T("Chapter 12 makes one.");
        s.hint = T("The examples 24_app_bracket_maker and 25_app_beam_load_case are apps.");
        s.menuTitle = "File";
        s.menuEntry = "Export as app...";
        s.place = Right;
        s.enter = [] { while (auto m = QApplication::activePopupWidget()) m->close(); };
        add(s);
    }
    // 27: the property tab of a result (a model of its own: one beam, held at one end and pulled at the other)
    {
        currentChapter = 2;
        Step s;
        s.title = T("Properties");
        s.text = T("A result has more to give than its picture. Point at result: a tab comes out at the edge of the tree. Click it, then click a field -- von_mises, say.");
        s.hint = T("The drawer lists the fields and the numbers of the result; the one you click is written under it as a model of its own: "
                   "von_mises_1 = result.von_mises -- a field, which can be shown, sliced and used in other models. The tab is there once the analysis has run. Or press Show me.");
        s.code = linesWith("^result\\b|^\\w+ = result\\.");
        s.holes = [this, treeWithTab]() {
            QVector<QRect> out = treeWithTab();
            const QRect view = widgetRect(m_view);
            if (!view.isEmpty()) out << view;           // (the simulation is drawn in the viewport: it is not dimmed)
            return out;
        };
        s.cardNear = [this]() { return widgetRect(m_view); };
        s.aim = aimRow({"result"});
        s.place = InsideBottomLeft;
        s.group = "properties";
        s.closeRows = true;
        s.enter = [this, scene] {
            while (auto m = QApplication::activePopupWidget()) m->close();
            useScript(tourHeader("[-5, -10, -10], [65, 30, 20]", 2) +
                      "beam = box((0, 0, 0), (60, 20, 10))\n"
                      "support = fixed(box((-1, -1, -1), (1, 21, 11)))\n"
                      "load = force(box((59, -1, -1), (61, 21, 11)), (0, 0, -300))\n"
                      "result = static_analysis(beam, supports=[support], loads=[load], material=steel, element_size=5)\n"
                      "result");
            scene()->clearSelection();
            m_view->standardView(View::VIEW_ISO);                    // (the beam is framed by useScript)
        };
        s.done = [this] {
            static const QRegularExpression re(R"(^\w+ = result\.\w+)", QRegularExpression::MultilineOption);
            return re.match(m_editor->getScript()).hasMatch();
        };
        s.doneNote = T("Nice. It is a field like any other: shown, sliced, used in other models.");
        s.show = [this, scene](std::function<void()> over) {
            // (the mouse comes to the row of result: the tab comes out; the hand goes to it, and then to the field in the drawer)
            const QRect row = scene()->visibleRowRect("result", m_window);
            if (row.isEmpty()) { over(); return; }
            QWidget* vp = scene()->treeViewport();
            const QPoint rowPoint(row.left() + 80, row.center().y());
            QPointer<QWidget> viewport(vp);
            m_busy = true;
            setCursor(rowPoint + QPoint(60, 40));
            glide(m_cursor, rowPoint, 700, [=](QPoint wp) {
                setCursor(wp);
                if (viewport) sendMouse(viewport, QEvent::MouseMove, viewport->mapFrom(m_window, wp), Qt::NoButton, Qt::NoButton);
            }, [=] {
                after(350, [=] {
                    QPointer<QWidget> tab = scene()->propertyTab();
                    if (!tab || !tab->isVisible()) { m_busy = false; hideCursor(); over(); return; }
                    const QPoint tabPoint = m_window->mapFromGlobal(tab->mapToGlobal(tab->rect().center()));
                    glide(rowPoint, tabPoint, 500, [=](QPoint wp) { setCursor(wp); }, [=] {
                        setCursor(tabPoint, true);
                        if (!tab) { m_busy = false; hideCursor(); over(); return; }
                        sendMouse(tab, QEvent::MouseButtonPress, tab->rect().center(), Qt::LeftButton, Qt::LeftButton);
                        sendMouse(tab, QEvent::MouseButtonRelease, tab->rect().center(), Qt::LeftButton, Qt::NoButton);
                        after(900, [=] {
                            setCursor(tabPoint, false);
                            QPointer<QPushButton> button = m_window->findChild<QPushButton*>("prop_von_mises");
                            if (!button) { m_busy = false; hideCursor(); over(); return; }
                            const QPoint target = m_window->mapFromGlobal(button->mapToGlobal(button->rect().center()));
                            glide(tabPoint, target, 700, [=](QPoint wp) { setCursor(wp); }, [=] {
                                setCursor(target, true);
                                after(350, [=] {
                                    if (button) button->click();
                                    setCursor(target, false);
                                    after(500, [=] { m_busy = false; hideCursor(); over(); });
                                });
                            });
                        });
                    });
                });
            });
        };
        add(s);
    }
    // ---- Chapters 7 to 12: each builds a small version of one of the special features with the right-click menus, step by step.  The models are what the menus write, so that a step
    // starts from the same model whether the user did the step before or not: a step that finds what it needs missing puts it there
    auto ensureModel = [this, scene](bool have, const QString& text) {
        while (auto m = QApplication::activePopupWidget()) m->close();
        if (!have)
        {
            useScript(text);
            m_view->standardView(View::VIEW_ISO);
        }
        scene()->clearSelection();
    };
    // (the row of a model of the tree the step is about: the arrow ends at it)
    auto rowAim = [aimRow](const QString& name) { return aimRow({name}); };
    // (the placeholder, once there is one; else the row of the model the menu was chosen on)
    auto holeOrRow = [this, scene, aimRow](const QString& name) {
        auto row = aimRow({name});
        return [this, scene, row]() -> QRect {
            const QRect r = scene()->placeholderRect(m_window);
            return r.isEmpty() ? row() : r;
        };
    };

    // ---- Chapter 7: a lattice
    const QString latticeHead = tourHeader("[-30, -22, -8], [30, 22, 10]", 6);
    auto latticeModel = [latticeHead](bool built) {
        QString s = latticeHead + "part = box((-24, -16, -5), (24, 16, 5), alpha=0.2)\npart\nwalls = ramp(x_field(), (-24, 24), (1.0, 2.6))\nwalls\n";
        if (built) s += "\nlattice_1 = lattice(part, cell_periodic('gyroid'), cell_size=12, thickness=walls)  # shadow: part, walls\nlattice_1\n";
        return s;
    };
    {
        currentChapter = 6;
        Step s;
        s.title = T("Fill a block with a lattice");
        s.text = T("part is a block (drawn faint), and walls is a field: thin at the left, thick at the right. Select part, then Ctrl+click walls, right-click part and choose "
                   "Operations ▸ Periodic lattices ▸ lattice. The block is filled with a lattice, and the field becomes the thickness of its walls.");
        s.hint = T("The first model selected is the one the lattice fills; a field selected with it is the thickness. A lattice takes a few seconds to draw. Or press Show me.");
        buildLook(s);
        s.group = "lattice";
        s.aim = rowAim("part");
        s.enter = [=] { ensureModel(hasLines({"part =", "walls ="}), latticeModel(false)); };
        s.done = [made] { return made("lattice"); };
        s.doneNote = T("Nice. Every cell of the lattice follows the field.");
        s.show = [=](std::function<void()> over) {
            showPick({"part", "walls"}, {T("Operations"), T("Periodic lattices"), "lattice"}, over);
        };
        add(s);
    }
    {
        currentChapter = 6;
        Step s;
        s.title = T("Choose another cell");
        s.text = T("The call that was written says which cell the lattice is made of: cell_periodic('gyroid'). Change gyroid to schwarz_p -- another smooth surface -- or to octet, for struts.");
        s.hint = T("The call is open in the tree and the code. Cells of every kind are under Periodic lattices ▸ Strut cells, TPMS cells and Planar cells. Or press Show me.");
        buildLook(s);
        s.group = "lattice";
        s.openRows = QStringList{"lattice_1"};
        s.code = callLines("lattice");
        s.enter = [=] { ensureModel(made("lattice"), latticeModel(true)); openCall("lattice"); };
        s.done = [callAt, balanced] {
            static const QStringList cells = {"cubic", "bcc", "bccz", "fcc", "fccz", "octet", "octahedron", "kelvin", "diamond_struts", "cross", "tesseract", "cuboctahedron",
                                              "schwarz_p", "diamond", "neovius", "lidinoid", "split_p", "iwp", "frd", "fischer_koch_s", "hexagon", "triangle", "square", "kagome"};
            const Statement st = callAt("lattice");
            static const QRegularExpression re(R"(cell_periodic\('(\w+)'\))");
            const auto m = re.match(st.text);
            return st.first >= 0 && balanced(st.text) && m.hasMatch() && cells.contains(m.captured(1));
        };
        s.doneNote = T("Nice. The same block, a different cell.");
        s.show = [=](std::function<void()> over) {
            typeInCall("lattice", R"(cell_periodic\('(\w+)'\))", "schwarz_p", over);
        };
        add(s);
    }
    {
        currentChapter = 6;
        Step s;
        s.title = T("Make the cells smaller");
        s.text = T("cell_size says how long one cell is. Make the number smaller -- half of what it is, say -- and the block holds more cells, each with finer walls.");
        s.hint = T("A lattice with more cells takes longer to draw. Or press Show me.");
        buildLook(s);
        s.group = "lattice";
        s.openRows = QStringList{"lattice_1"};
        s.code = callLines("lattice");
        s.enter = [=] {
            ensureModel(made("lattice"), latticeModel(true));
            openCall("lattice");
            static const QRegularExpression re(R"(cell_size=([\d.]+))");
            const auto m = re.match(callAt("lattice").text);
            m_baseline = m.hasMatch() ? m.captured(1).toDouble() : 0;
        };
        s.done = [this, callAt, balanced] {
            static const QRegularExpression re(R"(cell_size=([\d.]+))");
            const Statement st = callAt("lattice");
            const auto m = re.match(st.text);
            return st.first >= 0 && balanced(st.text) && m.hasMatch() && m_baseline > 0 && m.captured(1).toDouble() > 0 && m.captured(1).toDouble() < m_baseline;
        };
        s.doneNote = T("Nice. More cells, finer walls.");
        s.show = [=](std::function<void()> over) {
            static const QRegularExpression re(R"(cell_size=([\d.]+))");
            const auto m = re.match(callAt("lattice").text);
            const double half = m.hasMatch() ? m.captured(1).toDouble() / 2 : 6;
            typeInCall("lattice", R"(cell_size=([\d.]+))", QString::number(std::max(1.0, qRound(half * 10) / 10.0)), over);
        };
        add(s);
    }

    // ---- Chapter 8: a static analysis
    const QString beamHead = tourHeader("[-5, -12, -12], [65, 22, 20]", 4);
    // (the beam as the menus write it, stage by stage: 0 the beam and the two boxes that say where, 1 held at the left end, 2 pulled at the right end, 3 with its material, 4 analysed)
    auto beamModel = [beamHead](int stage, const QString& vector, const QString& elementSize) {
        // (the beam is hidden once it is analysed: it is drawn over its own analysis, and the stress is seen on the part that is left uncovered only -- the example files hide the body the same way)
        QString s = beamHead + "beam = box((0, 0, 0), (60, 10, 8))\n" + QString(stage >= 4 ? "# hidden: beam\n" : "beam\n") + "left = box((-1, -1, -1), (1, 11, 9))\n";
        if (stage >= 1) s += "fixed_1 = fixed(\n    region=left,\n    body=beam,\n    displacement=(0.0, 0.0, 0.0),\n)  # shadow: beam, left\nfixed_1\n";
        s += "right = box((59, -1, -1), (61, 11, 9))\n";
        if (stage >= 2) s += "force_1 = force(\n    region=right,\n    body=beam,\n    vector=" + vector + ",\n    profile=None,\n)  # shadow: beam, right\nforce_1\n";
        if (stage >= 3) s += "\naluminium_1 = Material(\n    'aluminium',\n    69000,\n    0.33,\n    2.7e-09,\n    95,\n    0.167,\n    2.3e-05,\n)\n";
        if (stage == 3) s += "aluminium_1\n";
        if (stage >= 4)
            s += "static_analysis_1 = static_analysis(\n    beam,\n    supports=[fixed_1],\n    loads=[force_1],\n    material=aluminium_1,\n    element_size=" + elementSize +
                 ",\n    bounds=None,\n    tolerance=1e-06,\n    cache=True,\n    element='tet',\n)  # shadow: beam, fixed_1, force_1, aluminium_1\nstatic_analysis_1\naluminium_1\n";
        return s;
    };
    {
        currentChapter = 7;
        Step s;
        s.title = T("Hold one end");
        s.text = T("A beam, and a box at each end that says where something acts: left and right (hidden). Select beam, then Ctrl+click left, right-click beam and choose "
                   "Engineering ▸ Structural ▸ New support or load ▸ fixed. The beam is held there: its left end cannot move.");
        s.hint = T("The first model selected is the body, the others are where the support acts. Or press Show me.");
        buildLook(s);
        s.group = "static";
        s.aim = rowAim("beam");
        s.enter = [=] { ensureModel(hasLines({"beam =", "left =", "right ="}), beamModel(0, QString(), QString())); };
        s.done = [made] { return made("fixed"); };
        s.doneNote = T("Nice. A support is a model like any other: fixed_1.");
        s.show = [=](std::function<void()> over) {
            showPick({"beam", "left"}, {T("Engineering"), T("Structural"), T("New support or load"), "fixed"}, over);
        };
        add(s);
    }
    {
        currentChapter = 7;
        Step s;
        s.title = T("Pull the other end");
        s.text = T("Now the load. Select beam, then Ctrl+click right, right-click beam and choose Engineering ▸ Structural ▸ New support or load ▸ force. The force has a placeholder "
                   "where its vector goes: click the amber row and type (0, 0, -100) over the three dots -- 100 N, downwards.");
        s.hint = T("The vector is the force in newtons: x, y and z. Or press Show me.");
        buildLook(s);
        s.group = "static";
        s.aim = holeOrRow("beam");
        s.code = callLines("force");
        s.enter = [=] { ensureModel(made("fixed"), beamModel(1, QString(), QString())); };
        s.done = [madeAndFilled] { return madeAndFilled("force"); };
        s.doneNote = T("Nice. The beam is pulled at its right end.");
        s.show = [=](std::function<void()> over) {
            showPick({"beam", "right"}, {T("Engineering"), T("Structural"), T("New support or load"), "force"}, [=] {
                after(1800, [=] { fillDots("force", "(0, 0, -100)", over, 80); });
            });
        };
        add(s);
    }
    {
        currentChapter = 7;
        Step s;
        s.title = T("Choose a material");
        s.text = T("A part is made of something. Right-click the empty space of the viewport and choose Engineering ▸ New material ▸ aluminium. A material is a model too: its stiffness, "
                   "its density, its strength.");
        s.hint = T("The numbers are in the model: change them for another alloy. Or press Show me.");
        buildLook(s);
        s.group = "static";
        s.enter = [=] { ensureModel(madeAndFilled("force"), beamModel(2, "(0, 0, -100)", QString())); };
        s.done = [made] { return made("Material"); };
        s.doneNote = T("Nice. aluminium_1 is the material.");
        s.show = [=](std::function<void()> over) {
            showPick({}, {T("Engineering"), T("New material"), "aluminium"}, over);
        };
        add(s);
    }
    {
        currentChapter = 7;
        Step s;
        s.title = T("Run the analysis");
        s.text = T("Everything is there. Select beam, then Ctrl+click fixed_1, force_1 and aluminium_1, right-click beam and choose Engineering ▸ Structural ▸ static_analysis. "
                   "FielDes meshes the beam, solves the equations of elasticity and colours the beam by its stress.");
        s.hint = T("Red is where the material works hardest. The card at the right lists the numbers: the highest stress, how far the beam bends, the safety factor. Or press Show me.");
        buildLook(s);
        s.group = "static";
        s.aim = rowAim("beam");
        s.enter = [=] { ensureModel(made("Material"), beamModel(3, "(0, 0, -100)", QString())); };
        s.done = [made] { return made("static_analysis"); };
        s.doneNote = T("Nice. The stress is a field on the beam, drawn in colour. Click the eye of beam: it is drawn over its own analysis.");
        s.show = [=](std::function<void()> over) {
            showPick({"beam", "fixed_1", "force_1", "aluminium_1"}, {T("Engineering"), T("Structural"), "static_analysis"}, [=] {
                after(2500, [=] { hideModel("beam"); });        // (the stress is drawn on its own: the eye of the beam is clicked)
                over();
            });
        };
        add(s);
    }
    {
        currentChapter = 7;
        Step s;
        s.title = T("Change the load");
        s.text = T("A load is a number, and the analysis follows it. In force_1, change the vector from (0, 0, -100) to (0, 0, -300): three times the force, three times the stress.");
        s.hint = T("The call is open in the tree and the code. The beam is analysed again in a moment. Or press Show me.");
        buildLook(s);
        s.group = "static";
        s.openRows = QStringList{"force_1"};
        s.code = callLines("force");
        s.enter = [=] {
            ensureModel(made("static_analysis"), beamModel(4, "(0, 0, -100)", "None"));
            openCall("force");
            after(500, [=] { hideModel("beam"); });             // (the beam that is still shown is drawn over its analysis: the stress is drawn on its own)
        };
        s.done = [flatCall] {
            static const QRegularExpression re(R"(vector=\((-?[\d.]+),(-?[\d.]+),(-?[\d.]+)\))");
            const auto m = re.match(flatCall("force"));
            return m.hasMatch() && (m.captured(1).toDouble() != 0 || m.captured(2).toDouble() != 0 || m.captured(3).toDouble() != -100);
        };
        s.doneNote = T("Nice. The analysis ran again: the stress is higher.");
        s.show = [=](std::function<void()> over) {
            typeInCall("force", R"(vector=\(\s*-?[\d.]+\s*,\s*-?[\d.]+\s*,\s*(-?[\d.]+)\s*\))", "-300", over);
        };
        add(s);
    }

    // ---- Chapter 9: a flow
    const QString flowHead = tourHeaderFixed("[-4, -4, -4], [44, 24, 14]");
    // (the channel as the menus write it, stage by stage: 0 the post, the water around it and the two faces, 1 the fluid, 2 the inlet, 3 the outlet, 4 solved)
    auto flowModel = [flowHead](int stage, const QString& speed, const QString& boundaries, const QString& radius) {
        // (the post is drawn; the water, the faces it is in and the places it comes in and goes out at are not: a faint box over the result of the flow turns it brown.  faces and sides are the
        // boxes that say where the water slides along -- the top and the bottom of the channel, and its two sides -- as in example 16)
        QString s = flowHead + "post = cylinder(" + radius + ", 12, (14, 10, -1))\npost\nslab = box((0, 0, 0), (40, 20, 10))\ninlet_face = box((-1, -1, -1), (0.01, 21, 11))\n"
                    "faces = union(box((-1, -1, -1), (41, 21, 0.01)), box((-1, -1, 9.99), (41, 21, 11)))\n"
                    "sides = union(box((-1, -1, -1), (41, 0.01, 11)), box((-1, 19.99, -1), (41, 21, 11)))\n";
        if (stage >= 2) s += "inlet_1 = inlet(\n    region=inlet_face,\n    body=slab,\n    velocity=None,\n    speed=" + speed + ",\n    flow_rate=None,\n    profile='uniform',\n)  # shadow: slab, inlet_face\ninlet_1\n";
        s += "outlet_face = box((39.99, -1, -1), (41, 21, 11))\n";
        if (stage >= 3) s += "outlet_1 = outlet(\n    region=outlet_face,\n    body=slab,\n    pressure=0.0,\n)  # shadow: slab, outlet_face\noutlet_1\n";
        if (stage >= 1) s += "water_1 = Fluid(\n    'water',\n    1e-09,\n    1e-09,\n)\n";
        if (stage >= 1 && stage < 4) s += "water_1\n";
        if (stage >= 4)
            s += "fluid_analysis_1 = fluid_analysis(\n    post,\n    slab,\n    inlets=[inlet_1],\n    outlets=[outlet_1],\n    boundaries=" + boundaries +
                 ",\n    fluid=water_1,\n    element_size=None,\n    bounds=None,\n    gravity=None,\n    stokes=False,\n    tolerance=1e-05,\n    relaxation=1.0,\n    cache=True,\n"
                 "    time=None,\n    store_every=1,\n)  # shadow: post, slab, inlet_1, outlet_1, water_1\nfluid_analysis_1\nwater_1\n";
        return s;
    };
    {
        currentChapter = 8;
        Step s;
        s.title = T("Choose the fluid");
        s.text = T("A channel of water, with a post standing in it: post is the body, slab is the water around it (hidden), and two thin boxes mark the faces the water comes in and goes out at. "
                   "Right-click the empty space of the viewport and choose Engineering ▸ Fluid ▸ New fluid ▸ water.");
        s.hint = T("A fluid is a model, with its density and its viscosity. Or press Show me.");
        buildLook(s);
        s.group = "flow";
        s.enter = [=] { ensureModel(hasLines({"post =", "slab =", "inlet_face =", "outlet_face ="}), flowModel(0, QString(), QString(), "3")); };
        s.done = [made] { return made("Fluid"); };
        s.doneNote = T("Nice. water_1 is the fluid.");
        s.show = [=](std::function<void()> over) {
            showPick({}, {T("Engineering"), T("Fluid"), T("New fluid"), "water"}, over);
        };
        add(s);
    }
    {
        currentChapter = 8;
        Step s;
        s.title = T("Where the water comes in");
        s.text = T("Select slab, then Ctrl+click inlet_face, right-click slab and choose Engineering ▸ Fluid ▸ New flow condition ▸ inlet. Then type the speed over the placeholder: 5, "
                   "millimetres per second.");
        s.hint = T("The first model selected is the water, the second is where the condition acts. Or press Show me.");
        buildLook(s);
        s.group = "flow";
        s.aim = holeOrRow("slab");
        s.code = callLines("inlet");
        s.enter = [=] { ensureModel(made("Fluid"), flowModel(1, QString(), QString(), "3")); };
        s.done = [madeAndFilled] { return madeAndFilled("inlet"); };
        s.doneNote = T("Nice. The water comes in at the left face.");
        s.show = [=](std::function<void()> over) {
            showPick({"slab", "inlet_face"}, {T("Engineering"), T("Fluid"), T("New flow condition"), "inlet"}, [=] {
                after(1800, [=] { fillDots("inlet", "5", over, 120); });
            });
        };
        add(s);
    }
    {
        currentChapter = 8;
        Step s;
        s.title = T("Where it leaves");
        s.text = T("The same way for the other end: select slab, then Ctrl+click outlet_face, right-click slab and choose Engineering ▸ Fluid ▸ New flow condition ▸ outlet.");
        s.hint = T("An outlet has a pressure, 0 here: the water leaves freely. Or press Show me.");
        buildLook(s);
        s.group = "flow";
        s.aim = rowAim("slab");
        s.code = callLines("outlet");
        s.enter = [=] { ensureModel(madeAndFilled("inlet"), flowModel(2, "5", QString(), "3")); };
        s.done = [made] { return made("outlet"); };
        s.doneNote = T("Nice. The water leaves at the right face.");
        s.show = [=](std::function<void()> over) {
            showPick({"slab", "outlet_face"}, {T("Engineering"), T("Fluid"), T("New flow condition"), "outlet"}, over);
        };
        add(s);
    }
    {
        currentChapter = 8;
        Step s;
        s.title = T("Solve the flow");
        s.text = T("Select post, then Ctrl+click slab, inlet_1, outlet_1 and water_1, right-click post and choose Engineering ▸ Fluid ▸ fluid_analysis. The call has a placeholder for the "
                   "boundaries: type [slip(faces), slip(sides)] over it. faces and sides are boxes that mark the top and the bottom of the channel and its two sides, and a slip is a plane the "
                   "water slides along: the flow is two-dimensional, as in the examples.");
        s.hint = T("Everything that is not named is a wall at rest; walls, slips, inlets and outlets are the boundaries of a flow. The flow is solved in a few seconds. Or press Show me.");
        buildLook(s);
        s.group = "flow";
        s.aim = holeOrRow("post");
        s.code = callLines("fluid_analysis");
        s.enter = [=] { ensureModel(made("outlet"), flowModel(3, "5", QString(), "3")); };
        s.done = [madeAndFilled] { return madeAndFilled("fluid_analysis"); };
        s.doneNote = T("Nice. The streams go round the post; the colours are the speed.");
        s.show = [=](std::function<void()> over) {
            showPick({"post", "slab", "inlet_1", "outlet_1", "water_1"}, {T("Engineering"), T("Fluid"), "fluid_analysis"}, [=] {
                after(1800, [=] { fillDots("fluid_analysis", "[slip(faces), slip(sides)]", over, 70); });
            });
        };
        add(s);
    }
    {
        currentChapter = 8;
        Step s;
        s.title = T("Change the post");
        s.text = T("Make the post wider: in its line, change the radius, 3, to 5. The water has to go round more, and the flow is solved again.");
        s.hint = T("Nothing else is touched: the conditions and the analysis follow the post. Or press Show me.");
        buildLook(s);
        s.group = "flow";
        s.code = linesWith("^post =");
        s.enter = [=] { ensureModel(made("fluid_analysis"), flowModel(4, "5", "[slip(faces), slip(sides)]", "3")); };
        s.done = [callAt] {
            static const QRegularExpression re(R"(cylinder\(\s*(\d+\.?\d*))");
            const auto m = re.match(callAt("cylinder").text);
            return m.hasMatch() && m.captured(1).toDouble() > 3;
        };
        s.doneNote = T("Nice. The flow is different round a wider post.");
        s.show = [=](std::function<void()> over) {
            typeInCall("cylinder", R"(cylinder\(\s*(\d+\.?\d*))", "5", over);
        };
        add(s);
    }

    // ---- Chapter 10: a topology optimization
    const QString topologyCall = QString("topology_optimization_1 = topology_optimization(\n    static_analysis_1,\n    constraints=[minimize('compliance'), 'volume_fraction <= 0.4'],\n"
                                         "    iterations=None,\n    filter_radius=None,\n    keep=None,\n    avoid=None,\n    grow=False,\n    grow_limit=None,\n    extrude=None,\n    penalty=3.0,\n"
                                         "    sharpness=16.0,\n    symmetry='auto',\n    tolerance=1e-05,\n    cache=True,\n)  # shadow: static_analysis_1\ntopology_optimization_1\n");
    {
        currentChapter = 9;
        Step s;
        s.title = T("Find the lightest shape");
        s.text = T("The beam of chapter 8, analysed. An optimization asks where material can be taken away without losing stiffness. Select static_analysis_1, right-click it and choose "
                   "Engineering ▸ topology_optimization. Then type over the placeholder what it should do: [minimize('compliance'), 'volume_fraction <= 0.4'].");
        s.hint = T("Compliance is how much the beam bends: minimizing it makes the beam as stiff as it can be, in 40 % of the material. The optimizer decides how dense the material is at "
                   "every place -- a field. Or press Show me.");
        buildLook(s);
        s.group = "topology";
        s.aim = holeOrRow("static_analysis_1");
        s.code = callLines("topology_optimization");
        s.enter = [=] { ensureModel(made("static_analysis"), beamModel(4, "(0, 0, -100)", "3")); };
        s.done = [madeAndFilled] { return madeAndFilled("topology_optimization"); };
        s.doneNote = T("Nice. The red is where material stays; the blue is where it was taken away. Click the eye of static_analysis_1 to see the optimized part alone.");
        s.show = [=](std::function<void()> over) {
            showPick({"static_analysis_1"}, {T("Engineering"), "topology_optimization"}, [=] {
                after(1800, [=] {
                    fillDots("topology_optimization", "[minimize('compliance'), 'volume_fraction <= 0.4']", [=] {
                        after(1500, [=] { hideModel("static_analysis_1"); });       // (the stress of the analysis is drawn over the density of the optimization)
                        over();
                    }, 70);
                });
            });
        };
        add(s);
    }
    {
        currentChapter = 9;
        Step s;
        s.title = T("Allow less material");
        s.text = T("Ask for less: in the call of topology_optimization_1, change the 0.4 in 'volume_fraction <= 0.4' to 0.25 -- a quarter of the material. The optimizer runs again.");
        s.hint = T("The call is open in the tree and the code. The part that is left is thinner, and still carries the load. Or press Show me.");
        buildLook(s);
        s.group = "topology";
        s.openRows = QStringList{"topology_optimization_1"};
        s.code = callLines("topology_optimization");
        s.enter = [=] {
            const bool have = madeAndFilled("topology_optimization");
            ensureModel(have, beamModel(4, "(0, 0, -100)", "3") + topologyCall);
            openCall("topology_optimization");
            after(500, [=] { hideModel("static_analysis_1"); hideModel("beam"); });         // (the optimization is drawn on its own: the analysis and the beam would be drawn over it)
        };
        s.done = [flatCall, callAt, balanced] {
            static const QRegularExpression re(R"(volume_fraction<=([\d.]+))");
            const Statement st = callAt("topology_optimization");
            const auto m = re.match(flatCall("topology_optimization"));
            return st.first >= 0 && balanced(st.text) && m.hasMatch() && m.captured(1).toDouble() > 0 && m.captured(1).toDouble() < 0.4;
        };
        s.doneNote = T("Nice. Less material, found again.");
        s.show = [=](std::function<void()> over) {
            typeInCall("topology_optimization", R"(volume_fraction <= ([\d.]+))", "0.25", over);
        };
        add(s);
    }

    // ---- Chapter 11: a manufacturing optimization
    const QString mfgHead = tourHeader("[-24, -12, -4], [24, 12, 28]", 5);
    // (the part as the menus write it, stage by stage: 0 the part, 1 with the rule of a printer, 3 with the analysis)
    auto mfgModel = [mfgHead](int stage, const QString& overhang) {
        // (the cap's edges are rounded a little: the colours of the analysis are read at the corners of the mesh, and the corner of a sharp box edge has no lean of its own to read)
        QString s = mfgHead + "stem = box((-6, -6, 0), (6, 6, 16))\ncap = rounded_box((-18, -6, 16), (18, 6, 22), 0.3)\npart = union(stem, cap)\npart\n";
        if (stage >= 1)
            s += "\nfff_constraint_1 = fff_constraint(\n    nozzle=0.4,\n    build_direction=(0, 0, 1),\n    overhang=" + overhang + ",\n    max_bridge=20.0,\n    region=None,\n)\nfff_constraint_1\n";
        if (stage >= 3)
            s += "manufacturing_analysis_1 = manufacturing_analysis(\n    part,\n    constraints=[fff_constraint_1],\n    samples=5000,\n    keep=None,\n)  # shadow: part, fff_constraint_1\nmanufacturing_analysis_1\n";
        return s;
    };
    {
        currentChapter = 10;
        Step s;
        s.title = T("The rules of a printer");
        s.text = T("A part with a cap that hangs out 12 mm on both sides. A process has rules: a 3D printer needs walls thicker than its nozzle, and cannot print a wall that leans too far over "
                   "nothing. Select part, right-click it and choose Engineering ▸ Manufacturing ▸ New manufacturing constraint ▸ fff_constraint.");
        s.hint = T("fff is the printing that lays down melted plastic, layer by layer. Mills and moulds have their own constraints in the same menu. Or press Show me.");
        buildLook(s);
        s.group = "manufacturing";
        s.aim = rowAim("part");
        s.enter = [=] { ensureModel(hasLines({"stem =", "cap =", "part ="}), mfgModel(0, QString())); };
        s.done = [made] { return made("fff_constraint"); };
        s.doneNote = T("Nice. fff_constraint_1 is the rule.");
        s.show = [=](std::function<void()> over) {
            showPick({"part"}, {T("Engineering"), T("Manufacturing"), T("New manufacturing constraint"), "fff_constraint"}, over);
        };
        add(s);
    }
    {
        currentChapter = 10;
        Step s;
        s.title = T("How far it may lean");
        s.text = T("overhang=None means no limit on how far a wall may lean over the layer below. Say how far a printer can go: change None to 45, degrees.");
        s.hint = T("The call is open in the tree and the code. Or press Show me.");
        buildLook(s);
        s.group = "manufacturing";
        s.openRows = QStringList{"fff_constraint_1"};
        s.code = callLines("fff_constraint");
        s.enter = [=] { ensureModel(made("fff_constraint"), mfgModel(1, "None")); openCall("fff_constraint"); };
        s.done = [flatCall] {
            static const QRegularExpression re(R"(overhang=(\d+\.?\d*)[,)])");
            return re.match(flatCall("fff_constraint")).hasMatch();
        };
        s.doneNote = T("Nice. The rule says what the printer cannot do.");
        s.show = [=](std::function<void()> over) {
            typeInCall("fff_constraint", R"(overhang=(None))", "45", over);
        };
        add(s);
    }
    {
        currentChapter = 10;
        Step s;
        s.title = T("See where it breaks the rule");
        s.text = T("Select part, then Ctrl+click fff_constraint_1, right-click part and choose Engineering ▸ Manufacturing ▸ manufacturing_analysis. The part is coloured: red where "
                   "the rule is broken.");
        s.hint = T("The card at the right says how much of the surface breaks each rule. The colours are read at the corners of the mesh, so the edges of the cap are rounded a little. Or press Show me.");
        buildLook(s);
        s.group = "manufacturing";
        s.aim = rowAim("part");
        s.enter = [=] {
            static const QRegularExpression re(R"(overhang=(\d+\.?\d*)[,)])");
            ensureModel(re.match(flatCall("fff_constraint")).hasMatch(), mfgModel(1, "45"));
            m_view->standardView(View::VIEW_BOTTOM);            // (the red is under the cap: the part is seen from below)
        };
        s.done = [made] { return made("manufacturing_analysis"); };
        s.doneNote = T("Nice. The underside of the cap is red: a printer would let the plastic drop. Click the eye of part: it is drawn over its own analysis.");
        s.show = [=](std::function<void()> over) {
            showPick({"part", "fff_constraint_1"}, {T("Engineering"), T("Manufacturing"), "manufacturing_analysis"}, [=] {
                after(3500, [=] { hideModel("part"); });            // (the result is the part coloured: the part itself is drawn over it)
                over();
            });
        };
        add(s);
    }
    {
        currentChapter = 10;
        Step s;
        s.title = T("Let the tool fix it");
        s.text = T("The analysis and the part are hidden now. Select part and fff_constraint_1 again, right-click part and choose Engineering ▸ Manufacturing ▸ manufacturing_optimization. It changes the part "
                   "until the rule is kept, and says what it changed.");
        s.hint = T("The result's card lists what was added and what was cut. The tool never changes more than max_change of the part. Or press Show me.");
        buildLook(s);
        s.group = "manufacturing";
        s.aim = rowAim("part");
        s.enter = [=] {
            ensureModel(made("manufacturing_analysis"), mfgModel(3, "45"));
            m_view->standardView(View::VIEW_BOTTOM);
            after(500, [=] { hideModel("manufacturing_analysis_1"); hideModel("part"); });         // (the script of the step has run once: the lines that show the analysis and the part are there)
        };
        s.done = [made] { return made("manufacturing_optimization"); };
        s.doneNote = T("Nice. The part keeps the rule now.");
        s.show = [=](std::function<void()> over) {
            showPick({"part", "fff_constraint_1"}, {T("Engineering"), T("Manufacturing"), "manufacturing_optimization"}, over);
        };
        add(s);
    }

    // ---- Chapter 12: a FielDes app
    const QString appHead = tourHeader("[-35, -25, -8], [35, 25, 30]", 4);
    auto appModel = [appHead](const QString& size) {
        return appHead + "app.title('Block maker', 'A block of the size you choose', '1.0')\nsize = " + size + "\nblock = box((-size, -12, 0), (size, 12, 10))\nblock";
    };
    {
        currentChapter = 11;
        Step s;
        s.title = T("An input instead of a number");
        s.text = T("This script makes a block as wide as size says. To let someone who never opens the editor choose the width, write the number as an input: in the size line, replace 24 "
                   "with app.number('Size', 24, min=10, max=30).");
        s.hint = T("In the editor the input is just its default, 24: the script runs as always. In the app it is a field. There are texts, choices, check boxes and files too. Or press Show me.");
        s.holes = bothAndScript;
        s.cardNear = [this]() { return widgetRect(m_view); };
        s.place = InsideBottomLeft;
        s.showView = true;
        s.keys = true;
        s.group = "app";
        s.closeRows = true;
        s.code = linesWith("^(app\\.title|size) ");
        s.enter = [=] { ensureModel(m_editor->getScript().contains("size = 24"), appModel("24")); };
        s.done = [this] { return QRegularExpression("^size\\s*=\\s*app\\.number\\(.+\\)\\s*$", QRegularExpression::MultilineOption).match(m_editor->getScript()).hasMatch(); };
        s.doneNote = T("Nice. size is an input now.");
        s.show = [this](std::function<void()> over) {
            const int line = scriptLine("size =");
            if (line < 0) { over(); return; }
            const QString text = m_editor->getScript().split('\n').value(line);
            const int at = text.indexOf("24");
            if (at < 0) { over(); return; }
            typeOver(line, at, at + 2, "app.number('Size', 24, min=10, max=30)", over, 50);
        };
        add(s);
    }
    {
        currentChapter = 11;
        Step s;
        s.title = T("See it as the user will");
        s.text = T("File ▸ Preview as app opens the form the way the person who gets the app sees it: a window with the input, a Run button and the result. Change the number there and press Run.");
        s.hint = T("The window reads the script again by itself when you change it. Preview it yourself when the tour is over.");
        s.menuTitle = "File";
        s.menuEntry = "Preview as app";
        s.place = Right;
        s.group = "app";
        s.enter = [] { while (auto m = QApplication::activePopupWidget()) m->close(); };
        add(s);
    }
    {
        currentChapter = 11;
        Step s;
        s.title = T("Send it as a file");
        s.text = T("File ▸ Export as app... bundles the script, the blocks it uses and the files it needs into one .fieldesapp file. Whoever receives it double-clicks it, fills in the form "
                   "and presses Run -- FielDes does the rest.");
        s.hint = T("Settings ▸ Open .fieldesapp files with FielDes makes the double-click work. The examples 24_app_bracket_maker and 25_app_beam_load_case are apps.");
        s.menuTitle = "File";
        s.menuEntry = "Export as app...";
        s.place = Right;
        s.group = "app";
        s.enter = [] { while (auto m = QApplication::activePopupWidget()) m->close(); };
        add(s);
    }
    // 22
    {
        currentChapter = 5;
        Step s;
        s.title = T("Custom function from block");
        s.text = T("A Python file in the blocks folder is a block: its functions work in every script, like the built-in ones.");
        s.hint = T("Settings ▸ Blocks folder chooses the folder. The right-click menu lists them under Parametric shapes ▸ New custom block.");
        s.menuTitle = "Settings";
        s.menuEntry = "Blocks folder...";
        s.place = Right;
        add(s);
    }
    // 26: the keys worth knowing (at the end: they are of what the steps before showed)
    {
        currentChapter = 1;
        Step s;
        s.title = T("Keys worth knowing");
        // (one key a line: the card is narrow, and a line with several keys wraps in the middle of a key in most languages)
        s.text = T("With the viewport focused:") + "\n\n" +
                 T("General") + "\n" +
                 T("D  delete") + "\n" +
                 T("R  lock") + "\n" +
                 T("I  isolate") + "\n" +
                 T("V  show or hide") + "\n" +
                 T("E  next gizmo mode") + "\n" +
                 T("Shift+X  section view") + "\n" +
                 T("Home  frame everything") + "\n\n" +
                 T("Copies") + "\n" +
                 T("Shift+D  copy") + "\n" +
                 T("Ctrl+D  link") + "\n\n" +
                 T("Work plane") + "\n" +
                 T("Shift+W  work plane") + "\n" +
                 T("Ctrl+Shift+W  new work plane") + "\n" +
                 T("W  look at the work plane") + "\n" +
                 T("E  next kind of drawing, or the last one") + "\n" +
                 T("Shift+S  symmetry on or off") + "\n" +
                 T("Right click  stop drawing");
        s.hint = T("The pane that has the keyboard has a blue frame, and the status bar says which.") + " " +
                 T("Shift+F1 lists every shortcut. The examples 26, 27 and 28 show profiles, linked copies and the work plane.");
        s.place = Right;
        s.enter = [this] { m_view->holdShift(false); m_view->setWorkPlaneOpen(false); };
        add(s);
    }
    // 23
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
    // The card a skipped tour ends with: where the tour is found again, framed in the menu that has it
    {
        Step s;
        s.title = T("The tour stays here");
        s.text = T("You can start the guided tour again whenever you like: Help ▸ Guided tour.");
        s.hint = T("Shift+F1 opens the guide with every feature and shortcut. File ▸ Open example file lists the examples.");
        s.last = true;
        s.exitCard = true;
        s.menuTitle = "Help";
        s.menuEntry = "Guided tour";
        s.place = Right;
        s.enter = [] { while (auto m = QApplication::activePopupWidget()) m->close(); };
        add(s);
    }
    // What a step is left with is not what the next one starts from: a step that works on the model of the tour (the steps of a group make a model of their own, and
    // the group puts the user's back) puts it back as it was when it was opened -- the number that was changed, the rows that were deleted or renamed, the
    // section that was made -- so that every step starts from its own state, whatever was done before it
    // The chapters: the steps were written in the order they were made in, and are put in the order of the tour now -- the welcome card, then the chapters one after the other,
    // each ending with a card that congratulates and asks whether to go on or to explore alone (the last one ends with "That is the tour"), and the card that says where the
    // tour is found again
    {
        QVector<Step> welcome, body, closing;
        for (const Step& s : steps)
        {
            if (s.welcome) welcome << s;
            else if (s.last) closing << s;
            else body << s;
        }
        std::stable_sort(body.begin(), body.end(), [](const Step& a, const Step& b) { return a.chapter < b.chapter; });
        QVector<Step> ordered = welcome;
        for (int c = 0; c < chapterCount(); ++c)
        {
            for (const Step& s : body)
                if (s.chapter == c) ordered << s;
            if (c + 1 < chapterCount())
            {
                Step e;
                e.chapter = c;
                e.chapterEnd = true;
                e.title = T("Chapter %1 is done").arg(c + 1);
                e.text = chapterLearned(c) + "\n\n" + T("Next is chapter %1: %2.").arg(c + 2).arg(chapterTitle(c + 1));
                e.hint = T("Continue goes on with it. Explore on my own leaves the tour here: Help ▸ Guided tour brings it back at the next chapter.");
                e.place = Center;
                ordered << e;
            }
        }
        for (Step& s : closing)
        {
            if (s.exitCard) continue;
            s.chapter = chapterCount() - 1;             // ("That is the tour": the end of the last chapter)
            s.chapterEnd = true;
        }
        for (const Step& s : closing) ordered << s;
        steps = ordered;
    }
    for (int i = 1; i < steps.size(); ++i)
        if (steps[i].group.isEmpty() && !steps[i].last) steps[i].undone = true;
    m_entry.resize(steps.size());
    m_hasEntry = QVector<char>(steps.size(), 0);
}

////////////////////////////////////////////////////////////////////////////////
// Chapters, and where the user was

int Tutorial::chapterCount() { return 12; }

QString Tutorial::chapterTitle(int c) const
{
    switch (c)
    {
    case 0: return T("How FielDes works: the script and fields");
    case 1: return T("Navigation, gizmos and shortcuts");
    case 2: return T("Modelling with objects and the model tree");
    case 3: return T("Work planes, 2D shapes, paths and extrusions");
    case 4: return T("Symmetry");
    case 5: return T("The special features, in short");
    case 6: return T("Build a lattice");
    case 7: return T("Build a static analysis");
    case 8: return T("Build a flow analysis");
    case 9: return T("Build a topology optimization");
    case 10: return T("Build a manufacturing optimization");
    case 11: return T("Build a FielDes app");
    }
    return QString();
}

// What each chapter was about: the congratulation on the card that ends it
QString Tutorial::chapterLearned(int c) const
{
    switch (c)
    {
    case 0: return T("Well done. The script is the model, and a field -- a number at every place in space -- can stand wherever a number does: an offset, a wall, a load.");
    case 1: return T("Well done. You can turn, move and zoom the view, move a model with its gizmo, and you know the keys that work on what is selected.");
    case 2: return T("Well done. You can make models, change them in the viewport, the model tree and the code, and they stay one thing whichever you change them in.");
    case 3: return T("Well done. You can draw a shape on a work plane, pull its points, and make a body of it by extruding or sweeping it.");
    case 4: return T("Well done. A symmetry makes the copies and keeps them true: you edit the original and the images follow.");
    case 5: return T("Well done. You have seen what else FielDes does: lattices, analyses, flows, manufacturing and apps. The next chapters build a small one of each.");
    case 6: return T("Well done. You built a lattice, and made it follow a field.");
    case 7: return T("Well done. You analysed a part: a support, a load, and the stress that results.");
    case 8: return T("Well done. You solved a flow around a body and looked at what it does.");
    case 9: return T("Well done. You let an optimizer find the shape that carries a load with the least material.");
    case 10: return T("Well done. You checked a part against the rules of a process and let the tool change it until it keeps them.");
    }
    return QString();
}

int Tutorial::chapter() const
{
    return m_index >= 0 && m_index < m_steps->size() ? (*m_steps)[m_index].chapter : -1;
}

int Tutorial::chapterStep(int c) const
{
    for (int i = 0; i < m_steps->size(); ++i)
        if ((*m_steps)[i].chapter == c && !(*m_steps)[i].exitCard) return i;
    return -1;
}

void Tutorial::goToChapter(int c)
{
    if (!m_active || c < 0 || c >= chapterCount()) return;
    const int index = chapterStep(c);
    if (index >= 0) goTo(index);
}

void Tutorial::exploreAlone()
{
    if (!m_active || m_index < 0 || m_index >= m_steps->size()) return;
    const int c = (*m_steps)[m_index].chapter;
    QSettings st;
    // (the chapter is done, and the tour is left at the start of the next one: Help > Guided tour goes on from there)
    QStringList done = st.value("tour/chaptersDone").toStringList();
    if (c >= 0 && !done.contains(QString::number(c))) done << QString::number(c);
    st.setValue("tour/chaptersDone", done);
    st.setValue("tour/chapter", std::min(c + 1, chapterCount() - 1));
    st.setValue("tour/step", 0);
    goTo(m_steps->size() - 1);              // (the card that says where the tour is found again)
}

void Tutorial::saveProgress()
{
    if (m_index < 0 || m_index >= m_steps->size()) return;
    const Step& s = (*m_steps)[m_index];
    if (s.welcome || s.exitCard || s.chapter < 0) return;
    QSettings st;
    if (s.chapterEnd)
    {
        // (a chapter is done when the card that ends it is reached)
        QStringList done = st.value("tour/chaptersDone").toStringList();
        if (!done.contains(QString::number(s.chapter))) done << QString::number(s.chapter);
        st.setValue("tour/chaptersDone", done);
    }
    st.setValue("tour/chapter", s.chapter);
    st.setValue("tour/step", m_index - chapterStep(s.chapter));
}

int Tutorial::resumeIndex() const
{
    QSettings st;
    const int c = std::max(0, std::min(chapterCount() - 1, st.value("tour/chapter", 0).toInt()));
    const int step = std::max(0, st.value("tour/step", 0).toInt());
    const int first = chapterStep(c);
    if (first < 0) return 1;
    int last = first;                       // (inside the chapter: up to the card that ends it)
    while (last + 1 < m_steps->size() && (*m_steps)[last + 1].chapter == c && !(*m_steps)[last + 1].exitCard) ++last;
    return std::min(first + step, last);
}

void Tutorial::showChapters()
{
    if (!m_active) return;
    const QStringList done = QSettings().value("tour/chaptersDone").toStringList();
    const int current = chapter();
    // (a child of the card: the keys and the mouse of the card's own widgets are not held back by the tour)
    QMenu* menu = new QMenu(m_card);
    menu->setObjectName("TourChapterMenu");
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->setStyleSheet("QMenu { background: #0d3a4a; color: #eee8d5; border: 1px solid #ffcc33; padding: 4px; }"
                        "QMenu::item { padding: 5px 18px 5px 10px; }"
                        "QMenu::item:selected { background: rgba(255, 204, 51, 90); }");
    for (int c = 0; c < chapterCount(); ++c)
    {
        QAction* a = menu->addAction(QString("%1  %2.  %3").arg(done.contains(QString::number(c)) ? QString("✓") : QString("○")).arg(c + 1).arg(chapterTitle(c)));
        if (c == current)
        {
            QFont f = a->font();
            f.setBold(true);
            a->setFont(f);
        }
        connect(a, &QAction::triggered, this, [this, c] { goToChapter(c); });
    }
    menu->ensurePolished();
    const QPoint above = m_chapters->mapToGlobal(QPoint(0, 0)) - QPoint(0, menu->sizeHint().height() + 2);
    const QPoint below = m_chapters->mapToGlobal(QPoint(0, m_chapters->height() + 2));
    menu->popup(above.y() > 0 ? above : below);
}

bool Tutorial::tourModelHere() const
{
    const QString now = m_editor->getScript();
    return now.contains("drilled =") && now.contains("plate =");
}

void Tutorial::ensureTourModel(std::function<void()> then)
{
    // (the steps of the first chapters work on the model of the tour: a step reached by the chapter menu, or where the user left the tour, finds another script in the editor)
    ++m_modelTries;
    if (m_loadTour && !m_loadTour()) { m_modelTries = 0; stop(false); return; }
    m_scriptSeen.clear();
    m_changed.clear();
    ++m_generation;
    after(900, [then] { then(); });
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

void Tutorial::start(int chapter)
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
    m_hasGroup = false;
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
    // (where the user was -- or, for a chapter that was asked for, its first step)
    const int index = chapter >= 0 && chapterStep(chapter) >= 0 ? chapterStep(chapter) : resumeIndex();
    m_modelTries = 0;
    after(900, [this, index] { goTo(std::max(1, index)); });
}

void Tutorial::stop(bool completed)
{
    const bool wasActive = m_active;
    if (wasActive && completed)
    {
        // (the tour was gone through to its end: the next time it starts at the first chapter)
        QSettings st;
        st.setValue("tour/chapter", 0);
        st.setValue("tour/step", 0);
    }
    endDemo();                              // (the tour ends with the program as the user had it, not as a demonstration left it)
    if (wasActive && m_index >= 0 && m_index < m_steps->size() && (*m_steps)[m_index].leave) (*m_steps)[m_index].leave();
    if (wasActive && m_index >= 0 && m_index < m_steps->size() && (*m_steps)[m_index].undone && m_editor->getScript() != m_stepScript)
    {
        m_view->scenePanel()->clearSelection();
        useScript(m_stepScript, false);
    }
    if (wasActive && m_hasGroup)
    {
        restore(m_groupBefore);                 // (the models the steps made for themselves are not the user's)
        m_hasGroup = false;
    }
    if (wasActive) m_view->scenePanel()->showAll();
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
    // A step of the first chapters works on the model of the tour: when the editor has another script -- the step was reached by the chapter menu, or where the user left
    // the tour -- the tour's model is put there first (and the step is opened once it has run)
    if (m_active && (*m_steps)[index].needsTourModel && !tourModelHere() && m_modelTries < 2)
    {
        ensureTourModel([this, index, viaBack] { goTo(index, viaBack); });
        return;
    }
    m_modelTries = 0;
    endDemo();                              // (what a "Show me" did is undone before the step changes)
    if (m_index >= 0 && m_index < m_steps->size())
    {
        const Step& old = (*m_steps)[m_index];
        if (old.leave) old.leave();
        if (old.undone && m_editor->getScript() != m_stepScript)
        {
            m_view->scenePanel()->clearSelection();         // (a model that stays selected would write its placement arguments again)
            useScript(m_stepScript, false);
        }
        // The steps of a group work on models of their own: when the group is left the user's model is back
        if (!old.group.isEmpty() && old.group != (*m_steps)[index].group && m_hasGroup)
        {
            restore(m_groupBefore);
            m_hasGroup = false;
        }
    }
    ++m_generation;
    m_index = index;
    m_done = false;
    showDoneLook(false);
    m_viaBack = viaBack;
    m_busy = false;
    hideCursor();
    while (auto m = QApplication::activePopupWidget()) m->close();
    if (!(*m_steps)[index].group.isEmpty() && !m_hasGroup)
    {
        m_groupBefore = capture();
        m_hasGroup = true;
    }
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
    if ((*m_steps)[m_index].last || m_index + 1 >= m_steps->size()) { stop(true); return; }
    goTo(m_index + 1);
}

void Tutorial::skip()
{
    if (!m_active) return;
    // The card that ends a chapter has "Explore on my own" where the other cards have "Quit the tour": the tour is left at the next chapter
    if (m_index >= 0 && m_index < m_steps->size() && (*m_steps)[m_index].chapterEnd && !(*m_steps)[m_index].last)
    {
        exploreAlone();
        return;
    }
    // (a tour that is left is never left without saying where it is: the last step is that card)
    const int card = m_steps->size() - 1;
    if (m_index == card || !(*m_steps)[card].exitCard) { stop(false); return; }
    goTo(card);
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
        // A moment to look at what was done, then back -- but what was done is looked at once it is on the screen: a model that is still being made
        // (the script runs, the viewport draws) is not what the demonstration is for, and putting everything back then shows nothing
        auto settle = std::make_shared<std::function<void(int)>>();
        *settle = [this, step, gen, settle](int waited) {
            if (!m_demo || gen != m_generation) return;
            // (a result that takes a minute -- an analysis, a flow, an optimization -- is waited for: the demonstration is of what it makes)
            if ((m_editor->scriptRunning() || m_view->isRendering()) && waited < 60000)
            {
                after(200, [settle, waited] { (*settle)(waited + 200); });
                return;
            }
            // (how long the result stays on show before everything is put back: FIELDES_TOUR_HOLD_MS makes it longer for a test window that takes its pictures of it)
            static const int hold = std::max(1500, qEnvironmentVariableIntValue("FIELDES_TOUR_HOLD_MS"));
            after(hold, [this, step, gen] {
                if (!m_demo || gen != m_generation) return;
                endDemo();
                const Step& s = (*m_steps)[step];
                if (s.enter) s.enter();
                m_note->setText(s.hint + (s.hint.isEmpty() ? "" : "\n") + T("Everything is back as it was. Now you try."));
                m_note->setStyleSheet("font-size: 9pt; color: #93a1a1;");
            });
        };
        after(800, [settle] { (*settle)(0); });          // (the run an edit makes starts a moment after the edit)
    };
    s.show(over);
    // (a demonstration that never says it is over -- what it was to work on is not there -- does not hold the step)
    static const int holdMore = std::max(0, qEnvironmentVariableIntValue("FIELDES_TOUR_HOLD_MS") - 1500);
    after(100000 + holdMore, [this, gen] { if (m_demo && gen == m_generation) { endDemo(); refresh(); } });
}

// The card and every outline of the tour are green once what the step asks is done -- yellow means there is still something to do
void Tutorial::showDoneLook(bool done)
{
    m_card->setProperty("done", done);
    m_title->setStyleSheet(QString("font-size: 12.5pt; font-weight: bold; color: %1;").arg(done ? "#82cc58" : "#ffcc33"));
    for (QWidget* w : {static_cast<QWidget*>(m_card), static_cast<QWidget*>(m_next)})
    {
        w->style()->unpolish(w);
        w->style()->polish(w);
        w->update();
    }
}

void Tutorial::showStep()
{
    const Step& s = (*m_steps)[m_index];
    m_title->setText(s.title);
    m_text->setText(s.text);
    m_note->setText(s.hint);
    m_note->setStyleSheet("font-size: 9pt; color: #93a1a1;");
    m_note->setVisible(!s.hint.isEmpty());
    {
        // (the chapter and the step in it: "Chapter 3 of 12  ·  4 / 15"; the card that ends a chapter says the chapter only)
        QString counter;
        if (!s.welcome && !s.exitCard && s.chapter >= 0)
        {
            counter = T("Chapter %1 of %2").arg(s.chapter + 1).arg(chapterCount());
            if (!s.chapterEnd)
            {
                const int first = chapterStep(s.chapter);
                int total = 0;
                for (int i = first; i >= 0 && i < m_steps->size() && (*m_steps)[i].chapter == s.chapter && !(*m_steps)[i].chapterEnd; ++i) ++total;
                counter += QString("  ·  %1 / %2").arg(m_index - first + 1).arg(total);
            }
        }
        m_counter->setText(counter);
        m_counter->setVisible(!counter.isEmpty());
    }
    m_chapters->setVisible(!s.welcome && !s.exitCard);
    m_skipTour->setText(s.welcome ? T("Skip the guided tour") : s.chapterEnd ? T("Explore on my own") : T("Quit the tour"));
    // (the two words do almost the same: the tour is left, and the card that says where it is found comes -- they differ in where the tour goes on, and the button says so)
    m_skipTour->setToolTip(s.welcome ? T("Not now. Help ▸ Guided tour starts it whenever you like.")
                           : s.chapterEnd ? T("Leave the tour after this chapter. Help ▸ Guided tour goes on with the next chapter.")
                                          : T("Leave the tour here. Help ▸ Guided tour goes on from this step."));
    m_skipTour->setVisible(!s.last);
    m_back->setVisible(!s.welcome && !s.last && m_index > 1);
    m_show->setVisible(bool(s.show));
    m_show->setText(s.showLabel);
    m_next->setText(s.welcome ? T("Start the guided tour") : (s.chapterEnd && !s.last) ? T("Continue  ▸") : s.last ? T("Finish") : (s.done ? T("Skip step  ▸") : T("Next  ▸")));
    if (s.exitCard)
    {
        // (the card that says where the tour is found says where it goes on, too: the step the user left it at is kept)
        const Step& at = (*m_steps)[resumeIndex()];
        if (at.chapter >= 0)
            m_text->setText(s.text + "\n\n" + T("It goes on where you stopped: chapter %1, %2.").arg(at.chapter + 1).arg(chapterTitle(at.chapter)));
    }
    // (the welcome card is the one place the button is orange: it is what the user is asked to choose, and the other way out is quiet)
    m_next->setStyleSheet(s.welcome
        ? "QPushButton { background: #ff8a1f; color: #2b1500; border: 1px solid #ff8a1f; border-radius: 6px; padding: 6px 16px; font-size: 10pt; font-weight: bold; }"
          "QPushButton:hover { background: #ffa347; border-color: #ffa347; }"
        : QString());
    // (every button is as wide as its text: see fitButton)
    fitButton(m_chapters, false, 10);
    fitButton(m_skipTour, false, 4);
    fitButton(m_back, false, 12);
    fitButton(m_show, false, 12);
    fitButton(m_next, true, s.welcome ? 16 : 12);
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
    m_stepScript = m_editor->getScript();
    if (s.closeRows) m_view->scenePanel()->showOnly(s.openRows);
    else m_view->scenePanel()->showAll();
    if (s.enter) s.enter();
    // (what the step asks is done already: this is a step that was done before and not put back, or a state the user is in --
    // it does not move on by itself)
    m_doneOnArrival = s.done && s.done();
    saveProgress();                         // (the step the user is at is kept: Help > Guided tour goes on from it)
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
    layer->accent = m_done ? kDone : kAmber;
    layer->dim = true;
    QRect target;
    for (const QRect& h : holes) target = target.isNull() ? h : target.united(h);
    // What the card must not cover: the row the step points at, and the lines of the script it is about
    const QRect aimRect = s.aim ? s.aim() : QRect();
    const QVector<int> codeLines = s.code ? s.code() : QVector<int>();
    QVector<QRect> keep;
    if (!aimRect.isEmpty()) keep << aimRect;
    {
        Script* sc = m_editor->scriptWidget();
        if (sc && sc->isVisible())
            for (int line : codeLines)
            {
                const QRect r = scriptLineRect(line);
                if (!r.isEmpty()) keep << r;
            }
        if (s.focus)
        {
            const QRect f = s.focus();
            if (!f.isEmpty()) keep << f;
        }
    }
    layoutCard(s.cardNear ? s.cardNear() : target, keep);
    QPoint to;
    bool arrow = false;
    if (!s.welcome && (!s.last || !entryRect.isNull()) && s.place != Center)
    {
        if (!entryRect.isNull())
        {
            to = QPoint(entryRect.right() + 3, entryRect.center().y());         // (the arrow ends at the right edge of the entry)
            arrow = true;
        }
        else if (!aimRect.isEmpty())
        {
            // (it ends at the edge of the row that faces the card: the arrow never crosses the row, or the rows beside it)
            const QRect c = m_card->geometry();
            if (c.center().x() > aimRect.right()) to = QPoint(aimRect.right() + 4, aimRect.center().y());
            else if (c.center().x() < aimRect.left()) to = QPoint(aimRect.left() - 4, aimRect.center().y());
            else if (c.center().y() > aimRect.bottom()) to = QPoint(std::max(aimRect.left(), std::min(aimRect.right(), c.center().x())), aimRect.bottom() + 4);
            else to = QPoint(std::max(aimRect.left(), std::min(aimRect.right(), c.center().x())), aimRect.top() - 4);
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
    if (!aimRect.isEmpty()) layer->frames << aimRect;
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
            const QVector<int>& lines = codeLines;
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
    // (a step that changes a model by a number in the code: the viewport is not greyed, so that the model that changes is where the eye goes)
    if (s.showView)
    {
        const QRect view = widgetRect(m_view);
        if (!view.isEmpty()) layer->lit << view;
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

void Tutorial::layoutCard(const QRect& hole, const QVector<QRect>& keepClear)
{
    // (the card is as wide as the row of its buttons needs when that is more than the usual width -- a larger font, a longer language -- so that no button is cut off at its edge)
    m_card->setMinimumWidth(0);
    m_card->setMaximumWidth(QWIDGETSIZE_MAX);
    m_card->layout()->activate();
    const int cardWidth = std::max(kCardWidth, m_card->layout()->minimumSize().width());
    m_card->setFixedWidth(cardWidth);
    m_card->adjustSize();
    // (the words wrap at the card's width: its height is what they need there, not what one line of them would)
    m_card->layout()->activate();
    const int wrapped = m_card->layout()->hasHeightForWidth() ? m_card->layout()->totalHeightForWidth(cardWidth) : 0;
    m_card->resize(cardWidth, std::max(wrapped, m_card->sizeHint().height()));
    const Step& s = (*m_steps)[m_index];
    const QRect win = m_window->rect();
    const int margin = (s.point || s.aim || !s.menuTitle.isEmpty()) ? 46 : 24;
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
    // The places the card can stand, in the order the step prefers them; the first that is in the window and covers nothing the step is about
    // (the row it points at, the lines of the script it asks for) is where it goes
    auto spot = [&](Place p) {
        QRect c = card;
        switch (p)
        {
        case Right: c.moveTopLeft(QPoint(hole.right() + margin, hole.top())); break;
        case Left: c.moveTopRight(QPoint(hole.left() - margin, hole.top())); break;
        case Below: c.moveTopLeft(QPoint(hole.left(), hole.bottom() + margin)); break;
        case Above: c.moveBottomLeft(QPoint(hole.left(), hole.top() - margin)); break;
        case InsideTopRight: c.moveTopRight(QPoint(hole.right() - margin, hole.top() + margin)); break;
        case InsideBottom:
            c.moveCenter(QPoint(hole.center().x(), 0));
            c.moveBottom(hole.bottom() - 46);
            break;
        case InsideBottomLeft: c.moveBottomLeft(QPoint(hole.left() + 24, hole.bottom() - 46)); break;
        default: break;
        }
        c.moveLeft(std::max(8, std::min(win.right() - c.width() - 8, c.left())));
        c.moveTop(std::max(8, std::min(win.bottom() - c.height() - 8, c.top())));
        return c;
    };
    QList<Place> order;
    if (place == Right) order << Right << Left << Below << Above;
    else if (place == Left) order << Left << Right << Below << Above;
    else if (place == Below) order << Below << Above << Right << Left;
    else if (place == Above) order << Above << Below << Right << Left;
    else if (place == InsideTopRight) order << InsideTopRight << InsideBottom;
    else if (place == InsideBottom) order << InsideBottom << InsideTopRight;
    else if (place == InsideBottomLeft) order << InsideBottomLeft << InsideBottom << InsideTopRight;
    else order << Right << Left << Below << Above;
    for (Place p : {InsideBottom, InsideBottomLeft, InsideTopRight, Right, Left, Below, Above})
        if (!order.contains(p)) order << p;
    auto clear = [&](const QRect& c) {
        for (const QRect& k : keepClear)
            if (c.intersects(k.adjusted(-6, -4, 6, 4))) return false;
        return true;
    };
    const QRect inner = win.adjusted(8, 8, -8, -8);
    QRect chosen;
    // (beside what the step is about, without crossing it, where there is room; else anywhere that covers nothing it points at)
    for (Place p : order)
    {
        const QRect c = spot(p);
        const bool beside = (p == Right || p == Left || p == Below || p == Above);
        if (!inner.contains(c) || !clear(c)) continue;
        if (beside && c.intersects(hole)) continue;                         // (beside it means beside: not over it)
        chosen = c;
        break;
    }
    if (chosen.isNull())
    {
        for (Place p : order)
        {
            const QRect c = spot(p);
            if (inner.contains(c) && clear(c)) { chosen = c; break; }
        }
    }
    if (chosen.isNull())
    {
        // (no room at all: over the bottom of what it is about)
        card.moveCenter(QPoint(hole.center().x(), 0));
        card.moveBottom(std::min(hole.bottom() - 30, win.bottom() - 20));
        card.moveLeft(std::max(8, std::min(win.right() - card.width() - 8, card.left())));
        card.moveTop(std::max(8, std::min(win.bottom() - card.height() - 8, card.top())));
        chosen = card;
    }
    m_card->move(chosen.topLeft());
}

bool Tutorial::eventFilter(QObject* obj, QEvent* e)
{
    if (!m_active) return false;
    if (obj == m_view && (e->type() == QEvent::MouseButtonPress || e->type() == QEvent::Wheel)) m_viewTouched = true;
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
