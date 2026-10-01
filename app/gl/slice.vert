#version 150
#extension GL_ARB_explicit_attrib_location : require

layout(location=0) in vec3 vertex_position;
layout(location=1) in vec2 vertex_uv;

uniform mat4 M;

out vec2 frag_uv;

void main()
{
    gl_Position = M * vec4(vertex_position, 1.0f);
    gl_Position.w = max(0, gl_Position.w);
    frag_uv = vertex_uv;
}
