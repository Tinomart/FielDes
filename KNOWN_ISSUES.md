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

## The picture

- **A copy (an image of a symmetry, a turned copy) of an extrusion, meshed at resolution 2, can show small notches along its walls.** A finer resolution removes them.
- **While you drag a gizmo ring on a large model the picture is a coarse mesh**, which can look cut into layers; the fine mesh comes when you let go.

## Platforms

- **Windows** is the main platform. The setup program and the zip are not signed: Windows SmartScreen may say *unknown publisher* the first time; *More info ▸ Run anyway*
  opens it (the source is public, and the setup is built from it by the script in `scripts/`).
- **Linux** packages (a folder, a `.run` installer, a `.deb`) are built on Ubuntu 22.04 so that they open on older systems; they were tried under WSL2 on Fedora, not on every distribution.
- There is **no macOS** build.
