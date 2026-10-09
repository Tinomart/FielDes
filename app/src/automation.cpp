/*
FielDes: field-driven design
Derived from Studio, a simple GUI for the libfive CAD kernel

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/
#include <iostream>

#include <QApplication>
#include <QFile>
#include <QKeyEvent>
#include <QKeySequence>
#include <QTextStream>
#include <QWidget>

#include "fieldes/automation.hpp"
#include "fieldes/shortcuts.hpp"

namespace FielDes {

Automation::Automation(QWidget* window, QObject* parent)
    : QObject(parent), m_window(window)
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &Automation::step);

    add("wait", [this](const QString& a){ m_timer.start(a.toInt()); });
    add("action", [](const QString& id){
        for (auto& e : Shortcuts::entries())
        {
            if (e.id == id && e.action)
            {
                e.action->trigger();
                return;
            }
        }
        std::cerr << "automation: unknown action " << id.toStdString() << "\n";
    });
    add("key", [this](const QString& a){
        const QKeySequence seq(a, QKeySequence::PortableText);
        if (seq.isEmpty()) return;
#if QT_VERSION >= 0x060000
        const auto combo = seq[0];
        const int key = combo.key();
        const auto mods = combo.keyboardModifiers();
#else
        const int combo = seq[0];
        const int key = combo & ~Qt::KeyboardModifierMask;
        const auto mods = Qt::KeyboardModifiers(combo & Qt::KeyboardModifierMask);
#endif
        QString text;
        if (key >= 0x20 && key < 0x7f && !(mods & (Qt::ControlModifier | Qt::AltModifier)))
        {
            text = QChar(key);
            if (!(mods & Qt::ShiftModifier)) text = text.toLower();
        }
        if (key == Qt::Key_Return) text = "\r";
        if (key == Qt::Key_Tab) text = "\t";
        // (a popup, such as the completion list, has the keys while it is open)
        QWidget* w = QApplication::activePopupWidget();
        if (!w) w = QApplication::focusWidget();
        // (a window that is not the active one has no keyboard focus; the widget that would have it is the one that is sent the keys)
        if (!w) w = m_window->focusWidget();
        if (!w) return;
        QKeyEvent press(QEvent::KeyPress, key, mods, text);
        QApplication::sendEvent(w, &press);
        QKeyEvent release(QEvent::KeyRelease, key, mods, text);
        QApplication::sendEvent(w, &release);
    });
    add("type", [this](const QString& a){
        for (QChar c : a)
        {
            QWidget* w = QApplication::focusWidget();
            if (!w) w = m_window->focusWidget();
            if (!w) return;
            const int key = c.toUpper().unicode();
            QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier, QString(c));
            QApplication::sendEvent(w, &press);
            QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier, QString(c));
            QApplication::sendEvent(w, &release);
            QApplication::processEvents();
        }
    });
    add("grab", [this](const QString& path){
        m_window->grab().save(path);
    });
    add("quit", [this](const QString&){
        m_window->setWindowModified(false);
        QApplication::exit(0);
    });
    add("log", [](const QString& a){ std::cerr << "automation: " << a.toStdString() << "\n"; });
}

bool Automation::load(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
    {
        return false;
    }
    QTextStream in(&f);
    in.setCodec("UTF-8");           // (a script may name a menu or a row in any language)
    while (!in.atEnd())
    {
        const QString line = in.readLine().trimmed();
        if (!line.isEmpty() && !line.startsWith('#'))
        {
            m_lines << line;
        }
    }
    return true;
}

void Automation::start()
{
    m_timer.start(0);
}

void Automation::step()
{
    while (m_next < m_lines.size())
    {
        const QString line = m_lines[m_next++];
        const QString cmd = line.section(' ', 0, 0);
        const QString args = line.section(' ', 1);
        std::cerr << "automation> " << line.toStdString() << "\n";
        auto h = m_handlers.find(cmd);
        if (h == m_handlers.end())
        {
            std::cerr << "automation: unknown command " << cmd.toStdString() << "\n";
            continue;
        }
        h.value()(args);
        QApplication::processEvents();
        if (cmd == "wait")
        {
            return;   // the timer resumes us
        }
    }
}

}   // namespace FielDes
