# Known issues

What does not work as it should yet, so that you do not lose time on it. The limits that come from the design are in the README (*Known limitations*) and in
[docs/analysis.md](docs/analysis.md#limitations). A problem that is not here: **Help ▸ Report a bug** opens a new issue with the version and your system filled in.

## Drawing

- **The filled face of a profile with round corners is a mesh of triangles.** Its blue outline is the arc itself, at any zoom; the fill behind it is cut into 8 pieces to a
  quarter turn, so zoomed in far it shows facets a pixel or so inside the outline. Everything that is made from the profile (`extrude`, `offset`, `shell`, a simulation)
  sees the exact arc.
- **The distance to a spline is measured to straight pieces**, 14 to a segment: a cubic curve has no closed form for its distance. An arc has one and is exact. The outline
  of a spline is drawn as the curve.
- **A round corner on a line, or on the path of a sweep, is a hard corner.** The radius belongs to the corners of a profile.

## Analysis

- **The 10-node element (`element='tet10'`) is for static and modal analysis.** Topology optimisation, thermal and flow analysis have no 10-node version, and `export_fea` has no model of it. Its conjugate-gradient
  solve needs up to twice the iterations of the linear element's, so a modal analysis with it is slow (a beam of 7,000 nodes: 19 s); a better preconditioner would help.

## The picture

- **While you drag a handle or a gizmo ring on a large model the picture is the quick, coarse level of the mesher** (it keeps up with the hand); the fine mesh comes when you let go.

## Platforms

- **Windows** is the main platform. The setup program and the zip are not signed: Windows SmartScreen may say *unknown publisher* the first time; *More info ▸ Run anyway*
  opens it (the source is public, and the setup is built from it by the script in `scripts/`).
- **Linux** packages (a folder, a `.run` installer, a `.deb`) are built on Ubuntu 22.04 so that they open on older systems; they were tried under WSL2 on Fedora, not on every distribution.
- There is **no macOS** build.
