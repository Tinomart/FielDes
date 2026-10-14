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
better = manufacturing_optimization(part, constraints=[printer])    # the part changed, coloured by what changed
```

- [Constraints and their rules](#constraints-and-their-rules)
- [The check](#the-check)
- [The shell](#the-shell)
- [Supports and the print file](#supports-and-the-print-file)
- [Speed](#speed)
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
| `min_radius=3` | the smallest radius of an inside corner, mm | whether a ball of that radius reaches into it |
| `draft=1.0` | the least slope of a wall away from `pull_direction`, degrees | the angle of the wall to the pull direction |
| `undercut_along=(0, 0, 1)` | nothing a tool or a mould half coming along this direction, from either side, cannot reach | a ray along it, both ways, meets the part |

`build_direction` is the way up of a print (default z); `None` leaves it to be found: of the six axis directions and 40 spread over the sphere, the one that leaves the least of the surface leaning out
(with an overhang rule; the first 0.6 mm, resting on the plate, is not leaning out), and of those the one that makes the lowest print. It is a start, not a search for the best way to stand a part: say it when you know it. `pull_direction` is the way a mould opens. `region=` is a shape: the constraint holds only inside it, so a part can be given a constraint for each of its regions --
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
is the field), and nothing is meshed. The normals are central differences of the field; a thickness is found along the normal, by where the field crosses zero
between fourteen places; a ball that does not fit into a corner is found by the field at probes along the normal and on the ball's sphere.

The **numbers** of the report -- how much of the surface breaks a rule, how badly, in which places -- are **measured**, on a few thousand points of the surface (`samples=`, 5000):
random points near it, each moved onto it by Newton steps along the gradient. They are worked out a second way -- plain arithmetic on the values of the field at points along the lines
each rule looks along -- which is a check of the colours: the fields and the numbers are made two ways. The points are never drawn and never used to build anything.

```text
Manufacturing analysis: 5000 point(s) of the surface measured (0.5 s)
  rules are broken:
    thin wall (0.8 mm): 3.9 % of the surface, worst 0.16 mm, in 1 place(s)
        (15.27, 19.96, 20.37)  3.9 %
