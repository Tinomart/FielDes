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
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include "fieldes/carddrag.hpp"
#include "fieldes/i18n.hpp"
#include "fieldes/units.hpp"
#include "fieldes/workplane.hpp"

namespace FielDes {

namespace {

// The icon of a kind of drawing: what it makes, with its points drawn as the viewport draws them -- a shape of straight edges with square corners (line), a smooth
// curve through round points with a ring (spline), one dot (point)
QIcon drawingIcon(int kind)
{
    const int w = 30, h = 18;
    QPixmap pm(2 * w, 2 * h);
    pm.setDevicePixelRatio(2);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor ink(238, 232, 213);
    p.setPen(QPen(ink, 1.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    if (kind == 1)
    {
        const QPointF a[4] = {{5, 14}, {8, 4}, {24, 4}, {26, 14}};
        QPolygonF poly;
        for (const QPointF& q : a) poly << q;
        p.drawPolygon(poly);
        p.setBrush(ink);
        for (const QPointF& q : a) p.drawRect(QRectF(q.x() - 1.9, q.y() - 1.9, 3.8, 3.8));
    }
    else if (kind == 2)
    {
        // (a closed curve through four points: Catmull-Rom, as cubic pieces)
        const QPointF a[4] = {{5, 11}, {13, 3.5}, {25, 6}, {21, 14.5}};
        QPainterPath path;
        path.moveTo(a[0]);
        for (int i = 0; i < 4; ++i)
        {
            const QPointF p0 = a[(i + 3) % 4], p1 = a[i], p2 = a[(i + 1) % 4], p3 = a[(i + 2) % 4];
            path.cubicTo(p1 + (p2 - p0) / 6.0, p2 - (p3 - p1) / 6.0, p2);
        }
        p.drawPath(path);
        for (const QPointF& q : a)
        {
            p.setBrush(ink);
            p.drawEllipse(q, 1.7, 1.7);
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(QColor(238, 232, 213, 170), 0.8));
            p.drawEllipse(q, 3.3, 3.3);
            p.setPen(QPen(ink, 1.3));
        }
    }
    else
    {
        p.setBrush(ink);
        p.drawEllipse(QPointF(w / 2.0, h / 2.0), 3.4, 3.4);
    }
    return QIcon(pm);
}

}   // namespace

WorkPlanePanel::WorkPlanePanel(QWidget* parent)
    : QFrame(parent),
      m_header(new QToolButton), m_close(new QToolButton), m_body(new QWidget),
      m_grid(new QToolButton), m_snap(new QToolButton), m_snapPoints(new QToolButton), m_auto(new QToolButton),
      m_step(new Units::LengthSpinBox), m_offset(new Units::LengthSpinBox), m_info(new QLabel), m_readout(new QLabel)
{
    setObjectName("WorkPlanePanel");
    setAttribute(Qt::WA_StyledBackground, true);
    // (the look of the section card)
    setStyleSheet(
        "#WorkPlanePanel { background: rgba(22, 76, 94, 222);"
        "  border: 1px solid rgba(147, 161, 161, 90); border-radius: 6px; }"
        "QLabel { color: #93a1a1; font-size: 8pt; }"
        "QLabel#WorkPlaneInfo { color: #eee8d5; }"
        "QLabel#WorkPlaneReadout { color: #eee8d5; font-size: 8.5pt; }"
        "QToolButton { color: #eee8d5; background: rgba(147, 161, 161, 28);"
        "  border: 1px solid rgba(147, 161, 161, 60); border-radius: 4px;"
        "  padding: 2px 8px; font-size: 8pt; }"
        "QToolButton:hover { background: rgba(147, 161, 161, 60); }"
        "QToolButton:checked { background: rgba(38, 139, 210, 170);"
        "  border-color: rgba(38, 139, 210, 230); color: white; }"
        "QToolButton:disabled { color: rgba(147, 161, 161, 120); }"
        "QToolButton#WorkPlaneHeader { background: transparent; border: none; font-weight: bold;"
        "  font-size: 9pt; padding: 3px 4px; }"
        "QToolButton#WorkPlaneClose { background: transparent; border: none; color: #93a1a1;"
        "  padding: 2px 5px; font-size: 9pt; }"
        "QToolButton#WorkPlaneClose:hover { color: #eee8d5; }"
        "QDoubleSpinBox { color: #eee8d5; background: rgba(0, 0, 0, 70);"
        "  border: 1px solid rgba(147, 161, 161, 70); border-radius: 3px; padding: 1px 2px;"
        "  font-size: 8pt; selection-background-color: rgba(38, 139, 210, 200); }"
        "QDoubleSpinBox:disabled { color: rgba(147, 161, 161, 140); }");

    m_header->setObjectName("WorkPlaneHeader");
    m_header->setText(QString(QChar(0x25be)) + "  " + T("Work plane"));
    m_header->setCursor(Qt::PointingHandCursor);
    m_header->setToolTip(T("Collapse / expand"));
    m_close->setObjectName("WorkPlaneClose");
    m_close->setText(QString(QChar(0x2715)));
    m_close->setToolTip(T("Close the work plane view (Shift+W)"));
    auto head = new QHBoxLayout;
    head->setContentsMargins(0, 0, 0, 0);
    head->setSpacing(4);
    head->addWidget(m_header);
    head->addStretch();
    head->addWidget(m_close);

    auto label = [](const QString& t) {
        auto l = new QLabel(t);
        l->setFixedWidth(60);          // (wide enough for "Symmetry")
        return l;
    };

    // What a click puts down: the points of a shape of straight edges, of a spline, or a point.  There is no button for not drawing: with none of them on, a
    // click selects, moves, turns and scales things as usual (the active kind is turned off by clicking it again, or with a right click in the viewport)
    auto modeRow = new QHBoxLayout;
    modeRow->setSpacing(3);
    const QString names[3] = {T("Line"), T("Spline"), T("Point")};
    const QString tips[3] = {
        T("Click to put down the corners of a shape with straight edges; a click on the first one closes it, Enter ends it open (E: the next kind, right click: stop drawing)"),
        T("Click to put down the points of a smooth curve; a click on the first one closes it into a shape, Enter ends it open (E: the next kind, right click: stop drawing)"),
        T("Every click puts a point on the plane (E: the next kind, right click: stop drawing)")};
    for (int i = 0; i < 3; ++i)
    {
        m_modes[i] = new QToolButton;
        m_modes[i]->setText(names[i]);
        m_modes[i]->setIcon(drawingIcon(i + 1));
        m_modes[i]->setIconSize(QSize(30, 18));
        m_modes[i]->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        m_modes[i]->setCheckable(true);
        m_modes[i]->setObjectName(QString("workPlaneMode%1").arg(i + 1));
        m_modes[i]->setToolTip(tips[i]);
        m_modes[i]->setStyleSheet("padding: 3px 3px;");
        m_modes[i]->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        modeRow->addWidget(m_modes[i], 1);
        // (the button that is on and is clicked goes off: not drawing)
        connect(m_modes[i], &QToolButton::clicked, this, [this, i](bool on) {
            if (!m_updating) emit(modeChanged(on ? i + 1 : 0));
        });
    }
    m_hint = new QLabel;
    m_hint->setObjectName("WorkPlaneHint");
    m_hint->setWordWrap(true);
    m_hint->setFixedHeight(40);                 // (three lines: the card does not change its size when the text does)
    m_hint->setAlignment(Qt::AlignLeft | Qt::AlignTop);

    // The grid and what snaps
    m_grid->setObjectName("workPlaneGrid");
    m_grid->setCheckable(true);
    m_grid->setChecked(true);
    m_grid->setText(T("Grid"));
    m_grid->setToolTip(T("Draw the grid on the plane"));
    m_snap->setObjectName("workPlaneSnap");
    m_snap->setCheckable(true);
    m_snap->setChecked(true);
    m_snap->setText(T("Snap to grid"));
    m_snap->setToolTip(T("New points and the points you drag in the plane land on the grid"));
    m_snapPoints->setObjectName("workPlaneSnapPoints");
    m_snapPoints->setCheckable(true);
    m_snapPoints->setChecked(true);
    m_snapPoints->setText(T("Snap to points"));
    m_snapPoints->setToolTip(T("New points and the points you drag land on the points that are already there"));
    auto showRow = new QHBoxLayout;
    showRow->addWidget(label(T("Show")));
    showRow->addWidget(m_grid);
    showRow->addStretch();
    auto snapRow = new QHBoxLayout;
    snapRow->addWidget(label(T("Snap")));
    snapRow->addWidget(m_snap);
    snapRow->addWidget(m_snapPoints);
    snapRow->addStretch();

    // The step of the grid, and where the plane is along its normal
    m_auto->setObjectName("workPlaneAuto");
    m_auto->setCheckable(true);
    m_auto->setChecked(true);
    m_auto->setText(T("Auto"));
    m_auto->setToolTip(T("The grid's step from how large the picture is"));
    m_step->setLengthDecimals(4);
    m_step->setRange(1e-4, 1e6);
    m_step->setValue(1);
    m_step->setKeyboardTracking(false);
    m_step->setButtonSymbols(QDoubleSpinBox::NoButtons);
    m_step->setFixedWidth(74);
    m_step->setEnabled(false);
    m_step->setToolTip(T("The step of the grid"));
    auto stepRow = new QHBoxLayout;
    stepRow->addWidget(label(T("Grid step")));
    stepRow->addWidget(m_auto);
    stepRow->addWidget(m_step, 1);
    m_offset->setObjectName("workPlaneOffset");
    m_offset->setLengthDecimals(3);
    m_offset->setRange(-1e6, 1e6);
    m_offset->setKeyboardTracking(false);
    m_offset->setButtonSymbols(QDoubleSpinBox::NoButtons);
    m_offset->setFixedWidth(74);
    m_offset->setToolTip(T("How far the plane is moved along its normal"));
    auto offsetRow = new QHBoxLayout;
    offsetRow->addWidget(label(T("Offset")));
    offsetRow->addWidget(m_offset);
    offsetRow->addStretch();

    m_info->setObjectName("WorkPlaneInfo");
    m_info->setWordWrap(true);
    m_info->setMinimumHeight(16);
    m_readout->setObjectName("WorkPlaneReadout");
    m_readout->setMinimumHeight(16);

    // What to do with the plane
    auto face = new QToolButton;
    face->setObjectName("workPlaneFace");
    face->setText(T("Set view to work plane"));
    face->setToolTip(T("Turn the camera to look straight at the plane (W)"));
    auto fromView = new QToolButton;
    fromView->setObjectName("workPlaneFromView");
    fromView->setText(T("Set work plane to view"));
    fromView->setToolTip(T("Make the plane parallel to the view, through the origin"));
    auto fresh = new QToolButton;
    fresh->setObjectName("workPlaneNew");
    fresh->setText(T("New work plane"));
    fresh->setToolTip(T("A new plane: the plane of the selected 2D shape or surface, or parallel to the view (Ctrl+Shift+W)"));
    auto actions = new QHBoxLayout;
    actions->addWidget(face);
    actions->addWidget(fromView);
    auto actions2 = new QHBoxLayout;
    actions2->addWidget(fresh);
    actions2->addStretch();

    auto body = new QVBoxLayout(m_body);
    body->setContentsMargins(6, 2, 6, 4);
    body->setSpacing(6);
    body->addLayout(modeRow);
    body->addWidget(m_hint);
    body->addLayout(showRow);
    body->addLayout(snapRow);
    body->addLayout(stepRow);
    body->addLayout(offsetRow);
    body->addWidget(m_info);
    body->addWidget(m_readout);
    body->addLayout(actions);
    body->addLayout(actions2);

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(7, 4, 7, 7);            // (the margin is where a card is resized from)
    layout->setSpacing(2);
    layout->addLayout(head);
    layout->addWidget(m_body);
    setMinimumWidth(300);
    resize(300, 100);

    new CardController(this, "workplane", m_header, QSize(280, 60));
    connect(m_header, &QToolButton::clicked, this, [this] {
        m_collapsed = !m_collapsed;
        m_body->setVisible(!m_collapsed);
        m_header->setText(QString(QChar(m_collapsed ? 0x25b8 : 0x25be)) + "  " + T("Work plane"));
        CardController::collapse(this, m_collapsed, m_header->sizeHint().height() + 14);
        place();
    });
    connect(m_close, &QToolButton::clicked, this, &WorkPlanePanel::closeRequested);
    connect(m_grid, &QToolButton::toggled, this, [this] { emitOptions(); });
    connect(m_snap, &QToolButton::toggled, this, [this] { emitOptions(); });
    connect(m_snapPoints, &QToolButton::toggled, this, [this] { emitOptions(); });
    connect(m_auto, &QToolButton::toggled, this, [this](bool on) {
        m_step->setEnabled(!on);
        emitOptions();
    });
    connect(m_step, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { emitOptions(); });
    connect(m_offset, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v) {
        if (!m_updating) emit(offsetChanged(float(v)));
    });
    connect(face, &QToolButton::clicked, this, &WorkPlanePanel::faceRequested);
    connect(fromView, &QToolButton::clicked, this, &WorkPlanePanel::fromViewRequested);
    connect(fresh, &QToolButton::clicked, this, &WorkPlanePanel::newRequested);

