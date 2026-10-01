/*
FielDes: field-driven design
Derived from Studio, a simple GUI for the libfive CAD kernel

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/
#pragma once

#include <QAction>
#include <QDialog>
#include <QKeySequence>
#include <QList>
#include <QPointer>

class QTableWidget;
class QLineEdit;

namespace FielDes {

/*
 *  Every rebindable command is registered here with a stable id
 *  (e.g. "edit.find") and its default key sequences.  User overrides
 *  live in QSettings under "shortcuts/<id>" and are applied on
 *  registration, so they survive restarts.
 */
class Shortcuts
{
public:
    struct Entry {
        QString id;
        QString category;
        QPointer<QAction> action;
        QList<QKeySequence> defaults;
    };

    /*  Registers an action and applies any stored override  */
    static QAction* add(QAction* a, const QString& id,
                        const QList<QKeySequence>& defaults);

    static QList<Entry>& entries();

    /*  Sets (and persists) the keys for an entry  */
    static void set(const QString& id, const QList<QKeySequence>& keys);

    /*  Restores every default (and clears stored overrides)  */
    static void resetAll();

    /*  Human-readable, e.g. "Ctrl+F, F3"  */
    static QString toText(const QList<QKeySequence>& keys);
};

/*  Table of all commands with editable shortcuts  */
class ShortcutDialog : public QDialog
{
    Q_OBJECT
public:
    ShortcutDialog(QWidget* parent=nullptr);

protected:
    void rebuild();
    void editRow(int row);

    QTableWidget* m_table;
    QLineEdit* m_filter;
};

}   // namespace FielDes
