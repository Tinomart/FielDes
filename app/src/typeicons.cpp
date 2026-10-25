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

#include <QFont>
#include <QHash>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPolygonF>
#include <QRadialGradient>
#include <QTransform>

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
        {"python",     {"Python",       QColor("#6c71c4")}},
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
    else if (type == "python")
    {
        // plain code: a tile with the letters of the language
        p.setPen(QPen(c, 1.5));
        p.setBrush(alpha(c, 70));
        p.drawRoundedRect(QRectF(1.8, 2.8, 12.4, 10.4), 2.4, 2.4);
        QFont f = p.font();
        f.setPixelSize(8);
        f.setBold(true);
        p.setFont(f);
        p.setPen(c.lighter(150));
        p.drawText(QRectF(1.8, 2.8, 12.4, 10.4), Qt::AlignCenter, QStringLiteral("py"));
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

// ---- The pictures of the primitives: what each one is, drawn in the colour of its kind (16 x 16, like the kinds' pictures) ------------------------------------------

QPen strokeOf(const QColor& c, double w)
{
    return QPen(c, w, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
}

// An arrow from a to b: the shaft and a filled head
void arrowOf(QPainter& p, const QPointF& a, const QPointF& b, const QColor& c, double w = 1.4, double head = 2.6)
{
    const double dx = b.x() - a.x(), dy = b.y() - a.y(), len = std::hypot(dx, dy);
    if (len < 1e-6) return;
    const double ux = dx / len, uy = dy / len;
    p.setPen(strokeOf(c, w));
    p.drawLine(a, QPointF(b.x() - ux * head * 0.5, b.y() - uy * head * 0.5));
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawPolygon(QPolygonF({b, QPointF(b.x() - ux * head + uy * head * 0.55, b.y() - uy * head - ux * head * 0.55),
                             QPointF(b.x() - ux * head - uy * head * 0.55, b.y() - uy * head + ux * head * 0.55)}));
}

// A sine wave from x0 to x1 around the line y0
QPainterPath sineOf(double x0, double x1, double y0, double amp, double periods, double phase = 0.0)
{
    QPainterPath path;
    const int n = 28;
    for (int i = 0; i <= n; ++i)
    {
        const double t = double(i) / n;
        const QPointF q(x0 + (x1 - x0) * t, y0 - amp * std::sin(phase + 2 * kPi * periods * t));
        if (i == 0) path.moveTo(q);
        else path.lineTo(q);
    }
    return path;
}

// A regular polygon of n corners, the first at `startDeg` (90 = straight up)
QPolygonF regularOf(const QPointF& centre, double r, int n, double startDeg)
{
    QPolygonF poly;
    for (int k = 0; k < n; ++k)
    {
        const double a = (startDeg + 360.0 * k / n) * kPi / 180.0;
        poly << QPointF(centre.x() + r * std::cos(a), centre.y() - r * std::sin(a));
    }
    return poly;
}

// A function's f, as on a custom block: `origin` is its upper left, `size` its height
void drawF(QPainter& p, const QColor& col, const QPointF& origin, double size, double w)
{
    const double s = size / 6.0;
    QPainterPath f;
    f.moveTo(origin.x() + 3.6 * s, origin.y() + 0.4 * s);
    f.quadTo(origin.x() + 1.6 * s, origin.y() - 0.1 * s, origin.x() + 1.6 * s, origin.y() + 1.9 * s);
    f.lineTo(origin.x() + 1.6 * s, origin.y() + 6.0 * s);
    f.moveTo(origin.x() + 0.2 * s, origin.y() + 3.2 * s);
    f.lineTo(origin.x() + 3.4 * s, origin.y() + 3.2 * s);
    p.setBrush(Qt::NoBrush);
    p.setPen(strokeOf(col, w));
    p.drawPath(f);
}

// The corners of a cube seen at an angle: the front face (0 upper left, 1 upper right, 2 lower right, 3 lower left), then the back face the same way (4..7)
const QPointF* cubeCorners()
{
    static const QPointF v[8] = {QPointF(2.6, 6.0), QPointF(9.8, 6.0), QPointF(9.8, 13.2), QPointF(2.6, 13.2),
                                 QPointF(6.2, 2.4), QPointF(13.4, 2.4), QPointF(13.4, 9.6), QPointF(6.2, 9.6)};
    return v;
}

// The edges of that cube: all of them, or only the four that stand upright
void cubeEdges(QPainter& p, const QColor& col, double w, bool uprightOnly = false)
{
    const QPointF* v = cubeCorners();
    static const int all[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    static const int upright[4][2] = {{0, 3}, {1, 2}, {4, 7}, {5, 6}};
    p.setPen(strokeOf(col, w));
    if (uprightOnly)
        for (const auto& e : upright) p.drawLine(v[e[0]], v[e[1]]);
    else
        for (const auto& e : all) p.drawLine(v[e[0]], v[e[1]]);
}

void nodeAt(QPainter& p, const QColor& col, const QPointF& at, double r)
{
    p.setPen(Qt::NoPen);
    p.setBrush(col);
    p.drawEllipse(at, r, r);
}

// A thermometer, its column filled (the picture of a thermal result), with its middle at x
void thermometerAt(QPainter& p, const QColor& c, double x)
{
    p.setPen(QPen(c, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(x - 1.4, 1.8, 2.8, 8.8), 1.4, 1.4);
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawEllipse(QPointF(x, 11.7), 2.7, 2.7);
    p.drawRoundedRect(QRectF(x - 0.75, 5.6, 1.5, 6.0), 0.7, 0.7);
}

// A drop, pointed at the top
QPainterPath dropPath()
{
    QPainterPath d;
    d.moveTo(8, 1.6);
    d.cubicTo(11.6, 6.0, 12.9, 8.0, 12.9, 9.8);
    d.cubicTo(12.9, 12.6, 10.7, 14.5, 8, 14.5);
    d.cubicTo(5.3, 14.5, 3.1, 12.6, 3.1, 9.8);
    d.cubicTo(3.1, 8.0, 4.4, 6.0, 8, 1.6);
    return d;
}

// The shapes: 3D, 2D, 1D, surfaces and fields.  False: not one of them
bool drawShape(QPainter& p, const QString& name, const QColor& c)
{
    const QColor lit = c.lighter(140), shade = c.darker(140);
    // ---- 3D
    if (name == "sphere")
    {
        QRadialGradient g(QPointF(6.0, 5.6), 8.6);
        g.setColorAt(0.0, c.lighter(170));
        g.setColorAt(0.5, c);
        g.setColorAt(1.0, c.darker(160));
        p.setPen(Qt::NoPen);
        p.setBrush(g);
        p.drawEllipse(QPointF(8, 8), 6.4, 6.4);
        return true;
    }
    if (name == "rounded_box")
    {
        // a cube with its edges and corners rounded: each face is drawn with a round pen of its own colour
        const QPointF top(8, 3.0), ur(12.4, 5.5), lr(12.4, 10.5), bottom(8, 13.0), ll(3.6, 10.5), ul(3.6, 5.5), mid(8, 8);
        const QPolygonF faces[3] = {QPolygonF({top, ur, mid, ul}), QPolygonF({ul, mid, bottom, ll}), QPolygonF({ur, lr, bottom, mid})};
        const QColor colors[3] = {lit, c, shade};
        for (int k = 0; k < 3; ++k)
        {
            p.setPen(strokeOf(colors[k], 1.9));
            p.setBrush(colors[k]);
            p.drawPolygon(faces[k]);
        }
        return true;
    }
    if (name == "cylinder")
    {
        QPainterPath body;
        body.addRect(QRectF(3.2, 4.4, 9.6, 7.4));
        QPainterPath bottom;
        bottom.addEllipse(QPointF(8, 11.8), 4.8, 2.0);
        QLinearGradient g(3.2, 0, 12.8, 0);
        g.setColorAt(0.0, c.lighter(125));
        g.setColorAt(0.55, c);
        g.setColorAt(1.0, c.darker(150));
        p.setPen(Qt::NoPen);
        p.setBrush(g);
        p.drawPath(body.united(bottom));
        p.setBrush(c.lighter(150));
        p.drawEllipse(QPointF(8, 4.4), 4.8, 2.0);
        return true;
    }
    if (name == "cone")
    {
        QPainterPath body;
        body.addPolygon(QPolygonF({QPointF(8, 1.6), QPointF(2.8, 11.6), QPointF(13.2, 11.6)}));
        body.closeSubpath();
        QPainterPath base;
        base.addEllipse(QPointF(8, 11.6), 5.2, 2.3);
        QLinearGradient g(2.8, 0, 13.2, 0);
        g.setColorAt(0.0, c.lighter(125));
        g.setColorAt(0.55, c);
        g.setColorAt(1.0, c.darker(150));
        p.setPen(Qt::NoPen);
        p.setBrush(g);
        p.drawPath(body.united(base));
        return true;
    }
    if (name == "torus")
    {
        QPainterPath ring;
        ring.addEllipse(QPointF(8, 8), 6.6, 4.9);
        QPainterPath hole;
        hole.addEllipse(QPointF(8, 8), 2.7, 1.4);
        QLinearGradient g(0, 3.1, 0, 12.9);
        g.setColorAt(0.0, c.lighter(150));
        g.setColorAt(0.5, c);
        g.setColorAt(1.0, c.darker(150));
        p.setPen(Qt::NoPen);
        p.setBrush(g);
        p.drawPath(ring.subtracted(hole));
        return true;
    }
    if (name == "pyramid")
    {
        const QPointF apex(8, 1.6), fl(2.6, 10.2), fr(8.8, 13.6), br(13.6, 9.4);
        p.setPen(Qt::NoPen);
        p.setBrush(lit);
        p.drawPolygon(QPolygonF({apex, fl, fr}));
        p.setBrush(shade);
        p.drawPolygon(QPolygonF({apex, fr, br}));
        return true;
    }
    if (name == "prism")
    {
        // a triangular prism lying on its side: its end, and the sloping face it is seen along
        const QPointF a(2.4, 12.6), b(7.4, 12.6), t(4.9, 6.8), e(5.8, -3.8);
        p.setPen(Qt::NoPen);
        p.setBrush(shade);
        p.drawPolygon(QPolygonF({b, t, t + e, b + e}));
        p.setBrush(lit);
        p.drawPolygon(QPolygonF({a, b, t}));
        return true;
    }
    if (name == "half_space")
    {
        // everything on one side of a plane: that side filled, fading out as it goes on forever, the plane a line
        QLinearGradient g(0, 8, 0, 15);
        g.setColorAt(0.0, alpha(c, 190));
        g.setColorAt(1.0, alpha(c, 25));
        p.setPen(Qt::NoPen);
        p.setBrush(g);
        p.drawRect(QRectF(2.2, 8.0, 11.6, 7.0));
        p.setPen(QPen(alpha(c, 150), 1.2, Qt::DashLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        p.drawLine(QPointF(2.2, 2.6), QPointF(13.8, 2.6));
        p.drawLine(QPointF(2.2, 2.6), QPointF(2.2, 8.0));
        p.drawLine(QPointF(13.8, 2.6), QPointF(13.8, 8.0));
        p.setPen(strokeOf(c, 1.9));
        p.drawLine(QPointF(1.6, 8.0), QPointF(14.4, 8.0));
        return true;
    }
    if (name == "low_level_body")
    {
        // a body of your own logic: a cube, dim, with the function's f on it
        const QPointF top(8, 1.8), ur(13.4, 4.9), lr(13.4, 11.1), bottom(8, 14.2), ll(2.6, 11.1), ul(2.6, 4.9), mid(8, 8);
        p.setPen(Qt::NoPen);
        p.setBrush(alpha(lit, 120));
        p.drawPolygon(QPolygonF({top, ur, mid, ul}));
        p.setBrush(alpha(c, 120));
        p.drawPolygon(QPolygonF({ul, mid, bottom, ll}));
        p.setBrush(alpha(shade, 120));
        p.drawPolygon(QPolygonF({ur, lr, bottom, mid}));
        drawF(p, QColor(0xf4, 0xf6, 0xfa), QPointF(5.2, 3.6), 9.0, 1.8);
        return true;
    }
    // ---- 2D
    if (name == "circle" || name == "rectangle" || name == "rounded_rectangle" || name == "ring" || name == "polygon" || name == "triangle" || name == "profile")
    {
        p.setPen(strokeOf(c, 1.5));
        p.setBrush(alpha(c, 95));
        if (name == "circle") p.drawEllipse(QPointF(8, 8), 5.6, 5.6);
        else if (name == "rectangle") p.drawRect(QRectF(2.4, 4.2, 11.2, 7.6));
        else if (name == "rounded_rectangle") p.drawRoundedRect(QRectF(2.4, 4.2, 11.2, 7.6), 3.2, 3.2);
        else if (name == "ring")
        {
            QPainterPath outer;
            outer.addEllipse(QPointF(8, 8), 5.8, 5.8);
            QPainterPath inner;
            inner.addEllipse(QPointF(8, 8), 2.6, 2.6);
            p.drawPath(outer.subtracted(inner));
        }
        else if (name == "polygon") p.drawPolygon(regularOf(QPointF(8, 8), 6.0, 6, 0.0));
        else if (name == "triangle") p.drawPolygon(QPolygonF({QPointF(2.4, 12.6), QPointF(13.6, 12.6), QPointF(8, 3.0)}));
        else
        {
            // a shape made of points: its outline, and the points
            const QPointF pts[5] = {QPointF(2.8, 11.6), QPointF(4.6, 4.0), QPointF(10.0, 2.8), QPointF(13.2, 8.0), QPointF(11.0, 13.0)};
            p.drawPolygon(QPolygonF({pts[0], pts[1], pts[2], pts[3], pts[4]}));
            for (const QPointF& q : pts) nodeAt(p, c, q, 1.5);
        }
        return true;
    }
    // ---- 1D (a spline is the picture of its kind)
    if (name == "polyline")
    {
        const QPointF pts[4] = {QPointF(2.4, 11.6), QPointF(6.0, 4.4), QPointF(9.6, 10.8), QPointF(13.6, 4.2)};
        p.setPen(strokeOf(c, 1.6));
        p.setBrush(Qt::NoBrush);
        p.drawPolyline(QPolygonF({pts[0], pts[1], pts[2], pts[3]}));
        for (const QPointF& q : pts) nodeAt(p, c, q, 1.6);
        return true;
    }
    // ---- surfaces (a wave surface is the picture of its kind)
    if (name == "plane")
    {
        // a flat sheet, a grid across it
        p.setPen(strokeOf(c, 1.4));
        p.setBrush(alpha(c, 85));
        p.drawPolygon(QPolygonF({QPointF(1.5, 11.5), QPointF(5.2, 3.5), QPointF(14.5, 3.5), QPointF(10.8, 11.5)}));
        p.setPen(strokeOf(alpha(c, 200), 1.0));
        p.drawLine(QPointF(3.35, 7.5), QPointF(12.65, 7.5));
        p.drawLine(QPointF(9.85, 3.5), QPointF(6.15, 11.5));
        return true;
    }
    if (name == "sphere_surface")
    {
        // the skin of a ball: a globe of lines
        p.setPen(strokeOf(c, 1.5));
        p.setBrush(alpha(c, 45));
        p.drawEllipse(QPointF(8, 8), 6.4, 6.4);
        p.setBrush(Qt::NoBrush);
        p.setPen(strokeOf(alpha(c, 215), 1.1));
        p.drawEllipse(QPointF(8, 8), 6.4, 2.3);
        p.drawEllipse(QPointF(8, 8), 2.7, 6.4);
        return true;
    }
    if (name == "cylinder_surface")
    {
        // the skin of a tube: open, with its rims and its sides
        p.setPen(strokeOf(c, 1.5));
        p.setBrush(alpha(c, 50));
        QPainterPath body;
        body.moveTo(3.2, 4.4);
        body.lineTo(3.2, 11.6);
        body.arcTo(QRectF(3.2, 9.6, 9.6, 4.0), 180, 180);
        body.lineTo(12.8, 4.4);
        body.closeSubpath();
        p.drawPath(body);
        p.setBrush(alpha(c, 100));
        p.drawEllipse(QPointF(8, 4.4), 4.8, 2.0);
        return true;
    }
    // ---- fields
    if (name == "distance_to_point")
    {
        // the distance from a point: rings around it
        p.setBrush(Qt::NoBrush);
        p.setPen(strokeOf(alpha(c, 130), 1.2));
        p.drawEllipse(QPointF(8, 8), 6.3, 6.3);
        p.setPen(strokeOf(alpha(c, 200), 1.3));
        p.drawEllipse(QPointF(8, 8), 3.8, 3.8);
        nodeAt(p, c, QPointF(8, 8), 1.8);
        return true;
    }
    if (name == "radial_field")
    {
        // the distance from an axis: shells around a line
        p.setBrush(Qt::NoBrush);
        p.setPen(strokeOf(alpha(c, 130), 1.2));
        p.drawEllipse(QPointF(8, 8), 6.4, 2.6);
        p.setPen(strokeOf(alpha(c, 200), 1.3));
        p.drawEllipse(QPointF(8, 8), 3.9, 1.6);
        p.setPen(strokeOf(c, 1.4));
        p.drawLine(QPointF(8, 1.4), QPointF(8, 14.6));
        return true;
    }
    if (name == "z_field")
    {
        // the height: layers that grow with it, an arrow up beside them
        p.setPen(Qt::NoPen);
        for (int k = 0; k < 5; ++k)
        {
            p.setBrush(alpha(c, 255 - k * 48));
            p.drawRoundedRect(QRectF(2.2, 2.6 + k * 2.5, 9.4, 1.8), 0.7, 0.7);
        }
        arrowOf(p, QPointF(13.8, 14.0), QPointF(13.8, 2.4), c, 1.4, 3.0);
        return true;
    }
    if (name == "noise_field")
    {
        // a value that changes without order: a grid of cells of every strength
        static const int levels[16] = {230, 90, 160, 40, 110, 250, 70, 190, 60, 200, 120, 30, 170, 80, 240, 130};
        p.setPen(Qt::NoPen);
        for (int j = 0; j < 4; ++j)
            for (int i = 0; i < 4; ++i)
            {
                p.setBrush(alpha(c, levels[j * 4 + i]));
                p.drawRoundedRect(QRectF(2.0 + i * 3.1, 2.0 + j * 3.1, 2.7, 2.7), 0.6, 0.6);
            }
        return true;
    }
    if (name == "wave")
    {
        // a value that goes up and down across space: waves
        p.setBrush(Qt::NoBrush);
        const double ys[3] = {4.4, 8.0, 11.6};
        for (int k = 0; k < 3; ++k)
        {
            p.setPen(strokeOf(alpha(c, k == 1 ? 255 : 150), 1.5));
            p.drawPath(sineOf(1.6, 14.4, ys[k], 1.7, 1.5, 0.0));
        }
        return true;
    }
    if (name == "low_level_field")
    {
        // a field of your own logic: the soft disc of a field with the function's f on it
        QRadialGradient g(QPointF(8, 8), 7);
        g.setColorAt(0.0, alpha(c, 200));
        g.setColorAt(0.55, alpha(c, 110));
        g.setColorAt(1.0, alpha(c, 25));
        p.setPen(Qt::NoPen);
        p.setBrush(g);
        p.drawEllipse(QPointF(8, 8), 7, 7);
        drawF(p, QColor(0xf4, 0xf6, 0xfa), QPointF(5.2, 3.6), 9.0, 1.8);
        return true;
    }
    return false;
}

// What an analysis is given: supports, loads, heat, flow, and the rules of a manufacturing check.  False: not one of them
bool drawCondition(QPainter& p, const QString& name, const QColor& c)
{
    if (name == "fixed")
    {
        // a support: a wall with its hatching, a block held by it
        p.setPen(strokeOf(c, 1.3));
        for (double y : {3.0, 6.0, 9.0, 12.0}) p.drawLine(QPointF(5.2, y), QPointF(2.6, y + 2.2));
        p.setPen(strokeOf(c, 2.2));
        p.drawLine(QPointF(5.2, 2.2), QPointF(5.2, 13.8));
        p.setPen(strokeOf(c, 1.4));
        p.setBrush(alpha(c, 110));
        p.drawRect(QRectF(5.2, 5.4, 8.0, 5.2));
        return true;
    }
    if (name == "force")
    {
        // a push: one arrow, from where it is applied
        nodeAt(p, c, QPointF(3.2, 3.2), 1.5);
        arrowOf(p, QPointF(3.2, 3.2), QPointF(12.8, 12.8), c, 1.9, 4.2);
        return true;
    }
    if (name == "gravity")
    {
        // a weight falling: a ball above an arrow down
        nodeAt(p, c, QPointF(8, 4.2), 2.7);
        arrowOf(p, QPointF(8, 8.6), QPointF(8, 14.4), c, 1.7, 3.6);
        return true;
    }
    if (name == "thermal_expansion")
    {
        // a part that grows: a square with arrows out of its corners
        p.setPen(strokeOf(c, 1.4));
        p.setBrush(alpha(c, 150));
        p.drawRect(QRectF(5.4, 5.4, 5.2, 5.2));
        const double d = 2.8;
        for (int sx : {-1, 1})
            for (int sy : {-1, 1})
                arrowOf(p, QPointF(8 + sx * 3.4, 8 + sy * 3.4), QPointF(8 + sx * (3.4 + d), 8 + sy * (3.4 + d)), c, 1.3, 2.4);
        return true;
    }
    if (name == "fixed_temperature")
    {
        // a temperature that is held: a thermometer and an equals sign
        thermometerAt(p, c, 5.6);
        p.setPen(strokeOf(c, 1.7));
        p.drawLine(QPointF(10.6, 5.6), QPointF(14.2, 5.6));
        p.drawLine(QPointF(10.6, 9.0), QPointF(14.2, 9.0));
        return true;
    }
    if (name == "heat_input")
    {
        // heat into a surface: wavy arrows down onto a wall
        for (int k = 0; k < 3; ++k)
        {
            const double x = 4.2 + k * 3.8;
            QPainterPath wave;
            wave.moveTo(x, 1.8);
            wave.cubicTo(x - 2.0, 3.6, x + 2.0, 5.6, x, 8.0);
            p.setBrush(Qt::NoBrush);
            p.setPen(strokeOf(c, 1.4));
            p.drawPath(wave);
            p.setPen(Qt::NoPen);
            p.setBrush(c);
            p.drawPolygon(QPolygonF({QPointF(x - 1.8, 7.6), QPointF(x + 1.8, 7.6), QPointF(x, 10.6)}));
        }
        p.setPen(strokeOf(c, 2.4));
        p.drawLine(QPointF(2.4, 13.2), QPointF(13.6, 13.2));
        return true;
    }
    if (name == "heat_generation")
    {
        // heat made inside a body: a flame in a block
        p.setPen(strokeOf(c, 1.5));
        p.setBrush(alpha(c, 60));
        p.drawRoundedRect(QRectF(2.4, 3.4, 11.2, 10.2), 2.0, 2.0);
        QPainterPath flame;
        flame.moveTo(8, 5.6);
        flame.cubicTo(11.6, 8.2, 11.4, 12.2, 8, 12.2);
        flame.cubicTo(4.6, 12.2, 4.4, 8.2, 8, 5.6);
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawPath(flame);
        return true;
    }
    if (name == "convection")
    {
        // heat carried away by a fluid: a wall, and a fluid circulating next to it
        p.setPen(strokeOf(alpha(c, 230), 2.2));
        p.drawLine(QPointF(2.8, 2.2), QPointF(2.8, 13.8));
        const QPointF centre(9.4, 8.0);
        const double r = 3.9;
        auto at = [&](double deg) { const double a = deg * kPi / 180.0; return QPointF(centre.x() + r * std::cos(a), centre.y() - r * std::sin(a)); };
        p.setBrush(Qt::NoBrush);
        p.setPen(strokeOf(c, 1.6));
        p.drawArc(QRectF(centre.x() - r, centre.y() - r, 2 * r, 2 * r), 70 * 16, 250 * 16);
        arrowOf(p, at(320.0), at(346.0), c, 1.6, 2.8);
        return true;
    }
    if (name == "inlet" || name == "outlet")
    {
        // flow through a boundary: arrows that start at a solid line (the inlet), or that end at an open, dashed one (the outlet)
        const bool in = name == "inlet";
        const double bar = in ? 3.2 : 12.8;
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(alpha(c, 230), 2.0, in ? Qt::SolidLine : Qt::DashLine, Qt::RoundCap));
        p.drawLine(QPointF(bar, 2.2), QPointF(bar, 13.8));
        for (double y : {4.6, 8.0, 11.4})
        {
            if (in) arrowOf(p, QPointF(4.4, y), QPointF(13.0, y), c, 1.5, 2.8);
            else arrowOf(p, QPointF(2.6, y), QPointF(11.4, y), c, 1.5, 2.8);
        }
        return true;
    }
    if (name == "wall" || name == "slip")
    {
        // flow along a wall: with no slip it is slower the nearer the wall (arrows that get shorter), with slip it is the same all the way
        const bool noSlip = name == "wall";
        p.setPen(strokeOf(alpha(c, 230), 2.0));
        p.drawLine(QPointF(1.8, 12.2), QPointF(14.2, 12.2));
        if (noSlip)
        {
            p.setPen(strokeOf(alpha(c, 200), 1.2));
            for (double x : {3.4, 6.4, 9.4, 12.4}) p.drawLine(QPointF(x, 12.2), QPointF(x - 1.8, 14.6));
        }
        const double ys[3] = {9.2, 6.2, 3.2};
        const double ends[3] = {6.0, 9.6, 13.2};
        for (int k = 0; k < 3; ++k) arrowOf(p, QPointF(2.8, ys[k]), QPointF(noSlip ? ends[k] : 12.6, ys[k]), c, 1.4, 2.6);
        return true;
    }
    if (name == "fff_constraint")
    {
        // a printer's nozzle above the layers it lays
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawPolygon(QPolygonF({QPointF(5.8, 1.6), QPointF(10.2, 1.6), QPointF(9.0, 5.0), QPointF(7.0, 5.0)}));
        const double starts[3] = {3.0, 2.4, 3.4};
        const double widths[3] = {10.0, 11.2, 9.2};
        for (int i = 0; i < 3; ++i)
        {
            p.setBrush(alpha(c, i == 0 ? 230 : 170));
            p.drawRoundedRect(QRectF(starts[i], 6.2 + i * 2.7, widths[i], 2.0), 0.9, 0.9);
        }
        return true;
    }
    if (name == "cnc_constraint")
    {
        // a milling cutter cutting into a block
        p.setPen(Qt::NoPen);
        p.setBrush(alpha(c, 140));
        p.drawRect(QRectF(6.6, 1.4, 2.8, 4.0));
        p.setBrush(c);
        p.drawRoundedRect(QRectF(5.8, 5.2, 4.4, 5.6), 0.8, 0.8);
        p.setPen(strokeOf(QColor(0x14, 0x18, 0x1f), 0.9));
        p.drawLine(QPointF(6.2, 9.6), QPointF(9.8, 6.4));
        p.drawLine(QPointF(6.2, 7.6), QPointF(9.8, 5.6));
        p.setPen(strokeOf(alpha(c, 220), 2.2));
        p.drawLine(QPointF(1.8, 13.2), QPointF(14.2, 13.2));
        return true;
    }
    if (name == "molding_constraint")
    {
        // the two halves of a mould, the cavity between them
        QPainterPath mould;
        mould.addRect(QRectF(1.8, 3.2, 6.0, 9.6));
        mould.addRect(QRectF(8.2, 3.2, 6.0, 9.6));
        QPainterPath cavity;
        cavity.addEllipse(QPointF(8, 8), 2.8, 2.8);
        p.setPen(strokeOf(c, 1.4));
        p.setBrush(alpha(c, 110));
        p.drawPath(mould.subtracted(cavity));
        return true;
    }
    return false;
}

// What a part is made of: a metal as an ingot with its element, a plastic as the thing it is known by, and the fluids.  False: not one of them
bool drawMaterial(QPainter& p, const QString& name, const QColor& c)
{
    static const QHash<QString, QString> metals = {{"steel", "Fe"}, {"stainless_steel", "Cr"}, {"aluminium", "Al"}, {"titanium", "Ti"}};
    auto metal = metals.constFind(name);
    if (metal != metals.constEnd())
    {
        // a bar of the metal, its element on it
        p.setPen(strokeOf(c, 1.2));
        p.setBrush(alpha(c.lighter(140), 220));
        p.drawPolygon(QPolygonF({QPointF(4.0, 6.0), QPointF(5.2, 3.2), QPointF(10.8, 3.2), QPointF(12.0, 6.0)}));
        p.setBrush(alpha(c, 200));
        p.drawPolygon(QPolygonF({QPointF(2.0, 13.2), QPointF(14.0, 13.2), QPointF(12.0, 6.0), QPointF(4.0, 6.0)}));
        QFont f = p.font();
        f.setPixelSize(6);
        f.setBold(true);
        p.setFont(f);
        p.setPen(c.darker(320));
        p.drawText(QRectF(3.0, 6.2, 10.0, 6.8), Qt::AlignCenter, metal.value());
        return true;
    }
    if (name == "pla")
    {
        // made of plants: a leaf
        QPainterPath leaf;
        leaf.moveTo(2.8, 13.2);
        leaf.cubicTo(2.2, 6.0, 7.0, 2.4, 13.6, 2.4);
        leaf.cubicTo(13.8, 9.6, 9.6, 13.8, 2.8, 13.2);
        p.setPen(strokeOf(c, 1.4));
        p.setBrush(alpha(c, 130));
        p.drawPath(leaf);
        p.drawLine(QPointF(2.8, 13.2), QPointF(10.4, 5.6));
        return true;
    }
    if (name == "petg")
    {
        // the plastic of bottles: a bottle
        p.setPen(strokeOf(c, 1.4));
        p.setBrush(alpha(c, 110));
        p.drawRoundedRect(QRectF(4.4, 5.4, 7.2, 8.8), 2.2, 2.2);
        p.drawRect(QRectF(6.8, 3.0, 2.4, 2.4));
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawRoundedRect(QRectF(6.2, 1.4, 3.6, 1.8), 0.6, 0.6);
        p.setPen(strokeOf(alpha(c, 220), 1.1));
        p.drawLine(QPointF(5.2, 9.0), QPointF(10.8, 9.0));
        return true;
    }
    if (name == "abs_plastic")
    {
        // the plastic of building bricks: a brick
        p.setPen(strokeOf(c, 1.3));
        p.setBrush(alpha(c, 150));
        p.drawRoundedRect(QRectF(2.2, 6.4, 11.6, 7.4), 1.0, 1.0);
        p.drawRoundedRect(QRectF(3.8, 3.8, 3.2, 2.6), 0.6, 0.6);
        p.drawRoundedRect(QRectF(9.0, 3.8, 3.2, 2.6), 0.6, 0.6);
        return true;
    }
    if (name == "nylon")
    {
        // a fibre: two strands twisted round each other
        p.setBrush(Qt::NoBrush);
        QPainterPath a, b;
        for (int i = 0; i <= 28; ++i)
        {
            const double t = double(i) / 28, y = 1.6 + 12.8 * t, dx = 2.6 * std::sin(2 * kPi * 1.5 * t);
            if (i == 0) { a.moveTo(8 + dx, y); b.moveTo(8 - dx, y); }
            else { a.lineTo(8 + dx, y); b.lineTo(8 - dx, y); }
        }
        p.setPen(strokeOf(alpha(c, 200), 2.2));
        p.drawPath(b);
        p.setPen(strokeOf(c, 2.2));
        p.drawPath(a);
        return true;
    }
    if (name == "water")
    {
        QLinearGradient g(0, 1.6, 0, 14.5);
        g.setColorAt(0.0, alpha(c.lighter(140), 230));
        g.setColorAt(1.0, alpha(c, 230));
        p.setPen(strokeOf(c, 1.3));
        p.setBrush(g);
        p.drawPath(dropPath());
        p.setBrush(Qt::NoBrush);
        p.setPen(strokeOf(c.lighter(185), 1.2));
        p.drawArc(QRectF(4.6, 8.0, 4.4, 4.4), 200 * 16, 90 * 16);
        return true;
    }
    if (name == "oil")
    {
        // thick and dark: a drop with bands in it
        p.setPen(strokeOf(c, 1.3));
        p.setBrush(c.darker(210));
        const QPainterPath drop = dropPath();
        p.drawPath(drop);
        p.save();
        p.setClipPath(drop);
        p.setPen(Qt::NoPen);
        p.setBrush(alpha(c, 170));
        p.drawRect(QRectF(0, 8.2, 16, 1.5));
        p.drawRect(QRectF(0, 11.0, 16, 1.5));
        p.restore();
        return true;
    }
    if (name == "glycerol")
    {
        // a liquid of the laboratory: a flask with the liquid in it
        QPainterPath flask;
        flask.addPolygon(QPolygonF({QPointF(6.4, 1.8), QPointF(9.6, 1.8), QPointF(9.6, 6.2), QPointF(13.8, 13.6), QPointF(2.2, 13.6), QPointF(6.4, 6.2)}));
        flask.closeSubpath();
        p.save();
        p.setClipPath(flask);
        p.setPen(Qt::NoPen);
        p.setBrush(alpha(c, 170));
        p.drawRect(QRectF(0, 9.4, 16, 6));
        p.restore();
        p.setPen(strokeOf(c, 1.4));
        p.setBrush(Qt::NoBrush);
        p.drawPath(flask);
        return true;
    }
    if (name == "air")
    {
        // moving air: curling lines
        p.setBrush(Qt::NoBrush);
        p.setPen(strokeOf(c, 1.6));
        QPainterPath a, b, d;
        a.moveTo(1.8, 5.2);
        a.lineTo(10.2, 5.2);
        a.cubicTo(13.8, 5.2, 13.8, 1.6, 10.8, 2.2);
        b.moveTo(1.8, 8.6);
        b.lineTo(13.4, 8.6);
        d.moveTo(1.8, 12.0);
        d.lineTo(8.6, 12.0);
        d.cubicTo(11.8, 12.0, 11.8, 15.2, 9.2, 14.6);
        p.drawPath(a);
        p.drawPath(b);
        p.drawPath(d);
        return true;
    }
    return false;
}

// The cells of a lattice, by kind (the name without `cell_`): struts as the cube they repeat in, a TPMS as a section of its sheet, a planar pattern as the pattern.  False: not one of them
bool drawCell(QPainter& p, const QString& kind, const QColor& c)
{
    const QPointF* v = cubeCorners();
    p.setBrush(Qt::NoBrush);
    if (kind == "cubic" || kind == "bcc" || kind == "bccz" || kind == "fcc" || kind == "fccz" || kind == "octet" || kind == "diamond_struts" || kind == "cuboctahedron")
    {
        // a cube of struts: the edges, dim, and what each cell adds to them
        const QPointF centre(8.0, 7.8);
        cubeEdges(p, alpha(c, kind == "cubic" ? 255 : 105), 1.3);
        if (kind == "bcc" || kind == "bccz")
        {
            p.setPen(strokeOf(c, 1.3));
            for (int i = 0; i < 8; ++i) p.drawLine(v[i], centre);
            if (kind == "bccz") cubeEdges(p, c, 1.7, true);
            for (int i = 0; i < 8; ++i) nodeAt(p, c, v[i], 1.0);
            nodeAt(p, c, centre, 1.7);
        }
        else if (kind == "fcc" || kind == "fccz" || kind == "octet")
        {
            // the diagonals of the faces: front, right, top (and, for the octet, the cube's own diagonals as well)
            p.setPen(strokeOf(c, 1.2));
            p.drawLine(v[0], v[2]); p.drawLine(v[1], v[3]);
            p.drawLine(v[1], v[6]); p.drawLine(v[5], v[2]);
            p.drawLine(v[0], v[5]); p.drawLine(v[1], v[4]);
            if (kind == "fccz") cubeEdges(p, c, 1.7, true);
            if (kind == "octet")
            {
                p.setPen(strokeOf(alpha(c, 170), 1.0));
                p.drawLine(v[4], v[7]); p.drawLine(v[7], v[6]);
                p.drawLine(v[0], v[7]); p.drawLine(v[3], v[6]);
            }
            for (const QPointF& f : {QPointF(6.2, 9.6), QPointF(11.6, 7.8), QPointF(8.0, 4.2)}) nodeAt(p, c, f, 1.4);
            for (int i : {0, 1, 2, 3, 5}) nodeAt(p, c, v[i], 1.0);
        }
        else if (kind == "diamond_struts")
        {
            // the bonds of a diamond: a node with four struts to the corners of a tetrahedron
            cubeEdges(p, alpha(c, 70), 1.0);
            p.setPen(strokeOf(c, 1.5));
            const QPointF corners[4] = {v[0], v[2], v[5], v[7]};
            for (const QPointF& q : corners) p.drawLine(centre, q);
            for (const QPointF& q : corners) nodeAt(p, c, q, 1.3);
            nodeAt(p, c, centre, 1.8);
        }
        else if (kind == "cuboctahedron")
        {
            // the corners of the cuboctahedron are the middles of the cube's edges: each face a diamond of them
            const QPointF front[4] = {QPointF(6.2, 6.0), QPointF(9.8, 9.6), QPointF(6.2, 13.2), QPointF(2.6, 9.6)};
            const QPointF right[4] = {QPointF(11.6, 4.2), QPointF(13.4, 6.0), QPointF(11.6, 11.4), QPointF(9.8, 9.6)};
            const QPointF top[4] = {QPointF(6.2, 6.0), QPointF(11.6, 4.2), QPointF(9.8, 2.4), QPointF(4.4, 4.2)};
            p.setPen(strokeOf(c, 1.4));
            p.setBrush(alpha(c, 55));
            p.drawPolygon(QPolygonF({front[0], front[1], front[2], front[3]}));
            p.drawPolygon(QPolygonF({right[0], right[1], right[2], right[3]}));
            p.drawPolygon(QPolygonF({top[0], top[1], top[2], top[3]}));
            for (const QPointF& q : {front[0], front[1], front[2], front[3], right[0], right[1], right[2], top[2], top[3]}) nodeAt(p, c, q, 1.1);
        }
        else
            for (int i = 0; i < 8; ++i) nodeAt(p, c, v[i], 1.5);
        return true;
    }
    if (kind == "octahedron")
    {
        // eight triangles round a middle
        const QPointF top(8, 1.8), bottom(8, 14.2), left(2.2, 8), right(13.8, 8), front(8, 10.8), back(8, 5.2);
        p.setPen(QPen(alpha(c, 110), 1.1, Qt::DashLine, Qt::RoundCap));
        p.drawLine(left, back); p.drawLine(back, right); p.drawLine(top, back); p.drawLine(bottom, back);
        p.setPen(strokeOf(c, 1.5));
        p.setBrush(alpha(c, 55));
        p.drawPolygon(QPolygonF({top, left, front}));
        p.drawPolygon(QPolygonF({top, front, right}));
        p.setBrush(Qt::NoBrush);
        p.drawLine(bottom, left); p.drawLine(bottom, front); p.drawLine(bottom, right);
        p.drawLine(left, front); p.drawLine(front, right);
        for (const QPointF& q : {top, bottom, left, right, front}) nodeAt(p, c, q, 1.3);
        return true;
    }
    if (kind == "kelvin")
    {
        // a truncated octahedron: hexagons and squares -- a hexagon with a square in it
        p.setPen(strokeOf(c, 1.5));
        p.setBrush(alpha(c, 55));
        p.drawPolygon(regularOf(QPointF(8, 8), 6.4, 6, 90.0));
        p.setBrush(Qt::NoBrush);
        p.setPen(strokeOf(alpha(c, 215), 1.2));
        p.drawRect(QRectF(5.2, 5.2, 5.6, 5.6));
        return true;
    }
    if (kind == "cross")
    {
        // three struts through one node, one along each axis
        p.setPen(strokeOf(c, 1.5));
        p.drawLine(QPointF(2.0, 8.0), QPointF(14.0, 8.0));
        p.drawLine(QPointF(8.0, 1.8), QPointF(8.0, 14.2));
        p.drawLine(QPointF(3.4, 12.6), QPointF(12.6, 3.4));
        for (const QPointF& q : {QPointF(2.0, 8.0), QPointF(14.0, 8.0), QPointF(8.0, 1.8), QPointF(8.0, 14.2), QPointF(3.4, 12.6), QPointF(12.6, 3.4)}) nodeAt(p, c, q, 1.2);
        nodeAt(p, c, QPointF(8, 8), 1.8);
        return true;
    }
    if (kind == "tesseract")
    {
        // a cube within a cube: the picture of four dimensions
        const QRectF outer(2.4, 2.4, 11.2, 11.2), inner(5.2, 5.2, 5.6, 5.6);
        p.setPen(strokeOf(c, 1.4));
        p.drawRect(outer);
        p.setPen(strokeOf(alpha(c, 220), 1.2));
        p.drawRect(inner);
        p.drawLine(outer.topLeft(), inner.topLeft()); p.drawLine(outer.topRight(), inner.topRight());
        p.drawLine(outer.bottomLeft(), inner.bottomLeft()); p.drawLine(outer.bottomRight(), inner.bottomRight());
        for (const QPointF& q : {outer.topLeft(), outer.topRight(), outer.bottomLeft(), outer.bottomRight(), inner.topLeft(), inner.topRight(), inner.bottomLeft(), inner.bottomRight()})
            nodeAt(p, c, q, 1.1);
        return true;
    }
    // ---- a TPMS: a section through the sheet
    if (kind == "gyroid")
    {
        // the two strands that wind round each other
        p.setPen(strokeOf(c, 1.8));
        p.drawPath(sineOf(1.8, 14.2, 8.0, 3.6, 1.0, 0.0));
        p.setPen(strokeOf(alpha(c, 190), 1.8));
        p.drawPath(sineOf(1.8, 14.2, 8.0, 3.6, 1.0, kPi));
        return true;
    }
    if (kind == "schwarz_p" || kind == "split_p")
    {
        // the P surface: a rounded square round a round hole
        QPainterPath outer;
        outer.addRoundedRect(QRectF(2.2, 2.2, 11.6, 11.6), 3.6, 3.6);
        QPainterPath inner;
        inner.addEllipse(QPointF(8, 8), 3.0, 3.0);
        p.setPen(strokeOf(c, 1.5));
        p.setBrush(alpha(c, 95));
        p.drawPath(outer.subtracted(inner));
        if (kind == "split_p")
        {
            // split: cut in two along a diagonal
            p.setBrush(Qt::NoBrush);
            p.setCompositionMode(QPainter::CompositionMode_Clear);
            p.setPen(QPen(Qt::transparent, 1.8, Qt::SolidLine, Qt::FlatCap));
            p.drawLine(QPointF(1.6, 14.4), QPointF(14.4, 1.6));
            p.setCompositionMode(QPainter::CompositionMode_SourceOver);
        }
        return true;
    }
    if (kind == "diamond")
    {
        // Schwarz D: a diamond of tubes -- a rhombus round a rhombus
        QPainterPath outer;
        outer.addPolygon(QPolygonF({QPointF(8, 1.6), QPointF(14.4, 8), QPointF(8, 14.4), QPointF(1.6, 8)}));
        outer.closeSubpath();
        QPainterPath inner;
        inner.addPolygon(QPolygonF({QPointF(8, 5.2), QPointF(10.8, 8), QPointF(8, 10.8), QPointF(5.2, 8)}));
        inner.closeSubpath();
        p.setPen(strokeOf(c, 1.5));
        p.setBrush(alpha(c, 95));
        p.drawPath(outer.subtracted(inner));
        return true;
    }
    if (kind == "neovius")
    {
        // four lobes round a hole
        QPainterPath lobes;
        for (const QPointF& q : {QPointF(5.4, 5.4), QPointF(10.6, 5.4), QPointF(5.4, 10.6), QPointF(10.6, 10.6)}) lobes.addEllipse(q, 3.2, 3.2);
        QPainterPath hole;
        hole.addEllipse(QPointF(8, 8), 1.3, 1.3);
        p.setPen(strokeOf(c, 1.4));
        p.setBrush(alpha(c, 95));
        p.drawPath(lobes.subtracted(hole));
        return true;
    }
    if (kind == "lidinoid")
    {
        // a flower of petals turned round a point
        QPainterPath petals;
        for (int k = 0; k < 3; ++k)
        {
            QPainterPath one;
            one.addEllipse(QPointF(0, 0), 6.2, 2.3);
            petals = petals.united(QTransform().translate(8, 8).rotate(60.0 * k).map(one));
        }
        p.setPen(strokeOf(c, 1.4));
        p.setBrush(alpha(c, 95));
        p.drawPath(petals);
        return true;
    }
    if (kind == "iwp")
    {
        // a ring with a ring of passages in it
        p.setPen(strokeOf(c, 1.5));
        p.setBrush(alpha(c, 70));
        p.drawEllipse(QPointF(8, 8), 6.3, 6.3);
        p.setBrush(Qt::NoBrush);
        const QPolygonF dots = regularOf(QPointF(8, 8), 3.3, 6, 90.0);
        p.setPen(strokeOf(alpha(c, 210), 1.2));
        for (const QPointF& q : dots) p.drawEllipse(q, 1.3, 1.3);
        nodeAt(p, c, QPointF(8, 8), 1.2);
        return true;
    }
    if (kind == "frd")
    {
        // an eight-pointed star: two squares, one turned
        QPainterPath star;
        star.addPolygon(regularOf(QPointF(8, 8), 6.6, 4, 45.0));
        star.closeSubpath();
        QPainterPath turned;
        turned.addPolygon(regularOf(QPointF(8, 8), 6.6, 4, 90.0));
        turned.closeSubpath();
        p.setPen(strokeOf(c, 1.4));
        p.setBrush(alpha(c, 95));
        p.drawPath(star.united(turned));
        return true;
    }
    if (kind == "fischer_koch_s")
    {
        // an S
        QPainterPath s;
        s.moveTo(11.6, 3.4);
        s.cubicTo(3.6, 0.8, 3.4, 7.2, 8.0, 8.0);
        s.cubicTo(12.6, 8.8, 12.4, 15.2, 4.4, 12.6);
        p.setPen(strokeOf(c, 2.2));
        p.drawPath(s);
        return true;
    }
    // ---- a planar pattern
    if (kind == "hexagon")
    {
        // a honeycomb
        p.setPen(strokeOf(c, 1.3));
        p.setBrush(alpha(c, 60));
        for (const QPointF& q : {QPointF(5.14, 5.4), QPointF(10.86, 5.4), QPointF(8.0, 10.35)}) p.drawPolygon(regularOf(q, 3.3, 6, 90.0));
        return true;
    }
    if (kind == "triangle" || kind == "square")
    {
        // a grid of triangles, a grid of squares
        const QRectF frame(2.2, 2.2, 11.6, 11.6);
        p.setPen(strokeOf(alpha(c, 220), 1.4));
        p.setBrush(alpha(c, 45));
        p.drawRoundedRect(frame, 1.2, 1.2);
        p.setBrush(Qt::NoBrush);
        p.setPen(strokeOf(c, 1.1));
        p.save();
        p.setClipRect(frame);
        if (kind == "square")
        {
            for (double t : {6.07, 9.93})
            {
                p.drawLine(QPointF(t, 2.2), QPointF(t, 13.8));
                p.drawLine(QPointF(2.2, t), QPointF(13.8, t));
            }
        }
        else
        {
            for (double y : {5.1, 8.0, 10.9}) p.drawLine(QPointF(2.2, y), QPointF(13.8, y));
            const double step = 4.0, run = 11.6 * (step / 2) / 2.9;
            for (int k = -3; k <= 4; ++k)
            {
                p.drawLine(QPointF(2.2 + k * step, 13.8), QPointF(2.2 + k * step + run, 2.2));
                p.drawLine(QPointF(2.2 + k * step + run, 13.8), QPointF(2.2 + k * step, 2.2));
            }
        }
        p.restore();
        return true;
    }
    if (kind == "kagome")
    {
        // the star of two triangles
        p.setPen(strokeOf(c, 1.4));
        p.setBrush(alpha(c, 50));
        p.drawPolygon(regularOf(QPointF(8, 8), 6.4, 3, 90.0));
        p.drawPolygon(regularOf(QPointF(8, 8), 6.4, 3, 270.0));
        return true;
    }
    // ---- a cell of your own
    if (kind == "custom")
    {
        // a box and what is in it
        p.setPen(strokeOf(c, 1.5));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(2.6, 2.6, 10.8, 10.8));
        nodeAt(p, c, QPointF(8, 8), 3.3);
        return true;
    }
    if (kind == "custom_truss")
    {
        // struts and nodes of your own
        const QPointF a(2.8, 12.6), b(13.2, 12.6), t(8, 3.0), m(8, 9.0);
        p.setPen(strokeOf(c, 1.5));
        p.drawLine(a, t); p.drawLine(t, b); p.drawLine(a, b);
        p.setPen(strokeOf(alpha(c, 215), 1.2));
        p.drawLine(m, a); p.drawLine(m, b); p.drawLine(m, t);
        for (const QPointF& q : {a, b, t}) nodeAt(p, c, q, 1.5);
        nodeAt(p, c, m, 1.8);
        return true;
    }
    if (kind == "custom_tpms")
    {
        // a surface of your own equation: a wave, and the f of a function
        p.setPen(strokeOf(c, 1.8));
        p.drawPath(sineOf(1.8, 14.2, 10.6, 2.6, 1.0, 0.0));
        drawF(p, c.lighter(150), QPointF(8.4, 1.4), 6.4, 1.4);
        return true;
    }
    return false;
}

// The picture of a primitive; false when it has none of its own
bool drawPrimitive(QPainter& p, const QString& name, const QColor& c)
{
    p.save();
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::NoBrush);
    bool done;
    if (name.startsWith("cell_")) done = drawCell(p, name.mid(5), c);
    else done = drawShape(p, name, c) || drawCondition(p, name, c) || drawMaterial(p, name, c);
    p.restore();
    return done;
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

QIcon icon(const QString& type, bool block, const QString& domain, const QString& primitive)
{
    // (only a simulation and what an analysis is given have a picture of their domain)
    const QString d = (type == "simulation" || type == "conditions") ? domain : QString();
    const QString key = type + (block ? "+block" : "") + (d.isEmpty() ? QString() : "@" + d) + (primitive.isEmpty() ? QString() : "#" + primitive);
    auto it = cache().constFind(key);
    if (it != cache().constEnd()) return *it;
    const QColor c = color(type, d);
    QIcon made = draw([&](QPainter& p) {
        // (a primitive has its own picture; one that has none -- and every model that is not a primitive -- has the picture of its kind)
        if (primitive.isEmpty() || !drawPrimitive(p, primitive, c)) drawKind(p, type, c, d);
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
