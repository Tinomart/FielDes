#!/usr/bin/env bash
# Builds FielDes on Linux (made and tested on Fedora 44 under WSL2; Ubuntu / Debian package names are below).
#
#   scripts/build-linux.sh [--package] [--jobs N] [--build-dir DIR] [--no-app] [--out DIR]
#
#   --package     also makes the portable folder: FielDes, its libraries, the Python package, the examples, the blocks and
#                 the docs, in --out (default: ~/FielDes-linux) and as dist/FielDes-linux-x64.tar.gz in the repository
#   --build-dir   where the build goes (default: ~/fieldes-build)
#   --no-app      the kernel and the Python library only (no Qt)
#
# When the sources are on a Windows drive (/mnt/c/...), they are copied to the Linux file system first: compiling from there
# is several times faster than from the Windows drive.
#
# What it needs (install once):
#   Fedora:         sudo dnf install qt5-qtbase-devel qt5-qtbase-private-devel boost-devel eigen3-devel libpng-devel python3-devel \
#                       manifold-devel tbb-devel mesa-libGL-devel mesa-libGLU-devel libatomic cmake ninja-build gcc-c++ pkgconf-pkg-config rsync patchelf
#   Ubuntu/Debian:  sudo apt install qtbase5-dev qtbase5-private-dev libqt5opengl5-dev libboost-dev libeigen3-dev libpng-dev \
#                       python3-dev libmanifold-dev libtbb-dev libgl-dev libglu1-mesa-dev cmake ninja-build g++ pkg-config rsync patchelf
#                   (libmanifold-dev is not in every release: then build manifold from https://github.com/elalish/manifold)
set -euo pipefail

PACKAGE=0
APP=ON
JOBS=$(nproc)
BUILD="$HOME/fieldes-build"
OUT="$HOME/FielDes-linux"
while [ $# -gt 0 ]; do
    case "$1" in
        --package) PACKAGE=1 ;;
        --no-app) APP=OFF ;;
        --jobs) JOBS="$2"; shift ;;
        --build-dir) BUILD="$2"; shift ;;
        --out) OUT="$2"; shift ;;
        -h|--help) sed -n 2,22p "$0"; exit 0 ;;
        *) echo "unknown option $1"; exit 2 ;;
    esac
    shift
done

REPO=$(cd "$(dirname "$0")/.." && pwd)
SRC="$REPO"
case "$REPO" in
    /mnt/*)
        SRC="$HOME/fieldes-src"
        mkdir -p "$SRC"
        echo "== copying the sources to $SRC"
        rsync -a --delete --exclude '/dist' --exclude '/.git' --exclude '__pycache__' \
              --exclude '*.fieldes-cache.*' --include '/examples/step/README.md' --include '/examples/step/PivotBearingSupportBracket.STEP' \
              --exclude '/examples/step/*' --exclude '/examples/meshes' "$REPO/" "$SRC/"
        ;;
esac

# Manifold (the polygon triangulation of the kernel) needs Clipper2, which not every distribution packages (Fedora does not):
# built once from its sources into ~/fieldes-deps when cmake cannot find it
DEPS="$HOME/fieldes-deps"
if ! (cd "$HOME" && cmake --find-package -DNAME=Clipper2 -DCOMPILER_ID=GNU -DLANGUAGE=CXX -DMODE=EXIST) >/dev/null 2>&1 \
        && [ ! -f "$DEPS/lib64/cmake/Clipper2/Clipper2Config.cmake" ] && [ ! -f "$DEPS/lib/cmake/Clipper2/Clipper2Config.cmake" ]; then
    echo "== building Clipper2 into $DEPS"
    rm -rf "$HOME/clipper2-src"
    git clone --depth 1 --branch Clipper2_1.5.4 https://github.com/AngusJohnson/Clipper2.git "$HOME/clipper2-src"
    cmake -S "$HOME/clipper2-src/CPP" -B "$HOME/clipper2-src/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
          -DCMAKE_INSTALL_PREFIX="$DEPS" -DCLIPPER2_UTILS=OFF -DCLIPPER2_EXAMPLES=OFF -DCLIPPER2_TESTS=OFF \
          -DBUILD_SHARED_LIBS=ON -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    cmake --build "$HOME/clipper2-src/build" --parallel "$JOBS"
    cmake --install "$HOME/clipper2-src/build"
fi

echo "== configuring ($BUILD)"
cmake -S "$SRC" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_APP=$APP -DCMAKE_PREFIX_PATH="$DEPS"
echo "== building with $JOBS jobs"
cmake --build "$BUILD" --parallel "$JOBS"
echo "== built"

if [ "$PACKAGE" = 1 ]; then
    echo "== packaging into $OUT"
    rm -rf "$OUT"
    mkdir -p "$OUT"
    # (the kernel's libraries, the application, the Python package, the examples with one sample STEP file (the others are not distributed), the blocks, the docs)
    cp -f "$BUILD"/kernel/src/fieldes.so "$BUILD"/kernel/stdlib/fieldes-stdlib.so "$OUT"/
    [ "$APP" = ON ] && cp -f "$BUILD"/app/FielDes "$OUT"/
    rsync -a --exclude '__pycache__' "$SRC/python" "$OUT/"
    mkdir -p "$OUT/examples"
    rsync -a --exclude '__pycache__' --include 'step/PivotBearingSupportBracket.STEP' --exclude 'step/*.s*p' --exclude 'step/*.STEP' --exclude 'step/*.STP' --exclude '*.fieldes-cache.*' \
          --exclude 'meshes' "$SRC/examples/" "$OUT/examples/"
    rsync -a "$SRC/blocks" "$SRC/docs" "$OUT/"
    cp -f "$SRC"/README.md "$SRC"/CHANGELOG.md "$SRC"/LICENSE-GPL-2.0 "$SRC"/LICENSE-MPL-2.0 "$SRC"/NOTICE.md "$OUT"/
    # (what was built here and is not a system library: Clipper2)
    cp -fa "$DEPS"/lib*/libClipper2*.so* "$OUT"/ 2>/dev/null || true
    # (the libraries find each other next to themselves)
    for f in "$OUT"/FielDes "$OUT"/fieldes.so "$OUT"/fieldes-stdlib.so "$OUT"/libClipper2*.so*; do
        [ -f "$f" ] && patchelf --set-rpath '$ORIGIN' "$f"
    done
    cat > "$OUT/run.sh" <<'EOF'
#!/usr/bin/env bash
# Starts FielDes from this folder (it finds its libraries and the Python package next to itself).
here=$(cd "$(dirname "$0")" && pwd)
export FIELDES_DIR="$here"
exec "$here/FielDes" "$@"
EOF
    chmod +x "$OUT/run.sh"
    mkdir -p "$REPO/dist"
    tar -C "$(dirname "$OUT")" -czf "$REPO/dist/FielDes-linux-x64.tar.gz" "$(basename "$OUT")"
    echo "== portable folder: $OUT"
    echo "== archive: $REPO/dist/FielDes-linux-x64.tar.gz"
    echo "   run it:  $OUT/run.sh"
fi
