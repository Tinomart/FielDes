#version 150

// Section plane: colours the signed distance field (a float texture of raw
// values) per fragment, so iso-lines and the zero contour stay crisp at any
// zoom.  The colour stops match fieldColour() in section.cpp.  For a model
// shown with an analysis result (color_mode != 0), the plane shows that
// result inside the model instead (a second float texture), through the
// result's colour map, and nothing outside.

uniform sampler2D field;
uniform sampler2D color_field;
uniform int color_mode;     // 0: distance; 1 turbo, 2 viridis, 3 grey; 4: a field of the field viewer, in the section's colours
uniform float color_lo;
uniform float color_hi;
uniform float opacity;
uniform float range_in;     // colour scale inside (negative values)
uniform float range_out;    // colour scale outside
uniform float spacing;      // iso-line spacing
uniform float fade;         // outside distance where the plane fades out

in vec2 frag_uv;
out vec4 fragColor;

vec3 cmap(float t)
{
    t = clamp(t, -1.0, 1.0);
    vec3 inside_far  = vec3(0.06, 0.20, 0.48);
    vec3 inside_near = vec3(0.60, 0.78, 0.93);
    vec3 mid         = vec3(0.97, 0.96, 0.93);
    vec3 out_near    = vec3(0.99, 0.78, 0.55);
    vec3 out_far     = vec3(0.60, 0.20, 0.04);
    float a = abs(t);
    if (t < 0.0)
        return a < 0.25 ? mix(mid, inside_near, a / 0.25)
                        : mix(inside_near, inside_far, (a - 0.25) / 0.75);
    return a < 0.25 ? mix(mid, out_near, a / 0.25)
                    : mix(out_near, out_far, (a - 0.25) / 0.75);
}

// The same colour maps as studio/colormap.hpp
vec3 resultMap(float t)
{
    t = clamp(t, 0.0, 1.0);
    if (color_mode == 2)
    {
        return clamp(vec3(
            0.2777273 + t*(0.1050930 + t*(-0.3308618 + t*(-4.6342305 + t*(6.2282699 + t*(4.7763850 + t*-5.4354559))))),
            0.0054073 + t*(1.4046134 + t*(0.2148476 + t*(-5.7991007 + t*(14.1799334 + t*(-13.7451454 + t*4.6458526))))),
            0.3340998 + t*(1.3845901 + t*(0.0950952 + t*(-19.3324409 + t*(56.6905526 + t*(-65.3530326 + t*26.3124352)))))),
            0.0, 1.0);
    }
    if (color_mode == 3)
    {
        return vec3(0.15 + 0.8 * t);
    }
    return clamp(vec3(
        0.13572138 + t*(4.61539260 + t*(-42.66032258 + t*(132.13108234 + t*(-152.94239396 + t*59.28637943)))),
        0.09140261 + t*(2.19418839 + t*(4.84296658 + t*(-14.18503333 + t*(4.27729857 + t*2.82956604)))),
        0.10667330 + t*(12.64194608 + t*(-60.58204836 + t*(110.36276771 + t*(-89.90310912 + t*27.34824973))))),
        0.0, 1.0);
}

void main()
{
    float d = texture(field, frag_uv).r;
    if (!(d < fade))            // also rejects NaN
    {
        discard;
    }
    // Line widths in screen pixels, from the field's screen-space gradient
    float w = max(fwidth(d), 1e-12);

    if (color_mode == 4)
    {
        // A field of the field viewer (d is the distance field of its disc): the section's colours over the field's range, the section's
        // dark lines at round steps of its values, a heavier one where it is zero when that is in range, the rim of the disc dark
        float z = 1.0 - smoothstep(0.7 * w, 1.8 * w, abs(d));
        float v = texture(color_field, frag_uv).r;
        if (!(d < 0.0) || !(v == v))
        {
            if (z < 0.05) discard;
            fragColor = vec4(0.08, 0.08, 0.08, z * opacity);
            return;
        }
        float span = color_hi - color_lo;
        float u = (color_lo < 0.0 && color_hi > 0.0) ? (v < 0.0 ? v / -color_lo : v / color_hi)
                                                      : (span > 0.0 ? 2.0 * (v - color_lo) / span - 1.0 : 0.0);
        vec3 c = cmap(clamp(u, -1.0, 1.0));
        float wv = max(fwidth(v), 1e-12);
        float f = abs(v - spacing * floor(v / spacing + 0.5));
        float iso = 1.0 - smoothstep(0.35 * wv, 1.1 * wv, f);
        c = mix(c, c * 0.68, 0.85 * iso);
        if (color_lo < 0.0 && color_hi > 0.0)
        {
            float zv = 1.0 - smoothstep(0.7 * wv, 1.8 * wv, abs(v));
            c = mix(c, vec3(0.08, 0.08, 0.08), zv);
        }
        c = mix(c, vec3(0.08, 0.08, 0.08), z);
        fragColor = vec4(c, opacity);
        return;
    }

    if (color_mode != 0)
    {
        float z = 1.0 - smoothstep(0.7 * w, 1.8 * w, abs(d));
        float v = texture(color_field, frag_uv).r;
        if (!(d < 0.0) || !(v == v))
        {
            // outside the model: only its outline
            if (z < 0.05) discard;
            fragColor = vec4(0.08, 0.08, 0.08, z * opacity);
            return;
        }
        float span = color_hi - color_lo;
        vec3 c = resultMap(span > 0.0 ? (v - color_lo) / span : 0.5);
        c = mix(c, vec3(0.08, 0.08, 0.08), z);
        fragColor = vec4(c, opacity);
        return;
    }

    vec3 c = cmap(d < 0.0 ? d / range_in : d / range_out);

    // Iso-lines
    float f = abs(d - spacing * floor(d / spacing + 0.5));
    float iso = 1.0 - smoothstep(0.35 * w, 1.1 * w, f);
    c = mix(c, c * 0.68, 0.85 * iso);

    // The surface itself (zero contour), a little heavier
    float z = 1.0 - smoothstep(0.7 * w, 1.8 * w, abs(d));
    c = mix(c, vec3(0.08, 0.08, 0.08), z);

    // Fade out with distance from the model
    float a = 1.0 - smoothstep(0.55 * fade, fade, d);
    fragColor = vec4(c, a * opacity);
}
