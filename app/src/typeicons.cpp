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
        {"profile",    {"2D shape",     QColor("#35c4b3")}},
        {"field",      {"Field",        QColor("#82cc58")}},
        {"surface",    {"Surface",      QColor("#b583ee")}},
        {"point",      {"Point",        QColor("#f4b73a")}},
        {"simulation", {"Simulation",   QColor("#ee6a5e")}},
        {"material",   {"Material",     QColor("#c9a66b")}},
        {"conditions", {"Conditions",   QColor("#e08f58")}},
        {"cell",       {"Lattice cell", QColor("#d4b43c")}},
        {"import",     {"Import",       QColor("#2aa198")}},
        {"block",      {"Custom block", QColor("#8da2c0")}},
    };
    return k;
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

void drawKind(QPainter& p, const QString& type, const QColor& c)
{
    if (type == "profile")
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

QColor color(const QString& type)
{
    auto it = kinds().constFind(type);
    return it == kinds().constEnd() ? kinds()["solid"].color : it->color;
}

QString label(const QString& type)
{
    auto it = kinds().constFind(type);
    return it == kinds().constEnd() ? type : T(it->label);
}

QIcon icon(const QString& type, bool block)
{
    const QString key = type + (block ? "+block" : "");
    auto it = cache().constFind(key);
    if (it != cache().constEnd()) return *it;
    const QColor c = color(type);
    QIcon made = draw([&](QPainter& p) {
        drawKind(p, type, c);
        if (block) drawBlockMark(p);
    });
    cache().insert(key, made);
    return made;
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
