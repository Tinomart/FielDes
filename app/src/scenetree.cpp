/*
FielDes: field-driven design
Derived from Studio, a simple GUI for the libfive CAD kernel

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <QElapsedTimer>
#include <cstdio>
#include <functional>
#include <memory>

#include <algorithm>

#include <QAbstractItemView>
#include <QCheckBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDirIterator>
#include <QSaveFile>
#include <QStandardPaths>
#include <QItemSelectionModel>
#include <QLocale>
#include <QKeyEvent>
#include <QLineEdit>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QDoubleValidator>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QWidgetAction>
#include <array>
#include <memory>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSet>
#include <QSettings>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

#include "fieldes/carddrag.hpp"
#include "fieldes/i18n.hpp"
#include "fieldes/scenetree.hpp"
#include "fieldes/color.hpp"
#include "fieldes/regionformat.hpp"
#include "fieldes/typeicons.hpp"

namespace FielDes {

namespace {

enum Role { ROLE_ITEM = Qt::UserRole, ROLE_TYPE, ROLE_PART, ROLE_KEY, ROLE_LINE, ROLE_RENAME };
// (COL_DOT is the last by number and the second on the screen: the orange dot of a model whose surfaces cannot be pulled)
enum Column { COL_NAME = 0, COL_EYE, COL_HANDLES, COL_LOCK, COL_CACHE, COL_ACTION, COL_RESET, COL_DELETE, COL_DOT };

const QColor kText(0xee, 0xe8, 0xd5);
const QColor kDim(0x93, 0xa1, 0xa1);
const QColor kCopy(0x82, 0xcc, 0x58);                  // (a drag that copies: it makes a reference)
const QString kGone = QStringLiteral("\x01gone");       // (a line of the script that is going to go)

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

// An import of a STEP file (the ones that have a cache to reset and parts to reimport; a mesh file has neither)
bool isStepImport(const QJsonObject& it)
{
    const QString p = it["path"].toString().toLower();
    return p.endsWith(".step") || p.endsWith(".stp");
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

// A model with no numbers to pull its surfaces by gets them when it is selected, if they are no more than this many; a bigger one
// (an imported part can have a thousand) is not made draggable by its surfaces: it is marked with an orange dot, and its gizmo moves it
const int kAutoExposeNumbers = 120;

// Why a model's surfaces cannot be pulled, for the orange dot and the line under the tree: "" when they can (it has the numbers, or it
// is given them when it is selected), or when there is nothing to pull (a point, a field, a material).  `mesh`: a tessellated part
QString noDragReason(const QJsonObject& m, bool mesh)
{
    if (mesh)
        return T("Its surfaces cannot be dragged: it is a mesh (tessellated), which has no faces to pull. Its gizmo moves it.");
    if (!m.contains("var") || m["failed"].toBool() || m["kind"].toString() == "display" || m["kind"].toString() == "failed") return QString();
    if (m["has_var"].toBool() || m.contains("exposed")) return QString();
    const int n = m["expose_count"].toInt();
    if (n <= 0) return QString();
    const int cap = m["expose_cap"].toInt();
    if (cap > 0 && n > cap)
        return T("Its surfaces cannot be dragged: %1 numbers place its faces, more than the %2 FielDes writes into a script. "
                       "Its gizmo moves it.").arg(n).arg(cap);
    if (n > kAutoExposeNumbers)
        return T("Its surfaces cannot be dragged: %1 numbers place its faces, and FielDes makes the surfaces of a model draggable "
                       "up to %2 numbers. Its gizmo moves it.").arg(n).arg(kAutoExposeNumbers);
    return QString();
}

const QIcon& dragDotIcon()
{
    static QIcon dot = makeIcon([](QPainter& p) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0xf0, 0x90, 0x28));
        p.drawEllipse(QPointF(8, 8), 3.4, 3.4);
    });
    return dot;
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
    if (mode == "never") tip = T("Gizmo: never shown");
    else if (mode == "always") tip = T("Gizmo: always shown");
    else tip = T("Gizmo: shown while the model is selected (click)");
    const QString surfaces = target["tessellated"].toBool()
        ? T("  ·  a mesh (tessellated): its faces cannot be dragged, the gizmo moves it")
        : T("  ·  its surfaces can always be dragged");
    row->setToolTip(COL_HANDLES, tip + surfaces + T("  ·  click: %1  ·  key E").arg(T(nextMode(target))));
}

// What a shape's lock button says and does
void setLockButton(QTreeWidgetItem* row, const QJsonObject& target)
{
    const bool locked = target.contains("locked");
    row->setIcon(COL_LOCK, lockIcon(locked));
    row->setToolTip(COL_LOCK, locked ? T("Locked: it cannot be dragged  ·  click to unlock  ·  key R")
                                     : T("Unlocked  ·  click to lock it (writes lock(x) under its definition)  ·  key R"));
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
    static QIcon partMeshI = makeIcon([](QPainter& p) {     // a triangle cut into triangles: a part that is a mesh (tessellated)
        const QColor amber(0xe0, 0xa0, 0x50);
        p.setPen(QPen(amber, 1.1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(QColor(0xe0, 0xa0, 0x50, 40));
        p.drawPolygon(QPolygonF({QPointF(8, 2.3), QPointF(13.9, 12.7), QPointF(2.1, 12.7)}));
        p.setBrush(Qt::NoBrush);
        p.drawPolygon(QPolygonF({QPointF(5.05, 7.5), QPointF(10.95, 7.5), QPointF(8, 12.7)}));
    });
    static QIcon partEmptyI = makeIcon([](QPainter& p) {    // a dashed outline: a body the file lists and gives no surface
        QColor c = kDim;
        c.setAlpha(150);
        QPen pen(c, 1.1, Qt::DashLine);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(3.2, 3.2, 9.6, 9.6), 1.5, 1.5);
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
    if (kind == "part_empty") return partEmptyI;
    if (failed) return failedI;
    if (kind == "import") return importI;
    if (kind == "part") return partI;
    if (kind == "part_mesh") return partMeshI;
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

QString md5Hex(const QString& text)
{
    return QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Md5).toHex());
}

// The item of the scene that is on `line` (1-based) and is called `var`, changed by `f` (nothing happens when there is none)
void patchAt(QJsonObject& scene, int line, const QString& var, const std::function<void(QJsonObject&)>& f)
{
    QJsonArray items = scene["items"].toArray();
    for (int i = 0; i < items.size(); ++i)
    {
        QJsonObject o = items[i].toObject();
        if (o["line"].toInt() != line || o["var"].toString() != var) continue;
        f(o);
        items[i] = o;
        scene["items"] = items;
        return;
    }
}

// What showing or hiding a model does to its item: its line that shows it (`x`) is, or is not, a `# hidden: x` line.  The lines are
// where the edit put them already; a model that had no such line gets one right under its definition
void patchVisible(QJsonObject& o, bool show)
{
    // (the number is copied out first: o["a"] = o["b"] inserts a key while it holds a reference into the same object)
    if (show)
    {
        if (o.contains("hidden_line"))
        {
            const int line = o["hidden_line"].toInt();
            o.remove("hidden_line");
            o["display_line"] = line;
        }
        else
        {
            const int line = o["end_line"].toInt() + 1;
            o["display_line"] = line;
        }
        o["visible"] = true;
    }
    else
    {
        if (o.contains("display_line"))
        {
            const int line = o["display_line"].toInt();
            o.remove("display_line");
            o["hidden_line"] = line;
        }
        o["visible"] = false;
    }
}

// A statement `name = fn(name)` that the edit put on `line` (1-based), as the scene describes the statements of that kind
QJsonObject statementInfo(int line, const QString& var, const QString& fn, const QString& argsText, const QString& indent)
{
    const QString text = var + " = " + fn + "(" + var + argsText + ")";
    const int col = indent.size() + var.size() + 3;                     // (where the call starts: after `name = `)
    QJsonObject c;
    c["line"] = line;
    c["end_line"] = line;
    c["call"] = QJsonArray{line, col, line, int(indent.size() + text.size())};
    c["text"] = text;
    return c;
}

// Whether `text` is the line the rows mean by `kind` for the model `var` (see ScenePanel::lineIs)
bool lineMatches(const QString& text, const QString& var, const QString& kind)
{
    const QString name = QRegularExpression::escape(var);
    QString pattern;
    if (kind == "display")
    {
        pattern = "^\\s*" + name + "\\s*(?:#.*)?$";
    }
    else if (kind == "hidden")
    {
        pattern = "^\\s*(?:#\\s*hidden:\\s?)+" + name + "\\s*(?:#.*)?$";
    }
    else if (kind == "definition")
    {
        // (a statement of the model that is none of the ones that edit it: its name stands on the left of the =)
        for (const char* fn : {"lock", "handles", "expose", "render_cache", "custom_resolution"})
        {
            if (lineMatches(text, var, QString::fromLatin1(fn))) return false;
        }
        // (it may be one of several targets: `a = b = f()`; what stands between its name and the next = is no call, no comparison)
        pattern = "^[^#]*?\\b" + name + "\\b[^#=()\\[\\]]*=(?!=)";
    }
    else
    {
        pattern = "^\\s*" + name + "\\s*=\\s*(?:[A-Za-z_][\\w.]*\\.)?" + kind + "\\(\\s*" + name + "\\b";
    }
    return QRegularExpression(pattern).match(text).hasMatch();
}

// The program and the library that make a scene and read it: a tree that was kept by other ones is not shown (it would be built by
// other rules than the ones the next run follows)
QString sceneCacheStamp()
{
    static QString stamp;
    if (!stamp.isEmpty()) return stamp;
    qint64 newest = QFileInfo(QCoreApplication::applicationFilePath()).lastModified().toMSecsSinceEpoch();
    QDirIterator it(QDir(QCoreApplication::applicationDirPath()).filePath("python/fieldes"), QStringList{"*.py"}, QDir::Files,
                    QDirIterator::Subdirectories);
    int count = 0;
    while (it.hasNext())
    {
        it.next();
        newest = std::max(newest, it.fileInfo().lastModified().toMSecsSinceEpoch());
        ++count;
    }
    stamp = "1-" + QString::number(count) + "-" + QString::number(newest, 36);
    return stamp;
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

// The icon of a row of the model tree: what kind of thing the statement makes (its colour and shape), a failed one a cross
QIcon itemIcon(const QJsonObject& it, bool failed)
{
    if (failed) return kindIcon(it["kind"].toString(), true);
    const QString kind = it["kind"].toString();
    if (kind == "import") return TypeIcons::icon("import");
    const QString type = it["type"].toString();
    if (!type.isEmpty()) return TypeIcons::icon(type, it.contains("block"));
    return kindIcon(kind, false);
}

// The icon of a placeholder row: a dashed box with three dots, in the colour of what is missing
QIcon holeIcon()
{
    static QIcon icon;
    if (!icon.isNull()) return icon;
    const qreal dpr = 2.0;
    QPixmap pm(int(16 * dpr), int(16 * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor c(0xf0, 0xa0, 0x40);
    p.setPen(QPen(c, 1.3, Qt::DashLine));
    p.drawRoundedRect(QRectF(1.5, 4.0, 13, 8), 2, 2);
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    for (int i = 0; i < 3; ++i) p.drawEllipse(QPointF(4.8 + i * 3.2, 8), 1.0, 1.0);
    p.end();
    icon = QIcon(pm);
    return icon;
}

// The icon of a shadow: the icon of the model it stands for, half transparent
QIcon fadedIcon(const QIcon& icon)
{
    QIcon out;
    for (const QSize& size : {QSize(16, 16), QSize(32, 32)})
    {
        const QPixmap src = icon.pixmap(size);
        if (src.isNull()) continue;
        QPixmap dst(src.size());
        dst.setDevicePixelRatio(src.devicePixelRatio());
        dst.fill(Qt::transparent);
        QPainter p(&dst);
        p.setOpacity(0.42);
        p.drawPixmap(0, 0, src);
        out.addPixmap(dst);
    }
    return out;
}

// The models a call is given as its own arguments (not those used deeper in it, nor those given by keyword)
QStringList directInputs(const QJsonObject& stmt)
{
    QStringList out;
    for (const auto iv : stmt["inputs"].toArray())
    {
        const auto in = iv.toObject();
        if (in["deep"].toBool() || in.contains("keyword")) continue;
        out << in["name"].toString();
    }
    return out;
}

// Whether every one of these models is a field (a field can take the place of a number written in a call)
bool allFields(const QList<QJsonObject>& models)
{
    if (models.isEmpty()) return false;
    for (const auto& m : models)
    {
        if (m["type"].toString() != "field") return false;
    }
    return true;
}

// Whether a drop of these models on `target` is for the REGION of a condition: a body, a surface or a field dropped on a support, a load or a
// thermal or flow condition becomes (part of) the place it acts -- `fixed(region)`.  A field is a region only where the condition has no
// plain number a field would take the place of (a temperature, a coefficient: those keep the field as their value)
bool regionDrop(const QJsonObject& target, const QList<QJsonObject>& models)
{
    if (target["type"].toString() != "conditions" || models.isEmpty() || target["failed"].toBool()) return false;
    bool fields = true;
    for (const auto& m : models)
    {
        const QString type = m["type"].toString();
        if (!(type == "solid" || type == "profile" || type == "surface" || type == "field")) return false;
        if (type != "field") fields = false;
    }
    if (target["region_args"].toArray().isEmpty()) return false;        // (the call has no region written: nothing to put the model beside)
    if (fields)
    {
        for (const auto nv : target["numbers"].toArray())
        {
            const auto n = nv.toObject();
            if (n["label"].toString().contains('[') || n["discrete"].toBool()) continue;
            return false;               // (a plain number: the field is its value, as it always was)
        }
    }
    return true;
}

// Whether every one of these models is a point (a point reads as its coordinates, so it can take the place of a position
// written as a tuple in a call: distance_to_point((0, 0, 0)) -> distance_to_point(anchor))
bool allPoints(const QList<QJsonObject>& models)
{
    if (models.isEmpty()) return false;
    for (const auto& m : models)
    {
        if (m["type"].toString() != "point") return false;
    }
    return true;
}

// The `# shadow: a, b` comment at the end of a statement: the models it holds a REFERENCE to without being their first user. A
// A Ctrl+drag of a model that nothing else uses writes it, so that the model stays where it is (a top-level row) and the statement
// shows a shadow of it; without it the statement would be the model's first user, and the model would move under it
const QRegularExpression& shadowComment()
{
    static const QRegularExpression re(R"(#\s*shadow:\s*([A-Za-z_]\w*(?:\s*,\s*[A-Za-z_]\w*)*))");
    return re;
}

// `name` put into the shadow comment of a line (a comment is added at its end when there is none)
QString markShadow(const QString& line, const QString& name)
{
    const auto m = shadowComment().match(line);
    if (!m.hasMatch()) return line + "  # shadow: " + name;
    QStringList names = m.captured(1).split(",");
    for (auto& n : names) n = n.trimmed();
    if (names.contains(name)) return line;
    return line.left(m.capturedEnd(1)) + ", " + name + line.mid(m.capturedEnd(1));
}

// `name` taken out of the shadow comment of a line (the comment goes with its last name)
QString unmarkShadow(const QString& line, const QString& name)
{
    const auto m = shadowComment().match(line);
    if (!m.hasMatch()) return line;
    QStringList names = m.captured(1).split(",");
    for (auto& n : names) n = n.trimmed();
    if (!names.contains(name)) return line;
    names.removeAll(name);
    QString out = line.left(m.capturedStart(0));
    if (names.isEmpty())
    {
        while (out.endsWith(' ')) out.chop(1);
        return out + line.mid(m.capturedEnd(0));
    }
    return out + "# shadow: " + names.join(", ") + line.mid(m.capturedEnd(0));
}

// What a model may be swapped for another of: a field for a field, a body (3D or 2D) for a body, any other kind for its own
QString typeClass(const QString& type)
{
    return type == "solid" || type == "profile" ? QString("shape") : type;
}

// The editor of a name in the tree: only the rows of a variable have one (a double click), and what is typed goes to the panel
// (which renames the variable in the script), not into the row's text
class RenameDelegate : public QStyledItemDelegate
{
public:
    std::function<void(const QModelIndex&, const QString&)> renamed;
    // (every key: the script is renamed as the name is typed)
    std::function<void(const QModelIndex&, const QString&)> typed;
    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem&, const QModelIndex& index) const override
    {
        if (index.data(ROLE_RENAME).toString().isEmpty()) return nullptr;
        auto e = new QLineEdit(parent);
        e->setText(index.data(ROLE_RENAME).toString());
        const QPersistentModelIndex where(index);
        connect(e, &QLineEdit::textEdited, e, [this, where](const QString& text) {
            if (typed && where.isValid()) typed(where, text);
        });
        e->setFrame(false);
        // (opaque, and as wide as the row: the name that is being replaced must not show through or stick out behind it)
        e->setAutoFillBackground(true);
        e->setStyleSheet("QLineEdit { background: #0b3240; color: #eee8d5; border: 1px solid #268bd2;"
                         " border-radius: 2px; selection-background-color: #268bd2; font-size: 8.5pt; }");
        e->selectAll();
        return e;
    }
    void setEditorData(QWidget*, const QModelIndex&) const override {}
    void setModelData(QWidget* editor, QAbstractItemModel*, const QModelIndex& index) const override
    {
        if (renamed) renamed(index, static_cast<QLineEdit*>(editor)->text());
    }
    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex&) const override
    {
        editor->setGeometry(option.rect.adjusted(18, 0, 0, 0));       // (after the icon; the icon stays)
        editor->raise();
    }
};

// The columns of the buttons have no editor
class NoEditDelegate : public QStyledItemDelegate
{
public:
    QWidget* createEditor(QWidget*, const QStyleOptionViewItem&, const QModelIndex&) const override { return nullptr; }
};

// The tree of the model tree: a model is dragged by its row with the mouse -- done here, from the mouse events, not with Qt's
// drag and drop (which is slow to start, runs a loop of its own that holds the application, and cannot be driven by a test):
// onto an operation (to be one of its inputs), or between two rows (to stand there in the script).  The tree draws where a drop
// would land and carries a small label with the dragged models.  What a drop does is the panel's: it is given the row it is
// over and where in it (above, on, below, in the empty space)
class SceneTree : public QTreeWidget
{
public:
    SceneTree()
    {
        setDragEnabled(false);
        setAcceptDrops(false);
        setExpandsOnDoubleClick(false);                 // (a double click renames a variable)
    }

    // a drag starts from this row: the panel says whether it can, and what the label shows
    std::function<bool(QTreeWidgetItem*, QString*, QIcon*, bool, bool)> beginDrag;      // (Ctrl was down; the row was selected)
    // can the dragged models be dropped here (bool: as a copy); when not, the reason goes in the string (it is shown at the mouse)
    std::function<bool(QTreeWidgetItem*, int, bool, QString*)> accepts;
    std::function<bool(QTreeWidgetItem*)> takesOn;           // can a drop be ON this row (else above or below it only)
    std::function<void(QTreeWidgetItem*, int, QPoint, bool)> dropped;
    std::function<void()> dragEnded;
    std::function<bool()> alwaysCopy;                        // is what is carried only ever copied (a locked model is read-only: a reference)
    std::function<void(bool)> held;                          // the left button is down on the tree / is up again

    bool dragging() const { return m_dragging; }
    bool isEditing() const { return state() == EditingState; }
    QTreeWidgetItem* rowOf(const QModelIndex& index) const { return itemFromIndex(index); }
    // (the rows are made again when the scene changes: a row that was pressed or is hovered is gone with them)
    void resetDrop() { m_ok = false; m_over = nullptr; m_pressRow = nullptr; m_toggleRow = nullptr; m_replay.reset(); viewport()->update(); }

    // Where in the tree a point is: above a row (its top third), on it (the middle), below it, or in the empty space
    int where(const QPoint& pos, QTreeWidgetItem** over) const
    {
        QTreeWidgetItem* row = itemAt(pos);
        *over = row;
        if (!row) return ScenePanel::DropViewport;
        const QRect r = visualItemRect(row);
        const double f = double(pos.y() - r.top()) / std::max(1, r.height());
        const QString rowType = row->data(COL_NAME, ROLE_TYPE).toString();
        if (rowType == "hole" || rowType == "bcgroup") return ScenePanel::DropOn;     // (a placeholder: a model goes on it, whole row)
        if (takesOn && takesOn(row)) return f < 0.28 ? ScenePanel::DropAbove : f > 0.72 ? ScenePanel::DropBelow : ScenePanel::DropOn;
        return f < 0.5 ? ScenePanel::DropAbove : ScenePanel::DropBelow;
    }

protected:
    void mousePressEvent(QMouseEvent* e) override
    {
        m_pressRow = nullptr;
        m_toggleRow = nullptr;
        m_replay.reset();
        QTreeWidgetItem* hit = itemAt(e->pos());
        const bool wasSelected = hit && hit->isSelected();
        // Ctrl on a row that is selected: the press would take it out of the selection (as a Ctrl+click does) before a drag
        // could begin, and there would be nothing to carry.  The selection stays as it is until the button comes up: a drag
        // carries what is selected, a click takes the row out of the selection
        if (e->button() == Qt::LeftButton && (e->modifiers() & Qt::ControlModifier) && !(e->modifiers() & Qt::ShiftModifier) &&
            hit && wasSelected && columnAt(e->pos().x()) == COL_NAME && (hit->flags() & Qt::ItemIsDragEnabled))
        {
            m_held = true;
            if (held) held(true);
            m_pressRow = hit;
            m_toggleRow = hit;
            m_pressPos = e->pos();
            m_pressCtrl = true;
            m_pressWasSelected = true;
            e->accept();
            return;
        }
        // A plain press on a row that is one of several selected: Qt would make it the only one selected at once (the tree does its own
        // drag, so Qt does not expect one), and the drag would carry that row alone.  The selection stays until the button comes up: a
        // drag carries all of it, and a click that was not a drag selects the row as it always did (Qt is given the press then)
        if (e->button() == Qt::LeftButton && !(e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier)) &&
            hit && wasSelected && columnAt(e->pos().x()) == COL_NAME && (hit->flags() & Qt::ItemIsDragEnabled) &&
            selectedItems().size() > 1)
        {
            m_held = true;
            if (held) held(true);
            m_pressRow = hit;
            m_replay.reset(new QMouseEvent(*e));
            m_pressPos = e->pos();
            m_pressCtrl = false;
            m_pressWasSelected = true;
            e->accept();
            return;
        }
        QTreeWidget::mousePressEvent(e);
        if (e->button() != Qt::LeftButton) return;
        m_held = true;
        if (held) held(true);
        QTreeWidgetItem* row = itemAt(e->pos());
        // (a press with Ctrl still toggles the selection, as a Ctrl+click does)
        if (row && columnAt(e->pos().x()) == COL_NAME && (row->flags() & Qt::ItemIsDragEnabled) &&
            !(e->modifiers() & Qt::ShiftModifier))
        {
            m_pressRow = row;
            m_pressPos = e->pos();
            m_pressCtrl = e->modifiers() & Qt::ControlModifier;
            m_pressWasSelected = wasSelected;
        }
    }
    void mouseMoveEvent(QMouseEvent* e) override
    {
        if (m_dragging)
        {
            m_copy = copyFor(e->modifiers());
            updateDrag(e->pos());
            e->accept();
            return;
        }
        // A press that was held back (on one of several selected rows) is not Qt's: a move it gets would start a rubber band from a
        // press it never saw, and the rubber band selects the row under the mouse alone -- the rest of the selection goes at the first
        // tremor of the hand.  Until the button comes up, or the drag begins, the moves are the tree's own
        const bool heldBack = bool(m_replay) || m_toggleRow;
        if (m_pressRow && (e->buttons() & Qt::LeftButton) &&
            (e->pos() - m_pressPos).manhattanLength() >= QApplication::startDragDistance())
        {
            QTreeWidgetItem* row = m_pressRow;
            m_pressRow = nullptr;
            m_toggleRow = nullptr;                      // (it is a drag, not a click: the selection stays)
            if (beginDrag && beginDrag(row, &m_ghost, &m_ghostIcon, m_pressCtrl, m_pressWasSelected))
            {
                m_replay.reset();                       // (a drag: the press that was held back is not given to Qt)
                m_dragging = true;
                m_copy = copyFor(e->modifiers());
                viewport()->setCursor(Qt::ClosedHandCursor);
                qApp->installEventFilter(this);
                m_timer = startTimer(30);
                updateDrag(e->pos());
                e->accept();
                return;
            }
        }
        if (heldBack && (e->buttons() & Qt::LeftButton))
        {
            e->accept();
            return;
        }
        QTreeWidget::mouseMoveEvent(e);
    }
    void mouseReleaseEvent(QMouseEvent* e) override
    {
        const bool left = e->button() == Qt::LeftButton;
        if (m_dragging && left)
        {
            QTreeWidgetItem* over = nullptr;
            const int at = where(e->pos(), &over);
            const bool copy = copyFor(e->modifiers());
            const bool ok = !accepts || accepts(over, at, copy, nullptr);
            endDrag();
            e->accept();
            if (ok && dropped) dropped(over, at, e->globalPos(), copy);
        }
        else if (m_replay && left)
        {
            // (a click on one of several selected rows that did not become a drag: Qt is given the press that was held back, so the row
            // becomes the selection now, as a click always did)
            const std::unique_ptr<QMouseEvent> press = std::move(m_replay);
            if (itemAt(e->pos()) == itemAt(press->pos())) QTreeWidget::mousePressEvent(press.get());
            QTreeWidget::mouseReleaseEvent(e);
        }
        else if (m_toggleRow && left)
        {
            // (a Ctrl+click on a selected row that did not become a drag: the row leaves the selection)
            QTreeWidgetItem* row = m_toggleRow;
            m_toggleRow = nullptr;
            if (row && itemAt(e->pos()) == row)
                selectionModel()->select(indexFromItem(row), QItemSelectionModel::Toggle | QItemSelectionModel::Rows);
            e->accept();
        }
        else
        {
            QTreeWidget::mouseReleaseEvent(e);
        }
        if (left)
        {
            m_pressRow = nullptr;
            m_toggleRow = nullptr;
            if (m_held)
            {
                m_held = false;
                if (held) held(false);
            }
        }
    }
    bool eventFilter(QObject* obj, QEvent* e) override
    {
        // (Escape gives the drag up; Ctrl makes it a copy, as long as it is held)
        if (m_dragging && e->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(e)->key() == Qt::Key_Escape)
        {
            endDrag();
            return true;
        }
        if (m_dragging && (e->type() == QEvent::KeyPress || e->type() == QEvent::KeyRelease) &&
            static_cast<QKeyEvent*>(e)->key() == Qt::Key_Control)
        {
            m_copy = e->type() == QEvent::KeyPress || (alwaysCopy && alwaysCopy());
            updateDrag(m_pos);
        }
        return QTreeWidget::eventFilter(obj, e);
    }
    void timerEvent(QTimerEvent* e) override
    {
        if (e->timerId() != m_timer) { QTreeWidget::timerEvent(e); return; }
        // (near the top or the bottom edge the rows scroll, and the drop is looked at again)
        if (auto bar = verticalScrollBar())
        {
            const int y = m_pos.y();
            const int before = bar->value();
            if (y < 16) bar->setValue(before - 8);
            else if (y > viewport()->height() - 16) bar->setValue(before + 8);
            if (bar->value() != before) updateDrag(m_pos);
        }
    }
    void paintEvent(QPaintEvent* e) override
    {
        QTreeWidget::paintEvent(e);
        if (!m_dragging) return;
        QPainter p(viewport());
        p.setRenderHint(QPainter::Antialiasing);
        const QColor blue = m_copy ? kCopy : QColor(0x26, 0x8b, 0xd2);
        if (m_ok)
        {
            if (m_over && m_at == ScenePanel::DropOn)
            {
                QRect r = visualItemRect(m_over);
                r = QRect(1, r.top(), viewport()->width() - 2, r.height());
                p.setPen(QPen(blue, 1.6));
                p.setBrush(QColor(blue.red(), blue.green(), blue.blue(), 70));
                p.drawRoundedRect(r, 3, 3);
            }
            else
            {
                int y = 0;
                if (m_over)
                {
                    const QRect r = visualItemRect(m_over);
                    y = m_at == ScenePanel::DropAbove ? r.top() : r.bottom();
                }
                else
                {
                    // (the empty space: after the last row that is shown)
                    for (QTreeWidgetItemIterator i(this); *i; ++i)
                    {
                        const QRect r = visualItemRect(*i);
                        if (r.height() > 0) y = std::max(y, r.bottom());
                    }
                }
                p.setPen(QPen(blue, 2.0, Qt::SolidLine, Qt::RoundCap));
                p.drawLine(QPointF(4, y), QPointF(viewport()->width() - 4, y));
                p.setBrush(blue);
                p.drawEllipse(QPointF(4, y), 2.6, 2.6);
            }
        }
        // the dragged models, carried by the mouse (dim when they cannot be dropped where the mouse is)
        QFont f = font();
        f.setBold(true);
        p.setFont(f);
        const QFontMetrics fm(f);
        const QString shown = (m_copy ? QString("+ ") : QString()) + m_ghost;
        const int w = fm.horizontalAdvance(shown) + 34;
        const QRect box(m_pos + QPoint(14, 8), QSize(std::min(w, viewport()->width() - 8), 22));
        QRect fit = box;
        if (fit.right() > viewport()->width() - 2) fit.moveRight(viewport()->width() - 2);
        p.setOpacity(m_ok ? 0.96 : 0.6);
        p.setPen(QPen(m_ok ? blue : QColor(0xdc, 0x32, 0x2f), 1.4));
        p.setBrush(QColor(0x0b, 0x32, 0x40));
        p.drawRoundedRect(fit, 5, 5);
        m_ghostIcon.paint(&p, QRect(fit.left() + 4, fit.top() + 3, 16, 16));
        p.setPen(QColor(0xee, 0xe8, 0xd5));
        p.drawText(QRect(fit.left() + 24, fit.top(), fit.width() - 28, fit.height()), Qt::AlignVCenter | Qt::AlignLeft,
                   fm.elidedText(shown, Qt::ElideRight, fit.width() - 30));
        if (!m_ok && !m_why.isEmpty())
        {
            // why the drop is refused, in words, where the mouse is (the forbidden cursor alone does not say)
            QFont small = font();
            small.setPointSizeF(std::max(7.5, small.pointSizeF() * 0.95));
            p.setFont(small);
            const int width = std::max(120, viewport()->width() - 16);
            const QRect text = QFontMetrics(small).boundingRect(QRect(0, 0, width - 12, 1000), Qt::TextWordWrap, m_why);
            QRect note(8, fit.bottom() + 5, width, text.height() + 10);
            if (note.bottom() > viewport()->height() - 2) note.moveBottom(fit.top() - 5);
            if (note.top() < 2) note.moveTop(2);
            p.setOpacity(0.97);
            p.setPen(QPen(QColor(0xdc, 0x32, 0x2f), 1.2));
            p.setBrush(QColor(0x0b, 0x32, 0x40));
            p.drawRoundedRect(note, 5, 5);
            p.setPen(QColor(0xee, 0xe8, 0xd5));
            p.drawText(note.adjusted(6, 5, -6, -5), Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, m_why);
        }
    }

private:
    // (Ctrl makes a drag a copy; so does what is carried when it can only be copied)
    bool copyFor(Qt::KeyboardModifiers mods) const { return (mods & Qt::ControlModifier) || (alwaysCopy && alwaysCopy()); }
    void updateDrag(const QPoint& pos)
    {
        m_pos = pos;
        QTreeWidgetItem* over = nullptr;
        const int at = where(pos, &over);
        m_why.clear();
        m_ok = !accepts || accepts(over, at, m_copy, &m_why);
        m_over = m_ok ? over : nullptr;
        m_at = at;
        viewport()->setCursor(m_ok ? Qt::ClosedHandCursor : Qt::ForbiddenCursor);
        viewport()->update();
    }
    void endDrag()
    {
        m_dragging = false;
        m_copy = false;
        qApp->removeEventFilter(this);
        if (m_timer) { killTimer(m_timer); m_timer = 0; }
        viewport()->unsetCursor();
        resetDrop();
        if (dragEnded) dragEnded();
    }

    QTreeWidgetItem* m_pressRow = nullptr;
    QTreeWidgetItem* m_toggleRow = nullptr;
    std::unique_ptr<QMouseEvent> m_replay;      // (a plain press on one of several selected rows, held back until the button comes up)
    QString m_why;                 // (why the models cannot be dropped where the mouse is)
    QPoint m_pressPos, m_pos;
    bool m_held = false, m_dragging = false;
    bool m_copy = false, m_pressCtrl = false, m_pressWasSelected = false;
    int m_timer = 0;
    QString m_ghost;
    QIcon m_ghostIcon;
    QTreeWidgetItem* m_over = nullptr;
    int m_at = ScenePanel::DropViewport;
    bool m_ok = false;
};

}   // anonymous namespace

////////////////////////////////////////////////////////////////////////////////

ScenePanel::ScenePanel(QWidget* parent)
    : QFrame(parent), m_tree(new SceneTree), m_header(new QToolButton),
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
        "QLabel { color: #93a1a1; font-size: 8pt; padding: 0px 6px 4px 6px; }"
        // The script has an error: the card says so (a red frame, the header, the note under the rows)
        "#ScenePanel[error=\"true\"] { border: 2px solid rgba(255, 56, 56, 245); }"
        "QToolButton#SceneHeader[error=\"true\"] { color: #ff4a4a; }"
        "QLabel[error=\"true\"] { color: #ff4a4a; font-size: 8.5pt; font-weight: bold; }"
        // What has to be done to run the script (a placeholder): orange, not red -- red is for what is wrong
        "QLabel[waiting=\"true\"] { color: #f5a623; font-size: 8.5pt; }"
        // The scroll bars of the card: a thin pill that floats over nothing -- no track, no arrows
        "QScrollBar:vertical { background: transparent; width: 11px; margin: 2px 0px; border: none; }"
        "QScrollBar::handle:vertical { background: rgba(147, 161, 161, 105); border-radius: 4px; min-height: 28px;"
        "  margin: 0px 2px; }"
        "QScrollBar::handle:vertical:hover, QScrollBar::handle:vertical:pressed { background: rgba(147, 161, 161, 190); }"
        "QScrollBar:horizontal { background: transparent; height: 11px; margin: 0px 2px; border: none; }"
        "QScrollBar::handle:horizontal { background: rgba(147, 161, 161, 105); border-radius: 4px; min-width: 28px;"
        "  margin: 2px 0px; }"
        "QScrollBar::handle:horizontal:hover, QScrollBar::handle:horizontal:pressed { background: rgba(147, 161, 161, 190); }"
        "QScrollBar::add-line, QScrollBar::sub-line { width: 0px; height: 0px; background: none; border: none; }"
        "QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }");

    m_header->setObjectName("SceneHeader");
    m_header->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_header->setText(headerText());
    m_header->setCursor(Qt::PointingHandCursor);
    m_header->setToolTip(T("Show / hide the model tree\nDrag a model onto an operation to make it one of its inputs,\n"
                         "or between two rows to move it in the script"));
    connect(m_header, &QToolButton::clicked, this, [this]{ setCollapsed(!m_collapsed); });

    m_tree->setColumnCount(9);
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
    // (the dot of a model whose surfaces cannot be pulled: narrow, and right after the name)
    m_tree->header()->setSectionResizeMode(COL_DOT, QHeaderView::Fixed);
    m_tree->header()->resizeSection(COL_DOT, 14);
    m_tree->header()->moveSection(m_tree->header()->visualIndex(COL_DOT), 1);
    m_tree->setIndentation(14);
    m_tree->setIconSize(QSize(16, 16));
    {   // Models are dragged by their rows with the mouse: onto an operation (an input of it), between rows (their place in the
        // script).  A double click on the name of a variable renames it
        auto tree = static_cast<SceneTree*>(m_tree);
        tree->beginDrag = [this](QTreeWidgetItem* row, QString* text, QIcon* icon, bool ctrl, bool wasSelected) {
            return beginDrag(row, text, icon, ctrl, wasSelected);
        };
        tree->accepts = [this](QTreeWidgetItem* over, int at, bool copy, QString* why) { return canDrop(over, at, copy, why); };
        tree->takesOn = [this](QTreeWidgetItem* row) {
            const QString type = row->data(COL_NAME, ROLE_TYPE).toString();
            if (type == "hole" || type == "dep" || type == "bcgroup") return true;     // (a model goes on a placeholder, or takes the place of a shadow; the
                                                                                        // conditions row of a simulation takes what the simulation takes)
            if (type != "item") return false;
            const auto it = row->data(COL_NAME, ROLE_ITEM).toJsonObject();
            const auto models = m_dragging ? m_dragModels : draggedModels();
            if (takesInput(it) || it["hole"].toBool() || (takesNumber(it) && allFields(models)) || (takesPoint(it) && allPoints(models))
                || regionDrop(it, models))
                return true;
            // (one of the models of a call can be replaced by a model of its kind dropped on it)
            if (!it.contains("owner") || models.isEmpty()) return false;
            for (const auto& m : models)
                if (!sameKind(m, it)) return false;
            return true;
        };
        tree->alwaysCopy = [this] {
            // A locked model is read-only: dragging it never moves it (its definition stays where it is), it makes a reference to it
            // wherever it is dropped.  (The shadow of a locked model is a reference already: it is moved like any other)
            if (!m_dragging || m_dragShadow) return false;
            for (const auto& m : m_dragModels)
                if (m.contains("locked")) return true;
            return false;
        };
        tree->dropped = [this](QTreeWidgetItem* over, int at, QPoint global, bool copy) { onDrop(over, at, global, copy); };
        tree->dragEnded = [this] { m_dragging = false; };            // (the models stay: the drop that follows needs them)
        tree->held = [this](bool down) { onTreeHeld(down); };

        auto renamer = new RenameDelegate;
        renamer->setParent(m_tree);
        // (the script is renamed as the name is typed; Enter or leaving the field keeps it, Escape gives the old name back, and a
        // name that is not one -- empty, taken, a keyword -- does not stay either: the script has the name it had before)
        renamer->typed = [this](const QModelIndex& index, const QString& text) {
            if (auto row = static_cast<SceneTree*>(m_tree)->rowOf(index)) liveRename(row, text);
        };
        renamer->renamed = [this](const QModelIndex& index, const QString& text) {
            auto row = static_cast<SceneTree*>(m_tree)->rowOf(index);
            if (!row) return;
            if (m_revertClock.isValid() && m_revertClock.elapsed() < 500) return;       // (Escape was pressed: see m_revertClock)
            // (what the script calls it now: after typing, the last name that could be one)
            const QString current = row->data(COL_NAME, ROLE_RENAME).toString();
            const bool had = m_renaming.active && m_renaming.current == current;
            const QString original = had ? m_renaming.original : current;
            if (!applyRename(current, text, false) && original != current) applyRename(current, original, true);
        };
        connect(renamer, &QAbstractItemDelegate::closeEditor, this, [this](QWidget*, QAbstractItemDelegate::EndEditHint hint) {
            endRenaming(hint != QAbstractItemDelegate::RevertModelCache);
        });
        m_tree->setItemDelegateForColumn(COL_NAME, renamer);
        for (int c : {int(COL_EYE), int(COL_HANDLES), int(COL_LOCK), int(COL_CACHE), int(COL_ACTION), int(COL_RESET), int(COL_DELETE), int(COL_DOT)})
        {
            auto none = new NoEditDelegate;
            none->setParent(m_tree);
            m_tree->setItemDelegateForColumn(c, none);
        }
        m_tree->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    }
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
        if (!m_rebuilding) m_expandState[i->data(COL_NAME, ROLE_KEY).toString()] = true;
        setCollapsed(m_collapsed);    // resize to the new row count
    });
    connect(m_tree, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem* i){
        if (!m_rebuilding) m_expandState[i->data(COL_NAME, ROLE_KEY).toString()] = false;
        setCollapsed(m_collapsed);
    });

    m_note->setWordWrap(true);
    m_note->setOpenExternalLinks(false);
    {
        QPalette amber = m_note->palette();
        amber.setColor(QPalette::Link, QColor(0xf0, 0xa0, 0x40));
        amber.setColor(QPalette::LinkVisited, QColor(0xf0, 0xa0, 0x40));
        m_note->setPalette(amber);
    }
    connect(m_note, &QLabel::linkActivated, this, [this](const QString& link) {
        // (a placeholder of the script: "hole:line:column", 0-based)
        const QStringList p = link.split(':');
        if (p.size() == 3 && p[0] == "hole") emit(placeholderRequested(p[1].toInt(), p[2].toInt()));
    });
    m_note->hide();

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 3, 6, 6);            // (the margin is where the card is resized from)
    layout->setSpacing(0);
    layout->addWidget(m_header);
    layout->addWidget(m_tree);
    layout->addWidget(m_note);

    setMinimumWidth(220);
    new CardController(this, "model-tree", m_header, QSize(220, 80));      // (dragged by its header, resized from its edges)
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
    // (Escape in a field of the render settings gives up what was typed)
    if (auto field = qobject_cast<QLineEdit*>(obj))
    {
        if (field->property("fn").isValid() && e->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(e)->key() == Qt::Key_Escape)
        {
            field->setText(field->property("shown").toString());
            field->clearFocus();
            return true;
        }
    }
    if (obj == parentWidget() && e->type() == QEvent::Resize)
    {
        setCollapsed(m_collapsed);
    }
    // (the modifiers of a click on the tree: itemClicked, which comes after the release, does not carry them)
    if (obj == m_tree->viewport() && (e->type() == QEvent::MouseButtonPress || e->type() == QEvent::MouseButtonRelease))
    {
        m_clickMods = static_cast<QMouseEvent*>(e)->modifiers();
    }
    if (obj == m_tree->viewport() && e->type() == QEvent::MouseButtonPress)
    {
        // (what was selected when the button went down: a click on a button of one of several selected rows is for all of them, but the
        // click makes that row the only one selected before itemClicked comes)
        m_pressedSelection.clear();
        for (auto row : m_tree->selectedItems()) m_pressedSelection << row->data(COL_NAME, ROLE_KEY).toString();
    }
    return QFrame::eventFilter(obj, e);
}

void ScenePanel::setCollapsed(bool c)
{
    m_collapsed = c;
    m_tree->setVisible(!c);
    m_note->setVisible(!c && !m_note->text().isEmpty());
    m_header->setText(headerText());
    if (CardController::sized(this))
    {
        // (a card the user sized keeps its size: only collapsing to the header and opening again change it)
        if (property("collapsedBefore").toBool() != c)
        {
            setProperty("collapsedBefore", c);
            CardController::collapse(this, c, m_header->sizeHint().height() + 14);
        }
        return;
    }
    adjustSize();
    resize(360, height());                  // (wide enough for "boundary conditions" under a simulation; the user can make it wider or narrower)
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
    // (a text that does not parse -- a line that is being typed -- gives no tree: the rows stay as they were, and the editor says what
    // is wrong)
    m_textUnreadable = doc.object().contains("syntax_error");
    if (m_textUnreadable) return;
    if (staleWhileRenaming(doc.object())) return;
    // The tree that an edit of the rows made at once (a prediction) is not given back to the text before it by a run that was begun
    // before the edit and could not be stopped in time: the scene of the run that fits the script is on its way (every edit has one)
    if (m_predicted && m_source)
    {
        const QString md5 = doc.object()["source_md5"].toString();
        if (!md5.isEmpty() && md5 != md5Hex(m_source())) return;
    }
    // (the models of the scene that was shown: the ones this scene has that those did not are new -- the render region grows to hold them)
    // (the rows come from the text, before any run: a model is new to the render region when it has an extent now that it had not)
    QSet<QString> modelsBefore;
    for (const auto v : m_scene["items"].toArray())
        if (v.toObject()["bounds"].toArray().size() == 2) modelsBefore << keyOf(v.toObject());
    modelsBefore.subtract(m_unfitted);          // (a model that waited for the region to be known is new to it still)
    m_scene = doc.object();
    m_sceneClock.start();
    m_predicted = false;
    m_liveSettingStale = false;         // (the spans of the scene are those of a run again)
    // The tree of this text is kept for the next time it is opened (a scene of a run of the text that is in the editor now)
    if (m_source && m_scene["run_done"].toBool() && !m_scene["errored"].toBool() && m_scene["source_md5"].toString() == md5Hex(m_source()))
        saveSceneCache(json, m_scene["source_md5"].toString());
    TypeIcons::setKinds(m_scene["kinds"].toObject());
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
    // (a model locked or unlocked while it is in a selection of several: what the selection can do changes, said without a popup)
    noteLockedInMulti(false);
    // (the shapes of this run are in the viewport already, and a changed shape is a new one: the selected models are
    // lit up again, from the lines they are on now)
    emit(highlightLines(selectedLines()));
    updateMultiNote();
    updateProvisional();
    updateFieldView();
    // A click that was refused because the rows were of an older text (refuseOutOfDate) says so again now that the rows are the script's
    if (!m_refusedNote.isEmpty() && m_source && m_scene["source_md5"].toString() == md5Hex(m_source()))
    {
        const QString note = m_refusedNote;
        m_refusedNote.clear();
        notify(note);
    }
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

    // What is selected is made ready to be dragged after every run, not only after the runs that something asked it of (a model
    // shown by the isolate key, a script that was edited by hand): it writes nothing for a model that is ready
    if (!m_prepareAgain && !m_editPending && !selectedModels().isEmpty() && !m_treeHeld) m_prepareTimer.start();

    // A model that came and reaches out of the render region: the region grows (not for the first scene of a file, nor for a scene of
    // a run that is not of the text that is in the editor)
    if (!modelsBefore.isEmpty()) QTimer::singleShot(0, this, [this, modelsBefore] { growRegion(modelsBefore); });

    // A click made while the script was running again waits for this scene (one at a time: see runDeferred)
    runDeferred();
}

void ScenePanel::growRegion(const QSet<QString>& before)
{
    if (m_predicted || !m_source || m_scene["errored"].toBool() || m_scene["source_md5"].toString() != md5Hex(m_source())) return;
    auto sane = [](double v) { return std::isfinite(v) && std::abs(v) < 1e6; };
    // The box of the models that are new (shown, drawn, with an extent)
    double nlo[3], nhi[3];
    bool any = false;
    for (const auto v : m_scene["items"].toArray())
    {
        const auto it = v.toObject();
        if (before.contains(keyOf(it)) || !it["visible"].toBool() || it["failed"].toBool() || it["kind"].toString() == "failed") continue;
        if (it.contains("displayable") && !it["displayable"].toBool()) continue;
        const auto b = it["bounds"].toArray();
        if (b.size() != 2) continue;
        double l[3], h[3];
        bool ok = true;
        for (int k = 0; k < 3; ++k)
        {
            l[k] = b[0].toArray()[k].toDouble();
            h[k] = b[1].toArray()[k].toDouble();
            ok = ok && sane(l[k]) && sane(h[k]) && h[k] >= l[k];
        }
        if (!ok) continue;
        for (int k = 0; k < 3; ++k)
        {
            nlo[k] = any ? std::min(nlo[k], l[k]) : l[k];
            nhi[k] = any ? std::max(nhi[k], h[k]) : h[k];
        }
        any = true;
    }
    if (!any) return;
    // The region now: the script's, else the default one
    const auto settings = m_scene["settings"].toObject();
    double lo[3] = {-10, -10, -10}, hi[3] = {10, 10, 10};
    const auto set = settings["bounds"].toObject()["value"].toArray();
    if (set.size() == 2)
    {
        for (int k = 0; k < 3; ++k)
        {
            lo[k] = set[0].toArray()[k].toDouble();
            hi[k] = set[1].toArray()[k].toDouble();
        }
    }
    else if (settings.contains("set_bounds"))
    {
        // The script sets a region, but the run has not got to that line yet (the scene of a run that is still on its way): what the region
        // is is not known, and the default one is not what is in the script -- nothing is written on a guess.  The models try again
        // when the run is done
        for (const auto v : m_scene["items"].toArray())
            if (v.toObject()["bounds"].toArray().size() == 2 && !before.contains(keyOf(v.toObject()))) m_unfitted << keyOf(v.toObject());
        return;
    }
    m_unfitted.clear();
    bool outside = false;
    for (int k = 0; k < 3; ++k) outside = outside || nlo[k] < lo[k] - 1e-6 || nhi[k] > hi[k] + 1e-6;
    if (!outside) return;
    // The region grown on the sides that are reached: whole numbers, and room beyond the model (a twentieth of the whole, at least 1 mm)
    QStringList text;
    for (int pass = 0; pass < 2; ++pass)
    {
        for (int k = 0; k < 3; ++k)
        {
            const double room = std::max(1.0, 0.05 * (std::max(nhi[k], hi[k]) - std::min(nlo[k], lo[k])));
            text << (pass == 0 ? regionNumber(nlo[k] < lo[k] ? nlo[k] - room : lo[k], -1)
                               : regionNumber(nhi[k] > hi[k] ? nhi[k] + room : hi[k], 1));
        }
    }
    const QString call = QString("view.set_bounds([%1, %2, %3], [%4, %5, %6])").arg(text[0], text[1], text[2], text[3], text[4], text[5]);
    if (!settings.contains("set_bounds") || !settings["set_bounds"].toObject().contains("span"))
    {
        insertSettingLine(call, "set_bounds", false);
        return;
    }
    const auto span = settings["set_bounds"].toObject()["span"].toArray();
    const QString current = spanText(span);
    // (the region the script gets from the models, view.set_bounds(*roi(parts)), stays that: the new models are given to roi as a box)
    static const QRegularExpression fromRoi(R"(^view\.set_bounds\(\s*\*\s*roi\((.*)\)\s*\)$)", QRegularExpression::DotMatchesEverythingOption);
    const auto m = fromRoi.match(current);
    QString replacement = call;
    if (m.hasMatch())
    {
        QStringList a, b;
        for (int k = 0; k < 3; ++k)
        {
            a << regionNumber(nlo[k], -1);
            b << regionNumber(nhi[k], 1);
        }
        replacement = "view.set_bounds(*roi(" + m.captured(1) + ", ((" + a.join(", ") + "), (" + b.join(", ") + "))))";
    }
    const QList<TextEdit> edits{TextEdit{span[0].toInt() - 1, span[1].toInt(), span[2].toInt() - 1, span[3].toInt(), replacement}};
    emit(editScript(edits, "Grow the region"));
}

void ScenePanel::clearScene()
{
    emit(provisionalGizmo(false, QVector3D(), QList<int>()));
    m_scene = QJsonObject();
    m_predicted = false;
    m_deferred.clear();
    m_dragModels.clear();
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

QWidget* ScenePanel::typingWidget() const
{
    if (auto w = QApplication::focusWidget()) return w;
    // (no active window: what a row's name is being typed in is its editor, and what a render setting is typed in is the window's
    // widget that would have the keyboard)
    if (static_cast<SceneTree*>(m_tree)->isEditing())
        if (auto e = m_tree->indexWidget(m_tree->currentIndex())) return e;
    if (auto w = window() ? window()->focusWidget() : nullptr)
    {
        if (isAncestorOf(w)) return w;
    }
    return nullptr;
}

namespace {

// FIELDES_TIMING: how long the model tree takes to be made (what the user sees as "the tree reloads")
struct TreeTimer
{
    QElapsedTimer clock;
    const char* what;
    QTreeWidget* tree;
    explicit TreeTimer(const char* w, QTreeWidget* t) : what(w), tree(t) { clock.start(); }
    ~TreeTimer()
    {
        const char* log = std::getenv("FIELDES_TREE_LOG");       // (a file that the tests read: the stderr of a window is not caught)
        if (!std::getenv("FIELDES_TIMING") && !log) return;
        int rows = 0;
        for (QTreeWidgetItemIterator i(tree); *i; ++i) ++rows;
        if (std::getenv("FIELDES_TIMING"))
            std::cerr << "[tree] " << what << ": " << rows << " rows, " << clock.elapsed() << " ms" << std::endl;
        if (log)
        {
            if (FILE* f = std::fopen(log, "a"))
            {
                std::fprintf(f, "[tree] %s: %d rows, %lld ms\n", what, rows, static_cast<long long>(clock.elapsed()));
                std::fclose(f);
            }
        }
    }
};

}   // anonymous namespace

void ScenePanel::rebuild()
{
    TreeTimer timer("rebuild", m_tree);
    m_rebuildPending = false;
    // (for the tests: where the time of a rebuild goes, phase by phase, into FIELDES_TREE_LOG)
    QElapsedTimer phaseClock;
    phaseClock.start();
    const char* phaseLog = std::getenv("FIELDES_TREE_LOG");
    auto phase = [&](const char* name) {
        if (phaseLog)
        {
            if (FILE* f = std::fopen(phaseLog, "a"))
            {
                std::fprintf(f, "[tree]     phase %-12s %lld ms\n", name, static_cast<long long>(phaseClock.elapsed()));
                std::fclose(f);
            }
        }
        phaseClock.restart();
    };
    const int scroll = m_tree->verticalScrollBar() ? m_tree->verticalScrollBar()->value() : 0;
    // (clearing and filling the tree changes its selection; the order the rows were selected in is kept meanwhile)
    m_rebuilding = true;
    static_cast<SceneTree*>(m_tree)->resetDrop();
    // (a field of the render settings that is being typed in keeps what is typed, and the keyboard, through a rebuild)
    QString typedFn, typedText;
    int typedIndex = -1, typedCursor = 0;
    if (auto typing = qobject_cast<QLineEdit*>(typingWidget()))
    {
        if (typing->property("fn").isValid())
        {
            typedFn = typing->property("fn").toString();
            typedText = typing->text();
            typedCursor = typing->cursorPosition();
            typedIndex = m_settingEditors.value(typedFn).indexOf(QPointer<QLineEdit>(typing));
        }
    }
    // (so does a name that is being typed in a row: its editor is opened again on the same row, with what was typed -- the
    // selection of a row writes the lines that make it ready to be dragged, and the tree is built again when they have run)
    QString nameKey, nameText;
    int nameCursor = 0, nameSelStart = -1, nameSelLength = 0;
    if (static_cast<SceneTree*>(m_tree)->isEditing() && m_tree->currentItem())
    {
        if (auto typing = qobject_cast<QLineEdit*>(typingWidget()))
        {
            if (!typing->property("fn").isValid())
            {
                nameKey = m_tree->currentItem()->data(COL_NAME, ROLE_KEY).toString();
                nameText = typing->text();
                nameCursor = typing->cursorPosition();
                nameSelStart = typing->selectionStart();
                nameSelLength = typing->selectedText().size();
            }
        }
    }
    m_settingEditors.clear();
    phase("setup");
    m_tree->clear();
    phase("clear");
    QTreeWidgetItem* toSelect = nullptr;
    QList<QTreeWidgetItem*> restore;

    // A model is dragged by its row when it is a variable that is defined once (a model that is changed again by a later
    // statement has the statements of its own that cannot be moved apart from it)
    QHash<QString, int> definitions;
    for (const auto v : m_scene["items"].toArray())
    {
        const auto o = v.toObject();
        if (o.contains("var")) definitions[o["var"].toString()]++;
    }

    // The rows of the models are made apart from the tree and put into it once, when they are nested: a row that is in the tree makes
    // the view lay out every row that is there at each change of it (an icon, a tooltip), which made the tree of 400 models take six
    // seconds to build (14 ms for each row, 94 % of it the making of the row); made apart, the same rows take a few milliseconds
    bool detachTop = false;
    QList<QTreeWidgetItem*> topRows;
    QSet<QTreeWidgetItem*> nested;
    auto makeRow = [&](QTreeWidgetItem* parent, const QString& text, const QIcon& icon,
                       const QString& type, const QJsonObject& it, const QString& key) {
        auto row = parent ? new QTreeWidgetItem(parent) : (detachTop ? new QTreeWidgetItem() : new QTreeWidgetItem(m_tree));
        if (!parent && detachTop) topRows << row;
        row->setText(COL_NAME, text);
        row->setIcon(COL_NAME, icon);
        row->setData(COL_NAME, ROLE_ITEM, it);
        row->setData(COL_NAME, ROLE_TYPE, type);
        row->setData(COL_NAME, ROLE_KEY, key);
        const bool draggable = type == "item" && it.contains("var") && !it["failed"].toBool() &&
                               it["kind"].toString() != "failed" && definitions.value(it["var"].toString()) == 1;
        row->setFlags((row->flags() | Qt::ItemIsDragEnabled) & ~Qt::ItemIsDropEnabled);
        if (!draggable) row->setFlags(row->flags() & ~Qt::ItemIsDragEnabled);
        // (a double click on the name of a variable renames it)
        const bool renameable = type == "item" && it.contains("var") && !it["var"].toString().isEmpty();
        row->setData(COL_NAME, ROLE_RENAME, renameable ? it["var"].toString() : QString());
        if (renameable) row->setFlags(row->flags() | Qt::ItemIsEditable);
        if (key == m_selectedKey) toSelect = row;
        if (m_selectOrder.contains(key)) restore << row;
        return row;
    };

    const auto items = m_scene["items"].toArray();

    // The models an operation takes are its children: the rows of the models that a statement is the first to use (the
    // scene says which: `owner`) go under that statement's row.  The others it uses are listed, dim, as "uses x"
    QHash<QString, QSet<QString>> owned;            // (the row key of a statement -> the variables that are its children)
    for (const auto v : items)
    {
        const auto o = v.toObject();
        if (o.contains("owner") && o.contains("var")) owned[o["owner"].toString()].insert(o["var"].toString());
    }
    QHash<QString, QTreeWidgetItem*> rowOfKey;
    QHash<QTreeWidgetItem*, bool> expandedByDefault;
    QList<QPair<QTreeWidgetItem*, QString>> conditionGroups;      // (the "boundary conditions" rows, and the keys they are remembered by)

    // Render settings: the numbers are fields, typed in here (the call in the script is rewritten, or added)
    const auto settings = m_scene["settings"].toObject();
    if (!settings.isEmpty())
    {
        auto row = makeRow(nullptr, T("Render settings"), kindIcon("settings", false),
                           "settings", QJsonObject(), "settings");
        row->setForeground(COL_NAME, kDim);
        // The numbers are the code's: a click on a row goes to its line, and they are changed there, in the code editor
        auto settingRow = [&](const QString& fn, const QString& label, const QString& value, const QString& tip) {
            const auto s = settings[fn].toObject();
            const bool set = s.contains("line");
            auto c = makeRow(row, label + "   " + value, QIcon(), "setting", s, "setting:" + fn);
            c->setData(COL_NAME, ROLE_LINE, set ? s["line"].toInt() - 1 : -1);
            c->setForeground(COL_NAME, set ? QColor(0xee, 0xe8, 0xd5) : kDim);
            c->setToolTip(COL_NAME, tip + "\n" + (set ? s["text"].toString() + "\n" + T("Click: go to the line. Change the numbers in the code editor.")
                                                    : T("Not set in the script (the default is shown): write the line in the code editor.")));
        };
        auto number = [](const QJsonValue& v) { return QString::number(v.toDouble(), 'g', 8); };
        const auto bounds = settings["bounds"].toObject()["value"].toArray();
        QStringList lo, hi;
        // (whole numbers, rounded so that the region still covers what it covered; two decimals only for what is under 1: see regionNumber)
        for (int k = 0; k < 3; ++k)
        {
            lo << (bounds.size() == 2 ? regionNumber(bounds[0].toArray()[k].toDouble(), -1) : QString("-10"));      // (not set: the default region)
            hi << (bounds.size() == 2 ? regionNumber(bounds[1].toArray()[k].toDouble(), 1) : QString("10"));
        }
        settingRow("set_bounds", T("Region"), "(" + lo.join(", ") + ") " + QString(QChar(0x2192)) + " (" + hi.join(", ") + ")",
                   T("The region the viewport meshes (mm): nothing outside it is drawn"));
        const auto res = settings["resolution"].toObject(), qual = settings["quality"].toObject();
        settingRow("set_resolution", T("Resolution"), res.contains("value") ? number(res["value"]) : QString("10"),
                   T("Samples per mm: more is a finer picture and a slower one"));
        settingRow("set_quality", T("Quality"), qual.contains("value") ? number(qual["value"]) : QString("8"),
                   T("How carefully the surface is followed (8 is the default)"));
        row->setExpanded(m_expandState.value("settings", false));
    }
    const auto region = settings["bounds"].toObject()["value"].toArray();
    phase("settings");
    detachTop = true;           // (the settings row is in the tree: its fields are widgets; the rows of the models come apart)

    // Which imports list their parts as rows (the part variables are shown
    // there rather than again at the top level)
    QSet<int> partLists;
    QList<QPair<QTreeWidgetItem*, QJsonObject>> partResolutions;      // (part rows that have a resolution of their own: see below)
    QHash<QTreeWidgetItem*, QString> partNotes;                       // (and what is to be said in that row: a mesh has no resolution to change)
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

        auto row = makeRow(nullptr, text, itemIcon(it, failed), "item", it, key);
        rowOfKey[key] = row;
        QString tip = it["text"].toString();
        if (it.contains("path")) tip = it["path"].toString() + "\n" + tip;
        {   // (what kind of thing it is, first)
            const QString type = it["type"].toString(kind == "import" ? "import" : QString());
            if (!type.isEmpty() && !failed)
            {
                QString head = TypeIcons::label(type);
                // (a primitive stands on its own; an operation is made of other models, its children)
                if (it["role"].toString() == "operation") head += T("  ·  operation");
                else if (it["role"].toString() == "primitive") head += T("  ·  primitive");
                if (it["has_var"].toBool() || it.contains("exposed")) head += T("  ·  can be dragged");
                if (it.contains("block")) head += T("  ·  block %1").arg(it["block"].toString());
                tip = head + "\n" + tip;
            }
        }
        if (it.contains("unit_mm") && it["unit_mm"].toDouble() != 1.0 &&
            it["units"].toString() != "file")
        {
            tip += T("\nUnits converted (%1 mm per unit)").arg(it["unit_mm"].toDouble());
        }
        if (it.contains("bounds")) tip += "\n" + T("Size: %1").arg(sizeText(it["bounds"].toArray()));
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
            row->setToolTip(COL_NAME, row->toolTip(COL_NAME) + "\n" + T("Outside the render region"));
        }

        // (a material, a lattice cell ...: nothing to draw, so no eye and no render cache)
        const bool displayable = !(it.contains("displayable") && !it["displayable"].toBool());
        // Visibility eye
        const bool hasEye = !failed && displayable && (it.contains("var") || kind == "display");
        if (hasEye)
        {
            const bool visible = it["visible"].toBool();
            row->setIcon(COL_EYE, eyeIcon(visible));
            row->setData(COL_EYE, Qt::UserRole, visible);
            row->setToolTip(COL_EYE, visible ? T("Hide") : T("Show"));
        }
        // (the assembly -- the import row the parts are nested under -- has no eye: it is the file, not something drawn; each of its
        // parts has its own eye, in its own row below)

        // A model whose surfaces cannot be pulled: an orange dot, and why
        if (!failed && kind != "import")
        {
            const QString why = noDragReason(it, false);
            if (!why.isEmpty())
            {
                row->setIcon(COL_DOT, dragDotIcon());
                row->setToolTip(COL_DOT, why);
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
        if (!failed && displayable && ((it.contains("var") && kind != "display" && !it["reassigned"].toBool()) ||
                        (kind == "display" && it["can_name"].toBool() && it["visible"].toBool())))
        {
            setCacheButton(row, it);
        }

        // Reset: an import back to the file (its cache deleted, its handle edits removed)
        if (kind == "import" && !failed && it.contains("path") && isStepImport(it))
        {
            row->setIcon(COL_RESET, resetIcon());
            row->setToolTip(COL_RESET, T("Reset: clear the cache and handle edits"));
        }

        // Action: reimport for imports
        if (kind == "import")
        {
            row->setIcon(COL_ACTION, reimportIcon());
            row->setToolTip(COL_ACTION, T("Reimport the file"));
        }

        // Delete: its statements go from the script
        row->setIcon(COL_DELETE, deleteIcon());
        row->setToolTip(COL_DELETE, T("Delete from the script"));

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
                QString t = p.contains("var") ? p["var"].toString() : T("part %1").arg(k);
                if (!shortName.isEmpty()) t += QString("  ·  ") + shortName;
                else if (p.contains("var")) t += T("   (part %1)").arg(k);
                if (k == current) t += "  *";
                // (a cube for a part that is rebuilt from its faces -- its faces can be dragged -- and a triangle cut into triangles for
                // one that is a mesh, tessellated: only its gizmo moves it)
                const bool tessPart = ok && p["method"].toString() == "tessellate";
                // (a body that has no surface in the file: nothing to import, which is not a failure of the import -- it is shown dimmed,
                // not struck through in red)
                const bool emptyPart = !ok && p["empty"].toBool();
                auto c = makeRow(row, t, kindIcon(emptyPart ? "part_empty" : tessPart ? "part_mesh" : "part", !ok && !emptyPart), "part", it,
                                 key + "#" + QString::number(k));
                c->setData(COL_NAME, ROLE_PART, k);
                QString ptip = T("Size: %1").arg(sizeText(p["bounds"].toArray()));
                ptip = T("Part %1").arg(k) + (path.isEmpty() ? "" : ": " + path) + "\n" + ptip;
                // How it was imported (import_model chooses per part: see fieldes.stdlib.importing)
                if (p.contains("method"))
                {
                    const bool tess = p["method"].toString() == "tessellate";
                    const int share = int(std::lround(100.0 * p["free_form"].toDouble()));
                    if (!tess)
                    {
                        ptip += T("\nReconstructed (a cube): %1 % of its surface is free-form").arg(share);
                        if (p.contains("fit"))
                            ptip += T(", its fitted faces off by %1 % of their size at most").arg(100.0 * p["fit"].toDouble(), 0, 'g', 2);
                        ptip += T("\nIts gizmo moves it, and its faces can be dragged");
                    }
                    else
                    {
                        if (p["surface"].toBool())
                            ptip += T("\nTessellated (a mesh): a surface body (a sheet of faces: no inside to reconstruct)");
                        else if (p.contains("poor_fit"))
                            ptip += T("\nTessellated (a mesh): its free-form faces were fitted %1 % of their size off, which shows; "
                                            "its exact surface is used instead").arg(100.0 * p["poor_fit"].toDouble(), 0, 'g', 2);
                        else
                            ptip += T("\nTessellated (a mesh): %1 % of its surface is free-form").arg(share);
                        ptip += T("\nIts gizmo moves it; its faces cannot be dragged (a mesh has none)");
                    }
                }
                const QJsonObject bound = p.contains("var") ? itemForVar(p["var"].toString())
                                                            : QJsonObject();
                // (a resolution of its own that the script gives the part: its row is made under this one, once the rows are in the tree)
                if (!bound.isEmpty() && bound.contains("custom_resolution") && !bound["failed"].toBool())
                {
                    partResolutions << qMakePair(c, bound);
                    if (tessPart) partNotes[c] = T("a mesh");
                }
                // A part that is in the script is a model like any other: it is dragged (onto an operation, between models)
                // from its row here as from its own
                if (ok && !bound.isEmpty() && !bound["failed"].toBool() && bound["kind"].toString() != "failed" &&
                    definitions.value(bound["var"].toString()) == 1)
                {
                    c->setFlags(c->flags() | Qt::ItemIsDragEnabled);
                }
                // Handles work on a part that is in the script (as a variable)
                const QJsonObject target = handlesTarget(it, k);
                // (a mesh has no faces to pull, and a part with more numbers than FielDes makes draggable cannot be pulled either:
                // the dot says so from the start, which parts have handles and which have not)
                if (ok)
                {
                    const QString why = tessPart ? noDragReason(QJsonObject(), true) : noDragReason(target, false);
                    if (!why.isEmpty())
                    {
                        c->setIcon(COL_DOT, dragDotIcon());
                        c->setToolTip(COL_DOT, why);
                    }
                }
                if (ok && !target.isEmpty())
                {
                    QJsonObject modeTarget = target;
                    if (tessPart) modeTarget["tessellated"] = true;
                    setModeButton(c, modeTarget);
                    setLockButton(c, target);
                    setCacheButton(c, target);
                }
                // Reimport: back to what the file says (its handle edits gone), the file read again
                if (ok && isStepImport(it))
                {
                    c->setIcon(COL_RESET, reimportIcon());
                    c->setToolTip(COL_RESET, T("Reimport this part"));
                }
                // Delete: the part's variable (a part that is not in the script has nothing to delete)
                if (!bound.isEmpty())
                {
                    c->setIcon(COL_DELETE, deleteIcon());
                    c->setToolTip(COL_DELETE, T("Delete this part from the script"));
                }
                const bool partVisible = !bound.isEmpty() ? bound["visible"].toBool()
                                         : (k == current && it["visible"].toBool());
                if (ok && !partVisible) c->setForeground(COL_NAME, kDim);
                if (ok)
                {
                    // (the eye of a part: hides or shows it when it is a model of the script; for one that is not yet, it adds it)
                    c->setIcon(COL_EYE, eyeIcon(partVisible));
                    c->setData(COL_EYE, Qt::UserRole, partVisible);
                    c->setToolTip(COL_EYE, !bound.isEmpty() ? (partVisible ? T("Hide") : T("Show"))
                                                            : T("Show (adds it to the script)"));
                }
                if (ok && partVisible && region.size() == 2 &&
                    !boxesOverlap(p["bounds"].toArray(), region))
                {
                    c->setForeground(COL_NAME, kDim);
                    ptip += T("\nOutside the render region");
                }
                if (emptyPart)
                {
                    c->setForeground(COL_NAME, kDim);
                    ptip = T("Part %1%2 is empty: the file lists this body but gives it no surface, so there is nothing to import.\n"
                                   "It is not an error of your script; the parts after it keep their numbers.")
                               .arg(k).arg(path.isEmpty() ? QString() : " (" + path + ")");
                }
                else if (!ok)
                {
                    c->setForeground(COL_NAME, QColor(0xdc, 0x6e, 0x5e));
                    ptip = p["error"].toString();
                }
                else if (it.contains("index"))
                {
                    const bool inUse = (k == current);
                    ptip += inUse ? T("\nIn use") : T("\nDouble-click: use this part");
                }
                else if (!p.contains("var"))
                {
                    ptip += T("\nDouble-click: add to the script");
                }
                c->setToolTip(COL_NAME, ptip);
            }
            bool anyBound = false;
            for (const auto pv : parts) anyBound |= pv.toObject().contains("var");
            expandedByDefault[row] = parts.size() <= 8 || anyBound;
        }
        // The models it uses and does not own: shadows of their rows (what it owns is its children, put under it below).
        // A shadow is a reference of this statement to the model: dragged away, it takes the model out of this call only
        const QSet<QString> mine = owned.value(key);
        bool anyUses = false;
        for (const auto d : it["deps"].toArray())
        {
            if (mine.contains(d.toString())) continue;
            const QJsonObject ref = itemBefore(d.toString(), it["line"].toInt());
            auto c = makeRow(row, d.toString(), ref.isEmpty() ? QIcon() : fadedIcon(itemIcon(ref, false)), "dep", it,
                             key + ">" + d.toString());
            c->setForeground(COL_NAME, QColor(kDim.red(), kDim.green(), kDim.blue(), 150));
            c->setData(COL_NAME, ROLE_PART, d.toString());
            if (!ref.isEmpty() && ref.contains("var") && !ref["failed"].toBool()) c->setFlags(c->flags() | Qt::ItemIsDragEnabled);
            c->setIcon(COL_DELETE, deleteIcon());
            c->setToolTip(COL_DELETE, T("Take %1 out of %2: its reference here goes, the model stays (D does the same)")
                                         .arg(d.toString(), it["var"].toString()));
            c->setToolTip(COL_NAME, T("%1 is used here too (its own row is under another statement).\n"
                                   "Drag this shadow to another operation to move the reference there, or out to take it out of this "
                                   "call; Ctrl+drag copies it.  Click: select %1").arg(d.toString()));
            anyUses = true;
        }
        // The placeholders of a statement the run stopped before (`...` where an argument goes): a row each, to drop a model on
        {
            const QJsonArray holes = it["holes"].toArray();
            for (int k = 0; k < holes.size(); ++k)
            {
                const QString param = holes[k].toObject()["param"].toString();
                auto c = makeRow(row, param.isEmpty() ? T("placeholder") : param, holeIcon(), "hole", it, key + ">hole" + QString::number(k));
                c->setData(COL_NAME, ROLE_PART, k);
                c->setForeground(COL_NAME, QColor(0xf0, 0xa0, 0x40));
                c->setToolTip(COL_NAME, T("To run the script, write what goes here in %1 (the three dots): click this row to select them in the code editor.\n"
                                       "A model dropped on this row takes their place.").arg(it["var"].toString()));
                anyUses = true;
            }
        }
        if (anyUses || !mine.isEmpty()) expandedByDefault[row] = true;
    }
    phase("items");
    // The nesting: each owned model's row goes under its owner's.  Under a statement the rows of the models it is given stand
    // in the order of its arguments (the owned ones and the shadows alike), then what it uses otherwise
    QHash<QTreeWidgetItem*, QList<QTreeWidgetItem*>> owning;
    for (const auto v : items)
    {
        const auto it = v.toObject();
        if (!it.contains("owner")) continue;
        QTreeWidgetItem* child = rowOfKey.value(keyOf(it));
        QTreeWidgetItem* parent = rowOfKey.value(it["owner"].toString());
        if (!child || !parent || child == parent || nested.contains(child)) continue;
        // (an owner is a later statement than what it owns, so a row is never put under its own child)
        nested.insert(child);
        owning[parent] << child;
    }
    for (auto parent : rowOfKey)
    {
        if (parent->childCount() == 0 && owning.value(parent).isEmpty()) continue;
        const auto pit = parent->data(COL_NAME, ROLE_ITEM).toJsonObject();
        QStringList order;                      // (the models of its call, in the order the call is written)
        for (const auto iv : pit["inputs"].toArray()) order << iv.toObject()["name"].toString();
        QList<QTreeWidgetItem*> rows, others;
        while (parent->childCount() > 0)
        {
            auto c = parent->takeChild(0);
            const QString t = c->data(COL_NAME, ROLE_TYPE).toString();
            (t == "dep" || t == "hole" ? rows : others) << c;
        }
        rows << owning.value(parent);
        auto place = [&](QTreeWidgetItem* c) {
            const bool shadow = c->data(COL_NAME, ROLE_TYPE).toString() == "dep";
            const QString name = shadow ? c->data(COL_NAME, ROLE_PART).toString()
                                        : c->data(COL_NAME, ROLE_ITEM).toJsonObject()["var"].toString();
            const int k = order.indexOf(name);
            return k >= 0 ? k : 100000;
        };
        std::stable_sort(rows.begin(), rows.end(), [&](QTreeWidgetItem* a, QTreeWidgetItem* b) { return place(a) < place(b); });
        // The conditions of a simulation -- and a placeholder for each kind of them it still waits for -- stand under one row of their
        // own, "boundary conditions": its eye shows or hides every one of them at once.  Each condition keeps its own row, its own eye,
        // and shows itself on its own.  The kinds stand in the order the simulation takes them (supports, loads ... / inlets, outlets ...)
        {
            const QJsonArray holesOf = pit["holes"].toArray();
            QStringList slotOrder;
            for (const auto sv : pit["slots"].toObject()["params"].toArray()) slotOrder << sv.toString();
            QList<QTreeWidgetItem*> group;
            QHash<QTreeWidgetItem*, QString> roleOf;
            for (auto c : rows)
            {
                const QString t = c->data(COL_NAME, ROLE_TYPE).toString();
                QString role;
                bool isCondition = false;
                if (t == "hole")
                {
                    const QJsonObject h = holesOf[c->data(COL_NAME, ROLE_PART).toInt()].toObject();
                    if (h["list"].toBool()) { isCondition = true; role = h["param"].toString(); }
                }
                else
                {
                    const QJsonObject m = t == "dep" ? itemBefore(c->data(COL_NAME, ROLE_PART).toString(), pit["line"].toInt())
                                                     : c->data(COL_NAME, ROLE_ITEM).toJsonObject();
                    if (m["type"].toString() == "conditions") { isCondition = true; role = m["condition_role"].toString(); }
                }
                if (!isCondition) continue;
                group << c;
                roleOf[c] = role;
            }
            if (!group.isEmpty())
            {
                std::stable_sort(group.begin(), group.end(), [&](QTreeWidgetItem* a, QTreeWidgetItem* b) {
                    const int x = slotOrder.indexOf(roleOf[a]), y = slotOrder.indexOf(roleOf[b]);
                    return (x < 0 ? 1000 : x) < (y < 0 ? 1000 : y);
                });
                int at = int(rows.size());
                for (auto c : group) at = std::min(at, int(rows.indexOf(c)));
                for (auto c : group) rows.removeAll(c);
                const QString gkey = parent->data(COL_NAME, ROLE_KEY).toString() + ">bc";
                auto g = makeRow(nullptr, T("boundary conditions"), TypeIcons::icon("conditions"), "bcgroup", pit, gkey);
                nested.insert(g);
                conditionGroups << qMakePair(g, gkey);
                g->setToolTip(COL_NAME, T("The conditions of %1: a placeholder for every kind it takes, and the conditions in them.\n"
                                        "The eye shows or hides all of them at once; each condition has its own eye and shows itself on its own.\n"
                                        "Conditions dropped here go in the input of their kind.").arg(pit["var"].toString()));
                QSet<QString> counted;
                int shown = 0, total = 0;
                for (auto c : group)
                {
                    g->addChild(c);
                    const QString t = c->data(COL_NAME, ROLE_TYPE).toString();
                    if (t == "hole") continue;
                    const QJsonObject m = t == "dep" ? itemBefore(c->data(COL_NAME, ROLE_PART).toString(), pit["line"].toInt())
                                                     : c->data(COL_NAME, ROLE_ITEM).toJsonObject();
                    if (!m.contains("var") || m["failed"].toBool() || (m.contains("displayable") && !m["displayable"].toBool())) continue;
                    if (counted.contains(keyOf(m))) continue;
                    counted.insert(keyOf(m));
                    ++total;
                    if (m["visible"].toBool()) ++shown;
                }
                if (total > 0)
                {
                    const bool all = shown == total;
                    g->setIcon(COL_EYE, eyeIcon(all));
                    g->setData(COL_EYE, Qt::UserRole, all);
                    g->setToolTip(COL_EYE, all ? T("Hide all the boundary conditions") : T("Show all the boundary conditions"));
                    if (shown == 0) g->setForeground(COL_NAME, kDim);
                }
                rows.insert(std::min(at, int(rows.size())), g);
            }
        }
        for (auto c : others) parent->addChild(c);
        for (auto c : rows) parent->addChild(c);
    }
    {
        // Every row that is not under another goes into the tree, all at once
        QList<QTreeWidgetItem*> top;
        for (auto r : topRows)
        {
            if (!nested.contains(r)) top << r;
        }
        m_tree->addTopLevelItems(top);
    }
    phase("nesting");
    // The property rows of the models.  They are made here, once the rows are in their order: a row that is taken out of its parent and put back
    // (above) is given back without its widgets
    auto addResolutionRow = [&](QTreeWidgetItem* row, const QJsonObject& it, const QString& meshNote = QString()) {
        // A property of the model, a child row: the resolution it is meshed at, whatever the scene's is.  The number is typed in here
        // (the line `x = custom_resolution(x, number)` is rewritten as it is typed); the bin takes the line away
        const auto cr = it["custom_resolution"].toObject();
        const QString var = it["var"].toString();
        const QString fn = "custom_resolution:" + var;
        auto c = makeRow(row, QString(), QIcon(), "setting", cr, "setting:" + fn);
        c->setData(COL_NAME, ROLE_LINE, cr["line"].toInt() - 1);
        c->setToolTip(COL_NAME, T("A resolution of its own for %1 (samples per mm), whatever the scene's is. Type a number; the bin "
                                "deletes the line").arg(var) + "\n" + cr["text"].toString());
        c->setFirstColumnSpanned(true);
        auto w = new QWidget;
        w->setStyleSheet("background: transparent;");
        auto lay = new QHBoxLayout(w);
        lay->setContentsMargins(0, 0, 2, 0);
        lay->setSpacing(3);
        auto lab = new QLabel("custom_resolution");
        lab->setFixedWidth(116);
        lab->setAttribute(Qt::WA_TransparentForMouseEvents);
        lab->setStyleSheet("color: #eee8d5; font-size: 8.5pt; padding: 0px;");
        lay->addWidget(lab);
        const QString shown = cr["value"].isDouble() ? QString::number(cr["value"].toDouble(), 'g', 8) : QString();
        auto e = new QLineEdit(shown);
        e->setFixedSize(58, 18);
        e->setAlignment(Qt::AlignCenter);                   // (a number is centred in its field)
        e->setCursorPosition(0);
        auto vd = new QDoubleValidator(0.0, 1e9, 6, e);
        vd->setNotation(QDoubleValidator::StandardNotation);
        vd->setLocale(QLocale::c());
        e->setValidator(vd);
        e->setProperty("fn", fn);
        e->setProperty("shown", shown);
        e->setToolTip(T("Samples per mm"));
        e->setStyleSheet("QLineEdit { background: rgba(0, 0, 0, 80); color: #eee8d5; border: 1px solid rgba(147, 161, 161, 90);"
                         " border-radius: 2px; padding: 0px 3px; font-size: 8.5pt; selection-background-color: #268bd2; }"
                         "QLineEdit:focus { border: 1px solid #268bd2; background: rgba(0, 0, 0, 120); }");
        e->installEventFilter(this);
        connect(e, &QLineEdit::textEdited, this, [this, e] { commitSetting(e, true); });
        connect(e, &QLineEdit::editingFinished, this, [this, e] { commitSetting(e); });
        lay->addWidget(e);
        m_settingEditors[fn] << QPointer<QLineEdit>(e);
        if (cr["used"].isDouble())
        {
            // A body can be drawn at 2000 samples along its longest side at the most: a finer number is not an error, it is capped,
            // and what is drawn is said here
            auto capped = new QLabel(QString(QChar(0x2192)) + " " + QString::number(cr["used"].toDouble(), 'g', 3));
            capped->setStyleSheet("color: #e0b050; font-size: 8.5pt; padding: 0px;");
            capped->setToolTip(T("%1 samples per mm is more than this body can be drawn at: it is %2 mm long, and 2000 samples along its "
                                       "longest side is the finest there is. It is drawn at %3 per mm.")
                                   .arg(shown).arg(QString::number(2000.0 / cr["used"].toDouble(), 'g', 4))
                                   .arg(QString::number(cr["used"].toDouble(), 'g', 3)));
            lay->addWidget(capped);
        }
        if (!meshNote.isEmpty())
        {
            // A part that was imported as a mesh (tessellated) is drawn from its own triangles: there is nothing to mesh, so no resolution
            // changes how it looks
            auto mesh = new QLabel("(" + meshNote + ")");
            mesh->setStyleSheet("color: #93a1a1; font-size: 8.5pt; padding: 0px;");
            mesh->setToolTip(T("This part was imported as a mesh: it is drawn from its own triangles, not meshed from a field, so a resolution "
                             "does not change how it looks."));
            lay->addWidget(mesh);
        }
        lay->addStretch(1);
        auto bin = new QToolButton;
        bin->setIcon(deleteIcon());
        bin->setIconSize(QSize(16, 16));
        bin->setFixedSize(20, 20);
        bin->setAutoRaise(true);
        bin->setToolTip(T("Delete this line (the model is drawn at the scene's resolution again)"));
        connect(bin, &QToolButton::clicked, this, [this, c] { deleteRow(c); });
        lay->addWidget(bin);
        m_tree->setItemWidget(c, COL_NAME, w);
    };
    for (auto row : rowOfKey)
    {
        const auto it = row->data(COL_NAME, ROLE_ITEM).toJsonObject();
        if (it.contains("custom_resolution") && it.contains("var") && !it["failed"].toBool()) addResolutionRow(row, it);
    }
    // (a part of an import that the script has as a variable is a row of its own under the import, not a model row: its resolution is
    // a row under it, the same one)
    for (const auto& pr : partResolutions)
    {
        addResolutionRow(pr.first, pr.second, partNotes.value(pr.first));
        pr.first->setExpanded(m_expandState.value(pr.first->data(COL_NAME, ROLE_KEY).toString(), true));
    }
    phase("properties");
    // (a row's expansion is a property of its place in the view: set once the rows are where they stay)
    for (auto row : rowOfKey)
    {
        if (row->childCount() == 0) continue;
        row->setExpanded(m_expandState.value(row->data(COL_NAME, ROLE_KEY).toString(), expandedByDefault.value(row, true)));
    }
    for (const auto& g : conditionGroups) g.first->setExpanded(m_expandState.value(g.second, true));

    // Footer notes: analysis problems, truncated huge scripts
    QString note;
    if (m_scene.contains("error"))
    {
        note = T("Model tree unavailable: %1").arg(m_scene["error"].toString());
    }
    else if (m_scene["truncated"].toInt() > 0)
    {
        note = T("%1 more intermediate shapes not listed").arg(m_scene["truncated"].toInt());
    }
    else if (items.isEmpty())
    {
        note = T("No shapes yet");
    }
    if (m_isolated && note.isEmpty())
    {
        note = T("Isolated: %1. Press I to show everything again.").arg(m_isolatedName);
    }
    bool waitingNote = false;
    {
        // Placeholders (`...`): the script stops before the first statement that has one.  Said whenever there is one, whoever put it
        // there (the tree, when a model was taken out of a call that needs it; the completion; the script's writer), and how to fill it
        QStringList links;
        int total = 0;
        for (const auto v : items)
        {
            const auto it = v.toObject();
            if (!it["hole"].toBool()) continue;
            for (const auto hv : it["holes"].toArray())
            {
                const auto h = hv.toObject();
                const auto span = h["span"].toArray();
                ++total;
                if (span.size() < 2 || links.size() >= 6) continue;
                const QString param = h["param"].toString();
                const QString label = it["var"].toString() + (param.isEmpty() ? QString() : " " + QString(QChar(0x00b7)) + " " + param);
                links << QString("<a href=\"hole:%1:%2\" style=\"text-decoration:none\"><span style=\"color:#f0a040\">%3</span></a>")
                             .arg(span[0].toInt() - 1).arg(span[1].toInt()).arg(label.toHtmlEscaped());
            }
        }
        if (total > 0)
        {
            // (amber, not red: it is no error, it is what has to be done for the script to run -- and it is done in the code editor)
            note = "<span style=\"color:#f0a040; font-weight:bold\">" + T("To run the script, fill in in the code editor:").toHtmlEscaped() + "</span><br>" +
                   links.join("<br>") + (total > links.size() ? "<br>" + QString(QChar(0x2026)) : QString());
        }
        waitingNote = total > 0;
    }
    // (the script has an error: the note says what, in red, and the rows are the last that worked)
    if (!m_errorText.isEmpty()) note = errorNote();
    setErrorStyle(m_note, !m_errorText.isEmpty());
    setWaitingStyle(m_note, waitingNote && m_errorText.isEmpty());
    m_note->setText(note);

    phase("expansion");
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
    phase("selection");
    m_rebuilding = false;
    // The tree is where it was.  The range of the scroll bar is only known once the rows have been laid out (done lazily, after
    // this function), and a value put into the bar before that is cut down to the old range: the tree jumped to the top
    // whenever its rows changed under it (the I key writes a line for each model, and the tree is built again).  So the layout
    // is done now, the value put back, the panel resized to its rows (which can change the range again), and once more when
    // the event loop has laid the rest out
    auto putScrollBack = [this, scroll] {
        if (auto bar = m_tree->verticalScrollBar()) bar->setValue(scroll);
    };
    m_tree->doItemsLayout();
    putScrollBack();
    phase("layout1");
    setCollapsed(m_collapsed);
    phase("collapse");
    m_tree->doItemsLayout();
    putScrollBack();
    phase("layout2");
    if (scroll > 0)
    {
        // (only if something put it back to the top meanwhile: a scroll the user or a selection makes after this stays)
        QTimer::singleShot(0, this, [this, scroll] {
            auto bar = m_tree->verticalScrollBar();
            if (bar && bar->value() == 0 && !m_rebuilding) bar->setValue(scroll);
        });
    }
    if (typedIndex >= 0)
    {
        if (auto typing = settingEditor(typedFn, typedIndex))
        {
            typing->setText(typedText);
            typing->setCursorPosition(typedCursor);
            typing->setFocus();
        }
    }
    if (!nameKey.isEmpty())
    {
        for (QTreeWidgetItemIterator i(m_tree); *i; ++i)
        {
            if ((*i)->data(COL_NAME, ROLE_KEY).toString() != nameKey || !((*i)->flags() & Qt::ItemIsEditable)) continue;
            if (m_tree->currentItem() != *i) m_tree->setCurrentItem(*i);
            m_tree->editItem(*i, COL_NAME);
            if (auto e = qobject_cast<QLineEdit*>(typingWidget()))
            {
                e->setText(nameText);
                e->setCursorPosition(nameCursor);
                if (nameSelLength > 0) e->setSelection(nameSelStart, nameSelLength);
            }
            break;
        }
    }
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

bool ScenePanel::lineIs(int line0, const QString& var, const QString& kind) const
{
    const QString text = lineText(line0);
    if (lineMatches(text, var, kind)) return true;
    QString says;
    if (kind == "display") says = T("the line that shows %1").arg(var);
    else if (kind == "hidden") says = T("the line that hides %1 (# hidden: %1)").arg(var);
    else if (kind == "definition") says = T("a statement of %1").arg(var);
    else says = T("the line %1 = %2(%1, ...)").arg(var, kind);
    m_lineMismatch = T("line %1 of the script is `%2`, and the rows say it is %3").arg(line0 + 1).arg(text.trimmed().left(70)).arg(says);
    return false;
}

bool ScenePanel::lineIsExpression(int line0, const QJsonObject& it) const
{
    // (a displayed expression has no name: its row has the first characters of the statement; hidden, the line has `# hidden: ` before it)
    static const QRegularExpression hiddenRe(R"(^(\s*)(?:#\s*hidden:\s?)+)");
    QString text = lineText(line0);
    text.replace(hiddenRe, "\\1");
    const QString head = it["text"].toString().simplified().left(12);
    if (!head.isEmpty() && text.simplified().left(head.size()) == head) return true;
    m_lineMismatch = T("line %1 of the script is `%2`, and the rows say it is the expression `%3`").arg(line0 + 1)
                         .arg(lineText(line0).trimmed().left(70)).arg(it["text"].toString().left(40));
    return false;
}

void ScenePanel::refuseOutOfDate()
{
    notify(T("Nothing was written: the rows of the tree are of an older text than the script (%1), and an edit made "
           "from them would have changed the wrong line. The script is being run again to bring the rows up to date: click again then.")
               .arg(m_lineMismatch));
    // (the run that brings the rows up to date clears the line under the tree: what happened is said again when it is done)
    m_refusedNote = T("Your last click wrote nothing: the rows were of an older text than the script (%1). They are up to "
                    "date now: click again.").arg(m_lineMismatch);
    m_lineMismatch.clear();
    emit(rerunRequested());
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
            {
                // (the line is checked to be the line that shows the model: m_lineMismatch says when it is not, and the caller writes nothing)
                if (!lineIs(L, it["var"].toString(), "display")) return false;
                edits << TextEdit{L, 0, L, int(t.size()), indentOf(t) + "# hidden: " + t.trimmed()};
            }
        }
        else if (show && it.contains("hidden_line"))
        {
            const int L = it["hidden_line"].toInt() - 1;
            if (!lineIs(L, it["var"].toString(), "hidden")) return false;
            const QString t = lineText(L);
            QString shown = t;
            shown.replace(hiddenRe, "\\1");
            edits << TextEdit{L, 0, L, int(t.size()), shown};
        }
        else if (show)
        {
            const int L = it["end_line"].toInt() - 1;
            if (!lineIs(it["line"].toInt() - 1, it["var"].toString(), "definition")) return false;
            const QString t = lineText(L);
            edits << TextEdit{L, int(t.size()), L, int(t.size()), "\n" + indentOf(t) + it["var"].toString()};
        }
        return true;
    }
    if (it["kind"].toString() == "display")
    {
        const int a = it["line"].toInt() - 1, b = it["end_line"].toInt() - 1;
        if (!lineIsExpression(a, it)) return false;
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

void ScenePanel::showOtherPart(const QJsonObject& imp, int part)
{
    // "x = import_model(P)[i]..." can show only part i: split it into
    // "name = import_model(P)" and "x = name[i]...", then add the part
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

bool ScenePanel::sceneIsStale() const
{
    // The rows know the lines of the script as they were when it ran.  When the text has changed since (an edit of the script that is
    // still running, or one that a click of the tree has made and whose run is not done), an edit made from those lines lands on
    // other lines: a definition commented out, a line of the lock deleted that is another's
    if (!m_source) return false;
    // The rows are made again a moment from now (after an edit that the tree followed): the ones that are there are the old ones
    if (m_rebuildPending) return true;
    // A prediction is of the text that the edit gave: while the script is as the edit left it, the lines of the rows fit it (a key typed
    // in the editor since makes them wrong again)
    if (m_predicted) return md5Hex(m_source()) != m_predictedMd5;
    if (m_sceneClock.isValid() && m_sceneClock.elapsed() > 120000) return false;     // (no scene has come for two minutes: not waited for)
    const QString md5 = m_scene["source_md5"].toString();
    if (md5.isEmpty()) return false;
    return QCryptographicHash::hash(m_source().toUtf8(), QCryptographicHash::Md5).toHex() != md5.toLatin1();
}

bool ScenePanel::deferEdit(std::function<void()> again, const QString& what)
{
    if (!sceneIsStale()) return false;
    if (m_textUnreadable)
    {
        // (no tree is going to be read from this text: the one shown is of the last text that could be read)
        notify(T("The script has an error that stops it from being read, so the rows are those of the last text that could be read and "
                 "no longer fit it: %1 cannot be done from them. Fix the error first.").arg(what));
        return true;
    }
    m_deferred << again;
    notify(T("The model tree is still reading the text: %1 follows in a moment.").arg(what));
    return true;
}

bool ScenePanel::deferItems(const QList<QJsonObject>& items, const QString& what,
                            std::function<void(const QList<QJsonObject>&)> again)
{
    for (const auto& it : items)
    {
        if (!it.contains("var") && !it.contains("list_var")) return false;       // (an expression has no name to find it again by)
    }
    if (!sceneIsStale()) return false;
    QStringList keys;
    for (const auto& it : items) keys << keyOf(it);
    return deferEdit([this, keys, again] {
        QList<QJsonObject> fresh;
        for (const QString& k : keys)
        {
            const QJsonObject o = itemByKey(k);
            if (!o.isEmpty()) fresh << o;
        }
        if (!fresh.isEmpty()) again(fresh);
    }, what);
}

void ScenePanel::runDeferred()
{
    // (one at a time: what it edits makes the scene out of date again, and the next waits for the run that answers it)
    if (m_deferred.isEmpty() || sceneIsStale()) return;
    const auto next = m_deferred.takeFirst();
    QTimer::singleShot(0, this, next);
}

QVector<int> ScenePanel::originAfter(const QStringList& before, const QList<TextEdit>& edits, const QStringList& after) const
{
    // The text is its lines joined by newlines: every edit is a range of characters replaced by a text.  A line is the same line
    // afterwards when its first character is where a line begins afterwards -- a line that is replaced from its first character on
    // stays that line, a line that is deleted from its first character on is gone, and a line before which text is put is pushed down
    // by the lines of that text (what is put in front of it on its own line does not make it another line)
    const int n = before.size(), m = after.size();
    if (n == 0 || m == 0) return QVector<int>();
    QVector<int> start(n + 1);
    int off = 0;
    for (int i = 0; i < n; ++i)
    {
        start[i] = off;
        off += before[i].size() + 1;
    }
    start[n] = off;
    const int end = off - 1;                                        // (the text's length)
    auto at = [&](int line, int col) {
        if (line < 0) return 0;
        if (line >= n) return end;
        return start[line] + std::min(std::max(col, 0), int(before[line].size()));
    };
    struct Span { int a, b, len, lastNewline; };
    QList<Span> spans;
    int expected = end;
    for (const auto& e : edits)
    {
        const Span s{at(e.line0, e.col0), at(e.line1, e.col1), int(e.text.size()), int(e.text.lastIndexOf('\n'))};
        if (s.b < s.a) return QVector<int>();
        spans << s;
        expected += s.len - (s.b - s.a);
    }
    std::stable_sort(spans.begin(), spans.end(), [](const Span& x, const Span& y) { return x.a != y.a ? x.a < y.a : x.b < y.b; });
    for (int k = 1; k < spans.size(); ++k)
    {
        if (spans[k].a < spans[k - 1].b) return QVector<int>();            // (edits that overlap)
    }
    // Where each line of the new text begins; the new text must be what these edits make of the old one (its length at least)
    QHash<int, int> lineAt;
    off = 0;
    for (int i = 0; i < m; ++i)
    {
        lineAt.insert(off, i);
        off += after[i].size() + 1;
    }
    if (off - 1 != expected) return QVector<int>();
    QVector<int> origin(m, -1);
    for (int i = 0; i < n; ++i)
    {
        const int p = start[i];
        int delta = 0;
        bool gone = false;
        for (const auto& s : spans)
        {
            if (p < s.a) break;
            if (s.a == s.b)
            {
                delta += (p == s.a) ? s.lastNewline + 1 : s.len;
                continue;
            }
            if (p == s.a)
            {
                gone = s.len == 0;
                break;
            }
            if (p < s.b)
            {
                gone = true;
                break;
            }
            delta += s.len - (s.b - s.a);
        }
        if (gone) continue;
        const auto found = lineAt.constFind(p + delta);
        if (found == lineAt.constEnd()) return QVector<int>();             // (a line that stays, and is not where a line begins: not these edits)
        if (origin[found.value()] >= 0) return QVector<int>();             // (two lines that cannot be told apart)
        origin[found.value()] = i;
    }
    // A line that no edit touched is the same text afterwards: if it is not, the edits are not what happened to the text
    for (int k = 0; k < m; ++k)
    {
        const int i = origin[k];
        if (i < 0) continue;
        const int lo = start[i], hi = start[i] + before[i].size();
        bool touched = false;
        for (const auto& s : spans)
        {
            if (s.a <= hi && s.b >= lo)
            {
                touched = true;
                break;
            }
        }
        if (!touched && before[i] != after[k]) return QVector<int>();
    }
    return origin;
}

bool ScenePanel::editFollowed(const QList<TextEdit>& edits, const QString& what,
                              const std::function<void(QJsonObject&, const QVector<int>&)>& patch)
{
    if (!m_source)
    {
        emit(editScript(edits, what));
        return false;
    }
    // An edit that starts or ends before the first line is not one: the rows it was worked out from do not fit the script (the editor
    // would put it at the end of the text).  It is not written
    for (const auto& e : edits)
    {
        if (e.line0 < 0 || e.line1 < 0 || e.col0 < 0 || e.col1 < 0)
        {
            notify(T("The rows do not fit the script (a line they point at is not there): nothing was changed. Wait for the script to run."));
            return false;
        }
    }
    // (the rows must be of the text the edit is made from: the buttons ask deferItems before they work out the edit)
    const bool fits = !sceneIsStale();
    const QStringList before = m_source().split('\n');
    QElapsedTimer clock;
    clock.start();
    emit(editScript(edits, what));
    if (!fits) return false;
    const QStringList after = m_source().split('\n');
    if (after == before) return false;
    const QVector<int> origin = originAfter(before, edits, after);
    if (origin.isEmpty()) return false;                 // (not followed: the tree waits for the run, as an edit of the editor's does)
    QVector<int> newIndex(before.size(), -1);
    for (int k = 0; k < origin.size(); ++k)
    {
        if (origin[k] >= 0 && origin[k] < newIndex.size()) newIndex[origin[k]] = k;
    }
    QJsonObject scene = predictedScene(before.size(), after, origin, QList<ColShift>(), QStringList(), QHash<QString, int>());
    {
        // (an edit that took no line away takes no model away: a tree that lost some is not what the edit made)
        bool taken = false;
        for (int v : newIndex) taken = taken || v < 0;
        int was = 0, now = 0;
        for (const auto v : m_scene["items"].toArray()) was += v.toObject().contains("line") ? 1 : 0;
        for (const auto v : scene["items"].toArray()) now += v.toObject().contains("line") ? 1 : 0;
        if (!taken && was != now) return false;
    }
    if (const char* log = std::getenv("FIELDES_TREE_LOG"))
    {
        // (for the tests: what the edit was and what the tree made of it)
        if (FILE* f = std::fopen(log, "a"))
        {
            QString o;
            for (int k = 0; k < origin.size() && k < 40; ++k) o += QString::number(origin[k]) + " ";
            std::fprintf(f, "[tree] edit \"%s\": %d -> %d lines, origin %s\n", what.toUtf8().constData(), int(before.size()),
                         int(after.size()), o.toUtf8().constData());
            for (const auto& e : edits)
                std::fprintf(f, "[tree]   replace %d:%d - %d:%d by \"%s\"\n", e.line0, e.col0, e.line1, e.col1,
                             QString(e.text).replace('\n', "\\n").toUtf8().constData());
            std::fclose(f);
        }
    }
    patch(scene, newIndex);
    if (const char* log = std::getenv("FIELDES_TREE_LOG"))
    {
        if (FILE* f = std::fopen(log, "a"))
        {
            for (const auto v : scene["items"].toArray())
            {
                const auto o = v.toObject();
                if (!o.contains("var")) continue;
                std::fprintf(f, "[tree]   item %s line=%d display=%d hidden=%d visible=%d locked=%d cache_off=%d\n",
                             o["var"].toString().toUtf8().constData(), o["line"].toInt(), o["display_line"].toInt(), o["hidden_line"].toInt(),
                             o["visible"].toBool() ? 1 : 0, o["locked"].toObject()["line"].toInt(), o["cache_off"].toObject()["line"].toInt());
            }
            std::fclose(f);
        }
    }
    // The scene is the prediction from now on; the rows are made from it a moment later, not inside the click that is being handled
    // (the click's own row is deleted by that: the tree is still using it) -- until then an edit waits (sceneIsStale)
    m_scene = scene;
    m_predicted = true;
    m_predictedMd5 = md5Hex(m_source());
    m_predictClock.start();
    // The viewport's shapes are made on lines that this edit has moved: they are told, so that a shape that is clicked is the row it
    // is, and the row that is selected lights up its own shape, until the run has made new ones
    emit(sourceLinesMoved(newIndex));
    if (!m_rebuildPending)
    {
        m_rebuildPending = true;
        QTimer::singleShot(0, this, [this] {
            if (!m_rebuildPending) return;              // (a scene made the rows meanwhile)
            rebuild();
            updateMultiNote();
            emit(highlightLines(selectedLines()));      // (the shapes of the selected rows, by their lines as they are now)
            runDeferred();
        });
    }
    if (std::getenv("FIELDES_TREE_LOG"))
    {
        if (FILE* f = std::fopen(std::getenv("FIELDES_TREE_LOG"), "a"))
        {
            std::fprintf(f, "[tree] followed \"%s\": %lld ms to write and follow\n", what.toUtf8().constData(),
                         static_cast<long long>(clock.elapsed()));
            std::fclose(f);
        }
    }
    return true;
}

QString ScenePanel::sceneCachePath(const QString& md5) const
{
    const QByteArray env = qgetenv("FIELDES_SCENE_CACHE_DIR");
    const QString dir = !env.isEmpty() ? QString::fromLocal8Bit(env)
                                       : QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/scene-cache";
    return dir + "/" + md5 + "-" + sceneCacheStamp() + ".fdscene";
}

void ScenePanel::saveSceneCache(const QString& json, const QString& md5)
{
    const QString key = md5 + "|" + QString::fromLatin1(QCryptographicHash::hash(json.toUtf8(), QCryptographicHash::Md5).toHex());
    if (key == m_savedSceneKey) return;                 // (the same tree is kept already)
    m_savedSceneKey = key;
    const QString path = sceneCachePath(md5);
    // (a moment later, with the rows made: the tree does not wait for the disk)
    QTimer::singleShot(250, this, [path, json] {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QSaveFile f(path);
        if (!f.open(QIODevice::WriteOnly)) return;
        f.write(qCompress(json.toUtf8(), 3));
        if (!f.commit()) return;
        // (the oldest trees go: sixty are kept)
        const auto files = QDir(QFileInfo(path).absolutePath()).entryInfoList(QStringList{"*.fdscene"}, QDir::Files, QDir::Time);
        for (int i = 60; i < files.size(); ++i) QFile::remove(files[i].absoluteFilePath());
    });
}

void ScenePanel::showCached()
{
    if (!m_source || m_predicted || !m_scene["items"].toArray().isEmpty()) return;
    const QString text = m_source();
    if (text.trimmed().isEmpty()) return;
    const QString md5 = md5Hex(text);
    const QString path = sceneCachePath(md5);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return;
    const QByteArray json = qUncompress(f.readAll());
    f.close();
    const auto doc = QJsonDocument::fromJson(json);
    if (!doc.isObject()) return;
    const QJsonObject scene = doc.object();
    // (a tree of this very text, from a run that worked)
    if (scene["errored"].toBool() || scene["source_md5"].toString() != md5 || scene["items"].toArray().isEmpty()) return;
    {
        // (the newest are kept when the oldest go)
        QFile touch(path);
        if (touch.open(QIODevice::ReadWrite)) touch.setFileTime(QDateTime::currentDateTime(), QFileDevice::FileModificationTime);
    }
    m_savedSceneKey = md5 + "|" + QString::fromLatin1(QCryptographicHash::hash(json, QCryptographicHash::Md5).toHex());
    if (m_support)
    {
        // (what the kept tree says its statements made is what the tree read from the text knows until a run says it again)
        QString ignored;
        m_support("seed_known", QString::fromUtf8(json), &ignored);
    }
    m_scene = scene;
    m_sceneClock.start();
    m_predicted = false;
    TypeIcons::setKinds(m_scene["kinds"].toObject());
    ++m_generation;
    rebuild();
    updateMultiNote();
}

void ScenePanel::setVisibleAll(const QList<QJsonObject>& models, bool show)
{
    if (deferItems(models, T("showing or hiding them"), [this, show](const QList<QJsonObject>& f) { setVisibleAll(f, show); })) return;
    QList<TextEdit> edits;
    QStringList names;
    QList<QJsonObject> changed;
    m_lineMismatch.clear();
    for (const auto& m : models)
    {
        if (visibilityEdit(m, show, edits))
        {
            names << m["var"].toString();
            changed << m;
        }
    }
    // (a line that is not the one the rows say: nothing is written, not even for the models whose lines are right)
    if (!m_lineMismatch.isEmpty()) { refuseOutOfDate(); return; }
    if (edits.isEmpty()) return;
    m_prepareAgain = true;          // (a model that is shown now is made ready to be dragged once the script has run)
    editFollowed(edits, (show ? T("Show %1") : T("Hide %1")).arg(names.join(", ")),
                 [changed, show](QJsonObject& scene, const QVector<int>& moved) {
        for (const auto& item : changed)
        {
            const int line = moved.value(item["line"].toInt() - 1, -1) + 1;
            if (line > 0) patchAt(scene, line, item["var"].toString(), [show](QJsonObject& o) { patchVisible(o, show); });
        }
    });
}

void ScenePanel::toggleVisible(const QJsonObject& it)
{
    if (deferItems({it}, T("showing or hiding it"), [this](const QList<QJsonObject>& f) { toggleVisible(f[0]); })) return;
    QList<TextEdit> edits;
    static const QRegularExpression hiddenRe(R"(^(\s*)(?:#\s*hidden:\s?)+)");
    if (it.contains("var"))
    {
        const QString var = it["var"].toString();
        bool show = true;
        // (the line an edit is written on is checked to be the line the row says: see lineIs)
        if (it.contains("display_line"))
        {
            show = false;
            const int L = it["display_line"].toInt() - 1;
            const QString t = lineText(L);
            if (!hiddenRe.match(t).hasMatch())
            {
                if (!lineIs(L, var, "display")) { refuseOutOfDate(); return; }
                edits << TextEdit{L, 0, L, int(t.size()), indentOf(t) + "# hidden: " + t.trimmed()};
            }
        }
        else if (it.contains("hidden_line"))
        {
            const int L = it["hidden_line"].toInt() - 1;
            if (!lineIs(L, var, "hidden")) { refuseOutOfDate(); return; }
            const QString t = lineText(L);
            QString shown = t;
            shown.replace(hiddenRe, "\\1");
            edits << TextEdit{L, 0, L, int(t.size()), shown};
            m_prepareAgain = true;          // (a selected model that is shown now is made ready to be dragged once the script has run)
        }
        else
        {
            const int L = it["end_line"].toInt() - 1;
            // (the statement starts on its `line`; the last of its lines can be the end of a call)
            if (!lineIs(it["line"].toInt() - 1, var, "definition") || lineText(L).isNull()) { refuseOutOfDate(); return; }
            const QString t = lineText(L);
            edits << TextEdit{L, int(t.size()), L, int(t.size()), "\n" + var};
            m_prepareAgain = true;          // (a selected model that is shown now is made ready to be dragged once the script has run)
        }
        if (edits.isEmpty()) return;
        // The tree follows the edit at once (its eye, its dimmed name, every line under it), without waiting for the run
        editFollowed(edits, (show ? "Show " : "Hide ") + var, [it, show](QJsonObject& scene, const QVector<int>& moved) {
            const int line = moved.value(it["line"].toInt() - 1, -1) + 1;
            if (line > 0) patchAt(scene, line, it["var"].toString(), [show](QJsonObject& o) { patchVisible(o, show); });
        });
        return;
    }
    if (it["kind"].toString() == "display")
    {
        const int a = it["line"].toInt() - 1, b = it["end_line"].toInt() - 1;
        const bool visible = it["visible"].toBool();
        if (!lineIsExpression(a, it)) { refuseOutOfDate(); return; }
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
    // "x = import_model(P)[k]": the statement's own variable is the part
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
    if (targets.isEmpty()) { notify(T("Select a model first.")); return; }
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
    if (targets.isEmpty()) { notify(T("Select a model first.")); return; }
    // (not all locked: they are all locked; all locked: they are all unlocked)
    bool allLocked = true;
    for (const auto& t : targets) allLocked = allLocked && t.contains("locked");
    applyLock(targets, !allLocked);
}

void ScenePanel::toggleSelectedVisible()
{
    if (deferEdit([this] { toggleSelectedVisible(); }, T("showing or hiding the selected models"))) return;
    QList<Model> models;
    for (const Model& m : selectedModels())
    {
        // (a material, a lattice cell ...: nothing to show or hide)
        if (m.item.contains("displayable") && !m.item["displayable"].toBool()) continue;
        models << m;
    }
    if (models.isEmpty()) { notify(T("Select a model that can be drawn first.")); return; }
    // (not all shown: they are all shown; all shown: they are all hidden)
    bool allShown = true;
    for (const Model& m : models) allShown = allShown && m.item["visible"].toBool();
    QList<TextEdit> edits;
    QStringList names;
    QList<QJsonObject> changed;
    bool followable = true;                     // (the tree follows models that are variables; an expression is the run's to say)
    m_lineMismatch.clear();
    for (const Model& m : models)
    {
        if (visibilityEdit(m.item, !allShown, edits))
        {
            names << (m.item.contains("var") ? m.item["var"].toString() : m.item["label"].toString());
            changed << m.item;
            followable = followable && m.item.contains("var");
        }
    }
    // (a line that is not the one the rows say: nothing is written, not even for the models whose lines are right)
    if (!m_lineMismatch.isEmpty()) { refuseOutOfDate(); return; }
    if (edits.isEmpty()) return;
    m_prepareAgain = true;          // (a selected model that is shown now is made ready to be dragged once the script has run)
    const QString what = (allShown ? T("Hide %1") : T("Show %1")).arg(names.join(", "));
    if (!followable)
    {
        emit(editScript(edits, what));
        return;
    }
    const bool show = !allShown;
    editFollowed(edits, what, [changed, show](QJsonObject& scene, const QVector<int>& moved) {
        for (const auto& item : changed)
        {
            const int line = moved.value(item["line"].toInt() - 1, -1) + 1;
            if (line > 0) patchAt(scene, line, item["var"].toString(), [show](QJsonObject& o) { patchVisible(o, show); });
        }
    });
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
    if (targets.isEmpty()) { notify(T("Select a model first.")); return; }
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

QString ScenePanel::headerText() const
{
    return QString(QChar(m_collapsed ? 0x25b8 : 0x25be)) + "  " + T("Model tree") +
           (m_errorText.isEmpty() ? QString() : QString("   ") + QChar(0x26a0) + " " + T("error"));
}

QString ScenePanel::errorNote() const
{
    return QString(QChar(0x26a0)) + " " + (m_errorLine >= 0 ? T("Line %1: ").arg(m_errorLine + 1) : QString()) + m_errorText;
}

void ScenePanel::setErrorStyle(QWidget* w, bool on)
{
    if (w->property("error").toBool() == on) return;
    w->setProperty("error", on);
    w->style()->unpolish(w);
    w->style()->polish(w);
    w->update();
}

void ScenePanel::setWaitingStyle(QWidget* w, bool on)
{
    if (w->property("waiting").toBool() == on) return;
    w->setProperty("waiting", on);
    w->style()->unpolish(w);
    w->style()->polish(w);
    w->update();
}

void ScenePanel::setError(const QString& text, int line0)
{
    m_errorText = text;
    m_errorLine = line0;
    const bool on = !text.isEmpty();
    setErrorStyle(this, on);
    setErrorStyle(m_header, on);
    setErrorStyle(m_note, on);
    m_header->setText(headerText());
    // (cleared: the next scene, which comes with the run that worked, writes the note again; shown: it is written now)
    m_note->setText(on ? errorNote() : QString());
    setCollapsed(m_collapsed);
}

void ScenePanel::notify(const QString& text)
{
    // (the line under the tree: it says what could not be done, until the next run of the script)
    setWaitingStyle(m_note, false);
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
    if (deferItems(targets, T("changing the gizmo mode"),
                   [this, next, quiet](const QList<QJsonObject>& f) { applyModes(f, next, quiet); })) return;
    if (targets.size() == 1 && targets[0]["kind"].toString() == "display")
    {
        nameThen(targets[0], next);
        return;
    }
    QList<TextEdit> edits;
    QStringList names;
    QList<QJsonObject> changed;
    ModeImports imports;
    QString why;
    m_lineMismatch.clear();
    for (const auto& t : targets)
    {
        // (an expression that has no name yet is named one at a time, above)
        if (t["kind"].toString() == "display" || !t.contains("var")) continue;
        if (modeEdits(t, next, edits, imports, &why))
        {
            names << t["var"].toString();
            changed << t;
        }
    }
    // (a line that is not the one the rows say: nothing is written, not even for the models whose lines are right)
    if (!m_lineMismatch.isEmpty()) { refuseOutOfDate(); return; }
    if (imports.handles) addImportIfMissing(edits, "handles", "handles");
    if (imports.expose) addImportIfMissing(edits, "expose", "handles");
    if (edits.isEmpty())
    {
        if (!why.isEmpty() && !quiet) notify(why);
        return;
    }
    emit(editScript(edits, "Edit " + names.join(", ") + ": " + next));
    // The buttons say the new mode at once, and the rows know it for the next click.  The lines the edit wrote (a handles() line, its
    // mode) are the run's to say: until its scene is there the rows are the old ones and the next edit waits for it
    auto sameModel = [&](const QJsonObject& a, const QJsonObject& b) {
        return a["var"].toString() == b["var"].toString() && a["line"].toInt() == b["line"].toInt();
    };
    QJsonArray items = m_scene["items"].toArray();
    for (int i = 0; i < items.size(); ++i)
    {
        QJsonObject o = items[i].toObject();
        for (const auto& t : changed)
        {
            if (!sameModel(o, t)) continue;
            o["mode"] = next;
            o["mode_explicit"] = true;
            items[i] = o;
            break;
        }
    }
    m_scene["items"] = items;
    for (QTreeWidgetItemIterator i(m_tree); *i; ++i)
    {
        QTreeWidgetItem* row = *i;
        if (row->icon(COL_HANDLES).isNull()) continue;
        QJsonObject model = modelOfRow(row);
        if (model.isEmpty()) continue;
        for (const auto& t : changed)
        {
            if (!sameModel(model, t)) continue;
            model["mode"] = next;
            setModeButton(row, model);
            if (row->data(COL_NAME, ROLE_TYPE).toString() == "item") row->setData(COL_NAME, ROLE_ITEM, model);
            break;
        }
    }
}

void ScenePanel::applyLock(const QList<QJsonObject>& targets, bool lock)
{
    if (deferItems(targets, lock ? "locking" : "unlocking", [this, lock](const QList<QJsonObject>& f) { applyLock(f, lock); })) return;
    if (targets.size() == 1 && targets[0]["kind"].toString() == "display")
    {
        // (an expression is named first, and locked once the script has run again)
        if (lock) nameThen(targets[0], "lock");
        return;
    }
    QList<TextEdit> edits;
    QStringList names;
    struct Done { QJsonObject t; int after; QString indent; };
    QList<Done> done;
    for (const auto& t : targets)
    {
        if (t["kind"].toString() == "display" || !t.contains("var")) continue;
        if (t.contains("locked") == lock) continue;             // (as asked already)
        const QString var = t["var"].toString();
        int placed = -1;
        QString indent;
        if (lock)
        {
            // (the line the new one goes under is the definition of the model, as the rows say)
            if (!lineIs(t["line"].toInt() - 1, var, "definition")) { refuseOutOfDate(); return; }
            // Under the definition, and under the numbers exposed for its surfaces, its handles and its cache
            int after = t["end_line"].toInt() - 1;
            for (const char* key : {"exposed", "handles", "cache", "cache_off", "custom_resolution"})
            {
                if (t.contains(key)) after = std::max(after, t[key].toObject()["end_line"].toInt() - 1);
            }
            indent = indentOf(lineText(t["line"].toInt() - 1));
            const QString last = lineText(after);
            edits << TextEdit{after, int(last.size()), after, int(last.size()),
                              "\n" + indent + var + " = lock(" + var + ")"};
            placed = after;
        }
        else
        {
            const auto c = t["locked"].toObject();
            if (!lineIs(c["line"].toInt() - 1, var, "lock")) { refuseOutOfDate(); return; }
            edits << deleteLines(c["line"].toInt() - 1, c["end_line"].toInt() - 1);
        }
        done << Done{t, placed, indent};
        names << var;
    }
    if (edits.isEmpty()) return;
    if (lock) addImportIfMissing(edits, "lock", "handles");
    // (a model that was locked from the start -- an imported part -- has no gizmo line and no numbers for its surfaces yet: once it
    // is unlocked and the script has run, the selected ones are made ready to be dragged)
    else m_prepareAgain = true;
    // The tree follows the edit at once: the lock line is under the definition (or gone), every line under it has moved
    editFollowed(edits, QString(lock ? "Lock " : "Unlock ") + names.join(", "),
                 [done, lock](QJsonObject& scene, const QVector<int>& moved) {
        for (const auto& d : done)
        {
            const int line = moved.value(d.t["line"].toInt() - 1, -1) + 1;
            if (line <= 0) continue;
            const QString var = d.t["var"].toString();
            if (!lock)
            {
                patchAt(scene, line, var, [](QJsonObject& o) { o.remove("locked"); });
                continue;
            }
            const int placed = moved.value(d.after, -1);        // (the line the new one follows, in the new text)
            if (placed < 0) continue;
            patchAt(scene, line, var, [&](QJsonObject& o) {
                o["locked"] = statementInfo(placed + 2, var, "lock", QString(), d.indent);
            });
        }
    });
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
    // (the lines this edit is written at are checked to be what the rows say: the caller writes nothing when m_lineMismatch says one is not)
    if (!lineIs(a, var, "definition")) return false;
    if (hasHandles && !lineIs(h["line"].toInt() - 1, var, "handles")) return false;
    if (hasExposed && !lineIs(target["exposed"].toObject()["line"].toInt() - 1, var, "expose")) return false;

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
        const QString reason = error.isEmpty() ? T("the script has not been run") : error;
        if (qEnvironmentVariableIsSet("FIELDES_AUTOMATION"))
            fprintf(stderr, "[handles] %s: %s\n", target["var"].toString().toUtf8().constData(), reason.toUtf8().constData());
        if (why) *why = T("Cannot make %1 draggable: %2").arg(target["var"].toString(), reason);
        return QString();
    }
    const QString indent = indentOf(lineText(target["line"].toInt() - 1));
    QStringList lines = text.split('\n');
    for (auto& l : lines) l = indent + l;
    return lines.join('\n');
}

void ScenePanel::exposeSurfaces(const QJsonObject& target)
{
    if (deferItems({target}, T("making its surfaces draggable"), [this](const QList<QJsonObject>& f) { exposeSurfaces(f[0]); })) return;
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
    QString tip = on ? T("Render cache: on (the default)") : T("Render cache: off");
    if (!words.isEmpty()) tip += "  ·  " + words;
    tip += on ? T("\nClick: turn it off for this shape (writes render_cache(x, False) under its definition)")
              : T("\nClick: turn it back on (deletes its render_cache line)");
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
    if (deferItems(targets, T("the render cache"), [this, on](const QList<QJsonObject>& f) { applyCache(f, on); })) return;
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
    // What the tree does with each (the cache lines are `x = render_cache(x, False)`; `placed` is the line a new one follows)
    struct Done { QJsonObject t; int placed; QString indent; };
    QList<Done> done;
    for (const auto& t : targets)
    {
        if (t["kind"].toString() == "display" || !t.contains("var")) continue;
        const QString var = t["var"].toString();
        int placed = -1;
        QString indent;
        if (on)
        {
            // Off by a line `x = render_cache(x, False)`: the line goes, and it is on again (the default)
            if (!t.contains("cache_off")) continue;
            const auto c = t["cache_off"].toObject();
            if (!lineIs(c["line"].toInt() - 1, var, "render_cache")) { refuseOutOfDate(); return; }
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
                if (!lineIs(a, var, "render_cache")) { refuseOutOfDate(); return; }
                indent = indentOf(lineText(a));
                edits << TextEdit{a, 0, b, int(lineText(b).size()), indent + var + " = render_cache(" + var + ", False)"};
            }
            else
            {
                // On by default: the line that turns it off goes under the definition, and under the numbers exposed
                // for its surfaces, its handles and its lock: the cache is the last of what is done to the shape, so
                // that it keeps the shape as it is shown
                if (!lineIs(t["line"].toInt() - 1, var, "definition")) { refuseOutOfDate(); return; }
                int after = t["end_line"].toInt() - 1;
                for (const char* key : {"exposed", "handles", "locked", "custom_resolution"})
                {
                    if (t.contains(key)) after = std::max(after, t[key].toObject()["end_line"].toInt() - 1);
                }
                indent = indentOf(lineText(t["line"].toInt() - 1));
                const QString last = lineText(after);
                edits << TextEdit{after, int(last.size()), after, int(last.size()),
                                  "\n" + indent + var + " = render_cache(" + var + ", False)"};
                needImport = true;
                placed = after;
            }
        }
        done << Done{t, placed, indent};
        names << var;
    }
    if (edits.isEmpty()) return;
    if (needImport) addImportIfMissing(edits, "render_cache", "render_cache");
    // The tree follows the edit at once
    editFollowed(edits, QString(on ? "Render cache on: " : "Render cache off: ") + names.join(", "),
                 [done, on](QJsonObject& scene, const QVector<int>& moved) {
        for (const auto& d : done)
        {
            const int line = moved.value(d.t["line"].toInt() - 1, -1) + 1;
            if (line <= 0) continue;
            const QString var = d.t["var"].toString();
            patchAt(scene, line, var, [&](QJsonObject& o) {
                if (on)
                {
                    o.remove("cache_off");
                }
                else if (d.t.contains("cache"))
                {
                    // (the line that said it was on says it is off now: the same lines)
                    const QJsonObject c = o["cache"].toObject();
                    o.remove("cache");
                    o["cache_off"] = statementInfo(c["line"].toInt(), var, "render_cache", ", False", d.indent);
                }
                else if (d.placed >= 0)
                {
                    const int at = moved.value(d.placed, -1);
                    if (at >= 0) o["cache_off"] = statementInfo(at + 2, var, "render_cache", ", False", d.indent);
                }
            });
        }
    });
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
    {
        // (a row is found again by its key when the script has run: the rows are made anew)
        const QString key = row->data(COL_NAME, ROLE_KEY).toString();
        if (deferEdit([this, key] {
                for (QTreeWidgetItemIterator i(m_tree); *i; ++i)
                {
                    if ((*i)->data(COL_NAME, ROLE_KEY).toString() == key) { deleteRow(*i); return; }
                }
            }, "deleting"))
            return;
    }
    const QString type = row->data(COL_NAME, ROLE_TYPE).toString();
    const auto it = row->data(COL_NAME, ROLE_ITEM).toJsonObject();
    if (type == "setting")
    {
        // (the render settings are lines of the code, changed there and never deleted from here: only a model's own resolution is a row
        // that goes)
        if (!row->data(COL_NAME, ROLE_KEY).toString().startsWith("setting:custom_resolution:")) return;
        if (it.contains("line"))
        {
            const QList<TextEdit> edit{deleteLines(it["line"].toInt() - 1, it["end_line"].toInt() - 1)};
            const QString what = "Delete " + it["text"].toString();
            // (the resolution of a model of its own is a block of its model: its row goes with its line, at once.  What a render setting
            // goes back to is the run's to say)
            const QString key = row->data(COL_NAME, ROLE_KEY).toString();
            if (key.startsWith("setting:custom_resolution:"))
            {
                if (!lineIs(it["line"].toInt() - 1, key.mid(int(QString("setting:custom_resolution:").size())), "custom_resolution"))
                {
                    refuseOutOfDate();
                    return;
                }
                editFollowed(edit, what, [](QJsonObject&, const QVector<int>&) {});
            }
            else
            {
                emit(editScript(edit, what));
            }
        }
        return;
    }
    if (type == "bcgroup") return;              // (a group is not a model: the conditions in it are deleted one by one)
    if (type == "dep")
    {
        deleteReferences({qMakePair(it, row->data(COL_NAME, ROLE_PART).toString())});
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
    // (a shadow that is selected is a reference: it is taken out of its call)
    QList<QPair<QJsonObject, QString>> refs;
    for (auto row : m_tree->selectedItems())
    {
        if (row->data(COL_NAME, ROLE_TYPE).toString() == "dep")
            refs << qMakePair(row->data(COL_NAME, ROLE_ITEM).toJsonObject(), row->data(COL_NAME, ROLE_PART).toString());
    }
    if (items.isEmpty() && refs.isEmpty()) { notify(T("Select a model first.")); return; }
    if (!refs.isEmpty())
    {
        QStringList keys;
        for (const auto& it : items) keys << keyOf(it);
        deleteReferences(refs);
        // (the models selected with them are deleted from the script as it is now)
        QList<QJsonObject> again;
        for (const QString& k : keys)
        {
            const QJsonObject o = itemByKey(k);
            if (!o.isEmpty()) again << o;
        }
        if (!again.isEmpty()) deleteItems(again);
        return;
    }
    deleteItems(items);
}

namespace {

// `text` with the name `from` made `to` wherever it is a name: not a keyword argument (`f(from=1)`), an attribute (`.from`), part of
// another name, or inside a string
QString renamedIn(const QString& text, const QString& from, const QString& to)
{
    const QRegularExpression re("(?<![\\w.])" + QRegularExpression::escape(from) + "(?!\\w)");
    QString out;
    int last = 0;
    for (auto m = re.globalMatch(text); m.hasNext(); )
    {
        const auto hit = m.next();
        const int a = hit.capturedStart(), b = hit.capturedEnd();
        // (a keyword argument: after "(" or ",", before a single "=")
        int before = a - 1;
        while (before >= 0 && text[before] == ' ') --before;
        int after = b;
        while (after < text.size() && text[after] == ' ') ++after;
        const bool keyword = before >= 0 && (text[before] == '(' || text[before] == ',') && after < text.size() &&
                             text[after] == '=' && !(after + 1 < text.size() && text[after + 1] == '=');
        // (inside a string: an odd number of quotes of one kind before it on its line)
        const int lineStart = text.lastIndexOf('\n', a) + 1;
        const QString head = text.mid(lineStart, a - lineStart);
        const bool inString = head.count('"') % 2 == 1 || head.count('\'') % 2 == 1;
        if (keyword || inString) continue;
        out += text.mid(last, a - last) + to;
        last = b;
    }
    return out + text.mid(last);
}

}   // namespace

// The lines of a model's statements as they are in the script: its definition, the line that shows or hides it, its handles(),
// expose(), render_cache() and lock() lines
QString ScenePanel::groupText(const QJsonObject& it) const
{
    QStringList out;
    for (const auto& r : groupRanges(it, false))
        for (int l = r.first; l <= r.second; ++l) out << lineText(l);
    return out.join('\n');
}

// A free name for a copy of `name`: its number counted up (`box_1`: `box_2`), or a number added to it
QString ScenePanel::copyName(const QString& name, const QSet<QString>& taken, bool part) const
{
    static const QRegularExpression numbered(R"(^(.*)_(\d+)$)");
    const auto m = numbered.match(name);
    const QString base = (m.hasMatch() && !part) ? m.captured(1) : name;
    int n = (m.hasMatch() && !part) ? m.captured(2).toInt() + 1 : 2;
    const QString src = m_source ? m_source() : QString();
    for (;; ++n)
    {
        const QString candidate = base + "_" + QString::number(n);
        if (taken.contains(candidate)) continue;
        if (QRegularExpression("\\b" + QRegularExpression::escape(candidate) + "\\b").match(src).hasMatch()) continue;
        return candidate;
    }
}

// The models that Ctrl+D / Ctrl+C work on: the selected ones that are defined by a statement of their own (an import is
// imported again by importing the file, not by copying its statement)
QList<QJsonObject> ScenePanel::copyableModels() const
{
    QList<QJsonObject> out;
    for (const Model& m : selectedModels())
    {
        if (!m.item.contains("var") || m.item["kind"].toString() == "import" || m.item["failed"].toBool()) continue;
        out << m.item;
    }
    return out;
}

void ScenePanel::duplicateSelected()
{
    const QList<QJsonObject> items = copyableModels();
    if (items.isEmpty()) { notify(T("Select a model to duplicate first.")); return; }
    if (m_editPending && m_editClock.elapsed() < 20000)
    {
        m_afterRun = [=] { duplicateSelected(); };             // (the script is being edited to get the models ready: the scene is a run behind)
        return;
    }
    const QString src = m_source ? m_source() : QString();
    const QStringList lines = src.split('\n');
    QSet<QString> taken;
    QList<QPair<QString, QString>> renames;                       // old name, new name
    for (const auto& it : items)
    {
        const QString old = it["var"].toString();
        const QString fresh = copyName(old, taken, it.contains("part_of"));
        taken << fresh;
        renames << qMakePair(old, fresh);
    }
    QList<TextEdit> edits;
    QStringList made;
    for (int i = 0; i < items.size(); ++i)
    {
        QString text = groupText(items[i]);
        for (const auto& r : renames) text = renamedIn(text, r.first, r.second);
        const int at = groupEnd(items[i]);
        if (at < lines.size()) edits << TextEdit{at, 0, at, 0, text + "\n"};
        else edits << TextEdit{int(lines.size()) - 1, int(lines.last().size()), int(lines.size()) - 1, int(lines.last().size()),
                               "\n" + text};
        made << renames[i].second;
    }
    m_selectNew = made.last();
    m_selectNewTries = 0;
    emit(editScript(edits, "Duplicate " + made.join(", ")));
}

void ScenePanel::copySelected()
{
    const QList<QJsonObject> items = copyableModels();
    if (items.isEmpty()) { notify(T("Select a model to copy first.")); return; }
    if (m_editPending && m_editClock.elapsed() < 20000)
    {
        m_afterRun = [=] { copySelected(); };                  // (the lines selecting a model writes are not in its statements yet)
        return;
    }
    m_copied.clear();
    m_copiedPart.clear();
    QStringList all;
    for (const auto& it : items)
    {
        if (it.contains("part_of")) m_copiedPart << it["var"].toString();         // (a part of an import: its number is not a copy count)
        m_copied << qMakePair(it["var"].toString(), groupText(it));
        all << m_copied.last().second;
    }
    m_copiedText = all.join("\n");
    QApplication::clipboard()->setText(m_copiedText);
    notify((items.size() == 1 ? T("Copied %1 model: Ctrl+V pastes it.") : T("Copied %1 models: Ctrl+V pastes them.")).arg(items.size()));
}

void ScenePanel::pasteModels()
{
    // (what the tree copied, unless the clipboard has been given something else since: that is what the user means to paste)
    auto plain = [](QString t) { return t.remove('\r').trimmed(); };
    const QString now = plain(QApplication::clipboard()->text());
    if (m_copied.isEmpty())
    {
        notify(T("Nothing copied from the model tree: select models and press Ctrl+C first."));
        return;
    }
    if (!now.isEmpty() && now != plain(m_copiedText))
    {
        notify(T("The clipboard holds something else than the models copied from the tree: copy them again (Ctrl+C) to paste them."));
        return;
    }
    const QString src = m_source ? m_source() : QString();
    const QStringList lines = src.split('\n');
    // (the copies go under the last selected model; with none selected, at the end of the script)
    QJsonObject after;
    const QList<Model> picked = selectedModels();
    if (!picked.isEmpty()) after = picked.last().item;
    if (m_editPending && m_editClock.elapsed() < 20000)
    {
        m_afterRun = [=] { pasteModels(); };
        return;
    }
    QSet<QString> taken;
    QList<QPair<QString, QString>> renames;
    for (const auto& c : m_copied)
    {
        // (a name the script does not have yet -- a copy pasted into another script -- is kept)
        const bool exists = QRegularExpression("\\b" + QRegularExpression::escape(c.first) + "\\b").match(src).hasMatch();
        const QString name = exists || taken.contains(c.first) ? copyName(c.first, taken, m_copiedPart.contains(c.first)) : c.first;
        taken << name;
        renames << qMakePair(c.first, name);
    }
    QStringList blocks;
    for (const auto& c : m_copied)
    {
        QString text = c.second;
        for (const auto& r : renames) text = renamedIn(text, r.first, r.second);
        blocks << text;
    }
    const QString text = blocks.join("\n");
    static const QRegularExpression starImport(R"(^\s*from\s+fieldes\s+import\s+\*)", QRegularExpression::MultilineOption);
    const QString star = starImport.match(src).hasMatch() ? QString() : QString("from fieldes import *\n\n");
    QList<TextEdit> edits;
    if (!after.isEmpty() && after.contains("end_line"))
    {
        const int at = groupEnd(after);
        if (at < lines.size()) edits << TextEdit{at, 0, at, 0, text + "\n"};
        else edits << TextEdit{int(lines.size()) - 1, int(lines.last().size()), int(lines.size()) - 1, int(lines.last().size()),
                               "\n" + text};
        if (!star.isEmpty()) edits << TextEdit{0, 0, 0, 0, star};
    }
    else
    {
        int last = int(lines.size()) - 1;
        while (last >= 0 && lines[last].trimmed().isEmpty()) --last;
        if (last < 0)
        {
            // (an empty script: the import of the library, then the models)
            edits << TextEdit{0, 0, int(lines.size()) - 1, int(lines.last().size()), star + text + "\n"};
        }
        else
        {
            edits << TextEdit{last, int(lines[last].size()), last, int(lines[last].size()), "\n\n" + text};
            if (!star.isEmpty()) edits << TextEdit{0, 0, 0, 0, star};
        }
    }
    m_selectNew = renames.last().second;
    m_selectNewTries = 0;
    emit(editScript(edits, "Paste " + renames.last().second));
}

void ScenePanel::deleteReferences(const QList<QPair<QJsonObject, QString>>& refs)
{
    for (const auto& r : refs)
    {
        if (!r.first["stale"].toBool()) continue;
        m_afterRun = [=] { deleteReferences(refs); };           // (its inputs are only known once the script has run)
        return;
    }
    Rewire w;
    QStringList names, from;
    for (const auto& r : refs)
    {
        const QJsonObject model = itemBefore(r.second, r.first["line"].toInt());
        if (model.isEmpty()) continue;
        wireRemove(w, r.first, model);
        if (!names.contains(r.second)) names << r.second;
        if (!from.contains(r.first["var"].toString())) from << r.first["var"].toString();
    }
    if (w.edits.isEmpty()) return;
    const QString what = T("Take %1 out of %2").arg(names.join(", "), from.join(", "));
    const QStringList before = (m_source ? m_source() : QString()).split('\n');
    QStringList lines = before;
    QVector<int> origin(lines.size());
    for (int i = 0; i < origin.size(); ++i) origin[i] = i;
    QStringList targets;
    QHash<QString, int> newLength;
    QString why;
    if (!wireStage(w, lines, &targets, &newLength, &why))
    {
        notify(why);
        return;
    }
    wireDropGone(lines, origin);
    applyLines(before, lines, what, origin, QList<ColShift>(), targets, newLength);
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
    if (deferItems(items, "deleting", [this](const QList<QJsonObject>& f) { deleteItems(f); })) return;
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
    // Every line that goes is checked to be the line the rows say it is (see lineIs): rows of another text than the script's would
    // have a definition, a lock or a handles line of ANOTHER model deleted.  Nothing is written when one is not
    m_lineMismatch.clear();
    for (const auto& o : all)
    {
        if (o.contains("var") || o.contains("list_var"))
        {
            const QString var = o.contains("var") ? o["var"].toString() : o["list_var"].toString();
            lineIs(o["line"].toInt() - 1, var, "definition");
            if (o.contains("display_line")) lineIs(o["display_line"].toInt() - 1, var, "display");
            if (o.contains("hidden_line")) lineIs(o["hidden_line"].toInt() - 1, var, "hidden");
            static const QList<QPair<const char*, const char*>> blocks = {
                {"handles", "handles"}, {"exposed", "expose"}, {"cache", "render_cache"}, {"cache_off", "render_cache"},
                {"locked", "lock"}, {"custom_resolution", "custom_resolution"}};
            for (const auto& b : blocks)
            {
                if (o.contains(b.first)) lineIs(o[b.first].toObject()["line"].toInt() - 1, var, QString::fromLatin1(b.second));
            }
        }
        else if (o["kind"].toString() == "display")
        {
            lineIsExpression(o["line"].toInt() - 1, o);
        }
        if (!m_lineMismatch.isEmpty())
        {
            refuseOutOfDate();
            return;
        }
    }
    QList<QPair<int, int>> ranges;                  // 0-based, inclusive
    QStringList names;
    for (const auto& o : all)
    {
        ranges << qMakePair(o["line"].toInt() - 1, o["end_line"].toInt() - 1);
        if (o.contains("display_line")) ranges << qMakePair(o["display_line"].toInt() - 1, o["display_line"].toInt() - 1);
        if (o.contains("hidden_line")) ranges << qMakePair(o["hidden_line"].toInt() - 1, o["hidden_line"].toInt() - 1);
        for (const char* key : {"handles", "exposed", "cache", "cache_off", "locked", "custom_resolution"})
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

    // The models that are made of what is deleted keep their statements: the input that is gone is a placeholder where it was written
    // (`move(..., v=(5, 0, 0))`), or is taken out of the list it was in -- nothing is left that names a model that is not there
    Rewire rewire;
    QList<QPair<int, int>> rewritten;                // (the statements that are rewritten: the lines that use the names are not "users" any more)
    {
        QSet<QString> goneKeys;
        for (const auto& o : all) goneKeys << keyOf(o);
        for (const auto v : m_scene["items"].toArray())
        {
            const auto c = v.toObject();
            if (goneKeys.contains(keyOf(c)) || c["failed"].toBool()) continue;
            // (every place the call is given the model: its arguments, the ones it is given by keyword -- `boundaries=[a, b]`, `material=m` -- and
            // what it is used in inside an expression)
            QStringList given;
            for (const auto iv : c["inputs"].toArray()) given << iv.toObject()["name"].toString();
            bool uses = false;
            for (const auto& o : all)
            {
                if (!o.contains("var") || !given.contains(o["var"].toString())) continue;
                wireRemove(rewire, c, o);
                uses = true;
            }
            if (uses) rewritten << qMakePair(c["line"].toInt() - 1, c["end_line"].toInt() - 1);
        }
    }

    // Does the rest of the script still use what is deleted?
    const QStringList lines = m_source ? m_source().split('\n') : QStringList();
    QStringList users;
    for (int L = 0; L < lines.size(); ++L)
    {
        bool gone = false;
        for (const auto& r : merged) gone |= (L >= r.first && L <= r.second);
        for (const auto& r : rewritten) gone |= (L >= r.first && L <= r.second);
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
    const QString what = whats.size() <= 3 ? whats.join(", ") : T("%1 models").arg(whats.size());
    if (!users.isEmpty() && !qEnvironmentVariableIsSet("FIELDES_AUTOMATION"))
    {
        QMessageBox box(QMessageBox::Question, T("Delete"), T("Delete %1?").arg(what), QMessageBox::Yes | QMessageBox::Cancel, this);
        box.setInformativeText((users.size() > 1 ? T("The script still uses %1 on lines %2: those lines will fail until you edit them.")
                                                 : T("The script still uses %1 on line %2: those lines will fail until you edit them."))
                                   .arg(names.join(", "), users.mid(0, 8).join(", ") + (users.size() > 8 ? ", ..." : "")));
        box.setDefaultButton(QMessageBox::Cancel);
        if (box.exec() != QMessageBox::Yes) return;
    }

    if (!rewire.edits.isEmpty())
    {
        // One edit: the models that were made of what goes are rewritten, and the lines of what goes are dropped
        const QStringList before = m_source ? m_source().split('\n') : QStringList();
        QStringList staged = before;
        QVector<int> origin(staged.size());
        for (int i = 0; i < origin.size(); ++i) origin[i] = i;
        QStringList targets;
        QHash<QString, int> newLength;
        QString why;
        if (!wireStage(rewire, staged, &targets, &newLength, &why))
        {
            notify(why);
            return;
        }
        for (const auto& r : merged)
            for (int L = r.first; L <= r.second && L < staged.size(); ++L) staged[L] = kGone;
        wireDropGone(staged, origin);
        applyLines(before, staged, "Delete " + what, origin, QList<ColShift>(), targets, newLength);
        return;
    }
    QList<TextEdit> edits;
    for (const auto& r : merged) edits << deleteLines(r.first, r.second);
    // The rows go at once: the lines of the statements are gone, every line under them has moved up, and what an operation that is
    // deleted was made of is on its own again (the prediction works that out from the lines).  The run follows
    editFollowed(edits, "Delete " + what, [](QJsonObject&, const QVector<int>&) {});
}

void ScenePanel::removeHandles(const QJsonObject& target)
{
    if (deferItems({target}, T("removing its handles"), [this](const QList<QJsonObject>& f) { removeHandles(f[0]); })) return;
    if (!target.contains("handles")) return;
    const auto h = target["handles"].toObject();
    if (!lineIs(h["line"].toInt() - 1, target["var"].toString(), "handles")) { refuseOutOfDate(); return; }
    emit(editScript({deleteLines(h["line"].toInt() - 1, h["end_line"].toInt() - 1)},
                    "Remove the handles of " + target["var"].toString()));
}

void ScenePanel::reimportPart(const QJsonObject& imp, int part)
{
    if (deferItems({imp}, T("importing the part again"), [this, part](const QList<QJsonObject>& f) { reimportPart(f[0], part); })) return;
    // What was done to the part in the script is undone (its handles() and expose() lines are deleted)...
    QList<TextEdit> edits;
    const QJsonObject target = handlesTarget(imp, part);
    for (const char* key : {"handles", "exposed"})
    {
        if (!target.contains(key)) continue;
        const auto h = target[key].toObject();
        if (!lineIs(h["line"].toInt() - 1, target["var"].toString(), QString::fromLatin1(key[0] == 'h' ? "handles" : "expose")))
        {
            refuseOutOfDate();
            return;
        }
        edits << deleteLines(h["line"].toInt() - 1, h["end_line"].toInt() - 1);
    }
    // ...and the file is read again: the import call's rev= goes up (see reimport)
    reimportEdit(imp, edits);
    if (edits.isEmpty()) return;
    emit(editScript(edits, QString("Reimport part %1 of %2").arg(part).arg(imp["label"].toString())));
}

void ScenePanel::resetImport(const QJsonObject& imp, bool ask)
{
    if (deferItems({imp}, T("resetting the import"), [this, ask](const QList<QJsonObject>& f) { resetImport(f[0], ask); })) return;
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
        QString what = T("Delete the cache of %1 and import it afresh?").arg(label);
        QString more = QFileInfo::exists(cacheFile) || QDir(treesDir).exists()
            ? T("The import cache is deleted and rebuilt on the next run (this can take a while).")
            : T("There is no cache; the file is imported afresh.");
        if (!handled.isEmpty())
        {
            more += T("\n\nThe handle edits of %1 part(s) are removed from the script.").arg(handled.size());
        }
        QMessageBox box(QMessageBox::Question, T("Reset import"), what, QMessageBox::Yes | QMessageBox::Cancel, this);
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
            if (!lineIs(h["line"].toInt() - 1, t["var"].toString(), QString::fromLatin1(key[0] == 'h' ? "handles" : "expose")))
            {
                refuseOutOfDate();
                return;                         // (before the cache is deleted: nothing has been done)
            }
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

// The menu of the row of an import: how the file is imported (the function the statement calls), and the row's own buttons
void ScenePanel::importMenu(const QJsonObject& it, const QPoint& global)
{
    auto menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->setToolTipsVisible(true);
    const QString current = it["func"].toString();
    struct Way { const char* func; QString title; QString tip; };
    static const Way ways[] = {
        {"import_model", T("Import (automatic)"),
         T("Each part is reconstructed as exact CSG, or tessellated if more of its surface is free-form (B-spline) than the "
         "threshold says.")},
        {"reconstruct", T("Reconstruct"),
         T("Every part rebuilt as CSG from its faces: planes, cylinders... exact, free-form faces fitted (fast, light, "
         "draggable).")},
        {"tessellate", T("Tessellate"),
         T("Every part as the exact triangles of its faces, made a distance field (any free-form shape, any thin wall).")},
    };
    auto way = menu->addMenu(T("Import as"));
    way->setToolTipsVisible(true);
    for (const Way& w : ways)
    {
        const QString func = w.func;
        auto a = way->addAction(w.title, this, [=] {
            const auto c = it["call"].toArray();
            if (c.size() != 4 || func == current) return;
            QList<TextEdit> edits;
            // (the function's name at the start of the call: its other arguments -- the file, rev=, units= -- stay)
            edits << TextEdit{c[0].toInt() - 1, c[1].toInt(), c[0].toInt() - 1, c[1].toInt() + int(current.size()), func};
            emit(editScript(edits, "Import " + it["label"].toString() + " as " + func));
        });
        a->setCheckable(true);
        a->setChecked(func == current);
        a->setToolTip(w.tip);
    }
    menu->addSeparator();
    menu->addAction(T("Reimport the file"), this, [=] { reimport(it); });
    menu->addAction(T("Reset (clear the cache and handle edits)"), this, [=] { resetImport(it, true); });
    menu->addAction(T("Delete from the script"), this, [=] { deleteItem(it); });
    menu->popup(global);
}

void ScenePanel::reimport(const QJsonObject& it)
{
    if (deferItems({it}, T("importing the file again"), [this](const QList<QJsonObject>& f) { reimport(f[0]); })) return;
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
    if (deferItems({imp}, T("showing the part"), [this, part](const QList<QJsonObject>& f) { showPart(f[0], part); })) return;
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
    m_selectNew = name;                 // (it is a model of the script now, and the selected one: ready to be dragged)
    m_selectNewTries = 0;
    emit(editScript(edits, "Add part " + QString::number(part)));
}

void ScenePanel::switchPart(const QJsonObject& imp, int part)
{
    if (deferItems({imp}, T("using the part"), [this, part](const QList<QJsonObject>& f) { switchPart(f[0], part); })) return;
    if (!imp.contains("index_span")) return;
    const auto s = imp["index_span"].toArray();
    QList<TextEdit> edits;
    edits << TextEdit{s[0].toInt() - 1, s[1].toInt(), s[2].toInt() - 1, s[3].toInt(),
                      QString::number(part)};
    emit(editScript(edits, "Use part " + QString::number(part)));
}

void ScenePanel::focusOn(const QJsonObject& it)
{
    // (a point is not drawn, and its box is a few pixels wide: the camera would go in until nothing else is in view)
    if (it["type"].toString() == "point") return;
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

void ScenePanel::expandTo(QTreeWidgetItem* row)
{
    // (a row inside a collapsed model is shown by opening the models it is inside)
    for (auto p = row ? row->parent() : nullptr; p; p = p->parent())
    {
        if (!p->isExpanded()) p->setExpanded(true);
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
    if (type == "bcgroup")
    {
        // (the conditions of a simulation: the code shows the simulation's call; nothing is carried to the view)
        emit(goToLine(it["line"].toInt() - 1));
        emit(highlightLines(linesOf(it)));
        return;
    }
    if (type == "hole")
    {
        // (the three dots in the code, selected: what goes there is written in the code editor)
        const auto holes = it["holes"].toArray();
        const int k = row->data(COL_NAME, ROLE_PART).toInt();
        if (k >= 0 && k < holes.size())
        {
            const auto span = holes[k].toObject()["span"].toArray();
            if (span.size() >= 2)
            {
                emit(placeholderRequested(span[0].toInt() - 1, span[1].toInt()));
                emit(highlightLines({}));
                return;
            }
        }
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
        // A shadow stays selected itself (the delete button and D take its reference out of the call): the code shows the
        // call it is in, and the model it stands for is lit up
        const QJsonObject ref = itemBefore(row->data(COL_NAME, ROLE_PART).toString(), it["line"].toInt());
        emit(goToLine(it["line"].toInt() - 1));
        emit(highlightLines(ref.isEmpty() ? QList<int>() : linesOf(ref)));
        if (focus && !ref.isEmpty()) focusOn(ref);
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
    // (a click on the eye or the lock changes that row's button at once: the script that answers it may take a while to run
    // again, and nothing else in the tree changes)
    auto flipEye = [&](QTreeWidgetItem* r) {
        const bool now = !r->data(COL_EYE, Qt::UserRole).toBool();
        r->setData(COL_EYE, Qt::UserRole, now);
        r->setIcon(COL_EYE, eyeIcon(now));
        r->setToolTip(COL_EYE, now ? T("Hide") : T("Show"));
        if (now) r->setData(COL_NAME, Qt::ForegroundRole, QVariant());
        else r->setForeground(COL_NAME, kDim);
    };
    if (column == COL_EYE && !row->icon(COL_EYE).isNull())
    {
        if (type == "part")
        {
            const int k = row->data(COL_NAME, ROLE_PART).toInt();
            const auto p = it["parts"].toArray()[k].toObject();
            // (a part that is a model of the script is shown or hidden; one that is not is added to the script)
            const bool toggles = p.contains("var") || (it.contains("index") && it["index"].toInt() == k);
            showPart(it, k);
            if (toggles) flipEye(row);
        }
        else if (type == "bcgroup")
        {
            // "boundary conditions": every condition of the simulation is shown -- or, when they are all shown, hidden
            QList<QJsonObject> conditions;
            QSet<QString> seen;
            bool all = true;
            for (int k = 0; k < row->childCount(); ++k)
            {
                auto c = row->child(k);
                const QString ct = c->data(COL_NAME, ROLE_TYPE).toString();
                QJsonObject m;
                if (ct == "item") m = c->data(COL_NAME, ROLE_ITEM).toJsonObject();
                else if (ct == "dep")
                    m = itemBefore(c->data(COL_NAME, ROLE_PART).toString(), c->data(COL_NAME, ROLE_ITEM).toJsonObject()["line"].toInt());
                if (!m.contains("var") || m["failed"].toBool() || (m.contains("displayable") && !m["displayable"].toBool())) continue;
                if (seen.contains(keyOf(m))) continue;
                seen.insert(keyOf(m));
                conditions << m;
                all = all && m["visible"].toBool();
            }
            if (conditions.isEmpty()) return;
            flipEye(row);
            setVisibleAll(conditions, !all);
        }
        else
        {
            // (the button first: the edit makes the tree follow it, and the rows are made again a moment later)
            flipEye(row);
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
        const QJsonObject target = type == "part" ? handlesTarget(it, row->data(COL_NAME, ROLE_PART).toInt()) : it;
        if (target.contains("var") && m_pressedSelection.size() >= 2 &&
            m_pressedSelection.contains(row->data(COL_NAME, ROLE_KEY).toString()))
        {
            // A row that was one of several selected when its button was pressed: the button is for all of them (to lock or unlock a
            // handful of parts at once, as R does), and they stay selected
            const bool lock = !target.contains("locked");
            QList<QJsonObject> targets;
            QList<QTreeWidgetItem*> again;
            for (QTreeWidgetItemIterator i(m_tree); *i; ++i)
            {
                if (!m_pressedSelection.contains((*i)->data(COL_NAME, ROLE_KEY).toString())) continue;
                QJsonObject model = modelOfRow(*i);
                if (model.isEmpty() || !model.contains("var") || model["kind"].toString() == "display") continue;
                targets << model;
                again << *i;
                QJsonObject now = model;
                if (lock) now["locked"] = QJsonObject();
                else now.remove("locked");
                setLockButton(*i, now);
            }
            for (auto r : again) m_tree->setCurrentItem(r, 0, QItemSelectionModel::Select | QItemSelectionModel::Rows);
            m_pressedSelection.clear();
            applyLock(targets, lock);
            return;
        }
        if (target.contains("var") && target["kind"].toString() != "display")
        {
            QJsonObject now = target;
            if (now.contains("locked")) now.remove("locked");
            else now["locked"] = QJsonObject();
            setLockButton(row, now);
        }
        toggleLock(target);
        return;
    }
    if (column == COL_CACHE && !row->icon(COL_CACHE).isNull())
    {
        const QJsonObject target = type == "part" ? handlesTarget(it, row->data(COL_NAME, ROLE_PART).toInt()) : it;
        if (target.contains("var") && target["kind"].toString() != "display")
        {
            QJsonObject now = target;
            if (now.contains("cache_off")) now.remove("cache_off");
            else now["cache_off"] = QJsonObject();
            setCacheButton(row, now);
        }
        toggleCache(target);
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
    // A right-click opens the menu of the viewport: the one a right-click on the model of the row opens there (the model
    // of a shadow row, or of a part of an import, is that model), and in the empty space under the rows the menu of empty
    // space.  Everything else the old menu did has its own key or button on the row
    const QPoint global = m_tree->viewport()->mapToGlobal(pos);
    auto row = m_tree->itemAt(pos);
    if (!row)
    {
        emit menuRequested(-1, global);
        return;
    }
    const QString type = row->data(COL_NAME, ROLE_TYPE).toString();
    QJsonObject model = row->data(COL_NAME, ROLE_ITEM).toJsonObject();
    if (type == "dep")
    {
        model = itemForVar(row->data(COL_NAME, ROLE_PART).toString());
    }
    else if (type == "part")
    {
        const int k = row->data(COL_NAME, ROLE_PART).toInt();
        const auto p = model["parts"].toArray()[k].toObject();
        if (p.contains("var") && !itemForVar(p["var"].toString()).isEmpty()) model = itemForVar(p["var"].toString());
        else
        {
            // A part that is not in the script yet has no model to make a menu of: it can be added (it is then a model like
            // any other, with its menu and its place in the tree), or read from the file again
            const QJsonObject imp = model;
            auto menu = new QMenu(this);
            menu->setAttribute(Qt::WA_DeleteOnClose);
            menu->setToolTipsVisible(true);
            auto add = menu->addAction(imp.contains("index") ? T("Use this part") : T("Add to the script"), this, [=] {
                if (imp.contains("index")) switchPart(imp, k);
                else showPart(imp, k);
            });
            add->setEnabled(p["ok"].toBool());
            add->setToolTip(imp.contains("index") ? T("The import statement takes this part instead")
                                                  : T("Writes `name = %1[%2][0]` and shows it: a model like any other")
                                                        .arg(imp["list_var"].toString()).arg(k));
            if (isStepImport(imp) && p["ok"].toBool())
                menu->addAction(T("Reimport this part"), this, [=] { reimportPart(imp, k); });
            menu->popup(global);
            return;
        }
    }
    else if (type != "item") return;
    if (!model.isEmpty() && model["kind"].toString() == "import" && isStepImport(model))
    {
        importMenu(model, global);
        return;
    }
    if (model.isEmpty() || model["failed"].toBool() || model["kind"].toString() == "failed") return;
    if (!model.contains("var") && model["kind"].toString() != "display") return;       // (the render settings and the like)
    emit menuRequested(model["line"].toInt() - 1, global);
}

bool ScenePanel::modelAtLine(int line0, QVector3D* centre) const
{
    QJsonObject model;
    QString source;
    int after = 0;
    if (!resolveModel(line0, &model, &source, &after)) return false;
    if (centre)
    {
        const auto b = model["bounds"].toArray();
        *centre = b.size() == 2
            ? QVector3D(float((b[0].toArray()[0].toDouble() + b[1].toArray()[0].toDouble()) / 2),
                        float((b[0].toArray()[1].toDouble() + b[1].toArray()[1].toDouble()) / 2),
                        float((b[0].toArray()[2].toDouble() + b[1].toArray()[2].toDouble()) / 2))
            : QVector3D();
    }
    return true;
}

QString ScenePanel::typeAtLine(int line0) const
{
    QJsonObject model;
    QString source;
    int after = 0;
    if (!resolveModel(line0, &model, &source, &after)) return QString();
    return model["type"].toString();
}

bool ScenePanel::describeModel(const QJsonObject& target, QString* source, int* after) const
{
    if (target.isEmpty()) return false;
    int aft = target["end_line"].toInt() - 1;
    QString src;
    if (target.contains("var"))
    {
        src = target["var"].toString();
        // (the lines that go on making the model after its statement: what a new statement under it has to stay below -- the numbers that
        // drag it, its gizmo, its lock: `c = lock(c)` is the model that is used, not the one before it)
        for (const char* key : {"exposed", "handles", "locked"})
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

bool ScenePanel::ownsLine(const QJsonObject& o, int line0) const
{
    // A model's own lines: the line that displays it (what a click in the viewport gives), its statement, and the lines that make
    // it draggable (expose, handles) -- what a click in the text editor or in the model tree gives is one of those
    if (linesOf(o).contains(line0)) return true;
    if (line0 >= o["line"].toInt() - 1 && line0 <= o["end_line"].toInt() - 1) return true;
    for (const char* key : {"exposed", "handles"})
    {
        if (!o.contains(key)) continue;
        const auto block = o[key].toObject();
        const int first = block.contains("line") ? block["line"].toInt() - 1 : block["end_line"].toInt() - 1;
        if (line0 >= first && line0 <= block["end_line"].toInt() - 1) return true;
    }
    return false;
}

bool ScenePanel::resolveModel(int line0, QJsonObject* model, QString* source, int* after) const
{
    // The model that is displayed on that line: a variable (the shape, a part of an import) or an expression
    QJsonObject target;
    for (const auto v : m_scene["items"].toArray())
    {
        const auto o = v.toObject();
        if (o["failed"].toBool() || o["kind"].toString() == "failed") continue;
        if (ownsLine(o, line0) && (o.contains("var") || o["kind"].toString() == "display"))
        {
            target = o;
            break;
        }
    }
    if (!describeModel(target, source, after)) return false;
    *model = target;
    return true;
}

// "Custom resolution" from a model's menu: the line that gives the model a resolution of its own goes under its definition (and under the
// other lines that edit it), with the scene's resolution as its number, so that nothing changes until the number is typed over
void ScenePanel::addCustomResolution(int line0)
{
    QJsonObject target;
    QString source;
    int after = 0;
    if (!resolveModel(line0, &target, &source, &after)) return;
    if (!target.contains("var"))
    {
        notify(T("Give the model a name first (a variable): the resolution is a line under its definition."));
        return;
    }
    const QString var = target["var"].toString();
    if (target.contains("custom_resolution"))
    {
        notify(T("%1 has a resolution of its own already: it is the row under it in the tree.").arg(var));
        return;
    }
    if (!lineIs(target["line"].toInt() - 1, var, "definition")) { refuseOutOfDate(); return; }
    // the scene's resolution, as the number to start from
    double res = m_scene["settings"].toObject()["resolution"].toObject()["value"].toDouble(0.0);
    if (!(res > 0)) res = 10.0;
    int last = target["end_line"].toInt() - 1;
    for (const char* key : {"exposed", "handles", "locked", "cache", "cache_off"})
    {
        if (target.contains(key)) last = std::max(last, target[key].toObject()["end_line"].toInt() - 1);
    }
    const QString indent = indentOf(lineText(target["line"].toInt() - 1));
    const QString lastText = lineText(last);
    QList<TextEdit> edits;
    edits << TextEdit{last, int(lastText.size()), last, int(lastText.size()),
                      "\n" + indent + var + " = custom_resolution(" + var + ", " + QString::number(res, 'g', 3) + ")"};
    addImportIfMissing(edits, "custom_resolution", "custom_resolution");
    emit(editScript(edits, "Custom resolution " + var));
}

void ScenePanel::addSurfaceSelection(int line0, QVector3D seed, QString mode, double angle, double radius)
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
        expandTo(r);
        m_tree->setCurrentItem(r);          // (the selection is this row alone)
        m_tree->scrollToItem(r);
        select(r, false);
    }
}

void ScenePanel::toggleByLine(int line0)
{
    auto r = rowForLine(line0);
    if (!r) return;
    expandTo(r);
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
            expandTo(r);
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

bool ScenePanel::lockedInMulti() const
{
    // A selection of several models that has a locked one in it: it can be selected and operated on, but not moved or edited
    const auto models = selectedModels();
    if (models.size() < 2) return false;
    for (const Model& m : models)
    {
        if (m.item.contains("locked")) return true;
    }
    return false;
}

void ScenePanel::noteLockedInMulti(bool warn)
{
    const bool now = lockedInMulti();
    if (now && !m_lockedInMulti && warn) QTimer::singleShot(0, this, [this]{ warnLockedMultiSelect(); });
    m_lockedInMulti = now;
    updateMultiNote();
}

void ScenePanel::warnLockedMultiSelect()
{
    QSettings store;
    if (store.value("hidden-messages/locked-in-multi-select", false).toBool()) return;
    const QString text = T("When a locked object is part of a multi select, the multi selection can no longer be moved or edited.");
    if (qEnvironmentVariableIsSet("FIELDES_AUTOMATION"))
    {
        fprintf(stderr, "[warning] %s\n", text.toUtf8().constData());
        return;
    }
    QMessageBox box(QMessageBox::Warning, T("Locked shapes"), text, QMessageBox::Ok, this);
    box.setInformativeText(T("It can still be unlocked (R, or a lock button) and combined, hidden or deleted. Unlock the locked "
                           "ones and the selection can be moved again."));
    auto again = new QCheckBox(T("Do not show this message again"));
    box.setCheckBox(again);
    box.exec();
    if (again->isChecked()) store.setValue("hidden-messages/locked-in-multi-select", true);
}

void ScenePanel::updateMultiNote()
{
    // The multi-select state is not an edit mode of any model: the line under the tree says what it is.  So does a locked
    // model that is selected on its own: nothing appears to drag, and the reason is that it is locked
    const auto models = selectedModels();
    const int n = models.size();
    int movable = 0;                            // (a condition has no gizmo: it is not moved with the others)
    for (const Model& m : models) movable += m.item["no_handles"].toBool() ? 0 : 1;
    QString mine;
    if (n >= 2 && lockedInMulti())
    {
        int locked = 0;
        for (const Model& m : models) locked += m.item.contains("locked") ? 1 : 0;
        mine = T("%1 models selected, %2 locked: a selection with a locked model cannot be moved or edited. Unlock "
                       "them (R) and one gizmo moves them all.").arg(n).arg(locked);
    }
    else if (movable >= 2)
    {
        mine = T("%1 models selected: one gizmo moves them all. Each keeps its own gizmo mode (E) for "
                       "when it is selected alone.").arg(movable);
    }
    else if (n == 1 && models[0].item.contains("locked") && models[0].item.contains("var"))
    {
        mine = T("%1 is locked, so it has no gizmo and its surfaces cannot be dragged. Unlock it with its lock button (or R).")
                   .arg(models[0].item["var"].toString());
    }
    else if (n == 1 && !noDragReason(models[0].item, false).isEmpty())
    {
        // (the model that is selected and cannot be pulled by its surfaces says why, as its orange dot does)
        mine = models[0].item["var"].toString() + ": " + noDragReason(models[0].item, false);
    }
    if (!mine.isEmpty())
    {
        if (m_note->text().isEmpty() || m_multiNote)
        {
            m_multiNote = true;
            m_note->setText(mine);
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

void ScenePanel::prepareNow()
{
    m_prepareTimer.stop();
    prepareSelection();
}

void ScenePanel::updateFieldView()
{
    // The selected models that are fields: the field viewer shows them (a field has no body to draw: it has a value at every point,
    // and the viewer colours a disc by that value); with several, a menu in it chooses the one shown
    QStringList keys;
    for (const auto& m : selectedModels())
    {
        const QJsonObject& t = m.item;
        if (t["type"].toString() == "field" && !t["failed"].toBool())
            keys << (t.contains("var") ? t["var"].toString() : "line:" + QString::number(t["line"].toInt()));
    }
    const QString joined = keys.join('|');
    if (joined == m_fieldKeyShown) return;
    m_fieldKeyShown = joined;
    emit(fieldsSelected(keys));
}

void ScenePanel::updateProvisional()
{
    // One selected model that is shown, can have a gizmo, and has no numbers for it yet: the view draws its gizmo at once, from
    // where the model is (the middle of its box, which is where the real one will be)
    const auto models = selectedModels();
    if (models.size() == 1)
    {
        const QJsonObject t = models[0].item;
        const bool numbers = t.contains("handles") && t["handles"].toObject()["has_numbers"].toBool();
        const auto b = t["bounds"].toArray();
        if (!numbers && b.size() == 2 && t["visible"].toBool() && !t["failed"].toBool() && !t["reassigned"].toBool() &&
            !t["no_handles"].toBool() && !t.contains("locked") && t["mode"].toString("click") != "never" && !linesOf(t).isEmpty())
        {
            const auto lo = b[0].toArray(), hi = b[1].toArray();
            const QVector3D pivot(float(lo[0].toDouble() + hi[0].toDouble()) / 2, float(lo[1].toDouble() + hi[1].toDouble()) / 2,
                                  float(lo[2].toDouble() + hi[2].toDouble()) / 2);
            emit(provisionalGizmo(true, pivot, linesOf(t)));
            return;
        }
    }
    emit(provisionalGizmo(false, QVector3D(), QList<int>()));
}

void ScenePanel::prepareSelection()
{
    // The selected models are made ready to be dragged.  Pulling a shape's surfaces is always there, so a model that has
    // no numbers to pull them by gets them -- an `expose(x, [...])` line -- when they are no more than kAutoExposeNumbers (a bigger one
    // has an orange dot in the tree and is moved by its gizmo).  The gizmo is shown by its mode (click: while the model is selected) and
    // moves what has numbers to move it
    // by -- a `handles(x, move=(var, ...))` line, which a model that has none gets too, in the mode it is in (click, when
    // it has no line).  Nothing else about the model changes
    const auto models = selectedModels();
    // (a selection of several with a locked model in it is not moved or edited: nothing is written for it)
    if (models.isEmpty() || lockedInMulti())
    {
        // (nothing to prepare: what was waiting for the scene goes ahead)
        if (m_afterRun)
        {
            const auto later = m_afterRun;
            m_afterRun = nullptr;
            QTimer::singleShot(0, this, later);
        }
        return;
    }
    if (m_editPending && m_editClock.elapsed() < 20000)
    {
        m_prepareAgain = true;                  // (the scene is out of date until the run of the last edit is done)
        return;
    }
    // The tree is a prediction: an edit of its buttons has been followed, and the run that answers it is not done.  What is written now
    // would make the rows wrong again (the lines it adds are not in them) and the next click would wait for the run; it is written
    // when the scene is there, as it is for every run (setScene starts this again)
    if (m_predicted)
    {
        m_prepareAgain = true;
        return;
    }
    QList<TextEdit> edits;
    QStringList names;
    QStringList reselect;           // (what is selected after the run: an expression that is named is another model)
    bool naming = false;
    ModeImports imports;

    // Whether a model is made ready: it is shown (a model that is not shown has nothing to drag, and its numbers stay as they
    // are for the models made of it), and it has no gizmo line with numbers or no way of pulling its surfaces yet
    const QString allText = m_source ? m_source() : QString();
    auto prepares = [&](const QJsonObject& t, bool* gizmoOut, bool* exposeOut) {
        if (t["kind"].toString() == "display" || t["failed"].toBool() || t["reassigned"].toBool() ||
            t["no_handles"].toBool() || !t.contains("var") || t.contains("locked") || !t["visible"].toBool() ||
            t["type"].toString() == "field")                // (a field is not drawn: it has nothing to drag)
            return false;
        const QString var = t["var"].toString();
        const bool hasHandles = t.contains("handles");
        bool needGizmo = !(hasHandles && t["handles"].toObject()["has_numbers"].toBool());
        // (the scene may be a run behind the text: a line that is there already is never written twice)
        if (needGizmo && !hasHandles &&
            QRegularExpression(QString("^\\s*%1\\s*=\\s*handles\\(\\s*%1\\b").arg(QRegularExpression::escape(var)),
                               QRegularExpression::MultilineOption).match(allText).hasMatch())
            needGizmo = false;
        bool needExpose = !t["has_var"].toBool() && !t.contains("exposed") && t["can_expose"].toBool() &&
                          t["expose_count"].toInt() <= kAutoExposeNumbers && !m_exposeTried.contains(keyOf(t));
        if (needExpose &&
            QRegularExpression(QString("^\\s*%1\\s*=\\s*expose\\(\\s*%1\\b").arg(QRegularExpression::escape(var)),
                               QRegularExpression::MultilineOption).match(allText).hasMatch())
            needExpose = false;
        *gizmoOut = needGizmo;
        *exposeOut = needExpose;
        return needGizmo || needExpose;
    };
    // A model made of another has that one's numbers among its own: when the other gets numbers of its own (an expose() and a
    // gizmo line), an expose() line of the model made of it no longer fits its numbers.  That line goes -- what it is made of
    // has the numbers to pull its surfaces by -- and a model made of it is not given one
    QSet<QString> madeOfPrepared, dropped;
    for (const Model& m : models)
    {
        bool g = false, e = false;
        if (!prepares(m.item, &g, &e)) continue;
        for (const auto v : m_scene["items"].toArray())
        {
            const auto d = v.toObject();
            if (d.contains("var") && d["var"].toString() != m.item["var"].toString() && dependsOn(d, m.item))
                madeOfPrepared.insert(keyOf(d));
        }
    }
    for (const auto v : m_scene["items"].toArray())
    {
        const auto d = v.toObject();
        if (!madeOfPrepared.contains(keyOf(d)) || !d.contains("exposed") || dropped.contains(keyOf(d))) continue;
        const auto ex = d["exposed"].toObject();
        edits << deleteLines(ex["line"].toInt() - 1, ex["end_line"].toInt() - 1);
        dropped.insert(keyOf(d));
        names << d["var"].toString();
    }

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
        bool needGizmo = false, needExpose = false;
        if (!prepares(t, &needGizmo, &needExpose)) continue;
        if (madeOfPrepared.contains(keyOf(t))) needExpose = false;
        if (!needGizmo && !needExpose) continue;
        const QString var = t["var"].toString();
        const bool hasHandles = t.contains("handles");
        const bool hasExposed = t.contains("exposed") && !dropped.contains(keyOf(t));
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
            const int after = hasExposed ? t["exposed"].toObject()["end_line"].toInt() - 1 : b;
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
    if (m_rebuilding) return;
    noteLockedInMulti(true);
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
    updateProvisional();
    updateFieldView();
    // (once the selection has settled: a rectangle selects its models one after the other)
    if (!selectedModels().isEmpty() && !m_treeHeld) m_prepareTimer.start();
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

QList<ScenePanel::Model> ScenePanel::operandsFor(int line0, bool fallback) const
{
    const QList<Model> picked = selectedModels();
    if (line0 < 0)
    {
        // (empty space: the selected models, else the last one the script defines)
        if (!picked.isEmpty() || !fallback) return picked;
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
            if (o["group"].toString() == "Simulations") m_simulations.insert(o["name"].toString());
            m_combining[o["name"].toString()] = o["other"].toBool();
            m_needsSecond[o["name"].toString()] = o["needs_other"].toBool(o["other"].toBool());
        }
        for (const auto v : doc.object()["primitives"].toArray())
        {
            const auto p = v.toObject();
            if (p["with_bodies"].toBool()) m_withBodies.insert(p["name"].toString());
        }
    }
    return m_combining.value(operation, false);
}

bool ScenePanel::pointsOnly(int line0) const
{
    const QList<Model> operands = operandsFor(line0, false);
    if (operands.isEmpty()) return false;
    for (const Model& m : operands)
    {
        if (m.item["type"].toString() != "point") return false;
    }
    return true;
}

bool ScenePanel::isSimulation(const QString& operation) const
{
    combines(operation);            // (reads the interpreter's list the first time)
    return m_simulations.contains(operation);
}

bool ScenePanel::takesBodies(const QString& primitive) const
{
    combines(primitive);            // (reads the interpreter's list the first time)
    return m_withBodies.contains(primitive);
}

bool ScenePanel::needsSecond(const QString& operation) const
{
    combines(operation);            // (reads the interpreter's list the first time)
    return m_needsSecond.value(operation, false);
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
            expandTo(*i);
            m_tree->setCurrentItem(*i);
            m_tree->scrollToItem(*i);
            select(*i, false);
            return true;
        }
    }
    // (a part of an import that is a model of the script is a row under its file: that row stands for it)
    for (QTreeWidgetItemIterator i(m_tree); *i; ++i)
    {
        if ((*i)->data(COL_NAME, ROLE_TYPE).toString() != "part") continue;
        const auto imp = (*i)->data(COL_NAME, ROLE_ITEM).toJsonObject();
        const auto parts = imp["parts"].toArray();
        const int k = (*i)->data(COL_NAME, ROLE_PART).toInt();
        if (k >= 0 && k < parts.size() && parts[k].toObject()["var"].toString() == var)
        {
            expandTo(*i);
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

bool ScenePanel::entryAllowed(const QString& kind, const QString& name, int line0) const
{
    if (!m_support) return true;
    // The request of createFromMenu for this entry and this selection (what the library writes it from), without writing anything
    const bool operation = kind == "operation";
    const QList<Model> picked = (kind == "primitive" && takesBodies(name)) ? operandsFor(line0) : QList<Model>();
    const auto regionKind = [](const QJsonObject& it) {
        const QString type = it["type"].toString();
        return type == "surface" || type == "field";
    };
    const bool regionOnly = picked.size() == 1 && regionKind(picked[0].item) && (line0 >= 0 || !selectedModels().isEmpty());
    const bool withBodies = picked.size() >= 2 || regionOnly;
    if (!operation && !withBodies) return true;             // (no model is needed for it, or none is selected)
    const QList<Model> operands = operandsFor(line0, !(operation && isSimulation(name)));
    if (operands.isEmpty()) return true;                    // (no model to work on: the menu says so itself -- a simulation writes placeholders)
    const bool combining = withBodies || combines(name);
    QJsonObject bodyItem = operands[0].item;
    QString body = operands[0].source, other;
    QJsonArray othersInfo;
    if (regionOnly)
    {
        QJsonObject info;
        info["name"] = operands[0].source;
        info["type"] = operands[0].item["type"].toString();
        info["role"] = QString();
        info["cls"] = operands[0].item["cls"].toString();
        othersInfo.append(info);
        other = operands[0].source;
        body.clear();
        bodyItem = QJsonObject();
    }
    if (combining && operands.size() >= 2)
    {
        QStringList names;
        for (int i = 1; i < operands.size(); ++i)
        {
            names << operands[i].source;
            QJsonObject info;
            info["name"] = operands[i].source;
            info["type"] = operands[i].item["type"].toString();
            info["role"] = operands[i].item["condition_role"].toString();
            info["cls"] = operands[i].item["cls"].toString();
            othersInfo.append(info);
        }
        other = names.join(", ");
    }
    else if (combining && needsSecond(name))
    {
        return true;                                        // (a second model is what it needs: the menu says that itself)
    }
    QJsonObject request;
    request["kind"] = kind;
    request["name"] = name;
    request["x"] = 0;
    request["y"] = 0;
    request["z"] = 0;
    request["scale"] = 1;
    request["body"] = body;
    request["other"] = other;
    if (!othersInfo.isEmpty()) request["others"] = othersInfo;
    if (!bodyItem.isEmpty())
    {
        QJsonObject info;
        info["type"] = bodyItem["type"].toString();
        info["role"] = bodyItem["condition_role"].toString();
        info["cls"] = bodyItem["cls"].toString();
        info["part"] = bodyItem["part_name"].toString();
        request["body_info"] = info;
    }
    request["var"] = "x";
    if (bodyItem["bounds"].toArray().size() == 2)
    {
        request["lo"] = bodyItem["bounds"].toArray()[0];
        request["hi"] = bodyItem["bounds"].toArray()[1];
    }
    QString error;
    const QString call = m_support("menu_call", QString::fromUtf8(QJsonDocument(request).toJson(QJsonDocument::Compact)), &error);
    return !call.isEmpty();
}

bool ScenePanel::shownAtLine(int line1) const
{
    for (const auto v : m_scene["items"].toArray())
    {
        const auto o = v.toObject();
        if (o["line"].toInt() == line1 && o.contains("var") && o["kind"].toString() != "import") return o["visible"].toBool();
    }
    return true;
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

// Whether a statement can be written under this (0-based) line of the script without cutting anything in two: the line is not
// indented, does not open a block or end in a continuation, and leaves no bracket open
static bool canInsertAfter(const QStringList& lines, int line0)
{
    const QString own = lines[line0];
    if (!own.isEmpty() && own[0].isSpace()) return false;
    const QString trimmed = own.trimmed();
    if (trimmed.endsWith(':') || trimmed.endsWith('\\') || trimmed.endsWith(',')) return false;
    int depth = 0;
    QChar quote;
    for (int i = 0; i <= line0; ++i)
    {
        const QString& s = lines[i];
        for (int k = 0; k < s.size(); ++k)
        {
            const QChar c = s[k];
            if (!quote.isNull())
            {
                if (c == '\\') ++k;
                else if (c == quote) quote = QChar();
                continue;
            }
            if (c == '#') break;
            if (c == '"' || c == '\'') quote = c;
            else if (c == '(' || c == '[' || c == '{') ++depth;
            else if (c == ')' || c == ']' || c == '}') --depth;
        }
        quote = QChar();                    // (a string that does not end on its line is not looked into)
    }
    return depth <= 0;
}

void ScenePanel::createFromMenu(QString kind, QString name, QVector3D point, double scale, int line0, int generation)
{
    if (!m_support) return;
    const bool operation = kind == "operation";
    // A support, a load or another condition of a simulation, when several models are selected: the first is the body, the others are
    // where the condition is -- made of them like an operation (with one model selected, or none, it has a placeholder where it acts)
    const QList<Model> picked = (kind == "primitive" && takesBodies(name)) ? operandsFor(line0) : QList<Model>();
    // ONE model that is a surface (a patch picked on a part) or a field (a distance, a ramp ...) is where the condition is: it is passed in as
    // the region, there is no body to name -- the condition acts where the part meets it.  (One body, or none: a placeholder)
    const auto regionKind = [](const QJsonObject& it) {
        const QString type = it["type"].toString();
        return type == "surface" || type == "field";
    };
    const bool regionOnly = picked.size() == 1 && regionKind(picked[0].item) && (line0 >= 0 || !selectedModels().isEmpty());
    const bool withBodies = picked.size() >= 2 || regionOnly;
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
        m_afterRun = [=] {
            createFromMenu(kind, name, point, scale,
                           (operation && !selectedModels().isEmpty()) || (withBodies && !regionOnly && selectedModels().size() >= 2) ? -1 : line0);
        };
        return;
    }
    const QString src = m_source ? m_source() : QString();
    QJsonObject target;
    QString body;
    int after = 0;
    QString other;
    QJsonObject bodyItem;           // (the model it is made for: the first selected; its extent is what a simulation is sized by)
    QJsonArray othersInfo;          // (what each of the others is: the menu writes a region, a condition or a material differently)
    bool bare = false;              // (a simulation with nothing selected: it is written at the end of the script, whole of placeholders)
    QStringList lockedNames;        // (the selected models that are locked: read-only, so the new statement holds a reference to each, as a drag does)
    if (kind == "operation" || withBodies)
    {
        // What it works on: the model that was right-clicked, or the selected ones, in the order they were selected
        const QList<Model> operands = operandsFor(line0, !(operation && isSimulation(name)));
        for (const Model& m : operands)
        {
            if (m.item.contains("locked") && m.item.contains("var")) lockedNames << m.item["var"].toString();
        }
        if (operands.isEmpty() && operation && isSimulation(name))
        {
            // (a simulation of nothing that is selected: every input is a placeholder, written at the end of the script)
            bare = true;
        }
        else if (operands.isEmpty())
        {
            notify(T("There is no model to apply %1 to.").arg(name));
            return;
        }
        const bool combining = withBodies || combines(name);
        if (!bare)
        {
            bodyItem = operands[0].item;
            target = operands[0].item;
            body = operands[0].source;
            after = operands[0].after;
        }
        if (regionOnly)
        {
            // (the one model is where the condition is: it goes in as the region, and there is no body)
            QJsonObject info;
            info["name"] = operands[0].source;
            info["type"] = operands[0].item["type"].toString();
            info["role"] = QString();
            info["cls"] = operands[0].item["cls"].toString();
            othersInfo.append(info);
            other = operands[0].source;
            body.clear();
            bodyItem = QJsonObject();
        }
        if (bare)
        {
            // (nothing is selected: no body, no other models)
        }
        else if (combining && operands.size() >= 2)
        {
            // Several models are selected: the first is what the operation works on, the others are what it
            // combines it with (union: all of them together; difference: the others taken from the first).  It is
            // written under the model that is defined last, so that every name it uses exists by then
            QStringList others;
            Model last = operands[0];
            for (int i = 0; i < operands.size(); ++i)
            {
                if (i > 0)
                {
                    others << operands[i].source;
                    QJsonObject info;
                    info["name"] = operands[i].source;
                    info["type"] = operands[i].item["type"].toString();
                    info["role"] = operands[i].item["condition_role"].toString();
                    info["cls"] = operands[i].item["cls"].toString();
                    othersInfo.append(info);
                }
                if (operands[i].after > last.after) last = operands[i];
            }
            other = others.join(", ");
            target = last.item;
            after = last.after;
        }
        else if (combining && needsSecond(name))
        {
            // (one model: the operation takes the last other model of the script; one that takes others only if there are some,
            // like surface_from_bodies, is written with the one model)
            for (const auto v : m_scene["items"].toArray())
            {
                const auto o = v.toObject();
                if (o["failed"].toBool() || o["reassigned"].toBool() || o["kind"].toString() == "failed") continue;
                if (o.contains("var") && o["var"].toString() != target["var"].toString()) other = o["var"].toString();
            }
            if (other.isEmpty())
            {
                notify(T("%1 needs a second model.").arg(name));
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
    if (!othersInfo.isEmpty()) request["others"] = othersInfo;
    if (!bodyItem.isEmpty())
    {
        // (what the first model selected is: a set of boundary conditions made for a part is the conditions of an analysis of that part)
        QJsonObject info;
        info["type"] = bodyItem["type"].toString();
        info["role"] = bodyItem["condition_role"].toString();
        info["cls"] = bodyItem["cls"].toString();
        info["part"] = bodyItem["part_name"].toString();
        request["body_info"] = info;
    }
    // (the variable the model will get: a function written above the call is named after it)
    const QString var = freeName(name);
    request["var"] = var;
    // (how far the statement is indented: the library lays the call out for that column)
    request["indent"] = ((kind == "operation" || withBodies) && !bare) ? int(indentOf(lineText(target["line"].toInt() - 1)).size()) : 0;
    const QJsonObject sized = bodyItem.isEmpty() ? target : bodyItem;
    if (sized["bounds"].toArray().size() == 2)             // (what a simulation lays its supports and loads on)
    {
        request["lo"] = sized["bounds"].toArray()[0];
        request["hi"] = sized["bounds"].toArray()[1];
    }
    QString error;
    QString call = m_support("menu_call", QString::fromUtf8(QJsonDocument(request).toJson(QJsonDocument::Compact)),
                             &error);
    if (call.isEmpty())
    {
        notify(T("Could not create %1").arg(name) + (error.isEmpty() ? QString() : ": " + error));
        return;
    }
    // The rest: the statement written under the model
    auto finish = [=](QString call) {
        // (a template may write a function above the call, for the user to put their own logic in: see menu_catalog.PRELUDE)
        static const QString preludeMark = "\n#@@#\n";
        const int cut = call.indexOf(preludeMark);
        const QString prelude = cut >= 0 ? call.left(cut) : QString();
        QString statement = (cut >= 0 ? prelude + "\n" : QString()) + var + " = " + (cut >= 0 ? call.mid(cut + preludeMark.size()) : call);
        // (a locked model the call is made of is held by reference: `# shadow: name` on the statement's last line, so it stays where it is
        // and is not made part of the call)
        for (const QString& locked : lockedNames)
        {
            if (!QRegularExpression("\\b" + QRegularExpression::escape(locked) + "\\b").match(call).hasMatch()) continue;
            const int cutAt = statement.lastIndexOf('\n');
            statement = (cutAt >= 0 ? statement.left(cutAt + 1) : QString()) + markShadow(statement.mid(cutAt + 1), locked);
        }

        // The library's functions are in scope with `from fieldes import *`
        static const QRegularExpression starImport(R"(^\s*from\s+fieldes\s+import\s+\*)", QRegularExpression::MultilineOption);
        const QString star = starImport.match(src).hasMatch() ? QString() : QString("from fieldes import *\n\n");

        QList<TextEdit> edits;
        if ((kind == "operation" || withBodies) && !bare)
        {
            // Under the definition of the model, with a line showing the result
            const QString indent = indentOf(lineText(target["line"].toInt() - 1));
            const QString last = lineText(after);
            edits << TextEdit{after, int(last.size()), after, int(last.size()),
                              "\n" + indent + statement + "\n" + indent + var};
            if (!star.isEmpty()) edits << TextEdit{0, 0, 0, 0, star};
        }
        else if (line0 >= 0 && line0 < src.split('\n').size() && canInsertAfter(src.split('\n'), line0))
        {
            // (a menu of the text editor opened on a line that is no model: what it makes goes under that line)
            const QString at = src.split('\n')[line0];
            edits << TextEdit{line0, int(at.size()), line0, int(at.size()), "\n" + statement + "\n" + var};
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
    };
    // (nothing is asked: the call is written with every argument in it -- what it needs as a placeholder, the rest with its defaults -- and
    // changed in the code editor)
    finish(call);
}

void ScenePanel::toggleIsolation()
{
    if (deferEdit([this] { toggleIsolation(); }, T("isolating the selected models"))) return;
    // What is selected now: the models that stay shown (all the selected ones), and their names
    QSet<QString> keep;
    QStringList names;
    QStringList selectedKeys;
    QList<QTreeWidgetItem*> rows = m_tree->selectedItems();
    if (rows.isEmpty() && m_tree->currentItem()) rows << m_tree->currentItem();
    for (auto row : rows)
    {
        const QString type = row->data(COL_NAME, ROLE_TYPE).toString();
        selectedKeys << row->data(COL_NAME, ROLE_KEY).toString();
        // (a model: a variable, or an expression that is shown.  The row of a part that is in the script stands for its model, as
        // for every other key; a file that was imported -- the assembly -- is not one: its parts are)
        const QJsonObject it = (type == "item" || type == "part") ? modelOfRow(row) : QJsonObject();
        if (!it.isEmpty())
        {
            keep << keyOf(it);
            names << (it.contains("var") ? it["var"].toString() : it["label"].toString());
        }
    }
    selectedKeys.sort();
    const QString selectedKey = selectedKeys.join('|');
    const QString name = names.join(", ");

    // Isolated already, and the same model still selected (or nothing to isolate): everything shown before comes back
    const bool restore = m_isolated && (keep.isEmpty() || selectedKey == m_isolatedKey);
    if (!restore && keep.isEmpty())
    {
        notify(T("Select a model in the tree, then press I to show only it."));
        return;
    }
    QList<TextEdit> edits;
    const QSet<QString> shown = restore ? m_isolateRestore : keep;
    QSet<QString> visibleNow;
    m_lineMismatch.clear();
    for (const auto v : m_scene["items"].toArray())
    {
        const auto o = v.toObject();
        const QString kind = o["kind"].toString();
        if (o["failed"].toBool() || kind == "failed") continue;
        if (!(o.contains("var") || kind == "display")) continue;
        if (o["visible"].toBool()) visibleNow << keyOf(o);
        visibilityEdit(o, shown.contains(keyOf(o)), edits);
    }
    // (a line that is not the one the rows say: nothing is written, and the isolation is not begun or ended)
    if (!m_lineMismatch.isEmpty()) { refuseOutOfDate(); return; }
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
    {
        m_prepareAgain = true;      // (the model that is shown now is made ready to be dragged -- its gizmo line, the numbers of its surfaces)
        emit(editScript(edits, restore ? QString("Show everything again") : "Isolate " + name));
    }
    else
        rebuild();      // (nothing to change in the script: the note only)
}

////////////////////////////////////////////////////////////////////////////////
// Drag and drop, and the rows as text

QString ScenePanel::dumpRows() const
{
    QString out;
    std::function<void(QTreeWidgetItem*, int)> walk = [&](QTreeWidgetItem* r, int depth) {
        const QString type = r->data(COL_NAME, ROLE_TYPE).toString();
        const auto it = r->data(COL_NAME, ROLE_ITEM).toJsonObject();
        out += QString(depth * 2, ' ') + r->text(COL_NAME);
        if (!r->icon(COL_DOT).isNull()) out += "  [orange dot: " + r->toolTip(COL_DOT) + "]";
        if (type == "item")
        {
            const QString t = it["type"].toString();
            if (!t.isEmpty()) out += "  [" + t + (it.contains("block") ? "+block" : "") + "]";
            if (!(r->flags() & Qt::ItemIsDragEnabled)) out += " (fixed)";
        }
        else if (type == "dep") out += "  [uses]";
        else if (type == "hole") out += "  [placeholder]";
        else if (type == "bcgroup")
            out += QString("  [conditions of %1, eye %2]").arg(it["var"].toString(), r->icon(COL_EYE).isNull() ? "none" : r->data(COL_EYE, Qt::UserRole).toBool() ? "on" : "off");
        out += "\n";
        for (int k = 0; k < r->childCount(); ++k) walk(r->child(k), depth + 1);
    };
    for (int k = 0; k < m_tree->topLevelItemCount(); ++k) walk(m_tree->topLevelItem(k), 0);
    if (!m_note->text().isEmpty()) out += "note: " + m_note->text() + "\n";
    return out;
}

void ScenePanel::debugShiftLines(int delta, bool blocks)
{
    QJsonArray items = m_scene["items"].toArray();
    for (int i = 0; i < items.size(); ++i)
    {
        QJsonObject o = items[i].toObject();
        if (!blocks)
        {
            for (const char* key : {"display_line", "hidden_line"})
            {
                if (o.contains(key)) o[key] = o[key].toInt() + delta;
            }
        }
        else
        {
            // (the lines of the statements that edit a model: lock, handles, expose, the render cache, its resolution)
            for (const char* key : {"locked", "handles", "exposed", "cache", "cache_off", "custom_resolution"})
            {
                if (!o.contains(key)) continue;
                QJsonObject b = o[key].toObject();
                b["line"] = b["line"].toInt() + delta;
                b["end_line"] = b["end_line"].toInt() + delta;
                o[key] = b;
            }
        }
        items[i] = o;
    }
    m_scene["items"] = items;
    rebuild();
}

QString ScenePanel::dumpState() const
{
    QString out;
    for (QTreeWidgetItemIterator i(m_tree); *i; ++i)
    {
        QTreeWidgetItem* r = *i;
        if (r->data(COL_NAME, ROLE_TYPE).toString() != "item") continue;
        const auto it = r->data(COL_NAME, ROLE_ITEM).toJsonObject();
        if (!it.contains("var")) continue;
        out += QString("%1 line=%2-%3 display=%4 hidden=%5 visible=%6 locked=%7 cache_off=%8 mode=%9 | eye=%10 lock=%11 gizmo=%12\n")
                   .arg(it["var"].toString())
                   .arg(it["line"].toInt()).arg(it["end_line"].toInt())
                   .arg(it["display_line"].toInt()).arg(it["hidden_line"].toInt())
                   .arg(it["visible"].toBool() ? 1 : 0)
                   .arg(it.contains("locked") ? it["locked"].toObject()["line"].toInt() : 0)
                   .arg(it.contains("cache_off") ? it["cache_off"].toObject()["line"].toInt() : 0)
                   .arg(it["mode"].toString())
                   .arg(r->data(COL_EYE, Qt::UserRole).toBool() ? "on" : "off")
                   .arg(r->toolTip(COL_LOCK).startsWith("Locked") ? "locked" : "unlocked")
                   .arg(r->toolTip(COL_HANDLES).section(" ", 1, 2));
    }
    out += QString("state: predicted=%1 pending=%2 partial=%3 stale=%4\n")
               .arg(m_predicted ? 1 : 0).arg(m_rebuildPending ? 1 : 0).arg(0).arg(sceneIsStale() ? 1 : 0);
    return out;
}

QList<QJsonObject> ScenePanel::draggedModels() const
{
    // The selected models that can be moved, in the order they were selected
    QHash<QString, QJsonObject> byKey;
    for (auto row : m_tree->selectedItems())
    {
        const QString type = row->data(COL_NAME, ROLE_TYPE).toString();
        if ((type != "item" && type != "part") || !(row->flags() & Qt::ItemIsDragEnabled)) continue;
        // (the row of a part of an import that is a model of the script stands for that model)
        const QJsonObject model = type == "part" ? modelOfRow(row) : row->data(COL_NAME, ROLE_ITEM).toJsonObject();
        if (model.isEmpty()) continue;
        byKey[row->data(COL_NAME, ROLE_KEY).toString()] = model;
    }
    QList<QJsonObject> out;
    for (const QString& k : m_selectOrder)
    {
        if (byKey.contains(k)) out << byKey.take(k);
    }
    QList<QJsonObject> rest = byKey.values();
    std::sort(rest.begin(), rest.end(), [](const QJsonObject& a, const QJsonObject& b) { return a["line"].toInt() < b["line"].toInt(); });
    return out + rest;
}

bool ScenePanel::takesInput(const QJsonObject& m) const
{
    // A model made by a call that is given models (or any number of them) can be given another
    if (!m.contains("var") || m["failed"].toBool() || m["kind"].toString() == "failed" || m["kind"].toString() == "import")
        return false;
    return !m["inputs"].toArray().isEmpty() || m["variadic"].toBool();
}

bool ScenePanel::takesNumber(const QJsonObject& m) const
{
    if (!m.contains("var") || m["failed"].toBool() || m["kind"].toString() == "failed" || m["kind"].toString() == "import")
        return false;
    return !m["numbers"].toArray().isEmpty();
}

bool ScenePanel::takesPoint(const QJsonObject& m) const
{
    if (!m.contains("var") || m["failed"].toBool() || m["kind"].toString() == "failed" || m["kind"].toString() == "import")
        return false;
    return !m["points"].toArray().isEmpty();
}

QJsonObject ScenePanel::itemBefore(const QString& var, int line) const
{
    QJsonObject best;
    for (const auto v : m_scene["items"].toArray())
    {
        const auto o = v.toObject();
        if (o["var"].toString() == var && o["line"].toInt() < line) best = o;
    }
    return best;
}

bool ScenePanel::dependsOn(const QJsonObject& model, const QJsonObject& on) const
{
    QSet<QString> seen;
    std::function<bool(const QJsonObject&)> walk = [&](const QJsonObject& m) {
        for (const auto d : m["deps"].toArray())
        {
            const QJsonObject dep = itemBefore(d.toString(), m["line"].toInt());
            if (dep.isEmpty() || seen.contains(keyOf(dep))) continue;
            if (keyOf(dep) == keyOf(on)) return true;
            seen.insert(keyOf(dep));
            if (walk(dep)) return true;
        }
        return false;
    };
    return walk(model);
}

ScenePanel::OnPlan ScenePanel::planFill(const QList<QJsonObject>& models, const QJsonObject& stmt, int which) const
{
    // The statement has a placeholder (`...`) where an argument goes: the model dropped takes its place, whatever it is (a body, a
    // surface, a field, a point ...: the run says if it does not fit).  Nothing is asked
    OnPlan p;
    QStringList names;
    for (const auto& m : models) names << m["var"].toString();
    const QString sname = stmt["var"].toString();
    const QJsonArray holes = stmt["holes"].toArray();
    if (which < 0 || which >= holes.size())
    {
        p.why = T("'%1' has no placeholder to put a model in.").arg(sname);
        return p;
    }
    const QJsonObject hole = holes[which].toObject();
    const bool listed = hole["list"].toBool();           // (an input that takes conditions: one, or a list of them)
    const QString slot = hole["param"].toString();
    if (listed)
    {
        for (const auto& m : models)
        {
            if (m["type"].toString() != "conditions")
            {
                p.why = T("%1 takes conditions: '%2' is not one. Make one of it first (the Simulation menu: New support or load, New thermal condition, "
                          "New flow condition).").arg(slot, m["var"].toString());
                return p;
            }
            if (m["condition_role"].toString() != slot)
            {
                const QString role = m["condition_role"].toString();
                p.why = T("'%1' goes in %2=, not in %3=.").arg(m["var"].toString(), role.isEmpty() ? T("another input") : role, slot);
                return p;
            }
        }
    }
    else if (models.size() > 1)
    {
        p.why = T("A placeholder takes one model: drop one of %1 on it.").arg("'" + names.join("', '") + "'");
        return p;
    }
    for (const auto& m : models)
    {
        if (m["var"].toString() == sname || dependsOn(m, stmt))
        {
            p.why = T("'%1' is made of '%2', so it cannot also be one of its arguments: that would be a loop.").arg(m["var"].toString(), sname);
            return p;
        }
    }
    p.ok = true;
    p.replace = holes[which].toObject()["span"].toArray();
    p.text = listed ? "[" + names.join(", ") + "]" : names[0];
    p.what = T("Use %1 in place of a placeholder of %2").arg(names.join(", "), sname);
    return p;
}

ScenePanel::OnPlan ScenePanel::planDropOn(const QList<QJsonObject>& models, const QJsonObject& target) const
{
    // What a drop ON an operation does: never a question.  A model becomes one more argument of an operation that takes any number
    // of them; an operation that works on exactly one model gives its place to the dropped one; a field takes the place of the
    // first plain number of the call.  Anything else cannot be done, and the reason is given
    OnPlan p;
    QStringList names;
    for (const auto& m : models) names << m["var"].toString();
    const QString tname = target["var"].toString();
    const QString fn = target["callee"].toString();
    const QString fnText = fn.isEmpty() ? T("its call") : fn + "()";
    const QString quoted = "'" + names.join("', '") + "'";
    for (const auto& m : models)
    {
        if (dependsOn(m, target))
        {
            p.why = T("'%1' is made of '%2', directly or through other models, so it cannot also be "
                    "one of its arguments: that would be a loop ('%2' needs '%1' to be built, and '%1' would need '%2').")
                        .arg(m["var"].toString(), tname);
            return p;
        }
    }
    // (dropped on a statement that has placeholders: a model takes the first of them.  Conditions go in the inputs of their kinds -- the
    // placeholders among them -- one kind at a time, or all at once)
    bool onlyConditions = !models.isEmpty();
    for (const auto& m : models) onlyConditions = onlyConditions && m["type"].toString() == "conditions";
    if (target["hole"].toBool() && !onlyConditions) return planFill(models, target, 0);
    const QJsonArray inputs = target["inputs"].toArray();
    if (regionDrop(target, models))
    {
        const QJsonObject callSlots = target["slots"].toObject();
        const QJsonObject given = callSlots["args"].toObject();
        // The region: a condition takes any number of them, one argument after the other, so the models dropped are more arguments
        // (`fixed(a)` + b -> `fixed(a, b)`).  The box the menu made for it (`fixed_1_region`) is swapped for them
        const QJsonArray regs = target["region_args"].toArray();
        QStringList written;
        for (const auto rv : regs) written << rv.toObject()["text"].toString().trimmed();
        for (const QString& n : names)
        {
            for (const QString& w : written)
            {
                if (QRegularExpression("\\b" + QRegularExpression::escape(n) + "\\b").match(w).hasMatch())
                {
                    p.why = T("'%1' is already part of the region of '%2' (%3), so dropping it here changes nothing.").arg(n, tname, w);
                    return p;
                }
            }
        }
        static const QRegularExpression identifier(R"(^[A-Za-z_]\w*$)");
        p.ok = true;
        if (regs.size() == 1 && identifier.match(written[0]).hasMatch() && written[0].endsWith("_region"))
        {
            p.replace = regs[0].toObject()["span"].toArray();
            p.text = names.join(", ");
            p.what = T("Use %1 as the region of %2 (in place of its box %3)").arg(names.join(", "), tname, written[0]);
        }
        else
        {
            p.after = regs.last().toObject()["span"].toArray();
            p.text = names.join(", ");
            p.what = T("Add %1 to the regions of %2").arg(names.join(", "), tname);
        }
        return p;
    }
    // A material or a condition goes where an analysis asks for one: in place of the one it is given, or as the argument it is
    // not given yet (`material=`, `conditions=`, or the list of supports and loads).  Never a question
    {
        const auto allOfType = [&](const char* type) {
            for (const auto& m : models) if (m["type"].toString() != type) return false;
            return !models.isEmpty();
        };
        const QJsonObject callSlots = target["slots"].toObject();
        const QJsonObject given = callSlots["args"].toObject();
        const auto hasParam = [&](const QString& name) {
            for (const auto pv : callSlots["params"].toArray()) if (pv.toString() == name) return true;
            return false;
        };
        // (written in place of the argument the call has, or as a keyword after its last argument)
        const auto place = [&](const QString& param, const QString& text, const QString& what) {
            const QJsonObject cur = given[param].toObject();
            p.ok = true;
            if (!cur.isEmpty()) p.replace = cur["span"].toArray();
            else p.after = callSlots["last"].toArray();
            p.text = cur.isEmpty() ? param + "=" + text : text;
            p.what = what;
            return p;
        };
        if (allOfType("material"))
        {
            if (models.size() > 1)
            {
                p.why = T("An analysis is made of one material (or one fluid): drop one of %1 on it.").arg(quoted);
                return p;
            }
            const QString param = hasParam("material") ? "material" : hasParam("fluid") ? "fluid" : QString();
            if (param.isEmpty())
            {
                p.why = T("'%1' is made by %2, which is not given a material or a fluid: the analyses are "
                        "(static_analysis(part, supports, loads, material=steel), fluid_analysis(body, domain, inlets, outlets, boundaries, fluid=water)).")
                            .arg(tname, fnText);
                return p;
            }
            if (given[param].toObject()["text"].toString() == names[0])
            {
                p.why = T("'%1' is already the %2 of '%3', so dropping it here changes nothing.").arg(names[0], T(param), tname);
                return p;
            }
            return place(param, names[0], T("Use %1 as the %2 of %3").arg(names[0], T(param), tname));
        }
        if (allOfType("conditions"))
        {
            // Every kind of condition is an input of its own, named like the kind (supports, loads, inlets ...: the model says which in
            // its `condition_role`).  The conditions go into the input of their kind, as a list -- all of them at once when they are of
            // one kind, an input that is a placeholder or has one item or a list written in the call alike -- and conditions of several
            // kinds each go to theirs, in the one edit: the simulation takes them all, or the drop says why it cannot
            QStringList roles;
            QHash<QString, QStringList> namesOf;
            for (const auto& m : models)
            {
                const QString r = m["condition_role"].toString();
                if (!roles.contains(r)) roles << r;
                namesOf[r] << m["var"].toString();
            }
            struct Put { QString role, text, list; QJsonArray replace, after; };
            QList<Put> puts;
            QString nothing;                                // (why a kind adds nothing: all of it is in the call already)
            for (const QString& role : roles)
            {
                if (role.isEmpty() || !hasParam(role))
                {
                    p.why = T("'%1' is made by %2, which has no input for %3: they go in an analysis that takes them.")
                                .arg(tname, fnText, role.isEmpty() ? T("conditions") : role);
                    return p;
                }
                const QJsonObject cur = given[role].toObject();
                const QString curText = cur["text"].toString().trimmed();
                QStringList have;                           // (what the input holds already: a list written in the call, or one name)
                if (curText.startsWith('[') && curText.endsWith(']') && !curText.contains('('))
                    for (const QString& h : curText.mid(1, curText.size() - 2).split(',', Qt::SkipEmptyParts)) have << h.trimmed();
                else if (!curText.isEmpty() && curText != "...")
                    have << curText;
                QStringList added;
                for (const QString& n : namesOf[role])
                    if (!have.contains(n)) added << n;
                if (added.isEmpty())
                {
                    nothing = T("%1 is already in the %2 of '%3', so dropping it here changes nothing.")
                                  .arg("'" + namesOf[role].join("', '") + "'", role, tname);
                    continue;
                }
                Put put;
                put.role = role;
                put.list = added.join(", ");
                if (cur.isEmpty())
                {
                    put.after = callSlots["last"].toArray();
                    put.text = role + "=[" + put.list + "]";
                }
                else if (curText == "..." || curText == "[]")
                {
                    put.replace = cur["span"].toArray();
                    put.text = "[" + put.list + "]";
                }
                else if (cur["list"].toBool() && cur.contains("last"))
                {
                    put.after = cur["last"].toArray();
                    put.text = put.list;
                }
                else if (have.size() == 1 && curText == have[0])
                {
                    put.replace = cur["span"].toArray();        // (one condition written: it becomes the list with the new ones)
                    put.text = "[" + curText + ", " + put.list + "]";
                }
                else
                {
                    p.why = T("The %1 of '%2' is given as %3, which is neither a condition nor a list of them written in the call: "
                              "write the list in the script.").arg(role, tname, curText);
                    return p;
                }
                puts << put;
            }
            if (puts.isEmpty())
            {
                p.why = nothing;
                return p;
            }
            // (several new keywords after the call's last argument are one place: `, inlets=[a], boundaries=[b]`)
            QList<Put> places;
            for (const Put& put : puts)
            {
                bool merged = false;
                if (put.replace.isEmpty())
                    for (Put& q : places)
                        if (q.replace.isEmpty() && q.after == put.after)
                        {
                            q.text += ", " + put.text;
                            merged = true;
                            break;
                        }
                if (!merged) places << put;
            }
            p.ok = true;
            p.after = places[0].after;
            p.replace = places[0].replace;
            p.text = places[0].text;
            for (int k = 1; k < places.size(); ++k) p.more << OnPlan::Piece{places[k].replace, places[k].after, places[k].text};
            QStringList kinds, all;
            for (const Put& put : puts) { kinds << put.role; all << put.list; }
            p.what = T("Add %1 to the %2 of %3").arg(all.join(", "), kinds.join(", "), tname);
            return p;
        }
    }
    if (allPoints(models) && models.size() == 1)
    {
        // A point is the position wherever one is asked for: it takes the place of another point the call is given, or else of the
        // first position written as a tuple of numbers; a call with neither is left to the rules for models below
        QJsonObject slot;
        for (const auto iv : inputs)
        {
            const auto in = iv.toObject();
            if (in["deep"].toBool() || in.contains("keyword") || in["in_list"].toBool()) continue;
            if (itemBefore(in["name"].toString(), target["line"].toInt())["type"].toString() != "point") continue;
            if (in["name"].toString() == names[0])
            {
                p.why = T("'%1' is already the point '%2' is given, so dropping it here changes nothing.").arg(names[0], tname);
                return p;
            }
            p.ok = true;
            p.replace = in["span"].toArray();
            p.text = names[0];
            p.what = T("Use %1 in place of %2 in %3").arg(names[0], in["name"].toString(), tname);
            return p;
        }
        const QJsonArray points = target["points"].toArray();
        if (!points.isEmpty())
        {
            slot = points[0].toObject();
            p.ok = true;
            p.replace = slot["span"].toArray();
            // (a position of a flat shape has two coordinates: the point gives its x and y)
            p.text = slot["dims"].toInt() == 2 ? "(" + names[0] + ".x, " + names[0] + ".y)" : names[0];
            p.what = T("Use %1 instead of %2 (%3) in %4").arg(names[0], slot["text"].toString(), slot["label"].toString(), tname);
            return p;
        }
    }
    if (allFields(models))
    {
        if (models.size() > 1)
        {
            p.why = T("A field takes the place of one number in a call, so one field can be dropped at a time (%1 are several).").arg(quoted);
            return p;
        }
        QJsonObject slot;
        QStringList counts;
        for (const auto nv : target["numbers"].toArray())
        {
            const auto n = nv.toObject();
            if (n["label"].toString().contains('[')) continue;          // (an element of a tuple: a corner, a centre)
            if (n["discrete"].toBool())
            {
                counts << n["label"].toString() + " = " + n["text"].toString();
                continue;                                               // (a count: whole numbers only)
            }
            slot = n;
            break;
        }
        if (slot.isEmpty())
        {
            p.why = counts.isEmpty()
                ? T("'%1' has no plain number in %2 for a field to take the place of: its numbers are written inside "
                  "tuples (a corner, a centre), where a field cannot go by a drop. Write the field into the call in the script.")
                      .arg(tname, fnText)
                : T("The only plain numbers in %1 of '%2' are counts (%3), and a count is a whole "
                  "number: a field has a different value at every point, so it cannot say how many. A field can take the place of a length, "
                  "an angle or a strength, not of a count.").arg(fnText, tname, counts.join(", "));
            return p;
        }
        p.ok = true;
        p.replace = slot["span"].toArray();
        p.text = names[0];
        p.what = T("Use %1 instead of %2 (%3) in %4").arg(names[0], slot["text"].toString(), slot["label"].toString(), tname);
        return p;
    }
    QJsonObject last;
    QStringList have;
    int directCount = 0;
    QJsonObject only;
    for (const auto iv : inputs)
    {
        const auto in = iv.toObject();
        if (in["deep"].toBool() || in.contains("keyword")) continue;
        have << in["name"].toString();
        ++directCount;
        only = in;
        const auto s = in["span"].toArray(), l = last["span"].toArray();
        if (last.isEmpty() || s[2].toInt() > l[2].toInt() || (s[2].toInt() == l[2].toInt() && s[3].toInt() > l[3].toInt())) last = in;
    }
    for (const QString& n : names)
    {
        if (have.contains(n))
        {
            // Held by a reference only (`# shadow: n` in its statement), by a model that is no call's own yet: dragged onto the call it
            // is the call's own now (its row moves in); a Ctrl+drag changes nothing, it is a reference already
            bool onlyReference = !models.isEmpty();
            QStringList marked;
            for (const auto sv : target["shadows"].toArray()) marked << sv.toString();
            for (const auto& m : models) onlyReference = onlyReference && marked.contains(m["var"].toString()) && !m.contains("owner");
            if (onlyReference)
            {
                p.ok = true;
                p.own = true;
                p.what = (names.size() > 1 ? T("Move %1 into %2: they are its own now") : T("Move %1 into %2: it is its own now"))
                             .arg(names.join(", "), tname);
                return p;
            }
            p.why = T("'%1' is already one of the models '%2' is given, so dropping it here changes nothing. To change its "
                    "place among them, drop it between two of them.").arg(n, tname);
            return p;
        }
    }
    if (target["variadic"].toBool())
    {
        if (last.isEmpty())
        {
            p.why = T("'%1' is made by %2, which takes any number of models, but none is written in its call by "
                    "name, so there is no argument to put %3 after.").arg(tname, fnText, quoted);
            return p;
        }
        p.ok = true;
        p.after = last["span"].toArray();
        p.text = names.join(", ");
        p.what = T("Use %1 in %2").arg(names.join(", "), tname);
        return p;
    }
    if (models.size() > 1)
    {
        p.why = T("'%1' is made by %2, which works on exactly the models it is given and has one place for each, so "
                "several models cannot be dropped on it at once: drop one.").arg(tname, fnText);
        return p;
    }
    if (directCount == 1)
    {
        p.ok = true;
        p.replace = only["span"].toArray();
        p.text = names[0];
        p.what = T("Use %1 in place of %2 in %3").arg(names[0], only["name"].toString(), tname);
        return p;
    }
    p.why = T("'%1' is made by %2, which works on exactly the models it is given%3, so a dropped model could only take the place of one "
              "of them, and nothing says which. Edit the call in the script to swap one.")
                .arg(tname, fnText, have.isEmpty() ? T(" (it uses them inside expressions)") : " (" + have.join(", ") + ")");
    return p;
}

bool ScenePanel::sameKind(const QJsonObject& a, const QJsonObject& b) const
{
    auto kind = [](const QJsonObject& m) {
        const QString type = m["type"].toString();
        return typeClass(type.isEmpty() ? QString("solid") : type);
    };
    if (kind(a) != kind(b)) return false;
    // (a support is not a load: they stand in different lists of a set of conditions)
    return a["condition_role"].toString() == b["condition_role"].toString();
}

ScenePanel::OnPlan ScenePanel::planReplace(const QList<QJsonObject>& models, const QJsonObject& parent, const QString& replaced) const
{
    OnPlan p;
    QStringList names;
    for (const auto& m : models) names << m["var"].toString();
    const QString pname = parent["var"].toString();
    const QJsonObject old = itemBefore(replaced, parent["line"].toInt());
    if (old.isEmpty() || parent["failed"].toBool() || parent["kind"].toString() == "failed")
    {
        p.why = T("'%1' is not a model that can be taken out of its place.").arg(replaced);
        return p;
    }
    for (const auto& m : models)
    {
        const QString name = m["var"].toString();
        if (name == replaced)
        {
            p.why = T("'%1' is the model it would replace, so dropping it here changes nothing.").arg(name);
            return p;
        }
        if (name == pname || dependsOn(m, parent))
        {
            p.why = T("'%1' is made of '%2', so it cannot also be one of its arguments: that would be a loop.").arg(name, pname);
            return p;
        }
        if (!sameKind(m, old))
        {
            p.why = T("'%1' is not the kind of model '%2' is, so it cannot take its place: only a model of the same kind can.")
                        .arg(name, replaced);
            return p;
        }
    }
    // Where the call is given it: the first place it is written
    QJsonObject slot;
    for (const auto iv : parent["inputs"].toArray())
    {
        const auto in = iv.toObject();
        if (in["name"].toString() != replaced) continue;
        if (slot.isEmpty())
        {
            slot = in;
            continue;
        }
        const auto s = in["span"].toArray(), l = slot.value("span").toArray();
        if (s[0].toInt() < l[0].toInt() || (s[0].toInt() == l[0].toInt() && s[1].toInt() < l[1].toInt())) slot = in;
    }
    if (slot.isEmpty())
    {
        p.why = T("'%1' does not say where it uses '%2' (it is used inside an expression), so there is no place to put another.")
                    .arg(pname, replaced);
        return p;
    }
    // (several models take one place only where the call takes several there: a list, or any number of arguments)
    const bool room = slot.value("listed").toBool() || (parent["variadic"].toBool() && !slot.value("deep").toBool() && !slot.contains("keyword"));
    if (models.size() > 1 && !room)
    {
        p.why = T("'%1' is made by %2, which has one place there: drop one model on it to take its place.")
                    .arg(pname, parent["callee"].toString().isEmpty() ? T("its call") : parent["callee"].toString() + "()");
        return p;
    }
    for (const auto iv : parent["inputs"].toArray())
    {
        const auto in = iv.toObject();
        if (names.contains(in["name"].toString()) && in["span"].toArray() != slot.value("span").toArray())
        {
            p.why = T("'%1' is already one of the models '%2' is given, so dropping it here would put it in twice.")
                        .arg(in["name"].toString(), pname);
            return p;
        }
    }
    p.ok = true;
    p.replace = slot.value("span").toArray();
    p.text = names.join(", ");
    p.drop = replaced;
    p.what = T("Use %1 instead of %2 in %3").arg(names.join(", "), replaced, pname);
    return p;
}

bool ScenePanel::canDrop(QTreeWidgetItem* over, int at, bool copy, QString* why) const
{
    auto refuse = [&](const QString& text) { if (why) *why = text; return false; };
    const QList<QJsonObject> models = m_dragging ? m_dragModels : draggedModels();
    if (models.isEmpty()) return false;
    bool anyLocked = false;
    for (const auto& m : models) anyLocked = anyLocked || (!m_dragShadow && m.contains("locked"));
    const QString copyNeedsCall = anyLocked
        ? T("A locked model is read-only, so dragging it never moves it: it makes a reference to it, and a reference only has a place "
            "inside a call. Here there is no call for it to be an argument of: drop it on an operation, or between the models one is made of.")
        : T("A copy is a reference to a model, and a reference only has a place inside a call: here there is no call "
            "for it to be an argument of. Drop it on an operation, or between the models one is made of.");
    if (!over) return at == DropViewport && !copy ? true : (copy ? refuse(copyNeedsCall) : false);
    const QString type = over->data(COL_NAME, ROLE_TYPE).toString();
    if (type != "item" && type != "dep" && type != "hole" && type != "bcgroup") return false;
    const QJsonObject t = over->data(COL_NAME, ROLE_ITEM).toJsonObject();
    if (type == "bcgroup")
    {
        // dropped on the "boundary conditions" row of a simulation: it is dropped on the simulation (the conditions go in the input of their kind)
        if (at != DropOn) return false;
        const OnPlan plan = planDropOn(models, t);
        return plan.ok ? true : refuse(plan.why);
    }
    if (type == "hole")
    {
        // dropped on a placeholder: it is replaced by the model (nothing is dropped between placeholders)
        if (at != DropOn) return false;
        const OnPlan plan = planFill(models, t, over->data(COL_NAME, ROLE_PART).toInt());
        return plan.ok ? true : refuse(plan.why);
    }
    QJsonObject parent;                         // the statement whose models the row stands among
    if (type == "dep")
    {
        if (at == DropOn)
        {
            // dropped ON a shadow: a model of its kind takes its place in the call (the reference goes)
            const OnPlan plan = planReplace(models, t, over->data(COL_NAME, ROLE_PART).toString());
            return plan.ok ? true : refuse(plan.why);
        }
        parent = t;
    }
    else
    {
        for (const auto& m : models)
        {
            if (keyOf(m) == keyOf(t)) return false;
        }
        if (at != DropOn && t.contains("owner")) parent = itemByKey(t["owner"].toString());
    }
    if (at == DropOn)
    {
        const bool nests = takesInput(t) || t["hole"].toBool() || (allFields(models) && takesNumber(t)) || (allPoints(models) && takesPoint(t))
                           || regionDrop(t, models);
        QString nestWhy;
        if (nests)
        {
            const OnPlan plan = planDropOn(models, t);
            if (plan.ok) return true;
            nestWhy = plan.why;
        }
        // It cannot go into it.  A model of its kind, dropped on one of the models a call is given, takes its place in that call
        const QJsonObject owner = t.contains("owner") ? itemByKey(t["owner"].toString()) : QJsonObject();
        bool same = true;
        for (const auto& m : models) same = same && sameKind(m, t);
        if (!owner.isEmpty() && same)
        {
            const OnPlan plan = planReplace(models, owner, t["var"].toString());
            return plan.ok ? true : refuse(plan.why);
        }
        if (nests) return refuse(nestWhy);
        return refuse((allFields(models) ? T("'%1' is not made from other models or numbers, so there is nothing for a dropped model to be an argument of.")
                       : allPoints(models) ? T("'%1' is not made from other models or positions, so there is nothing for a dropped model to be an argument of.")
                       : T("'%1' is not made from other models, so there is nothing for a dropped model to be an argument of.")).arg(t["var"].toString()));
    }
    if (parent.isEmpty()) return copy ? refuse(copyNeedsCall) : true;      // (out of every call: a copy needs a call to be in)
    const QStringList have = directInputs(parent);
    const QString pname = parent["var"].toString();
    for (const auto& m : models)
    {
        const QString name = m["var"].toString();
        if (name == pname || dependsOn(m, parent))
            return refuse(T("'%1' is made of '%2', so it cannot also be one of its arguments: that would be a loop.").arg(name, pname));
        if (!have.contains(name) && !parent["variadic"].toBool())
            return refuse(T("'%1' is made by %2, which works on exactly the models it is given (%3), so there is no place between them for another.")
                              .arg(pname, parent["callee"].toString().isEmpty() ? T("its call") : parent["callee"].toString() + "()", have.join(", ")));
        if (copy && have.contains(name))
            return refuse(T("'%1' is already one of the models '%2' is given: a copy would put it in twice, which changes nothing.").arg(name, pname));
    }
    return true;
}

QList<QPair<int, int>> ScenePanel::groupRanges(const QJsonObject& it, bool comments) const
{
    QList<QPair<int, int>> r;
    int a = it["line"].toInt() - 1;
    const int b = it["end_line"].toInt() - 1;
    static const QRegularExpression hidden(R"(^\s*#\s*hidden:)");
    while (comments && a > 0)
    {
        // (the comment lines right above a definition are about it)
        const QString t = lineText(a - 1).trimmed();
        if (t.startsWith('#') && !hidden.match(t).hasMatch()) --a;
        else break;
    }
    r << qMakePair(a, b);
    for (const char* key : {"display_line", "hidden_line"})
    {
        if (it.contains(key)) { const int L = it[key].toInt() - 1; r << qMakePair(L, L); }
    }
    for (const char* key : {"handles", "exposed", "locked", "cache", "cache_off", "custom_resolution"})
    {
        if (it.contains(key))
        {
            const auto o = it[key].toObject();
            r << qMakePair(o["line"].toInt() - 1, o["end_line"].toInt() - 1);
        }
    }
    std::sort(r.begin(), r.end());
    return r;
}

int ScenePanel::groupStart(const QJsonObject& it) const
{
    return groupRanges(it).first().first;
}

int ScenePanel::groupEnd(const QJsonObject& it) const
{
    QSet<int> own;
    for (const auto& r : groupRanges(it))
    {
        for (int l = r.first; l <= r.second; ++l) own.insert(l);
    }
    int e = it["end_line"].toInt();             // (1-based end = the 0-based index of the line after it)
    while (own.contains(e)) ++e;
    return e;
}

bool ScenePanel::moveGroups(QStringList& lines, QVector<int>* origin, const QList<QJsonObject>& models, int at, bool onlyLater,
                            QString* why, QList<QJsonObject>* pulled) const
{
    // What moves: the models asked for, and of what they are made, what is defined from `at` on (it has to be there first)
    QList<QJsonObject> moved;
    QSet<QString> have;
    std::function<void(const QJsonObject&, bool)> add = [&](const QJsonObject& m, bool asked) {
        const QString key = keyOf(m);
        if (have.contains(key)) return;
        if ((!asked || onlyLater) && groupStart(m) < at) return;
        have.insert(key);
        moved << m;
        if (!asked && pulled) *pulled << m;                     // (defined below `at`, and needed by what is moved: it goes along)
        for (const auto d : m["deps"].toArray())
        {
            const QJsonObject dep = itemBefore(d.toString(), m["line"].toInt());
            if (!dep.isEmpty()) add(dep, false);
        }
    };
    for (const auto& m : models) add(m, true);
    if (moved.isEmpty()) return true;
    std::sort(moved.begin(), moved.end(), [&](const QJsonObject& a, const QJsonObject& b) { return groupStart(a) < groupStart(b); });

    const int n = lines.size();
    at = std::max(0, std::min(at, n));
    QSet<int> gone;
    QStringList block;
    QVector<int> blockOrigin;
    QHash<QString, QString> textOf;             // (the lines of each moved model, as one text)
    for (const auto& m : moved)
    {
        for (const auto& r : groupRanges(m))
        {
            for (int l = r.first; l <= r.second && l < n; ++l)
            {
                if (gone.contains(l)) continue;
                gone.insert(l);
                block << lines[l];
                if (origin) blockOrigin << (*origin)[l];
                textOf[keyOf(m)] += lines[l] + "\n";
            }
        }
    }

    // Nothing may be used before it is defined: not what moves later, and not what moves earlier
    static const QRegularExpression importRe(R"(^\s*(from|import)\s)");
    static const QRegularExpression defineRe(R"(^(?:def|class)\s+(\w+)|^([A-Za-z_][\w\s,]*?)\s*=(?!=))");
    for (const auto& m : moved)
    {
        const int start = groupStart(m);
        if (start < at)
        {
            const QString name = m["var"].toString();
            const QRegularExpression use("\\b" + QRegularExpression::escape(name) + "\\b");
            for (int l = start; l < at && l < n; ++l)
            {
                if (gone.contains(l) || !use.match(lines[l]).hasMatch()) continue;
                if (lines[l].trimmed().startsWith('#')) continue;
                if (why) *why = T("%1 is used on line %2: it cannot be moved below it").arg(name).arg(l + 1);
                return false;
            }
        }
        else
        {
            for (int l = at; l < start && l < n; ++l)
            {
                if (gone.contains(l)) continue;
                if (importRe.match(lines[l]).hasMatch())
                {
                    if (why) *why = T("%1 would come before the import on line %2").arg(m["var"].toString()).arg(l + 1);
                    return false;
                }
                const auto d = defineRe.match(lines[l]);
                if (!d.hasMatch()) continue;
                QStringList names;
                if (!d.captured(1).isEmpty()) names << d.captured(1);
                else for (const QString& s : d.captured(2).split(',')) names << s.trimmed();
                for (const QString& name : names)
                {
                    if (name.isEmpty()) continue;
                    const QRegularExpression use("\\b" + QRegularExpression::escape(name) + "\\b");
                    if (use.match(textOf.value(keyOf(m))).hasMatch())
                    {
                        if (why) *why = T("%1 uses %2, which is defined on line %3: it cannot be moved above it")
                                            .arg(m["var"].toString(), name).arg(l + 1);
                        return false;
                    }
                }
            }
        }
    }

    QStringList out;
    QVector<int> outOrigin;
    for (int i = 0; i <= n; ++i)
    {
        if (i == at)
        {
            out << block;
            outOrigin << blockOrigin;
        }
        if (i < n && !gone.contains(i))
        {
            out << lines[i];
            if (origin) outOrigin << (*origin)[i];
        }
    }
    lines = out;
    if (origin) *origin = outOrigin;
    return true;
}

void ScenePanel::applyLines(const QStringList& before, const QStringList& after, const QString& what,
                            const QVector<int>& origin, const QList<ColShift>& shifts, const QStringList& targets,
                            const QHash<QString, int>& newLength)
{
    // One replacement of what differs (the lines that stay the same at the top and at the bottom are left alone)
    const int n = before.size(), m = after.size();
    int p = 0;
    while (p < n && p < m && before[p] == after[p]) ++p;
    if (p == n && p == m) return;
    int s = 0;
    while (s < n - p && s < m - p && before[n - 1 - s] == after[m - 1 - s]) ++s;
    const int oldEnd = n - s, newEnd = m - s;           // [p, oldEnd) of the old lines is [p, newEnd) of the new ones
    const QString mid = after.mid(p, newEnd - p).join("\n");
    TextEdit e;
    if (oldEnd == p)
    {
        e = p < n ? TextEdit{p, 0, p, 0, mid + "\n"}
                  : TextEdit{n - 1, int(before[n - 1].size()), n - 1, int(before[n - 1].size()), "\n" + mid};
    }
    else if (newEnd == p)
    {
        if (oldEnd < n) e = TextEdit{p, 0, oldEnd, 0, QString()};
        else if (p > 0) e = TextEdit{p - 1, int(before[p - 1].size()), n - 1, int(before[n - 1].size()), QString()};
        else e = TextEdit{0, 0, n - 1, int(before[n - 1].size()), QString()};
    }
    else
    {
        e = TextEdit{p, 0, oldEnd - 1, int(before[oldEnd - 1].size()), mid};
    }
    // The tree this edit gives is worked out first: if it puts a shadow above its original, that is asked ONCE (all of them in
    // one message), before anything is written
    const QJsonObject predicted = predictedScene(n, after, origin, shifts, targets, newLength);
    const QList<Flip> flips = ownershipFlips(predicted);
    const bool go = confirmFlips(flips);
    m_copyAdds.clear();
    if (!go) return;
    m_dropEdited = true;
    emit(editScript(QList<TextEdit>{e}, what));
    // (the tree does not wait for the script to run: it is there at once)
    installPrediction(predicted);
    // (and the viewport's shapes are told which lines went where: see sourceLinesMoved)
    QVector<int> moved(n, -1);
    for (int k = 0; k < origin.size(); ++k)
    {
        if (origin[k] >= 0 && origin[k] < n) moved[origin[k]] = k;
    }
    emit(sourceLinesMoved(moved));
    emit(highlightLines(selectedLines()));
}

QJsonObject ScenePanel::predictedScene(int oldCount, const QStringList& after, const QVector<int>& origin, const QList<ColShift>& shifts,
                                       const QStringList& targets, const QHash<QString, int>& newLength, const QString& renameFrom,
                                       const QString& renameTo) const
{
    QVector<int> map(oldCount, -1);
    for (int k = 0; k < origin.size(); ++k)
    {
        if (origin[k] >= 0 && origin[k] < oldCount) map[origin[k]] = k;
    }
    auto newLine = [&](int oldLine1) {              // (1-based; -1: the line is gone)
        if (oldLine1 < 1 || oldLine1 > oldCount) return -1;
        const int k = map[oldLine1 - 1];
        return k < 0 ? -1 : k + 1;
    };
    auto newCol = [&](int oldLine1, int col) {
        int c = col;
        for (const auto& sh : shifts)
        {
            if (sh.line == oldLine1 && col >= sh.col) c += sh.delta;
        }
        return c;
    };

    static const QSet<QString> lineKeys = {"line", "end_line", "display_line", "hidden_line", "part_of"};
    static const QStringList blockKeys = {"handles", "exposed", "locked", "cache", "cache_off", "custom_resolution"};
    std::function<QJsonValue(const QString&, const QJsonValue&)> walk;
    walk = [&](const QString& key, const QJsonValue& v) -> QJsonValue {
        if (v.isObject())
        {
            const QJsonObject o = v.toObject();
            QJsonObject out;
            for (auto it = o.begin(); it != o.end(); ++it)
            {
                const QString k = it.key();
                if (lineKeys.contains(k) && it.value().isDouble())
                {
                    const int nl = newLine(int(it.value().toDouble()));
                    if (nl > 0) out[k] = nl;            // (a line that is gone leaves its key out)
                    continue;
                }
                const QJsonValue r = walk(k, it.value());
                if (!r.isUndefined()) out[k] = r;
            }
            if (blockKeys.contains(key) && !out.contains("line")) return QJsonValue(QJsonValue::Undefined);
            return out;
        }
        if (v.isArray())
        {
            const QJsonArray a = v.toArray();
            if ((key == "call" || key.endsWith("span")) && a.size() == 4 && a[0].isDouble())
            {
                const int l0 = int(a[0].toDouble()), l1 = int(a[2].toDouble());
                const int n0 = newLine(l0), n1 = newLine(l1);
                if (n0 < 0 || n1 < 0) return a;
                return QJsonArray{n0, newCol(l0, int(a[1].toDouble())), n1, newCol(l1, int(a[3].toDouble()))};
            }
            QJsonArray out;
            for (const auto& e : a)
            {
                const QJsonValue r = walk(key, e);
                if (!r.isUndefined()) out.append(r);
            }
            return out;
        }
        return v;
    };
    QJsonObject scene = walk(QString(), m_scene).toObject();

    // The items, in the order of the script (a statement that was deleted has no line any more: it is gone)
    QList<QJsonObject> list;
    for (const auto v : scene["items"].toArray())
    {
        if (v.toObject().contains("line")) list << v.toObject();
    }
    std::stable_sort(list.begin(), list.end(),
                     [](const QJsonObject& a, const QJsonObject& b) { return a["line"].toInt() < b["line"].toInt(); });

    // A rename: the variable is called something else everywhere
    if (!renameFrom.isEmpty())
    {
        const QRegularExpression keyRe("(:|>)" + QRegularExpression::escape(renameFrom) + "(?=$|#|>)");
        auto renameKey = [&](QString k) { return k.replace(keyRe, "\\1" + renameTo); };
        for (auto& it : list)
        {
            if (it["var"].toString() == renameFrom) it["var"] = renameTo;
            if (it["label"].toString() == renameFrom) it["label"] = renameTo;
            QJsonArray deps;
            for (const auto d : it["deps"].toArray()) deps.append(d.toString() == renameFrom ? renameTo : d.toString());
            it["deps"] = deps;
            QJsonArray inputs;
            for (const auto iv : it["inputs"].toArray())
            {
                auto in = iv.toObject();
                if (in["name"].toString() == renameFrom) in["name"] = renameTo;
                inputs.append(in);
            }
            it["inputs"] = inputs;
            QJsonArray parts;
            for (const auto pv : it["parts"].toArray())
            {
                auto pt = pv.toObject();
                if (pt["var"].toString() == renameFrom) pt["var"] = renameTo;
                parts.append(pt);
            }
            it["parts"] = parts;
            if (it.contains("shadows"))
            {
                QJsonArray marked;
                for (const auto sv : it["shadows"].toArray()) marked.append(sv.toString() == renameFrom ? renameTo : sv.toString());
                it["shadows"] = marked;
            }
            if (it.contains("owner")) it["owner"] = renameKey(it["owner"].toString());
        }
    }

    // The statements that were rewritten use what their new text says
    QSet<QString> vars;
    for (const auto& it : list) if (it.contains("var")) vars.insert(it["var"].toString());
    if (!targets.isEmpty())
    {
        static const QRegularExpression strRe(R"(("""|''')[\s\S]*?\1|"(?:\\.|[^"\\\n])*"|'(?:\\.|[^'\\\n])*')");
        static const QRegularExpression commentRe(R"(#[^\n]*)");
        static const QRegularExpression nameRe(R"((?<![\w.])([A-Za-z_]\w*)(?!\w)(?!\s*=(?!=)))");
        for (auto& it : list)
        {
            const QString target = it["var"].toString();
            if (!targets.contains(target)) continue;
            if (newLength.contains(target)) it["end_line"] = it["line"].toInt() + newLength.value(target) - 1;
            QString text;
            for (int l = it["line"].toInt(); l <= it["end_line"].toInt(); ++l) text += after.value(l - 1) + "\n";
            {
                // (the `# shadow: a, b` comment: the models this statement holds a reference to without owning them)
                static const QRegularExpression shadowRe(R"(#\s*shadow:\s*([A-Za-z_]\w*(?:\s*,\s*[A-Za-z_]\w*)*))");
                QStringList marked;
                for (auto m = shadowRe.globalMatch(text); m.hasNext();)
                    for (const QString& n : m.next().captured(1).split(",")) marked << n.trimmed();
                if (marked.isEmpty()) it.remove("shadows");
                else it["shadows"] = QJsonArray::fromStringList(marked);
            }
            text.remove(strRe);
            text.remove(commentRe);
            QStringList deps;
            for (auto m = nameRe.globalMatch(text); m.hasNext();)
            {
                const QString name = m.next().captured(1);
                if (name == target || !vars.contains(name) || deps.contains(name)) continue;
                for (const auto& other : list)
                {
                    if (other["var"].toString() == name && other["line"].toInt() < it["line"].toInt()) { deps << name; break; }
                }
            }
            deps.sort();
            it["deps"] = QJsonArray::fromStringList(deps);
            it["stale"] = true;                 // (its inputs and numbers are the run's to say)
        }
    }

    // A model belongs to the first statement that uses it
    QHash<QString, QList<int>> versions;
    for (int i = 0; i < list.size(); ++i)
    {
        if (list[i].contains("var")) versions[list[i]["var"].toString()] << i;
    }
    QHash<int, QString> owner;
    for (int i = 0; i < list.size(); ++i)
    {
        QStringList marked;
        for (const auto sv : list[i]["shadows"].toArray()) marked << sv.toString();
        for (const auto dv : list[i]["deps"].toArray())
        {
            if (marked.contains(dv.toString())) continue;          // (a reference made by a Ctrl+drag: not the first use)
            int earlier = -1;
            for (int j : versions.value(dv.toString()))
            {
                if (list[j]["line"].toInt() < list[i]["line"].toInt()) earlier = j;
            }
            if (earlier >= 0 && earlier != i && !owner.contains(earlier)) owner[earlier] = keyOf(list[i]);
        }
    }
    QJsonArray items;
    for (int i = 0; i < list.size(); ++i)
    {
        QJsonObject it = list[i];
        it.remove("owner");
        if (owner.contains(i)) it["owner"] = owner[i];
        items.append(it);
    }
    scene["items"] = items;
    return scene;
}

void ScenePanel::installPrediction(const QJsonObject& scene)
{
    m_scene = scene;
    m_predicted = true;
    m_predictedMd5 = m_source ? md5Hex(m_source()) : QString();
    m_predictClock.start();
    rebuild();
    updateMultiNote();
}

void ScenePanel::predictScene(int oldCount, const QStringList& after, const QVector<int>& origin, const QList<ColShift>& shifts,
                              const QStringList& targets, const QHash<QString, int>& newLength, const QString& renameFrom,
                              const QString& renameTo)
{
    const QJsonObject scene = predictedScene(oldCount, after, origin, shifts, targets, newLength, renameFrom, renameTo);
    if (!renameFrom.isEmpty())
    {
        // (what the panel remembers about rows is by their keys: the renamed variable has another one)
        const QRegularExpression keyRe("(:|>)" + QRegularExpression::escape(renameFrom) + "(?=$|#|>)");
        auto renameKey = [&](QString k) { return k.replace(keyRe, "\\1" + renameTo); };
        m_selectedKey = renameKey(m_selectedKey);
        for (auto& k : m_selectOrder) k = renameKey(k);
        // (a name that is being typed in a row: its editor is opened again on the row that has the new key)
        if (auto current = m_tree->currentItem())
            current->setData(COL_NAME, ROLE_KEY, renameKey(current->data(COL_NAME, ROLE_KEY).toString()));
        QHash<QString, bool> expand;
        for (auto it = m_expandState.begin(); it != m_expandState.end(); ++it) expand[renameKey(it.key())] = it.value();
        m_expandState = expand;
    }
    installPrediction(scene);
}

void ScenePanel::onDrop(QTreeWidgetItem* over, int at, const QPoint& global, bool copy)
{
    // (at once: the mouse was let go, and the script is edited and the tree changed before anything else happens)
    m_dropModels = m_dragModels;
    m_dropShadow = m_dragShadow;
    m_copyAdds.clear();
    m_dragModels.clear();
    finishDrop(over ? over->data(COL_NAME, ROLE_KEY).toString() : QString(), at, global, copy);
}

bool ScenePanel::beginDrag(QTreeWidgetItem* row, QString* text, QIcon* icon, bool ctrl, bool wasSelected)
{
    m_dragSourceLine.clear();
    m_dragShadow = false;
    QList<QJsonObject> models;
    const QString type = row->data(COL_NAME, ROLE_TYPE).toString();
    if (type == "dep")
    {
        // A shadow: the reference of a statement to a model that another statement owns.  It is dragged on its own: the
        // reference moves (or, with Ctrl, is copied); the model, and its other references, stay
        const QString name = row->data(COL_NAME, ROLE_PART).toString();
        const QJsonObject stmt = row->data(COL_NAME, ROLE_ITEM).toJsonObject();
        const QJsonObject ref = itemBefore(name, stmt["line"].toInt());
        if (ref.isEmpty() || !ref.contains("var") || ref["failed"].toBool() || ref["kind"].toString() == "failed") return false;
        models << ref;
        m_dragSourceLine[keyOf(ref)] = stmt["line"].toInt();
        m_dragShadow = true;
        *text = name;
        *icon = row->icon(COL_NAME);
    }
    else
    {
        // The models that are dragged: the selected ones (the row pressed is one of them: pressing selected it).  With Ctrl,
        // a row that was not selected is dragged on its own
        if (ctrl && !row->isSelected()) m_tree->setCurrentItem(row, 0, QItemSelectionModel::Select | QItemSelectionModel::Rows);
        if (ctrl && !wasSelected)
        {
            const QJsonObject m = modelOfRow(row);
            if (!m.isEmpty()) models << m;
        }
        else
        {
            models = draggedModels();
        }
        // (the row of a part of an import stands for the model of that part: its key is the model's, not the row's)
        const QJsonObject own = modelOfRow(row);
        const QString key = !own.isEmpty() ? keyOf(own) : row->data(COL_NAME, ROLE_KEY).toString();
        bool has = false;
        for (const auto& m : models) has = has || keyOf(m) == key;
        if (!has) return false;
        for (const auto& m : models)
        {
            const QJsonObject owner = itemByKey(m["owner"].toString());
            if (!owner.isEmpty()) m_dragSourceLine[keyOf(m)] = owner["line"].toInt();
        }
        *text = models.size() == 1 ? row->text(COL_NAME) : T("%1 models").arg(models.size());
        *icon = row->icon(COL_NAME);
    }
    m_prepareTimer.stop();              // (nothing is written into the script while the models are carried)
    m_dragModels = models;
    m_dragging = true;
    m_dropEdited = false;
    return true;
}

void ScenePanel::onTreeHeld(bool down)
{
    m_treeHeld = down;
    if (down) return;
    // The button is up.  A drag that did not edit the script, or no drag at all (a click): the selected models are made ready
    // to be dragged, as a selection always does
    if (!m_dropEdited && !selectedModels().isEmpty()) m_prepareTimer.start();
    m_dropEdited = false;
}

////////////////////////////////////////////////////////////////////////////////
// The tree is the structure of the calls

QJsonObject ScenePanel::itemByKey(const QString& key) const
{
    if (key.isEmpty()) return QJsonObject();
    for (const auto v : m_scene["items"].toArray())
    {
        const auto o = v.toObject();
        if (keyOf(o) == key) return o;
    }
    return QJsonObject();
}

QJsonObject ScenePanel::itemAtLine(int line) const
{
    for (const auto v : m_scene["items"].toArray())
    {
        const auto o = v.toObject();
        if (o["line"].toInt() == line && o.contains("var")) return o;
    }
    return QJsonObject();
}

QJsonObject ScenePanel::sourceStatement(const QJsonObject& m) const
{
    if (m_dragSourceLine.contains(keyOf(m))) return itemAtLine(m_dragSourceLine.value(keyOf(m)));
    return itemByKey(m["owner"].toString());
}

void ScenePanel::wireRemove(Rewire& w, const QJsonObject& from, const QJsonObject& model) const
{
    const int id = from["line"].toInt();
    const QString name = model["var"].toString();
    const QStringList direct = directInputs(from);
    auto& change = w.edits[id];
    change.item = from;
    if (change.remove.contains(name) || change.hole.contains(name)) return;
    {
        // A model written in a list that the call is given -- loads=[a, b], [push, pull] -- is taken out of the list, and the call keeps
        // the rest: a call does not stop making sense because one of the things in a list is gone.  (A call given one list as an
        // argument cannot do without the last one of it: a placeholder takes its place -- `loads=...` for a list a keyword is given, `[...]` for
        // another -- never an empty list.)
        int seen = 0;
        bool every = true;
        for (const auto iv : from["inputs"].toArray())
        {
            const auto in = iv.toObject();
            if (in["name"].toString() != name) continue;
            ++seen;
            if (!in["listed"].toBool()) every = false;           // (the last one of a list leaves its placeholder: the interpreter says which)
        }
        if (seen > 0 && every)
        {
            change.remove << name;
            return;
        }
    }
    // (a call that takes any number of models keeps as many as its signature cannot do without -- `difference(a, b, *rest)` needs two, so taking
    // one of two out leaves a placeholder -- the function says, so no operation is listed here)
    if (from["variadic"].toBool() && direct.contains(name) &&
        direct.size() - change.remove.size() - 1 >= std::max(1, from["min_inputs"].toInt(1)))
    {
        change.remove << name;
        return;
    }
    // The call cannot do without it (it takes exactly its models, or this is the only one it has, or the model is used inside an
    // expression or a call that is given to it): a placeholder, `...`, takes its place.  The statement stays -- nothing is deleted,
    // nothing is asked -- and the script stops before it until what goes there is written
    change.hole << name;
}

void ScenePanel::wireInsert(Rewire& w, const QJsonObject& parent, const QString& name, const QString& relative,
                            const QString& side) const
{
    auto& change = w.edits[parent["line"].toInt()];
    change.item = parent;
    change.insert << Rewire::Put{name, relative, side};
}

bool ScenePanel::wireStage(const Rewire& w, QStringList& lines, QStringList* targets, QHash<QString, int>* newLength,
                           QString* why) const
{
    // The calls are rewritten by the interpreter (it knows where the commas and brackets are).  The lines stay where they are -- a
    // call that got shorter leaves lines that are marked to go -- so that the line numbers of the scene still say where everything is
    QJsonArray statements;
    for (auto it = w.edits.constBegin(); it != w.edits.constEnd(); ++it)
    {
        const Rewire::Change& c = it.value();
        if (c.remove.isEmpty() && c.insert.isEmpty() && c.hole.isEmpty()) continue;
        QJsonObject o;
        o["var"] = c.item["var"].toString();
        o["line"] = c.item["line"].toInt();
        o["remove"] = QJsonArray::fromStringList(c.remove);
        o["hole"] = QJsonArray::fromStringList(c.hole);
        QJsonArray put;
        for (const auto& p : c.insert)
        {
            QJsonObject q;
            q["name"] = p.name;
            q["relative"] = p.relative;
            q["side"] = p.side;
            put.append(q);
        }
        o["insert"] = put;
        statements.append(o);
    }
    if (!statements.isEmpty())
    {
        if (!m_support || !m_source)
        {
            if (why) *why = T("The interpreter is not ready.");
            return false;
        }
        QJsonObject request;
        request["source"] = m_source();
        request["statements"] = statements;
        QString error;
        const QString answer = m_support("arg_edits", QString::fromUtf8(QJsonDocument(request).toJson(QJsonDocument::Compact)), &error);
        if (answer.isEmpty())
        {
            if (why) *why = error.isEmpty() ? T("Could not change the call.") : error;
            return false;
        }
        const auto out = QJsonDocument::fromJson(answer.toUtf8()).object()["statements"].toArray();
        for (int k = 0; k < out.size() && k < statements.size(); ++k)
        {
            const auto o = out[k].toObject();
            const int a = o["line"].toInt(), b = o["end_line"].toInt();
            const QStringList text = o["text"].toString().split('\n');
            if (a < 0 || b >= lines.size() || b < a) continue;
            for (int i = 0; i <= b - a; ++i) lines[a + i] = i < text.size() ? text[i] : kGone;
            // (a model taken out of the call is not held as a reference by it any more: its `# shadow:` mark goes too)
            for (const char* key : {"remove", "hole"})
                for (const auto nv : statements[k].toObject()[key].toArray())
                    for (int i = 0; i <= b - a; ++i)
                        if (i < text.size()) lines[a + i] = unmarkShadow(lines[a + i], nv.toString());
            const QString var = statements[k].toObject()["var"].toString();
            if (targets) *targets << var;
            if (newLength) (*newLength)[var] = text.size();
        }
    }
    // The numbers written for the surfaces of what was changed, and of what is made of it, do not fit any more
    QList<QJsonObject> changed;
    for (const auto& c : w.edits)
    {
        if (!c.remove.isEmpty() || !c.insert.isEmpty() || !c.hole.isEmpty()) changed << c.item;
    }
    for (const auto v : m_scene["items"].toArray())
    {
        const auto d = v.toObject();
        if (!d.contains("exposed") || !d.contains("var")) continue;
        bool hit = false;
        for (const auto& c : changed) hit = hit || d["var"].toString() == c["var"].toString() || dependsOn(d, c);
        if (!hit) continue;
        const auto ex = d["exposed"].toObject();
        for (int l = ex["line"].toInt() - 1; l <= ex["end_line"].toInt() - 1; ++l)
            if (l >= 0 && l < lines.size()) lines[l] = kGone;
    }
    return true;
}

void ScenePanel::wireDropGone(QStringList& lines, QVector<int>& origin) const
{
    for (int k = lines.size() - 1; k >= 0; --k)
    {
        if (lines[k] != kGone) continue;
        lines.removeAt(k);
        origin.removeAt(k);
    }
}

bool ScenePanel::isAboveOriginal(const QList<QJsonObject>& models, const QJsonObject& target) const
{
    for (const auto& m : models)
    {
        const QJsonObject owner = itemByKey(m["owner"].toString());
        if (!owner.isEmpty() && owner["line"].toInt() > target["line"].toInt()) return true;
    }
    return false;
}

static QString flipName(const QJsonObject& it)
{
    return "'" + (it.contains("var") ? it["var"].toString() : it["label"].toString()) + "'";
}

QList<ScenePanel::Flip> ScenePanel::ownershipFlips(const QJsonObject& predicted) const
{
    QHash<QString, QJsonObject> oldBy, newBy;
    for (const auto v : m_scene["items"].toArray()) oldBy[keyOf(v.toObject())] = v.toObject();
    for (const auto v : predicted["items"].toArray()) newBy[keyOf(v.toObject())] = v.toObject();
    auto uses = [](const QJsonObject& stmt, const QString& name) {
        for (const auto d : stmt["deps"].toArray()) if (d.toString() == name) return true;
        return false;
    };
    QList<Flip> out;
    for (const auto v : predicted["items"].toArray())
    {
        const QJsonObject it = v.toObject();
        if (!it.contains("var")) continue;
        const QString key = keyOf(it), name = it["var"].toString();
        const QJsonObject before = oldBy.value(key);
        if (before.isEmpty()) continue;
        const QString oldOwner = before["owner"].toString(), newOwner = it["owner"].toString();
        if (oldOwner.isEmpty() || newOwner.isEmpty() || oldOwner == newOwner) continue;
        const QJsonObject original = newBy.value(oldOwner), first = newBy.value(newOwner);
        if (original.isEmpty() || first.isEmpty()) continue;
        if (!uses(original, name)) continue;                    // (it was taken out of the original: nothing is left to be a shadow of)
        // the use that is now first was a use already (a shadow that moved above), or is a reference this drop adds
        const bool was = uses(oldBy.value(newOwner), name);
        const bool added = m_copyAdds.contains(qMakePair(name, first["var"].toString()));
        if (!was && !added) continue;                           // (a model that was moved on purpose to its new place)
        out << Flip{"'" + name + "'", flipName(original), flipName(first), added};
    }
    return out;
}

bool ScenePanel::confirmFlips(const QList<Flip>& flips)
{
    if (flips.isEmpty()) return true;
    // (a scripted test does not wait for the question, unless it asks for it: FIELDES_SHOW_DIALOGS)
    const bool shown = qEnvironmentVariableIsSet("FIELDES_SHOW_DIALOGS");
    if (qEnvironmentVariableIsSet("FIELDES_AUTOMATION") && !shown)
    {
        for (const auto& f : flips)
            fprintf(stderr, "[automation] a shadow of %s above its original (%s): %s becomes the original\n",
                    f.model.toUtf8().constData(), f.from.toUtf8().constData(), f.to.toUtf8().constData());
        return true;
    }
    QSettings store;
    if (!shown && store.value("hidden-messages/shadow-above-original", false).toBool()) return true;     // (always the fix)
    QString list;
    bool added = false;
    QStringList firsts;
    for (const auto& f : flips)
    {
        list += QString(QChar(0x2022)) + " " + T("%1: %2 would come before %3").arg(f.model, f.to, f.from) + "\n";
        added = added || f.added;
        if (!firsts.contains(f.to)) firsts << f.to;
    }
    const QString lead = flips.size() == 1
        ? T("A shadow of %1 in %2 would come before its original in %3.").arg(flips[0].model, flips[0].to, flips[0].from)
        : T("%1 shadows would come before their originals.").arg(flips.size());
    QMessageBox box(QMessageBox::Question, T("A shadow cannot go above its original"), lead, QMessageBox::NoButton, this);
    box.setObjectName("ConfirmShadow");
    QPushButton* fix = box.addButton(firsts.size() == 1 ? T("Make %1 the original").arg(firsts[0]) : T("Make the first uses the originals"),
                                     QMessageBox::AcceptRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(fix);
    box.setInformativeText(
        list + "\n" +
        T("The model tree gives a model its row under the first statement that uses it: that is the original, and every later use is a "
        "shadow. A use that comes first would take that role, so the row would move there and the old place would only show a "
        "shadow: the original cannot stay where it is.") + "\n\n" +
        (added ? T("The easy fix: let the first use be the original place (a model's definition goes directly above it, when nothing in "
                   "between uses it) and leave a reference where the original was now.")
               : T("The easy fix: let the first use be the original place and leave a reference where the original was now.")) +
        "\n\n" + T("Cancel leaves everything as it is. The whole drop is one undo step (Ctrl+Z)."));
    auto again = new QCheckBox(T("Do not show this message again (always make the first use the original)"));
    box.setCheckBox(again);
    box.exec();
    if (box.clickedButton() != fix) return false;
    if (again->isChecked() && !shown) store.setValue("hidden-messages/shadow-above-original", true);
    return true;
}

////////////////////////////////////////////////////////////////////////////////
// Dropping

void ScenePanel::finishDrop(const QString& overKey, int at, const QPoint& global, bool copy)
{
    // The scene is a run behind the text when something was written that was not worked out here (the lines that made a model
    // ready to be dragged): the drop goes ahead when the script has run.  (An edit of a drop is worked out: nothing waits)
    if (m_editPending && m_editClock.elapsed() < 20000)
    {
        m_afterRun = [=] { finishDrop(overKey, at, global, copy); };
        return;
    }
    m_prepareTimer.stop();
    const QList<QJsonObject> models = !m_dropModels.isEmpty() ? m_dropModels : draggedModels();
    if (models.isEmpty()) return;
    QJsonObject target;                     // the model of the row dropped over (for a shadow or a placeholder: the statement it is in)
    QString shadowOf;                       // ... and, for a shadow, the model it stands for
    int holeOf = -1;                        // ... and, for a placeholder, which of the statement's it is
    if (!overKey.isEmpty())
    {
        for (QTreeWidgetItemIterator i(m_tree); *i; ++i)
        {
            const QString type = (*i)->data(COL_NAME, ROLE_TYPE).toString();
            if ((*i)->data(COL_NAME, ROLE_KEY).toString() == overKey && (type == "item" || type == "dep" || type == "hole" || type == "bcgroup"))
            {
                target = (*i)->data(COL_NAME, ROLE_ITEM).toJsonObject();
                if (type == "dep") shadowOf = (*i)->data(COL_NAME, ROLE_PART).toString();
                if (type == "hole") holeOf = (*i)->data(COL_NAME, ROLE_PART).toInt();
                break;
            }
        }
        if (target.isEmpty()) return;
    }
    // Dropped between rows: the call whose models the row stands among, and the model of it that the drop is next to
    QJsonObject parent;
    QString relative;
    if (at != DropOn)
    {
        if (!shadowOf.isEmpty())
        {
            parent = target;
            relative = shadowOf;
        }
        else if (!target.isEmpty() && target.contains("owner"))
        {
            parent = itemByKey(target["owner"].toString());
            relative = target["var"].toString();
        }
    }
    // (the inputs of a rewritten statement are only known once the script has run: what needs them waits for it)
    QList<QJsonObject> involved;
    if (at == DropOn) involved << target;
    involved << parent;
    for (const auto& m : models) involved << sourceStatement(m);
    for (const auto& o : involved)
    {
        if (!o["stale"].toBool()) continue;
        m_afterRun = [=] { finishDrop(overKey, at, global, copy); };
        return;
    }
    // (the script was edited by hand since it ran: the lines the scene names are not those of the text)
    for (const auto& m : models + involved)
    {
        if (m.isEmpty()) continue;
        const QString head = lineText(m["line"].toInt() - 1).trimmed();
        if (!head.startsWith(m["var"].toString()))
        {
            notify(T("The script has changed since it ran: drop again once it has."));
            return;
        }
    }
    if (at == DropOn)
    {
        if (holeOf >= 0)
        {
            // On a placeholder: the model takes its place, at once
            const OnPlan plan = planFill(models, target, holeOf);
            if (!plan.ok)
            {
                notify(plan.why);
                return;
            }
            dropOnModel(models, target, copy, &plan);
            return;
        }
        if (!shadowOf.isEmpty())
        {
            // On a shadow: a model of its kind takes its place in the call, and the reference goes
            const OnPlan plan = planReplace(models, target, shadowOf);
            if (!plan.ok)
            {
                notify(plan.why);
                return;
            }
            dropOnModel(models, target, copy, &plan);
            return;
        }
        // On a row: what can go into it goes into it; what cannot, and is of the kind of a model a call is given, takes its place
        const bool nests = takesInput(target) || target["hole"].toBool() || (allFields(models) && takesNumber(target))
                           || (allPoints(models) && takesPoint(target)) || regionDrop(target, models);
        if (!nests || !planDropOn(models, target).ok)
        {
            const QJsonObject owner = target.contains("owner") ? itemByKey(target["owner"].toString()) : QJsonObject();
            bool same = true;
            for (const auto& m : models) same = same && sameKind(m, target);
            if (!owner.isEmpty() && same)
            {
                if (owner["stale"].toBool())
                {
                    m_afterRun = [=] { finishDrop(overKey, at, global, copy); };
                    return;
                }
                const OnPlan plan = planReplace(models, owner, target["var"].toString());
                if (!plan.ok)
                {
                    notify(plan.why);
                    return;
                }
                dropOnModel(models, owner, copy, &plan);
                return;
            }
        }
        dropOnModel(models, target, copy);
        return;
    }
    if (!parent.isEmpty())
    {
        insertAmong(models, parent, relative, at == DropBelow, copy);
        return;
    }
    if (copy)
    {
        bool locked = false;
        for (const auto& m : models) locked = locked || (!m_dropShadow && m.contains("locked"));
        notify(locked ? T("A locked model is read-only, so dragging it never moves it: it makes a reference to it, and a reference only has "
                          "a place inside a call. Between top-level rows, or in the empty space, there is no call for it to be an argument "
                          "of: drop it on an operation, or between the models one is made of.")
                      : T("A copy is a reference to a model, and a reference only has a place inside a call. Between top-level rows, or in the "
                          "empty space, there is no call for it to be an argument of: drop it on an operation, or between the models one is made of."));
        return;
    }

    // Between two rows that are no models of a call: the models leave the calls they were in, and stand there in the script
    QStringList names;
    for (const auto& m : models) names << m["var"].toString();
    const QStringList before = (m_source ? m_source() : QString()).split('\n');
    QStringList lines = before;
    QVector<int> origin(lines.size());
    for (int i = 0; i < origin.size(); ++i) origin[i] = i;
    Rewire w;
    QList<QJsonObject> moving;              // (what is dragged out of a call as a shadow is a reference: its model stays where it is)
    for (const auto& m : models)
    {
        const QJsonObject src = sourceStatement(m);
        if (!src.isEmpty()) wireRemove(w, src, m);
        if (!m_dropShadow) moving << m;
    }
    if (moving.isEmpty() && w.edits.isEmpty()) return;
    QString what;
    {
        QStringList from;
        for (const auto& m : models)
        {
            const QJsonObject src = sourceStatement(m);
            if (!src.isEmpty() && !from.contains(src["var"].toString())) from << src["var"].toString();
        }
        what = T("Take %1 out of %2").arg(names.join(", "), from.isEmpty() ? T("its call") : from.join(", "));
    }
    QStringList targets;
    QHash<QString, int> newLength;
    QString why;
    if (!wireStage(w, lines, &targets, &newLength, &why))
    {
        notify(why);
        return;
    }
    // A model that was inside a call and is dragged out to the top level stands there on its own: the other statements that use it hold a
    // reference to it (`# shadow: name`) instead of becoming its owner and taking its row under theirs (the next of them would have it, and the
    // model would still be nested in a call -- only another one)
    if (!m_dropShadow)
    {
        for (const auto& m : moving)
        {
            if (!m.contains("owner") || !m.contains("var")) continue;
            const QString name = m["var"].toString();
            const QString from = sourceStatement(m)["var"].toString();
            for (const auto v : m_scene["items"].toArray())
            {
                const auto u = v.toObject();
                const QString uname = u["var"].toString();
                if (uname.isEmpty() || uname == name || uname == from || !directInputs(u).contains(name)) continue;
                const int last = u["end_line"].toInt() - 1;
                if (last >= 0 && last < lines.size()) lines[last] = markShadow(lines[last], name);
            }
        }
    }
    QList<Dissolve> dissolve;
    if (!moving.isEmpty())
    {
        int place = int(lines.size());
        if (!target.isEmpty()) place = at == DropAbove ? groupStart(target) : groupEnd(target);
        else while (place > 0 && lines[place - 1].trimmed().isEmpty()) --place;

        // A top-level model that statements hold references to (`# shadow: name`), dragged below the first of them: the reference
        // is the first use then, so the model goes directly above that statement and becomes its own (a message asks first)
        QList<QJsonObject> rest;
        for (const auto& m : moving)
        {
            QJsonObject first;
            if (!m.contains("owner") && m.contains("var"))
            {
                for (const auto v : m_scene["items"].toArray())
                {
                    const auto u = v.toObject();
                    bool marked = false;
                    for (const auto sv : u["shadows"].toArray()) marked = marked || sv.toString() == m["var"].toString();
                    if (!marked || u["line"].toInt() <= m["line"].toInt() || groupStart(u) >= place) continue;
                    if (first.isEmpty() || u["line"].toInt() < first["line"].toInt()) first = u;
                }
            }
            if (first.isEmpty())
            {
                rest << m;
                continue;
            }
            for (int l = first["line"].toInt() - 1; l <= first["end_line"].toInt() - 1 && l < lines.size(); ++l)
                lines[l] = unmarkShadow(lines[l], m["var"].toString());
            QString ignored;
            if (!moveGroups(lines, &origin, {m}, groupStart(first), false, &ignored))
            {
                notify(ignored);
                return;
            }
            dissolve << Dissolve{m, first};
        }
        // A statement dragged above a top-level model it only references: the model is needed by it, so its definition goes along
        // (directly above the statement) -- and then the reference is the first use, the same
        QList<QJsonObject> pulled;
        {
            QStringList probe = lines;
            QVector<int> probeOrigin = origin;
            QString ignored;
            if (!rest.isEmpty() && moveGroups(probe, &probeOrigin, rest, place, false, &ignored, &pulled))
            {
                for (const auto& p : pulled)
                {
                    if (p.contains("owner") || !p.contains("var")) continue;
                    for (const auto& u : rest + pulled)
                    {
                        bool marked = false;
                        for (const auto sv : u["shadows"].toArray()) marked = marked || sv.toString() == p["var"].toString();
                        if (!marked || u["line"].toInt() <= p["line"].toInt()) continue;
                        for (int l = u["line"].toInt() - 1; l <= u["end_line"].toInt() - 1 && l < lines.size(); ++l)
                            lines[l] = unmarkShadow(lines[l], p["var"].toString());
                        dissolve << Dissolve{p, u};
                        break;
                    }
                }
            }
        }
        if (!rest.isEmpty())
        {
            // What was taken out of a call is out of it, whatever its new place: a place that would put its definition below a statement
            // that uses it (or that the script cannot have it at) leaves the model where its definition is and the call a placeholder.
            // Only a plain move, with nothing taken out of a call, is refused
            QStringList moved = lines;
            QVector<int> movedOrigin = origin;
            if (moveGroups(moved, &movedOrigin, rest, place, false, &why))
            {
                lines = moved;
                origin = movedOrigin;
            }
            else if (w.edits.isEmpty())
            {
                notify(why);
                return;
            }
        }
        if (!dissolve.isEmpty() && !confirmDissolve(dissolve)) return;
        for (const auto& d : dissolve)
            if (!targets.contains(d.user["var"].toString())) targets << d.user["var"].toString();
    }
    wireDropGone(lines, origin);
    const bool left = !w.edits.isEmpty();
    applyLines(before, lines, left ? what : "Move " + names.join(", "), origin, QList<ColShift>(), targets, newLength);
    m_prepareAgain = true;              // (what is selected is made ready to be dragged, once the script has run)
}

bool ScenePanel::confirmDissolve(const QList<Dissolve>& list)
{
    if (list.isEmpty()) return true;
    // (a scripted test does not wait for the question, unless it asks for it: FIELDES_SHOW_DIALOGS)
    const bool shown = qEnvironmentVariableIsSet("FIELDES_SHOW_DIALOGS");
    auto name = [](const QJsonObject& it) { return "'" + (it.contains("var") ? it["var"].toString() : it["label"].toString()) + "'"; };
    if (qEnvironmentVariableIsSet("FIELDES_AUTOMATION") && !shown)
    {
        for (const auto& d : list)
            fprintf(stderr, "[automation] %s would be above %s, which it holds a reference to: %s becomes part of it\n",
                    name(d.user).toUtf8().constData(), name(d.model).toUtf8().constData(), name(d.model).toUtf8().constData());
        return true;
    }
    QSettings store;
    if (!shown && store.value("hidden-messages/shadow-top-level", false).toBool()) return true;      // (always the fix)
    QString items;
    QStringList users;
    for (const auto& d : list)
    {
        items += QString(QChar(0x2022)) + " " + T("%1 would be above %2, which it only references").arg(name(d.user), name(d.model)) + "\n";
        if (!users.contains(name(d.user))) users << name(d.user);
    }
    const QString lead = list.size() == 1
        ? T("%1 would come before %2, which it holds a reference to.").arg(name(list[0].user), name(list[0].model))
        : T("%1 statements would come before models they hold references to.").arg(list.size());
    QMessageBox box(QMessageBox::Question, T("A shadow cannot stand alone in the tree"), lead, QMessageBox::NoButton, this);
    box.setObjectName("ConfirmShadow");
    QPushButton* fix = box.addButton(list.size() == 1 ? T("Make %1 part of %2").arg(name(list[0].model), name(list[0].user))
                                                      : T("Make them part of the statements above them"),
                                     QMessageBox::AcceptRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(fix);
    box.setInformativeText(
        items + "\n" +
        T("A model has its real row under the first statement that uses it, and a reference to it is a shadow. A shadow can only be an "
        "argument of a statement: it never stands by itself in the tree. The model keeps its own row at the top level only while the "
        "statements that use it merely hold a reference (their line says '# shadow: name'). Once such a statement is above it, that "
        "use comes first, so the one inside the statement is the real model.") + "\n\n" +
        T("The easy fix: the model's definition goes directly above that statement and becomes the statement's own, and the separate "
        "row at the top level is gone (a statement that uses it too keeps a shadow of it).") + "\n\n" +
        T("Cancel leaves everything as it is. The whole drop is one undo step (Ctrl+Z)."));
    auto again = new QCheckBox(T("Do not show this message again (always do this)"));
    box.setCheckBox(again);
    box.exec();
    if (box.clickedButton() != fix) return false;
    if (again->isChecked() && !shown) store.setValue("hidden-messages/shadow-top-level", true);
    return true;
}

void ScenePanel::markShadows(QStringList& lines, const QList<QJsonObject>& models, const QJsonObject& target, int last, bool copy,
                             bool reference) const
{
    // A reference to a model that nothing else uses is MARKED in the call's statement (`# shadow: e`): its first use would make the
    // statement the owner and move the model's row under it, and a Ctrl+drag leaves the model where it is. A model that is moved
    // into the call for good (a plain drag) is the call's own, so it is not marked there, and a reference to it that was dragged
    // from another call is not marked in that one any more
    const int first = target["line"].toInt() - 1;
    for (const auto& m : models)
    {
        const QString name = m["var"].toString();
        for (int l = first; l <= last && l < lines.size(); ++l) lines[l] = unmarkShadow(lines[l], name);
        if (!copy)
        {
            const QJsonObject src = sourceStatement(m);
            if (!src.isEmpty())
                for (int l = src["line"].toInt() - 1; l <= src["end_line"].toInt() - 1 && l < lines.size(); ++l)
                    lines[l] = unmarkShadow(lines[l], name);
        }
        if (reference && !m.contains("owner") && last >= 0 && last < lines.size()) lines[last] = markShadow(lines[last], name);
    }
}

void ScenePanel::insertAmong(const QList<QJsonObject>& models, const QJsonObject& parent, const QString& relative, bool after,
                             bool copy)
{
    const QString pname = parent["var"].toString();
    QStringList names;
    for (const auto& m : models) names << m["var"].toString();
    if (names.contains(relative))
    {
        if (models.size() > 1)
            notify(T("The models were dropped next to one of themselves, so there is no place to put them: drop them next to a model "
                   "that is not among the dragged ones."));
        return;                             // (dropped next to itself: nothing changes)
    }
    const QStringList have = directInputs(parent);
    Rewire w;
    QStringList moved;
    for (const auto& m : models)
    {
        const QString name = m["var"].toString();
        if (name == pname || dependsOn(m, parent))
        {
            notify(T("%1 is made of %2, directly or through other models, so it cannot also be one of its arguments: "
                   "that would be a loop (%2 needs %1 to be built, and %1 would need %2).").arg(name, pname));
            return;
        }
        const bool already = have.contains(name);
        const QJsonObject src = sourceStatement(m);
        const bool fromHere = !src.isEmpty() && src["line"].toInt() == parent["line"].toInt();
        if (copy && already)
        {
            notify(T("%1 is already one of the models %2 is given: a copy would put the same model in twice, which "
                   "changes nothing.").arg(name, pname));
            return;
        }
        if (already && !fromHere)
        {
            notify(T("%1 is already one of the models %2 is given (it stands under it in the tree), so dragging it from "
                   "elsewhere would give it twice. Drag the one that stands under %2 to move it.").arg(name, pname));
            return;
        }
        if (!already && !parent["variadic"].toBool())
        {
            notify(T("%1 works on exactly the models it is given (%2), so there is no place between them "
                   "for another: a new model has to take the place of one. Drop %3 on %1 to choose which one, "
                   "or drop one of its own models here to reorder them.").arg(pname, have.join(", "), name));
            return;
        }
        if (!copy)
        {
            if (fromHere)
            {
                // (moved within the call: out of its old place, into the new one)
                auto& change = w.edits[parent["line"].toInt()];
                change.item = parent;
                change.remove << name;
            }
            else if (!src.isEmpty())
            {
                wireRemove(w, src, m);
            }
        }
        wireInsert(w, parent, name, relative, after ? "after" : "before");
        moved << name;
    }
    const QStringList before = (m_source ? m_source() : QString()).split('\n');
    QStringList lines = before;
    QVector<int> origin(lines.size());
    for (int i = 0; i < origin.size(); ++i) origin[i] = i;
    const QString what = (copy ? T("Use %1 in %2") : T("Move %1 in %2")).arg(names.join(", "), pname);
    bool above = false;
    if (copy || m_dropShadow)
    {
        // (the references this drop adds: whether one is above the original is asked once, when the tree it gives is known)
        above = isAboveOriginal(models, parent);
        for (const auto& m : models) m_copyAdds << qMakePair(m["var"].toString(), pname);
    }
    QStringList targets;
    QHash<QString, int> newLength;
    QString why;
    if (!wireStage(w, lines, &targets, &newLength, &why))
    {
        notify(why);
        return;
    }
    {
        const int len = newLength.contains(pname) ? newLength.value(pname) : parent["end_line"].toInt() - parent["line"].toInt() + 1;
        markShadows(lines, models, parent, parent["line"].toInt() - 1 + len - 1, copy, copy || m_dropShadow);
    }
    // (what is defined below the call moves up, with what it is made of: it has to be there first)
    bool placed = false;
    if (above)
    {
        // the fix: the model is now the original of this call, and its definition goes directly above it when nothing in between
        // uses it (if something does, it stays where it is, which is above the call already)
        QStringList tidied = lines;
        QVector<int> tidiedOrigin = origin;
        QString ignored;
        if (moveGroups(tidied, &tidiedOrigin, models, groupStart(parent), false, &ignored))
        {
            lines = tidied;
            origin = tidiedOrigin;
            placed = true;
        }
    }
    if (!placed && !moveGroups(lines, &origin, models, groupStart(parent), true, &why))
    {
        notify(why);
        return;
    }
    wireDropGone(lines, origin);
    applyLines(before, lines, what, origin, QList<ColShift>(), targets, newLength);
    m_prepareAgain = true;
}

void ScenePanel::dropOnModel(const QList<QJsonObject>& models, const QJsonObject& target, bool copy, const OnPlan* given)
{
    QStringList names;
    for (const auto& m : models) names << m["var"].toString();
    const QString tname = target["var"].toString();

    // Writes the models' names into the target's call -- in place of an input, or after the last one -- and makes sure they are
    // defined before it (what is below it moves up, with what it is made of): one edit of the script.  A model that was an input of
    // another call is taken out of that one (unless this is a copy)
    const OnPlan plan0 = given ? *given : planDropOn(models, target);
    if (plan0.ok && plan0.own && !copy)
    {
        // (the call held them by reference, `# shadow: name`: the mark goes, and the first user that is not a reference owns them)
        const QStringList before = (m_source ? m_source() : QString()).split('\n');
        QStringList lines = before;
        QVector<int> origin(lines.size());
        for (int i = 0; i < origin.size(); ++i) origin[i] = i;
        for (const auto& m : models)
            for (int l = target["line"].toInt() - 1; l <= target["end_line"].toInt() - 1 && l < lines.size(); ++l)
                lines[l] = unmarkShadow(lines[l], m["var"].toString());
        applyLines(before, lines, plan0.what, origin, QList<ColShift>(), QStringList{tname}, QHash<QString, int>());
        m_prepareAgain = true;
        return;
    }
    if (plan0.ok && plan0.own && copy)
    {
        notify((names.size() > 1 ? T("%1 are already referenced by %2 (a shadow of it is there), so a copy changes nothing. Drag it without Ctrl to make it %2's own: its row moves in.")
                                 : T("%1 is already referenced by %2 (a shadow of it is there), so a copy changes nothing. Drag it without Ctrl to make it %2's own: its row moves in."))
                   .arg(names.join(", "), tname));
        return;
    }
    const auto run = [=](const QJsonArray& replace, const QJsonArray& after, const QString& text, bool above) {
        const OnPlan& plan = plan0;
        Rewire w;
        QStringList from;
        if (!copy)
        {
            for (const auto& m : models)
            {
                const QJsonObject src = sourceStatement(m);
                if (src.isEmpty() || src["line"].toInt() == target["line"].toInt()) continue;
                wireRemove(w, src, m);
                if (!from.contains(src["var"].toString())) from << src["var"].toString();
            }
        }
        const QStringList before = (m_source ? m_source() : QString()).split('\n');
        QStringList lines = before;
        QVector<int> origin(lines.size());
        for (int i = 0; i < origin.size(); ++i) origin[i] = i;
        // (the place the models are written into: one, or several when the drop goes to several inputs of the call -- the last place of the
        // text first, so that the places before it stay where they are)
        struct Write { QJsonArray span; bool replaces; QString text; };
        QList<Write> writes;
        writes << Write{replace.isEmpty() ? after : replace, !replace.isEmpty(), text};
        for (const auto& pc : plan.more) writes << Write{pc.replace.isEmpty() ? pc.after : pc.replace, !pc.replace.isEmpty(), pc.text};
        std::sort(writes.begin(), writes.end(), [](const Write& a, const Write& b) {
            const int al = a.span.size() == 4 ? a.span[2].toInt() : 0, bl = b.span.size() == 4 ? b.span[2].toInt() : 0;
            return al != bl ? al > bl : (a.span.size() == 4 ? a.span[3].toInt() : 0) > (b.span.size() == 4 ? b.span[3].toInt() : 0);
        });
        QList<ColShift> shifts;
        for (const Write& w : writes)
        {
            const int l0 = w.span.size() == 4 ? w.span[0].toInt() - 1 : -1;
            if (l0 < 0 || l0 >= lines.size() || w.span[2].toInt() - 1 != l0)
            {
                notify(T("Could not find where %1 takes its models.").arg(tname));
                return;
            }
            const int c0 = w.span[1].toInt(), c1 = w.span[3].toInt();
            if (!w.replaces)
            {
                lines[l0].insert(c1, ", " + w.text);
                shifts << ColShift{l0 + 1, c1, int(w.text.size()) + 2};
            }
            else
            {
                lines[l0].replace(c0, c1 - c0, w.text);
                shifts << ColShift{l0 + 1, c1, int(w.text.size()) - (c1 - c0)};
            }
        }
        if (!plan.drop.isEmpty())
        {
            // (a model that was replaced is not used by the call any more: if the call held it by reference, the mark goes too)
            QString text;
            for (int l = target["line"].toInt() - 1; l <= target["end_line"].toInt() - 1 && l < lines.size(); ++l) text += lines[l] + "\n";
            text.remove(shadowComment());
            if (!QRegularExpression("\\b" + QRegularExpression::escape(plan.drop) + "\\b").match(text).hasMatch())
                for (int l = target["line"].toInt() - 1; l <= target["end_line"].toInt() - 1 && l < lines.size(); ++l)
                    lines[l] = unmarkShadow(lines[l], plan.drop);
        }
        // The numbers of the target, and of what is made of it, are not what they were: an expose() line written for them
        // does not fit any more (the lines are marked, and go once the models have been moved)
        for (const auto v : m_scene["items"].toArray())
        {
            const auto d = v.toObject();
            if (!d.contains("exposed") || !d.contains("var")) continue;
            if (d["var"].toString() != tname && !dependsOn(d, target)) continue;
            const auto ex = d["exposed"].toObject();
            for (int l = ex["line"].toInt() - 1; l <= ex["end_line"].toInt() - 1; ++l)
                if (l >= 0 && l < lines.size()) lines[l] = kGone;
        }
        QStringList targets{tname};
        QHash<QString, int> newLength;
        QString why;
        if (!wireStage(w, lines, &targets, &newLength, &why))
        {
            notify(why);
            return;
        }
        markShadows(lines, models, target, target["end_line"].toInt() - 1, copy, (copy || m_dropShadow) && !allFields(models) && !allPoints(models));
        bool placed = false;
        if (above)
        {
            // the fix: the model is now the original of this call, and its definition goes directly above it when nothing in between
            // uses it (if something does, it stays where it is, which is above the call already)
            QStringList tidied = lines;
            QVector<int> tidiedOrigin = origin;
            QString ignored;
            if (moveGroups(tidied, &tidiedOrigin, models, groupStart(target), false, &ignored))
            {
                lines = tidied;
                origin = tidiedOrigin;
                placed = true;
            }
        }
        if (!placed && !moveGroups(lines, &origin, models, groupStart(target), true, &why))
        {
            notify(why);
            return;
        }
        wireDropGone(lines, origin);
        applyLines(before, lines, plan.what.isEmpty() ? (copy ? T("Use %1 in %2") : T("Move %1 in %2")).arg(names.join(", "), tname) : plan.what,
                   origin, shifts, targets, newLength);
        m_prepareAgain = true;
    };

    // (no question about what goes where: the plan is what the rules say, or the reason it cannot be done; the one question there
    // is, is a reference that would go above the original)
    if (!plan0.ok)
    {
        notify(plan0.why);
        return;
    }
    bool above = false;
    if ((copy || m_dropShadow) && !allFields(models) && !allPoints(models))
    {
        above = isAboveOriginal(models, target);
        for (const auto& m : models) m_copyAdds << qMakePair(m["var"].toString(), tname);
    }
    run(plan0.replace, plan0.after, plan0.text, above);
}


////////////////////////////////////////////////////////////////////////////////
// Renaming a variable

bool ScenePanel::applyRename(const QString& oldName, const QString& newText, bool live)
{
    const QString newName = newText.trimmed();
    if (newName == oldName) return true;
    if (newName.isEmpty() || !m_support || !m_source) return false;
    QJsonObject request;
    request["source"] = m_source();
    request["old"] = oldName;
    request["new"] = newName;
    QString error;
    const QString answer = m_support("rename_edits", QString::fromUtf8(QJsonDocument(request).toJson(QJsonDocument::Compact)),
                                     &error);
    if (answer.isEmpty())
    {
        if (!live) notify(error.isEmpty() ? T("Could not rename %1.").arg(oldName) : error);
        return false;
    }
    const auto edits = QJsonDocument::fromJson(answer.toUtf8()).object()["edits"].toArray();
    if (edits.isEmpty())
    {
        if (!live) notify(T("%1 is not used in the script.").arg(oldName));
        return false;
    }
    QList<TextEdit> out;
    QList<ColShift> shifts;
    const int delta = int(newName.size() - oldName.size());
    for (const auto ev : edits)
    {
        const auto e = ev.toArray();                            // [line (0-based), first column, end column]
        out << TextEdit{e[0].toInt(), e[1].toInt(), e[0].toInt(), e[2].toInt(), newName};
        shifts << ColShift{e[0].toInt() + 1, e[2].toInt(), delta};
    }
    if (live) emit(editScriptLive(out, "Rename " + oldName + " to " + newName));
    else emit(editScript(out, "Rename " + oldName + " to " + newName));
    // (at once)
    const int n = (m_source ? m_source() : QString()).split('\n').size();
    QVector<int> origin(n);
    for (int i = 0; i < n; ++i) origin[i] = i;
    predictScene(n, QStringList(), origin, shifts, QStringList(), QHash<QString, int>(), oldName, newName);
    return true;
}

void ScenePanel::liveRename(QTreeWidgetItem* row, const QString& typed)
{
    const QString name = row->data(COL_NAME, ROLE_RENAME).toString();           // (what the script calls it now)
    if (!(m_renaming.active && m_renaming.current == name)) m_renaming = Renaming{name, name, true};
    if (applyRename(name, typed, true)) m_renaming.current = typed.trimmed();
}

void ScenePanel::endRenaming(bool keep)
{
    const Renaming r = m_renaming;
    m_renaming = Renaming();
    // (Escape: the name it had when the typing began)
    if (!keep) m_revertClock.start();
    if (!keep && r.active && r.current != r.original) applyRename(r.current, r.original, true);
}

bool ScenePanel::staleWhileRenaming(const QJsonObject& scene) const
{
    // (only while the name is being typed: a name editor that is gone -- it lost the keyboard, the tree was cleared -- holds nothing back)
    if (!m_renaming.active || !static_cast<SceneTree*>(m_tree)->isEditing()) return false;
    for (const auto v : scene["items"].toArray())
        if (v.toObject()["var"].toString() == m_renaming.current) return false;
    return true;
}

////////////////////////////////////////////////////////////////////////////////
// What the guided tour and the tests need

QWidget* ScenePanel::treeViewport() const
{
    return m_tree->viewport();
}

int ScenePanel::treeScroll() const
{
    return m_tree->verticalScrollBar()->value();
}

bool ScenePanel::isShadow(QTreeWidgetItem* row) const
{
    return row && row->data(COL_NAME, ROLE_TYPE).toString() == "dep";
}

QTreeWidgetItem* ScenePanel::rowUnder(const QString& parentVar, const QString& modelVar) const
{
    for (QTreeWidgetItemIterator i(m_tree); *i; ++i)
    {
        QTreeWidgetItem* r = *i;
        QTreeWidgetItem* p = r->parent();
        if (!p || p->data(COL_NAME, ROLE_ITEM).toJsonObject()["var"].toString() != parentVar) continue;
        const QString type = r->data(COL_NAME, ROLE_TYPE).toString();
        if (type == "hole" && (modelVar == "placeholder" || r->text(COL_NAME) == modelVar)) return r;    // (the first placeholder of the statement, or the one for that input)
        if (type == "bcgroup" && modelVar == "boundary conditions") return r;      // (the conditions row of the simulation)
        if (type != "dep" && type != "item") continue;
        const QString name = type == "dep" ? r->data(COL_NAME, ROLE_PART).toString()
                                           : r->data(COL_NAME, ROLE_ITEM).toJsonObject()["var"].toString();
        if (name == modelVar) return r;
    }
    return nullptr;
}

QRect ScenePanel::rowRect(const QString& prefix, QWidget* in)
{
    QTreeWidgetItem* row = nullptr;
    for (auto r : m_tree->findItems(prefix, Qt::MatchStartsWith | Qt::MatchRecursive, 0))
    {
        if (!isShadow(r)) { row = r; break; }           // (a shadow is the reference to a model, not the model)
    }
    if (!row) return QRect();
    expandTo(row);
    m_tree->scrollToItem(row);
    const QRect r = m_tree->visualItemRect(row);
    if (r.isEmpty()) return QRect();
    const QRect full(0, r.top(), m_tree->viewport()->width(), r.height());
    return QRect(m_tree->viewport()->mapTo(in, full.topLeft()), full.size());
}

void ScenePanel::showSettings(bool open)
{
    for (int k = 0; k < m_tree->topLevelItemCount(); ++k)
    {
        auto r = m_tree->topLevelItem(k);
        if (r->data(COL_NAME, ROLE_TYPE).toString() == "settings") r->setExpanded(open);
    }
}

bool ScenePanel::settingsOpen() const
{
    for (int k = 0; k < m_tree->topLevelItemCount(); ++k)
    {
        auto r = m_tree->topLevelItem(k);
        if (r->data(COL_NAME, ROLE_TYPE).toString() == "settings") return r->isExpanded();
    }
    return false;
}

QLineEdit* ScenePanel::settingEditor(const QString& fn, int index) const
{
    const auto list = m_settingEditors.value(fn);
    return index >= 0 && index < list.size() ? list[index].data() : nullptr;
}

////////////////////////////////////////////////////////////////////////////////
// The render settings, edited in the tree

void ScenePanel::insertSettingLine(const QString& call, const QString& fn, bool live)
{
    // After the last of the render settings the script has, else after the last import, else at the top
    const QStringList lines = (m_source ? m_source() : QString()).split('\n');
    int after = -1;
    const auto settings = m_scene["settings"].toObject();
    for (const QString& fn : {"set_bounds", "set_resolution", "set_quality"})
    {
        if (settings.contains(fn) && settings[fn].toObject().contains("end_line"))
            after = std::max(after, settings[fn].toObject()["end_line"].toInt() - 1);
    }
    if (after < 0)
    {
        for (int i = 0; i < lines.size(); ++i)
        {
            if (lines[i].startsWith("import ") || lines[i].startsWith("from ")) after = i;
        }
    }
    const QString what = "Set " + call.section('(', 0, 0);
    QList<TextEdit> edit;
    int line0 = 0;
    if (after >= 0)
    {
        const int len = int(lines[after].size());
        edit << TextEdit{after, len, after, len, "\n" + call};
        line0 = after + 1;
    }
    else
    {
        edit << TextEdit{0, 0, 0, 0, call + "\n"};
    }
    if (live) emit(editScriptLive(edit, what));
    else emit(editScript(edit, what));
    if (!fn.isEmpty()) m_liveSettings[fn] = LiveSetting{line0, 0, call};      // (the next key rewrites this line)
    m_liveSettingStale = true;
    m_editPending = true;               // (a line was added: the scene's lines are a run behind)
    m_editClock.start();
}

// The number of the line `x = custom_resolution(x, number)`, rewritten as it is typed (the line is found in the script as it is now)
void ScenePanel::commitCustomResolution(QLineEdit* edited, bool live, const QString& var)
{
    bool ok = false;
    const double v = QLocale::c().toDouble(edited->text().trimmed(), &ok);
    if (!ok)
    {
        if (!live) edited->setText(edited->property("shown").toString());           // (not a number: the one that was there stays)
        return;
    }
    if (!(v > 0))
    {
        if (live) return;
        edited->setText(edited->property("shown").toString());
        notify(T("The resolution must be more than 0 (samples per mm)."));
        return;
    }
    const QString text = QString::number(v, 'g', 8);
    if (text == edited->property("shown").toString())
    {
        if (!live && edited->hasFocus()) edited->clearFocus();
        return;
    }
    const QString src = m_source ? m_source() : QString();
    const QRegularExpression re("^(\\s*" + QRegularExpression::escape(var) + "\\s*=\\s*custom_resolution\\(\\s*" + QRegularExpression::escape(var) +
                                "\\s*,\\s*)([^,)\\n]+)", QRegularExpression::MultilineOption);
    const auto m = re.match(src);
    if (!m.hasMatch())
    {
        if (!live) notify(T("The line of custom_resolution is not in the script as the tree expects it (%1 = custom_resolution(%1, number)).").arg(var));
        return;
    }
    const int pos = m.capturedStart(2), end = m.capturedEnd(2);
    const int line = int(src.left(pos).count('\n'));
    const int col = pos - (int(src.lastIndexOf('\n', pos - 1)) + 1);
    edited->setProperty("shown", text);
    const QList<TextEdit> edit{TextEdit{line, col, line, col + (end - pos), text}};
    if (live) emit(editScriptLive(edit, "Set custom_resolution"));
    else emit(editScript(edit, "Set custom_resolution"));
    if (!live) edited->clearFocus();
}

void ScenePanel::commitSetting(QLineEdit* edited, bool live)
{
    if (m_rebuilding) return;
    const QString fn = edited->property("fn").toString();
    if (fn.startsWith("custom_resolution:"))
    {
        commitCustomResolution(edited, live, fn.mid(int(QString("custom_resolution:").size())));
        return;
    }
    // (the render settings are not typed into the tree: they are lines of the code, changed in the code editor)
}

}   // namespace FielDes
