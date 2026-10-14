# Building from source

FielDes is built with CMake. The tested platform is **Windows 10/11 x64 with MSVC and vcpkg**; the build
files are portable (Qt, C++17, Eigen, Boost) and other platforms should work with little change, but they
have not been tried.

- [Windows](#windows)
- [The portable folder](#the-portable-folder)
- [What the build needs](#what-the-build-needs)
- [Other platforms](#other-platforms)
- [Running the library without the application](#running-the-library-without-the-application)
- [Regenerating the library reference](#regenerating-the-library-reference)
- [The scripted GUI (automation)](#the-scripted-gui-automation)

## Windows

One-time prerequisites:

1. **Visual Studio 2022 Build Tools** with the *C++ build tools* workload: MSVC v143, a Windows SDK, and
   *C++ CMake tools for Windows*. (Nothing else is needed — not the full IDE.)
2. **vcpkg**, cloned and bootstrapped **outside** OneDrive and other synced folders:

   ```
   git clone https://github.com/microsoft/vcpkg C:\dev\vcpkg
   C:\dev\vcpkg\bootstrap-vcpkg.bat -disableMetrics
   ```

3. The dependencies, through the build script (this builds Qt: allow about an hour, and several GB of disk):

   ```
   powershell -ExecutionPolicy Bypass -File scripts\build-windows.ps1 -InstallDeps
   ```

Then, for every build:

```
powershell -ExecutionPolicy Bypass -File scripts\build-windows.ps1
```

This configures CMake in `C:\dev\fieldes-build`, builds `FielDes.exe` and leaves it, with its DLLs, in
`C:\dev\fieldes-build\app\Release`. A full build takes several minutes (the script uses `/MP`); an
incremental one less. Parameters: `-Vcpkg`, `-BuildDir`, `-Config`, `-PackageDir`.

To start it from the build folder: `C:\dev\fieldes-build\app\Release\FielDes.exe` needs to find its Python
package and runtime. It looks for `python\fieldes` in the folders above itself, and for a Python runtime in
`runtime\python3` (the portable folder) or `vcpkg\installed\x64-windows\tools\python3` next to a vcpkg
checkout. Set `PYTHONPATH` to the `python` folder of the source tree and `FIELDES_DIR` to the build
folder if it does not find them.

## The portable folder

```
powershell -ExecutionPolicy Bypass -File scripts\build-windows.ps1 -Package
```

builds and then mirrors everything into `dist\FielDes`:

(A build tree that was configured with `-DFIELDES_OUTPUT_DIR=<...>\dist\FielDes` -- the tree a developer works in: the exe is linked straight into the folder -- is packaged as it is: the exe and the
kernel's DLLs are there already, new, so the first step copies only what is newer or missing instead of mirroring `app\Release` over them, and nothing is deleted. A tree that builds into any other
folder is refused, with the reason.)

```
FielDes.exe, fieldes.dll, fieldes-stdlib.dll, manifold.dll, Qt5*.dll, plugins\, …
runtime\python3\        the private Python runtime
python\fieldes\         the Python library
examples\               the examples and their STEP files (without caches)
blocks\                 the custom blocks folder (the sample blocks; see docs\blocks.md)
translations\           the program's texts in Spanish, French, German, Japanese, Chinese and Russian (see translations\README.md)
docs\                   these documents
README.md, LICENSE, LICENSE-MPL-2.0, NOTICE.md, CHANGELOG.md
```

plus the MSVC runtime DLLs, so it runs without the VC++ redistributable. Zip the folder to distribute it.
(Importing a STEP file writes its cache next to the file; if the folder is read-only the import still
works, just without a cache.)

## The installer

```
powershell -ExecutionPolicy Bypass -File installer\build-installer.ps1
```

turns the portable folder into one `dist\FielDes-<version>-windows-x64-setup.exe` (about 30 MB): it asks for the language (Windows' by default) and the
folder, installs for the current user (no administrator rights), makes the Start menu shortcut and the entry in Settings → Apps, and uninstalls
(`uninstall.exe`, which removes only the files the installation wrote, listed in `install.manifest`; what you added to the folder stays). `/S` installs silently;
the switches are at the top of `installer\Setup.cs` (`/language=xx`, `/dir=…`, `/nodesktop`, `/nolaunch`). With its window it starts FielDes **when Finish is clicked** (if the box *Start FielDes when the installation is finished* is ticked), not
while it is still working. It is built with the C# compiler of the
Visual Studio Build Tools (or the .NET Framework's `csc.exe`: nothing to download) from the portable folder as `dist\FielDes` is after `-Package`; the version is
the one of `CMakeLists.txt`. It is **not code-signed**: Windows SmartScreen shows "unknown publisher" the first time it runs (*More info → Run anyway*); a
code-signing certificate and `signtool` at the end of `build-installer.ps1` would remove that.

## What the build needs

| Component | Used for | vcpkg port |
|---|---|---|
| Qt 5 (Core, Gui, Widgets, OpenGL, Network, Concurrent) ≥ 5.12 | the application | `qt5-base` |
| Python 3 (Development) | the embedded interpreter (and the private runtime) | `python3` |
| Eigen 3 | linear algebra in the kernel | `eigen3` |
| Boost (container, bimap, interval, lockfree, functional, algorithm, math) | containers, numerics | `boost-*` |
| libpng | screenshots | `libpng` |
| Manifold | polygon triangulation and mesh booleans | `manifold` |

The kernel needs no CAD library: the STEP reader and the tessellator are part of it.

The CMake options: `BUILD_APP` (default ON; OFF builds only the kernel and its C++ shapes) and
`ENABLE_DEBUG` (symbols, non-MSVC).

## Linux

`scripts/build-linux.sh` builds the kernel, the Python library and the application, and with `--package` makes a **self-contained** portable
folder (`~/FielDes-linux`, and `dist/FielDes-linux-x64.tar.gz` in the repository). **It opens on a Linux desktop with nothing installed beforehand**: Qt and its
plugins, the kernel's libraries, a Python runtime with its standard library and every other library the program loads are inside the folder
(`lib/`, `plugins/`, `runtime/python3/`, found through `qt.conf`, the programs' run paths and `PYTHONHOME`, which the program sets itself); only the C library and the
graphics driver of the machine are used. Unpack it, double-click `FielDes` (or run `./FielDes`; `run.sh` does the same). It was made
and tested on Fedora under WSL2 (Windows 11, WSLg shows the window).

**The installers.** `--package` also makes `dist/FielDes-<version>-linux-x64-installer.run` (`scripts/make-linux-installer.sh`: a shell script with the package appended -- it needs only a shell and
tar) and `dist/FielDes_<version>_amd64.deb` (`scripts/make-deb.sh`, written with `ar` and `tar`, so `dpkg-deb` is not needed). `sh FielDes-<version>-linux-x64-installer.run` installs for the current user into
`~/.local/share/FielDes` (a menu entry, the command `fieldes` in `~/.local/bin`, and an `uninstall.sh` in the folder); `--system` (with `sudo`) installs into `/opt/FielDes` for everyone, `--prefix DIR` anywhere, `--uninstall`
removes it again, and a folder that is not a FielDes installation is never touched. `sudo apt install ./FielDes_<version>_amd64.deb` puts the program in `/opt/fieldes`, the command in `/usr/bin` and the menu entry in
the system's. The installer was tried under WSL (install, upgrade over it, refuse a foreign folder, uninstall); the `.deb` was built and its parts checked, but not installed with dpkg.

**The one thing a package cannot bring is the C library.** A program asks for a glibc as new as the one it was built with, so a package built on Fedora 44
(glibc 2.43) opens only on a system that new. The script says at its end which glibc the package asks for; to open on nearly everything **build on an old
system** -- Ubuntu 22.04 (glibc 2.35) is what `.github/workflows/release.yml` uses: run it on GitHub (Actions → Release builds → Run workflow, or push a tag
such as `v0.6.1`), and the Linux package comes out as an artifact, after a check that starts it in a bare Ubuntu with only a virtual display. Install once:

```
# Fedora
sudo dnf install qt5-qtbase-devel qt5-qtbase-private-devel boost-devel eigen3-devel libpng-devel python3-devel \
    manifold-devel tbb-devel mesa-libGL-devel mesa-libGLU-devel libatomic cmake ninja-build gcc-c++ pkgconf-pkg-config rsync patchelf
# Ubuntu / Debian
sudo apt install qtbase5-dev qtbase5-private-dev libqt5opengl5-dev libboost-dev libeigen3-dev libpng-dev python3-dev \
    libtbb-dev libgl-dev libglu1-mesa-dev cmake ninja-build g++ pkg-config rsync patchelf git
```

then `scripts/build-linux.sh --package`. When the sources are on a Windows drive (`/mnt/c/...`) they are copied to the Linux
file system first (compiling from there is several times faster). Manifold and Clipper2 are not packaged by every distribution
(Fedora has Manifold but not Clipper2; Ubuntu 22.04 has neither): the script builds what it cannot find once into `~/fieldes-deps`. The first build takes a good while (the kernel is large) and each compiler needs about
a gigabyte: on a machine with 8 GB use `--jobs 4`, or the build thrashes; later builds only rebuild what changed. Under WSL, `~/FielDes-linux/FielDes` opens the window
through WSLg.

## Other platforms

Install the dependencies with your package manager (Eigen ≥ 3.2.92 via pkg-config, Boost, libpng, Qt5,
Python 3 development files, Manifold) and

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target FielDes -j
```

Expect to fix small things.

## Running the library without the application

The Python library needs only `fieldes.dll` (and its dependencies: `manifold.dll`):

```
set FIELDES_DIR=C:\dev\fieldes-build\app\Release
python scripts\run_example.py examples\05_static_analysis.py
```

`run_example.py` runs a script statement by statement with its own folder as the working directory. It prints
what the script prints and the time it took; nothing is drawn, `view.set_*` calls are remembered only, and
`var(3)` is the plain number 3. Any Python 3.8+ works; there is nothing to install.

## Before a release: cold against warm

```
python scripts\cold_vs_warm.py
```

Runs every example three times -- with the caches as they are shipped beside the STEP files, with every cache deleted, and again right after that, with the caches the second run made --
each in a copy of the examples folder with cache folders of its own, so nothing of yours is read or written, and compares what each run prints and a hash of the field of every shape it made
(`run_example.py --digest`). A difference means a cache that does not give back what the program makes now (or a result that is not repeatable): the stale-cache class of bug, which no test of a single
run finds. It takes a while (the analyses and the optimisations run three times); `python scripts\cold_vs_warm.py 05_static_analysis 16_fluid_flow` runs the ones named. The exit code is 1 when anything differs or fails.

## Regenerating the library reference

```
python scripts\gen_reference.py
```

writes `docs/reference.md` from the docstrings of the library. Run it after changing a docstring.

## The scripted GUI (automation)

A developer facility used to test the application without a person at the keyboard: set `FIELDES_AUTOMATION`
to a text file of commands (`wait <ms>`, `action <name>`, `grab <file.png>`, `quit`, …; see
`app/include/fieldes/automation.hpp`) and start `FielDes.exe script.py`. It is how the screenshots in the
documentation are made. Among the commands: `treedump <file>` (the model tree as indented text, with each row's kind),
`treedrop <source> > <target> > <above|on|below|end>` (a drag and drop in the tree, sent as the mouse events of a hand),
`treerename <row> > <name>` (rename by double-click, or `f2`), `setting <fn> <index> <text>` (a render setting's field), `tour ...`
(the guided tour, step by step), `treeclick`, `viewmouse`, `popup` (the context menus), `grabmenus`, `grabwidget`, `grabcrop`, `outdump` (the output pane), `blocksfolder <path>`.
