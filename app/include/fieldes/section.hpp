/*
FielDes: field-driven design
Derived from Studio, a simple GUI for the libfive CAD kernel

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/
#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <memory>

#include <QFrame>
#include <QImage>
#include <QVector>
#include <QVector3D>
#include <QVector4D>
#include <QWidget>

#include "libfive/tree/tree.hpp"

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QSlider;
class QToolButton;

namespace FielDes {

/*  How the section view / field viewer is configured  */
struct SectionSettings
{
    bool enabled = false;
    int axis = 2;            // plane normal: 0 = X, 1 = Y, 2 = Z
    float offset = 0;        // plane position along the axis
    bool flip = false;       // which half of the model is cut away
    bool clip = true;        // cut the model at the plane
    bool field = true;       // draw the SDF heat map on the plane
    float opacity = 0.9f;
    float range = 0;         // colour range (+-); 0 = automatic
    float spacing = 0;       // iso-line spacing; 0 = automatic
    bool wholeElements = false;  // analysis elements shown whole, not cut

    bool operator==(const SectionSettings& o) const;
    bool operator!=(const SectionSettings& o) const { return !(*this == o); }

    /*  Plane equation (a, b, c, d) with the KEPT side positive:
     *  a x + b y + c z + d >= 0 is kept  */
    QVector4D clipPlane() const;
};

/*  A sampled slice of the field (raw distance values; colouring happens
 *  in the viewport's shader, and in colorizeField for the 2D view)  */
struct FieldSlice
{
    int axis = 2;
    float offset = 0;
    QVector3D min, max;      // the plane rectangle (min/max along the axis are both = offset)
    int w = 0, h = 0;        // samples along the plane's u / v axes
    QVector<float> values;   // row-major, row 0 at the low end of v
    float lo = 0, hi = 0;    // value range present in the samples
    float rangeIn = 1;       // colour scale inside (deepest shown as dark blue)
    float rangeOut = 1;      // colour scale outside
    float fade = 1;          // outside distance where the plane fades out
    float spacing = 0;       // iso-line spacing actually used
    QImage image;            // colour-mapped samples for the 2D view (row 0 = low v)
    int generation = 0;
    bool fine = false;       // the full-resolution pass (not the quick preview)

    // A model coloured by a field (an analysis result): that field inside
    // the model (NaN elsewhere), shown instead of the distance
    bool hasColor = false;
    QVector<float> color;
    float colorLo = 0, colorHi = 1;
    QString colorMap = "turbo";
    QString colorLabel;

    // A deformed model's displacements (unscaled) on a coarser grid of
    // vertices over the plane (gw x gh, row-major from low v), so the plane
    // can be drawn deformed with the model
    int gw = 0, gh = 0;
    QVector<QVector3D> disp;

    bool valid() const { return w > 0 && h > 0 && values.size() == w * h; }

    /*  Plane axes: u / v index into (x, y, z) */
    static int uAxis(int axis) { return axis == 0 ? 1 : 0; }
    static int vAxis(int axis) { return axis == 2 ? 1 : 2; }

    /*  Bilinear sample at plane coordinates (u, v); NaN outside  */
    float sample(float u, float v) const;
};

/*  One displayed shape, copied so sampling can run off the GUI thread  */
struct FieldSource
{
    libfive::Tree tree = libfive::Tree::invalid();
    std::map<libfive::Tree::Id, float> vars;
    // The field it is coloured by, if any
    libfive::Tree color = libfive::Tree::invalid();
    float lo = 0, hi = 1;
    QString map = "turbo";
    QString label;
    // Its displacements, if it is drawn deformed
    libfive::Tree disp[3] = {libfive::Tree::invalid(), libfive::Tree::invalid(),
                             libfive::Tree::invalid()};
};

/*  The colour range (lo, hi) of a field source: its values on a coarse grid over the region, without the extreme one percent
 *  at each end  */
void autoColorRange(FieldSource& src, QVector3D lo, QVector3D hi);

