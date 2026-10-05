#version 150

uniform vec4 color_mul;
uniform vec4 color_add;
uniform int shading;

// Section view: faces seen from their back (the inside of a cut solid)
// are drawn flat in cut_color when cut_mode is non-zero; its sign says
// which gl_FrontFacing value means "back"; -1 = GL convention (back faces)
uniform int cut_mode;
uniform vec4 cut_color;

// A patch of a surface: where the value, interpolated from the vertices, is above zero the part is not drawn
// (the vertices that are not in the patch are the ones above zero: the edge of the patch runs through the
// triangles between them where the value crosses zero)
uniform int patch_mode;

in vec3 frag_norm;
in vec3 frag_pos;
in vec4 frag_color;
in float frag_patch;

out vec4 fragColor;

void main() {
    if (patch_mode != 0 && frag_patch > 0.0)
    {
        discard;
    }
    if (cut_mode != 0 && (gl_FrontFacing == (cut_mode > 0)))
    {
        fragColor = cut_color;
        return;
    }
    if (shading != 0)
    {
        vec3 norm;
        if (shading == 1 || shading == 4)
        {
            norm = frag_norm;
        }
        else
        {
            norm = cross(dFdx(frag_pos), dFdy(frag_pos));
        }
        norm.z /= 8;
        // Degenerate (sliver) fragments have no usable derivative normal
        if (!(length(norm) > 1e-20))
        {
            norm = vec3(0.0, 0.0, 1.0);
        }
        norm = normalize(norm);

        // Per-fragment shading
        vec3 dpos = normalize(vec3(1.0, -1.0, 4.0) - frag_pos);
        float brightness = clamp(dot(norm, dpos), 0.0, 1.0);
        // shading 4: lit from the eye, so that a surface and its mirror image are lit alike (the symbols of boundary conditions)
        if (shading == 4)
        {
            brightness = 0.2 + 0.8 * abs(norm.z);
        }

        // shading 3: the vertex colour (a field's colour map), lit
        fragColor = (shading == 3)
            ? vec4(frag_color.rgb * (0.35 + 0.65 * brightness), 1.0)
            : vec4(brightness, brightness, brightness, 1.0);
    }
    else
    {
        fragColor = frag_color;
    }
    fragColor = fragColor * color_mul + color_add;
}
