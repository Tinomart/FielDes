/*
FielDes: field-driven design
Derived from Studio, a simple GUI for the libfive CAD kernel
Copyright (C) 2017  Matt Keeter

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/
#pragma once
#include <QApplication>

#include "fieldes/appwindow.hpp"
#include "fieldes/window.hpp"

namespace FielDes {
class App : public QApplication
{
    Q_OBJECT
public:
    App(int& argc, char** argv);
    /*  With FIELDES_TIMING: the stall clock of main.cpp calls it at every tick with the time since the tick before.  When that is long it writes what the events of the stall were -- how many of each
     *  kind for which object and how long they took in all -- and it forgets them for the next stall  */
    static void eventStatsReport(qint64 gap);
protected:
    bool event(QEvent* event) override;
    bool notify(QObject* receiver, QEvent* event) override;       // (with FIELDES_TIMING: the events that the window thread takes 80 ms or more for are logged)

    QScopedPointer<Window> window;
    QScopedPointer<AppWindow> appWindow;
};
}   // namespace FielDes
