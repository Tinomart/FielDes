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

## The model tree and the code editor

- **A model made inside a block of Python is a locked row of that block, not a model row.** An `if`, a loop, a function or a class at the top level is a row of the tree and shows what is in it
  (the variables it assigns, the names its header reads, its parameters), but what is inside is plain code: its models have no eye, no handles and no gizmo in the tree, and they are drawn
  only when the script shows them at the top level (`b` under the loop). Rows are made down to eight levels and at most 80 to a level (a longer body ends in a row that says how many more).
- **The insertion point is known by the model it is above, and by the line a Python block starts on.** An edit made elsewhere leaves it above the same model; above a block of Python an edit
  that moves the block (a statement deleted above it) puts the point back at the end of the script until the cursor or a click puts it again. A point inside a block of code, or a tree that
  is not of the text in the editor (the script was edited a moment ago), writes at the end of the script, as it did before the point existed.
- **Ctrl+E extrudes, so Export mesh is F7 and Ctrl+Alt+E.** It had Ctrl+E. (Settings ▸ Keyboard shortcuts changes any of them.)

## Analysis

- **The 10-node element (`element='tet10'`) is for static and modal analysis.** Topology optimisation, thermal and flow analysis have no 10-node version, and `export_fea` has no model of it. Its conjugate-gradient
  solve needs up to twice the iterations of the linear element's, so a modal analysis with it is slow (a beam of 7,000 nodes: 19 s); a better preconditioner would help.

## Manufacturing

What each of these is, and the way round it, is in [docs/manufacturing.md](docs/manufacturing.md); the measured numbers are in `tests/RELEASE_REPORT.md`.

