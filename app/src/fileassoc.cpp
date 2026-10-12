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
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTextStream>

#include "fieldes/fileassoc.hpp"

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#endif

namespace FielDes {
namespace FileAssociation {

#ifdef _WIN32

namespace {

QString exePath()
{
    return QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
}

// The text value of a registry key's default entry (empty when there is none)
QString readDefault(HKEY root, const QString& subKey)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, reinterpret_cast<LPCWSTR>(subKey.utf16()), 0, KEY_READ, &key) != ERROR_SUCCESS) return QString();
    wchar_t buffer[2048];
    DWORD size = sizeof buffer, type = 0;
    QString out;
    if (RegQueryValueExW(key, nullptr, nullptr, &type, reinterpret_cast<LPBYTE>(buffer), &size) == ERROR_SUCCESS && type == REG_SZ)
        out = QString::fromWCharArray(buffer);
    RegCloseKey(key);
    return out;
}

bool writeDefault(const QString& subKey, const QString& value)
{
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, reinterpret_cast<LPCWSTR>(("Software\\Classes\\" + subKey).utf16()), 0, nullptr, 0, KEY_WRITE, nullptr,
                        &key, nullptr) != ERROR_SUCCESS)
        return false;
    const bool ok = RegSetValueExW(key, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.utf16()), DWORD((value.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
    RegCloseKey(key);
    return ok;
}

}   // anonymous namespace

bool isRegistered()
{
    // (the merged view of the machine's and the user's registrations is what a double click goes by)
    if (readDefault(HKEY_CLASSES_ROOT, ".fieldesapp") != "FielDes.App") return false;
    const QString command = readDefault(HKEY_CLASSES_ROOT, "FielDes.App\\shell\\open\\command");
    return command.compare("\"" + exePath() + "\" \"%1\"", Qt::CaseInsensitive) == 0;
}

bool registerForUser(QString* why)
{
    const QString exe = exePath();
    bool ok = writeDefault(".fieldesapp", "FielDes.App");
    ok = ok && writeDefault("FielDes.App", "FielDes app");
    ok = ok && writeDefault("FielDes.App\\DefaultIcon", "\"" + exe + "\",0");
    ok = ok && writeDefault("FielDes.App\\shell\\open", "Open");
    ok = ok && writeDefault("FielDes.App\\shell\\open\\command", "\"" + exe + "\" \"%1\"");
    if (!ok)
    {
        if (why) *why = "The registry of this user could not be written.";
        return false;
    }
    // (Explorer is told that the associations have changed, so the icon and the double click work at once)
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return true;
}

#else

namespace {

QString mimeFile() { return QDir::homePath() + "/.local/share/mime/packages/application-x-fieldesapp.xml"; }
QString desktopFile() { return QDir::homePath() + "/.local/share/applications/fieldes-app.desktop"; }

bool writeText(const QString& path, const QString& text)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream out(&f);
    out.setCodec("UTF-8");
    out << text;
    return true;
}

}   // anonymous namespace

bool isRegistered()
{
    QFile f(desktopFile());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    return QString::fromUtf8(f.readAll()).contains("Exec=\"" + QCoreApplication::applicationFilePath() + "\" %f");
}

bool registerForUser(QString* why)
{
    const QString exe = QCoreApplication::applicationFilePath();
    const bool ok =
        writeText(mimeFile(),
                  "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                  "<mime-info xmlns=\"http://www.freedesktop.org/standards/shared-mime-info\">\n"
                  "  <mime-type type=\"application/x-fieldesapp\">\n"
                  "    <comment>FielDes app</comment>\n"
                  "    <glob pattern=\"*.fieldesapp\"/>\n"
                  "    <icon name=\"fieldes\"/>\n"
                  "  </mime-type>\n"
                  "</mime-info>\n") &&
        writeText(desktopFile(),
                  "[Desktop Entry]\n"
                  "Type=Application\n"
                  "Name=FielDes app\n"
                  "Exec=\"" + exe + "\" %f\n"
                  "MimeType=application/x-fieldesapp;\n"
                  "NoDisplay=true\n"
                  "Terminal=false\n");
    if (!ok)
    {
        if (why) *why = "The files of this user's MIME database could not be written.";
        return false;
    }
    // (best effort: a system without these tools reads the files at its next start)
    QProcess::execute("update-mime-database", {QDir::homePath() + "/.local/share/mime"});
    QProcess::execute("update-desktop-database", {QDir::homePath() + "/.local/share/applications"});
    QProcess::execute("xdg-mime", {"default", "fieldes-app.desktop", "application/x-fieldesapp"});
    return true;
}

#endif

}   // namespace FileAssociation
}   // namespace FielDes
