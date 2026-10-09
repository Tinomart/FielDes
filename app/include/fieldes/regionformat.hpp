/*
FielDes: field-driven design

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/
#pragma once

#include <QString>
#include <cmath>

namespace FielDes {

/*  A corner of the render region as it is shown and written: a whole number, and two decimals only for what is smaller than 1 (a part
 *  a few millimetres across).  `outward` rounds a value the way the region has to keep covering what it covered: down for a minimum
 *  (-1), up for a maximum (+1); 0 is the nearest.  */
inline QString regionNumber(double v, int outward = 0)
{
    if (!std::isfinite(v)) return QStringLiteral("0");
    const double a = std::fabs(v);
    QString s;
    if (a >= 1.0 - 1e-9)
    {
        const double r = outward < 0 ? std::floor(v + 1e-9) : outward > 0 ? std::ceil(v - 1e-9) : std::round(v);
        s = QString::number(r, 'f', 0);
    }
    else
    {
        s = QString::number(std::round(v * 100.0) / 100.0, 'f', 2);
        if (s.contains('.'))
        {
            while (s.endsWith('0')) s.chop(1);
            if (s.endsWith('.')) s.chop(1);
        }
    }
    return (s == "-0" || s.isEmpty()) ? QStringLiteral("0") : s;
}

}   // namespace FielDes
