/*
FielDes: field-driven design
Derived from Studio, a simple GUI for the libfive CAD kernel
Copyright (C) 2017  Matt Keeter

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/
#include <array>
#include <cmath>
#include <vector>

#include <QColor>
#include <Eigen/StdVector>
#include <boost/math/constants/constants.hpp>

#include "fieldes/arrow.hpp"
#include "fieldes/shader.hpp"

namespace FielDes {

Arrow::Arrow()
{
    // Nothing to do here
}

void Arrow::initializeGL(int res)
{
    initializeOpenGLFunctions();

    tri_count = res * 8;
    Eigen::Array<float, Eigen::Dynamic, 18, Eigen::RowMajor> vs(tri_count, 18);

    Eigen::Array<float, 1, 18> zflip = Eigen::Array<float, 1, 18>::Ones();
    for (unsigned i=2; i < 18; i += 3)
    {
        zflip(i - 1) = -1;
        zflip(i) = -1;
    }

    const float pi = boost::math::constants::pi<float>();
    const float ro = 0.2;
    const float ri = 0.1;
    const float ah = 0.4;
    for (int i=0; i < res; ++i)
    {
        float a0 = pi * 2 * i / (res - 1.0);
        float a1 = pi * 2 * (i + 1) / (res - 1.0);
        float x0 = cos(a0);
        float y0 = sin(a0);
        float x1 = cos(a1);
        float y1 = sin(a1);

        // Each row is a triangle, with per-vertex values for
        //                 Position                 Normal
        vs.row(8*i + 0) << 0,0,1,                   (x0 + x1),(y0 + y1),0,
                           x0*ro, y0*ro, 1 - ah,    x0, y0, 0,
                           x1*ro, y1*ro, 1 - ah,    x1, y1, 0;
        vs.row(8*i + 1) << x0*ro, y0*ro, 1 - ah,    0, 0, -1,
                           x1*ri, y1*ri, 1 - ah,    0, 0, -1,
                           x1*ro, y1*ro, 1 - ah,    0, 0, -1;
        vs.row(8*i + 2) << x0*ro, y0*ro, 1 - ah,    0, 0, -1,
                           x0*ri, y0*ri, 1 - ah,    0, 0, -1,
                           x1*ri, y1*ri, 1 - ah,    0, 0, -1;
        vs.row(8*i + 3) << x1*ri, y1*ri, 1 - ah,    x1, y1, 0,
                           x0*ri, y0*ri, 1 - ah,    x0, y0, 0,
                           x0*ri, y0*ri, ah - 1,    x0, y0, 0;
        vs.row(8*i + 4) << x1*ri, y1*ri, 1 - ah,    x1, y1, 0,
                           x0*ri, y0*ri, ah - 1,    x0, y0, 0,
                           x1*ri, y1*ri, ah - 1,    x0, y0, 0;

        vs.row(8*i + 5) = vs.row(8*i + 0) * zflip;
        vs.row(8*i + 6) = vs.row(8*i + 1) * zflip;
        vs.row(8*i + 7) = vs.row(8*i + 2) * zflip;
    }

    vao.create();
    vao.bind();

    vbo.create();
    vbo.setUsagePattern(QOpenGLBuffer::StaticDraw);
    vbo.bind();
    vbo.allocate(vs.data(), vs.size() * sizeof(*vs.data()));

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6*sizeof(GLfloat), NULL);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 6*sizeof(GLfloat),
                          (GLvoid*)(3 * sizeof(GLfloat)));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(2);

    vbo.release();
    vao.release();
}

void Arrow::draw(QMatrix4x4 M, QVector3D pos, float scale,
                 QVector3D norm, QColor color)
{
    // Build an orthonormal basis, per Duff 2017
    QMatrix4x4 rot;
    rot.setColumn(2, norm.normalized().toVector4D());
    if (norm.z() < 0.)
    {
        const float a = 1.0f / (1.0f - norm.z());
        const float b = norm.x() * norm.y() * a;
        rot.setColumn(0, {1.0f - norm.x() * norm.x() * a, -b, norm.x(), 0});
        rot.setColumn(1, {b, norm.y() * norm.y()*a - 1.0f, -norm.y(), 0});
    }
    else
    {
        const float a = 1.0f / (1.0f + norm.z());
        const float b = -norm.x() * norm.y() * a;
        rot.setColumn(0, {1.0f - norm.x() * norm.x() * a, b, -norm.x(), 0});
        rot.setColumn(1, {b, 1.0f - norm.y() * norm.y() * a, -norm.y(), 0});
    }

    M.translate(pos);
    M.scale(scale);
    M *= rot;

    Shader::basic->bind();
    glUniformMatrix4fv(Shader::basic->uniformLocation("M"),
                       1, GL_FALSE, M.data());
    glUniform1i(Shader::basic->uniformLocation("shading"), 1); // per-triangle
    glUniform4f(Shader::basic->uniformLocation("color_add"),
                color.redF()*0.6, color.greenF()*0.6, color.blueF()*0.6, 0.0f);
    vao.bind();

    // Draw once at 20% opacity but without depth culling
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_STENCIL_TEST);
    glStencilFuncSeparate(GL_FRONT, GL_GREATER, 1, 0xFF);
    glStencilFuncSeparate(GL_BACK, GL_NEVER, 0, 0xFF);
    glStencilOpSeparate(GL_FRONT, GL_KEEP, GL_INCR, GL_INCR);
    glStencilOpSeparate(GL_BACK, GL_KEEP, GL_INCR, GL_INCR);

    glUniform4f(Shader::basic->uniformLocation("color_mul"),
                color.redF()/2, color.greenF()/2, color.blueF()/2, 0.3f);
    glDrawArrays(GL_TRIANGLES, 0, tri_count * 3);

    // Then draw in full color, with depth culling on
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);

    glUniform4f(Shader::basic->uniformLocation("color_mul"),
                color.redF()/2, color.greenF()/2, color.blueF()/2, 1.0f);
    glDrawArrays(GL_TRIANGLES, 0, tri_count * 3);

    vao.release();
    Shader::basic->release();
}

////////////////////////////////////////////////////////////////////////////////
// Glyphs: the arrows and pads of boundary conditions

void Glyphs::initializeGL()
{
    initializeOpenGLFunctions();

    std::vector<float> vs;      // position, normal; three vertices to a triangle
    auto vertex = [&](float x, float y, float z, float nx, float ny, float nz) {
        vs.insert(vs.end(), {x, y, z, nx, ny, nz});
    };
    const float pi = boost::math::constants::pi<float>();
    const int res = 28;

    // A surface of revolution about z: rings of (radius, z, normal radial part, normal z part), consecutive rings joined
    auto revolve = [&](const std::vector<std::array<float, 4>>& rings) {
        for (size_t k = 0; k + 1 < rings.size(); ++k)
            for (int i = 0; i < res; ++i)
            {
                const float a0 = 2 * pi * i / res, a1 = 2 * pi * (i + 1) / res;
                const float c0 = std::cos(a0), s0 = std::sin(a0), c1 = std::cos(a1), s1 = std::sin(a1);
                const auto& r0 = rings[k];
                const auto& r1 = rings[k + 1];
                auto v = [&](const std::array<float, 4>& r, float c, float s) {
                    vertex(r[0] * c, r[0] * s, r[1], r[2] * c, r[2] * s, r[3]);
                };
                if (r0[0] > 0)
                {
                    v(r0, c0, s0); v(r0, c1, s1); v(r1, c0, s0);
                }
                if (r1[0] > 0)
                {
                    v(r0, c1, s1); v(r1, c1, s1); v(r1, c0, s0);
                }
            }
    };
    // A flat annulus between two radii at height z, facing up (sign +1) or down (-1)
    auto annulus = [&](float rIn, float rOut, float z, float sign) {
        for (int i = 0; i < res; ++i)
        {
            const float a0 = 2 * pi * i / res, a1 = 2 * pi * (i + 1) / res;
            const float c0 = std::cos(a0), s0 = std::sin(a0), c1 = std::cos(a1), s1 = std::sin(a1);
            auto v = [&](float r, float c, float s) { vertex(r * c, r * s, z, 0, 0, sign); };
            if (sign > 0)
            {
                v(rIn, c0, s0); v(rOut, c0, s0); v(rOut, c1, s1);
                if (rIn > 0) { v(rIn, c0, s0); v(rOut, c1, s1); v(rIn, c1, s1); }
            }
            else
            {
                v(rIn, c0, s0); v(rOut, c1, s1); v(rOut, c0, s0);
                if (rIn > 0) { v(rIn, c0, s0); v(rIn, c1, s1); v(rOut, c1, s1); }
            }
        }
    };

    // The arrow: a shaft from z=0, a cone to z=1
    const float rs = 0.075f, rh = 0.23f, zh = 0.60f;
    const float hl = 1.0f - zh;                                      // the cone's slant: normal = (hl, rh) normalised
    const float nn = std::sqrt(hl * hl + rh * rh);
    revolve({{rs, 0.0f, 1.0f, 0.0f}, {rs, zh, 1.0f, 0.0f}});            // shaft
    annulus(0.0f, rs, 0.0f, -1.0f);                                     // its tail
    annulus(rs, rh, zh, -1.0f);                                         // the cone's base
    revolve({{rh, zh, hl / nn, rh / nn}, {0.0f, 1.0f, hl / nn, rh / nn}});   // the cone
    arrow_verts = int(vs.size() / 6);

    // The pad of a support: a low disc lying on the surface (z=0), its rim chamfered -- a patch of "held" on the face
    // that does not look like an arrow
    const float pr = 0.5f, pt = 0.42f, ph = 0.16f;
    const float pnr = ph, pnz = pr - pt;                                         // the chamfer's normal (radial, up), unnormalised
    const float pn = std::sqrt(pnr * pnr + pnz * pnz);
    revolve({{pr, 0.0f, pnr / pn, pnz / pn}, {pt, ph, pnr / pn, pnz / pn}});
    annulus(0.0f, pt, ph, 1.0f);                                                 // its top
    pad_verts = int(vs.size() / 6) - arrow_verts;

    vao.create();
    vao.bind();
    vbo.create();
    vbo.setUsagePattern(QOpenGLBuffer::StaticDraw);
    vbo.bind();
    vbo.allocate(vs.data(), int(vs.size() * sizeof(float)));
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(GLfloat), NULL);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(GLfloat), (GLvoid*)(3 * sizeof(GLfloat)));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(2);
    vbo.release();
    vao.release();
}

void Glyphs::drawArrow(const QMatrix4x4& M, QVector3D pos, float length, QVector3D dir, QColor color, bool tipAtPos)
{
    const QVector3D d = dir.normalized();
    draw(M, tipAtPos ? pos - d * length : pos, length, d, color, 0, arrow_verts);
}

void Glyphs::drawPad(const QMatrix4x4& M, QVector3D pos, float diameter, QVector3D normal, QColor color)
{
    draw(M, pos, diameter, normal.normalized(), color, arrow_verts, pad_verts);
}

void Glyphs::draw(QMatrix4x4 M, QVector3D origin, float size, QVector3D dir, QColor color, int first, int count)
{
    if (count <= 0 || !(size > 0)) return;
    // An orthonormal basis with z along dir, per Duff 2017 (as Arrow::draw)
    QMatrix4x4 rot;
    rot.setColumn(2, dir.toVector4D());
    if (dir.z() < 0.)
    {
        const float a = 1.0f / (1.0f - dir.z());
        const float b = dir.x() * dir.y() * a;
        rot.setColumn(0, {1.0f - dir.x() * dir.x() * a, -b, dir.x(), 0});
        rot.setColumn(1, {b, dir.y() * dir.y() * a - 1.0f, -dir.y(), 0});
    }
    else
    {
        const float a = 1.0f / (1.0f + dir.z());
        const float b = -dir.x() * dir.y() * a;
        rot.setColumn(0, {1.0f - dir.x() * dir.x() * a, b, -dir.x(), 0});
        rot.setColumn(1, {b, 1.0f - dir.y() * dir.y() * a, -dir.y(), 0});
    }
    M.translate(origin);
    M.scale(size);
    M *= rot;

    Shader::basic->bind();
    glUniformMatrix4fv(Shader::basic->uniformLocation("M"), 1, GL_FALSE, M.data());
    glUniform1i(Shader::basic->uniformLocation("shading"), 4);          // lit by the vertex normals, from the eye
    glUniform1i(Shader::basic->uniformLocation("use_rest"), 0);
    glUniform1i(Shader::basic->uniformLocation("cut_mode"), 0);
    glUniform4f(Shader::basic->uniformLocation("color_add"), color.redF() * 0.30f, color.greenF() * 0.30f, color.blueF() * 0.30f, 0.0f);
    glUniform4f(Shader::basic->uniformLocation("color_mul"), color.redF() * 0.70f, color.greenF() * 0.70f, color.blueF() * 0.70f, 1.0f);
    vao.bind();
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_STENCIL_TEST);
    glDrawArrays(GL_TRIANGLES, first, count);
    vao.release();
    Shader::basic->release();
}

}   // namespace FielDes
