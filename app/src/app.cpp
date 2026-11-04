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
#include <QElapsedTimer>
#include <QFileOpenEvent>
#include <QThread>
#include <QTimer>
#include <QSettings>

#include <algorithm>
#include <iostream>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "fieldes/app.hpp"
#include "fieldes/args.hpp"
#include "fieldes/i18n.hpp"
#include "fieldes/icons.hpp"
#include "fieldes/theme.hpp"

namespace FielDes {

App::App(int& argc, char** argv)
    : QApplication(argc, argv)
{
    // The Python package loads the kernel's libraries itself: point it at the ones
    // this program already uses, so both share their state (an import's progress,
    // the tessellation cache)
    if (!qEnvironmentVariableIsSet("FIELDES_DIR"))
        qputenv("FIELDES_DIR", QCoreApplication::applicationDirPath().toLocal8Bit());

    // The language of the program's own words: the setting, or the language of the computer (before any window is made)
    i18n::load();

    // The folder of the custom blocks that was chosen in Settings (the Python package reads it from the environment)
    if (!qEnvironmentVariableIsSet("FIELDES_BLOCKS"))
    {
        const QString blocks = QSettings().value("blocks-folder").toString();
        if (!blocks.isEmpty()) qputenv("FIELDES_BLOCKS", blocks.toLocal8Bit());
    }

    if (QFontDatabase::addApplicationFont(":/font/Inconsolata.otf") == -1) {
        std::cerr << "App: could not add application font";
    }

    Theme::load();          // (the set of colours that was chosen, light or dark, and the colours chosen by hand in it)
    Theme::apply(*this);
    setWindowIcon(Icons::logo(64));

    const Arguments args(this);
    if (args.app)
    {
        // A FielDes app: the form of its inputs, not the editor
        appWindow.reset(new AppWindow(args.appPath, args.appPresets, args.appRun));
        if (appWindow->isOpen()) appWindow->show();
        else QTimer::singleShot(0, this, &QCoreApplication::quit);
        return;
    }
    window.reset(new Window(args));
}

namespace {
// What the events of the window thread were since the last tick of the stall clock: for each kind of receiver and of event how many and how long in all (a stall is often thousands of events of 1 ms, none of which
// the 80 ms of App::notify sees)
struct EventStat { int count = 0; double ms = 0; };
std::map<std::pair<const QMetaObject*, int>, EventStat> g_eventStats;
}

void App::eventStatsReport(qint64 gap)
{
    if (gap > 250 && !g_eventStats.empty())
    {
        std::vector<std::pair<double, std::string>> rows;
        for (const auto& e : g_eventStats)
            rows.push_back({e.second.ms, std::to_string(e.second.count) + " events, " + std::to_string(int(e.second.ms)) + " ms in all: " + e.first.first->className() + " (event type " + std::to_string(e.first.second) + ")"});
        std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        std::cerr << "[event-stats] in the stall of " << gap << " ms, the events that ran in it:" << std::endl;
        for (size_t i = 0; i < rows.size() && i < 6; ++i) std::cerr << "[event-stats]   " << rows[i].second << std::endl;
    }
    g_eventStats.clear();
}

bool App::notify(QObject* receiver, QEvent* event)
{
    // With FIELDES_TIMING every event that the window's thread takes 80 ms or more to handle is written to the log, with who handled it (the class and the name of the receiver) and what it was: a stall
    // is then named by the object whose slot, timer or painting took the time.  (The stacks of the watchdog in main.cpp name no function that the map file does not list -- a static one --, and the
    // stalls of the stall test of 2026-11-03 had names in them that could not be right.  H90.)  The class and the name are taken before the event is handled: a receiver may be deleted by it
    static const bool timing = qEnvironmentVariableIsSet("FIELDES_TIMING");
    if (!timing || !receiver || QThread::currentThread() != thread()) return QApplication::notify(receiver, event);
    const char* className = receiver->metaObject()->className();
    const std::string name = receiver->objectName().toStdString();
    const int type = int(event->type());
    QElapsedTimer clock;
    clock.start();
    const QMetaObject* meta = receiver->metaObject();
    const bool handled = QApplication::notify(receiver, event);
    const qint64 ms = clock.elapsed();
    { EventStat& stat = g_eventStats[{meta, type}]; ++stat.count; stat.ms += double(clock.nsecsElapsed()) / 1e6; }
    if (ms >= 80)
        std::cerr << "[slow-event] " << ms << " ms in " << className << " '" << name << "' (event type " << type
                  << (type == int(QEvent::MetaCall) ? ", a queued call" : type == int(QEvent::Timer) ? ", a timer" : type == int(QEvent::Paint) ? ", painting" : "") << ")" << std::endl;
    return handled;
}

bool App::event(QEvent *event)
{
    if (event->type() == QEvent::FileOpen && window) {
        QFileOpenEvent *openEvent = static_cast<QFileOpenEvent*>(event);
        window->openFile(openEvent->file());
    }

    return QApplication::event(event);
}

}   // namespace FielDes
