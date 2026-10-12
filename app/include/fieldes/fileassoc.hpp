/*
FielDes: field-driven design
Derived from Studio, a simple GUI for the libfive CAD kernel
Copyright (C) 2017-2021  Matt Keeter

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

#include <QString>

namespace FielDes {

/*
 *  The `.fieldesapp` file type: a double click on one opens it in FielDes's app window.  The installers register it for the machine's user;
 *  this does the same for a copy of the program that was not installed (a downloaded folder), or whose installer did not get to it.
 *
 *    Windows   HKEY_CURRENT_USER\Software\Classes: the extension, the file type with its icon, and the command that opens it
 *    Linux     a MIME type (application/x-fieldesapp) in ~/.local/share/mime, and a desktop entry that opens it, made the default
 *
 *  Nothing is changed for other users or outside the user's own folders, and no administrator is needed.
 */
namespace FileAssociation {

/*  Whether a double click on a `.fieldesapp` file opens this program (the one that is running)  */
bool isRegistered();

/*  Makes it so for the user; false, and `why` says why, when it could not be done  */
bool registerForUser(QString* why = nullptr);

}   // namespace FileAssociation

}   // namespace FielDes
