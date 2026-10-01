/*
FielDes: field-driven design
Derived from Studio, a simple GUI for the libfive CAD kernel

Colour maps for scalar fields shown on models (FEA results and the like).

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/
#pragma once

#include <algorithm>
#include <cmath>

#include <QColor>
#include <QString>

namespace FielDes {

/*  t in [0, 1] -> colour.  "turbo" (Google's rainbow, polynomial fit by
 *  Mikhailov), "viridis" (polynomial fit), "fit" (a part's own light grey
 *  turning red: shades where an import is approximate), anything else
 *  grey.  */
inline void colormapRGB(const QString& map, float t, float& r, float& g, float& b)
{
    if (!(t == t)) t = 0;   // NaN
    t = std::max(0.f, std::min(1.f, t));
    if (map == "viridis")
    {
        r = 0.2777273f + t*(0.1050930f + t*(-0.3308618f + t*(-4.6342305f + t*(6.2282699f + t*(4.7763850f + t*-5.4354559f)))));
        g = 0.0054073f + t*(1.4046134f + t*(0.2148476f + t*(-5.7991007f + t*(14.1799334f + t*(-13.7451454f + t*4.6458526f)))));
        b = 0.3340998f + t*(1.3845901f + t*(0.0950952f + t*(-19.3324409f + t*(56.6905526f + t*(-65.3530326f + t*26.3124352f)))));
    }
    else if (map == "fit")
    {
        r = 0.80f + t * (0.92f - 0.80f);
        g = 0.80f + t * (0.16f - 0.80f);
        b = 0.77f + t * (0.13f - 0.77f);
    }
    else if (map == "grey" || map == "gray")
    {
        r = g = b = 0.15f + 0.8f * t;
    }
    else
    {
        r = 0.13572138f + t*(4.61539260f + t*(-42.66032258f + t*(132.13108234f + t*(-152.94239396f + t*59.28637943f))));
        g = 0.09140261f + t*(2.19418839f + t*(4.84296658f + t*(-14.18503333f + t*(4.27729857f + t*2.82956604f))));
        b = 0.10667330f + t*(12.64194608f + t*(-60.58204836f + t*(110.36276771f + t*(-89.90310912f + t*27.34824973f))));
    }
    r = std::max(0.f, std::min(1.f, r));
    g = std::max(0.f, std::min(1.f, g));
    b = std::max(0.f, std::min(1.f, b));
}

inline QColor colormapColor(const QString& map, float t)
{
    float r, g, b;
    colormapRGB(map, t, r, g, b);
    return QColor::fromRgbF(r, g, b);
}

}   // namespace FielDes