    if (parent) parent->installEventFilter(this);
}

void WorkPlanePanel::emitOptions()
{
    if (m_updating) return;
    emit(optionsChanged(m_grid->isChecked(), m_snap->isChecked(), m_snapPoints->isChecked(),
                        m_auto->isChecked() ? 0.0f : float(m_step->value())));
}

void WorkPlanePanel::setPlane(const WorkPlane& plane, float step)
{
    m_updating = true;
    {
        QSignalBlocker a(m_grid), b(m_snap), c(m_snapPoints), d(m_auto), e(m_step), f(m_offset);
        for (int i = 0; i < 3; ++i) m_modes[i]->setChecked(plane.mode == i + 1);
        m_grid->setChecked(plane.grid);
        m_snap->setChecked(plane.snap);
        m_snapPoints->setChecked(plane.snapPoints);
        m_auto->setChecked(plane.spacing <= 0);
        m_step->setEnabled(plane.spacing > 0);
        m_step->setValue(plane.spacing > 0 ? plane.spacing : std::max(1e-4f, step));
        m_offset->setValue(plane.offset);
    }
    m_updating = false;
    // What a click does now: dim when nothing is being drawn (it selects), lit when it puts a point down
    const QString hints[4] = {
        T("Not drawing: clicks select, move, turn and scale things. Pick a kind to draw (E)."),
        T("Click the corners of a shape; click the first one to close it, or press Enter to end it open."),
        T("Click the points of a spline; click the first one to close it, or press Enter to end it open."),
        T("Every click puts a point on the plane.")};
    const int mode = std::max(0, std::min(3, plane.mode));
    m_hint->setText(hints[mode]);
    m_hint->setStyleSheet(mode == 0 ? "color: #93a1a1;" : "color: #eee8d5;");
    const QVector3D n = plane.normal(), o = plane.origin();
    auto number = [](float x) { return QString::number(std::fabs(x) < 5e-5f ? 0.0 : double(x), 'g', 4); };
    m_info->setText(T("Normal (%1, %2, %3), through (%4, %5, %6)")
                        .arg(number(n.x()), number(n.y()), number(n.z()), number(o.x()), number(o.y()), number(o.z())));
}

void WorkPlanePanel::refreshUnits()
{
    QSignalBlocker a(m_step), b(m_offset);
    m_step->refreshUnit();
    m_offset->refreshUnit();
}

void WorkPlanePanel::setReadout(const QString& text)
{
    m_readout->setText(text);
}

bool WorkPlanePanel::eventFilter(QObject* obj, QEvent* e)
{
    if (obj == parentWidget() && e->type() == QEvent::Resize) place();
    return QFrame::eventFilter(obj, e);
}

void WorkPlanePanel::showEvent(QShowEvent* e)
{
    QFrame::showEvent(e);
    place();
}

void WorkPlanePanel::place()
{
    if (!CardController::sized(this)) resize(300, std::max(minimumSizeHint().height(), sizeHint().height()));
    if (parentWidget() && !CardController::moved(this))
    {
        // Top-right, below the orientation triad -- and below the section card when that is there too
        int top = 124;
        if (auto section = parentWidget()->findChild<QWidget*>("SectionPanel"))
        {
            if (section->isVisible()) top = section->geometry().bottom() + 8;
        }
        move(parentWidget()->width() - width() - 8, top);
    }
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The card of the snapping mode

GridPanel::GridPanel(QWidget* parent)
    : QFrame(parent),
      m_header(new QToolButton), m_close(new QToolButton), m_body(new QWidget),
      m_show(new QToolButton), m_snapPoints(new QToolButton), m_auto(new QToolButton), m_half(new QToolButton), m_double(new QToolButton),
      m_step(new Units::LengthSpinBox), m_angle(new QDoubleSpinBox), m_distance(new QSpinBox), m_hint(new QLabel)
{
    setObjectName("GridPanel");
    setAttribute(Qt::WA_StyledBackground, true);
    // (the look of the work plane's card, with the amber of the snapping mode in its border and in what is on)
    setStyleSheet(
        "#GridPanel { background: rgba(22, 76, 94, 222);"
        "  border: 1px solid rgba(176, 130, 255, 150); border-radius: 6px; }"
        "QLabel { color: #93a1a1; font-size: 8pt; }"
        "QLabel#GridHint { color: #eee8d5; }"
        "QToolButton { color: #eee8d5; background: rgba(147, 161, 161, 28);"
        "  border: 1px solid rgba(147, 161, 161, 60); border-radius: 4px;"
        "  padding: 2px 8px; font-size: 8pt; }"
        "QToolButton:hover { background: rgba(147, 161, 161, 60); }"
        "QToolButton:checked { background: rgba(110, 70, 190, 200);"
        "  border-color: rgba(176, 130, 255, 230); color: white; }"
        "QToolButton:disabled { color: rgba(147, 161, 161, 120); }"
        "QToolButton#GridHeader { background: transparent; border: none; font-weight: bold;"
        "  font-size: 9pt; padding: 3px 4px; color: #b182ff; }"
        "QToolButton#GridClose { background: transparent; border: none; color: #93a1a1;"
        "  padding: 2px 5px; font-size: 9pt; }"
        "QToolButton#GridClose:hover { color: #eee8d5; }"
        "QDoubleSpinBox, QSpinBox { color: #eee8d5; background: rgba(0, 0, 0, 70);"
        "  border: 1px solid rgba(147, 161, 161, 70); border-radius: 3px; padding: 1px 2px;"
        "  font-size: 8pt; selection-background-color: rgba(110, 70, 190, 200); }"
        "QDoubleSpinBox:disabled, QSpinBox:disabled { color: rgba(147, 161, 161, 140); }");

    m_header->setObjectName("GridHeader");
    m_header->setText(QString(QChar(0x25be)) + "  " + T("Snapping"));
    m_header->setCursor(Qt::PointingHandCursor);
    m_header->setToolTip(T("Collapse / expand"));
    m_close->setObjectName("GridClose");
    m_close->setText(QString(QChar(0x2715)));
    m_close->setToolTip(T("Leave the snapping mode (Tab)"));
    auto head = new QHBoxLayout;
    head->setContentsMargins(0, 0, 0, 0);
    head->setSpacing(4);
    head->addWidget(m_header);
    head->addStretch();
    head->addWidget(m_close);

    auto label = [](const QString& t) {
        auto l = new QLabel(t);
        l->setFixedWidth(70);
        return l;
    };

    m_show->setObjectName("gridShow");
    m_show->setCheckable(true);
    m_show->setChecked(true);
    m_show->setText(T("Show grid"));
    m_show->setToolTip(T("Draw the grid: a lattice of lines in the whole space, faint with distance"));
    m_snapPoints->setObjectName("gridSnapPoints");
    m_snapPoints->setCheckable(true);
    m_snapPoints->setChecked(true);
    m_snapPoints->setText(T("Snap to points"));
    m_snapPoints->setToolTip(T("A model put down, pulled out or moved by a plane square lands on a point that is already there when it is near one, on the point and not on the grid"));
    // (the two switches side by side, each with its own name: a row of its own with a label over a single switch says nothing)
    auto showRow = new QHBoxLayout;
    showRow->addWidget(m_show);
    showRow->addWidget(m_snapPoints);
    showRow->addStretch();

    // The step of the grid: automatic, from how large the picture is, or a number -- halved and doubled with a click
    m_auto->setObjectName("gridAuto");
    m_auto->setCheckable(true);
    m_auto->setChecked(true);
    m_auto->setText(T("Auto"));
    m_auto->setToolTip(T("The grid's step from the size of the region that is drawn: a round number, the same however you zoom"));
    m_step->setObjectName("gridStep");
    m_step->setLengthDecimals(4);
    m_step->setRange(1e-4, 1e6);
    m_step->setValue(1);
    m_step->setKeyboardTracking(false);
    m_step->setButtonSymbols(QDoubleSpinBox::NoButtons);
    m_step->setFixedWidth(96);
    m_step->setEnabled(false);
    m_step->setToolTip(T("The step of the grid: everything that is moved goes to a multiple of it"));
    m_half->setObjectName("gridHalf");
    m_half->setText(QString::fromUtf8("\xc2\xbd"));
    m_half->setToolTip(T("Half the step"));
    m_double->setObjectName("gridDouble");
    m_double->setText(QString::fromUtf8("\xc3\x97" "2"));
    m_double->setToolTip(T("Twice the step"));
    auto stepRow = new QHBoxLayout;
    stepRow->setSpacing(4);
    stepRow->addWidget(label(T("Grid step")));
    stepRow->addWidget(m_auto);
    stepRow->addWidget(m_step, 1);
    stepRow->addWidget(m_half);
    stepRow->addWidget(m_double);

    // The step of a turn: every turn in the mode, and the turn of the Shift gizmo whether the mode is on or not
    m_angle->setObjectName("gridAngle");
    m_angle->setRange(0.1, 180.0);
    m_angle->setDecimals(1);
    m_angle->setSingleStep(5.0);
    m_angle->setSuffix(QString::fromUtf8("\xc2\xb0"));
    m_angle->setValue(5.0);
    m_angle->setKeyboardTracking(false);
    m_angle->setButtonSymbols(QDoubleSpinBox::NoButtons);
    m_angle->setFixedWidth(60);
    m_angle->setToolTip(T("The step of a turn: every turn goes to a multiple of it -- the turn of the Shift gizmo too, in the mode or not"));
    auto angleRow = new QHBoxLayout;
    angleRow->setSpacing(4);
    angleRow->addWidget(label(T("Angle step")));
    angleRow->addWidget(m_angle);
    angleRow->addStretch();
    auto presetRow = new QHBoxLayout;               // (the steps people use, under the box: a row of its own, so that nothing is squeezed)
    presetRow->setSpacing(4);
    presetRow->addSpacing(74);
    for (const int degrees : {1, 5, 15, 45, 90})
    {
        auto b = new QToolButton;
        b->setText(QString::number(degrees) + QString::fromUtf8("\xc2\xb0"));
        b->setToolTip(T("Turn in steps of %1 degrees").arg(degrees));
        b->setStyleSheet("padding: 2px 6px;");
        connect(b, &QToolButton::clicked, this, [this, degrees] { m_angle->setValue(degrees); });
        presetRow->addWidget(b);
    }
    presetRow->addStretch();

    // The viewing distance of the lattice: how far from the viewer it can be seen -- a share of the distance to the place the view looks at, so that it is the same at every zoom and every pan
    m_distance->setObjectName("gridDistance");
    m_distance->setRange(105, 400);
    m_distance->setSingleStep(5);
    m_distance->setValue(135);
    m_distance->setSuffix(" %");
    m_distance->setKeyboardTracking(false);
    m_distance->setButtonSymbols(QSpinBox::NoButtons);
    m_distance->setFixedWidth(70);
    m_distance->setToolTip(T("The viewing distance of the grid: how far from you it can be seen, in per cent of the distance from you to the place you look at. It fades out towards that distance and nothing past it is drawn -- the same at every zoom and every pan"));
    auto distanceRow = new QHBoxLayout;
    distanceRow->addWidget(label(T("Distance")));
    distanceRow->addWidget(m_distance);
    distanceRow->addStretch();

    m_hint->setObjectName("GridHint");
    m_hint->setWordWrap(true);
    m_hint->setFixedHeight(70);
    m_hint->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_hint->setText(T("Everything you move snaps to the grid, and every turn to the angle step. Hold Alt to move freely. Ctrl and the scroll wheel move the grid toward or away from you. Tab leaves the mode."));

    auto body = new QVBoxLayout(m_body);
    body->setContentsMargins(6, 2, 6, 4);
    body->setSpacing(6);
    body->addLayout(showRow);
    body->addLayout(stepRow);
    body->addLayout(angleRow);
    body->addLayout(presetRow);
    body->addLayout(distanceRow);
    body->addWidget(m_hint);

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(7, 4, 7, 7);            // (the margin is where a card is resized from)
    layout->setSpacing(2);
    layout->addLayout(head);
    layout->addWidget(m_body);
    setMinimumWidth(340);
    resize(340, 100);

    new CardController(this, "snapping", m_header, QSize(320, 60));
    connect(m_header, &QToolButton::clicked, this, [this] {
        m_collapsed = !m_collapsed;
        m_body->setVisible(!m_collapsed);
        m_header->setText(QString(QChar(m_collapsed ? 0x25b8 : 0x25be)) + "  " + T("Snapping"));
        CardController::collapse(this, m_collapsed, m_header->sizeHint().height() + 14);
        place();
    });
    connect(m_close, &QToolButton::clicked, this, &GridPanel::closeRequested);
    connect(m_show, &QToolButton::toggled, this, [this] { emitOptions(); });
    connect(m_snapPoints, &QToolButton::toggled, this, [this] { emitOptions(); });
    connect(m_auto, &QToolButton::toggled, this, [this](bool on) {
        m_step->setEnabled(!on);
        emitOptions();
    });
    connect(m_step, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { emitOptions(); });
    connect(m_angle, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { emitOptions(); });
    connect(m_distance, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { emitOptions(); });
    // (half and double are of the step that is shown: when it was automatic it becomes a number of its own, the one the automatic step worked out to, halved or doubled)
    connect(m_half, &QToolButton::clicked, this, [this] {
        const double v = m_step->value();
        m_auto->setChecked(false);
        m_step->setValue(std::max(1e-4, v / 2.0));
    });
    connect(m_double, &QToolButton::clicked, this, [this] {
        const double v = m_step->value();
        m_auto->setChecked(false);
        m_step->setValue(std::min(1e6, v * 2.0));
    });

    if (parent) parent->installEventFilter(this);
}

void GridPanel::emitOptions()
{
    if (m_updating) return;
    emit(optionsChanged(m_show->isChecked(), m_auto->isChecked() ? 0.0f : float(m_step->value()), float(m_angle->value()), m_distance->value(), m_snapPoints->isChecked()));
}

void GridPanel::setGrid(const SnapGrid& grid, float step, bool suspended)
{
    Q_UNUSED(suspended);
    m_updating = true;
    {
        QSignalBlocker a(m_show), b(m_snapPoints), c(m_auto), d(m_step), e(m_angle), f(m_distance);
        m_show->setChecked(grid.show);
        m_snapPoints->setChecked(grid.snapPoints);
        m_auto->setChecked(grid.spacing <= 0);
        m_step->setEnabled(grid.spacing > 0);
        m_step->setValue(grid.spacing > 0 ? grid.spacing : std::max(1e-4f, step));
        m_angle->setValue(grid.angle);
        m_distance->setValue(grid.distance);
    }
    m_updating = false;
}

void GridPanel::refreshUnits()
{
    QSignalBlocker a(m_step);
    m_step->refreshUnit();
}

bool GridPanel::eventFilter(QObject* obj, QEvent* e)
{
    if (obj == parentWidget() && e->type() == QEvent::Resize) place();
    return QFrame::eventFilter(obj, e);
}

void GridPanel::showEvent(QShowEvent* e)
{
    QFrame::showEvent(e);
    place();
}

void GridPanel::place()
{
    if (!CardController::sized(this)) resize(340, std::max(minimumSizeHint().height(), sizeHint().height()));
    if (parentWidget() && !CardController::moved(this))
    {
        // Top-right, below the orientation triad -- and below the section card when that is there too
        int top = 124;
        if (auto section = parentWidget()->findChild<QWidget*>("SectionPanel"))
        {
            if (section->isVisible()) top = section->geometry().bottom() + 8;
        }
        move(parentWidget()->width() - width() - 8, top);
    }
}

}   // namespace FielDes
