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
#include <QFileOpenEvent>
#include <QSettings>

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

    Theme::apply(*this);
    setWindowIcon(Icons::logo(64));

    window.reset(new Window(Arguments(this)));
}

bool App::event(QEvent *event)
{
    if (event->type() == QEvent::FileOpen) {
        QFileOpenEvent *openEvent = static_cast<QFileOpenEvent*>(event);
        window->openFile(openEvent->file());
    }

    return QApplication::event(event);
}

}   // namespace FielDes
