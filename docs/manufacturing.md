# Manufacturing

A part has to be made. FielDes has three blocks for what a process asks of it, in the **Engineering** menu under **Manufacturing**:

| Block | What it is | |
|---|---|---|
| `manufacturing_constraint`, `fff_constraint`, `cnc_constraint`, `molding_constraint` | **constraints**: what the part has to keep | inputs, like a support or a load |
| `manufacturing_analysis(part, constraints=[...])` | the **check**: where the part breaks them, as colours on it, and how much | a result, like a stress |
| `manufacturing_optimization(part, constraints=[...])` | the **shell**: the part, **changed** until it keeps them, and what was changed | a body: wherever a body goes, it goes |

```python
printer = fff_constraint(nozzle=0.4)
report = manufacturing_analysis(part, constraints=[printer])        # coloured where it breaks a rule
better = manufacturing_optimization(part, constraints=[printer])    # the part changed, coloured by the geometry change
```

- [Constraints and their rules](#constraints-and-their-rules)
- [The check](#the-check)
- [The shell](#the-shell)
- [Supports and the print file](#supports-and-the-print-file)
- [Speed](#speed)
- [How it is tested](#how-it-is-tested)
- [What it does not do](#what-it-does-not-do)

## Constraints and their rules

`manufacturing_constraint` has every rule, each optional -- only what is written is asked. The presets write the numbers of a process; every number
of a preset is an argument of it, and the guides they come from are not a standard (ask your printer's and your plastic's makers).

| Rule | Asks | Measured |
|---|---|---|
| `overhang=45` | the steepest a surface that faces down may lean, in degrees from the vertical, without a support under it | the angle of the surface to the vertical; the first layer on the plate rests on it |
| `min_wall=0.8` | the thinnest wall, mm | the thickness along the normal, through the material (not where the surface turns within half the limit -- the rounded edge of a thin wall is not a thin wall) |
| `min_gap=0.4` | the narrowest gap between walls, a hole or a slot, mm | the width along the normal, through the air |
| `max_bridge=20` | the longest flat span with nothing under it, mm | the narrowest chord of the air under a flat roof that has a wall at both ends (a ledge with an open end is an overhang, not a bridge) |
| `min_radius=3` | the smallest radius of an inside corner, mm | whether a ball of that radius reaches into it (a corner is marked from about 17 % short of the radius: see The check) |
| `draft=1.0` | the least slope of a wall away from `pull_direction`, degrees | the angle of the wall to the pull direction |
| `undercut_along=(0, 0, 1)` | nothing a tool or a mould half coming along this direction, from either side, cannot reach | a ray along it, both ways, meets the part |

`build_direction` is the way up of a print (default z); `None` leaves it to be found: of the six axis directions and 40 spread over the sphere, the one that leaves the least of the surface leaning out
(with an overhang rule; the first 0.6 mm, resting on the plate, is not leaning out), and of those the one that makes the lowest print. It is a start, not a search for the best way to stand a part: say it when you know it. **A limit:** the plate is taken at the lowest corner of the part's box along the way up, which is the part's own lowest point for an axis and lies below it for a way up that is not an axis -- a face laid on the plate at an angle is then not taken to rest on it and counts as leaning out, so the search prefers the axes; to print a tilted part on its flat face, turn the part until the face is level and name that axis. `pull_direction` is the way a mould opens. `region=` is a shape: the constraint holds only inside it, so a part can be given a constraint for each of its regions --
the **overhang** rule is off in the presets (slicers put supports where they are needed) and is for what supports cannot reach: a channel from a flow or a thermal topology
optimisation, a bore, a cosmetic face, a lattice. `overhang_fix='fill'` (the default) grows the body under the overhang; `'cut'` cuts the air above it into a **teardrop**.
`gap_fix='fill'` closes a gap that is too narrow, `'widen'` makes it as wide as asked.

| Preset | Writes | From |
|---|---|---|
| `fff_constraint(nozzle=0.4)` | `min_wall` two nozzle widths, `min_gap` one, `max_bridge` 20 mm | guides give 0.8 to 1.2 mm of wall for a 0.4 mm nozzle, a gap narrower than the nozzle prints closed, bridges up to about 30 mm; the 45 degrees of overhang depends on the material and the printer: calibrate |
| `cnc_constraint(tool_diameter=6)` | `min_radius` the cutter's radius, `min_gap` its diameter, `undercut_along` the axis, `min_wall` 0.8 mm | the smallest radius a cutter leaves is its own; thin walls chatter (1.5 mm and more for plastics) |
| `molding_constraint(min_wall=1)` | `draft` 1 degree, `undercut_along` the pull, `min_radius` half the wall | 1 degree is the baseline, 2 for a texture; 0.5 times the wall inside; walls 1 to 4 mm by the plastic |

A rule counts as broken when it is more than 5 % short of its limit: what the part's field can say of a size is not exact.

## The check

`manufacturing_analysis` reads the part's own field and nothing else: **every rule is a field on the part**, drawn as colours on it (the result card switches rule; `result.min_wall`
is the field), and nothing is meshed. The normals are read from the field's symbolic derivative (`Shape.partial`: the kernel derives the field's expression; each component is the mean of the derivative at two points a thousandth of the part's size apart, which is what a central difference is, with the derivative's own bounds); a thickness is found along the normal, by where the field crosses zero
between fourteen places; a ball that does not fit into a corner is found by the field at probes along the normal and on the ball's sphere: how far from the part the centre of a ball that touches the point can be. In a corner of 90 degrees that
clearance grows by 0.29 mm for every mm of the corner's radius, so a corner is marked from about 17 % short of the limit (under 2.5 mm for a limit of 3); between that and the limit (2.5 to 2.85 mm) the reading is
within its own noise -- the normal is the field's slope averaged over a step, and a normal 5 degrees off at a radius of 3 moves the ball by 0.26 mm -- and a corner there may or may not be marked, in stripes along the arc where it is (seen on pockets with corners of 1.8 and 2.7 mm). A normal that is a degree or two off -- the normal of a field made
from a mesh -- does not mark a corner that is right (at the radius asked or more).

The **numbers** of the report -- how much of the surface breaks a rule, how badly, in which places -- are **measured**, on a few thousand points of the surface (`samples=`, 5000):
random points near it, each moved onto it by Newton steps along the gradient. They are worked out a second way -- plain arithmetic on the values of the field at points along the lines
each rule looks along -- which is a check of the colours: the fields and the numbers are made two ways. The points are never drawn and never used to build anything.

**The check does not share with the shell what finds a fault, so that it can verify it.** The code is in four files: `manufacturing_rules.py` (the constraints and the presets: what is asked;
nothing is measured), `manufacturing_field.py` (how a part's field is read: moved lookups, normals, values at points; no rule), `manufacturing_check.py` (`manufacturing_analysis`) and
`manufacturing.py` (`manufacturing_optimization` and its remedies). The check imports nothing of the optimization; the optimization calls one function of the check, `_analyse` (private), to be told what its
result still breaks. So `manufacturing_analysis` on a result is a verification of the remedies and not their own reading of themselves. Where the report measures a wall or a gap it has a way of its own:
the shortest of nine lines through the material (the air, for a gap) from the surface point -- the normal and eight leaning 20 degrees away from it -- where the remedies find a thin wall by the line
along the normal alone; what one of them misses the other may find.

```text
Manufacturing analysis: 5000 surface samples (10.8 s)
  Rule violations:
    Wall thickness (limit 0.8 mm): satisfied
    Gap width (limit 6 mm): 0.56 % of surface area, max. deviation 4.02 mm, 3 region(s)
        (7.904, 11.92, 5.139)  0.46 %
        (32, 21.83, 10.98)  0.08 %
        (5.65, 11.91, 13.97)  0.02 %
  Evaluated on the part's own field; not a substitute for the manufacturer's own check.
```

Limits: a wall that is thinner than the spacing of the probes, far off, is missed by the undercut check; a thickness is along the normal (a sloping thin wall reads right, a thin web
seen end-on does not); the part's field should be about a distance (a part imported from STEP has one that is a lower bound of it, so its walls read thinner than they are; so has a part made by taking a union of overlapping cutters out of a body, inside the cavity -- there the field is the deepest of the cutters' own depths, 1.4 mm where the wall is 3 mm away -- and the rules that read values up to a ball's radius from the surface, the corner rule above all, take a point in the middle of such a wall for a corner: run `exact_distance` on that part first).
**It is a check of the part's own field and not a substitute for the check of the one who makes it.**

## The shell

`manufacturing_optimization` changes the **geometry**. Each remedy is a field made from the part's own -- shifted copies of it -- in the region of its constraint:

| Rule | Remedy |
|---|---|
| `min_wall` | the surface is offset outwards by what the wall lacks, half on each side, and a margin of 5 % over it so that the check passes it: how thin the wall is is the length of the line along the normal, from the nearest point of the surface through the material to where it meets the surface again -- the thickness the check measures --, and the offset acts only where that line starts on the surface. The whole face is thickened, up to its edges and not past them (measured: a 0.6 fin asked for 0.8 comes out 0.800 over its whole face, and stands out 0.025 mm past its top). **A limit:** the faces are thickened, the places where faces meet are not filled: along an edge where thin walls meet (the corner of a box) and in the inside corner where a thin wall stands on a thick face a notch the size of the growth is left (0.15 mm for a wall of 0.5 asked 0.8), and the check still marks a few hundredths of a per cent of the surface there. Terms that filled them were made and measured and taken out: they drew three to ten times as slowly, left faint ridges 1.2 mm from the edges of walls that were thick enough, and made a field that the viewer could not mesh within the memory of the machine. Ask `min_radius` as well and the corners are rounded. **A second limit:** the wall is measured along the normal, which is the wall's thickness where its two faces are parallel; where both faces curve towards each other with a radius about the wall's own thickness (a web between two small holes) the closest approach of the faces is shorter than the chord along the normal away from the narrowest line, and the wall comes out up to about 10 % under the limit there (a web of 0.3 between holes of radius 1, asked 0.8: 0.800 along the line of centres, 0.71 to 0.73 on the oblique lines of the check, about 7 % of that plate's surface marked); the report lists those places |
| `min_gap` | the gap is closed, or made as wide as asked: each side grows by half the gap's width (the length of the line along the normal through the air, as the check measures it), up to the gap's mouth and not above it. **A limit:** the field is read through differences of a step of a thousandth of the part's size (0.03 mm for a part of 30 mm, never under 0.004 mm), and a slit narrower than two of those steps -- the point of a V-shaped slot, the first 0.06 mm above the floor of a slit -- is not read and stays as it was |
| `min_radius` | the library's **`fillet`** of that radius: a ball of that radius rolls in the air along the part, found from the part's own field by Newton steps along its gradient (nothing is meshed: `distance_field.py`), so an inside corner is filled by the arc of a ball that rolls in it, along the whole of it, and an outside corner and a flat face are as they were. The ball's centre is where the planes of the faces it touches meet, so the arc is exact for two flat faces at any angle and for the corner of three (measured: the fillet of an L of radius 3 adds 15.51 mm^3 where the arc says 15.45; the floor corner of a slot lies 1.243 mm from the corner point on its diagonal, as the ball gives). It works on any part -- an import, a lattice, a pocket -- and a slot narrower than the ball is filled, flush with its mouth (the sag of the ball resting on its two edges is not made), as the cutter leaves it. Where the part was made by taking a cutter that overshoots it out of a block, the field beyond the block is the cutter's own: the fillet reaches the end faces of flat walls, and stops 1 to 2 mm short of them where the corner is a curve tighter than the ball. The remedy is made only where the check of the part finds a corner short of the radius: the walk is good to a few tenths of a per cent, and over a smooth part with no inside corner to round (a torus) it can add specks where it falls short, so a part that keeps the rule comes out as it was, and the card says "no change required" |
| `draft` | what it is for: a moulded part leaves the mould in one direction, and a wall parallel to it rubs along the steel for its whole height (it sticks, scuffs, needs ejector force), so moulding guides ask 1 to 2 degrees of lean on every such wall. What it does: a wall that leans less than the draft is offset by what its slope lacks, in proportion to how far it is below the top of the part: a vertical wall comes out wider at the foot, by that depth times the tangent of the draft (0.35 mm at 10 mm for 2 degrees). Tops and floors are not moved; every sideways wall of the part gets it, the walls of holes included |
| `undercut_along` | what lies both under and over part of the part, along the direction, is filled: the part's field is read at 17 fixed heights along the direction (the first and the last just inside the part's two ends, the same for every point), so that the edge of what is filled is one straight wall under the tip of a lip and not a staircase. **A limit:** a shelf thinner than a sixteenth of the part's length along the direction can lie between two heights and is then not seen |
| `overhang` | `'fill'`: a point of the air is part of the part when a disc, as wide as the slope has run by the time it is up to the part, fits inside the part at the height where the part starts above it, or at any height up to `max_support_height` higher -- a gusset under a ledge at the angle asked, as deep as the ledge is wide and no deeper. **It is worked out once, by the kernel, on a lattice laid over the part in the build direction (a pitch of a 250th of the part's size, coarser for a part that would need more than twelve million nodes) and kept as a field of its own, which the result unites with the part**: the part is sampled at the nodes, the distance in each slice from the part to the air is found to the edge itself, and in each column the lowest height the gusset reaches. A point of the drawing then costs one look at the part and one at that field, where the same rule asked as a formula at every point cost about a hundred looks at the part. The faces of the gusset are exact planes; a crease between two of them is rounded within one pitch; `'cut'`: the same done to the air the other way up -- a teardrop above a channel (measured: the air above a hole of radius 5 follows the ideal 45 degree cap to within 0.1 mm). Nothing is grown or cut under the print plate, and the part's own bottom resting on it is not an overhang. **A limit:** a point is filled when the whole cone at the angle asked above it lies in the part as it was, so under a thin plate that leans only a few degrees over the limit (a plate 2 mm thick, 47 degrees) the cone fits at the foot alone, and the rest of the face stays as it was -- the shell lists those places as still violated, and does not claim a clean result; a thicker part, or a wider ledge, is filled whole. Where a gusset ends on the side face of the part a strip of a tenth of a millimetre or two is left, which the check marks (0.04 % of the surface of a ledge) |

They are all made from the part **as it is** (shifted copies of shifted copies would multiply the cost of a field at every step), united -- what they add, then what they cut -- and
what they changed is **measured** (on evenly spread points -- a Halton sequence -- of the shell round the surface for the remedies that change a thin layer -- walls, gaps, corners, draft --, so that a layer a tenth of a millimetre thick has points in it, and on points over the whole box for the others: the volumes are measured, not worked out. Measured on an L of two walls with a fillet of radius 6, 154.5 mm^3 by hand, over 8 starts of the sequence: 153.5 mm^3 on average, 1.0 % between one measurement and the next, 2.2 % off at worst -- on as many random points it was 4.7 % and 8.3 %; the part's own volume 0.1 %, on random points 1.5 %): a remedy that would change more of the part than `max_change` (0.25) allows is **not made**, and says so (a
wide overhang is filled down to a support, which this is not). A region that is excluded (`exclude`) or in `keep=` is never touched. Give the result to the shell again for a second pass.

The result is a **body** that is the part changed, shown as the **geometry change**: three bodies, each by its exact field -- the part where it is as it was (grey), the material added (red: in the result and outside the input part) and the material cut (blue: in the input part and outside the result). **When most of what was changed is a thin layer** (a wall thickened by a tenth of a millimetre, a gap made wider, the draft -- a skin that tapers to nothing at the top of its wall) **the result is drawn as ONE body, at the scene's resolution, coloured by the change at each point of its surface** (red where it lies outside the input part, blue where inside it, the part's colour on the old surface): three bodies of which two are such skins are drawn as stripes, spikes and missing patches. The card has a row for each remedy that was kept ("Draft applied to walls") so that it says what the added material is for. **In the three-body display a layer thinner than one cell of the picture is counted but not drawn:** the card and `.added` say 88 mm³ of a fin thickened by 0.15 mm, and at 2 cells to the millimetre (cells of 0.5 mm) no red is to be seen; draw such a part at a resolution of 2 cells or more across the layer (`view.set_resolution`), or look at it in a section. (Colouring the one mesh of the result by how far its vertices are from the input could not show a layer that lies flush with the faces of the part -- a slot that was closed: the vertices of its flat face are all on the old edges, where nothing has changed.) It has `.changes` (each remedy: what it adds or cuts on its own), `.added` (the volume of the union of what the additive remedies add: a place that two of them work on is counted once), `.removed`, `.share`, `.fail_states` (only when asked, `verify=True`: what the shell's own check, made on what it made, still finds broken and
where: the five biggest places of each rule; `.fail_totals` has all the places and their share of the surface for each rule, which is what the card counts) and `.before`, `.after` (the two checks). `.oriented()` is the part turned so that the way up of the print is z (draw it to see the part standing the way it is printed; when the way up was left free, `build_direction=None`, the card has a row "Way up of the print" with the direction the tool found), and an export (`export_mesh(result, "part.3mf")`) writes it **turned**
-- the model in the tree stays as it is. **Run `manufacturing_analysis` on the result**: the shell's check is its own. A call that was made before -- the same part, constraints and numbers, as when a model is shown or hidden and the script runs again -- gives back what it made, in the same session, and does not measure and shape the part again.

What it costs: the fields of the remedies are made of tens to hundreds of shifted copies of the part's field. A field made of fields made of fields used to cost a multiple of itself at every level (a kernel walk that
went down a shared node once for every way to reach it); it is linear now, and a ledge with a gusset is made and checked again in about 1.5 seconds. What a remedy costs to draw is not its number of lookups: the mesher can only skip a cell where the bounds of the field over it decide what is in it, and a term as fine as a wall
gets no help from that at cells about the size of the wall. So the remedies are kept to the least that is exact where it matters (the wall remedy is the offset where a wall is thin and nothing else, see the limit above), and the scene of a test is small and fine rather than large and coarse:
**the finer the picture, the more memory the mesher needs for any of them** (cubed: twice as fine is eight times), and a part with many features costs the copies times its own size. A picture too fine for the machine is the one thing that can stop the viewer; draw at the
resolution the thinnest wall needs (4 cells across it is enough to see it) and no finer. Measured numbers are in the report (`tests/RELEASE_REPORT.md`). A big imported part costs the copies times its own size. The shell does **not** check its own result unless it is asked to (`verify=True`): that check is the whole of the check run again on the result, and on a
topology-optimized bracket it was ten of the thirteen minutes. Run `manufacturing_analysis` on the result when you want it; the card then shows no "places still breaking a rule" until you do. The corner remedy rounds the corners with `fillet`, a ball rolled in the part's own field (see [fields](fields.md)): nothing is meshed for it.

**A part that is expensive to read is worked out once, on a lattice.** Where a point of the part's field costs more than 15 microseconds (the topology-optimized bracket: 26), five remedies come from the kernel as fields of their own instead of formulas that ask the part dozens of times at every point the mesher samples: the **overhang** (a gusset or a teardrop), the **undercut** fill (in every column of a lattice laid along the cutter's axis, the air between the first and the last node of the part), the **draft** (the formula of the rule, worked out on the lattice from the part's values in a band round its surface), the **walls** (a wall thinner than the limit is offset outwards by half of what it lacks; the thickness is the length of the line along the normal through the material, read off the lattice) and the **corners** (the rolling ball itself, with no walk: a point of the air is filled where the largest value of the part's field on the ball round it falls short of what open air gives, the surface being where the value less that shortfall is 0; the field is renormalised by its own slope first). The lattice is a 400th of the part's size (a 250th for the overhang and the undercut; coarser for a part that would need tens of millions of nodes); a distance read between nodes is good to a small part of a node, so these are placed within a node of the exact formula. A small part keeps the formulas, which are exact at any resolution, and so does the wall remedy where the limit is smaller than three and a half nodes. The gap remedy stays a formula. `FIELDES_UNDERCUT_LATTICE`, `FIELDES_CORNERS_LATTICE` (the older closing by distance transforms), `FIELDES_DRAFT_LATTICE` and `FIELDES_WALLS_LATTICE` set to 1 force the lattice, to 0 forbid it. Measured on the bracket's mould job: the tool 91 s and the drawing 12 s (it never finished before); see the report.

## Supports and the print file

A support is a **separate body** of the result, and it is the last resort: what the shell can do by changing the part it does first. `support='block'` on a constraint (with the overhang rule) lets the shell add, under every
surface of its result that still leans out more than the angle asked -- a gusset that was too big for `max_change`, a flat roof -- a **block straight under it**, down to the plate or the part below, kept `support_gap` (0.3 mm) from the part so that it breaks off.
`no_support=` (a shape, or a list) is where none may be made (what leans out over it is not held, and the card lists it). The result then has `.supports` (a field, drawn beside the part in amber; the legend of the result has it as a fourth entry, "print supports", under added, unchanged and removed -- it has no card of its own), `.support_volume` and `.blockers`, and the overhangs it holds up are not listed as broken.
A block stops four steps of the field (about a tenth of a millimetre on a part of 100 mm) short of the edge of what it holds up, so that its own face is read cleanly; the check counts a place within those steps, and two more, of the block's edge as held, and finds the block under a face that leans at the depth of the clearance along the normal (the gap divided by the cosine of the lean), not straight `support_gap` down.
Block is the only kind so far; a rib or a column is a different shape of the same field, and a tree is a different problem (it has to grow round the part).

`export_print(result, "part.3mf")` writes the print as a 3MF with an **object for each**: the part (turned so that the way up of the print is z, as `.oriented()` does), the supports (type `support`) and each no-support
volume (type `other`, named `support_blocker_N`), all turned the same way. It is a file for a slicer to read: how a slicer treats the objects of type `support` and `other` is its own, and the file does not say
it in the slicer's own words (no slicer-specific modifier is written, because it could not be checked here). `export_mesh(result, ...)` writes the part alone, turned.

**Layers.** `export_layers(part, "layers.dxf", layer_height=0.2)` writes the contours of a body at a height every `layer_height` (through the middle of each layer): the outer contours and the holes as closed 3D
polylines, each layer on a DXF layer of its own (`L0001`, `L0002` ...), for a laser, a router or a program that stacks them. `export_slice(part, "slice.svg", z)` writes one layer as a picture, in millimetres. Both read the body's
field with the kernel's 2D contouring -- nothing is meshed -- and neither has paths, infill or perimeters: they are the shape of each layer, and not a toolpath.

## Speed

The result is a **field**, not a mesh: each remedy asks the part's own field for its value at other places (how far to the far side of a wall, what is above a point) and the viewport meshes that when it draws. The more the part's field costs, the more each of
those lookups costs, and an overhang gusset looks at about a hundred places. **The time of a result is the tool and the drawing added**, measured at 3 cells to the millimetre on a laptop processor: a ledge 30 x 40 x 40 mm with its gusset takes about 1.5 s in the tool and
about 40 s to draw; a block with a round hole and its teardrop 1.2 s and about 33 s; example 33 (a thin fin, a post and a ledge) about 9 s and 45 to 55 s (the part alone: a second). The drawing costs about the square of the resolution: **at 2 cells to the millimetre it is about 2.3 times quicker, at 1.5 about 5 times**.
Draw at a coarser resolution first (`view.set_resolution(1.5)`), or run `manufacturing_analysis` on the result and `export_mesh` it -- an export meshes once, at the resolution you give it. The numbers (`.added`, `.removed`, `.fail_states`) do not depend on the picture.
To see where the drawing time goes, start the program with the environment variable `FIELDES_TIMING=1`: the log it writes ends with the `[dc]` lines of the mesher (the cells it worked through, the leaves on the surface and those with nothing in them, and the seconds each part took, summed over its threads).

## How it is tested

`tests/manufacturing/` holds a file for every thing that can happen -- a thin fin, a narrow slot, an inside corner, a leaning wall, an undercut, a ledge, a bridge, a refused remedy, the same part turned and mirrored and scaled, parts made at random,
a real bracket made ready for a printer, a mill and a mould -- each readable from the top (the part, the constraint, the answer worked out by hand, the checks) and each drawing the result beside the part as it was. Open `00_START_HERE.py` (File > Open): it lists
them, says what each one tries to break and writes down how its last run ended. `TEST_DESIGN.md` says why each exists and `JOB_TESTS.md` how the jobs are judged (against published design rules). The numbers of the older proof scripts are in `tests/proof/README.md`,
and the whole round in `tests/RELEASE_REPORT.md`. No test exports a mesh.

## What it does not do

- **Supports are minimal and of one kind** (a block under what is left, see above): a slicer makes them better where it matters, with tree supports and its own tuning. This changes the geometry so that fewer are needed, and makes a block where one is still needed.
- **No G-code and no slicing.** Files are for a slicer or a machine program to read.
- A bridge longer than `max_bridge` is reported, not repaired (an arch or a gable is not made yet); an overhang that is too wide to be filled to a support is reported.
- **Not validated against a real print or a real machine.** Run your own check, and print the part.
