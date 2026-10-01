/*
FielDes: field-driven design
Derived from Studio, a simple GUI for the libfive CAD kernel

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/
#pragma once

#include <QObject>
#include <QStringList>
#include <QTimer>
#include <functional>

class QWidget;

namespace FielDes {

/*
 *  A tiny command runner for scripted GUI checks, enabled only when the
 *  FIELDES_AUTOMATION environment variable names a command file.  Commands
 *  (one per line, '#' comments) are executed in order:
 *
 *    wait <ms>                 pause
 *    action <id>               trigger a registered command (see Shortcuts)
 *    key <sequence>            send a key press/release to the focus widget
 *    type <text>               send text one character at a time
 *    focus editor|view         give keyboard focus to a pane
 *    cursor <line> <col>       place the editor cursor (0-based)
 *    grab <file.png>           save an image of the main window
 *    dump <file.txt>           save the current script text
 *    quit                      close FielDes (discarding changes)
 *
 *  Extra commands can be registered by other components with add().
 */
class Automation : public QObject
{
    Q_OBJECT
public:
    Automation(QWidget* window, QObject* parent=nullptr);
    bool load(const QString& path);

    using Handler = std::function<void(const QString& args)>;
    void add(const QString& name, Handler h) { m_handlers[name] = h; }

public slots:
    void start();

protected slots:
    void step();

protected:
    QWidget* m_window;
    QStringList m_lines;
    int m_next=0;
    QTimer m_timer;
    QMap<QString, Handler> m_handlers;
};

}   // namespace FielDes
