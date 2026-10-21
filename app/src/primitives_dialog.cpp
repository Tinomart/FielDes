/*
FielDes: field-driven design
Derived from Studio, a simple GUI for the libfive CAD kernel

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/
#include <QAbstractItemDelegate>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRegularExpression>
#include <QTimer>
#include <QVBoxLayout>

#include "fieldes/i18n.hpp"
#include "fieldes/primitives_dialog.hpp"

namespace FielDes {

namespace {

// (the kinds of model a primitive can be, as the library names them, and what they are called here: the kinds whose menu is a LIST of shapes.  A point and a symmetry are one button in the menus,
// with nothing nested under it, so there is no list of them to tick, rename or add to)
struct KindName { const char* key; const char* text; };
const KindName kKinds[] = {
    {"solid", "3D shapes"}, {"profile", "2D shapes"}, {"curve", "Lines (1D)"}, {"surface", "Surfaces"}, {"field", "Fields"},
};

}   // namespace

PrimitivesDialog::PrimitivesDialog(const Hooks& hooks, QWidget* parent)
    : QDialog(parent), m_hooks(hooks), m_kind(new QComboBox), m_list(new QListWidget), m_name(new QLineEdit), m_add(new QPushButton),
      m_remove(new QPushButton(T("Remove"))), m_rename(new QPushButton(T("Rename"))), m_note(new QLabel)
{
    setObjectName("PrimitivesDialog");
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(T("Primitives"));
    if (m_hooks.selection) m_selection = m_hooks.selection();

    auto intro = new QLabel(T("Primitives are the shapes that the Primitives menus offer first. Tick a shape to make it a primitive, untick it to move it to the Parametric shapes. "
                              "\"Rename\" gives the selected one another name in the menus. "
                              "\"Add selection\" makes the model that is selected in the model tree a primitive of your own: it is kept in a file of yours and is there in every script you open."));
    intro->setWordWrap(true);

    for (const auto& k : kKinds) m_kind->addItem(T(k.text), QString::fromLatin1(k.key));
    // The window opens on the section of what is selected in the model tree: the library says which kind a model of that type is (a lattice or an imported part is a 3D shape).  A point or a
    // symmetry has no section -- it is one button in the menus -- and the window opens on the first one
    const QString selected = m_selection.isEmpty() ? QString()
                                                   : ask(QJsonObject{{"op", "kind"}, {"type", m_selection["type"].toString()}})["kind"].toString();
    const int at = m_kind->findData(selected);
    m_kind->setCurrentIndex(at >= 0 ? at : 0);
    const bool unlisted = !m_selection.isEmpty() && at < 0;      // (the selected model is of a kind that has no list: it cannot be made a primitive)
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
    m_add->setEnabled(!m_selection.isEmpty() && !unlisted);
    m_add->setToolTip(m_selection.isEmpty() ? T("Select a model in the model tree first: it becomes the primitive.")
                      : unlisted            ? T("A point or a symmetry is a single button in the menus, not a list, so it cannot be made a primitive.")
                                            : T("Make %1 (with everything it is made of) a primitive of your own.").arg(m_selection["name"].toString()));
    m_rename->setObjectName("PrimitiveRename");
    m_rename->setEnabled(false);
    m_rename->setToolTip(T("Give the primitive that is selected in the list another name: type it over the old one and press Return. The menus show the name you give; a shape of the library keeps "
                           "its function. Left empty, the library's own name comes back."));
    m_remove->setObjectName("PrimitiveRemove");
    m_remove->setEnabled(false);
    m_remove->setToolTip(T("Forget the primitive of your own that is selected in the list."));
    auto addRow = new QHBoxLayout;
    addRow->addWidget(m_name, 1);
    addRow->addWidget(m_add);
    addRow->addWidget(m_rename);
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
    resize(560, 560);

    connect(m_kind, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { fill(); });
    connect(m_list, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        if (m_filling || !m_renaming.isEmpty()) return;            // (a row that is filled, or whose name is being typed, is not a primitive ticked or unticked)
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
    connect(m_list, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* item) {
        m_remove->setEnabled(item && item->data(Qt::UserRole + 1).toBool());
        m_rename->setEnabled(item != nullptr);
    });
    connect(m_add, &QPushButton::clicked, this, [this] { addSelection(m_name->text()); });
    connect(m_remove, &QPushButton::clicked, this, [this] {
        if (auto item = m_list->currentItem()) removeEntry(item->data(Qt::UserRole).toString());
    });
    connect(m_rename, &QPushButton::clicked, this, [this] { beginRename(m_list->currentItem()); });
    // (the editor of a row closes -- with Return, Escape or a click elsewhere -- and what was typed is the new name; the row is read once the editor is gone)
    connect(m_list->itemDelegate(), &QAbstractItemDelegate::closeEditor, this, [this] { QTimer::singleShot(0, this, [this] { finishRename(); }); });
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
        // (what the library calls it, when the name it is shown under is another: the user's own name for it)
        if (!custom && e["renamed"].toBool()) item->setToolTip(T("The function is %1.").arg(e["name"].toString()));
        if (!custom) item->setCheckState(e["on"].toBool() ? Qt::Checked : Qt::Unchecked);
    }
    m_filling = false;
    m_remove->setEnabled(false);
    m_rename->setEnabled(false);
}

bool PrimitivesDialog::addSelection(const QString& name)
{
    if (m_selection.isEmpty())
    {
        say(T("Select a model in the model tree first."), true);
        return false;
    }
    const QString type = m_selection["type"].toString();
    // (the section it is in: the kind of its type -- a lattice is among the 3D shapes; a point or a symmetry has none: it is one button in the menus, not a list of shapes)
    const int at = m_kind->findData(ask(QJsonObject{{"op", "kind"}, {"type", type}})["kind"].toString());
    if (at < 0)
    {
        say(T("A point or a symmetry is a single button in the menus, not a list, so it cannot be made a primitive."), true);
        return false;
    }
    const QJsonObject answer = ask(QJsonObject{{"op", "add"}, {"source", m_hooks.source ? m_hooks.source() : QString()}, {"line", m_selection["line"].toInt()},
                                               {"end_line", m_selection["end_line"].toInt()}, {"name", name}, {"type", type}});
    if (!answer["ok"].toBool()) return false;
    if (at != m_kind->currentIndex()) m_kind->setCurrentIndex(at);
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

void PrimitivesDialog::beginRename(QListWidgetItem* item)
{
    if (!item || !m_renaming.isEmpty()) return;
    m_renaming = item->data(Qt::UserRole).toString();
    // (the text of the row is what is typed over: the name alone, without the group that is written after it)
    m_filling = true;
    item->setText(item->data(Qt::UserRole + 2).toString());
    item->setFlags(item->flags() | Qt::ItemIsEditable);
    m_filling = false;
    m_list->editItem(item);
}

void PrimitivesDialog::finishRename()
{
    if (m_renaming.isEmpty()) return;
    const QString name = m_renaming;
    m_renaming.clear();
    QListWidgetItem* item = nullptr;
    for (int i = 0; i < m_list->count(); ++i)
    {
        if (m_list->item(i)->data(Qt::UserRole).toString() == name) item = m_list->item(i);
    }
    // (nothing typed over, or Escape: the row as it was)
    if (!item || item->text().trimmed() == item->data(Qt::UserRole + 2).toString())
    {
        fill();
        return;
    }
    renameEntry(name, item->text().trimmed());
}

bool PrimitivesDialog::renameEntry(const QString& name, const QString& label)
{
    const QJsonObject answer = ask(QJsonObject{{"op", "rename"}, {"name", name}, {"label", label}, {"type", m_kind->currentData().toString()}});
    fill();         // (the list as the library has it now: the new name, or the old one where it was refused -- the reason stays in the note)
    if (!answer["ok"].toBool()) return false;
    say(label.isEmpty() ? T("%1 has the name of the library again.").arg(name) : T("The menus call it %1 now.").arg(answer["label"].toString(label)), false);
    if (m_hooks.changed) m_hooks.changed();
    return true;
}

bool PrimitivesDialog::editEntry(const QString& name, const QString& label)
{
    QListWidgetItem* item = nullptr;
    for (int i = 0; i < m_list->count(); ++i)
    {
        if (m_list->item(i)->data(Qt::UserRole).toString() == name) item = m_list->item(i);
    }
    if (!item)
    {
        say(T("%1 is not in this list.").arg(name), true);
        return false;
    }
    m_list->setCurrentItem(item);
    m_rename->click();                                  // (the editor opens on the row)
    QLineEdit* editor = m_list->viewport()->findChild<QLineEdit*>();
    if (!editor) return false;
    editor->setText(label);
    QKeyEvent press(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier), release(QEvent::KeyRelease, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(editor, &press);
    QApplication::sendEvent(editor, &release);
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
        out << item->data(Qt::UserRole).toString() + "|" + (custom || item->checkState() == Qt::Checked ? "on" : "off") + "|" + (custom ? "yours" : "built-in") + "|" +
               item->data(Qt::UserRole + 2).toString();
    }
    return out;
}

}   // namespace FielDes
