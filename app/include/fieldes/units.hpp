/*
FielDes: field-driven design

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/
#pragma once

#include <QDoubleSpinBox>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QLocale>
#include <QRegularExpression>
#include <QSettings>
#include <QString>
#include <QStringList>

#include <cmath>

namespace FielDes {

/*
 *  The units numbers are SHOWN in (Settings > Units).  A script, the solvers and the data of the program are in millimetres, newtons, megapascals,
 *  kilograms and degrees Celsius; what is drawn for a person -- the ruler, the cards of a result, the colour bars, the value under the cursor, the work
 *  plane -- is converted on its way to the screen by the table that fieldes.units made (Python is the one place that knows the units: Window::applyUnits
 *  asks it and gives the answer to setTable).  Without a table every number is shown as it is.
 */
namespace Units {

struct Conversion
{
    double scale = 1.0;         // shown = number * scale + offset
    double offset = 0.0;
    QString label;              // the unit as it is shown
    bool known = false;
};

inline QHash<QString, Conversion>& table() { static QHash<QString, Conversion> t; return t; }

inline QStringList quantities() { return {"length", "force", "stress", "mass", "temperature"}; }

/*  The table: {native unit: {scale, offset, label}}  */
inline void setTable(const QJsonObject& native)
{
    auto& t = table();
    t.clear();
    for (const QString& unit : native.keys())
    {
        const QJsonObject o = native[unit].toObject();
        Conversion c;
        c.scale = o["scale"].toDouble(1.0);
        c.offset = o["offset"].toDouble(0.0);
        c.label = o["label"].toString(unit);
        c.known = true;
        t.insert(unit, c);
    }
}

/*  How a number in a native unit is shown (itself when the unit is not converted)  */
inline Conversion of(const QString& unit)
{
    const auto it = table().constFind(unit.trimmed());
    if (it == table().constEnd())
    {
        Conversion c;
        c.label = unit;
        return c;
    }
    return *it;
}

/*  The units chosen, as the program keeps them between runs  */
inline QJsonObject saved()
{
    QSettings s;
    QJsonObject o;
    for (const QString& q : quantities())
    {
        const QString v = s.value("units/" + q).toString();
        if (!v.isEmpty()) o[q] = v;
    }
    return o;
}

inline void save(const QJsonObject& choice)
{
    QSettings s;
    for (const QString& q : quantities())
    {
        if (choice.contains(q)) s.setValue("units/" + q, choice[q].toString());
        else s.remove("units/" + q);
    }
}

/*  Shown lengths in one millimetre, and the name of the unit  */
inline double lengthScale() { return of("mm").scale; }
inline QString lengthLabel() { return of("mm").label; }

/*  The decimals of a length shown in the unit chosen, when a millimetre would have `base`: more of them in a larger unit (a hundredth of a millimetre is five decimals of a metre)  */
inline int lengthDecimals(int base)
{
    const double s = lengthScale();
    return base + (s > 0 && s < 1.0 ? int(std::ceil(-std::log10(s) - 1e-9)) : 0);
}

/*  A length in mm as text in the shown unit, 'g' with `digits` figures  */
inline QString lengthText(double mm, int digits = 4) { return QString::number(mm * lengthScale(), 'g', digits); }

/*
 *  A label names its unit in brackets at its end: "von Mises stress (MPa)", "speed (mm/s) [element]".  The brackets are how the labels of the results
 *  are written (they are the legends and the lists of fields), so the unit is read from there
 */
inline QRegularExpression labelUnitPattern() { return QRegularExpression(R"(\(([^()]*)\)(\s*\[[^\]]*\])?\s*$)"); }

inline Conversion ofLabel(const QString& label)
{
    static const QRegularExpression re = labelUnitPattern();
    const auto m = re.match(label);
    if (!m.hasMatch()) return Conversion();
    return of(m.captured(1));
}

/*  The label with its unit as it is shown  */
inline QString shownLabel(const QString& label)
{
    static const QRegularExpression re = labelUnitPattern();
    const auto m = re.match(label);
    if (!m.hasMatch()) return label;
    const Conversion c = of(m.captured(1));
    if (!c.known) return label;
    return label.left(m.capturedStart(1)) + c.label + label.mid(m.capturedEnd(1));
}

/*  A value of the field a label names, as it is shown  */
inline double shownValue(const QString& label, double v)
{
    const Conversion c = ofLabel(label);
    return c.known ? v * c.scale + c.offset : v;
}

/*  A key number of a result {label, text, unit}, as it is shown (a text that is no number is left as it is)  */
inline QStringList shownRow(const QStringList& row)
{
    if (row.size() < 3) return row;
    const Conversion c = of(row[2]);
    if (!c.known) return row;
    bool ok = false;
    const double v = row[1].toDouble(&ok);
    if (!ok) return row;
    return {row[0], QString::number(v * c.scale + c.offset, 'g', 4), c.label};
}

/*
 *  A box for a length that the program keeps in millimetres and shows in the unit chosen: the value() is mm, the text is in the unit, and the suffix
 *  says which.  refreshUnit() after the units were changed
 */
class LengthSpinBox : public QDoubleSpinBox
{
public:
    explicit LengthSpinBox(QWidget* parent = nullptr) : QDoubleSpinBox(parent) { refreshUnit(); }

    /*  The decimals of a millimetre.  In a larger unit there are more of them: a hundredth of a millimetre is five decimals of a metre  */
    void setLengthDecimals(int n)
    {
        m_decimals = n;
        refreshUnit();
    }

    /*  After the units were changed: the unit in the suffix, the decimals of that unit; the value (in mm) stays  */
    void refreshUnit()
    {
        setDecimals(lengthDecimals(m_decimals));
        setSuffix(" " + lengthLabel());
    }

    QString textFromValue(double mm) const override
    {
        QString text = locale().toString(mm * lengthScale(), 'f', decimals());
        if (!isGroupSeparatorShown()) text.remove(locale().groupSeparator());           // (as the box does: 1000, not 1,000)
        return text;
    }

    double valueFromText(const QString& text) const override
    {
        static const QRegularExpression number(R"([-+]?\d*[.,]?\d+(?:[eE][-+]?\d+)?)");
        const auto m = number.match(text);
        if (!m.hasMatch()) return value();
        bool ok = false;
        const double shown = locale().toDouble(m.captured(0), &ok);
        const double scale = lengthScale();
        return ok && scale > 0 ? shown / scale : value();
    }

private:
    int m_decimals = 2;
};

}   // namespace Units
}   // namespace FielDes
