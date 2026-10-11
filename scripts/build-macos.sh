#!/usr/bin/env bash
# Builds FielDes on macOS (Intel or Apple silicon) and packages it as FielDes.app, with Qt, Python and every library inside.
#
#   scripts/build-macos.sh [--jobs N] [--build-dir DIR] [--out DIR] [--dmg] [--pkg]
#
#   --out    where FielDes.app goes (default: ./dist/macos); --dmg also makes dist/FielDes-macos-<arch>.dmg (the disk image to drag
#            FielDes.app from, to Applications); --pkg also makes dist/FielDes-macos-<arch>.pkg (an installer: double-click, Continue, Install)
#
# NOT YET TRIED ON A MAC: it follows what the Linux build does and what Qt's own tools do for a bundle; the first run on a real machine (or the
# release workflow's) is where it will be corrected.  What it needs (Homebrew, once):
#   brew install cmake ninja qt@5 boost eigen libpng python@3.12 tbb pkg-config manifold clipper2 dylibbundler create-dmg
#
# The bundle is not signed by a developer: it is signed ad hoc (which Apple silicon requires to run at all), so a copy downloaded from the internet
# is opened the first time with a right-click on the icon, Open.
set -euo pipefail

JOBS=$(sysctl -n hw.ncpu)
REPO=$(cd "$(dirname "$0")/.." && pwd)
BUILD="$REPO/build-macos"
OUT="$REPO/dist/macos"
DMG=0
PKG=0
while [ $# -gt 0 ]; do
    case "$1" in
        --jobs) JOBS="$2"; shift ;;
        --build-dir) BUILD="$2"; shift ;;
        --out) OUT="$2"; shift ;;
        --dmg) DMG=1 ;;
        --pkg) PKG=1 ;;
        -h|--help) sed -n 2,16p "$0"; exit 0 ;;
        *) echo "unknown option $1"; exit 2 ;;
    esac
    shift
done

BREW=$(brew --prefix)
QT=$(brew --prefix qt@5)
PY=$(brew --prefix python@3.12)
PYVER=3.12
ARCH=$(uname -m)

echo "== configuring ($BUILD)"
cmake -S "$REPO" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_APP=ON \
      -DCMAKE_PREFIX_PATH="$QT;$BREW" -DPython3_ROOT_DIR="$PY" -DPython3_FIND_FRAMEWORK=LAST
echo "== building with $JOBS jobs"
cmake --build "$BUILD" --parallel "$JOBS"

APP="$OUT/FielDes.app"
MACOS="$APP/Contents/MacOS"
echo "== bundling into $APP"
rm -rf "$APP"
mkdir -p "$MACOS" "$APP/Contents/Resources" "$APP/Contents/Frameworks"
cp -f "$BUILD/app/FielDes" "$MACOS/FielDes"
cp -f "$BUILD"/kernel/src/fieldes.dylib "$BUILD"/kernel/stdlib/fieldes-stdlib.dylib "$MACOS/"
rsync -a --exclude '__pycache__' "$REPO/python" "$MACOS/"
mkdir -p "$MACOS/examples"
rsync -a --exclude '__pycache__' --include 'step/PivotBearingSupportBracket.STEP' --exclude 'step/*.s*p' --exclude 'step/*.STEP' --exclude 'step/*.STP' \
      --exclude '*.fieldes-cache.*' --exclude 'meshes' "$REPO/examples/" "$MACOS/examples/"
rsync -a "$REPO/blocks" "$REPO/docs" "$REPO/translations" "$MACOS/"
cp -f "$REPO"/README.md "$REPO"/CHANGELOG.md "$REPO"/LICENSE-GPL-2.0 "$REPO"/LICENSE-MPL-2.0 "$REPO"/NOTICE.md "$MACOS/"

# ---- A Python of its own: runtime/python3 next to the program holds the standard library (the program sets PYTHONHOME to it)
PYLIB="$MACOS/runtime/python3/lib/python$PYVER"
mkdir -p "$PYLIB"
rsync -a --exclude '__pycache__' --exclude 'test' --exclude 'tests' --exclude 'idlelib' --exclude 'tkinter' --exclude 'turtledemo' --exclude 'ensurepip' \
      --exclude 'lib2to3' --exclude 'site-packages' --exclude 'config-*' "$PY/Frameworks/Python.framework/Versions/$PYVER/lib/python$PYVER/" "$PYLIB/"