/*
 *  Samples min(shape values) over the plane (the union of what is shown)
 *  inside the rectangle bmin..bmax, about longSide samples along its long
 *  side.  `fade` is the distance outside the model where the plane fades
 *  out.  Runs in a worker thread; a changed `generation` aborts early
 *  (returning an invalid slice).
 */
FieldSlice sampleField(const QVector<FieldSource>& sources,
                       const SectionSettings& s,
                       QVector3D bmin, QVector3D bmax, int longSide, float fade,
                       const std::atomic<int>* generation, int myGeneration);

/*  Colour scale and iso-line spacing (automatic when the settings say 0),
 *  plus the image for the 2D view  */
void colorizeField(FieldSlice& slice, float range, float spacing);

/*  The image of a slice for the 2D view (slice.image), made from its samples: only when that view is shown  */
void buildSliceImage(FieldSlice& slice);

/*  The diverging colour map shared by the 2D view and the legend:
 *  t in [-1, 1], negative = inside  */
QColor fieldColour(float t);

/*  The 2D view of the slice with a readout under the mouse  */
class FieldView : public QWidget
{
    Q_OBJECT
public:
    FieldView(QWidget* parent=nullptr);
    void setSlice(const FieldSlice& s);
    QSize sizeHint() const override { return QSize(260, 200); }

protected:
    void paintEvent(QPaintEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent* e) override;
    QRect imageRect() const;

    FieldSlice m_slice;
    QPoint m_mouse;
    bool m_hover = false;
};

/*  Colour legend: the map from -rangeIn to +rangeOut, with labels  */
class FieldLegend : public QWidget
{
    Q_OBJECT
public:
    FieldLegend(QWidget* parent=nullptr);
    void setSlice(const FieldSlice& s);
    QSize sizeHint() const override { return QSize(240, 30); }

protected:
    void paintEvent(QPaintEvent* e) override;
    float m_in = 0, m_out = 0;
    bool m_valid = false;
    // (a plane coloured by a field -- an analysis result, a field model: its own colour map and range, not the distance scale)
    bool m_hasColor = false;
    QString m_map, m_label;
    float m_lo = 0, m_hi = 1;
};

/*
 *  The section controls, as a card floating in the viewport's top-right
 *  corner (styled like the model tree)
 */
class SectionPanel : public QFrame
{
    Q_OBJECT
public:
    SectionPanel(QWidget* parent=nullptr);
    const SectionSettings& settings() const { return m_settings; }

    /*  Supplies the bounds of the displayed models, so that the plane
     *  starts through their middle rather than the render region's  */
    void setModelBoundsProvider(std::function<bool(QVector3D&, QVector3D&)> f)
    { m_modelBounds = f; }

    /*  Supplies the direction from the model towards the camera, so the
     *  kept half can be the far one (its cut face then faces the camera)  */
    void setCameraDirectionProvider(std::function<QVector3D()> f)
    { m_cameraDir = f; }

public slots:
    /*  "Whole elements" is offered only while the plane cuts a part shown with analysis elements  */
    void setWholeElementsAvailable(bool available);
    /*  The render bounds, which set the offset slider's range  */
    void setBounds(QVector3D min, QVector3D max);
    void setSlice(FieldSlice s);
    void setEnabledSection(bool on);

    /*  The plane was dragged in the viewport  */
    void setOffset(float offset);

    /*  The field value under the mouse in the viewport (empty: none)  */
    void setReadout(const QString& text);

signals:
    void settingsChanged(SectionSettings s);

    /*  The close button: the section should be switched off  */
    void closeRequested();

protected:
    bool eventFilter(QObject* obj, QEvent* e) override;
    void place();
    void emitChange();
    void syncOffsetWidgets();
    float preferredOffset(int axis) const;
    void faceCamera();
    void updateInfo();
    std::function<QVector3D()> m_cameraDir;
    std::function<bool(QVector3D&, QVector3D&)> m_modelBounds;

    SectionSettings m_settings;
    QVector3D m_min, m_max;
    bool m_boundsKnown = false;
    bool m_updating = false;
    FieldSlice m_slice;
    QString m_readout;

