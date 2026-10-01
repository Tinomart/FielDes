/*
FielDes: field-driven design
Derived from Studio, a simple GUI for the libfive CAD kernel

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QVBoxLayout>

#include "fieldes/shortcuts.hpp"

namespace FielDes {

QList<Shortcuts::Entry>& Shortcuts::entries()
{
    static QList<Entry> e;
    return e;
}

QAction* Shortcuts::add(QAction* a, const QString& id,
                        const QList<QKeySequence>& defaults)
{
    Entry e;
    e.id = id;
    e.category = id.section('.', 0, 0);
    e.action = a;
    e.defaults = defaults;
    entries().append(e);

    QSettings s("FielDes", "FielDes");
    const QString key = "shortcuts/" + id;
    if (s.contains(key))
    {
        QList<QKeySequence> keys;
        for (const auto& str : s.value(key).toStringList())
        {
            if (!str.isEmpty())
            {
                keys << QKeySequence(str, QKeySequence::PortableText);
            }
        }
        a->setShortcuts(keys);
    }
    else
    {
        a->setShortcuts(defaults);
    }
    // Show the binding in tooltips too
    return a;
}

void Shortcuts::set(const QString& id, const QList<QKeySequence>& keys)
{
    QSettings s("FielDes", "FielDes");
    for (auto& e : entries())
    {
        if (e.id == id && e.action)
        {
            e.action->setShortcuts(keys);
            QStringList strs;
            for (const auto& k : keys) strs << k.toString(QKeySequence::PortableText);
            if (keys == e.defaults)
            {
                s.remove("shortcuts/" + id);
            }
            else
            {
                // An explicit empty list (no binding) is stored as [""]
                s.setValue("shortcuts/" + id, strs.isEmpty() ? QStringList{""} : strs);
            }
        }
    }
}

void Shortcuts::resetAll()
{
    QSettings s("FielDes", "FielDes");
    s.remove("shortcuts");
    for (auto& e : entries())
    {
        if (e.action)
        {
            e.action->setShortcuts(e.defaults);
        }
    }
}

QString Shortcuts::toText(const QList<QKeySequence>& keys)
{
    QStringList out;
    for (const auto& k : keys)
    {
        out << k.toString(QKeySequence::NativeText);
    }
    return out.join(",  ");
}

////////////////////////////////////////////////////////////////////////////////

namespace {
QString actionLabel(const QAction* a)
{
    QString t = a->text();
    t.remove('&');
    if (t.endsWith("...")) t.chop(3);
    return t;
}
}

ShortcutDialog::ShortcutDialog(QWidget* parent)
    : QDialog(parent), m_table(new QTableWidget), m_filter(new QLineEdit)
{
    setWindowTitle("Keyboard shortcuts");
    resize(640, 560);

    m_filter->setPlaceholderText("Filter commands or keys");
    m_filter->setClearButtonEnabled(true);

    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels({"Command", "Shortcut", "Category"});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->hide();
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSortingEnabled(false);

    auto hint = new QLabel("Double-click a command to change its shortcut. "
                           "Changes apply immediately and are remembered.");
    hint->setWordWrap(true);

    auto buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    auto reset = buttons->addButton("Reset all to defaults", QDialogButtonBox::ResetRole);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    connect(reset, &QPushButton::clicked, this, [this]{
        if (QMessageBox::question(this, "Reset shortcuts",
                "Restore every default shortcut?") == QMessageBox::Yes)
        {
            Shortcuts::resetAll();
            rebuild();
        }
    });
    connect(m_table, &QTableWidget::cellDoubleClicked,
            this, [this](int row, int){ editRow(row); });
    connect(m_filter, &QLineEdit::textChanged, this, &ShortcutDialog::rebuild);

    auto layout = new QVBoxLayout(this);
    layout->addWidget(hint);
    layout->addWidget(m_filter);
    layout->addWidget(m_table, 1);
    layout->addWidget(buttons);

    rebuild();
}

void ShortcutDialog::rebuild()
{
    m_table->setRowCount(0);
    const QString f = m_filter->text();
    const auto& es = Shortcuts::entries();
    for (int i=0; i < es.size(); ++i)
    {
        const auto& e = es[i];
        if (!e.action) continue;
        const QString label = actionLabel(e.action);
        const QString keys = Shortcuts::toText(e.action->shortcuts());
        if (!f.isEmpty() && !label.contains(f, Qt::CaseInsensitive) &&
            !keys.contains(f, Qt::CaseInsensitive) && !e.category.contains(f, Qt::CaseInsensitive))
        {
            continue;
        }
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        auto name = new QTableWidgetItem(label);
        name->setData(Qt::UserRole, i);
        name->setToolTip(e.id);
        auto k = new QTableWidgetItem(keys);
        if (e.action->shortcuts() != e.defaults)
        {
            QFont bold = k->font();
            bold.setBold(true);
            k->setFont(bold);
            k->setToolTip("Default: " + Shortcuts::toText(e.defaults));
        }
        m_table->setItem(row, 0, name);
        m_table->setItem(row, 1, k);
        m_table->setItem(row, 2, new QTableWidgetItem(e.category));
    }
}

void ShortcutDialog::editRow(int row)
{
    const int index = m_table->item(row, 0)->data(Qt::UserRole).toInt();
    auto& e = Shortcuts::entries()[index];
    if (!e.action) return;

    QDialog d(this);
    d.setWindowTitle("Shortcut for \"" + actionLabel(e.action) + "\"");
    auto edit = new QKeySequenceEdit(e.action->shortcut());
    auto clear = new QPushButton("No shortcut");
    auto defaults = new QPushButton("Default (" + Shortcuts::toText(e.defaults) + ")");
    auto conflict = new QLabel;
    conflict->setStyleSheet("color: #dc322f;");
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

    auto layout = new QVBoxLayout(&d);
    layout->addWidget(new QLabel("Press the new key combination:"));
    layout->addWidget(edit);
    auto row2 = new QHBoxLayout;
    row2->addWidget(clear);
    row2->addWidget(defaults);
    layout->addLayout(row2);
    layout->addWidget(conflict);
    layout->addWidget(buttons);

    bool useDefaults = false;
    connect(clear, &QPushButton::clicked, &d, [&]{ edit->clear(); useDefaults = false; });
    connect(defaults, &QPushButton::clicked, &d, [&]{ useDefaults = true; d.accept(); });
    connect(buttons, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    connect(edit, &QKeySequenceEdit::keySequenceChanged, &d, [&](const QKeySequence& k){
        QStringList users;
        for (const auto& o : Shortcuts::entries())
        {
            if (o.action && o.action != e.action && !k.isEmpty() &&
                o.action->shortcuts().contains(k))
            {
                users << actionLabel(o.action);
            }
        }
        conflict->setText(users.isEmpty() ? QString()
            : "Already used by: " + users.join(", ") + " (that binding will be removed)");
    });

    if (d.exec() != QDialog::Accepted) return;

    QList<QKeySequence> keys;
    if (useDefaults)
    {
        keys = e.defaults;
    }
    else if (!edit->keySequence().isEmpty())
    {
        keys << edit->keySequence();
    }

    // Steal the binding from any other command using it
    for (const auto& k : keys)
    {
        for (const auto& o : Shortcuts::entries())
        {
            if (o.action && o.action != e.action && o.action->shortcuts().contains(k))
            {
                auto remaining = o.action->shortcuts();
                remaining.removeAll(k);
                Shortcuts::set(o.id, remaining);
            }
        }
    }
    Shortcuts::set(e.id, keys);
    rebuild();
}

}   // namespace FielDes
