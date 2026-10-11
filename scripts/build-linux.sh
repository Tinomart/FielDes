#!/usr/bin/env bash
# Builds FielDes on Linux (made and tested on Fedora 44 under WSL2; Ubuntu / Debian package names are below) and, with --package, makes the
# portable folder that opens on any x86-64 Linux desktop with nothing installed beforehand.
#
#   scripts/build-linux.sh [--package] [--jobs N] [--build-dir DIR] [--no-app] [--out DIR]
#
#   --package     also makes the portable folder: FielDes with everything it needs inside -- Qt and its plugins, the libraries of the kernel, a Python
#                 runtime with its standard library, the Python package, the examples, the blocks and the docs -- in --out (default: ~/FielDes-linux)
#                 and as dist/FielDes-linux-x64.tar.gz in the repository.  Only the C library and the graphics driver of the machine are used
#   --build-dir   where the build goes (default: ~/fieldes-build)
#   --no-app      the kernel and the Python library only (no Qt)
#
# THE PORTABLE FOLDER NEEDS A C LIBRARY AS NEW AS THE ONE IT WAS BUILT WITH: a package made on Fedora 44 (glibc 2.43) opens only on a system with
# that glibc or newer.  For a package that opens almost everywhere, build on an OLD system (Ubuntu 22.04, glibc 2.35: what the release workflow in
# .github/workflows/release.yml does).  The script says at the end which glibc the package asks for.
#
# When the sources are on a Windows drive (/mnt/c/...), they are copied to the Linux file system first: compiling from there
# is several times faster than from the Windows drive.
#
# What it needs (install once):
#   Fedora:         sudo dnf install qt5-qtbase-devel qt5-qtbase-private-devel boost-devel eigen3-devel libpng-devel python3-devel \
#                       manifold-devel tbb-devel mesa-libGL-devel mesa-libGLU-devel libatomic cmake ninja-build gcc-c++ pkgconf-pkg-config rsync patchelf
#   Ubuntu/Debian:  sudo apt install qtbase5-dev qtbase5-private-dev libqt5opengl5-dev libboost-dev libeigen3-dev libpng-dev \
#                       python3-dev libtbb-dev libgl-dev libglu1-mesa-dev cmake ninja-build g++ pkg-config rsync patchelf git
#                   (Manifold and Clipper2 are not in every release -- Ubuntu 22.04 has neither: this script builds what it cannot find into ~/fieldes-deps)
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
        -h|--help) sed -n 2,30p "$0"; exit 0 ;;
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

# Clipper2 (the polygon clipping of Manifold) and Manifold itself (the polygon triangulation of the kernel) are not packaged by every
# distribution (Fedora has Manifold, not Clipper2; Ubuntu 22.04 neither): what cmake cannot find is built once from its sources into ~/fieldes-deps
DEPS="$HOME/fieldes-deps"
mkdir -p "$DEPS"
have_package() {
    (cd "$HOME" && cmake --find-package -DNAME="$1" -DCOMPILER_ID=GNU -DLANGUAGE=CXX -DMODE=EXIST) >/dev/null 2>&1 && return 0
    pkg-config --exists "$(echo "$1" | tr 'A-Z' 'a-z')" 2>/dev/null && return 0
    [ -n "$(find /usr/lib64/cmake /usr/lib/cmake /usr/lib/*-linux-gnu/cmake /usr/local/lib/cmake /usr/local/lib64/cmake -maxdepth 1 -iname "$1" 2>/dev/null | head -1)" ]
}
if ! have_package Clipper2 && [ ! -f "$DEPS/lib64/cmake/Clipper2/Clipper2Config.cmake" ] && [ ! -f "$DEPS/lib/cmake/Clipper2/Clipper2Config.cmake" ]; then
    echo "== building Clipper2 into $DEPS"
    rm -rf "$HOME/clipper2-src"
    git clone --depth 1 --branch Clipper2_1.5.4 https://github.com/AngusJohnson/Clipper2.git "$HOME/clipper2-src"
    cmake -S "$HOME/clipper2-src/CPP" -B "$HOME/clipper2-src/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
          -DCMAKE_INSTALL_PREFIX="$DEPS" -DCLIPPER2_UTILS=OFF -DCLIPPER2_EXAMPLES=OFF -DCLIPPER2_TESTS=OFF \
          -DBUILD_SHARED_LIBS=ON -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    cmake --build "$HOME/clipper2-src/build" --parallel "$JOBS"
    cmake --install "$HOME/clipper2-src/build"