- **On the topology-optimized bracket the tool's work for a mill and for a mould is only partly good, as the kit's independent judge measures it** (`tests/manufacturing/mfg_j8_bracket_three_processes_direct.py`, 10-20, real window, the part handed to the tool as the optimizer made it, 24 checks, 4 fail).
  For the 3D printer the whole job is clean (overhang 5.7 % of the surface before, 4.8 % after, wall and gap unchanged). For the mill the undercut fill (4,369 mm^3) and the inside-corner fill (1,608 mm^3) leave the
  surface that breaks the undercut rule at 9.2 % where it was 8.3 % (inside the noise of the count) and the surface that breaks the inside-radius rule at 23.5 % where it was 19.8 %. For the mould the draft share falls
  from 85.4 % to 36.3 % (and the tool does not list the rest), but the wall rule is broken on 1.5 % of the surface where it was 0.3 % and the undercut rule on 13.5 % where it was 8.3 %. The new surface that the fills
  and the draft make breaks the rules of its own process in places. Why was not found out; the draft is measured from the top of the whole part (a question that is the maintainer's, put in item 30 of `tests/RELEASE_REPORT.md`), and the corner fill adds about three
  times what the formula adds. A result of the tool on a real part should be read with `manufacturing_analysis`, which is independent of the tool, and not taken as clean because the tool listed nothing: the tool's own
  re-check is off unless asked for.

- **A notch the size of the growth is left where thin walls meet.** The shell thickens the faces of a thin wall, up to their edges: along an edge where two thin walls meet (the corner of a thin box) and in the inside corner where
  a thin wall stands on a thick face, a notch is left (0.15 mm for a wall of 0.5 asked to be 0.8), and the check marks a few hundredths of a per cent of the surface there. Ask `min_radius` as well and the corners are rounded.
- **The corner rule reads the field's value, which is a distance only where the field is one.** A part made by taking boxes out of a block with cutters that overshoot it, or imported from STEP, has only a lower bound of
  the distance inside its slots, and the check marks the walls under a slot's mouth although a cutter of that radius fits: read such a part through `exact_distance(part)` first.
- **`fillet`, `round_edges` and `exact_distance` read the shape's own field (nothing is meshed), and are as good as the field is a distance.** Measured on parts with true distance fields: the distance to a
  face, edge and corner of a box is exact to 0.003 mm, a box offset by 2 is 0.05 % off, the fillet of an L (radius 3) adds 15.51 mm^3 where the arc says 15.45, a rounded box 0.2 % over, a plate thinner than the ball is
  removed whole, a slot narrower than the ball is filled, and an L draws in 1.9 s. They are less exact where the field is not a distance, which is the case of a part made by taking cutters that overshoot the block out of
  it, or by overlapping pieces: a point where a walk along the gradient does not end on the surface is left as it is (the field there describes walls that are not the part's), which can leave a corner of such a part
  rounded a little short (the pocket of 1.5 mm corners made 3 mm: 38 mm^3 added where the arcs say 69). Three known differences from the exact rolling ball: **a slot narrower than the ball is filled flush with its
  mouth** (the ball resting on its two edges would leave a sag, 0.76 mm for a slot of 4 and a ball of 3; a slot narrower than the ball by less than 2 % of its radius counts as fitting; over the shoulder of a lower block beside
  a wall the top of the fill follows the exact closing to about a millimetre -- the CNC sampler's gap between its fin and its block: 13.5 against 12.8; and where a pocket was cut by a cutter that overshoots the block, the
  corner of a CURVE tighter than the ball -- a pocket of corner radius 1.5 asked to be 3 -- is rounded exactly over the middle of its height and not at all within 1 to 2 mm of the pocket's mouth, where the field
  carries the cutter's own end cap, which is not part of the part: a flat corner reaches the end faces), the material added at the end of a corner reaches up to 0.4 mm past the part's end face, and the check marks
  small regions (0.4 % of the surface in `mfg_a3_pocket_corners`) along the top and bottom rim of the pockets, even where the corner is at the radius asked or rounder (the mesh version marked none). The
  cause is the cutter that overshoots the block: within about 1.3 mm of the open end of a through pocket the part's own field is the distance to the cutter's end cap, which is not part of the part, so
  the distance at the centre of the ball reads 1.9 mm at 0.2 mm from the rim and 2.8 mm at 1.2 mm (3.0 from 2 mm on, which is right) and the check finds that the ball does not fit. A walk along the
  gradient cannot get past a face that is not there (it ends where the foot test says the face is not real); the cure is a part whose cutters stop at the block or a field that is a distance.
  **A gap within 5 % of the cutter's size is on the rule's own tolerance (`GRACE` = 5 %) and is neither filled nor left:** the ball cannot go into it, so its middle is filled, but the weight that says "no ball fits" ramps in from 2 % to 6 % of the radius, so beside the
  walls a slit of up to about 1 mm stays open (the CNC sampler's first gap, 5.7 mm for a 6 mm cutter: 0.82 mm of air at 1 mm from the L, where the exact closing is solid). A gap clearly narrower (5.4 mm in the sampler now) is filled whole.
  **What the six corner proofs show on the final code (10-19 21:25):** `mfg_a3_pocket_corners` marks the too-tight pockets (12 % of the surface) and one more region of 0.011 % at the outer end wall of the block, an outside corner
  (1 of 7 checks fails: found 2 regions where at least 3 were expected, one of them outside every pocket); `mfg_c2_corner_angles` leaves one floating point of material at (73.4, 13.0, 10.1) and the independent check marks 20 % of the surface
  after the remedy (1.9 % before), at the creases where the arcs meet the faces; `mfg_c3` and `mfg_c4` mark 15 % and 14 % after (4.5 % and 14 % before), `mfg_c1` and `mfg_c5` 1.5 % and 0.7 %. None of these was understood to the end: the marks
  are the independent check reading the result through `exact_distance`, whose walk is not exact at a crease.
- **The topology bracket made ready for a mould draws in 12 seconds after a tool of 91 seconds, with limits.** Measured: the mould job on the topology bracket (element 5, the optimized part handed to the tool directly, plates and lugs kept, ABS wall 1.5, draft 1 degree; one run in a real window, 10-20 00:10): the tool 90.9 s and the first complete drawing 12.1 s more (coarse level 2.7 s, fine level 9.2 s), 104 s in all; before the lattices the tool took 55.3 s and the drawing never finished (coarse level 158.8 s, the fine level not done at 420 s). 12,335 mm^3 added (8.6 %): walls 219, corners 207, draft 7,811, undercut 4,369. The topology optimization's own result draws in about 33 s. The undercut fill, the draft, the walls and the corners are lattices of the kernel now, as the overhang is (CHANGELOG, docs/manufacturing.md). **What is not as it should be:** (1) the tool's own 91 s: the four lattices each sample the part near its surface separately, one shared sample would cut that to about a third (not done; the progress bar moves one step for each remedy); (2) the corners add 207 mm^3 where the formula adds about 62, three times as much: the first corner fill, which trusted the part's slope, left a skin of shards (1,490 mm^3), it is renormalised by the slope now and the shards are gone from the pictures, but whether the rest is fill that the formula leaves out was not looked into; (3) against the formula the draft adds 3 % less (7,811 against 8,090 mm^3) and the walls 43 % more (219 against 153); (4) the mill job, the corner fill against the exact bisectors and the draft against the moulding sampler's hand answers were not run on this code; (5) a part held as `exact_distance` is still slow in the tool (below); (6) two ways of asking the corner FORMULA only near the corners (a zone found on the lattice) made the drawing slower, not faster (coarse level 114 s and 131 s): they stay in the code, off (`FIELDES_CORNERS_ZONE=1`).
- **A part held as `exact_distance(...)` is slow through the manufacturing tool and in the drawing of its results.** The distance is a walk of about 32 calls of the part's field at every point, and the tool asks
  the part 20 to 120 times at every point of its result: measured on a small part (plate, fin, post, wall, ledge; printer rules), the plain part takes 6.7 s in the tool and draws in 8.5 s, held as
  `exact_distance` the same tool takes 295 s and the drawing of its result does not end in 20 minutes (example 33's part: tool 282 s, the drawing was at 4 % after 35 minutes). The result is the same (483.3 mm^3
  added both ways); drawing the distance itself is not slow (`exact_distance(part)` shown on its own draws as the part does: the viewport meshes the part's own field, which has the same surface). Hand the tool the
  part's own field when you can; use `exact_distance` where a rule reads distances (the inside corner radius) or an offset, a shell or a fillet is asked. **Since 10-19 evening (written, not run):** the result of the tool is built on and united with the plain field that the `exact_distance` part carries (it has the same surface), and the remedies that are lattices (undercut, draft, walls, corners, overhang) read the walk once per lattice node, so the walk is no longer in every term of the result and the drawing should cost about what the plain part's does; the remedies that are still formulas (gaps, and thin walls, small corners and a small part's remedies, which the lattice cannot hold) read it dozens of times per point, and the tool's own seconds (the lattices sample the walk at every node near the surface: about 0.8 ms each) are still long for such a part.
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
- **A layer thinner than one cell of the picture is counted but only drawn where the result is one coloured body.** A result whose changes are mostly thin layers (walls thickened, gaps widened, the draft) is drawn as one body coloured by the change, and the layer
  shows as red wherever the surface moved. A result that mixes them with thick ones (a gap closed, a corner filled) is drawn as three bodies, and there the card says how much material was added to a fin thickened by 0.15 mm while at 2 cells to the millimetre (cells of 0.5 mm) the red
  layer is not to be seen: the mesher cannot draw a sheet thinner than its cell. Draw such a part with at least two cells across the layer, or look at a section. **The edge between red and grey is soft on a big flat face:** the one body is coloured per vertex of the mesh and blends across its triangles, so round a drafted hole in a flat plate (2 cells to the millimetre) a pink halo of about 1.5 mm surrounds the red ring, although the colour field is sharp (measured along a line out from the hole on the top face: 1.00 inside the 0.43 mm ring, 0.10 at 2.45 mm, 0.00 from 2.5 mm). Cutting the triangles at the colour edge, as it is done for boundary conditions, was tried and changed nothing visible, so it was removed. **The one coloured body costs more to draw than three:** it draws the whole surface of the
  result from the result's formula, where the three bodies draw only the thin bands that changed (the moulding sampler at 2 cells to the millimetre: 72.9 s as one body, 44.2 s as three; the tool itself 10 s either way).
- **A picture finer than the walls need can run the viewer out of memory**: the remedies are fields of many shifted copies of the part's, and the mesher needs memory in proportion to the cube of the resolution. Draw at four
  cells across the thinnest wall and no finer.
- **A result with several remedies is slow to draw at a fine resolution.** At 3 cells to the millimetre a ledge with its gusset takes about 40 s to draw (the tool: 1.5 s), a block with a round hole and its teardrop about 33 s, and example 33 (a thin fin, a post and a ledge) 45 to 55 s
  (tool 9 s); the wall remedy of a fin of 0.6 mm is the slowest part of that (about 20 lookups of the part for every cell near the fin). The cost grows with the square of the resolution: at 2 cells to the millimetre it is about 2.3 times quicker. A bigger part costs the number of lookups times its own size.

## The picture

- **While you drag a handle or a gizmo ring on a large model the picture is the quick, coarse level of the mesher** (it keeps up with the hand); the fine mesh comes when you let go.

## Platforms

- **Windows** is the main platform. The setup program and the zip are not signed: Windows SmartScreen may say *unknown publisher* the first time; *More info ▸ Run anyway*
  opens it (the source is public, and the setup is built from it by the script in `scripts/`).
- **Linux** packages (a folder, a `.run` installer, a `.deb`) are meant to be built on Ubuntu 22.04 (the release workflow does) so that they open on older systems; they were tried under WSL2 on Fedora, not on every distribution. **The Linux packages made on the maintainer's machine for 1.0.0 were built on Fedora 44 and ask for glibc 2.43 or newer**: they open on a current Fedora and not on Ubuntu 22.04 or Debian 12, which have an older C library. Packages made by the release workflow do not have this limit.
- There is **no macOS** build.