```

Limits: a wall that is thinner than the spacing of the probes, far off, is missed by the undercut check; a thickness is along the normal (a sloping thin wall reads right, a thin web
seen end-on does not); the part's field should be about a distance (a part imported from STEP has one that is a lower bound of it, so its walls read thinner than they are).
**It is a check of the part's own field and not a substitute for the check of the one who makes it.**

## The shell

`manufacturing_optimization` changes the **geometry**. Each remedy is a field made from the part's own -- shifted copies of it -- in the region of its constraint:

| Rule | Remedy |
|---|---|
| `min_wall` | the surface is offset outwards by what the wall lacks, half on each side (how thin the wall is comes from the depth of a ball along the normal, and the offset acts where the thickness along the normal agrees) |
| `min_gap` | the gap is closed, or made as wide as asked |
| `min_radius` | the part **closed by a ball** of that radius (the largest value of the field over the ball, less the radius): an inside corner a ball cannot reach into is filled to the radius, along the whole of it; an outside corner and a flat face are as they were |
| `draft` | a wall that leans less than the draft is offset by what its slope lacks, in proportion to how far it is below the top of the part: a vertical wall comes out wider at the foot |
| `undercut_along` | what lies both under and over part of the part, along the direction, is filled |
| `overhang` | `'fill'`: a point is part of the part when a disc, as wide as the slope has run by the time it is up to the part, fits inside the part above it (how far above the point the part starts is found smoothly, so the slope is a slope and not a stair) -- a gusset under a ledge at the angle asked; `'cut'`: the same done to the air the other way up -- a teardrop above a channel (measured: the air above a hole of radius 5 follows the ideal 45 degree cap to within 0.1 mm). Nothing is grown or cut under the print plate, and the part's own bottom resting on it is not an overhang |

They are all made from the part **as it is** (shifted copies of shifted copies would multiply the cost of a field at every step), united -- what they add, then what they cut -- and
what they changed is **measured** (on points of space, to a few per cent): a remedy that would change more of the part than `max_change` (0.25) allows is **not made**, and says so (a
wide overhang is filled down to a support, which this is not). A region that is excluded (`exclude`) or in `keep=` is never touched. Give the result to the shell again for a second pass.

The result is a **body** that is the part changed, coloured by **what changed** (red added, blue cut), with `.changes`, `.added`, `.removed`, `.share`, `.fail_states` (what the shell's own check, made on what it made, still finds broken and
where) and `.before`, `.after` (the two checks). `.oriented()` is the part turned so that the way up of the print is z, and an export (`export_mesh(result, "part.3mf")`) writes it **turned**
-- the model in the tree stays as it is. **Run `manufacturing_analysis` on the result**: the shell's check is its own.

What it costs: the fields of the remedies are made of tens to hundreds of shifted copies of the part's field. A field made of fields made of fields used to cost a multiple of itself at every level (a kernel walk that
went down a shared node once for every way to reach it); it is linear now, and a part with a thin fin is thickened and checked again in about 2 seconds, a ledge with a gusset in 6. A big imported part costs the copies times its own size: `verify=False`
leaves the check of the result to you. Inside corners are rounded by what the remedy is made of, and are not measured again: after the closing the field is not a distance, and a ball cannot be measured against it.

## Supports and the print file

A support is a **separate body** of the result, and it is the last resort: what the shell can do by changing the part it does first. `support='block'` on a constraint (with the overhang rule) lets the shell add, under every
surface of its result that still leans out more than the angle asked -- a gusset that was too big for `max_change`, a flat roof -- a **block straight under it**, down to the plate or the part below, kept `support_gap` (0.3 mm) from the part so that it breaks off.
`no_support=` (a shape, or a list) is where none may be made. The result then has `.supports` (a field, drawn beside the part), `.support_volume` and `.blockers`, and the overhangs it holds up are not listed as broken.
Block is the only kind so far; a rib or a column is a different shape of the same field, and a tree is a different problem (it has to grow round the part).

`export_print(result, "part.3mf")` writes the print as a 3MF with an **object for each**: the part (turned so that the way up of the print is z, as `.oriented()` does), the supports (type `support`) and each no-support
volume (type `other`, named `support_blocker_N`), all turned the same way. It is a file for a slicer to read: how a slicer treats the objects of type `support` and `other` is its own, and the file does not say
it in the slicer's own words (no slicer-specific modifier is written, because it could not be checked here). `export_mesh(result, ...)` writes the part alone, turned.

**Layers.** `export_layers(part, "layers.dxf", layer_height=0.2)` writes the contours of a body at a height every `layer_height` (through the middle of each layer): the outer contours and the holes as closed 3D
polylines, each layer on a DXF layer of its own (`L0001`, `L0002` ...), for a laser, a router or a program that stacks them. `export_slice(part, "slice.svg", z)` writes one layer as a picture, in millimetres. Both read the body's
field with the kernel's 2D contouring -- nothing is meshed -- and neither has paths, infill or perimeters: they are the shape of each layer, and not a toolpath.

## Speed

The result is a **field**, not a mesh: each remedy asks the part's own field for its value at other places (how far to the far side of a wall, what is above a point) and the viewport meshes that when it draws. The more the part's field costs, the more each of
those lookups costs, and an overhang gusset looks at about a hundred places. The example draws in about two minutes at resolution 3 (the part alone: a second). Draw at a coarser resolution first (`view.set_resolution(1)`), or run
`manufacturing_analysis` on the result and `export_mesh` it -- an export meshes once, at the resolution you give it. The numbers (`.added`, `.removed`, `.fail_states`) do not depend on the picture.

## What it does not do

- **Supports are minimal and of one kind** (a block under what is left, see above): a slicer makes them better where it matters, with tree supports and its own tuning. This changes the geometry so that fewer are needed, and makes a block where one is still needed.
- **No G-code and no slicing.** Files are for a slicer or a machine program to read.
- A bridge longer than `max_bridge` is reported, not repaired (an arch or a gable is not made yet); an overhang that is too wide to be filled to a support is reported.
- **Not validated against a real print or a real machine.** Run your own check, and print the part.