    QToolButton* m_header;
    QToolButton* m_enable;
    QToolButton* m_close;
    QWidget* m_body;
    QToolButton* m_axes[3];
    QToolButton* m_flip;
    QSlider* m_offsetSlider;
    QDoubleSpinBox* m_offsetSpin;
    QToolButton* m_clip;
    QToolButton* m_field;
    QToolButton* m_whole;
    QWidget* m_wholeBox = nullptr;
    QSlider* m_opacity;
    QToolButton* m_autoRange;
    QDoubleSpinBox* m_range;
    FieldLegend* m_legend;
    QLabel* m_info;
    QToolButton* m_show2d;
    FieldView* m_view;
    bool m_collapsed = false;
};

/*  How the field viewer is configured: a disc, facing along an axis, with its middle at a place (it starts where the field is about;
 *  the arrows in the viewport move it)  */
struct FieldViewSettings
{
    int axis = 2;            // the disc faces along: 0 = X, 1 = Y, 2 = Z
    QVector3D centre;        // its middle: the place along the axis is centre[axis]
    float radius = 0;        // of the disc (0: not chosen yet)
    float opacity = 0.9f;
    float offset() const { return centre[axis]; }
};

/*
 *  The field viewer, as a card floating in the viewport's top-right corner like the section card: it shows the field that is
 *  selected in the model tree on a DISC (a plane of a radius), centred where the field is about -- the point or the body it
 *  was made from, else the origin.  It cuts no models, and has none of the buttons that are only about models.  It is not closed:
 *  it is there while a field is selected.  With several fields selected, a menu chooses which one it shows.
 */
class FieldPanel : public QFrame
{
    Q_OBJECT
public:
    FieldPanel(QWidget* parent=nullptr);
    const FieldViewSettings& settings() const { return m_settings; }

    /*  The section view is a card of its own that is open when the user wants it, with or without a field: where both are open
     *  (and neither was moved by hand) this card sits below that one instead of over it  */
    void stackBelow(QWidget* sectionCard);

public slots:
    /*  The selected fields (their keys and the names shown) and the one that is shown; the menu is there for two or more  */
    void setFields(const QStringList& keys, const QStringList& names, int current);
    /*  Where the shown field is about (the disc is centred there, at its place along the axis) and the render region (the range
     *  of the sliders); the disc goes back to its place and its default size  */
    void setContext(QVector3D centre, bool known, QVector3D regionMin, QVector3D regionMax);
    void setSlice(FieldSlice s);
    /*  The disc was dragged in the viewport: along its normal or in its plane, by an arrow, or by the dot in the middle  */
    void setCentre(QVector3D centre);
    /*  The field's value under the mouse in the viewport (empty: none)  */
    void setReadout(const QString& text);

signals:
    void settingsChanged(FieldViewSettings s);
    /*  The menu chose another of the selected fields  */
    void fieldChosen(QString key);

protected:
    bool eventFilter(QObject* obj, QEvent* e) override;
    void place();
    void emitChange();
    void syncWidgets();
    void updateInfo();
    float radiusMax() const;

    FieldViewSettings m_settings;
    QVector3D m_home, m_min = QVector3D(-10, -10, -10), m_max = QVector3D(10, 10, 10);
    bool m_updating = false;
    bool m_collapsed = false;
    FieldSlice m_slice;
    QString m_readout, m_name;
    QStringList m_keys;
    QWidget* m_sectionCard = nullptr;

    QToolButton* m_header;
    QComboBox* m_combo;
    QWidget* m_comboRow;
    QWidget* m_body;
    QToolButton* m_axes[3];
    QSlider* m_offsetSlider;
    QDoubleSpinBox* m_offsetSpin;
    QSlider* m_radiusSlider;
    QDoubleSpinBox* m_radiusSpin;
    QSlider* m_opacity;
    FieldLegend* m_legend;
    QLabel* m_info;
    QToolButton* m_show2d;
    FieldView* m_view;
};

}   // namespace FielDes
