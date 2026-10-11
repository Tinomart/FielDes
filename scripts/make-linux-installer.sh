#!/usr/bin/env bash
# Makes the installer of the Linux package: ONE file that unpacks FielDes where the user wants it and puts it in the application menu.
#
#   scripts/make-linux-installer.sh <FielDes-linux-x64.tar.gz> <installer.run> [version]
#
# The installer is a shell script with the package appended to it (so it needs nothing but a shell, tar and gzip, which every system has).
# What the user does:
#
#   sh FielDes-0.6.0-linux-x64-installer.run                     installs for the current user into ~/.local/share/FielDes, with a menu entry
#                                                                and the command `fieldes` in ~/.local/bin (no administrator rights)
#   sudo sh FielDes-0.6.0-linux-x64-installer.run --system       installs for everyone into /opt/FielDes, the menu entry in /usr/share/applications
#   sh FielDes-0.6.0-linux-x64-installer.run --prefix DIR        installs into DIR
#   sh FielDes-0.6.0-linux-x64-installer.run --uninstall         removes what an installation made
#
# The installation is one folder (the package as it is) plus the menu entry and the command; `uninstall.sh` in the folder removes exactly those.
set -euo pipefail

ARCHIVE="${1:?usage: make-linux-installer.sh <package.tar.gz> <installer.run> [version]}"
OUTFILE="${2:?usage: make-linux-installer.sh <package.tar.gz> <installer.run> [version]}"
VERSION="${3:-}"
[ -f "$ARCHIVE" ] || { echo "no such package: $ARCHIVE"; exit 1; }

{
cat <<'HEADER'
#!/bin/sh
# FielDes installer. Run it with `sh <this file>` (options: --prefix DIR, --system, --no-menu, --uninstall, --help).
set -e

NAME=FielDes
PREFIX=""
SYSTEM=0
MENU=1
UNINSTALL=0
VERSION="@VERSION@"

usage() {
    cat <<EOF
FielDes $VERSION installer

  sh $0                       install for the current user (~/.local/share/FielDes, menu entry, command 'fieldes')
  sh $0 --system              install for everyone (/opt/FielDes; needs administrator rights: run it with sudo)
  sh $0 --prefix DIR          install into DIR
  sh $0 --no-menu             do not make the menu entry and the command
  sh $0 --uninstall           remove an installation made by this installer (with --system or --prefix: that one)

FielDes needs nothing else installed: Qt, Python and its libraries are in the package.
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        --prefix) PREFIX="$2"; shift ;;
        --prefix=*) PREFIX="${1#--prefix=}" ;;
        --system) SYSTEM=1 ;;
        --no-menu) MENU=0 ;;
        --uninstall) UNINSTALL=1 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown option $1 (see --help)"; exit 2 ;;
    esac
    shift
done

if [ "$SYSTEM" = 1 ]; then
    [ -n "$PREFIX" ] || PREFIX=/opt/FielDes
    APPS=/usr/share/applications
    ICONS=/usr/share/icons/hicolor/256x256/apps
    BIN=/usr/local/bin
    if [ "$(id -u)" != 0 ]; then
        echo "A system-wide installation needs administrator rights: run  sudo sh $0 --system"
        exit 1
    fi
else
    DATA="${XDG_DATA_HOME:-$HOME/.local/share}"
    [ -n "$PREFIX" ] || PREFIX="$DATA/FielDes"
    APPS="$DATA/applications"
    ICONS="$DATA/icons/hicolor/256x256/apps"
    BIN="$HOME/.local/bin"
