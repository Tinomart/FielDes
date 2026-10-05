/*
FielDes: field-driven design
Derived from Studio, a simple GUI for the libfive CAD kernel

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/
#include <cmath>
#include <cstdio>
#include <functional>

#include <algorithm>

#include <QAbstractItemView>
#include <QCheckBox>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSet>
#include <QSettings>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

#include "fieldes/scenetree.hpp"
#include "fieldes/color.hpp"

namespace FielDes {

namespace {

enum Role { ROLE_ITEM = Qt::UserRole, ROLE_TYPE, ROLE_PART, ROLE_KEY, ROLE_LINE };
enum Column { COL_NAME = 0, COL_EYE, COL_HANDLES, COL_LOCK, COL_CACHE, COL_ACTION, COL_RESET, COL_DELETE };

const QColor kText(0xee, 0xe8, 0xd5);
const QColor kDim(0x93, 0xa1, 0xa1);

// Small line-art icons, drawn at 2x for high-DPI screens
QIcon makeIcon(std::function<void(QPainter&)> draw)
{
    QPixmap px(32, 32);
    px.fill(Qt::transparent);
    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(2, 2);
    draw(p);
    p.end();
    px.setDevicePixelRatio(2);
    return QIcon(px);
}

const QIcon& eyeIcon(bool open)
{
    static QIcon on = makeIcon([](QPainter& p) {
        p.setPen(QPen(kText, 1.3));
        QPainterPath eye;
        eye.moveTo(1.5, 8);
        eye.quadTo(8, 1.5, 14.5, 8);
        eye.quadTo(8, 14.5, 1.5, 8);
        p.drawPath(eye);
        p.setBrush(kText);
        p.drawEllipse(QPointF(8, 8), 2.2, 2.2);
    });
    static QIcon off = makeIcon([](QPainter& p) {
        QColor c = kDim;
        c.setAlpha(150);
        p.setPen(QPen(c, 1.3));
        QPainterPath eye;
        eye.moveTo(1.5, 8);
        eye.quadTo(8, 13, 14.5, 8);
        p.drawPath(eye);
        for (double x : {4.0, 8.0, 12.0})
        {
            p.drawLine(QPointF(x, 10.5), QPointF(x + (x - 8) * 0.15, 13));
        }
    });
    return open ? on : off;
}

const QIcon& reimportIcon()
{
    static QIcon i = makeIcon([](QPainter& p) {
        p.setPen(QPen(kText, 1.4));
        p.drawArc(QRectF(3, 3, 10, 10), 30 * 16, 290 * 16);
        QPainterPath head;
        head.moveTo(13.6, 2.8);
        head.lineTo(13.4, 7.2);
        head.lineTo(9.3, 5.8);
        head.closeSubpath();
        p.setBrush(kText);
        p.drawPath(head);
    });
    return i;
}

// The gizmo button: one of three icons for when the gizmo is shown -- the three axes of a move gizmo, with a pointer
// on them when it is shown by a click (the default: while the model is selected), dim and crossed out when it is
// never shown, and plain when it is always shown
const QIcon& modeIcon(const QString& mode)
{
    auto axes = [](QPainter& p, int alpha) {
        const QColor cols[3] = {QColor(0xdc, 0x32, 0x2f), QColor(0x74, 0xb8, 0x1f), QColor(0x26, 0x8b, 0xd2)};
        const QPointF c(7, 9);
        const QPointF ends[3] = {QPointF(14, 9), QPointF(7, 2), QPointF(2.5, 13.5)};
        for (int a = 0; a < 3; ++a)
        {
            QColor col = cols[a];
            col.setAlpha(alpha);
            p.setPen(QPen(col, 1.7, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(c, ends[a]);
        }
        QColor dot = kText;
        dot.setAlpha(alpha);
        p.setPen(Qt::NoPen);
        p.setBrush(dot);
        p.drawEllipse(c, 1.7, 1.7);
    };
    static QIcon always = makeIcon([=](QPainter& p) { axes(p, 255); });
    static QIcon click = makeIcon([=](QPainter& p) {
        axes(p, 255);
        // (the pointer of a click, at the lower right)
        QPolygonF arrow;
        arrow << QPointF(9.4, 8.6) << QPointF(9.4, 15.2) << QPointF(11.3, 13.4) << QPointF(12.7, 16.0)
              << QPointF(13.9, 15.4) << QPointF(12.6, 12.8) << QPointF(15.2, 12.6);
        p.setPen(QPen(QColor(0, 0, 0, 210), 0.9, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(QColor(255, 255, 255));
        p.drawPolygon(arrow);
    });
    static QIcon never = makeIcon([=](QPainter& p) {
        axes(p, 90);
        p.setPen(QPen(QColor(0xdc, 0x32, 0x2f), 1.7, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(2.5, 13.5), QPointF(13.5, 2.5));
    });
    return mode == "never" ? never : mode == "always" ? always : click;
}

// The lock button: a closed padlock in amber when the shape is locked (it cannot be dragged at all), an open one,
// dim, when it is not
const QIcon& lockIcon(bool locked)
{
    static QIcon closed = makeIcon([](QPainter& p) {
        const QColor amber(0xe0, 0xb0, 0x50);
        p.setPen(QPen(amber, 1.5, Qt::SolidLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(5, 2.2, 6, 7), 0, 180 * 16);
        p.drawLine(QPointF(5, 5.7), QPointF(5, 8));
        p.drawLine(QPointF(11, 5.7), QPointF(11, 8));
        QColor fill = amber;
        fill.setAlpha(150);
        p.setBrush(fill);
        p.setPen(QPen(amber, 1.2));
        p.drawRoundedRect(QRectF(3.2, 7.8, 9.6, 6.4), 1.5, 1.5);
    });
    static QIcon open = makeIcon([](QPainter& p) {
        QColor c = kDim;
        c.setAlpha(170);
        p.setPen(QPen(c, 1.4, Qt::SolidLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        // (the shackle lifted off on one side)
        p.drawArc(QRectF(5, 0.6, 6, 7), 0, 180 * 16);
        p.drawLine(QPointF(5, 4.1), QPointF(5, 6.2));
        c.setAlpha(60);
        p.setBrush(c);
        p.setPen(QPen(kDim, 1.1));
        p.drawRoundedRect(QRectF(3.2, 7.8, 9.6, 6.4), 1.5, 1.5);
    });
    return locked ? closed : open;
}

// Where a click on the gizmo button (or the E key) goes: click, never, always, and round again
QString nextModeName(const QString& mode)
{
    return mode == "click" ? "never" : mode == "never" ? "always" : "click";
}

QString nextMode(const QJsonObject& t)
{
    return nextModeName(t["mode"].toString("click"));
}

// What a shape's gizmo button says and does.  Dragging a shape's surfaces is always possible, whatever the gizmo does
void setModeButton(QTreeWidgetItem* row, const QJsonObject& target)
{
    const QString mode = target["mode"].toString("click");
    row->setIcon(COL_HANDLES, modeIcon(mode));
    QString tip;
    if (mode == "never") tip = "Gizmo: never shown";
    else if (mode == "always") tip = "Gizmo: always shown";
    else tip = "Gizmo: shown while the model is selected (click)";
    row->setToolTip(COL_HANDLES, tip + "  ·  its surfaces can always be dragged  ·  click: " + nextMode(target) +
                                 "  ·  key E");
}

// What a shape's lock button says and does
void setLockButton(QTreeWidgetItem* row, const QJsonObject& target)
{
    const bool locked = target.contains("locked");
    row->setIcon(COL_LOCK, lockIcon(locked));
    row->setToolTip(COL_LOCK, locked ? "Locked: it cannot be dragged  ·  click to unlock  ·  key R"
                                     : "Unlocked  ·  click to lock it (writes lock(x) under its definition)  ·  key R");
}

// The render cache button: a stack of disks -- dim when the cache is off, filled when a shape's mesh is
// kept, green when the mesh on screen was read from it, amber when the shape cannot be kept
const QIcon& cacheIcon(const QString& state)
{
    auto make = [](QColor line, QColor fill) {
        return makeIcon([=](QPainter& p) {
            p.setPen(QPen(line, 1.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(fill);
            p.drawRect(QRectF(3, 3, 10, 10));
            p.setBrush(Qt::NoBrush);
            p.drawLine(QPointF(3, 6.4), QPointF(13, 6.4));
            p.drawLine(QPointF(3, 9.6), QPointF(13, 9.6));
            p.setBrush(line);
            p.setPen(Qt::NoPen);
            p.drawEllipse(QPointF(10.6, 4.7), 0.7, 0.7);
            p.drawEllipse(QPointF(10.6, 8.0), 0.7, 0.7);
            p.drawEllipse(QPointF(10.6, 11.3), 0.7, 0.7);
        });
    };
    QColor off = kDim;
    off.setAlpha(120);
    QColor blue(0x26, 0x8b, 0xd2), green(0x74, 0xb8, 0x1f), amber(0xe0, 0xb0, 0x50);
    QColor blueFill = blue, greenFill = green, amberFill = amber;
    blueFill.setAlpha(60);
    greenFill.setAlpha(60);
    amberFill.setAlpha(50);
    static QIcon iOff = make(off, Qt::transparent);
    static QIcon iOn = make(blue, blueFill);
    static QIcon iRead = make(green, greenFill);
    static QIcon iNo = make(amber, amberFill);
    if (state == "on" || state == "kept") return iOn;
    if (state == "read") return iRead;
    if (state == "no") return iNo;
    return iOff;
}

const QIcon& resetIcon()
{
    static QIcon i = makeIcon([](QPainter& p) {         // an arrow turning back
        p.setPen(QPen(kText, 1.4));
        p.drawArc(QRectF(3, 3, 10, 10), 120 * 16, -290 * 16);
        QPainterPath head;
        head.moveTo(2.4, 2.8);
        head.lineTo(2.6, 7.2);
        head.lineTo(6.7, 5.8);
        head.closeSubpath();
        p.setBrush(kText);
        p.drawPath(head);
    });
    return i;
}

const QIcon& deleteIcon()
{
    static QIcon i = makeIcon([](QPainter& p) {         // a bin
        p.setPen(QPen(kText, 1.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawLine(QPointF(3, 4.5), QPointF(13, 4.5));
        p.drawLine(QPointF(6.5, 4.5), QPointF(6.5, 2.8));
        p.drawLine(QPointF(6.5, 2.8), QPointF(9.5, 2.8));
        p.drawLine(QPointF(9.5, 2.8), QPointF(9.5, 4.5));
        QPainterPath can;
        can.moveTo(4.3, 4.5);
        can.lineTo(5, 13.2);
        can.lineTo(11, 13.2);
        can.lineTo(11.7, 4.5);
        p.drawPath(can);
        p.drawLine(QPointF(6.9, 6.5), QPointF(7.1, 11));
        p.drawLine(QPointF(9.1, 6.5), QPointF(8.9, 11));
    });
    return i;
}

const QIcon& kindIcon(const QString& kind, bool failed)
{
    static QIcon importI = makeIcon([](QPainter& p) {       // box
        p.setPen(QPen(QColor(0x2a, 0xa1, 0x98), 1.3));
        p.drawRect(QRectF(3, 5, 8, 8));
        p.drawLine(QPointF(3, 5), QPointF(6, 2));
        p.drawLine(QPointF(11, 5), QPointF(14, 2));
        p.drawLine(QPointF(6, 2), QPointF(14, 2));
        p.drawLine(QPointF(14, 2), QPointF(14, 10));
        p.drawLine(QPointF(11, 13), QPointF(14, 10));
    });
    static QIcon shapeI = makeIcon([](QPainter& p) {        // solid
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0xcb, 0xa8, 0x8a));
        p.drawEllipse(QPointF(8, 8), 5, 5);
    });
    static QIcon displayI = makeIcon([](QPainter& p) {      // expression
        p.setPen(QPen(QColor(0xcb, 0xa8, 0x8a), 1.3));
        QPolygonF d({QPointF(8, 2.5), QPointF(13.5, 8), QPointF(8, 13.5), QPointF(2.5, 8)});
        p.drawPolygon(d);
    });
    static QIcon failedI = makeIcon([](QPainter& p) {       // cross
        p.setPen(QPen(QColor(0xdc, 0x32, 0x2f), 2));
        p.drawLine(QPointF(4, 4), QPointF(12, 12));
        p.drawLine(QPointF(12, 4), QPointF(4, 12));
    });
    static QIcon partI = makeIcon([](QPainter& p) {         // small cube
        p.setPen(QPen(QColor(0x2a, 0xa1, 0x98), 1.2));
        p.drawRect(QRectF(4, 5, 7, 7));
        p.drawLine(QPointF(4, 5), QPointF(6.5, 2.5));
        p.drawLine(QPointF(6.5, 2.5), QPointF(13.5, 2.5));
        p.drawLine(QPointF(11, 5), QPointF(13.5, 2.5));
        p.drawLine(QPointF(13.5, 2.5), QPointF(13.5, 9.5));
        p.drawLine(QPointF(11, 12), QPointF(13.5, 9.5));
    });
    static QIcon settingsI = makeIcon([](QPainter& p) {     // sliders
        p.setPen(QPen(kDim, 1.3));
        for (int i=0; i < 3; ++i)
        {
            const double y = 4 + i * 4;
            p.drawLine(QPointF(2.5, y), QPointF(13.5, y));
            p.setBrush(kDim);
            p.drawEllipse(QPointF(4 + i * 3.5, y), 1.6, 1.6);
        }
    });
    if (failed) return failedI;
    if (kind == "import") return importI;
    if (kind == "part") return partI;
    if (kind == "display") return displayI;
    if (kind == "settings") return settingsI;
    return shapeI;
}

QString sizeText(const QJsonArray& b)
{
    if (b.size() != 2) return QString();
    const auto lo = b[0].toArray(), hi = b[1].toArray();
    QStringList dims;
    for (int i=0; i < 3; ++i)
    {
        dims << QString::number(hi[i].toDouble() - lo[i].toDouble(), 'g', 3);
    }
    return dims.join(QString(" ") + QChar(0x00d7) + " ");
}

QString keyOf(const QJsonObject& it)
{
    const QString kind = it["kind"].toString();
    if (it.contains("var")) return kind + ":" + it["var"].toString();
    if (it.contains("list_var")) return kind + ":" + it["list_var"].toString();
    return kind + ":" + it["label"].toString();
}

// Index of the parenthesis closing the one at `open`, skipping over string
// literals (plain, raw and triple-quoted); -1 if there is none
int matchingParen(const QString& s, int open)
{
    int depth = 0;
    for (int i=open; i < s.size(); ++i)
    {
        const QChar c = s[i];
        if (c == '"' || c == '\'')
        {
            // Raw string if the prefix letters include r / R
            bool raw = false;
            for (int k=i - 1; k >= 0 && s[k].isLetter(); --k)
            {
                raw |= (s[k] == 'r' || s[k] == 'R');
            }
            const bool triple = s.mid(i, 3) == QString(3, c);
            const QString close = triple ? QString(3, c) : QString(c);
            int j = i + close.size();
            while (j < s.size())
            {
                if (!raw && s[j] == '\\') { j += 2; continue; }
                if (s.mid(j, close.size()) == close) break;
                ++j;
            }
            i = j + close.size() - 1;
            continue;
        }
        if (c == '#')
        {
            while (i < s.size() && s[i] != '\n') ++i;
            continue;
        }
        if (c == '(' || c == '[' || c == '{') ++depth;
        else if (c == ')' || c == ']' || c == '}')
        {
            if (--depth == 0) return i;
        }
    }
    return -1;
}

bool boxesOverlap(const QJsonArray& a, const QJsonArray& b)
{
    if (a.size() != 2 || b.size() != 2) return true;
    for (int i=0; i < 3; ++i)
    {
        if (a[1].toArray()[i].toDouble() < b[0].toArray()[i].toDouble() ||
            b[1].toArray()[i].toDouble() < a[0].toArray()[i].toDouble())
        {
            return false;
        }
    }
    return true;
}

QString boxLiteral(const QJsonArray& b)
{
    QStringList pts;
    for (int k=0; k < 2; ++k)
    {
        QStringList c;
        for (auto v : b[k].toArray()) c << QString::number(v.toDouble(), 'g', 4);
        pts << "(" + c.join(", ") + ")";
    }
    return "(" + pts.join(", ") + ")";
}

QString indentOf(const QString& line)
{
    int i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    return line.left(i);
}

}   // anonymous namespace

////////////////////////////////////////////////////////////////////////////////

ScenePanel::ScenePanel(QWidget* parent)
    : QFrame(parent), m_tree(new QTreeWidget), m_header(new QToolButton),
      m_note(new QLabel)
{
    setObjectName("ScenePanel");
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(
        "#ScenePanel { background: rgba(22, 76, 94, 222);"
        "  border: 1px solid rgba(147, 161, 161, 90); border-radius: 6px; }"
        "QTreeWidget { background: transparent; color: #eee8d5; border: none;"
        "  outline: none; font-size: 8.5pt; }"
        "QTreeWidget::item { padding: 1px 0px; }"
        "QTreeWidget::item:hover { background: rgba(147, 161, 161, 40); }"
        "QTreeWidget::item:selected { background: rgba(38, 139, 210, 150); color: white; }"
        "QToolButton#SceneHeader { color: #eee8d5; border: none; font-weight: bold;"
        "  font-size: 9pt; padding: 3px 6px; text-align: left; }"
        "QLabel { color: #93a1a1; font-size: 8pt; padding: 0px 6px 4px 6px; }");

    m_header->setObjectName("SceneHeader");
    m_header->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_header->setText(QString(QChar(0x25be)) + "  Model tree");
    m_header->setCursor(Qt::PointingHandCursor);
    m_header->setToolTip("Show / hide the model tree");
    connect(m_header, &QToolButton::clicked, this, [this]{ setCollapsed(!m_collapsed); });

    m_tree->setColumnCount(8);
    m_tree->setHeaderHidden(true);
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(COL_NAME, QHeaderView::Stretch);
    m_tree->header()->setMinimumSectionSize(16);
    m_tree->header()->setDefaultSectionSize(20);
    for (int c : {int(COL_EYE), int(COL_HANDLES), int(COL_LOCK), int(COL_CACHE), int(COL_ACTION), int(COL_RESET), int(COL_DELETE)})
    {
        m_tree->header()->setSectionResizeMode(c, QHeaderView::Fixed);
        m_tree->header()->resizeSection(c, 20);
    }
    m_tree->setIndentation(12);
    m_tree->setIconSize(QSize(16, 16));
    m_tree->setMouseTracking(true);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setFocusPolicy(Qt::NoFocus);
    m_tree->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // Several rows can be selected, as in a file list: Ctrl+click adds one or takes it out, Shift+click selects
    // the rows from the one clicked before to this one (the modifiers of the click are noted by the event filter)
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tree->viewport()->installEventFilter(this);
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, &ScenePanel::onSelectionChanged);
    m_prepareTimer.setSingleShot(true);
    m_prepareTimer.setInterval(60);
    connect(&m_prepareTimer, &QTimer::timeout, this, &ScenePanel::prepareSelection);
    connect(m_tree, &QTreeWidget::itemClicked, this, &ScenePanel::onItemClicked);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, &ScenePanel::onItemDoubleClicked);
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, &ScenePanel::onContextMenu);
    connect(m_tree, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem* i){
        m_expandState[i->data(COL_NAME, ROLE_KEY).toString()] = true;
        setCollapsed(m_collapsed);    // resize to the new row count
    });
    connect(m_tree, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem* i){
        m_expandState[i->data(COL_NAME, ROLE_KEY).toString()] = false;
        setCollapsed(m_collapsed);
    });

    m_note->setWordWrap(true);
    m_note->hide();

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 4);
    layout->setSpacing(0);
    layout->addWidget(m_header);
    layout->addWidget(m_tree);
    layout->addWidget(m_note);

    setFixedWidth(320);
    m_tree->setTextElideMode(Qt::ElideMiddle);
    if (parent)
    {
        parent->installEventFilter(this);
    }
    move(8, 8);
    resize(320, 60);
}

bool ScenePanel::eventFilter(QObject* obj, QEvent* e)
{
    if (obj == parentWidget() && e->type() == QEvent::Resize)
    {
        setCollapsed(m_collapsed);
    }
    // (the modifiers of a click on the tree: itemClicked, which comes after the release, does not carry them)
    if (obj == m_tree->viewport() && (e->type() == QEvent::MouseButtonPress || e->type() == QEvent::MouseButtonRelease))
    {
        m_clickMods = static_cast<QMouseEvent*>(e)->modifiers();
    }
    return QFrame::eventFilter(obj, e);
}

void ScenePanel::setCollapsed(bool c)
{
    m_collapsed = c;
    m_tree->setVisible(!c);
    m_note->setVisible(!c && !m_note->text().isEmpty());
    m_header->setText(QString(QChar(c ? 0x25b8 : 0x25be)) + "  Model tree");
    adjustSize();
    if (parentWidget())
    {
        const int maxH = parentWidget()->height() * 0.62;
        int h = m_header->sizeHint().height() + 8;
        if (!c)
        {
            // Height from the visible rows
            int rows = 0;
            std::function<void(QTreeWidgetItem*)> count = [&](QTreeWidgetItem* i) {
                rows++;
                if (i->isExpanded())
                    for (int k=0; k < i->childCount(); ++k) count(i->child(k));
            };
            for (int k=0; k < m_tree->topLevelItemCount(); ++k) count(m_tree->topLevelItem(k));
            const int rowH = std::max(18, m_tree->sizeHintForRow(0));
            h += std::max(1, rows) * rowH + 6;
            if (m_note->isVisible()) h += m_note->sizeHint().height();
        }
        resize(width(), std::min(h, std::max(80, maxH)));
    }
}

void ScenePanel::setScene(const QString& json)
{
    const auto doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isObject())
    {
        return;
    }
    m_scene = doc.object();
    ++m_generation;
    rebuild();
    // Expressions that were named for a multi-selection: the selection is the same models again, under their new names
    if (!m_reselect.isEmpty())
    {
        QList<QTreeWidgetItem*> rows;
        bool all = true;
        for (const QString& key : m_reselect)
        {
            QTreeWidgetItem* found = nullptr;
            for (QTreeWidgetItemIterator i(m_tree); *i; ++i)
            {
                if ((*i)->data(COL_NAME, ROLE_KEY).toString() == key &&
                    (*i)->data(COL_NAME, ROLE_TYPE).toString() == "item") { found = *i; break; }
            }
            if (found) rows << found; else all = false;
        }
        if (all || ++m_reselectTries > 8)
        {
            m_reselect.clear();
            m_tree->clearSelection();
            for (auto r : rows) m_tree->setCurrentItem(r, 0, QItemSelectionModel::Select | QItemSelectionModel::Rows);
        }
    }
    // (a model locked while it was in a selection of several leaves it; the guard of the strip keeps the selection's
    // bookkeeping from running meanwhile, so it runs now)
    m_silentStrip = true;
    if (stripLocked(false)) onSelectionChanged();
    m_silentStrip = false;
    // (the shapes of this run are in the viewport already, and a changed shape is a new one: the selected models are
    // lit up again, from the lines they are on now)
    emit(highlightLines(selectedLines()));
    updateMultiNote();
    // The run that answers an edit of prepareSelection is done: the scene is current again, and a selection that
    // grew meanwhile gets its numbers too
    m_editPending = false;
    if (m_prepareAgain)
    {
        m_prepareAgain = false;
        m_prepareTimer.start();
    }
    else if (m_afterRun)
    {
        // (a menu entry chosen while the script was being edited: now the scene is current)
        const auto later = m_afterRun;
        m_afterRun = nullptr;
        QTimer::singleShot(0, this, later);
    }

    // A displayed expression was just given a name: its mode goes on once the variable exists
    if (!m_pending.var.isEmpty())
    {
        const QJsonObject target = itemForVar(m_pending.var);
        if (!target.isEmpty())
        {
            const QString mode = m_pending.mode;
            m_pending = Pending();
            m_selectedKey = keyOf(target);          // (the new variable is the selected model now)
            m_selectOrder = QStringList{m_selectedKey};
            QTimer::singleShot(0, this, [=]{
                if (mode == "cache") toggleCache(target);
                else if (mode == "lock") toggleLock(target);
                else setMode(target, mode);
            });
        }
        else if (++m_pending.tries > 8)
        {
            m_pending = Pending();
        }
    }

    // A model made from a viewport menu is selected once the script that makes it has run
    if (!m_selectNew.isEmpty())
    {
        if (selectVar(m_selectNew)) m_selectNew.clear();
        else if (++m_selectNewTries > 8) m_selectNew.clear();
    }

}

void ScenePanel::setPartialScene(const QString& json)
{
    const auto doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isObject())
    {
        return;
    }
    // (only the rows: the run is not over, so nothing that waits for the finished scene is answered, and the scene
    // does not count as a new run of the script)
    m_scene = doc.object();
    rebuild();
    updateMultiNote();
}

void ScenePanel::clearScene()
{
    m_scene = QJsonObject();
    ++m_generation;
    m_selectedKey.clear();
    m_selectOrder.clear();
    m_reselect.clear();
    m_reselectTries = 0;
    m_pending = Pending();
    m_selectNew.clear();
    m_selectNewTries = 0;
    m_afterRun = nullptr;
    m_editPending = false;
    m_prepareAgain = false;
    m_prepareTimer.stop();
    m_exposeTried.clear();
    m_isolated = false;
    m_isolateRestore.clear();
    m_isolatedKey.clear();
    m_isolatedName.clear();
    m_expandState.clear();
    rebuild();
    emit(highlightLines(QList<int>()));
    updateMultiNote();
}

void ScenePanel::rebuild()
{
    const int scroll = m_tree->verticalScrollBar() ? m_tree->verticalScrollBar()->value() : 0;
    // (clearing and filling the tree changes its selection; the order the rows were selected in is kept meanwhile)
    m_rebuilding = true;
    m_tree->clear();
    QTreeWidgetItem* toSelect = nullptr;
    QList<QTreeWidgetItem*> restore;

    auto makeRow = [&](QTreeWidgetItem* parent, const QString& text, const QIcon& icon,
                       const QString& type, const QJsonObject& it, const QString& key) {
        auto row = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tree);
        row->setText(COL_NAME, text);
        row->setIcon(COL_NAME, icon);
        row->setData(COL_NAME, ROLE_ITEM, it);
        row->setData(COL_NAME, ROLE_TYPE, type);
        row->setData(COL_NAME, ROLE_KEY, key);
        if (key == m_selectedKey) toSelect = row;
        if (m_selectOrder.contains(key)) restore << row;
        return row;
    };

    const auto items = m_scene["items"].toArray();

    // Render settings
    const auto settings = m_scene["settings"].toObject();
    if (!settings.isEmpty())
    {
        auto row = makeRow(nullptr, "Render settings", kindIcon("settings", false),
                           "settings", QJsonObject(), "settings");
        row->setForeground(COL_NAME, kDim);
        auto child = [&](const QString& name, const QString& fn, const QString& value) {
            const auto s = settings[fn].toObject();
            auto c = makeRow(row, name + (value.isEmpty() ? "" : ":  " + value),
                             QIcon(), "setting", s, "setting:" + fn);
            c->setData(COL_NAME, ROLE_LINE, s.contains("line") ? s["line"].toInt() - 1 : -1);
            c->setToolTip(COL_NAME, s.contains("text") ? s["text"].toString()
                                                      : "Not set in the script (default)");
            if (!s.contains("line")) c->setForeground(COL_NAME, kDim);
            else
            {
                c->setIcon(COL_DELETE, deleteIcon());
                c->setToolTip(COL_DELETE, "Delete this line (back to the default)");
            }
        };
        auto fmtBounds = [](const QJsonValue& v) {
            const auto b = v.toArray();
            if (b.size() != 2) return QString();
            QStringList out;
            for (int i=0; i < 2; ++i)
            {
                QStringList xyz;
                for (auto c : b[i].toArray()) xyz << QString::number(c.toDouble(), 'g', 4);
                out << "(" + xyz.join(", ") + ")";
            }
            return out.join(" " + QString(QChar(0x2192)) + " ");
        };
        child("Bounds", "set_bounds", fmtBounds(settings["bounds"].toObject()["value"]));
        child("Resolution", "set_resolution",
              settings["resolution"].toObject().contains("value")
                  ? QString::number(settings["resolution"].toObject()["value"].toDouble()) : "");
        child("Quality", "set_quality",
              settings["quality"].toObject().contains("value")
                  ? QString::number(settings["quality"].toObject()["value"].toDouble()) : "");
        row->setExpanded(m_expandState.value("settings", false));
    }
    const auto region = settings["bounds"].toObject()["value"].toArray();

    // Which imports list their parts as rows (the part variables are shown
    // there rather than again at the top level)
    QSet<int> partLists;
    for (const auto v : items)
    {
        const auto it = v.toObject();
        if (it["kind"].toString() == "import" && it.contains("list_var"))
        {
            partLists << it["line"].toInt();
        }
    }

    for (const auto v : items)
    {
        const auto it = v.toObject();
        const QString kind = it["kind"].toString();
        const bool failed = it["failed"].toBool() || kind == "failed";
        const QString key = keyOf(it);
        if (it.contains("part_of") && partLists.contains(it["part_of"].toInt()))
        {
            continue;
        }

        QString text;
        if (kind == "import")
        {
            text = it["label"].toString();
            if (it.contains("var")) text += "  " + QString(QChar(0x2192)) + " " + it["var"].toString();
            else if (it.contains("list_var")) text += "  " + QString(QChar(0x2192)) + " " + it["list_var"].toString();
        }
        else if (kind == "display")
        {
            text = it["label"].toString();
        }
        else
        {
            text = it["var"].toString();
        }

        auto row = makeRow(nullptr, text, kindIcon(kind, failed), "item", it, key);
        QString tip = it["text"].toString();
        if (it.contains("path")) tip = it["path"].toString() + "\n" + tip;
        if (it.contains("unit_mm") && it["unit_mm"].toDouble() != 1.0 &&
            it["units"].toString() != "file")
        {
            tip += QString("\nUnits converted (%1 mm per unit)").arg(it["unit_mm"].toDouble());
        }
        if (it.contains("bounds")) tip += "\nSize: " + sizeText(it["bounds"].toArray());
        if (it.contains("error")) tip += "\n" + it["error"].toString();
        const QString note = it["note"].toString();
        if (!note.isEmpty()) tip += "\n" + note;
        row->setToolTip(COL_NAME, tip.trimmed());
        if (failed) row->setForeground(COL_NAME, QColor(0xdc, 0x6e, 0x5e));
        else if (note.contains("NOT watertight") || note.contains(" across:"))
        {
            // Imported, but worth a look (open mesh / suspicious size)
            row->setForeground(COL_NAME, QColor(0xe0, 0xb0, 0x50));
        }
        if (kind == "display" && !it["visible"].toBool()) row->setForeground(COL_NAME, kDim);
        if (!failed && it["visible"].toBool() && it.contains("bounds") && region.size() == 2 &&
            !boxesOverlap(it["bounds"].toArray(), region))
        {
            // Displayed, but nothing of it is inside the render region
            row->setForeground(COL_NAME, kDim);
            row->setToolTip(COL_NAME, row->toolTip(COL_NAME) + "\nOutside the render region");
        }

        // Visibility eye
        const bool hasEye = !failed && (it.contains("var") || kind == "display");
        if (hasEye)
        {
            const bool visible = it["visible"].toBool();
            row->setIcon(COL_EYE, eyeIcon(visible));
            row->setToolTip(COL_EYE, visible ? "Hide" : "Show");
        }
        else if (kind == "import" && it.contains("list_var") && !failed)
        {
            // A parts list: the eye shows / hides every part
            int ok = 0, shown = 0;
            for (const auto pv : it["parts"].toArray())
            {
                const auto p = pv.toObject();
                if (!p["ok"].toBool()) continue;
                ++ok;
                if (p.contains("var") && itemForVar(p["var"].toString())["visible"].toBool()) ++shown;
            }
            if (ok)
            {
                row->setIcon(COL_EYE, eyeIcon(shown > 0));
                row->setData(COL_EYE, Qt::UserRole, shown == ok);
                row->setToolTip(COL_EYE, shown == ok ? "Hide all parts" : "Show all parts");
            }
        }

        // Handles: how it is edited by dragging in the view (a button cycling the three ways)
        if (!failed && it.contains("var") && kind != "display" && !it["reassigned"].toBool() &&
            !it["no_handles"].toBool())
        {
            setModeButton(row, it);
            setLockButton(row, it);
        }
        else if (!failed && kind == "display" && it["can_name"].toBool() && it["visible"].toBool())
        {
            // (a displayed expression has no name: the button gives it one first)
            setModeButton(row, it);
            setLockButton(row, it);
        }

        // Render cache: keeps the shape's mesh (a button writing and deleting `x = render_cache(x)`)
        if (!failed && ((it.contains("var") && kind != "display" && !it["reassigned"].toBool()) ||
                        (kind == "display" && it["can_name"].toBool() && it["visible"].toBool())))
        {
            setCacheButton(row, it);
        }

        // Reset: an import back to the file (its cache deleted, its handle edits removed)
        if (kind == "import" && !failed && it.contains("path") && it["func"].toString().startsWith("import_step"))
        {
            row->setIcon(COL_RESET, resetIcon());
            row->setToolTip(COL_RESET, "Reset: clear the cache and handle edits");
        }

        // Action: reimport for imports
        if (kind == "import")
        {
            row->setIcon(COL_ACTION, reimportIcon());
            row->setToolTip(COL_ACTION, "Reimport the file");
        }

        // Delete: its statements go from the script
        row->setIcon(COL_DELETE, deleteIcon());
        row->setToolTip(COL_DELETE, "Delete from the script");

        // Children: parts of an import, inputs of a shape
        const auto parts = it["parts"].toArray();
        if (kind == "import" && (parts.size() > 1 || it.contains("list_var")))
        {
            const int current = it.contains("index") ? it["index"].toInt() : -1;
            for (const auto pv : parts)
            {
                const auto p = pv.toObject();
                const int k = p["index"].toInt();
                const bool ok = p["ok"].toBool();
                // Its occurrence in the file's assembly, e.g. "M3x10-Screw:2"
                // from "Drive:1/Motor:1/M3x10-Screw:2"
                const QString path = p["name"].toString();
                const QString shortName = path.section('/', -1);
                QString t = p.contains("var") ? p["var"].toString() : QString("part %1").arg(k);
                if (!shortName.isEmpty()) t += QString("  ·  ") + shortName;
                else if (p.contains("var")) t += QString("   (part %1)").arg(k);
                if (k == current) t += "  *";
                auto c = makeRow(row, t, kindIcon("part", !ok), "part", it,
                                 key + "#" + QString::number(k));
                c->setData(COL_NAME, ROLE_PART, k);
                QString ptip = "Size: " + sizeText(p["bounds"].toArray());
                ptip = QString("Part %1").arg(k) + (path.isEmpty() ? "" : ": " + path) + "\n" + ptip;
                const QJsonObject bound = p.contains("var") ? itemForVar(p["var"].toString())
                                                            : QJsonObject();
                // Handles work on a part that is in the script (as a variable)
                const QJsonObject target = handlesTarget(it, k);
                if (ok && !target.isEmpty())
                {
                    setModeButton(c, target);
                    setLockButton(c, target);
                    setCacheButton(c, target);
                }
                // Reimport: back to what the file says (its handle edits gone), the file read again
                if (ok && it["func"].toString().startsWith("import_step"))
                {
                    c->setIcon(COL_RESET, reimportIcon());
                    c->setToolTip(COL_RESET, "Reimport this part");
                }
                // Delete: the part's variable (a part that is not in the script has nothing to delete)
                if (!bound.isEmpty())
                {
                    c->setIcon(COL_DELETE, deleteIcon());
                    c->setToolTip(COL_DELETE, "Delete this part from the script");
                }
                const bool partVisible = !bound.isEmpty() ? bound["visible"].toBool()
                                         : (k == current && it["visible"].toBool());
                if (ok && !partVisible) c->setForeground(COL_NAME, kDim);
                if (ok && partVisible && region.size() == 2 &&
                    !boxesOverlap(p["bounds"].toArray(), region))
                {
                    c->setForeground(COL_NAME, kDim);
                    ptip += "\nOutside the render region";
                }
                if (!ok)
                {
                    c->setForeground(COL_NAME, QColor(0xdc, 0x6e, 0x5e));
                    ptip = p["error"].toString();
                }
                else if (it.contains("index"))
                {
                    const bool inUse = (k == current);
                    ptip += inUse ? "\nIn use" : "\nDouble-click: use this part";
                    c->setIcon(COL_EYE, eyeIcon(inUse && it["visible"].toBool()));
                }
                else if (!p.contains("var"))
                {
                    ptip += "\nDouble-click: add to the script";
                    c->setIcon(COL_EYE, eyeIcon(false));
                }
                else
                {
                    // Mirror the bound variable's visibility
                    for (const auto ov : items)
                    {
                        const auto o = ov.toObject();
                        if (o["var"].toString() == p["var"].toString() && o["kind"].toString() != "import")
                        {
                            c->setIcon(COL_EYE, eyeIcon(o["visible"].toBool()));
                        }
                    }
                }
                c->setToolTip(COL_NAME, ptip);
            }
            bool anyBound = false;
            for (const auto pv : parts) anyBound |= pv.toObject().contains("var");
            row->setExpanded(m_expandState.value(key, parts.size() <= 8 || anyBound));
        }
        for (const auto d : it["deps"].toArray())
        {
            auto c = makeRow(row, "uses " + d.toString(), QIcon(), "dep", it,
                             key + ">" + d.toString());
            c->setForeground(COL_NAME, kDim);
            c->setData(COL_NAME, ROLE_PART, d.toString());
        }
        if (!it["deps"].toArray().isEmpty())
        {
            row->setExpanded(m_expandState.value(key, false));
        }
    }

    // Footer notes: analysis problems, truncated huge scripts
    QString note;
    if (m_scene.contains("error"))
    {
        note = "Model tree unavailable: " + m_scene["error"].toString();
    }
    else if (m_scene["truncated"].toInt() > 0)
    {
        note = QString("%1 more intermediate shapes not listed").arg(m_scene["truncated"].toInt());
    }
    else if (items.isEmpty())
    {
        note = "No shapes yet";
    }
    if (m_isolated && note.isEmpty())
    {
        note = "Isolated: " + m_isolatedName + ". Press I to show everything again.";
    }
    m_note->setText(note);

    // The selection comes back: every row that was selected, the current one as it was
    if (restore.isEmpty() && toSelect) restore << toSelect;
    for (auto r : restore) m_tree->setCurrentItem(r, 0, QItemSelectionModel::Select | QItemSelectionModel::Rows);
    if (toSelect) m_tree->setCurrentItem(toSelect, 0, QItemSelectionModel::NoUpdate);
    QStringList order;
    for (const QString& k : m_selectOrder)
    {
        for (auto r : restore) if (r->data(COL_NAME, ROLE_KEY).toString() == k) { order << k; break; }
    }
    for (auto r : restore)
    {
        const QString k = r->data(COL_NAME, ROLE_KEY).toString();
        if (!order.contains(k)) order << k;
    }
    m_selectOrder = order;
    m_rebuilding = false;
    if (m_tree->verticalScrollBar())
    {
        m_tree->verticalScrollBar()->setValue(scroll);
    }
    setCollapsed(m_collapsed);
}

////////////////////////////////////////////////////////////////////////////////

QList<int> ScenePanel::linesOf(const QJsonObject& it) const
{
    QList<int> out;
    if (it.contains("display_line"))
    {
        out << it["display_line"].toInt() - 1;
    }
    // A parts list import: the display lines of its part variables
    if (it["kind"].toString() == "import" && it.contains("list_var"))
    {
        const int line = it["line"].toInt();
        for (const auto v : m_scene["items"].toArray())
        {
            const auto o = v.toObject();
            if (o["part_of"].toInt() == line && o.contains("display_line"))
            {
                out << o["display_line"].toInt() - 1;
            }
        }
    }
    return out;
}

QString ScenePanel::lineText(int line0) const
{
    const QStringList lines = m_source ? m_source().split('\n') : QStringList();
    return (line0 >= 0 && line0 < lines.size()) ? lines[line0] : QString();
}

QString ScenePanel::uniqueName(const QString& base) const
{
    const QString src = m_source ? m_source() : QString();
    QString name = base;
    int n = 2;
    while (QRegularExpression("\\b" + QRegularExpression::escape(name) + "\\b").match(src).hasMatch())
    {
        name = base + "_" + QString::number(n++);
    }
    return name;
}

void ScenePanel::addImportIfMissing(QList<TextEdit>& edits, const QString& name,
                                    const QString& module) const
{
    const QString src = m_source ? m_source() : QString();
    const QRegularExpression has(
        "^\\s*from\\s+fieldes(\\.stdlib(\\." + module + ")?)?\\s+import\\s+(\\*|.*\\b" + name + "\\b)",
        QRegularExpression::MultilineOption);
    if (has.match(src).hasMatch())
    {
        return;
    }
    // After the last top-level import line (or at the top)
    const QStringList lines = src.split('\n');
    int after = -1;
    for (int i=0; i < lines.size(); ++i)
    {
        if (lines[i].startsWith("import ") || lines[i].startsWith("from "))
        {
            after = i;
        }
    }
    const QString stmt = "from fieldes.stdlib." + module + " import " + name;
    if (after >= 0)
    {
        edits << TextEdit{after, int(lines[after].size()), after, int(lines[after].size()),
                          "\n" + stmt};
    }
    else
    {
        edits << TextEdit{0, 0, 0, 0, stmt + "\n"};
    }
}

QJsonObject ScenePanel::itemForVar(const QString& var) const
{
    for (const auto v : m_scene["items"].toArray())
    {
        const auto o = v.toObject();
        if (o["var"].toString() == var && o["kind"].toString() != "import") return o;
    }
    return QJsonObject();
}

bool ScenePanel::visibilityEdit(const QJsonObject& it, bool show, QList<TextEdit>& edits) const
{
    static const QRegularExpression hiddenRe(R"(^(\s*)(?:#\s*hidden:\s?)+)");
    const bool visible = it["visible"].toBool();
    if (visible == show) return false;
    if (it.contains("var"))
    {
        if (!show && it.contains("display_line"))
        {
            const int L = it["display_line"].toInt() - 1;
            const QString t = lineText(L);
            if (!hiddenRe.match(t).hasMatch())
                edits << TextEdit{L, 0, L, int(t.size()), indentOf(t) + "# hidden: " + t.trimmed()};
        }
        else if (show && it.contains("hidden_line"))
        {
            const int L = it["hidden_line"].toInt() - 1;
            const QString t = lineText(L);
            QString shown = t;
            shown.replace(hiddenRe, "\\1");
            edits << TextEdit{L, 0, L, int(t.size()), shown};
        }
        else if (show)
        {
            const int L = it["end_line"].toInt() - 1;
            const QString t = lineText(L);
            edits << TextEdit{L, int(t.size()), L, int(t.size()), "\n" + indentOf(t) + it["var"].toString()};
        }
        return true;
    }
    if (it["kind"].toString() == "display")
    {
        const int a = it["line"].toInt() - 1, b = it["end_line"].toInt() - 1;
        for (int L = a; L <= b; ++L)
        {
            const QString t = lineText(L);
            QString n = t;
            if (!show) { if (!hiddenRe.match(t).hasMatch()) n = "# hidden: " + t; }
            else n.replace(hiddenRe, "\\1");
            edits << TextEdit{L, 0, L, int(t.size()), n};
        }
        return true;
    }
    return false;
}

void ScenePanel::setAllParts(const QJsonObject& imp, bool show)
{
    QList<TextEdit> edits;
    QString added;
    const QString list = imp["list_var"].toString();
    QSet<QString> taken;
    for (const auto pv : imp["parts"].toArray())
    {
        const auto p = pv.toObject();
        if (!p["ok"].toBool()) continue;
        if (p.contains("var"))
        {
            const auto o = itemForVar(p["var"].toString());
            if (!o.isEmpty()) visibilityEdit(o, show, edits);
        }
        else if (show)
        {
            QString name = uniqueName(list + "_" + QString::number(p["index"].toInt()));
            for (int n = 2; taken.contains(name); ++n) name += "_" + QString::number(n);
            taken << name;
            added += QString("\n%1 = %2[%3][0]\n%1").arg(name).arg(list).arg(p["index"].toInt());
        }
    }
    if (!added.isEmpty())
    {
        const int L = imp["end_line"].toInt() - 1;
        const QString t = lineText(L);
        edits << TextEdit{L, int(t.size()), L, int(t.size()), added};
    }
    if (show && !edits.isEmpty())
    {
        // ...and render the whole assembly, with whatever else is shown
        int end = imp["end_line"].toInt();
        QStringList exprs = visibleRoiExprs(imp["line"].toInt(), &end);
        exprs.removeAll(list);
        exprs.prepend(list);
        QJsonObject all;
        all["roi_expr"] = exprs.join(", ");
        all["end_line"] = end;
        roiEdits(all, edits);
    }
    if (!edits.isEmpty())
    {
        emit(editScript(edits, show ? "Show all parts" : "Hide all parts"));
        if (show && imp.contains("bounds"))
        {
            focusOn(imp);   // frame the whole assembly
        }
    }
}

void ScenePanel::showOtherPart(const QJsonObject& imp, int part)
{
    // "x = import_step_parts(P)[i]..." can show only part i: split it into
    // "name = import_step_parts(P)" and "x = name[i]...", then add the part
    const auto c = imp["call"].toArray();
    if (c.size() != 4) return;
    const QString callText = spanText(c);
    QString base = imp["label"].toString().section('.', 0, 0).toLower();
    base.replace(QRegularExpression("[^a-z0-9_]+"), "_");
    base.remove(QRegularExpression("^_+|_+$"));
    if (base.isEmpty() || base[0].isDigit()) base = "parts_" + base;
    const QString name = uniqueName(base);
    const QString shown = uniqueName(name + "_" + QString::number(part));

    const int a = imp["line"].toInt() - 1, b = imp["end_line"].toInt() - 1;
    const QString first = lineText(a), last = lineText(b);
    const QString indent = indentOf(first);
    QList<TextEdit> edits;
    edits << TextEdit{b, int(last.size()), b, int(last.size()),
                      QString("\n%1%2 = %3[%4][0]\n%1%2").arg(indent, shown, name).arg(part)};
    edits << TextEdit{c[0].toInt() - 1, c[1].toInt(), c[2].toInt() - 1, c[3].toInt(), name};
    edits << TextEdit{a, 0, a, 0, indent + name + " = " + callText + "\n"};
    emit(editScript(edits, "Show part " + QString::number(part)));
}

void ScenePanel::toggleVisible(const QJsonObject& it)
{
    QList<TextEdit> edits;
    static const QRegularExpression hiddenRe(R"(^(\s*)(?:#\s*hidden:\s?)+)");
    if (it.contains("var"))
    {
        const QString var = it["var"].toString();
        if (it.contains("display_line"))
        {
            const int L = it["display_line"].toInt() - 1;
            const QString t = lineText(L);
            if (!hiddenRe.match(t).hasMatch())
                edits << TextEdit{L, 0, L, int(t.size()), indentOf(t) + "# hidden: " + t.trimmed()};
            emit(editScript(edits, "Hide " + var));
        }
        else if (it.contains("hidden_line"))
        {
            const int L = it["hidden_line"].toInt() - 1;
            const QString t = lineText(L);
            QString shown = t;
            shown.replace(hiddenRe, "\\1");
            edits << TextEdit{L, 0, L, int(t.size()), shown};
            emit(editScript(edits, "Show " + var));
        }
        else
        {
            const int L = it["end_line"].toInt() - 1;
            const QString t = lineText(L);
            edits << TextEdit{L, int(t.size()), L, int(t.size()), "\n" + var};
            emit(editScript(edits, "Show " + var));
        }
        return;
    }
    if (it["kind"].toString() == "display")
    {
        const int a = it["line"].toInt() - 1, b = it["end_line"].toInt() - 1;
        const bool visible = it["visible"].toBool();
        for (int L = a; L <= b; ++L)
        {
            const QString t = lineText(L);
            QString n = t;
            if (visible) { if (!hiddenRe.match(t).hasMatch()) n = "# hidden: " + t; }
            else n.replace(hiddenRe, "\\1");
            edits << TextEdit{L, 0, L, int(t.size()), n};
        }
        emit(editScript(edits, visible ? "Hide expression" : "Show expression"));
    }
}

QJsonObject ScenePanel::handlesTarget(const QJsonObject& imp, int part) const
{
    const auto parts = imp["parts"].toArray();
    if (part < 0 || part >= parts.size()) return QJsonObject();
    const auto p = parts[part].toObject();
    if (p.contains("var")) return itemForVar(p["var"].toString());
    // "x = import_step_parts(P)[k]": the statement's own variable is the part
    if (imp.contains("var") && imp.contains("index") && imp["index"].toInt() == part &&
        !imp["failed"].toBool())
    {
        return imp;
    }
    return QJsonObject();
}

namespace {

// The gizmo's line under a shape's definition (the numbers its arrows, rings and knobs drag; `mode` is when the gizmo is
// shown: 'click' (the default, not written), 'never' or 'always')
QString gizmoLine(const QString& var, const QString& mode = "click")
{
    return var + " = handles(" + var + ", move=(var(0), var(0), var(0)), rotate=(var(0), var(0), var(0)), "
                 "scale=(var(1), var(1), var(1))" + (mode == "click" ? QString() : ", mode='" + mode + "'") + ")";
}

// A model with no numbers to pull its surfaces by gets them when it is selected, if they are no more than this many
// (a bigger one -- an imported part -- has it in its menu: they are pages of script)
const int kAutoExposeNumbers = 120;

}   // anonymous namespace

QJsonObject ScenePanel::selectedTarget() const
{
    auto editable = [&](QTreeWidgetItem* row) -> QJsonObject {
        const QString type = row->data(COL_NAME, ROLE_TYPE).toString();
        const auto it = row->data(COL_NAME, ROLE_ITEM).toJsonObject();
        if (type == "part") return handlesTarget(it, row->data(COL_NAME, ROLE_PART).toInt());
        if (type != "item" || it["failed"].toBool() || it["reassigned"].toBool()) return QJsonObject();
        const QString kind = it["kind"].toString();
        if (kind == "display") return (it["can_name"].toBool() && it["visible"].toBool()) ? it : QJsonObject();
        return (it.contains("var") && kind != "failed") ? it : QJsonObject();
    };
    if (auto row = m_tree->currentItem())
    {
        const QJsonObject t = editable(row);
        if (!t.isEmpty()) return t;
    }
    // (nothing selected: the first model that can be edited)
    for (QTreeWidgetItemIterator i(m_tree); *i; ++i)
    {
        const QJsonObject t = editable(*i);
        if (!t.isEmpty() && (*i)->data(COL_NAME, ROLE_TYPE).toString() == "item") return t;
    }
    return QJsonObject();
}

QList<QJsonObject> ScenePanel::editTargets() const
{
    // The selected models that can be edited by dragging, in the order they were selected; when none is, what
    // selectedTarget() falls back on
    auto editable = [](const QJsonObject& it) {
        if (it["failed"].toBool() || it["reassigned"].toBool() || it["no_handles"].toBool()) return false;
        const QString kind = it["kind"].toString();
        if (kind == "display") return it["can_name"].toBool() && it["visible"].toBool();
        return it.contains("var") && kind != "failed";
    };
    QList<QJsonObject> out;
    QSet<QString> seen;
    for (const Model& m : selectedModels())
    {
        if (!editable(m.item) || seen.contains(keyOf(m.item))) continue;
        seen << keyOf(m.item);
        out << m.item;
    }
    return out;
}

void ScenePanel::clearSelection()
{
    // (nothing is selected: the keys that work on the selection have nothing to work on until a model is clicked)
    m_tree->clearSelection();
    m_tree->setCurrentItem(nullptr);
    m_selectedKey.clear();
    m_selectOrder.clear();
    emit(highlightLines({}));
}

void ScenePanel::toggleSelectedEdit()
{
    const auto targets = editTargets();
    if (targets.isEmpty()) { notify("Select a model first."); return; }
    // The gizmo's mode goes round click -> never -> always.  Models that are all in one mode all go to the next; models
    // in different modes all go to click, where the round starts -- so a mixed selection is not turned model by model.
    // With several models selected this changes the mode each has when it is selected alone -- what the tree's buttons
    // show -- and nothing in the viewport: the models are in the multi-select state, moved by one shared gizmo
    const QString first = targets[0]["mode"].toString("click");
    bool same = true;
    for (const auto& t : targets) same = same && t["mode"].toString("click") == first;
    applyModes(targets, same ? nextModeName(first) : QString("click"));
}

void ScenePanel::toggleSelectedLock()
{
    const auto targets = editTargets();
    if (targets.isEmpty()) { notify("Select a model first."); return; }
    // (not all locked: they are all locked; all locked: they are all unlocked)
    bool allLocked = true;
    for (const auto& t : targets) allLocked = allLocked && t.contains("locked");
    applyLock(targets, !allLocked);
}

void ScenePanel::toggleSelectedVisible()
{
    const auto models = selectedModels();
    if (models.isEmpty()) { notify("Select a model first."); return; }
    // (not all shown: they are all shown; all shown: they are all hidden)
    bool allShown = true;
    for (const Model& m : models) allShown = allShown && m.item["visible"].toBool();
    QList<TextEdit> edits;
    QStringList names;
    for (const Model& m : models)
    {
        if (visibilityEdit(m.item, !allShown, edits))
            names << (m.item.contains("var") ? m.item["var"].toString() : m.item["label"].toString());
    }
    if (edits.isEmpty()) return;
    emit(editScript(edits, QString(allShown ? "Hide " : "Show ") + names.join(", ")));
}

void ScenePanel::toggleSelectedCache()
{
    // (the models the cache button is on)
    QList<QJsonObject> targets;
    for (const Model& m : selectedModels())
    {
        const auto& it = m.item;
        const QString kind = it["kind"].toString();
        if (it["failed"].toBool()) continue;
        if ((it.contains("var") && kind != "display" && !it["reassigned"].toBool()) ||
            (kind == "display" && it["can_name"].toBool() && it["visible"].toBool()))
            targets << it;
    }
    if (targets.isEmpty()) { notify("Select a model first."); return; }
    // (not all cached: they are all cached; all cached: the cache is off for all of them)
    bool allOn = true;
    for (const auto& t : targets) allOn = allOn && !t.contains("cache_off");
    applyCache(targets, !allOn);
}

void ScenePanel::toggleEditMode(const QJsonObject& target)
{
    setMode(target, nextMode(target));
}

void ScenePanel::toggleLock(const QJsonObject& target)
{
    if (!target.isEmpty()) applyLock({target}, !target.contains("locked"));
}

void ScenePanel::notify(const QString& text)
{
    // (the line under the tree: it says what could not be done, until the next run of the script)
    m_note->setText(text);
    setCollapsed(m_collapsed);
}

bool ScenePanel::nameThen(const QJsonObject& target, const QString& mode)
{
    // "sphere(3)" becomes "sphere_1 = sphere(3)" and a line "sphere_1" under it; the mode ("click", "never", "always",
    // "lock", "cache") is applied to the new variable once the script has run again
    const QString name = target["new_var"].toString();
    const auto span = target["span"].toArray();            // [line, column, end line, end column]
    if (name.isEmpty() || span.size() != 4 || !m_pending.var.isEmpty()) return false;   // (one at a time)
    const int a = span[0].toInt() - 1, b = span[2].toInt() - 1;
    // The text may have changed since the scene was made: the expression must still start there
    const QString head = target["label"].toString().section("...", 0, 0).left(12).simplified();
    if (lineText(a).mid(span[1].toInt()).simplified().left(head.size()) != head) return false;
    const QString last = lineText(b);
    QList<TextEdit> edits;
    edits << TextEdit{a, span[1].toInt(), a, span[1].toInt(), name + " = "};
    edits << TextEdit{b, int(last.size()), b, int(last.size()), "\n" + name};
    m_pending = Pending{name, mode, 0};
    emit(editScript(edits, "Name " + name));
    return true;
}

void ScenePanel::setMode(const QJsonObject& target, const QString& next, bool quiet)
{
    if (!target.isEmpty()) applyModes({target}, next, quiet);
}

void ScenePanel::applyModes(const QList<QJsonObject>& targets, const QString& next, bool quiet)
{
    if (next != "click" && next != "never" && next != "always") return;
    if (targets.size() == 1 && targets[0]["kind"].toString() == "display")
    {
        nameThen(targets[0], next);
        return;
    }
    QList<TextEdit> edits;
    QStringList names;
    ModeImports imports;
    QString why;
    for (const auto& t : targets)
    {
        // (an expression that has no name yet is named one at a time, above)
        if (t["kind"].toString() == "display" || !t.contains("var")) continue;
        if (modeEdits(t, next, edits, imports, &why)) names << t["var"].toString();
    }
    if (imports.handles) addImportIfMissing(edits, "handles", "handles");
    if (imports.expose) addImportIfMissing(edits, "expose", "handles");
    if (edits.isEmpty())
    {
        if (!why.isEmpty() && !quiet) notify(why);
        return;
    }
    emit(editScript(edits, "Edit " + names.join(", ") + ": " + next));
}

void ScenePanel::applyLock(const QList<QJsonObject>& targets, bool lock)
{
    if (targets.size() == 1 && targets[0]["kind"].toString() == "display")
    {
        // (an expression is named first, and locked once the script has run again)
        if (lock) nameThen(targets[0], "lock");
        return;
    }
    QList<TextEdit> edits;
    QStringList names;
    for (const auto& t : targets)
    {
        if (t["kind"].toString() == "display" || !t.contains("var")) continue;
        if (t.contains("locked") == lock) continue;             // (as asked already)
        const QString var = t["var"].toString();
        if (lock)
        {
            // Under the definition, and under the numbers exposed for its surfaces, its handles and its cache
            int after = t["end_line"].toInt() - 1;
            for (const char* key : {"exposed", "handles", "cache", "cache_off"})
            {
                if (t.contains(key)) after = std::max(after, t[key].toObject()["end_line"].toInt() - 1);
            }
            const QString indent = indentOf(lineText(t["line"].toInt() - 1));
            const QString last = lineText(after);
            edits << TextEdit{after, int(last.size()), after, int(last.size()),
                              "\n" + indent + var + " = lock(" + var + ")"};
        }
        else
        {
            const auto c = t["locked"].toObject();
            edits << deleteLines(c["line"].toInt() - 1, c["end_line"].toInt() - 1);
        }
        names << var;
    }
    if (edits.isEmpty()) return;
    if (lock) addImportIfMissing(edits, "lock", "handles");
    emit(editScript(edits, QString(lock ? "Lock " : "Unlock ") + names.join(", ")));
}

bool ScenePanel::modeEdits(const QJsonObject& target, const QString& next, QList<TextEdit>& edits,
                           ModeImports& imports, QString* why)
{
    Q_UNUSED(why);
    const QString var = target["var"].toString();
    const int a = target["line"].toInt() - 1, b = target["end_line"].toInt() - 1;
    const QString indent = indentOf(lineText(a));
    const bool hasHandles = target.contains("handles"), hasExposed = target.contains("exposed");
    const auto h = target["handles"].toObject();

    // Under the definition, or under the numbers exposed for its surfaces
    const int after = hasExposed ? target["exposed"].toObject()["end_line"].toInt() - 1 : b;
    // The mode of the handles() line there is
    auto writeMode = [&](const QString& m) {
        const QString quoted = "'" + m + "'";
        if (h["mode_span"].isArray())
        {
            const auto s = h["mode_span"].toArray();
            edits << TextEdit{s[0].toInt() - 1, s[1].toInt(), s[2].toInt() - 1, s[3].toInt(), quoted};
            return;
        }
        const auto c = h["call"].toArray();     // [line, col, end line, end col]
        if (c.size() != 4) return;
        const int L = c[2].toInt() - 1, col = c[3].toInt() - 1;     // (before the ")")
        edits << TextEdit{L, col, L, col, ", mode=" + quoted};
    };

    if (hasHandles && h["has_numbers"].toBool())
    {
        writeMode(next);
    }
    else if (hasHandles)
    {
        // (a handles() line without numbers to drag: its line becomes the gizmo's)
        const int e = h["end_line"].toInt() - 1;
        edits << TextEdit{h["line"].toInt() - 1, 0, e, int(lineText(e).size()), indent + gizmoLine(var, next)};
    }
    else
    {
        const QString t = lineText(after);
        edits << TextEdit{after, int(t.size()), after, int(t.size()), "\n" + indent + gizmoLine(var, next)};
    }
    imports.handles = true;
    return true;
}

QString ScenePanel::exposeBlock(const QJsonObject& target, QString* why) const
{
    // The lines that make a shape's surfaces draggable (`x = expose(x, [var(...), ...])`), indented like its definition
    QString error;
    const QString text = m_expose ? m_expose(target["var"].toString(), &error) : QString();
    if (text.isEmpty())
    {
        const QString reason = error.isEmpty() ? "the script has not been run" : error;
        if (qEnvironmentVariableIsSet("FIELDES_AUTOMATION"))
            fprintf(stderr, "[handles] %s: %s\n", target["var"].toString().toUtf8().constData(), reason.toUtf8().constData());
        if (why) *why = "Cannot make " + target["var"].toString() + " draggable: " + reason;
        return QString();
    }
    const QString indent = indentOf(lineText(target["line"].toInt() - 1));
    QStringList lines = text.split('\n');
    for (auto& l : lines) l = indent + l;
    return lines.join('\n');
}

void ScenePanel::exposeSurfaces(const QJsonObject& target)
{
    // (the shape's menu: the numbers of a shape too big to get them by being selected)
    if (target.isEmpty() || !target.contains("var")) return;
    QString why;
    const QString block = exposeBlock(target, &why);
    if (block.isEmpty())
    {
        notify(why);
        return;
    }
    const int b = target["end_line"].toInt() - 1;
    const QString last = lineText(b);
    QList<TextEdit> edits;
    edits << TextEdit{b, int(last.size()), b, int(last.size()), "\n" + block};
    addImportIfMissing(edits, "expose", "handles");
    emit(editScript(edits, "Make " + target["var"].toString() + " draggable by its surfaces"));
}

void ScenePanel::setCacheButton(QTreeWidgetItem* row, const QJsonObject& target)
{
    const bool on = !target.contains("cache_off");          // (on unless the script says render_cache(x, False))
    // What the cache did is looked up by the line that displays the shape
    const int line0 = (target.contains("display_line") ? target["display_line"].toInt() : target["line"].toInt()) - 1;
    QString state = on ? "on" : "off", words;
    if (on && m_cacheStates.contains(line0))
    {
        const QString s = m_cacheStates[line0];         // "state|words"
        state = s.section('|', 0, 0);
        words = s.section('|', 1);
    }
    row->setIcon(COL_CACHE, cacheIcon(state));
    QString tip = on ? "Render cache: on (the default)" : "Render cache: off";
    if (!words.isEmpty()) tip += "  ·  " + words;
    tip += on ? "\nClick: turn it off for this shape (writes render_cache(x, False) under its definition)"
              : "\nClick: turn it back on (deletes its render_cache line)";
    row->setToolTip(COL_CACHE, tip);
}

void ScenePanel::setCacheStates(const QHash<int, QString>& states)
{
    if (states == m_cacheStates) return;
    m_cacheStates = states;
    for (QTreeWidgetItemIterator i(m_tree); *i; ++i)
    {
        QTreeWidgetItem* row = *i;
        if (row->icon(COL_CACHE).isNull()) continue;
        const QString type = row->data(COL_NAME, ROLE_TYPE).toString();
        const auto it = row->data(COL_NAME, ROLE_ITEM).toJsonObject();
        const auto target = type == "part" ? handlesTarget(it, row->data(COL_NAME, ROLE_PART).toInt()) : it;
        if (!target.isEmpty()) setCacheButton(row, target);
    }
}

void ScenePanel::toggleCache(const QJsonObject& target)
{
    // (the button: the other way of what it is now)
    if (!target.isEmpty()) applyCache({target}, target.contains("cache_off"));
}

void ScenePanel::applyCache(const QList<QJsonObject>& targets, bool on)
{
    if (targets.size() == 1 && targets[0]["kind"].toString() == "display")
    {
        // (a displayed expression is on by default: it is named first, as the handles button does, and the line that
        // turns the cache off goes in once the script has run again)
        if (!on) nameThen(targets[0], "cache");
        return;
    }
    QList<TextEdit> edits;
    QStringList names;
    bool needImport = false;
    for (const auto& t : targets)
    {
        if (t["kind"].toString() == "display" || !t.contains("var")) continue;
        const QString var = t["var"].toString();
        if (on)
        {
            // Off by a line `x = render_cache(x, False)`: the line goes, and it is on again (the default)
            if (!t.contains("cache_off")) continue;
            const auto c = t["cache_off"].toObject();
            edits << deleteLines(c["line"].toInt() - 1, c["end_line"].toInt() - 1);
        }
        else
        {
            if (t.contains("cache_off")) continue;                    // (off already)
            if (t.contains("cache"))
            {
                // On, and said so by a line `x = render_cache(x)`: the line becomes the one that says off
                const auto c = t["cache"].toObject();
                const int a = c["line"].toInt() - 1, b = c["end_line"].toInt() - 1;
                const QString indent = indentOf(lineText(a));
                edits << TextEdit{a, 0, b, int(lineText(b).size()), indent + var + " = render_cache(" + var + ", False)"};
            }
            else
            {
                // On by default: the line that turns it off goes under the definition, and under the numbers exposed
                // for its surfaces, its handles and its lock: the cache is the last of what is done to the shape, so
                // that it keeps the shape as it is shown
                int after = t["end_line"].toInt() - 1;
                for (const char* key : {"exposed", "handles", "locked"})
                {
                    if (t.contains(key)) after = std::max(after, t[key].toObject()["end_line"].toInt() - 1);
                }
                const QString indent = indentOf(lineText(t["line"].toInt() - 1));
                const QString last = lineText(after);
                edits << TextEdit{after, int(last.size()), after, int(last.size()),
                                  "\n" + indent + var + " = render_cache(" + var + ", False)"};
                needImport = true;
            }
        }
        names << var;
    }
    if (edits.isEmpty()) return;
    if (needImport) addImportIfMissing(edits, "render_cache", "render_cache");
    emit(editScript(edits, QString(on ? "Render cache on: " : "Render cache off: ") + names.join(", ")));
}

TextEdit ScenePanel::deleteLines(int a, int b) const
{
    const QStringList lines = m_source ? m_source().split('\n') : QStringList();
    if (a < 0 || b >= lines.size() || a > b) return TextEdit{0, 0, 0, 0, QString()};
    if (b + 1 < lines.size()) return TextEdit{a, 0, b + 1, 0, QString()};
    if (a > 0) return TextEdit{a - 1, int(lines[a - 1].size()), b, int(lines[b].size()), QString()};
    return TextEdit{a, 0, b, int(lines[b].size()), QString()};
}

void ScenePanel::deleteRow(QTreeWidgetItem* row)
{
    const QString type = row->data(COL_NAME, ROLE_TYPE).toString();
    const auto it = row->data(COL_NAME, ROLE_ITEM).toJsonObject();
    if (type == "setting")
    {
        if (it.contains("line"))
        {
            emit(editScript({deleteLines(it["line"].toInt() - 1, it["end_line"].toInt() - 1)},
                            "Delete " + it["text"].toString()));
        }
        return;
    }
    if (type == "part")
    {
        const auto parts = it["parts"].toArray();
        const int k = row->data(COL_NAME, ROLE_PART).toInt();
        if (k >= 0 && k < parts.size() && parts[k].toObject().contains("var"))
        {
            deleteItem(itemForVar(parts[k].toObject()["var"].toString()));
        }
        return;
    }
    deleteItem(it);
}

void ScenePanel::deleteItem(const QJsonObject& it)
{
    deleteItems({it});
}

void ScenePanel::deleteSelected()
{
    QList<QJsonObject> items;
    for (const Model& m : selectedModels()) items << m.item;
    if (items.isEmpty()) { notify("Select a model first."); return; }
    deleteItems(items);
}

void ScenePanel::deleteByLine(int line0)
{
    QList<QJsonObject> items;
    for (const Model& m : operandsFor(line0)) items << m.item;
    if (items.isEmpty()) return;
    deleteItems(items);
}

void ScenePanel::deleteItems(const QList<QJsonObject>& items)
{
    // What goes: the statement(s) of each item, the line showing it, the line hiding it, its handles() and
    // expose() lines -- and, for a list of an import's parts, the same for every part variable made from it
    QList<QJsonObject> all;
    QStringList whats;
    QSet<QString> seen;
    for (const auto& it : items)
    {
        if (it.isEmpty() || seen.contains(keyOf(it))) continue;
        seen << keyOf(it);
        all << it;
        whats << (it.contains("var") ? it["var"].toString() : it["label"].toString());
        if (it["kind"].toString() == "import" && it.contains("list_var"))
        {
            for (const auto v : m_scene["items"].toArray())
            {
                const auto o = v.toObject();
                if (o.contains("part_of") && o["part_of"].toInt() == it["line"].toInt()) all << o;
            }
        }
    }
    if (all.isEmpty()) return;
    QList<QPair<int, int>> ranges;                  // 0-based, inclusive
    QStringList names;
    for (const auto& o : all)
    {
        ranges << qMakePair(o["line"].toInt() - 1, o["end_line"].toInt() - 1);
        if (o.contains("display_line")) ranges << qMakePair(o["display_line"].toInt() - 1, o["display_line"].toInt() - 1);
        if (o.contains("hidden_line")) ranges << qMakePair(o["hidden_line"].toInt() - 1, o["hidden_line"].toInt() - 1);
        for (const char* key : {"handles", "exposed", "cache", "cache_off", "locked"})
        {
            if (!o.contains(key)) continue;
            const auto h = o[key].toObject();
            ranges << qMakePair(h["line"].toInt() - 1, h["end_line"].toInt() - 1);
        }
        for (const char* key : {"var", "list_var", "part_tuple_var"})
        {
            if (o.contains(key) && !names.contains(o[key].toString())) names << o[key].toString();
        }
    }
    std::sort(ranges.begin(), ranges.end());
    QList<QPair<int, int>> merged;
    for (const auto& r : ranges)
    {
        if (r.first < 0 || r.second < r.first) continue;
        if (!merged.isEmpty() && r.first <= merged.last().second + 1)
            merged.last().second = std::max(merged.last().second, r.second);
        else
            merged << r;
    }
    if (merged.isEmpty()) return;

    // Does the rest of the script still use what is deleted?
    const QStringList lines = m_source ? m_source().split('\n') : QStringList();
    QStringList users;
    for (int L = 0; L < lines.size(); ++L)
    {
        bool gone = false;
        for (const auto& r : merged) gone |= (L >= r.first && L <= r.second);
        if (gone) continue;
        QString text = lines[L];
        const int hash = text.indexOf('#');
        if (hash >= 0) text = text.left(hash);
        for (const QString& n : names)
        {
            if (QRegularExpression("\b" + QRegularExpression::escape(n) + "\b").match(text).hasMatch())
            {
                users << QString::number(L + 1);
                break;
            }
        }
    }
    const QString what = whats.size() <= 3 ? whats.join(", ") : QString("%1 models").arg(whats.size());
    if (!users.isEmpty() && !qEnvironmentVariableIsSet("FIELDES_AUTOMATION"))
    {
        QMessageBox box(QMessageBox::Question, "Delete", "Delete " + what + "?", QMessageBox::Yes | QMessageBox::Cancel, this);
        box.setInformativeText(QString("The script still uses %1 on line%2 %3: those lines will fail until you edit them.")
                                   .arg(names.join(", "), users.size() > 1 ? "s" : "",
                                        users.mid(0, 8).join(", ") + (users.size() > 8 ? ", ..." : "")));
        box.setDefaultButton(QMessageBox::Cancel);
        if (box.exec() != QMessageBox::Yes) return;
    }

    QList<TextEdit> edits;
    for (const auto& r : merged) edits << deleteLines(r.first, r.second);
    emit(editScript(edits, "Delete " + what));
}

void ScenePanel::removeHandles(const QJsonObject& target)
{
    if (!target.contains("handles")) return;
    const auto h = target["handles"].toObject();
    emit(editScript({deleteLines(h["line"].toInt() - 1, h["end_line"].toInt() - 1)},
                    "Remove the handles of " + target["var"].toString()));
}

void ScenePanel::reimportPart(const QJsonObject& imp, int part)
{
    // What was done to the part in the script is undone (its handles() and expose() lines are deleted)...
    QList<TextEdit> edits;
    const QJsonObject target = handlesTarget(imp, part);
    for (const char* key : {"handles", "exposed"})
    {
        if (!target.contains(key)) continue;
        const auto h = target[key].toObject();
        edits << deleteLines(h["line"].toInt() - 1, h["end_line"].toInt() - 1);
    }
    // ...and the file is read again: the import call's rev= goes up (see reimport)
    reimportEdit(imp, edits);
    if (edits.isEmpty()) return;
    emit(editScript(edits, QString("Reimport part %1 of %2").arg(part).arg(imp["label"].toString())));
}

void ScenePanel::resetImport(const QJsonObject& imp, bool ask)
{
    const QString path = imp["path"].toString();
    if (path.isEmpty()) return;
    const QString label = imp["label"].toString();
    const QString cacheFile = path + ".fieldes-cache.py";
    const QString treesDir = path + ".fieldes-cache.trees";

    // The handle edits of the import's parts: their handles() and expose() lines
    QList<QJsonObject> handled;
    auto consider = [&](const QJsonObject& o) {
        if (!o.isEmpty() && (o.contains("handles") || o.contains("exposed"))) handled << o;
    };
    if (imp.contains("var")) consider(imp);
    for (const auto pv : imp["parts"].toArray())
    {
        const auto p = pv.toObject();
        if (p.contains("var")) consider(itemForVar(p["var"].toString()));
    }

    if (ask)
    {
        QString what = "Delete the cache of " + label + " and import it afresh?";
        QString more = QFileInfo::exists(cacheFile) || QDir(treesDir).exists()
            ? "The import cache is deleted and rebuilt on the next run (this can take a while)."
            : "There is no cache; the file is imported afresh.";
        if (!handled.isEmpty())
        {
            more += QString("\n\nThe handle edits of %1 part(s) are removed from the script.").arg(handled.size());
        }
        QMessageBox box(QMessageBox::Question, "Reset import", what, QMessageBox::Yes | QMessageBox::Cancel, this);
        box.setInformativeText(more);
        box.setDefaultButton(QMessageBox::Cancel);
        if (box.exec() != QMessageBox::Yes) return;
    }

    // Delete each handles() and expose() line
    QList<TextEdit> edits;
    for (const auto& t : handled)
    {
        for (const char* key : {"handles", "exposed"})
        {
            if (!t.contains(key)) continue;
            const auto h = t[key].toObject();
            edits << deleteLines(h["line"].toInt() - 1, h["end_line"].toInt() - 1);
        }
    }

    // ...and the cache: the import is rebuilt (and its cache written again) by the run
    QFile::remove(cacheFile);
    QDir(treesDir).removeRecursively();
    QDir(path + ".fieldes-tessellation").removeRecursively();          // (the tessellated import's tessellation)
    if (!edits.isEmpty())
    {
        emit(editScript(edits, "Reset " + label));
    }
    else
    {
        emit(rerunRequested());
    }
}

void ScenePanel::reimport(const QJsonObject& it)
{
    QList<TextEdit> edits;
    if (reimportEdit(it, edits)) emit(editScript(edits, "Reimport " + it["label"].toString()));
}

bool ScenePanel::reimportEdit(const QJsonObject& it, QList<TextEdit>& edits)
{
    // Works on the statement's current text, not on the last analysis of
    // the script: that is stale while a reimport is still running (or
    // after an error), and must never lead to a second "rev=" argument
    const int a = it["line"].toInt() - 1, b = it["end_line"].toInt() - 1;
    QStringList lines;
    for (int L=a; L <= b; ++L) lines << lineText(L);
    const QString text = lines.join('\n');

    static const QRegularExpression revRe(R"((,\s*)?\brev\s*=\s*(\d+))");
    QList<QRegularExpressionMatch> found;
    for (auto m = revRe.globalMatch(text); m.hasNext(); ) found << m.next();

    QString updated = text;
    if (!found.isEmpty())
    {
        int rev = 0;
        for (const auto& m : found) rev = std::max(rev, m.captured(2).toInt());
        // Bump the first; drop any repeats (from older versions)
        for (int k=found.size() - 1; k >= 1; --k)
        {
            updated.remove(found[k].capturedStart(0), found[k].capturedLength(0));
        }
        updated.replace(found[0].capturedStart(2), found[0].capturedLength(2),
                        QString::number(rev + 1));
    }
    else
    {
        const QString func = it["func"].toString();
        const auto m = QRegularExpression("\\b" + QRegularExpression::escape(func) + "\\s*\\(")
                           .match(text);
        const int close = m.hasMatch() ? matchingParen(text, m.capturedEnd(0) - 1) : -1;
        if (close < 0) return false;
        // Keep a trailing comma tidy: f(a, ) -> f(a, rev=1)
        int k = close - 1;
        while (k >= 0 && text[k].isSpace()) --k;
        const bool comma = k >= 0 && text[k] == ',';
        const bool empty = k >= 0 && text[k] == '(';
        updated.insert(close, (comma || empty) ? "rev=1" : ", rev=1");
    }
    if (updated == text) return false;
    edits << TextEdit{a, 0, b, int(lines.last().size()), updated};
    return true;
}

QStringList ScenePanel::visibleRoiExprs(int skipImportLine, int* endLine) const
{
    QStringList exprs;
    for (const auto v : m_scene["items"].toArray())
    {
        const auto it = v.toObject();
        if (!it["visible"].toBool() || it["failed"].toBool()) continue;
        if (skipImportLine > 0 && it["part_of"].toInt() == skipImportLine) continue;
        if (it.contains("roi_expr")) exprs << it["roi_expr"].toString();
        else if (it.contains("bounds")) exprs << boxLiteral(it["bounds"].toArray());
        else continue;
        if (endLine) *endLine = std::max(*endLine, it["end_line"].toInt());
    }
    exprs.removeDuplicates();
    return exprs;
}

bool ScenePanel::roiEdits(const QJsonObject& it, QList<TextEdit>& edits) const
{
    const auto settings = m_scene["settings"].toObject();
    // roi() of the model's name when it has one, else of its box, so the
    // automatic resolution below can follow either
    QString expr = it["roi_expr"].toString();
    if (expr.isEmpty() && it.contains("bounds"))
    {
        expr = boxLiteral(it["bounds"].toArray());
    }
    if (expr.isEmpty())
    {
        return false;
    }
    const QString boundsStmt = "view.set_bounds(*roi(" + expr + "))";

    const int after = it["end_line"].toInt() - 1;
    if (settings.contains("set_bounds"))
    {
        const auto s = settings["set_bounds"].toObject()["span"].toArray();
        if (spanText(s) != boundsStmt)
        {
            edits << TextEdit{s[0].toInt() - 1, s[1].toInt(), s[2].toInt() - 1, s[3].toInt(), boundsStmt};
        }
    }
    else
    {
        const QString t = lineText(after);
        edits << TextEdit{after, int(t.size()), after, int(t.size()), "\n" + boundsStmt};
    }
    if (!expr.isEmpty())
    {
        const QString resStmt = "view.set_resolution(roi_resolution(" + expr + "))";
        if (!settings.contains("set_resolution"))
        {
            const QString t = lineText(after);
            edits << TextEdit{after, int(t.size()), after, int(t.size()), "\n" + resStmt};
            if (!m_scene["has_roi_resolution"].toBool()) addImportIfMissing(edits, "roi_resolution");
        }
        else
        {
            // An automatic resolution follows the new region; a number
            // typed by hand is left alone
            const auto s = settings["set_resolution"].toObject()["span"].toArray();
            const QString cur = spanText(s);
            if (cur.contains("roi_resolution(") && cur != resStmt)
            {
                edits << TextEdit{s[0].toInt() - 1, s[1].toInt(), s[2].toInt() - 1, s[3].toInt(), resStmt};
            }
        }
    }
    if (!edits.isEmpty() && !m_scene["has_roi"].toBool())
    {
        addImportIfMissing(edits, "roi");
    }
    return true;
}

QString ScenePanel::spanText(const QJsonArray& s) const
{
    if (s.size() != 4) return QString();
    const int l0 = s[0].toInt() - 1, c0 = s[1].toInt();
    const int l1 = s[2].toInt() - 1, c1 = s[3].toInt();
    if (l0 == l1) return lineText(l0).mid(c0, c1 - c0);
    QString out = lineText(l0).mid(c0);
    for (int l = l0 + 1; l < l1; ++l) out += "\n" + lineText(l);
    return out + "\n" + lineText(l1).left(c1);
}

void ScenePanel::showPart(const QJsonObject& imp, int part)
{
    const auto parts = imp["parts"].toArray();
    if (part < 0 || part >= parts.size() || !parts[part].toObject()["ok"].toBool())
    {
        return;
    }
    if (imp.contains("index"))
    {
        // The part in use is the statement's own shape; others are added
        if (imp["index"].toInt() == part) toggleVisible(imp);
        else showOtherPart(imp, part);
        return;
    }
    const auto p = parts[part].toObject();
    if (p.contains("var"))
    {
        // Already bound: toggle that variable's item
        const auto o = itemForVar(p["var"].toString());
        if (!o.isEmpty()) toggleVisible(o);
        return;
    }
    const QString list = imp["list_var"].toString();
    const QString name = uniqueName(list + "_" + QString::number(part));
    const int L = imp["end_line"].toInt() - 1;
    const QString t = lineText(L);
    QList<TextEdit> edits;
    edits << TextEdit{L, int(t.size()), L, int(t.size()),
                      QString("\n%1 = %2[%3][0]\n%1").arg(name).arg(list).arg(part)};
    emit(editScript(edits, "Show part " + QString::number(part)));
}

void ScenePanel::switchPart(const QJsonObject& imp, int part)
{
    if (!imp.contains("index_span")) return;
    const auto s = imp["index_span"].toArray();
    QList<TextEdit> edits;
    edits << TextEdit{s[0].toInt() - 1, s[1].toInt(), s[2].toInt() - 1, s[3].toInt(),
                      QString::number(part)};
    emit(editScript(edits, "Use part " + QString::number(part)));
}

void ScenePanel::focusOn(const QJsonObject& it)
{
    if (it.contains("bounds"))
    {
        const auto b = it["bounds"].toArray();
        const auto lo = b[0].toArray(), hi = b[1].toArray();
        emit(focusRequested(QVector3D(lo[0].toDouble(), lo[1].toDouble(), lo[2].toDouble()),
                            QVector3D(hi[0].toDouble(), hi[1].toDouble(), hi[2].toDouble()),
                            linesOf(it)));
    }
    else
    {
        emit(focusRequested(QVector3D(), QVector3D(), linesOf(it)));
    }
}

void ScenePanel::select(QTreeWidgetItem* row, bool focus)
{
    if (!row) return;
    m_selectedKey = row->data(COL_NAME, ROLE_KEY).toString();
    const QString type = row->data(COL_NAME, ROLE_TYPE).toString();
    const auto it = row->data(COL_NAME, ROLE_ITEM).toJsonObject();

    if (type == "setting")
    {
        const int line = row->data(COL_NAME, ROLE_LINE).toInt();
        if (line >= 0) emit(goToLine(line));
        emit(highlightLines({}));
        return;
    }
    if (type == "settings")
    {
        emit(highlightLines({}));
        return;
    }
    if (type == "part")
    {
        const int k = row->data(COL_NAME, ROLE_PART).toInt();
        const auto p = it["parts"].toArray()[k].toObject();
        // Jump to the statement binding the part, else to the import
        int line = it["line"].toInt() - 1;
        QList<int> lines;
        for (const auto v : m_scene["items"].toArray())
        {
            const auto o = v.toObject();
            if (p.contains("var") && o["var"].toString() == p["var"].toString() &&
                o["kind"].toString() != "import")
            {
                line = o["line"].toInt() - 1;
                lines = linesOf(o);
            }
        }
        if (it.contains("index") && it["index"].toInt() == k) lines = linesOf(it);
        emit(goToLine(line));
        emit(highlightLines(lines));
        if (focus)
        {
            const auto b = p["bounds"].toArray();
            if (b.size() == 2)
            {
                const auto lo = b[0].toArray(), hi = b[1].toArray();
                emit(focusRequested(QVector3D(lo[0].toDouble(), lo[1].toDouble(), lo[2].toDouble()),
                                    QVector3D(hi[0].toDouble(), hi[1].toDouble(), hi[2].toDouble()),
                                    lines));
            }
        }
        return;
    }
    if (type == "dep")
    {
        // Select the item this one depends on
        const QString dep = row->data(COL_NAME, ROLE_PART).toString();
        for (int k=0; k < m_tree->topLevelItemCount(); ++k)
        {
            auto r = m_tree->topLevelItem(k);
            if (r->data(COL_NAME, ROLE_ITEM).toJsonObject()["var"].toString() == dep)
            {
                m_tree->setCurrentItem(r);
                select(r, focus);
                return;
            }
        }
        return;
    }
    emit(goToLine(it["line"].toInt() - 1));
    emit(highlightLines(linesOf(it)));
    if (focus)
    {
        focusOn(it);
    }
}

void ScenePanel::onItemClicked(QTreeWidgetItem* row, int column)
{
    const Qt::KeyboardModifiers mods = m_clickMods;
    m_clickMods = Qt::NoModifier;
    const QString type = row->data(COL_NAME, ROLE_TYPE).toString();
    const auto it = row->data(COL_NAME, ROLE_ITEM).toJsonObject();
    if (column == COL_EYE && !row->icon(COL_EYE).isNull())
    {
        if (type == "part")
        {
            showPart(it, row->data(COL_NAME, ROLE_PART).toInt());
        }
        else if (it.contains("list_var") && !it.contains("var"))
        {
            // Every part at once: show all unless all are already shown
            setAllParts(it, !row->data(COL_EYE, Qt::UserRole).toBool());
        }
        else
        {
            toggleVisible(it);
        }
        return;
    }
    if (column == COL_HANDLES && !row->icon(COL_HANDLES).isNull())
    {
        toggleEditMode(type == "part" ? handlesTarget(it, row->data(COL_NAME, ROLE_PART).toInt()) : it);
        return;
    }
    if (column == COL_LOCK && !row->icon(COL_LOCK).isNull())
    {
        toggleLock(type == "part" ? handlesTarget(it, row->data(COL_NAME, ROLE_PART).toInt()) : it);
        return;
    }
    if (column == COL_CACHE && !row->icon(COL_CACHE).isNull())
    {
        toggleCache(type == "part" ? handlesTarget(it, row->data(COL_NAME, ROLE_PART).toInt()) : it);
        return;
    }
    if (column == COL_DELETE && !row->icon(COL_DELETE).isNull())
    {
        deleteRow(row);
        return;
    }
    if (column == COL_RESET && !row->icon(COL_RESET).isNull())
    {
        if (type == "part") reimportPart(it, row->data(COL_NAME, ROLE_PART).toInt());
        else resetImport(it, !qEnvironmentVariableIsSet("FIELDES_AUTOMATION"));
        return;
    }
    if (column == COL_ACTION && !row->icon(COL_ACTION).isNull())
    {
        if (it["kind"].toString() == "import") reimport(it);
        return;
    }
    // Ctrl or Shift: the selection grew or shrank (the tree did that itself, onSelectionChanged lit the shapes up);
    // the editor and the camera stay where they are
    if (mods & (Qt::ControlModifier | Qt::ShiftModifier)) return;
    select(row, true);
}

void ScenePanel::onItemDoubleClicked(QTreeWidgetItem* row, int column)
{
    if (column != COL_NAME) return;
    if (row->data(COL_NAME, ROLE_TYPE).toString() == "part")
    {
        const auto imp = row->data(COL_NAME, ROLE_ITEM).toJsonObject();
        const int k = row->data(COL_NAME, ROLE_PART).toInt();
        if (imp.contains("index")) switchPart(imp, k);      // use this part instead
        else showPart(imp, k);
    }
}

void ScenePanel::onContextMenu(const QPoint& pos)
{
    auto row = m_tree->itemAt(pos);
    if (!row) return;
    const QString type = row->data(COL_NAME, ROLE_TYPE).toString();
    const auto it = row->data(COL_NAME, ROLE_ITEM).toJsonObject();
    const QString kind = it["kind"].toString();
    const bool failed = it["failed"].toBool() || kind == "failed";

    QMenu menu(this);
    menu.addAction("Go to code", this, [=]{ select(row, false); });
    menu.addSeparator();
    if (type == "item" || type == "part")
    {
        menu.addAction("Focus camera", this, [=]{ select(row, true); });
    }
    if (type == "item" && !failed && (it.contains("var") || kind == "display"))
    {
        menu.addAction(it["visible"].toBool() ? "Hide" : "Show", this,
                       [=]{ toggleVisible(it); });
    }
    if (type == "item" && kind == "import")
    {
        menu.addAction("Reimport", this, [=]{ reimport(it); });
        if (it.contains("path") && !failed && it["func"].toString().startsWith("import_step"))
        {
            menu.addAction("Reset (clear cache and handle edits)", this,
                           [=]{ resetImport(it, !qEnvironmentVariableIsSet("FIELDES_AUTOMATION")); });
        }
    }
    if (type == "item" && !failed && ((it.contains("var") && kind != "display") ||
                                      (kind == "display" && it["can_name"].toBool() && it["visible"].toBool())))
    {
        addModeActions(menu, it);
    }
    if (type == "part")
    {
        const int k = row->data(COL_NAME, ROLE_PART).toInt();
        const bool ok = it["parts"].toArray()[k].toObject()["ok"].toBool();
        const auto target = handlesTarget(it, k);
        if (ok && !target.isEmpty())
        {
            addModeActions(menu, target);
        }
        if (ok && it["func"].toString().startsWith("import_step"))
        {
            menu.addAction("Reimport this part", this,
                           [=]{ reimportPart(it, k); });
        }
        if (ok && it.contains("index"))
            menu.addAction("Use this part", this, [=]{ switchPart(it, k); });
        else if (ok)
            menu.addAction("Show / hide", this, [=]{ showPart(it, k); });
    }
    if (!row->icon(COL_DELETE).isNull())
    {
        menu.addSeparator();
        // (a row that is one of several selected models stands for all of them, as for the other keys)
        const bool group = row->isSelected() && selectedModels().size() >= 2;
        menu.addAction(deleteIcon(), group ? "Delete the selected models" : "Delete", this,
                       [=]{ if (group) deleteSelected(); else deleteRow(row); });
    }
    menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

void ScenePanel::addModeActions(QMenu& menu, const QJsonObject& target)
{
    const QString mode = target["mode"].toString("click");
    struct Way { const char* mode; const char* text; };
    for (const Way& w : {Way{"click", "Gizmo: shown when the model is selected (click)"},
                         Way{"never", "Gizmo: never shown"},
                         Way{"always", "Gizmo: always shown"}})
    {
        auto act = menu.addAction(w.text, this, [=]{ setMode(target, w.mode); });
        act->setCheckable(true);
        act->setChecked(mode == w.mode);
    }
    auto locked = menu.addAction("Locked: it cannot be dragged", this, [=]{ toggleLock(target); });
    locked->setCheckable(true);
    locked->setChecked(target.contains("locked"));
    if (target.contains("handles"))
    {
        menu.addAction("Remove its handles() line", this, [=]{ removeHandles(target); });
    }
    if (!target["has_var"].toBool() && !target.contains("exposed") && target["can_expose"].toBool() &&
        !target.contains("locked"))
    {
        menu.addAction(QString("Make its surfaces draggable (%1 numbers)").arg(target["expose_count"].toInt()), this,
                       [=]{ exposeSurfaces(target); });
    }
    menu.addSeparator();
    auto cache = menu.addAction("Render cache (on unless turned off)", this, [=]{ toggleCache(target); });
    cache->setCheckable(true);
    cache->setChecked(!target.contains("cache_off"));
}

bool ScenePanel::describeModel(const QJsonObject& target, QString* source, int* after) const
{
    if (target.isEmpty()) return false;
    int aft = target["end_line"].toInt() - 1;
    QString src;
    if (target.contains("var"))
    {
        src = target["var"].toString();
        for (const char* key : {"exposed", "handles"})
        {
            if (target.contains(key)) aft = std::max(aft, target[key].toObject()["end_line"].toInt() - 1);
        }
    }
    else
    {
        // (an expression: its own text goes into the call)
        const auto span = target["span"].toArray();
        if (span.size() == 4) src = spanText(span);
        else
        {
            QStringList parts;
            for (int L = target["line"].toInt() - 1; L <= target["end_line"].toInt() - 1; ++L) parts << lineText(L).trimmed();
            src = parts.join(' ');
        }
        src = src.trimmed();
    }
    if (src.isEmpty()) return false;
    *source = src;
    *after = aft;
    return true;
}

bool ScenePanel::resolveModel(int line0, QJsonObject* model, QString* source, int* after) const
{
    // The model that is displayed on that line: a variable (the shape, a part of an import) or an expression
    QJsonObject target;
    for (const auto v : m_scene["items"].toArray())
    {
        const auto o = v.toObject();
        if (o["failed"].toBool() || o["kind"].toString() == "failed") continue;
        if (linesOf(o).contains(line0) && (o.contains("var") || o["kind"].toString() == "display"))
        {
            target = o;
            break;
        }
    }
    if (!describeModel(target, source, after)) return false;
    *model = target;
    return true;
}

void ScenePanel::addSurfaceSelection(int line0, QVector3D seed, QString mode, double angle, double thickness,
                                     double radius)
{
    QJsonObject target;
    QString source;
    int after = 0;
    if (!resolveModel(line0, &target, &source, &after)) return;

    // A free name: selection_1, selection_2, ...
    const QString src = m_source ? m_source() : QString();
    QString name;
    for (int n = 1;; ++n)
    {
        name = "selection_" + QString::number(n);
        if (!QRegularExpression("\\b" + name + "\\b").match(src).hasMatch()) break;
    }
    auto num = [](double v) { return QString::number(v, 'g', 5); };
    QString call = QString("%1 = select_surface(%2, seed=(%3, %4, %5), angle=%6")
                       .arg(name, source, num(seed.x()), num(seed.y()), num(seed.z()), num(angle));
    if (mode != "flat") call += ", mode='" + mode + "'";
    if (thickness > 0) call += ", thickness=" + num(thickness);
    if (radius > 0) call += ", radius=" + num(radius);
    call += ")";

    const QString indent = indentOf(lineText(target["line"].toInt() - 1));
    const QString last = lineText(after);
    QList<TextEdit> edits;
    edits << TextEdit{after, int(last.size()), after, int(last.size()),
                      "\n" + indent + call + "\n" + indent + name};
    addImportIfMissing(edits, "select_surface", "selection");
    // (the new selection is the selected model once the script has run)
    m_selectNew = name;
    m_selectNewTries = 0;
    emit(editScript(edits, "Select surface " + name));
}

QTreeWidgetItem* ScenePanel::rowForLine(int line0) const
{
    std::function<QTreeWidgetItem*(QTreeWidgetItem*)> find = [&](QTreeWidgetItem* r) -> QTreeWidgetItem* {
        for (int k=0; k < r->childCount(); ++k)
        {
            if (auto f = find(r->child(k))) return f;
        }
        const auto it = r->data(COL_NAME, ROLE_ITEM).toJsonObject();
        const QString type = r->data(COL_NAME, ROLE_TYPE).toString();
        if (type == "item" && linesOf(it).contains(line0))
        {
            return r;
        }
        if (type == "part")
        {
            const auto p = it["parts"].toArray()[r->data(COL_NAME, ROLE_PART).toInt()].toObject();
            if (p.contains("var") && linesOf(itemForVar(p["var"].toString())).contains(line0))
            {
                return r;
            }
        }
        return nullptr;
    };
    for (int k=0; k < m_tree->topLevelItemCount(); ++k)
    {
        if (auto r = find(m_tree->topLevelItem(k))) return r;
    }
    return nullptr;
}

void ScenePanel::selectByLine(int line0)
{
    if (auto r = rowForLine(line0))
    {
        m_tree->setCurrentItem(r);          // (the selection is this row alone)
        m_tree->scrollToItem(r);
        select(r, false);
    }
}

void ScenePanel::toggleByLine(int line0)
{
    auto r = rowForLine(line0);
    if (!r) return;
    // (the row joins the selection, or leaves it; the others stay as they are)
    m_tree->setCurrentItem(r, 0, QItemSelectionModel::Toggle | QItemSelectionModel::Rows);
    m_tree->scrollToItem(r);
    if (r->isSelected()) m_selectedKey = r->data(COL_NAME, ROLE_KEY).toString();
}

void ScenePanel::selectLines(QList<int> lines0, bool add)
{
    if (!add) m_tree->clearSelection();
    QTreeWidgetItem* last = nullptr;
    for (int line0 : lines0)
    {
        if (auto r = rowForLine(line0))
        {
            m_tree->setCurrentItem(r, 0, QItemSelectionModel::Select | QItemSelectionModel::Rows);
            last = r;
        }
    }
    if (last)
    {
        m_selectedKey = last->data(COL_NAME, ROLE_KEY).toString();
        m_tree->scrollToItem(last);
    }
}

QList<int> ScenePanel::rowLines(QTreeWidgetItem* row) const
{
    // The lines that show a row's model (as select() lights them up)
    const QString type = row->data(COL_NAME, ROLE_TYPE).toString();
    const auto it = row->data(COL_NAME, ROLE_ITEM).toJsonObject();
    QList<int> out;
    if (type == "item") out = linesOf(it);
    else if (type == "part")
    {
        const int k = row->data(COL_NAME, ROLE_PART).toInt();
        const auto p = it["parts"].toArray()[k].toObject();
        if (p.contains("var"))
        {
            for (const auto v : m_scene["items"].toArray())
            {
                const auto o = v.toObject();
                if (o["var"].toString() == p["var"].toString() && o["kind"].toString() != "import") out << linesOf(o);
            }
        }
        if (it.contains("index") && it["index"].toInt() == k) out << linesOf(it);
    }
    return out;
}

QList<int> ScenePanel::selectedLines() const
{
    QList<int> out;
    for (auto row : m_tree->selectedItems())
    {
        for (int l : rowLines(row)) if (!out.contains(l)) out << l;
    }
    return out;
}

QJsonObject ScenePanel::modelOfRow(QTreeWidgetItem* row) const
{
    // The model a row stands for: a variable, or an expression displayed on its own (nothing for the other rows)
    const QString type = row->data(COL_NAME, ROLE_TYPE).toString();
    const auto it = row->data(COL_NAME, ROLE_ITEM).toJsonObject();
    QJsonObject model;
    if (type == "item") model = it;
    else if (type == "part")
    {
        const int k = row->data(COL_NAME, ROLE_PART).toInt();
        const auto p = it["parts"].toArray()[k].toObject();
        if (p.contains("var")) model = itemForVar(p["var"].toString());
        else if (it.contains("index") && it["index"].toInt() == k) model = it;
    }
    if (model.isEmpty() || model["failed"].toBool() || model["kind"].toString() == "failed") return QJsonObject();
    return (model.contains("var") || model["kind"].toString() == "display") ? model : QJsonObject();
}

bool ScenePanel::stripLocked(bool warn)
{
    // A locked model cannot be in a selection of several: it stays out (selected on its own it is fine: that is how
    // it is unlocked)
    QList<QTreeWidgetItem*> models, locked;
    for (auto row : m_tree->selectedItems())
    {
        const auto model = modelOfRow(row);
        if (model.isEmpty()) continue;
        models << row;
        if (model.contains("locked")) locked << row;
    }
    if (models.size() < 2 || locked.isEmpty()) return false;
    m_stripping = true;
    for (auto row : locked) m_tree->setCurrentItem(row, 0, QItemSelectionModel::Deselect | QItemSelectionModel::Rows);
    for (auto row : models)
    {
        if (locked.contains(row)) continue;
        m_tree->setCurrentItem(row, 0, QItemSelectionModel::NoUpdate);
        break;
    }
    m_stripping = false;
    if (warn && !m_silentStrip) QTimer::singleShot(0, this, [this]{ warnLockedMultiSelect(); });
    return true;
}

void ScenePanel::warnLockedMultiSelect()
{
    QSettings store;
    if (store.value("hidden-messages/locked-multi-select", false).toBool()) return;
    if (qEnvironmentVariableIsSet("FIELDES_AUTOMATION"))
    {
        fprintf(stderr, "[warning] locked shapes cannot be multi selected\n");
        return;
    }
    QMessageBox box(QMessageBox::Warning, "Locked shapes", "Locked shapes cannot be multi selected.", QMessageBox::Ok, this);
    box.setInformativeText("A locked shape stays out of a selection of several, and is left out of this one. Select it "
                           "on its own to unlock it (its lock button, or R).");
    auto again = new QCheckBox("Do not show this message again");
    box.setCheckBox(again);
    box.exec();
    if (again->isChecked()) store.setValue("hidden-messages/locked-multi-select", true);
}

void ScenePanel::updateMultiNote()
{
    // The multi-select state is not an edit mode of any model: the line under the tree says what it is
    const int n = selectedModels().size();
    if (n >= 2)
    {
        if (m_note->text().isEmpty() || m_multiNote)
        {
            m_multiNote = true;
            m_note->setText(QString("%1 models selected: one gizmo moves them all. Each keeps its own gizmo mode (E) for "
                                    "when it is selected alone.").arg(n));
            setCollapsed(m_collapsed);
        }
    }
    else if (m_multiNote)
    {
        m_multiNote = false;
        m_note->setText(QString());
        setCollapsed(m_collapsed);
    }
}

void ScenePanel::prepareSelection()
{
    // The selected models are made ready to be dragged.  Pulling a shape's surfaces is always there, so a model that has
    // no numbers to pull them by gets them -- an `expose(x, [...])` line -- when they are few (a bigger one has it in its
    // menu).  The gizmo is shown by its mode (click: while the model is selected) and moves what has numbers to move it
    // by -- a `handles(x, move=(var, ...))` line, which a model that has none gets too, in the mode it is in (click, when
    // it has no line).  Nothing else about the model changes
    const auto models = selectedModels();
    if (models.isEmpty()) return;
    if (m_editPending && m_editClock.elapsed() < 20000)
    {
        m_prepareAgain = true;                  // (the scene is out of date until the run of the last edit is done)
        return;
    }
    QList<TextEdit> edits;
    QStringList names;
    QStringList reselect;           // (what is selected after the run: an expression that is named is another model)
    bool naming = false;
    ModeImports imports;
    for (const Model& m : models)
    {
        const auto& t = m.item;
        if (t["kind"].toString() == "display" && !t["failed"].toBool())
        {
            // An expression displayed on its own (`sphere(3)`) has no name, so nothing to hang a gizmo line under:
            // it is given one first, as the gizmo button does, and gets its lines after the run
            const QString name = t["new_var"].toString();
            const auto span = t["span"].toArray();            // [line, column, end line, end column]
            bool named = false;
            if (t["can_name"].toBool() && t["visible"].toBool() && !name.isEmpty() && span.size() == 4)
            {
                const int a = span[0].toInt() - 1, b = span[2].toInt() - 1;
                const QString head = t["label"].toString().section("...", 0, 0).left(12).simplified();
                if (lineText(a).mid(span[1].toInt()).simplified().left(head.size()) == head)
                {
                    const QString last = lineText(b);
                    edits << TextEdit{a, span[1].toInt(), a, span[1].toInt(), name + " = "};
                    edits << TextEdit{b, int(last.size()), b, int(last.size()), "\n" + name};
                    reselect << "shape:" + name;
                    names << name;
                    naming = named = true;
                }
            }
            if (!named) reselect << keyOf(t);
            continue;
        }
        reselect << keyOf(t);
        if (t["failed"].toBool() || t["reassigned"].toBool() || t["no_handles"].toBool() ||
            !t.contains("var") || t.contains("locked")) continue;
        const QString var = t["var"].toString();
        const QString src = m_source ? m_source() : QString();
        const bool hasHandles = t.contains("handles");
        bool needGizmo = !(hasHandles && t["handles"].toObject()["has_numbers"].toBool());
        // (the scene may be a run behind the text: a line that is there already is never written twice)
        if (needGizmo && !hasHandles &&
            QRegularExpression(QString("^\\s*%1\\s*=\\s*handles\\(\\s*%1\\b").arg(QRegularExpression::escape(var)),
                               QRegularExpression::MultilineOption).match(src).hasMatch())
            needGizmo = false;
        bool needExpose = !t["has_var"].toBool() && !t.contains("exposed") && t["can_expose"].toBool() &&
                          t["expose_count"].toInt() <= kAutoExposeNumbers && !m_exposeTried.contains(keyOf(t));
        if (needExpose &&
            QRegularExpression(QString("^\\s*%1\\s*=\\s*expose\\(\\s*%1\\b").arg(QRegularExpression::escape(var)),
                               QRegularExpression::MultilineOption).match(src).hasMatch())
            needExpose = false;
        if (!needGizmo && !needExpose) continue;
        const QString indent = indentOf(lineText(t["line"].toInt() - 1));
        const int b = t["end_line"].toInt() - 1;
        QString block;
        if (needExpose)
        {
            m_exposeTried << keyOf(t);                  // (once: a shape that cannot be exposed is not asked again)
            block = exposeBlock(t, nullptr);
            if (!block.isEmpty()) imports.expose = true;
        }
        if (!needGizmo && block.isEmpty()) continue;
        const QString mode = t["mode"].toString("click");
        if (needGizmo && hasHandles)
        {
            // (a handles() line without numbers: its line becomes the gizmo's)
            const auto h = t["handles"].toObject();
            const int e = h["end_line"].toInt() - 1;
            edits << TextEdit{h["line"].toInt() - 1, 0, e, int(lineText(e).size()), indent + gizmoLine(var, mode)};
            if (!block.isEmpty())
                edits << TextEdit{b, int(lineText(b).size()), b, int(lineText(b).size()), "\n" + block};
        }
        else
        {
            // Under the definition, or under the numbers exposed for its surfaces
            const int after = t.contains("exposed") ? t["exposed"].toObject()["end_line"].toInt() - 1 : b;
            QString text;
            if (!block.isEmpty()) text += "\n" + block;
            if (needGizmo) text += "\n" + indent + gizmoLine(var, mode);
            const QString last = lineText(after);
            edits << TextEdit{after, int(last.size()), after, int(last.size()), text};
        }
        if (needGizmo) imports.handles = true;
        names << var;
    }
    if (edits.isEmpty())
    {
        // (nothing to prepare: a menu entry that was waiting for the scene goes ahead)
        if (m_afterRun)
        {
            const auto later = m_afterRun;
            m_afterRun = nullptr;
            QTimer::singleShot(0, this, later);
        }
        return;
    }
    if (imports.handles) addImportIfMissing(edits, "handles", "handles");
    if (imports.expose) addImportIfMissing(edits, "expose", "handles");
    m_editPending = true;
    m_editClock.start();
    if (naming)
    {
        // (the selection is the same models once the script has run, the named ones under their new names; they
        // get their lines then)
        m_reselect = reselect;
        m_reselectTries = 0;
        m_prepareAgain = true;
    }
    emit(editScript(edits, "Make " + names.join(", ") + " draggable"));
}

void ScenePanel::onSelectionChanged()
{
    if (m_rebuilding || m_stripping) return;
    stripLocked(true);
    // The order the rows were selected in (the first one is what a subtraction subtracts from): the ones that went
    // are dropped, the ones that came are put at the end -- in tree order when several came at once
    QSet<QString> now;
    for (auto row : m_tree->selectedItems()) now << row->data(COL_NAME, ROLE_KEY).toString();
    for (int i = m_selectOrder.size() - 1; i >= 0; --i)
    {
        if (!now.contains(m_selectOrder[i])) m_selectOrder.removeAt(i);
    }
    for (QTreeWidgetItemIterator i(m_tree); *i; ++i)
    {
        const QString key = (*i)->data(COL_NAME, ROLE_KEY).toString();
        if (now.contains(key) && !m_selectOrder.contains(key)) m_selectOrder << key;
    }
    emit(highlightLines(selectedLines()));
    updateMultiNote();
    // (once the selection has settled: a rectangle selects its models one after the other)
    if (!selectedModels().isEmpty()) m_prepareTimer.start();
}

QList<ScenePanel::Model> ScenePanel::selectedModels() const
{
    QHash<QString, QTreeWidgetItem*> rows;
    for (auto row : m_tree->selectedItems()) rows[row->data(COL_NAME, ROLE_KEY).toString()] = row;
    QList<Model> out;
    QSet<QString> seen;
    QStringList order = m_selectOrder;
    for (auto it = rows.constBegin(); it != rows.constEnd(); ++it)
    {
        if (!order.contains(it.key())) order << it.key();       // (defensive: a row the order does not know)
    }
    for (const QString& key : order)
    {
        auto row = rows.value(key);
        if (!row) continue;
        const QJsonObject item = modelOfRow(row);
        if (item.isEmpty()) continue;
        const QString k = keyOf(item);
        if (seen.contains(k)) continue;
        Model m;
        if (!describeModel(item, &m.source, &m.after)) continue;
        m.item = item;
        seen << k;
        out << m;
    }
    return out;
}

QList<ScenePanel::Model> ScenePanel::operandsFor(int line0) const
{
    const QList<Model> picked = selectedModels();
    if (line0 < 0)
    {
        // (empty space: the selected models, else the last one the script defines)
        if (!picked.isEmpty()) return picked;
        Model m;
        m.item = defaultModel();
        if (describeModel(m.item, &m.source, &m.after)) return {m};
        return {};
    }
    Model clicked;
    if (!resolveModel(line0, &clicked.item, &clicked.source, &clicked.after)) return {};
    // (a shape that is one of several selected ones stands for all of them; another one stands for itself)
    if (picked.size() >= 2)
    {
        for (const Model& p : picked)
        {
            if (keyOf(p.item) == keyOf(clicked.item)) return picked;
        }
    }
    return {clicked};
}

bool ScenePanel::combines(const QString& operation) const
{
    if (m_combining.isEmpty() && m_support)
    {
        // (which operations of the menu take several models: the interpreter's list says)
        QString error;
        const auto doc = QJsonDocument::fromJson(m_support("menu_catalog", QString(), &error).toUtf8());
        for (const auto v : doc.object()["operations"].toArray())
        {
            const auto o = v.toObject();
            m_combining[o["name"].toString()] = o["other"].toBool();
        }
    }
    return m_combining.value(operation, false);
}

////////////////////////////////////////////////////////////////////////////////
// The viewport's menus and the I key

bool ScenePanel::selectVar(const QString& var)
{
    const auto it = itemForVar(var);
    if (it.isEmpty()) return false;
    const QString key = keyOf(it);
    for (QTreeWidgetItemIterator i(m_tree); *i; ++i)
    {
        if ((*i)->data(COL_NAME, ROLE_KEY).toString() == key &&
            (*i)->data(COL_NAME, ROLE_TYPE).toString() == "item")
        {
            m_tree->setCurrentItem(*i);
            m_tree->scrollToItem(*i);
            select(*i, false);
            return true;
        }
    }
    return false;
}

QString ScenePanel::freeName(const QString& base) const
{
    // (never the library's own name: `sphere = sphere(...)` would shadow the function)
    const QString src = m_source ? m_source() : QString();
    for (int n = 1;; ++n)
    {
        const QString name = base + "_" + QString::number(n);
        if (!QRegularExpression("\\b" + QRegularExpression::escape(name) + "\\b").match(src).hasMatch()) return name;
    }
}

QJsonObject ScenePanel::defaultModel() const
{
    // The selected model, else the last one the script defines
    if (m_tree->currentItem())
    {
        const QJsonObject t = selectedTarget();
        if (!t.isEmpty()) return t;
    }
    QJsonObject last;
    for (const auto v : m_scene["items"].toArray())
    {
        const auto o = v.toObject();
        if (o["failed"].toBool() || o["reassigned"].toBool() || o["kind"].toString() == "failed") continue;
        if (o.contains("var")) last = o;
    }
    return last;
}

bool ScenePanel::hasModel() const
{
    for (const auto v : m_scene["items"].toArray())
    {
        const auto o = v.toObject();
        if (o["failed"].toBool() || o["kind"].toString() == "failed") continue;
        if (o.contains("var") || o["kind"].toString() == "display") return true;
    }
    return false;
}

bool ScenePanel::hasOtherModel(int line0) const
{
    // (several models selected: the operations that combine models work on them)
    if (operandsFor(line0).size() >= 2) return true;
    QJsonObject target;
    QString source;
    int after = 0;
    if (line0 >= 0) resolveModel(line0, &target, &source, &after);
    else target = defaultModel();
    for (const auto v : m_scene["items"].toArray())
    {
        const auto o = v.toObject();
        if (o["failed"].toBool() || o["reassigned"].toBool() || o["kind"].toString() == "failed") continue;
        if (o.contains("var") && o["var"].toString() != target["var"].toString()) return true;
    }
    return false;
}

void ScenePanel::createFromMenu(QString kind, QString name, QVector3D point, double scale, int line0, int generation)
{
    if (!m_support) return;
    const bool operation = kind == "operation";
    if (operation && generation >= 0 && generation != m_generation && !selectedModels().isEmpty())
    {
        // The script was run again since the menu was opened (the edit that makes the selected models ready to be
        // dragged): the line it was opened on is another model's now.  A menu on one of several selected models stands
        // for the selection, so it is the selection
        line0 = -1;
    }
    if (m_editPending && m_editClock.elapsed() < 20000)
    {
        // The script is being edited to get the selected models ready to be dragged: the scene is a run behind the text,
        // and the lines it names would be the wrong ones.  It is done when the run is: on the selected models, which is
        // what a menu on one of them stands for (and what the click that opened it had just selected)
        m_afterRun = [=] { createFromMenu(kind, name, point, scale, operation && !selectedModels().isEmpty() ? -1 : line0); };
        return;
    }
    const QString src = m_source ? m_source() : QString();
    QJsonObject target;
    QString body;
    int after = 0;
    QString other;
    if (kind == "operation")
    {
        // What it works on: the model that was right-clicked, or the selected ones, in the order they were selected
        const QList<Model> operands = operandsFor(line0);
        if (operands.isEmpty())
        {
            notify("There is no model to apply " + name + " to.");
            return;
        }
        const bool combining = combines(name);
        target = operands[0].item;
        body = operands[0].source;
        after = operands[0].after;
        if (combining && operands.size() >= 2)
        {
            // Several models are selected: the first is what the operation works on, the others are what it
            // combines it with (union: all of them together; difference: the others taken from the first).  It is
            // written under the model that is defined last, so that every name it uses exists by then
            QStringList others;
            Model last = operands[0];
            for (int i = 0; i < operands.size(); ++i)
            {
                if (i > 0) others << operands[i].source;
                if (operands[i].after > last.after) last = operands[i];
            }
            other = others.join(", ");
            target = last.item;
            after = last.after;
        }
        else if (combining)
        {
            // (one model: the operation takes the last other model of the script)
            for (const auto v : m_scene["items"].toArray())
            {
                const auto o = v.toObject();
                if (o["failed"].toBool() || o["reassigned"].toBool() || o["kind"].toString() == "failed") continue;
                if (o.contains("var") && o["var"].toString() != target["var"].toString()) other = o["var"].toString();
            }
            if (other.isEmpty())
            {
                notify(name + " needs a second model.");
                return;
            }
        }
    }
    QJsonObject request;
    request["kind"] = kind;
    request["name"] = name;
    request["x"] = point.x();
    request["y"] = point.y();
    request["z"] = point.z();
    request["scale"] = scale;
    request["body"] = body;
    request["other"] = other;
    QString error;
    const QString call = m_support("menu_call", QString::fromUtf8(QJsonDocument(request).toJson(QJsonDocument::Compact)),
                                   &error);
    if (call.isEmpty())
    {
        notify("Could not create " + name + (error.isEmpty() ? QString() : ": " + error));
        return;
    }
    const QString var = freeName(name);
    const QString statement = var + " = " + call;

    // The library's functions are in scope with `from fieldes import *`
    static const QRegularExpression starImport(R"(^\s*from\s+fieldes\s+import\s+\*)", QRegularExpression::MultilineOption);
    const QString star = starImport.match(src).hasMatch() ? QString() : QString("from fieldes import *\n\n");

    QList<TextEdit> edits;
    if (kind == "operation")
    {
        // Under the definition of the model, with a line showing the result
        const QString indent = indentOf(lineText(target["line"].toInt() - 1));
        const QString last = lineText(after);
        edits << TextEdit{after, int(last.size()), after, int(last.size()),
                          "\n" + indent + statement + "\n" + indent + var};
        if (!star.isEmpty()) edits << TextEdit{0, 0, 0, 0, star};
    }
    else
    {
        // At the end of the script
        const QStringList lines = src.split('\n');
        int last = int(lines.size()) - 1;
        while (last >= 0 && lines[last].trimmed().isEmpty()) --last;
        if (last < 0) edits << TextEdit{0, 0, lines.isEmpty() ? 0 : int(lines.size()) - 1,
                                        lines.isEmpty() ? 0 : int(lines.last().size()),
                                        star + statement + "\n" + var + "\n"};
        else
        {
            edits << TextEdit{last, int(lines[last].size()), last, int(lines[last].size()),
                              "\n\n" + statement + "\n" + var};
            if (!star.isEmpty()) edits << TextEdit{0, 0, 0, 0, star};
        }
    }
    m_selectNew = var;
    m_selectNewTries = 0;
    emit(editScript(edits, (kind == "primitive" ? "New " : "Add ") + name));
}

void ScenePanel::toggleIsolation()
{
    // What is selected now: the models that stay shown (all the selected ones), and their names
    QSet<QString> keep;
    QStringList names;
    QStringList selectedKeys;
    QList<QTreeWidgetItem*> rows = m_tree->selectedItems();
    if (rows.isEmpty() && m_tree->currentItem()) rows << m_tree->currentItem();
    for (auto row : rows)
    {
        const QString type = row->data(COL_NAME, ROLE_TYPE).toString();
        const auto it = row->data(COL_NAME, ROLE_ITEM).toJsonObject();
        selectedKeys << row->data(COL_NAME, ROLE_KEY).toString();
        if (type == "item" && !it["failed"].toBool() && it["kind"].toString() != "failed")
        {
            if (it.contains("var") || it["kind"].toString() == "display")
            {
                keep << keyOf(it);
                names << (it.contains("var") ? it["var"].toString() : it["label"].toString());
            }
            else if (it.contains("list_var"))
            {
                // (a list of the parts of an import: every part that is a variable)
                for (const auto pv : it["parts"].toArray())
                {
                    const auto p = pv.toObject();
                    if (p.contains("var") && !itemForVar(p["var"].toString()).isEmpty())
                        keep << keyOf(itemForVar(p["var"].toString()));
                }
                names << it["list_var"].toString();
            }
        }
        else if (type == "part")
        {
            const auto p = it["parts"].toArray()[row->data(COL_NAME, ROLE_PART).toInt()].toObject();
            if (p.contains("var") && !itemForVar(p["var"].toString()).isEmpty())
            {
                keep << keyOf(itemForVar(p["var"].toString()));
                names << p["var"].toString();
            }
            else if (it.contains("index") && it["index"].toInt() == row->data(COL_NAME, ROLE_PART).toInt())
            {
                keep << keyOf(it);
                names << (it.contains("var") ? it["var"].toString() : it["label"].toString());
            }
        }
    }
    selectedKeys.sort();
    const QString selectedKey = selectedKeys.join('|');
    const QString name = names.join(", ");

    // Isolated already, and the same model still selected (or nothing to isolate): everything shown before comes back
    const bool restore = m_isolated && (keep.isEmpty() || selectedKey == m_isolatedKey);
    if (!restore && keep.isEmpty())
    {
        notify("Select a model in the tree, then press I to show only it.");
        return;
    }
    QList<TextEdit> edits;
    const QSet<QString> shown = restore ? m_isolateRestore : keep;
    QSet<QString> visibleNow;
    for (const auto v : m_scene["items"].toArray())
    {
        const auto o = v.toObject();
        const QString kind = o["kind"].toString();
        if (o["failed"].toBool() || kind == "failed") continue;
        if (!(o.contains("var") || kind == "display")) continue;
        if (o["visible"].toBool()) visibleNow << keyOf(o);
        visibilityEdit(o, shown.contains(keyOf(o)), edits);
    }
    if (restore)
    {
        m_isolated = false;
        m_isolateRestore.clear();
        m_isolatedKey.clear();
        m_isolatedName.clear();
    }
    else
    {
        // (isolating another model while isolated keeps what was shown before the first isolation)
        if (!m_isolated) m_isolateRestore = visibleNow;
        m_isolated = true;
        m_isolatedKey = selectedKey;
        m_isolatedName = name;
    }
    if (!edits.isEmpty())
        emit(editScript(edits, restore ? QString("Show everything again") : "Isolate " + name));
    else
        rebuild();      // (nothing to change in the script: the note only)
}

}   // namespace FielDes
