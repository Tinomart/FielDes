/*
FielDes: field-driven design
Derived from Studio, a simple GUI for the libfive CAD kernel

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

#include "fieldes/i18n.hpp"
#include "fieldes/primitives_dialog.hpp"

namespace FielDes {

namespace {

// (the kinds of model a primitive can be, as the library names them, and what they are called here)
struct KindName { const char* key; const char* text; };
const KindName kKinds[] = {
    {"solid", "3D shapes"}, {"profile", "2D shapes"}, {"curve", "Lines (1D)"}, {"point", "Points"}, {"surface", "Surfaces"}, {"field", "Fields"},
};

}   // namespace

PrimitivesDialog::PrimitivesDialog(const Hooks& hooks, QWidget* parent)
    : QDialog(parent), m_hooks(hooks), m_kind(new QComboBox), m_list(new QListWidget), m_name(new QLineEdit), m_add(new QPushButton),
      m_remove(new QPushButton(T("Remove"))), m_note(new QLabel)
{
    setObjectName("PrimitivesDialog");
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(T("Primitives"));
    if (m_hooks.selection) m_selection = m_hooks.selection();

    auto intro = new QLabel(T("Primitives are the shapes that the Primitives menus offer first. Tick a shape to make it a primitive, untick it to move it to the Parametric shapes. "
                              "\"Add selection\" makes the model that is selected in the model tree a primitive of your own: it is kept in a file of yours and is there in every script you open."));
    intro->setWordWrap(true);

    for (const auto& k : kKinds) m_kind->addItem(T(k.text), QString::fromLatin1(k.key));
    const QString selected = m_selection["type"].toString();
    const int at = m_kind->findData(selected);
    m_kind->setCurrentIndex(at >= 0 ? at : 0);
    auto kindRow = new QHBoxLayout;
    kindRow->addWidget(new QLabel(T("Primitives of:")));
    kindRow->addWidget(m_kind, 1);

    m_list->setObjectName("PrimitivesList");
    m_list->setMinimumHeight(260);

    m_name->setObjectName("PrimitiveName");
    m_name->setPlaceholderText(T("Name of the new primitive"));
    m_name->setText(m_selection["name"].toString());
    m_add->setObjectName("PrimitiveAdd");
    m_add->setText(T("Add selection"));
    m_add->setEnabled(!m_selection.isEmpty());
    m_add->setToolTip(m_selection.isEmpty() ? T("Select a model in the model tree first: it becomes the primitive.")
                                            : T("Make %1 (with everything it is made of) a primitive of your own.").arg(m_selection["name"].toString()));
    m_remove->setObjectName("PrimitiveRemove");
    m_remove->setEnabled(false);
    m_remove->setToolTip(T("Forget the primitive of your own that is selected in the list."));
    auto addRow = new QHBoxLayout;
    addRow->addWidget(m_name, 1);
    addRow->addWidget(m_add);
    addRow->addWidget(m_remove);

    m_note->setWordWrap(true);
    m_note->setObjectName("PrimitivesNote");
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);

    auto layout = new QVBoxLayout(this);
    layout->addWidget(intro);
    layout->addLayout(kindRow);
    layout->addWidget(m_list, 1);
    layout->addLayout(addRow);
    layout->addWidget(m_note);
    layout->addWidget(buttons);
    resize(520, 560);

    connect(m_kind, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { fill(); });
    connect(m_list, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        if (m_filling) return;
        const bool on = item->checkState() == Qt::Checked;
        const QJsonObject answer = ask(QJsonObject{{"op", "set"}, {"name", item->data(Qt::UserRole).toString()}, {"on", on}, {"type", m_kind->currentData().toString()}});
        if (answer["ok"].toBool())
        {
            say(on ? T("%1 is a primitive now.").arg(item->data(Qt::UserRole + 2).toString()) : T("%1 is with the Parametric shapes now.").arg(item->data(Qt::UserRole + 2).toString()), false);
            if (m_hooks.changed) m_hooks.changed();
        }
        else
        {
            fill();
        }
    });
    connect(m_list, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* item) { m_remove->setEnabled(item && item->data(Qt::UserRole + 1).toBool()); });
    connect(m_add, &QPushButton::clicked, this, [this] { addSelection(m_name->text()); });
    connect(m_remove, &QPushButton::clicked, this, [this] {
        if (auto item = m_list->currentItem()) removeEntry(item->data(Qt::UserRole).toString());
    });
    fill();
}

QJsonObject PrimitivesDialog::ask(const QJsonObject& request)
{
    if (!m_hooks.manage) return QJsonObject{{"ok", false}, {"error", "no library"}};
    const QString text = m_hooks.manage(QString::fromUtf8(QJsonDocument(request).toJson(QJsonDocument::Compact)));
    const QJsonObject answer = QJsonDocument::fromJson(text.toUtf8()).object();
    if (!answer["ok"].toBool()) say(answer["error"].toString().isEmpty() ? T("The library did not answer.") : answer["error"].toString(), true);
    return answer;
}

void PrimitivesDialog::say(const QString& text, bool error)
{
    m_note->setText(text);
    m_note->setStyleSheet(error ? "color: #c0392b;" : QString());
}

void PrimitivesDialog::fill()
{
    const QJsonObject answer = ask(QJsonObject{{"op", "list"}, {"type", m_kind->currentData().toString()}});
    m_filling = true;
    m_list->clear();
    for (const auto v : answer["entries"].toArray())
    {
        const QJsonObject e = v.toObject();
        const bool custom = e["custom"].toBool();
        const QString label = e["label"].toString();
        auto item = new QListWidgetItem(custom ? label + "   " + T("(yours)") : label + "   [" + e["group"].toString() + "]", m_list);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | (custom ? Qt::NoItemFlags : Qt::ItemIsUserCheckable));
        item->setData(Qt::UserRole, e["name"].toString());
        item->setData(Qt::UserRole + 1, custom);
        item->setData(Qt::UserRole + 2, label);
        if (!custom) item->setCheckState(e["on"].toBool() ? Qt::Checked : Qt::Unchecked);
    }
    m_filling = false;
    m_remove->setEnabled(false);
}

bool PrimitivesDialog::addSelection(const QString& name)
{
    if (m_selection.isEmpty())
    {
        say(T("Select a model in the model tree first."), true);
        return false;
    }
    const QString type = m_selection["type"].toString();
    const QJsonObject answer = ask(QJsonObject{{"op", "add"}, {"source", m_hooks.source ? m_hooks.source() : QString()}, {"line", m_selection["line"].toInt()},
                                               {"end_line", m_selection["end_line"].toInt()}, {"name", name}, {"type", type}});
    if (!answer["ok"].toBool()) return false;
    const int at = m_kind->findData(type);
    if (at >= 0 && at != m_kind->currentIndex()) m_kind->setCurrentIndex(at);
    else fill();
    say(T("%1 is a primitive of yours now: it is under Primitives, in every script.").arg(name), false);
    if (m_hooks.changed) m_hooks.changed();
    return true;
}

bool PrimitivesDialog::removeEntry(const QString& name)
{
    const QJsonObject answer = ask(QJsonObject{{"op", "remove"}, {"name", name}, {"type", m_kind->currentData().toString()}});
    if (!answer["ok"].toBool()) return false;
    fill();
    say(T("%1 is forgotten.").arg(name), false);
    if (m_hooks.changed) m_hooks.changed();
    return true;
}

QString PrimitivesDialog::kind() const
{
    return m_kind->currentData().toString();
}

QStringList PrimitivesDialog::rows() const
{
    QStringList out;
    for (int i = 0; i < m_list->count(); ++i)
    {
        const auto item = m_list->item(i);
        const bool custom = item->data(Qt::UserRole + 1).toBool();
        out << item->data(Qt::UserRole).toString() + "|" + (custom || item->checkState() == Qt::Checked ? "on" : "off") + "|" + (custom ? "yours" : "built-in");
    }
    return out;
}

}   // namespace FielDes