fi
case "$PREFIX" in /*) ;; *) PREFIX="$(pwd)/$PREFIX" ;; esac

# --- uninstall
if [ "$UNINSTALL" = 1 ]; then
    if [ -x "$PREFIX/uninstall.sh" ]; then
        "$PREFIX/uninstall.sh"
    else
        echo "No FielDes installation made by this installer in $PREFIX"
        exit 1
    fi
    exit 0
fi

# --- a folder that is not ours is never touched
if [ -e "$PREFIX" ] && [ ! -f "$PREFIX/.fieldes-install" ]; then
    echo "$PREFIX exists and is not a FielDes installation: nothing was changed."
    echo "Choose another place with --prefix DIR."
    exit 1
fi

# --- where the package starts in this file
START=$(grep -a -n '^__FIELDES_PACKAGE_BELOW__$' "$0" | head -n 1 | cut -d: -f1)
[ -n "$START" ] || { echo "this installer is damaged (no package in it)"; exit 1; }

echo "Installing FielDes $VERSION into $PREFIX ..."
OLD=""
if [ -d "$PREFIX" ]; then
    # (an upgrade: the old files are put aside, and removed when the new ones are in place)
    OLD="$PREFIX.old.$$"
    mv "$PREFIX" "$OLD"
fi
mkdir -p "$PREFIX"
if ! tail -n +"$((START + 1))" "$0" | tar -xzf - -C "$PREFIX" --strip-components=1; then
    echo "Unpacking failed: the installer file may be incomplete."
    rm -rf "$PREFIX"
    [ -n "$OLD" ] && mv "$OLD" "$PREFIX"
    exit 1
fi
[ -n "$OLD" ] && rm -rf "$OLD"
chmod +x "$PREFIX/FielDes" "$PREFIX/run.sh" 2>/dev/null || true
echo "$VERSION" > "$PREFIX/.fieldes-install"

# --- the menu entry, the icon and the command
if [ "$MENU" = 1 ]; then
    mkdir -p "$APPS" "$ICONS" "$BIN"
    ICON=fieldes
    if [ -f "$PREFIX/fieldes.png" ]; then
        cp -f "$PREFIX/fieldes.png" "$ICONS/fieldes.png"
    else
        ICON=""
    fi
    cat > "$APPS/fieldes.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=FielDes
GenericName=Field-driven design
Comment=CAD and simulation made of fields
Exec="$PREFIX/FielDes" %f
Terminal=false
Categories=Graphics;Engineering;Science;
StartupWMClass=FielDes
EOF
    [ -z "$ICON" ] || echo "Icon=$ICON" >> "$APPS/fieldes.desktop"
    ln -sf "$PREFIX/FielDes" "$BIN/fieldes"
    command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$APPS" >/dev/null 2>&1 || true
    command -v gtk-update-icon-cache >/dev/null 2>&1 && gtk-update-icon-cache -q -t "$(dirname "$(dirname "$ICONS")")" >/dev/null 2>&1 || true
fi

# --- what removes it again: the folder, the menu entry, the command
cat > "$PREFIX/uninstall.sh" <<EOF
#!/bin/sh
# Removes the FielDes installation in $PREFIX and the menu entry and command that came with it.
[ -f "$PREFIX/.fieldes-install" ] || { echo "$PREFIX is not a FielDes installation"; exit 1; }
rm -f "$APPS/fieldes.desktop" "$ICONS/fieldes.png"
[ "\$(readlink "$BIN/fieldes" 2>/dev/null)" = "$PREFIX/FielDes" ] && rm -f "$BIN/fieldes"
rm -rf "$PREFIX"
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$APPS" >/dev/null 2>&1 || true
echo "FielDes was removed."
EOF
chmod +x "$PREFIX/uninstall.sh"

echo
if [ "$MENU" = 1 ]; then
    echo "Done. Start FielDes from the application menu, or with the command  fieldes  (or $PREFIX/FielDes)."
    case ":$PATH:" in *":$BIN:"*) ;; *) echo "($BIN is not in your PATH: add it to use the command, or start FielDes from the menu.)" ;; esac
else
    echo "Done. Start FielDes with  $PREFIX/FielDes"
fi
echo "To remove it:  $PREFIX/uninstall.sh"
exit 0
__FIELDES_PACKAGE_BELOW__
HEADER
} | sed "s/@VERSION@/${VERSION:-unknown}/" > "$OUTFILE"

cat "$ARCHIVE" >> "$OUTFILE"
chmod +x "$OUTFILE"
echo "== installer: $OUTFILE ($(du -h "$OUTFILE" | cut -f1))"