fi
if ! have_package manifold && [ ! -f "$DEPS/lib64/cmake/manifold/manifoldConfig.cmake" ] && [ ! -f "$DEPS/lib/cmake/manifold/manifoldConfig.cmake" ]; then
    echo "== building Manifold into $DEPS"
    rm -rf "$HOME/manifold-src"
    git clone --depth 1 --branch v3.0.1 https://github.com/elalish/manifold.git "$HOME/manifold-src"
    cmake -S "$HOME/manifold-src" -B "$HOME/manifold-src/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
          -DCMAKE_INSTALL_PREFIX="$DEPS" -DCMAKE_PREFIX_PATH="$DEPS" -DBUILD_SHARED_LIBS=ON -DMANIFOLD_PAR=OFF -DMANIFOLD_TEST=OFF \
          -DMANIFOLD_PYBIND=OFF -DMANIFOLD_CBIND=OFF -DMANIFOLD_JSBIND=OFF -DMANIFOLD_EXPORT=OFF -DMANIFOLD_DEBUG=OFF
    cmake --build "$HOME/manifold-src/build" --parallel "$JOBS"
    cmake --install "$HOME/manifold-src/build"
fi

echo "== configuring ($BUILD)"
cmake -S "$SRC" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_APP=$APP -DCMAKE_PREFIX_PATH="$DEPS"
echo "== building with $JOBS jobs"
cmake --build "$BUILD" --parallel "$JOBS"
echo "== built"

