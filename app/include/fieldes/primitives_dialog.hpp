/*
FielDes: field-driven design
Derived from Studio, a simple GUI for the libfive CAD kernel

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/
#pragma once

#include <QDialog>
#include <QJsonObject>
#include <functional>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace FielDes {

/*
 *  The Primitives menu of the menu bar: which shapes the Primitives menus offer, and primitives of the user's own.
 *
 *  The list is of one kind of model (3D shapes, 2D shapes, lines, points, surfaces, fields -- the kind of the model that is selected when the window is opened).  Every entry the
 *  menus have of that kind is a row with a check mark: marked, it is under Primitives; not marked, it is with the Parametric shapes.  "Add selection" makes the model that is
 *  selected in the model tree -- with every statement of the script that it is made of -- a primitive of the user's own, kept in a file of theirs (primitives.json in their data folder:
 *  user_primitives.py) and offered in every script; a primitive of their own can be removed again.
 *
 *  Everything is done by the library (primitives_manage); the window only asks, and tells the menus to be made again when something changed.
 */
class PrimitivesDialog : public QDialog
{
    Q_OBJECT
public:
    struct Hooks
    {
        std::function<QString(const QString&)> manage;      // primitives_manage: JSON request -> JSON answer
        std::function<QJsonObject()> selection;             // {name, type, line, end_line} of the model that is selected ({} when none)
        std::function<QString()> source;                    // the text of the script
        std::function<void()> changed;                      // the menus are made again
    };
    PrimitivesDialog(const Hooks& hooks, QWidget* parent);

    /*  For the scripted tests: the kind of the list, and the rows as "name|on|custom" lines  */
    QString kind() const;
    QStringList rows() const;
    bool addSelection(const QString& name);
    bool removeEntry(const QString& name);

private:
    void fill();
    QJsonObject ask(const QJsonObject& request);
    void say(const QString& text, bool error);

    Hooks m_hooks;
    QJsonObject m_selection;
    QComboBox* m_kind;
    QListWidget* m_list;
    QLineEdit* m_name;
    QPushButton* m_add;
    QPushButton* m_remove;
    QLabel* m_note;
    bool m_filling = false;
};

}   // namespace FielDes
