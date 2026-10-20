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

/*  The categories of the colour map "bc" (boundary conditions, a selected surface): the field's value over
 *  (0, kBcCategories) is the number of the category, 0 being the part itself  */
constexpr int kBcCategories = 13;

inline void bcCategoryRGB(int k, float& r, float& g, float& b)
{
    static const float table[kBcCategories + 1][3] = {
        {0.80f, 0.80f, 0.77f},      // the part
        {0.149f, 0.545f, 0.824f},   // 1 fixed support (blue)
        {0.165f, 0.631f, 0.596f},   // 2 sliding support (cyan)
        {0.863f, 0.196f, 0.184f},   // 3 force (red)
        {0.796f, 0.294f, 0.086f},   // 4 gravity (orange)
        {0.710f, 0.537f, 0.000f},   // 5 heat in (yellow)
        {0.827f, 0.212f, 0.510f},   // 6 a selected surface (magenta)
        {0.520f, 0.600f, 0.000f},   // 7 inlet (green)
        {0.420f, 0.443f, 0.769f},   // 8 outlet (violet)
        {0.576f, 0.631f, 0.631f},   // 9 wall (grey)
        {0.550f, 0.800f, 0.950f},   // 10 slip (light blue)
        {0.950f, 0.550f, 0.150f},   // 11 fixed temperature (bright orange)
        {0.700f, 0.450f, 0.800f},   // 12 convection (purple)
        {0.950f, 0.780f, 0.250f}};  // 13 heat generated (gold)
    k = std::max(0, std::min(kBcCategories, k));
    r = table[k][0];
    g = table[k][1];
    b = table[k][2];
}

/*  Where the value v of a field stands on the section view's scale (-1 deep blue ... 0 cream ... 1 deep orange; fieldColour in
 *  section.cpp, and gl/slice.frag, have the colours), for a field whose values run from lo to hi: a field that has both signs has its zero
 *  where the section view has the surface (blue below it, scaled by the lowest value; orange above it, by the highest), any other has its
 *  lowest value at the deep blue and its highest at the deep orange  */
inline float sectionLevel(float v, float lo, float hi)
{
    if (lo < 0.f && hi > 0.f) return std::max(-1.f, std::min(1.f, v < 0.f ? v / -lo : v / hi));
    return hi > lo ? std::max(-1.f, std::min(1.f, 2.f * (v - lo) / (hi - lo) - 1.f)) : 0.f;
}

/*  t in [0, 1] -> colour.  "turbo" (Google's rainbow, polynomial fit by
 *  Mikhailov), "viridis" (polynomial fit), "fit" (a part's own light grey
 *  turning red: shades where an import is approximate), "share", "change"
 *  and "violation" (the part's grey through amber to red: how far a rule is
 *  broken; blue, grey, red: what a shell cut and added; grey, red: where a
 *  rule is kept and where it is broken), "support" (amber: the supports of
 *  a print, one class), "bc" (the nearest
 *  of the categories of boundary conditions and selections), anything else
 *  grey.  */
inline void colormapRGB(const QString& map, float t, float& r, float& g, float& b)
{
    if (!(t == t)) t = 0;   // NaN
    t = std::max(0.f, std::min(1.f, t));
    if (map == "bc")
    {
        bcCategoryRGB(int(std::lround(t * float(kBcCategories))), r, g, b);
        return;
    }
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
    else if (map == "support")
    {
        // the supports of a print: ONE class and no scale -- amber, which no other class of a manufacturing result is (grey, red and blue are the change, grey and red the rule); the card draws one swatch
        r = 0.95f;
        g = 0.70f;
        b = 0.20f;
    }
    else if (map == "violation")
    {
        // a rule check: TWO classes and no scale -- the part's own light grey where the rule is kept, red where it is broken; the card draws them as two swatches
        const bool broken = t >= 0.5f;
        r = broken ? 0.80f : 0.80f;
        g = broken ? 0.10f : 0.80f;
        b = broken ? 0.10f : 0.77f;
    }
    else if (map == "share" || map == "change")
    {
        // "share": how far past its limit a rule is broken -- the part's own light grey where it is kept (0), through amber, to red where it is broken by as much as
        // its limit (1).  A part that needs nothing looks as any part does, and not black, which is what the rainbow's end is.
        // "change": the geometry change of a shell, THREE classes and no scale -- blue where material was removed (below a quarter), the part's own light grey where it is
        // unchanged, red where material was added (above three quarters); the card draws them as three swatches
        static const float grey[3] = {0.80f, 0.80f, 0.77f};
        static const float amber[3] = {0.95f, 0.62f, 0.15f};
        static const float red[3] = {0.80f, 0.10f, 0.10f};
        static const float blue[3] = {0.13f, 0.40f, 0.80f};
        float mixed[3] = {0.f, 0.f, 0.f};
        const float* start;
        const float* to;
        float u = 0.f;
        if (map == "share")
        {
            const bool low = t < 0.5f;
            start = low ? grey : amber;
            to = low ? amber : red;
            u = low ? 2.f * t : 2.f * (t - 0.5f);
        }
        else
        {
            // (a soft edge between the classes, not a hard one: the colour is read at the vertices of a mesh and blended across its triangles, and a hard edge between two classes follows the edges of
            // the triangles -- a zigzag, where a thickened lattice meets the flat face it was cut from.  The value runs from 0 (cut) through 0.5 (as it was) to 1 (added); each class is full from
            // a third of the way, and the blend is a smooth step, so the edge between two colours is a curve and not a staircase of triangles)
            const auto smooth = [](float a, float b, float x) {
                const float u = std::max(0.f, std::min(1.f, (x - a) / (b - a)));
                return u * u * (3.f - 2.f * u);
            };
            const float toRed = smooth(0.58f, 0.86f, t), toBlue = smooth(0.42f, 0.14f, t);
            for (int a = 0; a < 3; ++a) mixed[a] = grey[a] + toRed * (red[a] - grey[a]) + toBlue * (blue[a] - grey[a]);
            start = to = mixed;
        }
        r = start[0] + u * (to[0] - start[0]);
        g = start[1] + u * (to[1] - start[1]);
        b = start[2] + u * (to[2] - start[2]);
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
