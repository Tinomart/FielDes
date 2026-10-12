/*
FielDes: field-driven design
Derived from Studio, a simple GUI for the libfive CAD kernel
Copyright (C) 2018  Matt Keeter

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

#include <QCommandLineParser>

#include "fieldes/args.hpp"
#include "fieldes/editor.hpp"

namespace FielDes {

Arguments::Arguments(QCoreApplication* application)
{
    // An app: its file first (or `app` and the file); what follows are values of its form, which the parser below does not know
    {
        const QStringList all = application->arguments();
        int at = 1;
        if (all.size() > at && all[at] == "app") ++at;
        if (all.size() > at && (at > 1 || all[at].endsWith(".fieldesapp", Qt::CaseInsensitive)))
        {
            app = true;
            appPath = all[at];
            for (int i = at + 1; i < all.size(); ++i)
            {
                if (all[i] == "--run") appRun = true;
                else appPresets << all[i];
            }
            language = Language::LANGUAGE_PYTHON;
            return;
        }
    }

    QCommandLineParser parser;
    parser.setApplicationDescription("FielDes: field-driven design");
    parser.addHelpOption();
    parser.addPositionalArgument("filename", "Script to load");

    QCommandLineOption vertical_layout("vertical", "Use vertical layout for editor / 3D view.");
    parser.addOption(vertical_layout);

    parser.process(*application);

    const QStringList ps = parser.positionalArguments();
    filename = ps.isEmpty() ? "" : ps[0];

    vertical = parser.isSet(vertical_layout);
    language = Language::LANGUAGE_PYTHON;
}

}   // namespace FielDes
