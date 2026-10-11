#!/usr/bin/env bash
# Makes a Debian / Ubuntu package (.deb) from the Linux package folder:
#
#   scripts/make-deb.sh <the FielDes-linux folder or its .tar.gz> <out.deb> [version]
#
# It installs FielDes into /opt/fieldes, the command `fieldes` into /usr/bin, and the menu entry and icon where the desktop looks for them;
# `sudo apt install ./FielDes_0.6.0_amd64.deb` (or double-click it in the software centre) does it.  It depends on nothing but the C library
# (Qt, Python and the other libraries are inside) -- which one, is what the build machine had: build on Ubuntu 22.04 for the oldest.
#
# The .deb is written with ar and tar (a .deb is an ar archive of debian-binary, control.tar.gz and data.tar.gz), so dpkg-deb is not needed.
set -euo pipefail

SRC="${1:?usage: make-deb.sh <folder or .tar.gz> <out.deb> [version]}"
OUT="${2:?usage: make-deb.sh <folder or .tar.gz> <out.deb> [version]}"
VERSION="${3:-0.0.0}"
mkdir -p "$(dirname "$OUT")"
OUT="$(cd "$(dirname "$OUT")" && pwd)/$(basename "$OUT")"          # (the way back from the folder ar works in)

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

# --- the folder
if [ -d "$SRC" ]; then
    FOLDER="$SRC"
else
    mkdir -p "$WORK/unpacked"
    tar -xzf "$SRC" -C "$WORK/unpacked"
    FOLDER=$(find "$WORK/unpacked" -mindepth 1 -maxdepth 1 -type d | head -n 1)
fi
[ -x "$FOLDER/FielDes" ] || { echo "no FielDes program in $FOLDER"; exit 1; }

# --- what goes where
ROOT="$WORK/root"
mkdir -p "$ROOT/opt" "$ROOT/usr/bin" "$ROOT/usr/share/applications" "$ROOT/usr/share/icons/hicolor/256x256/apps"
cp -a "$FOLDER" "$ROOT/opt/fieldes"
rm -f "$ROOT/opt/fieldes/run.sh"
ln -s /opt/fieldes/FielDes "$ROOT/usr/bin/fieldes"
[ -f "$FOLDER/fieldes.png" ] && cp "$FOLDER/fieldes.png" "$ROOT/usr/share/icons/hicolor/256x256/apps/fieldes.png"
cat > "$ROOT/usr/share/applications/fieldes.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=FielDes
GenericName=Field-driven design
Comment=CAD and simulation made of fields
Exec=/opt/fieldes/FielDes %f
Icon=fieldes
Terminal=false
Categories=Graphics;Engineering;Science;
StartupWMClass=FielDes
EOF

# --- the control files
SIZE=$(du -sk "$ROOT" | cut -f1)
CONTROL="$WORK/control"
mkdir -p "$CONTROL"
cat > "$CONTROL/control" <<EOF
Package: fieldes
Version: $VERSION
Architecture: amd64
Maintainer: FielDes developers <noreply@users.noreply.github.com>
Installed-Size: $SIZE
Section: graphics
Priority: optional
Homepage: https://github.com/Tinomart/FielDes
Description: FielDes, field-driven design
 CAD, finite-element analysis, flow and optimisation made of fields: every shape is a field, and wherever a number goes, a field goes.
 Qt, Python and every library it needs are inside the package.
EOF
cat > "$CONTROL/postinst" <<'EOF'
#!/bin/sh
set -e
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database -q /usr/share/applications || true
command -v gtk-update-icon-cache >/dev/null 2>&1 && gtk-update-icon-cache -q -t /usr/share/icons/hicolor || true
exit 0
EOF
cat > "$CONTROL/postrm" <<'EOF'
#!/bin/sh
set -e
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database -q /usr/share/applications || true
exit 0
EOF
chmod 755 "$CONTROL/postinst" "$CONTROL/postrm"
(cd "$ROOT" && find . -type f ! -path './DEBIAN/*' -exec md5sum {} + | sed 's# \./# #') > "$CONTROL/md5sums"

# --- the archive: debian-binary, control.tar.gz, data.tar.gz, in this order, with ar
mkdir -p "$WORK/deb"
printf '2.0\n' > "$WORK/deb/debian-binary"
tar --owner=0 --group=0 --numeric-owner -C "$CONTROL" -czf "$WORK/deb/control.tar.gz" .
tar --owner=0 --group=0 --numeric-owner -C "$ROOT" -czf "$WORK/deb/data.tar.gz" .
rm -f "$OUT"
(cd "$WORK/deb" && ar rc "$OUT" debian-binary control.tar.gz data.tar.gz)
echo "== package: $OUT ($(du -h "$OUT" | cut -f1)); install with  sudo apt install ./$(basename "$OUT")"
