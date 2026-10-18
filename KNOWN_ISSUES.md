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

## Manufacturing

What each of these is, and the way round it, is in [docs/manufacturing.md](docs/manufacturing.md); the measured numbers are in `tests/RELEASE_REPORT.md`.

- **A notch the size of the growth is left where thin walls meet.** The shell thickens the faces of a thin wall, up to their edges: along an edge where two thin walls meet (the corner of a thin box) and in the inside corner where
  a thin wall stands on a thick face, a notch is left (0.15 mm for a wall of 0.5 asked to be 0.8), and the check marks a few hundredths of a per cent of the surface there. Ask `min_radius` as well and the corners are rounded.
- **The corner rule reads the field's value, which is a distance only where the field is one.** A part made by taking boxes out of a block with cutters that overshoot it, or imported from STEP, has only a lower bound of
  the distance inside its slots, and the check marks the walls under a slot's mouth although a cutter of that radius fits: read such a part through `exact_distance(part)` first.
- **A corner is marked only from about 17 % short of the radius asked** (under 2.5 mm for 3 mm), not from the 5 % of the other rules: the check reads how far the centre of the ball sits from the part, which in a corner of 90
  degrees grows by 0.29 mm for every mm of the corner's radius. Between that and the radius asked (2.5 to 2.85 mm for 3) the reading is within its noise and a corner may or may not be marked, in stripes along the arc. The shell rounds a
  corner to exactly the radius asked; a tight arc like that, read through a mesh, can show a few points short.
- **A thin plate that leans a few degrees past the overhang limit is given a gusset at its foot only.** The gusset is made of the points whose whole cone, at the angle asked, lies in the part as it was, and in a plate 2 mm thick
  that fits only a quarter of a millimetre from the foot; the rest of the face is left, and listed in `fail_states` and on the card. A thicker part or a wider ledge is filled whole.
- **A section through a result of the teardrop (`overhang_fix='cut'`) shows a faint hairline.** The cut remedy's field has a crease one reach and an inset over the plate (0.14 mm from zero, so it is no surface), and
  the section view draws it as a few short dashes across the cut face; the surface of the part, seen from outside, is clean.
- **Small leftovers are listed after a good remedy:** a strip a tenth of a millimetre wide where a gusset or a fillet ends on the part's own face, a few samples of five thousand. The card says how much of the surface they are.
- **Fine features are read through steps of a thousandth of the part's size** (never under 0.004 mm): a slit narrower than two steps, or a round hole whose radius is three steps, is not read, and an undercut read at 17 heights
  can miss a shelf thinner than a sixteenth of the part's length. Draw a small part in a scene of its own.
- **A free build direction takes the plate at the lowest corner of the part's box**, so a face laid on it at an angle is not taken to rest there and the search prefers the three axes. To print a tilted part on its flat face,
  turn the part until that face is level and name the axis.
- **A layer thinner than one cell of the picture is counted but not drawn.** The card says how much material was added to a fin thickened by 0.15 mm, and at 2 cells to the millimetre (cells of 0.5 mm) the red layer is not to be
  seen: the mesher cannot draw a sheet thinner than its cell. Draw such a part with at least two cells across the layer, or look at a section.
- **A picture finer than the walls need can run the viewer out of memory**: the remedies are fields of many shifted copies of the part's, and the mesher needs memory in proportion to the cube of the resolution. Draw at four
  cells across the thinnest wall and no finer.
- **A result with several remedies is slow to draw at a fine resolution.** At 3 cells to the millimetre a ledge with its gusset takes about 40 s to draw (the tool: 1.5 s), a block with a round hole and its teardrop about 33 s, and example 33 (a thin fin, a post and a ledge) 45 to 55 s
  (tool 9 s); the wall remedy of a fin of 0.6 mm is the slowest part of that (about 20 lookups of the part for every cell near the fin). The cost grows with the square of the resolution: at 2 cells to the millimetre it is about 2.3 times quicker. A bigger part costs the number of lookups times its own size.

## The picture

- **While you drag a handle or a gizmo ring on a large model the picture is the quick, coarse level of the mesher** (it keeps up with the hand); the fine mesh comes when you let go.

## Platforms

- **Windows** is the main platform. The setup program and the zip are not signed: Windows SmartScreen may say *unknown publisher* the first time; *More info ▸ Run anyway*
  opens it (the source is public, and the setup is built from it by the script in `scripts/`).
- **Linux** packages (a folder, a `.run` installer, a `.deb`) are built on Ubuntu 22.04 so that they open on older systems; they were tried under WSL2 on Fedora, not on every distribution.
- There is **no macOS** build.