# ---- The icon of the app: the picture of the icon in the sizes a .icns holds (macOS's own tools)
ICONSET="$(mktemp -d)/fieldes.iconset"
mkdir -p "$ICONSET"
SRCPNG="$REPO/app/deploy/icon/fieldes.png"
for s in 16 32 64 128 256 512; do sips -z "$s" "$s" "$SRCPNG" --out "$ICONSET/s$s.png" >/dev/null; done
cp "$ICONSET/s16.png" "$ICONSET/icon_16x16.png";   cp "$ICONSET/s32.png" "$ICONSET/icon_16x16@2x.png"
cp "$ICONSET/s32.png" "$ICONSET/icon_32x32.png";   cp "$ICONSET/s64.png" "$ICONSET/icon_32x32@2x.png"
cp "$ICONSET/s128.png" "$ICONSET/icon_128x128.png"; cp "$ICONSET/s256.png" "$ICONSET/icon_128x128@2x.png"
cp "$ICONSET/s256.png" "$ICONSET/icon_256x256.png"; cp "$ICONSET/s512.png" "$ICONSET/icon_256x256@2x.png"
rm -f "$ICONSET"/s*.png
iconutil -c icns "$ICONSET" -o "$APP/Contents/Resources/fieldes.icns" || echo "(no icon: iconutil failed)"

cat > "$APP/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>FielDes</string>
  <key>CFBundleDisplayName</key><string>FielDes</string>
  <key>CFBundleIdentifier</key><string>org.fieldes.FielDes</string>
  <key>CFBundleExecutable</key><string>FielDes</string>
  <key>CFBundleIconFile</key><string>fieldes</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>$(sed -n 's/^project(FielDes VERSION \([0-9.]*\).*/\1/p' "$REPO/CMakeLists.txt")</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>NSPrincipalClass</key><string>NSApplication</string>
  <key>LSMinimumSystemVersion</key><string>11.0</string>
</dict>
</plist>
EOF

# ---- Qt: the frameworks and plugins the program uses are copied into the bundle and its references changed (Qt's own tool)
"$QT/bin/macdeployqt" "$APP" -verbose=1
# ---- Everything else the program, the kernel's libraries and the Python modules link to (TBB, Manifold, Clipper2, libpng, libpython ...)
SEARCH=""
for d in "$BREW/lib" "$PY/lib" "$PY/Frameworks/Python.framework/Versions/$PYVER/lib"; do SEARCH="$SEARCH -s $d"; done
BUNDLE_ARGS="-x $MACOS/FielDes -x $MACOS/fieldes.dylib -x $MACOS/fieldes-stdlib.dylib"
for m in "$PYLIB"/lib-dynload/*.so; do BUNDLE_ARGS="$BUNDLE_ARGS -x $m"; done
# shellcheck disable=SC2086
dylibbundler -od -b $BUNDLE_ARGS -d "$APP/Contents/Frameworks" -p @executable_path/../Frameworks/ $SEARCH
# (the kernel's libraries are found next to the program, as ever)
codesign --force --deep --sign - "$APP"
echo "== $APP"

if [ "$DMG" = 1 ]; then
    rm -f "$REPO/dist/FielDes-macos-$ARCH.dmg"
    create-dmg --volname FielDes --window-size 520 320 --icon FielDes.app 130 150 --app-drop-link 390 150 "$REPO/dist/FielDes-macos-$ARCH.dmg" "$APP" \
        || hdiutil create -volname FielDes -srcfolder "$APP" -ov -format UDZO "$REPO/dist/FielDes-macos-$ARCH.dmg"
    echo "== $REPO/dist/FielDes-macos-$ARCH.dmg"
fi

if [ "$PKG" = 1 ]; then
    # (an installer package: it puts FielDes.app in Applications.  It is not signed by a developer: the first time it is opened with a right-click, Open)
    VERSION=$(sed -n 's/^project(FielDes VERSION \([0-9.]*\).*/\1/p' "$REPO/CMakeLists.txt")
    rm -f "$REPO/dist/FielDes-macos-$ARCH.pkg"
    pkgbuild --component "$APP" --install-location /Applications --identifier org.fieldes.FielDes --version "$VERSION" \
             "$REPO/dist/FielDes-macos-$ARCH.pkg"
    echo "== $REPO/dist/FielDes-macos-$ARCH.pkg"
fi
