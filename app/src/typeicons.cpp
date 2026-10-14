/*
FielDes: field-driven design
Derived from Studio, a simple GUI for the libfive CAD kernel

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/
#include <cmath>
#include <functional>

#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPolygonF>
#include <QRadialGradient>

#include "fieldes/i18n.hpp"
#include "fieldes/typeicons.hpp"

namespace FielDes {
namespace TypeIcons {

namespace {

const double kPi = 3.14159265358979323846;

struct Kind { QString label; QColor color; };

// (the same as fieldes/kinds.py says; the interpreter's own list replaces them when it has been asked)
QHash<QString, Kind>& kinds()
{
    static QHash<QString, Kind> k = {
        {"solid",      {"3D shape",     QColor("#4aa8e8")}},
        {"curve",      {"1D shape",     QColor("#c9d3e6")}},
        {"profile",    {"2D shape",     QColor("#35c4b3")}},
        {"field",      {"Field",        QColor("#82cc58")}},
        {"surface",    {"Surface",      QColor("#b583ee")}},
        {"point",      {"Point",        QColor("#f4b73a")}},
        {"simulation", {"Simulation",   QColor("#ee6a5e")}},
        {"material",   {"Material",     QColor("#c9a66b")}},
        {"conditions", {"Conditions",   QColor("#e08f58")}},
        {"cell",       {"Lattice cell", QColor("#d4b43c")}},
        {"lattice",    {"Lattice",      QColor("#e0709c")}},
        {"points",     {"Point set",    QColor("#f0a24c")}},
        {"import",     {"Import",       QColor("#2aa198")}},
        {"symmetry",   {"Symmetry",     QColor("#d33682")}},
        {"block",      {"Custom block", QColor("#8da2c0")}},
        // (not kinds of model: the parts of the right-click menus that are not primitives)
        {"operation",  {"Operation",    QColor("#b9c4d0")}},
        {"engineering", {"Engineering", QColor("#b9c4d0")}},
        {"shapes",     {"Parametric shape", QColor("#7aa2f0")}},
    };
    return k;
}

// The engineering domains: the colour of a simulation and the lighter one of what it is given (the same as fieldes/kinds.py says)
struct Domain { QColor result, conditions; };

QHash<QString, Domain>& domains()
{
    static QHash<QString, Domain> d = {
        {"structural", {QColor("#ee6a5e"), QColor("#f4958c")}},
        {"thermal",    {QColor("#f08c2a"), QColor("#f5b46e")}},
        {"fluid",      {QColor("#6f86f2"), QColor("#a1b0f7")}},
    };
    return d;
}

QHash<QString, QIcon>& cache()
{
    static QHash<QString, QIcon> c;
    return c;
}

// Line-art at 2x, as the other icons of the model tree are
QIcon draw(std::function<void(QPainter&)> paint)
{
    QPixmap px(32, 32);
    px.fill(Qt::transparent);
    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(2, 2);
    paint(p);
    p.end();
    px.setDevicePixelRatio(2);
    return QIcon(px);
}

QColor alpha(QColor c, int a)
{
    c.setAlpha(a);
    return c;
}

void drawKind(QPainter& p, const QString& type, const QColor& c, const QString& domain)
{
    if (type == "curve")
    {
        // a line through points: a spline with its two ends marked
        QPainterPath path;
        path.moveTo(2.4, 11.2);
        path.cubicTo(5.0, 2.6, 9.4, 14.2, 13.6, 4.8);
        p.setPen(QPen(c, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawEllipse(QPointF(2.4, 11.2), 1.7, 1.7);
        p.drawEllipse(QPointF(13.6, 4.8), 1.7, 1.7);
    }
    else if (type == "profile")
    {
        // a flat shape: a square with its fill
        p.setPen(QPen(c, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(alpha(c, 95));
        p.drawRoundedRect(QRectF(2.8, 2.8, 10.4, 10.4), 1.6, 1.6);
    }
    else if (type == "field")
    {
        // a value at every point: a soft disc, solid in the middle
        QRadialGradient g(QPointF(8, 8), 7);
        g.setColorAt(0.0, c);
        g.setColorAt(0.55, alpha(c, 150));
        g.setColorAt(1.0, alpha(c, 25));
        p.setPen(Qt::NoPen);
        p.setBrush(g);
        p.drawEllipse(QPointF(8, 8), 7, 7);
    }
    else if (type == "surface")
    {
        // an open sheet seen at an angle, with a wave across it
        QPolygonF sheet({QPointF(1.5, 11.5), QPointF(5.2, 3.5), QPointF(14.5, 3.5), QPointF(10.8, 11.5)});
        p.setPen(QPen(c, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(alpha(c, 85));
        p.drawPolygon(sheet);
        QPainterPath wave;
        wave.moveTo(3.4, 8.6);
        wave.cubicTo(5.6, 4.8, 8.4, 11.2, 12.6, 6.4);
        p.setBrush(Qt::NoBrush);
        p.drawPath(wave);
    }
    else if (type == "point")
    {
        p.setPen(QPen(alpha(c, 200), 1.3, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(8, 1.6), QPointF(8, 4.6));
        p.drawLine(QPointF(8, 11.4), QPointF(8, 14.4));
        p.drawLine(QPointF(1.6, 8), QPointF(4.6, 8));
        p.drawLine(QPointF(11.4, 8), QPointF(14.4, 8));
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawEllipse(QPointF(8, 8), 2.7, 2.7);
    }
    else if (type == "simulation" && domain == "thermal")
    {
        // a thermal result: a thermometer, its column filled
        p.setPen(QPen(c, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(6.6, 1.8, 2.8, 8.8), 1.4, 1.4);
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawEllipse(QPointF(8, 11.7), 2.7, 2.7);
        p.drawRoundedRect(QRectF(7.25, 5.6, 1.5, 6.0), 0.7, 0.7);
        p.setPen(QPen(alpha(c, 210), 1.2, Qt::SolidLine, Qt::RoundCap));
        for (double y : {3.6, 5.8, 8.0}) p.drawLine(QPointF(11.0, y), QPointF(12.8, y));
    }
    else if (type == "simulation" && domain == "fluid")
    {
        // a flow result: streamlines
        p.setBrush(Qt::NoBrush);
        for (int k = 0; k < 3; ++k)
        {
            const double y = 4.2 + k * 3.8;
            QPainterPath path;
            path.moveTo(1.6, y);
            path.cubicTo(4.6, y - 2.6, 6.4, y + 2.6, 8.2, y);
            path.cubicTo(10.0, y - 2.6, 11.8, y + 2.6, 14.4, y);
            p.setPen(QPen(k == 1 ? c : alpha(c, 185), 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawPath(path);
        }
    }
    else if (type == "simulation" && domain == "manufacturing")
    {
        // a manufacturing result: the layers a part is built of, each a little further out than the one below
        p.setPen(Qt::NoPen);
        const double starts[4] = {4.4, 3.0, 5.2, 2.4};
        const double widths[4] = {6.0, 8.6, 5.4, 9.6};
        for (int i = 0; i < 4; ++i)
        {
            p.setBrush(i == 1 ? c : alpha(c, 175));
            p.drawRoundedRect(QRectF(starts[i], 2.4 + i * 3.1, widths[i], 2.2), 0.8, 0.8);
        }
    }
    else if (type == "simulation")
    {
        // a result: bars of a plot on a base line
        p.setPen(Qt::NoPen);
        const double heights[3] = {5.0, 9.5, 7.0};
        for (int i = 0; i < 3; ++i)
        {
            p.setBrush(i == 1 ? c : alpha(c, 170));
            p.drawRoundedRect(QRectF(2.6 + i * 4.2, 13 - heights[i], 3.2, heights[i]), 0.8, 0.8);
        }
        p.setPen(QPen(alpha(c, 220), 1.2, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(1.8, 13.8), QPointF(14.2, 13.8));
    }
    else if (type == "material")
    {
        // what a part is made of: a block with the hatching of a section
        p.setPen(QPen(c, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(alpha(c, 50));
        p.drawRoundedRect(QRectF(2.4, 3.2, 11.2, 9.6), 1.2, 1.2);
        p.setPen(QPen(c, 1.1, Qt::SolidLine, Qt::RoundCap));
        for (int k = 0; k < 3; ++k) p.drawLine(QPointF(3.6 + 3.2 * k, 12.0), QPointF(6.6 + 3.2 * k, 4.4));
    }
    else if (type == "conditions" && domain == "thermal")
    {
        // what a thermal analysis is given: heat rising from a surface
        p.setBrush(Qt::NoBrush);
        for (int k = 0; k < 3; ++k)
        {
            const double x = 4.2 + k * 3.8;
            QPainterPath path;
            path.moveTo(x, 12.0);
            path.cubicTo(x - 2.2, 9.0, x + 2.2, 6.2, x, 2.6);
            p.setPen(QPen(c, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawPath(path);
        }
        p.setPen(QPen(c, 2.0, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(2.4, 14.0), QPointF(13.6, 14.0));
    }
    else if (type == "conditions" && domain == "fluid")
    {
        // what a flow analysis is given: a flow into a channel between two walls
        p.setPen(QPen(alpha(c, 220), 1.8, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(1.6, 3.0), QPointF(14.4, 3.0));
        p.drawLine(QPointF(1.6, 13.0), QPointF(14.4, 13.0));
        p.setPen(QPen(c, 1.6, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(2.6, 8.0), QPointF(10.6, 8.0));
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawPolygon(QPolygonF({QPointF(9.6, 5.0), QPointF(14.0, 8.0), QPointF(9.6, 11.0)}));
    }
    else if (type == "conditions" && domain == "manufacturing")
    {
        // what a manufacturing check is given: a limit, a gauge with its ticks
        p.setPen(QPen(c, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(alpha(c, 60));
        p.drawRoundedRect(QRectF(1.8, 5.2, 12.4, 5.6), 1.0, 1.0);
        p.setPen(QPen(c, 1.2, Qt::SolidLine, Qt::RoundCap));
        for (int k = 0; k < 4; ++k) p.drawLine(QPointF(4.2 + k * 2.6, 5.4), QPointF(4.2 + k * 2.6, k % 2 == 0 ? 8.6 : 7.4));
    }
    else if (type == "conditions")
    {
        // what an analysis is given: a load pressing down on a support
        p.setPen(QPen(c, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawLine(QPointF(8, 2), QPointF(8, 9.2));
        QPolygonF head({QPointF(4.8, 7.2), QPointF(11.2, 7.2), QPointF(8, 11)});
        p.setBrush(c);
        p.drawPolygon(head);
        p.setPen(QPen(c, 2.0, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(3, 13.4), QPointF(13, 13.4));
    }
    else if (type == "cell")
    {
        // a hexagonal cell
        QPolygonF hex;
        for (int k = 0; k < 6; ++k)
        {
            const double a = kPi / 3 * k + kPi / 6;
            hex << QPointF(8 + 6.4 * std::cos(a), 8 + 6.4 * std::sin(a));
        }
        p.setPen(QPen(c, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(alpha(c, 60));
        p.drawPolygon(hex);
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawEllipse(QPointF(8, 8), 1.6, 1.6);
    }
    else if (type == "lattice")
    {
        // a body filled with cells: a small cube of struts -- the edges and the diagonals of its faces
        const QPointF v0(3.2, 4.4), v1(11.2, 4.4), v2(13.2, 2.4), v3(5.2, 2.4), v4(3.2, 12.4), v5(11.2, 12.4), v6(13.2, 10.4), v7(5.2, 10.4);
        p.setPen(QPen(alpha(c, 200), 1.25, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawLine(v0, v1); p.drawLine(v1, v2); p.drawLine(v2, v3); p.drawLine(v3, v0);
        p.drawLine(v4, v5); p.drawLine(v5, v6); p.drawLine(v6, v7); p.drawLine(v7, v4);
        p.drawLine(v0, v4); p.drawLine(v1, v5); p.drawLine(v2, v6); p.drawLine(v3, v7);
        p.setPen(QPen(c, 1.25, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(v0, v5); p.drawLine(v1, v6); p.drawLine(v7, v0);
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        for (const QPointF& n : {v0, v1, v4, v5}) p.drawEllipse(n, 1.5, 1.5);
    }
    else if (type == "points")
    {
        // a set of points: a scatter
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        const QPointF dots[7] = {QPointF(3.4, 4.2), QPointF(8.2, 2.8), QPointF(12.6, 5.6), QPointF(6.0, 8.6), QPointF(11.0, 10.8),
                                 QPointF(3.6, 12.6), QPointF(8.6, 13.0)};
        for (int k = 0; k < 7; ++k) p.drawEllipse(dots[k], k % 3 == 0 ? 1.9 : 1.5, k % 3 == 0 ? 1.9 : 1.5);
    }
    else if (type == "import")
    {
        // a file read in: an open box
        p.setPen(QPen(c, 1.3));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(3, 5, 8, 8));
        p.drawLine(QPointF(3, 5), QPointF(6, 2));
        p.drawLine(QPointF(11, 5), QPointF(14, 2));
        p.drawLine(QPointF(6, 2), QPointF(14, 2));
        p.drawLine(QPointF(14, 2), QPointF(14, 10));
        p.drawLine(QPointF(11, 13), QPointF(14, 10));
    }
    else if (type == "symmetry")
    {
        // mirror planes: a mirror (dashed line) with a shape and its image either side of it
        p.setPen(QPen(c, 1.3, Qt::DashLine, Qt::RoundCap));
        p.drawLine(QPointF(8, 1.2), QPointF(8, 14.8));
        p.setPen(QPen(c, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(alpha(c, 110));
        QPolygonF left;
        left << QPointF(2.2, 4.0) << QPointF(6.4, 8.0) << QPointF(2.2, 12.0);
        p.drawPolygon(left);
        p.setBrush(Qt::NoBrush);
        QPolygonF right;
        right << QPointF(13.8, 4.0) << QPointF(9.6, 8.0) << QPointF(13.8, 12.0);
        p.drawPolygon(right);
    }
    else if (type == "engineering")
    {
        // what is worked out about a model: a truss, three bars and their joints
        p.setPen(QPen(c, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(alpha(c, 45));
        p.drawPolygon(QPolygonF({QPointF(2.0, 13.2), QPointF(8.0, 2.8), QPointF(14.0, 13.2)}));
        p.setBrush(Qt::NoBrush);
        p.drawLine(QPointF(5.0, 8.0), QPointF(11.0, 8.0));
        p.drawLine(QPointF(8.0, 8.0), QPointF(8.0, 13.2));
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        for (const QPointF& n : {QPointF(2.0, 13.2), QPointF(8.0, 2.8), QPointF(14.0, 13.2), QPointF(8.0, 8.0)}) p.drawEllipse(n, 1.5, 1.5);
    }
    else if (type == "operation")
    {
        // what is done to a model: a cog
        p.setPen(QPen(c, 2.3, Qt::SolidLine, Qt::RoundCap));
        for (int k = 0; k < 8; ++k)
        {
            const double a = kPi / 4 * k;
            p.drawLine(QPointF(8 + 5.3 * std::cos(a), 8 + 5.3 * std::sin(a)), QPointF(8 + 7.0 * std::cos(a), 8 + 7.0 * std::sin(a)));
        }
        p.setPen(QPen(c, 1.9));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPointF(8, 8), 3.9, 3.9);
    }
    else if (type == "shapes")
    {
        // a shape made with parameters, more than a primitive: a plate with a round hole through it
        QPainterPath plate;
        plate.addRoundedRect(QRectF(2.4, 2.4, 11.2, 11.2), 2.6, 2.6);
        QPainterPath hole;
        hole.addEllipse(QPointF(8, 8), 3.1, 3.1);
        p.setPen(QPen(c, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(alpha(c, 110));
        p.drawPath(plate.subtracted(hole));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPointF(8, 8), 3.1, 3.1);
    }
    else if (type == "block")
    {
        // a block of your own: a brick with a function's f on it
        p.setPen(QPen(c, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(alpha(c, 70));
        p.drawRoundedRect(QRectF(2.4, 3.4, 11.2, 9.2), 1.8, 1.8);
        QPainterPath f;
        f.moveTo(9.6, 5.4);
        f.quadTo(7.6, 4.9, 7.6, 7.0);
        f.lineTo(7.6, 11.0);
        f.moveTo(6.1, 8.2);
        f.lineTo(9.4, 8.2);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(c.lighter(150), 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(f);
    }
    else
    {
        // a solid: a cube, lit from above
        const QPointF top(8, 1.8), ur(13.4, 4.9), lr(13.4, 11.1), bottom(8, 14.2), ll(2.6, 11.1), ul(2.6, 4.9), mid(8, 8);
        p.setPen(Qt::NoPen);
        p.setBrush(c.lighter(135));
        p.drawPolygon(QPolygonF({top, ur, mid, ul}));
        p.setBrush(c);
        p.drawPolygon(QPolygonF({ul, mid, bottom, ll}));
        p.setBrush(c.darker(135));
        p.drawPolygon(QPolygonF({ur, lr, bottom, mid}));
    }
}

// The mark of a custom block: a small badge at the lower right with a function's f
void drawBlockMark(QPainter& p)
{
    p.setPen(QPen(QColor(0x16, 0x4c, 0x5e), 1.6));
    p.setBrush(QColor(0xee, 0xe8, 0xd5));
    p.drawEllipse(QPointF(12.2, 12.2), 3.4, 3.4);
    QPainterPath f;
    f.moveTo(13.4, 10.0);
    f.quadTo(12.1, 9.6, 12.1, 11.0);
    f.lineTo(12.1, 14.4);
    f.moveTo(11.0, 11.9);
    f.lineTo(13.3, 11.9);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(0x16, 0x4c, 0x5e), 1.05, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPath(f);
}

}   // anonymous namespace

QColor color(const QString& type, const QString& domain)
{
    if (!domain.isEmpty() && (type == "simulation" || type == "conditions"))
    {
        auto d = domains().constFind(domain);
        if (d != domains().constEnd()) return type == "simulation" ? d->result : d->conditions;
    }
    auto it = kinds().constFind(type);
    return it == kinds().constEnd() ? kinds()["solid"].color : it->color;
}

QString label(const QString& type)
{
    auto it = kinds().constFind(type);
    return it == kinds().constEnd() ? type : T(it->label);
}

QIcon icon(const QString& type, bool block, const QString& domain)
{
    // (only a simulation and what an analysis is given have a picture of their domain)
    const QString d = (type == "simulation" || type == "conditions") ? domain : QString();
    const QString key = type + (block ? "+block" : "") + (d.isEmpty() ? QString() : "@" + d);
    auto it = cache().constFind(key);
    if (it != cache().constEnd()) return *it;
    const QColor c = color(type, d);
    QIcon made = draw([&](QPainter& p) {
        drawKind(p, type, c, d);
        if (block) drawBlockMark(p);
    });
    cache().insert(key, made);
    return made;
}

void setDomains(const QJsonObject& given)
{
    bool changed = false;
    for (auto it = given.constBegin(); it != given.constEnd(); ++it)
    {
        const QJsonObject o = it.value().toObject();
        Domain& d = domains()[it.key()];
        const QColor r(o["color"].toString()), c(o["conditions"].toString());
        if (r.isValid() && r != d.result) { d.result = r; changed = true; }
        if (c.isValid() && c != d.conditions) { d.conditions = c; changed = true; }
    }
    if (changed) cache().clear();
}

void setKinds(const QJsonObject& given)
{
    bool changed = false;
    for (auto it = given.constBegin(); it != given.constEnd(); ++it)
    {
        const QJsonObject o = it.value().toObject();
        Kind& k = kinds()[it.key()];
        const QColor c(o["color"].toString());
        if (c.isValid() && c != k.color) { k.color = c; changed = true; }
        if (o.contains("label")) k.label = o["label"].toString();
    }
    if (changed) cache().clear();
}

}   // namespace TypeIcons
}   // namespace FielDes
