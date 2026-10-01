/*
FielDes: field-driven design

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <functional>

#include "fieldes/icons.hpp"

namespace FielDes {
namespace Icons {

namespace {

const QColor kInk(0xe4, 0xde, 0xcb);        // (cream: they sit on the dark top dock)
const QColor kAccent(0x3d, 0xa5, 0xec);

// Drawn on a 24 x 24 grid at 3x, so it stays sharp on any screen
QIcon make(std::function<void(QPainter&)> draw)
{
    QIcon icon;
    for (int pass = 0; pass < 2; ++pass)
    {
        QPixmap px(72, 72);
        px.fill(Qt::transparent);
        QPainter p(&px);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(3, 3);
        if (pass == 1) p.setOpacity(0.35);
        draw(p);
        p.end();
        px.setDevicePixelRatio(3);
        icon.addPixmap(px, pass == 0 ? QIcon::Normal : QIcon::Disabled);
    }
    return icon;
}

QPen ink(double w = 1.6)
{
    return QPen(kInk, w, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
}

}   // namespace

QIcon open()
{
    return make([](QPainter& p) {
        p.setPen(ink());
        QPainterPath f;
        f.moveTo(3.5, 7.5);
        f.lineTo(3.5, 18.5);
        f.lineTo(20.5, 18.5);
        f.lineTo(20.5, 9);
        f.lineTo(11, 9);
        f.lineTo(9.3, 6.5);
        f.lineTo(3.5, 6.5);
        f.closeSubpath();
        QColor tint = kAccent;
        tint.setAlpha(45);
        p.setBrush(tint);
        p.drawPath(f);
    });
}

QIcon importFile()
{
    return make([](QPainter& p) {
        p.setPen(ink());
        QPainterPath tray;
        tray.moveTo(4, 14);
        tray.lineTo(4, 19.5);
        tray.lineTo(20, 19.5);
        tray.lineTo(20, 14);
        p.drawPath(tray);
        p.setPen(QPen(kAccent, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawLine(QPointF(12, 3.5), QPointF(12, 14));
        p.drawLine(QPointF(7.8, 9.8), QPointF(12, 14));
        p.drawLine(QPointF(16.2, 9.8), QPointF(12, 14));
    });
}

QIcon logo(int size)
{
    // The mark: a dark tile, the F in cream, its field as two rings
    QPixmap px(size * 2, size * 2);
    px.fill(Qt::transparent);
    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(size * 2 / 24.0, size * 2 / 24.0);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x00, 0x2b, 0x36));
    p.drawRoundedRect(QRectF(0.5, 0.5, 23, 23), 5, 5);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(0x26, 0x8b, 0xd2, 200), 0.7));
    p.drawRoundedRect(QRectF(4.0, 4.0, 14.5, 16), 3.2, 3.2);
    p.setPen(QPen(QColor(0x2a, 0xa1, 0x98, 140), 0.6));
    p.drawRoundedRect(QRectF(2.5, 2.5, 17.5, 19), 3.8, 3.8);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0xfd, 0xf6, 0xe3));
    p.drawRoundedRect(QRectF(7, 6, 3, 12), 0.8, 0.8);
    p.drawRoundedRect(QRectF(7, 6, 9.5, 2.8), 0.8, 0.8);
    p.drawRoundedRect(QRectF(7, 11, 7, 2.6), 0.8, 0.8);
    p.end();
    px.setDevicePixelRatio(2);
    return QIcon(px);
}

}   // namespace Icons
}   // namespace FielDes
