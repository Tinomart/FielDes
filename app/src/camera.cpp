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
#include <cmath>

#include <cmath>
#include <QtMath>

#include "fieldes/camera.hpp"

namespace FielDes {

Camera::Camera(QSize size)
    : size(size), anim(this, "perspective")
{
    anim.setDuration(100);
    connect(&anim, &QPropertyAnimation::finished, this, &Camera::animDone);
}

QMatrix4x4 Camera::proj() const
{
    QMatrix4x4 m;

    //  Compress the Z axis to avoid clipping.  The exact value is arbitrary,
    //  but seems to work well for common model aspect ratios.
    const float frac = size.width() / float(size.height());
    const float Z_COMPRESS = 8;
    if (frac > 1)
    {
        m.scale(1/frac, -1, 1 / Z_COMPRESS);
    }
    else
    {
        m.scale(1, -frac, 1 / Z_COMPRESS);
    }

    m(3, 2) = perspective;
    return m;
}

QMatrix4x4 Camera::view() const
{
    QMatrix4x4 m;
    m.scale(scale, scale, scale);
    m.rotate(pitch, {1, 0, 0});
    m.rotate(yaw,   {0, 0, 1});
    m.rotate(axis);
    m.translate(center);

    return m;
}

QMatrix4x4 Camera::M() const
{
    return proj() * view();
}

QMatrix4x4 Camera::rotation() const
{
    QMatrix4x4 m;
    m.rotate(pitch, {1, 0, 0});
    m.rotate(yaw,   {0, 0, 1});
    m.rotate(axis);
    return m;
}

QVector3D Camera::towardViewer() const
{
    // Nearer points have smaller view-space z
    return rotation().inverted().map(QVector3D(0, 0, -1)).normalized();
}

void Camera::lookFrom(QVector3D dir)
{
    // In the turntable frame (after the axis rotation), the camera sits at
    // -(0, sin(pitch), cos(pitch)) rotated by -yaw about Z
    const QVector3D a = axis.rotatedVector(dir.normalized());
    const float pitch_end = qRadiansToDegrees(std::acos(qBound(-1.0f, -a.z(), 1.0f)));
    float yaw_end = 0;
    if (std::hypot(a.x(), a.y()) > 1e-4)
    {
        yaw_end = -90 - qRadiansToDegrees(std::atan2(a.y(), a.x()));
    }
    float dy = yaw_end - yaw;
    while (dy > 180)  dy -= 360;
    while (dy < -180) dy += 360;

    const float pitch_start = pitch;
    const float yaw_start = yaw;

    auto an = new QVariantAnimation(this);
    an->setStartValue(0);
    an->setEndValue(1000);
    an->setDuration(250);
    an->setEasingCurve(QEasingCurve::InOutSine);
    connect(an, &QVariantAnimation::valueChanged,
            this, [=](QVariant _v){
                const float v = _v.toFloat() / 1000.0f;
                pitch = pitch_start + (pitch_end - pitch_start) * v;
                yaw = yaw_start + dy * v;
                emit(changed());
            });
    connect(an, &QPropertyAnimation::finished, this, [=]{
        yaw = std::fmod(yaw, 360.0f);
        emit(animDone());
    });
    an->start(an->DeleteWhenStopped);
}

void Camera::rotateIncremental(QPoint delta)
{
    pitch += rotationSensitivity*float(delta.y()/float(size.height()));
    yaw += rotationSensitivity*float(delta.x()/float(size.width()));

    pitch = fmax(fmin(pitch, 180), 0);
    yaw = fmod(yaw, 360);
}

void Camera::panIncremental(QPoint delta)
{
    // Find the starting position in world coordinates
    auto inv = M().inverted();
    auto diff = inv.map({float(delta.x()/float(size.width())),
                        -float(delta.y()/float(size.height())), 0}) -
                inv.map({0, 0, 0});

    center += diff*2;
}

void Camera::zoomIncremental(float ds, QPoint c)
{
    QVector3D pt(c.x() / float(size.width()) - 0.5,
                -(c.y() / float(size.height()) - 0.5), 0);
    auto a = M().inverted().map(pt);

    scale *= pow(1.1, ds / 120.);
    center += 2 * (M().inverted().map(pt) - a);
}

void Camera::toOrthographic()
{
    anim.setStartValue(perspective);
    anim.setEndValue(0);
    anim.start();
}

void Camera::toPerspective()
{
    anim.setStartValue(perspective);
    anim.setEndValue(0.25);
    anim.start();
}

void Camera::animateAxis(QQuaternion end)
{
    auto a = new QVariantAnimation(this);
    a->setStartValue(0);
    a->setEndValue(1000);
    a->setDuration(200);
    a->setEasingCurve(QEasingCurve::InOutSine);

    const QQuaternion start = axis;

    connect(a, &QVariantAnimation::valueChanged,
            this, [=](QVariant _v){
                auto v = _v.toFloat() / a->endValue().toFloat();
                axis = QQuaternion::slerp(start, end, v);
                emit(changed());
            });
    connect(a, &QPropertyAnimation::finished, this, &Camera::animDone);
    a->start(a->DeleteWhenStopped);
}

void Camera::toTurnZ()
{
    animateAxis(QQuaternion::fromDirection({0, 0, 1}, {0, 1, 0}));
}

void Camera::toTurnY()
{
    animateAxis(QQuaternion::fromDirection({0, 1, 0}, {0, 0, 1}));
}

void Camera::setRotationSensitivity(float sensitivity)
{
    rotationSensitivity = sensitivity;
}

void Camera::zoomTo(const QVector3D& min, const QVector3D& max)
{
    QVector3D center_start = center;
    QVector3D center_end = (min + max) / -2;

    float scale_start = scale;
    float scale_end = 2 / (max - min).length();

    auto a = new QVariantAnimation(this);
    a->setStartValue(0);
    a->setEndValue(1000);
    a->setDuration(100);
    a->setEasingCurve(QEasingCurve::InOutSine);

    connect(a, &QVariantAnimation::valueChanged,
            this, [=](QVariant _v){
                auto v = _v.toFloat() / a->endValue().toFloat();
                scale = scale_end * v + scale_start * (1 - v);
                v = pow(v, 6);
                center = center_end * v + center_start * (1 - v);
                emit(changed());
            });
    connect(a, &QPropertyAnimation::finished, this, &Camera::animDone);
    a->start(a->DeleteWhenStopped);
}

}   // namespace FielDes
