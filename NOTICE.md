# Notices

## FielDes

FielDes — field-driven design. Copyright © the FielDes authors.

FielDes is built on **libfive**, copyright © Matt Keeter and contributors
([github.com/libfive/libfive](https://github.com/libfive/libfive)), and on its GUI **Studio**. The parts of
libfive that FielDes still contains are marked in their file headers and keep their licence; the work
added on top is offered under the same licence as the files it belongs to.

| Part | Licence | File |
|---|---|---|
| `kernel/` (the C++ kernel) and `python/` (the Python library) | **Mozilla Public License 2.0** | [LICENSE-MPL-2.0](LICENSE-MPL-2.0) |
| `app/` (the Qt application) | **GNU General Public License, version 2 or (at your option) any later version** | [LICENSE-GPL-2.0](LICENSE-GPL-2.0) |
| `examples/*.py`, `docs/`, `scripts/` | Mozilla Public License 2.0 | [LICENSE-MPL-2.0](LICENSE-MPL-2.0) |

A combined work (the `FielDes.exe` application together with the kernel and the Python library) is
distributed under the terms of the GPL, which the MPL-2.0 permits for files of the kernel and the library
(MPL-2.0 §3.3 secondary-licence compatibility).

## Third-party components

Distributed in the portable folder, or needed to build FielDes. Each keeps its own licence; the libraries
shipped as DLLs are used dynamically and can be replaced by a compatible build.

| Component | Used for | Licence |
|---|---|---|
| [Qt 5](https://www.qt.io) (Core, Gui, Widgets, OpenGL, Network, Concurrent) | the application's user interface | LGPL-3.0 / GPL-2.0 / GPL-3.0 (dynamically linked; Qt's source and licence texts are at qt.io) |
| [Python 3](https://www.python.org) | the embedded interpreter and the private runtime in `runtime/python3` | Python Software Foundation License |
| [Eigen 3](https://eigen.tuxfamily.org) | linear algebra in the kernel | MPL-2.0 |
| [Boost](https://www.boost.org) | containers and numerics (header libraries) | Boost Software License 1.0 |
| [Manifold](https://github.com/elalish/manifold) | polygon triangulation and mesh booleans | Apache-2.0 |
| [libpng](http://www.libpng.org) and zlib | screenshots | libpng licence, zlib licence |
| [Inconsolata](https://github.com/googlefonts/Inconsolata) | the editor font (`app/font`) | SIL Open Font License 1.1 (`app/font/SIL Open Font License.txt`) |
| FreeType, HarfBuzz, brotli, bzip2, double-conversion, libjpeg, PCRE2, md4c | Qt's own dependencies, shipped as DLLs | FreeType Licence / MIT / BSD-style licences of each project |

The texts of these licences are in the vcpkg ports' `copyright` files (`vcpkg/installed/x64-windows/share/<port>/copyright`)
and on the projects' sites.

## Example models

The examples were written against sample STEP parts downloaded from GrabCAD. They are **not part of
FielDes and are not distributed with it** (neither in this repository nor in the portable package): they are
not covered by the licences above, and their authors' terms are not known to allow redistribution.
[`examples/step/README.md`](examples/step/README.md) lists the file names the examples expect.

## No warranty

FielDes is provided **as is**, without warranty of any kind. The finite-element, thermal and optimisation
tools are linear, simplified design aids, not certified solvers: do not use their results as the sole basis
for a decision that affects safety.
