/*
FielDes: field-driven design

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/
#include <algorithm>
#include <cmath>

#include <QDoubleSpinBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include "fieldes/carddrag.hpp"
#include "fieldes/i18n.hpp"
#include "fieldes/symmetry.hpp"

namespace FielDes {

namespace {

float det3(const QMatrix4x4& m)
{
    return m(0, 0) * (m(1, 1) * m(2, 2) - m(1, 2) * m(2, 1)) - m(0, 1) * (m(1, 0) * m(2, 2) - m(1, 2) * m(2, 0)) + m(0, 2) * (m(1, 0) * m(2, 1) - m(1, 1) * m(2, 0));
}

QMatrix4x4 reflection(const QVector3D& n0)
{
    const QVector3D n = n0.normalized();
    return QMatrix4x4(1 - 2 * n.x() * n.x(), -2 * n.x() * n.y(), -2 * n.x() * n.z(), 0,
                      -2 * n.y() * n.x(), 1 - 2 * n.y() * n.y(), -2 * n.y() * n.z(), 0,
                      -2 * n.z() * n.x(), -2 * n.z() * n.y(), 1 - 2 * n.z() * n.z(), 0,
                      0, 0, 0, 1);
}

QMatrix4x4 turn(float degrees, const QVector3D& axis)
{
    QMatrix4x4 m;
    m.rotate(degrees, axis);
    return m;
}

// (a normal that is the same for n and -n: its largest part is positive)
QVector3D positive(QVector3D n)
{
    const float ax = std::fabs(n.x()), ay = std::fabs(n.y()), az = std::fabs(n.z());
    const float big = ax >= ay && ax >= az ? n.x() : (ay >= az ? n.y() : n.z());
    return big < 0 ? -n : n;
}

bool closeTo(const QVector3D& a, const QVector3D& b, float tol = 1e-4f)
{
    return (a - b).length() < tol;
}

}   // namespace

void Symmetry::frameFrom(const QVector3D& axis, const QVector3D& hint, QVector3D& a, QVector3D& c)
{
    c = axis.length() > 1e-9f ? axis.normalized() : QVector3D(0, 0, 1);
    QVector3D h = hint - c * QVector3D::dotProduct(hint, c);
    if (h.length() < 1e-3f)
    {
        // (the hint is along the axis: the world's axis that is the most across it)
        const QVector3D world[3] = {QVector3D(1, 0, 0), QVector3D(0, 1, 0), QVector3D(0, 0, 1)};
        int best = 0;
        for (int i = 1; i < 3; ++i)
            if (std::fabs(QVector3D::dotProduct(world[i], c)) < std::fabs(QVector3D::dotProduct(world[best], c))) best = i;
        h = world[best] - c * QVector3D::dotProduct(world[best], c);
    }
    a = h.normalized();
}

void Symmetry::normalise()
{
    frameFrom(c, a, a, c);
}

float Symmetry::angleOfFirst() const
{
    const QVector3D cc = c.normalized();
    QVector3D ref = std::fabs(cc.x()) > 0.95f ? QVector3D(0, 1, 0) : QVector3D(1, 0, 0);
    ref = (ref - cc * QVector3D::dotProduct(ref, cc)).normalized();
    const QVector3D aa = (a - cc * QVector3D::dotProduct(a, cc)).normalized();
    return std::atan2(QVector3D::dotProduct(QVector3D::crossProduct(ref, aa), cc), QVector3D::dotProduct(ref, aa)) * 180.0f / float(M_PI);
}

void Symmetry::setAngleOfFirst(float degrees)
{
    const QVector3D cc = c.normalized();
    QVector3D ref = std::fabs(cc.x()) > 0.95f ? QVector3D(0, 1, 0) : QVector3D(1, 0, 0);
    ref = (ref - cc * QVector3D::dotProduct(ref, cc)).normalized();
    a = turn(degrees, cc).mapVector(ref);
}

QList<QVector3D> Symmetry::planeNormals() const
{
    Symmetry s = *this;
    s.normalise();
    const QVector3D b = s.b();
    QList<QVector3D> out;
    if (kind == Turn) return out;                // (copies turned about the axis: no plane of it mirrors)
    if (kind == Cube)
    {
        const float r = std::sqrt(0.5f);
        out << s.a << b << s.c << (s.a + b) * r << (s.a - b) * r << (s.a + s.c) * r << (s.a - s.c) * r << (b + s.c) * r << (b - s.c) * r;
        return out;
    }
    const int n = std::max(1, std::min(8, count));
    for (int j = 0; j < n; ++j)
    {
        const float t = float(M_PI) * float(j) / float(n);
        out << QVector3D::crossProduct(s.c, s.a * std::cos(t) + b * std::sin(t)).normalized();
    }
    if (kind == AxisAndAcross) out << s.c;
    return out;
}

SymmetryElement Symmetry::element(const QMatrix4x4& L) const
{
    SymmetryElement e;
    QMatrix4x4 R = L;
    if (det3(L) < 0)
    {
        // A mirror image: the direction it turns round (L n = -n) is the normal of the mirror, and what is left when that is taken off is a turn about the same line
        e.improper = true;
        const QVector3D r[3] = {QVector3D(L(0, 0) + 1, L(0, 1), L(0, 2)), QVector3D(L(1, 0), L(1, 1) + 1, L(1, 2)), QVector3D(L(2, 0), L(2, 1), L(2, 2) + 1)};
        QVector3D best;
        for (const auto& pair : {std::make_pair(0, 1), std::make_pair(0, 2), std::make_pair(1, 2)})
        {
            const QVector3D x = QVector3D::crossProduct(r[pair.first], r[pair.second]);
            if (x.length() > best.length()) best = x;
        }
        const QVector3D n = best.length() > 1e-4f ? positive(best.normalized()) : c.normalized();      // (turned about nothing, as the point mirror: any line)
        e.mirror = n;
        R = L * reflection(n);
        e.axis = n;
        const float trace = R(0, 0) + R(1, 1) + R(2, 2);
        const QVector3D v(R(2, 1) - R(1, 2), R(0, 2) - R(2, 0), R(1, 0) - R(0, 1));
        const float cosine = std::max(-1.0f, std::min(1.0f, (trace - 1.0f) / 2.0f));
        float degrees = std::atan2(QVector3D::dotProduct(n, v) / 2.0f, cosine) * 180.0f / float(M_PI);
        if (std::fabs(degrees) < 1e-3f) degrees = 0;
        e.degrees = degrees;
    }
    else
    {
        const float trace = R(0, 0) + R(1, 1) + R(2, 2);
        const QVector3D v(R(2, 1) - R(1, 2), R(0, 2) - R(2, 0), R(1, 0) - R(0, 1));
        const float cosine = std::max(-1.0f, std::min(1.0f, (trace - 1.0f) / 2.0f));
        if (v.length() > 1e-4f)
        {
            e.axis = v.normalized();
            e.degrees = std::atan2(v.length() / 2.0f, cosine) * 180.0f / float(M_PI);
        }
        else if (cosine < 0)
        {
            // (half a turn: the axis is the column of (R + I) / 2 that is the longest)
            QVector3D best;
            for (int j = 0; j < 3; ++j)
            {
                const QVector3D col((R(0, j) + (j == 0 ? 1 : 0)) / 2.0f, (R(1, j) + (j == 1 ? 1 : 0)) / 2.0f, (R(2, j) + (j == 2 ? 1 : 0)) / 2.0f);
                if (col.length() > best.length()) best = col;
            }
            e.axis = positive(best.normalized());
            e.degrees = 180.0f;
        }
    }
    QMatrix4x4 full;
    full.translate(centre);
    full *= L;
    full.translate(-centre);
    e.map = full;
    return e;
}

QList<SymmetryElement> Symmetry::elements() const
{
    Symmetry s = *this;
    s.normalise();
    const QVector3D a = s.a, b = s.b(), c = s.c;
    QList<QMatrix4x4> linear;
    if (kind == Cube)
    {
        // Every turn and mirror image that takes the cube to itself: the signed permutations of its three axes
        const QMatrix4x4 B(a.x(), b.x(), c.x(), 0, a.y(), b.y(), c.y(), 0, a.z(), b.z(), c.z(), 0, 0, 0, 0, 1);
        const QMatrix4x4 Bt = B.transposed();
        const int perms[6][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
        for (const auto& p : perms)
        {
            for (int signs = 0; signs < 8; ++signs)
            {
                if (p[0] == 0 && p[1] == 1 && p[2] == 2 && signs == 0) continue;
                float m[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
                for (int i = 0; i < 3; ++i) m[i * 4 + p[i]] = (signs >> i) & 1 ? -1.0f : 1.0f;
                linear << B * QMatrix4x4(m, 4, 4) * Bt;
            }
        }
    }
    else if (kind == Turn)
    {
        const int n = std::max(1, std::min(24, count));
        for (int k = 1; k < n; ++k) linear << turn(360.0f * float(k) / float(n), c);
    }
    else
    {
        const int n = std::max(1, std::min(8, count));
        QList<QMatrix4x4> turns, mirrors;
        for (int k = 1; k < n; ++k) turns << turn(360.0f * float(k) / float(n), c);
        for (int j = 0; j < n; ++j)
        {
            const float t = float(M_PI) * float(j) / float(n);
            mirrors << reflection(QVector3D::crossProduct(c, a * std::cos(t) + b * std::sin(t)));
        }
        linear << turns << mirrors;
        if (kind == AxisAndAcross)
        {
            const QMatrix4x4 H = reflection(c);
            linear << H;
            for (const auto& t : turns) linear << H * t;
            for (const auto& m : mirrors) linear << H * m;
        }
    }
    QList<SymmetryElement> out;
    for (const auto& m : linear) out << element(m);
    return out;
}

bool Symmetry::onPlane(const QVector3D& p, float tol) const
{
    for (const QVector3D& n : planeNormals())
        if (std::fabs(QVector3D::dotProduct(p - centre, n)) < tol) return true;
    return false;
}

bool Symmetry::operator==(const Symmetry& o) const
{
    return on == o.on && kind == o.kind && count == o.count && closeTo(centre, o.centre, 1e-5f) && closeTo(a, o.a, 1e-5f) && closeTo(c, o.c, 1e-5f);
}

// ---- The card

SymmetryPanel::SymmetryPanel(QWidget* parent)
    : QFrame(parent), m_header(new QToolButton), m_close(new QToolButton), m_body(new QWidget), m_angle(new QDoubleSpinBox), m_info(new QLabel), m_use(new QToolButton)
{
    setObjectName("SymmetryPanel");
    setAttribute(Qt::WA_StyledBackground, true);
    // (the look of the work plane's card)
    setStyleSheet(
        "#SymmetryPanel { background: rgba(22, 76, 94, 222);"
        "  border: 1px solid rgba(147, 161, 161, 90); border-radius: 6px; }"
        "QLabel { color: #93a1a1; font-size: 8pt; }"
        "QLabel#SymmetryInfo { color: #eee8d5; }"
        "QToolButton { color: #eee8d5; background: rgba(147, 161, 161, 28);"
        "  border: 1px solid rgba(147, 161, 161, 60); border-radius: 4px;"
        "  padding: 2px 6px; font-size: 8pt; }"
        "QToolButton:hover { background: rgba(147, 161, 161, 60); }"
        "QToolButton:checked { background: rgba(211, 54, 130, 170);"
        "  border-color: rgba(211, 54, 130, 230); color: white; }"
        "QToolButton:disabled { color: rgba(147, 161, 161, 120); }"
        "QToolButton#SymmetryHeader { background: transparent; border: none; font-weight: bold;"
        "  font-size: 9pt; padding: 3px 4px; }"
        "QToolButton#SymmetryClose { background: transparent; border: none; color: #93a1a1;"
        "  padding: 2px 5px; font-size: 9pt; }"
        "QToolButton#SymmetryClose:hover { color: #eee8d5; }"
        "QDoubleSpinBox, QSpinBox { color: #eee8d5; background: rgba(0, 0, 0, 70);"
        "  border: 1px solid rgba(147, 161, 161, 70); border-radius: 3px; padding: 1px 2px;"
        "  font-size: 8pt; selection-background-color: rgba(38, 139, 210, 200); }");

    m_header->setObjectName("SymmetryHeader");
    m_header->setText(QString(QChar(0x25be)) + "  " + T("Symmetry"));
    m_header->setCursor(Qt::PointingHandCursor);
    m_header->setToolTip(T("Collapse / expand"));
    m_close->setObjectName("SymmetryClose");
    m_close->setText(QString(QChar(0x2715)));
    m_close->setToolTip(T("Stop using the symmetry (Shift+S)"));
    auto head = new QHBoxLayout;
    head->setContentsMargins(0, 0, 0, 0);
    head->setSpacing(4);
    head->addWidget(m_header);
    head->addStretch();
    head->addWidget(m_close);

    auto label = [](const QString& t) {
        auto l = new QLabel(t);
        l->setFixedWidth(52);
        return l;
    };
    auto button = [](const QString& text, const QString& tip, const QString& name, bool checkable) {
        auto b = new QToolButton;
        b->setText(text);
        b->setToolTip(tip);
        b->setObjectName(name);
        b->setCheckable(checkable);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        return b;
    };

    // How the planes are arranged: round an axis, round an axis and across it, or the planes of a cube
    auto kindRow = new QHBoxLayout;
    kindRow->setSpacing(3);
    const QString kindNames[4] = {T("Axis"), T("Axis + across"), T("Cube"), T("Turns")};
    const QString kindTips[4] = {
        T("Planes that all contain one axis, equally spaced: one plane is a mirror; with more, what you make is mirrored across each and turned about the axis"),
        T("The planes round the axis, and the plane across the axis: a part is mirrored top to bottom as well"),
        T("The nine planes of a cube: three across its axes, six through its edges -- everything that takes a cube to itself"),
        T("No planes: copies of what you make, equally spaced round the axis, each turned from the one before (3 copies: 120 degrees apart; any number up to 24) -- nothing is mirrored")};
    for (int i = 0; i < 4; ++i)
    {
        m_kind[i] = button(kindNames[i], kindTips[i], QString("symmetryKind%1").arg(i), true);
        kindRow->addWidget(m_kind[i], 1);
        connect(m_kind[i], &QToolButton::clicked, this, [this, i] {
            if (!m_updating) emit(kindChanged(i));
        });
    }
    auto countRow = new QHBoxLayout;
    countRow->setSpacing(3);
    const int counts[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    for (int i = 0; i < 8; ++i)
    {
        m_count[i] = button(QString::number(counts[i]), counts[i] == 1 ? T("One plane: a mirror") : T("%1 planes through the axis, %2 degrees apart").arg(counts[i]).arg(180.0 / counts[i]),
                            QString("symmetryCount%1").arg(counts[i]), true);
        countRow->addWidget(m_count[i], 1);
        connect(m_count[i], &QToolButton::clicked, this, [this, i, counts] {
            if (!m_updating) emit(countChanged(counts[i]));
        });
    }
    m_copies = new QSpinBox;
    m_copies->setObjectName("symmetryCopies");
    m_copies->setRange(1, 24);
    m_copies->setKeyboardTracking(false);
    m_copies->setFixedWidth(46);
    m_copies->setToolTip(T("How many copies there are round the axis (1 to 24)"));
    countRow->addWidget(m_copies);
    connect(m_copies, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
        if (!m_updating) emit(countChanged(v));
    });

    // Which way the axis goes: along a world axis, along the work plane's normal, up in the view, or across the plane of the 2D shape that is selected
    auto axisRow = new QHBoxLayout;
    axisRow->setSpacing(3);
    const QString axisNames[6] = {"X", "Y", "Z", T("Plane"), T("View"), T("Face")};
    const QString axisTips[6] = {
        T("The axis along x"), T("The axis along y"), T("The axis along z"),
        T("The axis along the work plane's normal: the planes stand on the work plane"),
        T("The axis up in the view: the first plane splits what you see into left and right"),
        T("The axis along the normal of the selected 2D shape or surface: its plane is across the axis")};
    for (int i = 0; i < 6; ++i)
    {
        auto b = button(axisNames[i], axisTips[i], QString("symmetryAxis%1").arg(i), false);
        axisRow->addWidget(b, i < 3 ? 1 : 2);
        connect(b, &QToolButton::clicked, this, [this, i] { emit(axisRequested(i)); });
    }

    m_angle->setObjectName("symmetryAngle");
    m_angle->setDecimals(1);
    m_angle->setRange(-180, 180);
    m_angle->setWrapping(true);
    m_angle->setKeyboardTracking(false);
    m_angle->setButtonSymbols(QDoubleSpinBox::NoButtons);
    m_angle->setFixedWidth(64);
    m_angle->setSuffix(QString(QChar(0x00b0)));
    m_angle->setToolTip(T("How far the first plane is turned about the axis"));
    auto angleRow = new QHBoxLayout;
    m_angleLabel = label(T("Turn"));
    angleRow->addWidget(m_angleLabel);
    angleRow->addWidget(m_angle);
    angleRow->addStretch();
    connect(m_angle, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v) {
        if (!m_updating) emit(angleChanged(float(v)));
    });

    auto centreRow = new QHBoxLayout;
    centreRow->setSpacing(3);
    centreRow->addWidget(label(T("Centre")));
    const char* axes[3] = {"x", "y", "z"};
    for (int i = 0; i < 3; ++i)
    {
        m_centre[i] = new QDoubleSpinBox;
        m_centre[i]->setObjectName(QString("symmetryCentre%1").arg(axes[i]));
        m_centre[i]->setDecimals(3);
        m_centre[i]->setRange(-1e6, 1e6);
        m_centre[i]->setKeyboardTracking(false);
        m_centre[i]->setButtonSymbols(QDoubleSpinBox::NoButtons);
        m_centre[i]->setPrefix(QString(axes[i]) + " ");
        m_centre[i]->setToolTip(T("Where the planes cross"));
        centreRow->addWidget(m_centre[i], 1);
        connect(m_centre[i], QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) {
            if (!m_updating) emit(centreChanged(QVector3D(float(m_centre[0]->value()), float(m_centre[1]->value()), float(m_centre[2]->value()))));
        });
    }
    auto centreButtons = new QHBoxLayout;
    centreButtons->setSpacing(3);
    const QString centreNames[3] = {T("Origin"), T("Selection"), T("Work plane")};
    const QString centreTips[3] = {T("The centre at the origin"), T("The centre at the middle of what is selected"), T("The centre at the work plane's origin")};
    centreButtons->addSpacing(55);
    for (int i = 0; i < 3; ++i)
    {
        auto b = button(centreNames[i], centreTips[i], QString("symmetryCentreFrom%1").arg(i), false);
        centreButtons->addWidget(b, 1);
        connect(b, &QToolButton::clicked, this, [this, i] { emit(centreRequested(i)); });
    }

    m_use->setObjectName("symmetryUse");
    m_use->setCheckable(true);
    m_use->setChecked(true);
    m_use->setText(T("In use"));
    m_use->setToolTip(T("Everything you make is made at the images of this symmetry too (Shift+S)"));
    m_use->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(m_use, &QToolButton::toggled, this, [this](bool on) {
        if (!m_updating) emit(useChanged(on));
    });
    auto followRow = new QHBoxLayout;
    followRow->addSpacing(55);
    followRow->addWidget(m_use, 1);

    m_info->setObjectName("SymmetryInfo");
    m_info->setWordWrap(true);
    m_info->setMinimumHeight(16);

    auto body = new QVBoxLayout(m_body);
    body->setContentsMargins(6, 2, 6, 4);
    body->setSpacing(5);
    auto row = [&](const QString& name, QLayout* l) {
        auto line = new QHBoxLayout;
        line->setSpacing(3);
        auto title = label(name);
        line->addWidget(title);
        line->addLayout(l, 1);
        body->addLayout(line);
        return title;
    };
    row(T("Planes"), kindRow);
    m_countLabel = row(T("Count"), countRow);
    row(T("Axis"), axisRow);
    body->addLayout(angleRow);
    body->addLayout(centreRow);
    body->addLayout(centreButtons);
    body->addLayout(followRow);
    body->addWidget(m_info);

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(7, 4, 7, 7);
    layout->setSpacing(2);
    layout->addLayout(head);
    layout->addWidget(m_body);
    setMinimumWidth(370);
    resize(380, 100);

    new CardController(this, "symmetry", m_header, QSize(300, 60));
    connect(m_header, &QToolButton::clicked, this, [this] {
        m_collapsed = !m_collapsed;
        m_body->setVisible(!m_collapsed);
        m_header->setText(QString(QChar(m_collapsed ? 0x25b8 : 0x25be)) + m_header->text().mid(1));
        CardController::collapse(this, m_collapsed, m_header->sizeHint().height() + 14);
        place();
    });
    connect(m_close, &QToolButton::clicked, this, &SymmetryPanel::closeRequested);
    if (parent) parent->installEventFilter(this);
}

void SymmetryPanel::setSymmetry(const Symmetry& s, int images, bool exists, bool inUse, const QString& name)
{
    setVisible(exists);
    if (!exists) return;
    raise();
    m_header->setText(QString(QChar(m_collapsed ? 0x25b8 : 0x25be)) + "  " + T("Symmetry") + (name.isEmpty() ? QString() : ": " + name));
    m_updating = true;
    {
        QSignalBlocker a(m_angle), b(m_centre[0]), c(m_centre[1]), d(m_centre[2]), e(m_use);
        m_use->setChecked(inUse);
        QSignalBlocker f(m_copies);
        for (int i = 0; i < 4; ++i) m_kind[i]->setChecked(s.kind == i);
        const bool turn = s.kind == Symmetry::Turn;
        const int counts[8] = {1, 2, 3, 4, 5, 6, 7, 8};
        for (int i = 0; i < 8; ++i)
        {
            m_count[i]->setChecked(s.kind != Symmetry::Cube && s.count == counts[i]);
            m_count[i]->setEnabled(s.kind != Symmetry::Cube);
            m_count[i]->setToolTip(turn ? (counts[i] == 1 ? T("One copy: nothing is made twice") : T("%1 copies, %2 degrees apart").arg(counts[i]).arg(360.0 / counts[i]))
                                        : (counts[i] == 1 ? T("One plane: a mirror") : T("%1 planes through the axis, %2 degrees apart").arg(counts[i]).arg(180.0 / counts[i])));
        }
        m_copies->setVisible(turn);
        m_copies->setValue(std::max(1, std::min(24, s.count)));
        m_countLabel->setText(turn ? T("Copies") : T("Count"));
        m_angleLabel->setVisible(!turn);                 // (copies turned about the axis do not care where a first plane is)
        m_angle->setVisible(!turn);
        m_angle->setValue(double(s.angleOfFirst()));
        for (int i = 0; i < 3; ++i) m_centre[i]->setValue(double(s.centre[i]));
    }
    m_updating = false;
    auto number = [](float x) { return QString::number(std::fabs(x) < 5e-5f ? 0.0 : double(x), 'g', 4); };
    m_info->setText((s.kind == Symmetry::Turn ? T("Copies: %1. Images of what you make: %2. Axis (%3, %4, %5)") : T("Planes: %1. Images of what you make: %2. Axis (%3, %4, %5)"))
                        .arg(s.kind == Symmetry::Turn ? s.count : s.planeNormals().size()).arg(images)
                        .arg(number(s.c.x()), number(s.c.y()), number(s.c.z())) + (inUse ? QString() : "  " + T("(not in use)")));
}

bool SymmetryPanel::eventFilter(QObject* obj, QEvent* e)
{
    if (obj == parentWidget() && e->type() == QEvent::Resize) place();
    return QFrame::eventFilter(obj, e);
}

void SymmetryPanel::showEvent(QShowEvent* e)
{
    QFrame::showEvent(e);
    place();
}

void SymmetryPanel::place()
{
    if (!CardController::sized(this)) resize(330, std::max(minimumSizeHint().height(), sizeHint().height()));
    if (parentWidget() && !CardController::moved(this))
    {
        // Top-right, below the orientation triad -- and below the section card and the work plane's, when they are there too
        int top = 124;
        for (const char* name : {"SectionPanel", "WorkPlanePanel"})
        {
            if (auto other = parentWidget()->findChild<QWidget*>(name))
                if (other->isVisible()) top = std::max(top, other->geometry().bottom() + 8);
        }
        move(parentWidget()->width() - width() - 8, top);
    }
}

}   // namespace FielDes