if [ "$PACKAGE" = 1 ]; then
    echo "== packaging into $OUT"
    rm -rf "$OUT"
    mkdir -p "$OUT/lib"
    # (the kernel's libraries, the application, the Python package, the examples with one sample STEP file (the others are not distributed), the
    # blocks, the docs, the translations)
    cp -f "$BUILD"/kernel/src/fieldes.so "$BUILD"/kernel/stdlib/fieldes-stdlib.so "$OUT"/
    [ "$APP" = ON ] && cp -f "$BUILD"/app/FielDes "$OUT"/
    rsync -a --exclude '__pycache__' "$SRC/python" "$OUT/"
    mkdir -p "$OUT/examples"
    rsync -a --exclude '__pycache__' --include 'step/PivotBearingSupportBracket.STEP' --exclude 'step/*.s*p' --exclude 'step/*.STEP' --exclude 'step/*.STP' --exclude '*.fieldes-cache.*' \
          --exclude 'meshes' "$SRC/examples/" "$OUT/examples/"
    rsync -a "$SRC/blocks" "$SRC/docs" "$SRC/translations" "$OUT/"
    cp -f "$SRC"/README.md "$SRC"/CHANGELOG.md "$SRC"/LICENSE-GPL-2.0 "$SRC"/LICENSE-MPL-2.0 "$SRC"/NOTICE.md "$OUT"/
    cp -f "$SRC"/app/deploy/icon/fieldes.png "$OUT"/fieldes.png          # (the icon of the menu entry the installers make)

    # ---- A Python of its own: its standard library next to the program (the program sets PYTHONHOME to runtime/python3 when it is there)
    PYVER=$(python3 -c 'import sys; print("%d.%d" % sys.version_info[:2])')
    PYLIB="$OUT/runtime/python3/lib/python$PYVER"
    mkdir -p "$PYLIB"
    # (the system's own scheme, not the one for what is installed by hand under /usr/local; some distributions keep the compiled modules in a
    # lib64 next to the pure ones, others together)
    PURE=$(python3 -c 'import sysconfig; print(sysconfig.get_path("stdlib", "posix_prefix"))')
    PLAT=$(python3 -c 'import sysconfig; print(sysconfig.get_path("platstdlib", "posix_prefix"))')
    for d in "$PURE" "$PLAT"; do
        [ -d "$d" ] || continue
        rsync -a --exclude '__pycache__' --exclude 'test' --exclude 'tests' --exclude 'idlelib' --exclude 'tkinter' --exclude 'turtledemo' \
              --exclude 'ensurepip' --exclude 'lib2to3' --exclude 'site-packages' --exclude 'config-*' --exclude 'turtle.py' \
              --exclude 'EXTERNALLY-MANAGED' "$d/" "$PYLIB/"
    done
    # (Python looks for its library in <home>/<platlibdir>/python3.x: "lib64" on Fedora, "lib" on Debian -- both lead to the same folder)
    PLATLIB=$(python3 -c 'import sys; print(getattr(sys, "platlibdir", "lib"))')
    [ "$PLATLIB" != lib ] && ln -sfn lib "$OUT/runtime/python3/$PLATLIB"
    rm -f "$PYLIB"/lib-dynload/_tkinter*.so "$PYLIB"/lib-dynload/_curses*.so "$PYLIB"/lib-dynload/_gdbm*.so "$PYLIB"/lib-dynload/_dbm*.so \
          "$PYLIB"/lib-dynload/_sqlite3*.so "$PYLIB"/lib-dynload/readline*.so "$PYLIB"/lib-dynload/_testcapi*.so 2>/dev/null || true

    # ---- Qt's plugins (the platform, the image formats, the icons)
    QTPLUG=""
    for q in qmake-qt5 qmake; do
        if command -v "$q" >/dev/null 2>&1; then QTPLUG=$("$q" -query QT_INSTALL_PLUGINS 2>/dev/null || true); [ -d "$QTPLUG/platforms" ] && break; fi
    done
    if [ ! -d "$QTPLUG/platforms" ]; then
        QTPLUG=$(find /usr/lib* /usr/lib/*-linux-gnu -maxdepth 3 -type d -path '*qt5/plugins' 2>/dev/null | head -1)
    fi
    if [ "$APP" = ON ]; then
        [ -d "$QTPLUG/platforms" ] || { echo "cannot find the Qt plugins"; exit 1; }
        mkdir -p "$OUT/plugins"
        for d in platforms imageformats iconengines platforminputcontexts xcbglintegrations wayland-decoration-client wayland-graphics-integration-client \
                 wayland-shell-integration; do
            [ -d "$QTPLUG/$d" ] && cp -a "$QTPLUG/$d" "$OUT/plugins/"
        done
        # (found next to the program, whatever else Qt is told)
        printf '[Paths]\nPlugins = plugins\n' > "$OUT/qt.conf"
    fi

    # ---- Every shared library all of that needs, except what only the machine can give: the C library, the graphics driver and the X11 core
    # libraries it talks to (the driver brings its own: two copies in one program would not agree), and the few that are tied to the system's
    # services.  Collected until nothing new turns up
    EXCLUDE='^(ld-linux.*|linux-vdso.*|libc\.so.*|libm\.so.*|libdl\.so.*|libpthread\.so.*|librt\.so.*|libutil\.so.*|libresolv\.so.*|libnsl\.so.*|libanl\.so.*|libBrokenLocale\.so.*|libGL\.so.*|libGLX\.so.*|libGLdispatch\.so.*|libEGL\.so.*|libOpenGL\.so.*|libGLESv2\.so.*|libdrm.*|libgbm\.so.*|libvulkan\.so.*|libnvidia.*|libcuda.*|libglapi\.so.*|libLLVM.*|libsystemd\.so.*|libselinux\.so.*|libudev\.so.*|libasound\.so.*|libwayland-.*\.so.*|libX11\.so.*|libX11-xcb\.so.*|libxcb\.so.*|libXau\.so.*|libXdmcp\.so.*|libXext\.so.*|libXfixes\.so.*)$'
    collect() {
        local todo=("$@") f dep base
        while [ ${#todo[@]} -gt 0 ]; do
            f=${todo[0]}
            todo=("${todo[@]:1}")
            while read -r dep; do
                base=$(basename "$dep")
                if [[ $base =~ $EXCLUDE ]]; then continue; fi
                [ "$(dirname "$dep")" = "$OUT" ] && continue          # (the kernel's own libraries stay where they are, beside the program)
                [ -e "$OUT/lib/$base" ] && continue
                cp -L "$dep" "$OUT/lib/$base"
                todo+=("$OUT/lib/$base")
            done < <(LD_LIBRARY_PATH="$OUT/lib:$OUT:${LD_LIBRARY_PATH:-}" ldd "$f" 2>/dev/null | sed -n 's/.*=> \(\/[^ ]*\) .*/\1/p')
        done
    }
    ELFS=("$OUT"/fieldes.so "$OUT"/fieldes-stdlib.so)
    [ "$APP" = ON ] && ELFS+=("$OUT"/FielDes)
    while IFS= read -r f; do ELFS+=("$f"); done < <(find "$OUT/plugins" "$OUT/runtime" -name '*.so*' -type f 2>/dev/null)
    collect "${ELFS[@]}"
    # (the library Clipper2 built here, and Manifold, are found by ldd through the build's own paths: copied by the same loop)

    # ---- The libraries find each other next to themselves
    rp() { patchelf --set-rpath "$1" "$2" 2>/dev/null || true; }
    [ "$APP" = ON ] && rp '$ORIGIN:$ORIGIN/lib' "$OUT/FielDes"
    rp '$ORIGIN:$ORIGIN/lib' "$OUT/fieldes.so"
    rp '$ORIGIN:$ORIGIN/lib' "$OUT/fieldes-stdlib.so"
    for f in "$OUT"/lib/*; do [ -f "$f" ] && [ ! -L "$f" ] && rp '$ORIGIN' "$f"; done
    while IFS= read -r f; do
        depth=$(echo "${f#$OUT/}" | tr -cd '/' | wc -c)               # (how many folders deep it is: the way back to lib)
        up=$(printf '../%.0s' $(seq 1 "$depth"))
        rp "\$ORIGIN/${up}lib" "$f"
    done < <(find "$OUT/plugins" "$OUT/runtime" -name '*.so*' -type f 2>/dev/null)

    cat > "$OUT/run.sh" <<'EOF'
#!/usr/bin/env bash
# Starts FielDes from this folder (it also starts by itself: the program finds its libraries, Qt and Python next to itself).
here=$(cd "$(dirname "$0")" && pwd)
export FIELDES_DIR="$here"
exec "$here/FielDes" "$@"
EOF
    chmod +x "$OUT/run.sh"
    mkdir -p "$REPO/dist"
    tar -C "$(dirname "$OUT")" -czf "$REPO/dist/FielDes-linux-x64.tar.gz" "$(basename "$OUT")"

    # ---- The installers: one file that installs it (and removes it again), and a Debian / Ubuntu package
    VERSION=$(sed -n 's/^project(FielDes VERSION \([0-9.]*\).*/\1/p' "$REPO/CMakeLists.txt")
    bash "$REPO/scripts/make-linux-installer.sh" "$REPO/dist/FielDes-linux-x64.tar.gz" "$REPO/dist/FielDes-$VERSION-linux-x64-installer.run" "$VERSION"
    if command -v ar >/dev/null 2>&1; then
        bash "$REPO/scripts/make-deb.sh" "$OUT" "$REPO/dist/FielDes_${VERSION}_amd64.deb" "$VERSION"
    fi

    # ---- What the package still asks of the machine, for the one who builds it
    echo "== what the program still takes from the machine:"
    LD_LIBRARY_PATH= ldd "$OUT/FielDes" 2>/dev/null | sed -n 's/.*=> \(\/[^ ]*\) .*/\1/p' | grep -v "^$OUT" | sed 's/^/     /' || true
    NEWEST=$( (objdump -T "$OUT/FielDes" "$OUT"/fieldes.so "$OUT"/lib/*.so* 2>/dev/null | grep -o 'GLIBC_[0-9.]*' | sort -V | tail -1) || true)
    echo "== it needs ${NEWEST:-glibc} or newer; build on an older system (Ubuntu 22.04) to open on older machines"
    echo "== portable folder: $OUT"
    echo "== archive: $REPO/dist/FielDes-linux-x64.tar.gz"
    echo "   run it:  $OUT/FielDes   (or $OUT/run.sh)"
fi
