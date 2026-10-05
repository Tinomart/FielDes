# Importing STEP files

FielDes does not turn a STEP file into a mesh. It **rebuilds every solid as a field**, from its faces,
the way you would model it: a plane cuts, a cylinder bores, a fillet is a torus. The result is an
ordinary shape — it can be offset, shelled, filled with a lattice, analysed and exported — and it carries
the exact dimensions of the CAD.

- [Import a file](#import-a-file)
- [What is reconstructed, and how exactly](#what-is-reconstructed-and-how-exactly)
- [Free-form (B-spline) faces](#free-form-b-spline-faces)
- [Putting the exact surface back: exclude()](#putting-the-exact-surface-back-exclude)
- [Parts that are almost all free-form: import_step_tessellated_parts()](#parts-that-are-almost-all-free-form-import_step_tessellated_parts)
- [Automatic: auto_exclude](#automatic-auto_exclude)
- [Parts, assemblies and names](#parts-assemblies-and-names)
- [Units](#units)
- [Region and resolution](#region-and-resolution)
- [Failed parts](#failed-parts)
- [Caching, reimport and hot reload](#caching-reimport-and-hot-reload)
- [Combining the parts as one shape](#combining-the-parts-as-one-shape)

## Import a file

From the application: **File → Import model…** (`Ctrl+I`) or drop a `.step`/`.stp` file on the window.
FielDes adds to the script, with a variable named after the file:

```python
# Imported model: bracket.step
bracket = import_step_parts(r"C:\cad\bracket.step")
view.set_bounds(*roi(bracket))
view.set_resolution(roi_resolution(bracket))
view.set_quality(8)
```

Once the script has run, it also adds one named, displayed line per part (`bracket_0 = bracket[0][0]`
followed by `bracket_0`), so every part is in the model tree and the viewport. The eye of a part shows or
hides its line.

`import_step_parts(path)` returns a list with one entry per solid: `(shape, (lower, upper))` — the part as
a field and its own bounding box. Use the parts like any other shape:

```python
part, (lo, hi) = import_step_parts("step/bracket.step")[0]
thick = offset(part, 0.5)
```

The import prints a summary (how many parts, the units, the fitted faces) in the output pane, and the
parts appear in the model tree under the import.

## What is reconstructed, and how exactly

| Face type in the STEP file | Becomes | Exact? |
|---|---|---|
| Plane | a half-space | yes |
| Cylinder, cone, sphere | the exact quadric, cut by the face's boundaries | yes |
| Torus | the exact torus | yes in the field; see the note on the exact mesh below |
| **B-spline surface** | a **fitted** closed-form surface | approximate — reported and painted |
| Trimmed faces, holes, fillets, chamfers | CSG of the above, following the topology | as exact as the pieces |

A solid is turned into a *formula*: an intersection of half-spaces bounded by the faces' surfaces, with
unions and differences for concave regions. Everything that is a plane, cylinder, cone, sphere or torus is
therefore the CAD's own surface. Near the faces the field's value is about the signed distance (correct
sign, correct zero set), which is what offsets, shells and analyses need; far from them, and at edges where
several faces meet, it is only roughly a distance (see [Fields](fields.md#distances)).

## Free-form (B-spline) faces

A B-spline face has no closed form. The importer **fits** a simple closed-form surface to each one and keeps
the kind that deviates least from the CAD face:

| Fit | What it is |
|---|---|
| plane | for faces that are flat to within tolerance |
| quadric | the ten-term second-degree surface: cylinder, cone, sphere, ellipsoid, hyperboloid, paraboloid |
| extruded | a cubic curve swept along a line |
| revolved | a cubic curve spun around an axis |
| helical | a cubic curve that turns as it advances along an axis: the flank of a helical gear, a worm, a thread, a coil spring |
| fillet | the rounded corner of a given radius between the face's two neighbours (the rolling-ball blend of a CAD system) |

No sample grid or mesh is involved: the fitted surface is a formula, evaluated exactly.

After the import, the output pane lists what was approximated:

```
Keukencombinatie.stp: 212 free-form faces fitted by closed-form surfaces
  part 14: 3 faces, worst 0.41 mm (2.6 % of the face)
  …
  Shaded 0.5 % (grey) to 10 % (red). exclude() or auto_exclude=True puts the exact surface there.
```

- The deviation is given in mm and as a percentage of the **face's own size**.
- The model is **shaded where the fit is poor**: nothing below 0.5 %, light grey from 0.5 %, fully red
  from 10 %. A legend at the bottom right explains it; hover to read the percentage under the cursor.
  (The shading is a colour field, `colored(shape, field)`; it does not change the geometry.)
- A fit this coarse is a *design-intent* surface: it has the right size, position and overall curvature, but
  not the detail.

Faces that fit well (under 0.5 %) look exact. Faces that do not are candidates for `exclude()`.

## Putting the exact surface back: exclude()

For any place where the approximate surface is not good enough — a free-form handle, a thread, a gear
tooth — use the STEP file's **own** surface. The place is a **region**, and a region is a **field object**:
any shape, the places where its field is negative.

```python
kitchen = import_step_parts("step/Keukencombinatie.stp")
kitchen = exclude(kitchen)                                   # no region: where the fit is poor
kitchen = exclude(kitchen, offset(union(kitchen[59][0], kitchen[60][0]), 90))   # your own: any shape
```

`exclude()` is not specific to imports — it works on every shape and every field, see
[Excluded regions](fields.md#excluded-regions-exclude). What it does for a part imported from a STEP file is this:
it cuts the part into **two fields that are always united again**. The *free field* is the import outside the
region; every later operation reshapes that. The *locked field* is the part inside the region: **the part's own
surface, meshed directly from the STEP file** — not the import's fit, not the tessellating importer — and made
into a field (a signed distance field, [like an imported mesh](meshes.md)). No operation done to the part afterwards
changes it.

**Without a region**, `exclude()` works on the places where a part's fit is worse than `threshold` percent of
its face's size (1 by default; 0.5 is the lowest): `poor_fit_region(part, threshold)` is the field that
says where — negative there, positive away from it — made from the part's fit marker, the field that
shades the poor fits grey to red. Nothing is sampled, and the region wraps the whole badly fitted face,
so the seam between the exact surface and the fitted one lies where the fit is good.

**With a region**, it is yours: a sphere, a box, a part, a part offset by a distance, a union or
intersection of those, made of fields of the model itself; several regions given at once are their union.
Make it enclose the badly fitted faces whole: where the fit is off and the region's surface cuts through a bad
face, the exact surface and the fitted one do not meet there and the seam shows as a step. A fitted face is off by
up to a tenth of its size, so an offset round the fitted parts has to be that large.

What you give `exclude()` decides how much of the model it works on:

| You give it | It does | It returns |
|---|---|---|
| the **whole import** (what `import_step_parts` returns) | the region goes into **every part** it reaches | the same list, the parts in the same places |
| **one entry** of the import, `kitchen[19]` | that part only | the entry |
| **one part**, `kitchen[19][0]` | that part only | the part |

```python
kitchen[19] = exclude(kitchen[19])                  # one part: where its own fit is poor
kitchen[19] = exclude(kitchen[19], region)          # one part, a region of your own
```

The STEP file is meshed **directly from its B-rep** (an exact tessellation: every face triangulated in its own
parameter space, edges shared so the pieces are watertight, free-form faces sampled from the exact spline with an
error bound) and the triangles are made the field with the [mesh importer](meshes.md)'s method: the distance to the
nearest triangle, signed by the closest feature's normal (the winding number where the mesh is not closed).
The mesh and its field are made **once per part** and kept while the script is run again; the part's moves, turns,
scales and mirrors since the import are done to the field again, with the same numbers (they may be `var()`s).
`quality` sets the points per full turn of a circle in the mesh (default 64).

What the rest of the program sees: the excluded part **is** the united field, so the viewport, STL export,
analyses, lattices and further CSG all see the exact surface inside the region and the import's field outside
it. Only the mesh for the locked field is made from the file; it costs the tessellation of the part (and the
distance structure of its triangles), once, and the render then evaluates it only inside the region.

- **Operations** on an excluded part work on the whole part and then put the locked field back: an offset, a
  shell, a cut, a lattice, a union — anything done to the part afterwards does not change what is inside the
  region. Moving, turning, scaling and mirroring carry both fields along. Copying (`array_*`, `symmetric_*`,
  `repeat`, `mirror_*`) or deforming (`twist_z`, `bend_z`, `taper_*`, `attract_*`, `repel_*`, `twirl_*`) would
  tear the two apart, so they raise an error: do them before `exclude()`, and `exclude()` the result.
- For a shape made from a part (cut, filled with a lattice ...) say which imported part the locked geometry
  comes from with `source=`, and exclude last, to the finished shape:

```python
part  = import_step_parts("step/panel.step")[0][0]
light = offset(part, -1.0)                          # anything made from the part
final = exclude(light, sphere(12, center=(0, 0, 6)), source=part)
```

- The part may be moved, rotated, scaled or mirrored after the import; the locked field follows. Dragging a
  `var()` that the region or the placement uses updates the exact surface when the drag ends (the script
  runs again then).
- Several exclusions can be made one after the other; each adds its own locked field.
- In the viewport, select the part first and the regions after it (Ctrl+click), right-click and choose
  **Combining → exclude**.

A note on the exact mesh: the tessellation is verified watertight and matches OpenCascade on the test set
(face areas to 0.1 %, see [the tessellating importer](#parts-that-are-almost-all-free-form-import_step_tessellated_parts),
which is made of the same tessellation). Tori are refined like the other curved faces, and a refinement is kept only if the
mesh's area comes closer to the torus patch's own.

`examples/03_kitchen_assembly.py` excludes the poor fits of the whole kitchen with one region, a union of the
parts' `poor_fit_region` fields.

## Parts that are almost all free-form: import_step_tessellated_parts()

The fitted surfaces above are the right trade for a part that is mostly planes, cylinders and fillets with a
few free-form faces. A part that is *almost all* free-form — a sculpted or organic body, a gear, a worm, a
thread, an impeller — does not survive them: the faces fit poorly, the poor fits get painted red, and
`exclude()` only makes the part exact inside a region, the rest stays the fit. For those there is a separate importer
that **does not reconstruct or fit anything**:

```python
parts = import_step_tessellated_parts("step/worm_gear.step")      # (shape, bounds) per part, like import_step_parts
worm = parts[0][0]
view.set_bounds(*roi(parts))
worm
```

(`import_step_tessellated(path)` is the union of the parts, as `import_step()` is of `import_step_parts()`.) It is the
way nTop makes an implicit body of a B-rep: every solid is **tessellated straight from its trimmed faces** — each
face in its own parameter space, edges shared so the pieces are watertight, free-form faces refined inside their
outlines until no triangle turns the surface by more than a turn's share (2π over `quality`, 64 by default) — and
the triangles are made the **exact signed distance field** of the part ([mesh import](meshes.md)). What comes back
are ordinary shapes with the same parts, names, units, bounds and assembly placements as `import_step_parts()`
(the parts are numbered the same way, so `parts[3]` is the same part in both), and since the field is a true
distance, `thicken`, `shell_*`, `offset_by`, lattices and analyses of it are exact where the main importer's
field is only roughly a distance.

| | `import_step_parts` | `import_step_tessellated_parts` |
|---|---|---|
| a plane, cylinder, … | an exact formula | triangles |
| a free-form face | a fitted closed-form surface (0.1 % to 10 % off) | the face's own surface, to the tessellation's tolerance |
| first import | seconds to minutes (the reconstruction) | seconds: HingedTable (13 parts) 2.4 s, Bandextruder (18 parts, a worm gear) 7 s, Keukencombinatie (90 parts) 22 s, Cribadora (111 parts, 3 500 faces) 16 s |
| meshing a part (viewport, STL export) | the formula's | the mesh's distance: 0.5 to 2.8 times as long — about the same for a part of free-form faces (the worm gear: 0.7 to 1.1), 2.2 to 2.8 times for analytic ones |
| dragging faces (`expose`) | yes | no — the field is a mesh; `handles()` (the gizmo) works |
| STEP faces it cannot handle | per part, with the reason | per part, with the reason |

**Accuracy**, measured on the test files (HingedTable, Bandextruder, PT, an engine assembly, Keukencombinatie,
Cribadora; more than 260 parts in all):

- against **OpenCascade** (gmsh): the area of every face agrees to 0.05 % for planes and cylinders and 0.1 % for
  free-form faces; the volume of a part to 0.5 % (the table tops of HingedTable, mostly free-form: 0.3 %);
- against the **main importer**, inside and outside at 1 500 random points of every part: no difference in HingedTable,
  Bandextruder (the worm gear too), PT, MobileStand and Cribadora (111 parts); four pistons of the engine differ in
  1 to 2 % of the points — there the tessellation matches OpenCascade's volume to 0.4 %.

`quality=128` halves the triangles' size where that matters (and makes four times as many). Not watertight is
possible (the output says so for the solids): the mesh's winding number then decides inside and outside there.

**Cost and caching.** The tessellation is the work, and it is done once, on all the processor's threads (the
solids of an assembly a few at a time, the free-form faces of each on every thread): it is kept in the folder
`<file>.fieldes-tessellation` next to the STEP file (`cache=False`: not; **↺ Reset** in the model tree deletes it;
`rev=` / **⟳ Reimport** makes it again) until the file, `quality` or the tessellation changes. Building the distance
fields takes a fraction of a second a part; they are kept in memory while the script is run again.

**When not to use it.** A part that is mostly analytic faces (a bracket, a housing, a plate with holes) is
better imported with `import_step_parts()`: it is a formula, exact at every plane and cylinder, with faces you can drag,
and the lighter field. Use the tessellating importer for the part where its fitted faces are red.

## Automatic: auto_exclude

The same without a line of its own, in the import:

```python
kitchen = import_step_parts("step/Keukencombinatie.stp",
                            auto_exclude=True, exclude_threshold=1.0)
```

It is `exclude(parts, threshold=exclude_threshold)` on the whole import: for every part, the places fitted
worse than `exclude_threshold` (percent of the face's size) are excluded. The print-out says how many parts
were affected. `exclude_quality` is `exclude()`'s `quality`. It is **off by default**: the exact surfaces cost
tessellation time, and a model whose fits are all good does not need it.

## Parts, assemblies and names

An assembly arrives assembled: every part is placed where the file's assembly structure puts it. A
component used several times (four identical screws) gives one entry per occurrence — the first at its
solid's index, the others after the last solid, so the indices of the other parts do not change.

Every part has a name, its occurrence in the assembly (`Drive:1/Motor:1/M3x10-Screw:2`). It is shown in the
tooltip of the model tree and in the warnings.

In the model tree, the parts are listed under the import. Click the eye on a part to show or hide it (a part
that is not in the script yet is added as `bracket_3 = bracket[3][0]`), or on the import to do it for all.

## Units

A STEP file can declare its units per part (and one file can mix them). The import converts every part from
the unit it declares into `units`, `'mm'` by default: a file exported in metres is not a thousand times too
small. `units='cm' | 'm' | 'in'` work too, and `units='file'` keeps the numbers of the file's first declared
unit. `step_length_unit_mm(path)` tells what a file declares.

Everything else in FielDes assumes millimetres. Keep the default unless you know why.

## Region and resolution

```python
view.set_bounds(*roi(model))                        # a box around the imported models, padded by 10 %
view.set_resolution(roi_resolution(model))          # a resolution for them
```

`roi(*items, pad=0.1)` encloses the parts, a part, a bounds pair or an imported shape. `roi_resolution`
chooses a resolution from the parts' features: each part knows its smallest feature (a sheet's thickness,
a hollow tube's wall, a pin's or fillet's radius) and its size, and is meshed in a cube around itself at
the resolution **it** asks for — `detail_cells` (4 by default) across the feature, no coarser than
`part_cells` (64) across its size, limited to about 1.5 million vertices by itself. So a thin part is
rendered finely without making the rest of the scene fine. Together the parts stay under
`max_vertices` (25 million); when they would not, the costliest are coarsened first, and parts too thin or
small to render properly within the limits are **named** in a warning.

The number it returns is the resolution of the scene, for shapes *you* make from the parts (a lattice,
a cut): it scales all parts' own resolutions with it.

## Failed parts

If a solid cannot be reconstructed, the rest of the file still imports. The entry of the solid is a
`FailedPart`; using it (meshing, combining it with other shapes) raises an error saying why. In the model
tree its row is marked and the tooltip shows the reason.

## Caching, reimport and hot reload

Reconstructing a large file takes seconds to minutes. The result is written next to the STEP file as
`<file>.fieldes-cache.py` (a plain Python file) and reused until the STEP file changes or the import algorithm
does. `cache=False` turns it off, `cache='some/path.py'` stores it elsewhere. `rev=` is part of the cache key:

- **⟳ Reimport** in the model tree bumps `rev` (see [Handles](handles.md#reimport-and-hot-reload));
- a STEP file saved by another program (your CAD system) is noticed while the script is open, and
  re-read by itself. Change the part in the CAD program, save, and the model updates.

See [Caching and performance](caching-and-performance.md) for the other caches.

## Combining the parts as one shape

```python
whole = import_step("step/bracket.step")
```

`import_step` is the union of `import_step_parts`. It is convenient for a single-solid file. For an
assembly keep the parts separate: one union would force a single resolution over the whole assembly,
driven by the smallest feature anywhere in it.
