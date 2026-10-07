# Importing models

One function imports a model, whatever file it is in: `import_model(path)`. It reads a **STEP file** (`.step`,
`.stp`) or a **triangle mesh** (`.stl`, `.obj`, `.ply`, `.3mf`, `.glb`, `.gltf`) and returns a list of
`(shape, (lower corner, upper corner))`, one per part (one for a mesh). The shapes are ordinary fields: they can
be offset, shelled, filled with a lattice, analysed and exported, and they carry the exact dimensions of the CAD.

How a part of a STEP file becomes a field is chosen **for each part**, by how much of its surface is free-form:

| The part | Made | Why |
|---|---|---|
| mostly planes, cylinders, cones, spheres, tori (a bracket, a housing) | **reconstructed**: a formula, rebuilt from its faces | exact at every plane and cylinder, light, its faces can be dragged |
| more than a tenth of its surface is free-form (B-spline): a sculpted body, a gear, a thread | **tessellated**: its exact surface as triangles, made a distance field | a fitted surface does not follow such a part |
| a surface body: an open sheet of faces with no inside (a shoe, a skin) | **tessellated**, as a thin sheet | there is no solid to rebuild |
| a mesh file | the exact signed distance to its triangles | |

The tenth is `threshold=0.10` (`import_model(path, threshold=...)`; 0 tessellates every part, 1 reconstructs every part).
It was measured on the test files: the parts of the examples have none to a few percent free-form surface, the sculpted
ones more than half, so nothing lies near the threshold. The model tree says which each part is (hover its row).

The two ways are also functions of their own, for when you want one by name:

```python
parts = reconstruct("step/bracket.step")      # every part rebuilt as CSG (STEP files only)
parts = tessellate("step/worm_gear.step")     # every part as its exact surface
t = tessellate(any_field)                      # ANY field: its surface meshed, the exact distance to that mesh
```

In the model tree, right-click an import row: **Import as** switches the function the statement calls.

- [Import a file](#import-a-file)
- [What is reconstructed, and how exactly](#what-is-reconstructed-and-how-exactly)
- [Free-form (B-spline) faces](#free-form-b-spline-faces)
- [Putting the exact surface back: exclude()](#putting-the-exact-surface-back-exclude)
- [Tessellated parts: tessellate()](#tessellated-parts-tessellate)
- [Surface models: sheets](#surface-models-sheets)
- [Automatic: auto_exclude](#automatic-auto_exclude)
- [Parts, assemblies and names](#parts-assemblies-and-names)
- [Units](#units)
- [Region and resolution](#region-and-resolution)
- [Failed parts](#failed-parts)
- [Caching, reimport and hot reload](#caching-reimport-and-hot-reload)
- [Combining the parts as one shape](#combining-the-parts-as-one-shape)

## Import a file

From the application: **File → Import model…** (`Ctrl+I`), **Import model…** in the right-click menu of empty
space in the viewport, or drop a file on the window. FielDes adds to the script, with a variable named after the file:

```python
# Imported model: bracket.step
bracket = import_model(r"C:\cad\bracket.step")
view.set_bounds(*roi(bracket))
view.set_resolution(roi_resolution(bracket))
view.set_quality(8)
```

Once the script has run, it also adds one named, displayed line per part (`bracket_0 = bracket[0][0]`
followed by `bracket_0`), so every part is in the model tree and the viewport. Each part is a model like any other, with
**its own eye**, lock and gizmo; the import row they are nested under (the assembly) has no eye, since it is the file and not
something drawn. A part is dragged by its gizmo (every part) or by its surfaces (a reconstructed one); lock the ones that
should stay where the file puts them (the lock button in the model tree, or `R`).

A body that the file lists but gives **no surface at all** (the last body of `pump.stp`) has nothing to import: its row is
dashed and dimmed, with a tooltip saying so, not struck through in red as a failure -- and the parts after it keep their numbers.

`import_model(path)` returns a list with one entry per part: `(shape, (lower, upper))` — the part as
a field and its own bounding box. Use the parts like any other shape:

```python
part, (lo, hi) = import_model("step/bracket.step")[0]
thick = offset(part, 0.5)
```

The import prints a summary (how many parts were reconstructed and how many tessellated, the units, the fitted faces)
in the output pane, and the parts appear in the model tree under the import.

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
  from 10 %. The colour is all there is: no legend, nothing to read under the cursor.
  (The shading is a colour field, `colored(shape, field)`; it does not change the geometry.)
- **`import_model` does not leave a badly fitted part to the reconstruction.** A part whose worst fitted face is off by
  `fit_tolerance` (1 % of the face's size by default) or more is tessellated instead: the model tree marks it with the
  mesh icon, and its tooltip says by how much the fit was off. (`fit_tolerance=None` keeps every reconstruction.) What stays
  reconstructed and shaded is the 0.5 to 1 % range.
- A fit this coarse is a *design-intent* surface: it has the right size, position and overall curvature, but
  not the detail.

Faces that fit well (under 0.5 %) look exact. Faces that do not are candidates for `exclude()`.

## Putting the exact surface back: exclude()

For any place where the approximate surface is not good enough — a free-form handle, a thread, a gear
tooth — use the STEP file's **own** surface. The place is a **region**, and a region is a **field object**:
any shape, the places where its field is negative.

```python
kitchen = import_model("step/Keukencombinatie.stp")
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
| the **whole import** (what `import_model` returns) | the region goes into **every part** it reaches | the same list, the parts in the same places |
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
part  = import_model("step/panel.step")[0][0]
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
(face areas to 0.1 %, see [the tessellated import](#tessellated-parts-tessellate),
which is made of the same tessellation). Tori are refined like the other curved faces, and a refinement is kept only if the
mesh's area comes closer to the torus patch's own.

`examples/03_kitchen_assembly.py` excludes the poor fits of the whole kitchen with one region, a union of the
parts' `poor_fit_region` fields.

## Tessellated parts: tessellate()

The fitted surfaces above are the right trade for a part that is mostly planes, cylinders and fillets with a
few free-form faces. A part that is *almost all* free-form — a sculpted or organic body, a gear, a worm, a
thread, an impeller — does not survive them: the faces fit poorly, the poor fits get painted red, and
`exclude()` only makes the part exact inside a region, the rest stays the fit. `import_model()` tessellates such a part
for you (more than a tenth of its surface free-form); `tessellate()` does it for all of a file's parts, by name. It
**does not reconstruct or fit anything**:

```python
parts = tessellate("step/worm_gear.step")      # (shape, bounds) per part, like import_model
worm = parts[0][0]
view.set_bounds(*roi(parts))
worm
```

It is the way nTop makes an implicit body of a B-rep: every solid is **tessellated straight from its trimmed faces** — each
face in its own parameter space, edges shared so the pieces are watertight, free-form faces refined inside their
outlines until no triangle turns the surface by more than a turn's share (2π over `quality`, 64 by default) — and
the triangles are made the **exact signed distance field** of the part ([mesh import](meshes.md)). What comes back
are ordinary shapes with the same parts, names, units, bounds and assembly placements as `reconstruct()`
(the parts are numbered the same way, so `parts[3]` is the same part in both), and since the field is a true
distance, `thicken`, `shell_*`, `offset_by`, lattices and analyses of it are exact where a reconstructed
field is only roughly a distance.

| | reconstructed | tessellated |
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
- against the **reconstruction**, inside and outside at 1 500 random points of every part: no difference in HingedTable,
  Bandextruder (the worm gear too), PT, MobileStand and Cribadora (111 parts); four pistons of the engine differ in
  1 to 2 % of the points — there the tessellation matches OpenCascade's volume to 0.4 %.

`quality=128` halves the triangles' size where that matters (and makes four times as many). Not watertight is
possible (the output says so for the solids): the mesh's winding number then decides inside and outside there.

**`tessellate()` of a field.** `tessellate(shape)` is not for files only: any field's surface is meshed
(`resolution` samples per unit, about 200 along its longest side by default) and the result is the exact signed distance to
that mesh. Booleans, blends and warps have fields that are only roughly a distance; after `tessellate()` an offset or a
shell of them is uniform. A mesh file given to it is imported as `import_model()` does. A list of parts (what
`import_model()` returns) gives the same list, every part tessellated.

**Cost and caching.** The tessellation is the work, and it is done once, on all the processor's threads (the
solids of an assembly a few at a time, the free-form faces of each on every thread): it is kept in the folder
`<file>.fieldes-tessellation` next to the STEP file (`cache=False`: not; **↺ Reset** in the model tree deletes it;
`rev=` / **⟳ Reimport** makes it again) until the file, `quality` or the tessellation changes. Building the distance
fields takes a fraction of a second a part; they are kept in memory while the script is run again. The folder also
holds the survey `import_model()` takes of a file (how much of each part's surface is free-form, from a coarse
tessellation: 12 points a turn).

**When not to tessellate.** A part that is mostly analytic faces (a bracket, a housing, a plate with holes) is
better reconstructed: it is a formula, exact at every plane and cylinder, with faces you can drag,
and the lighter field. That is what `import_model()` does with it.

## Surface models: sheets

Some files hold surfaces, not solids: a **surface model** (`SHELL_BASED_SURFACE_MODEL`, open shells of faces) as a CAD
program exports a sheet, a skin, a shoe. Such a body has no inside, so there is no solid to reconstruct: each of its
shells is a part, always tessellated, and the part is

- a **solid** if the shell closes (every edge shared by two faces): the signed distance, as for any solid;
- a **sheet** if it does not: the unsigned distance to its triangles less half a thickness, so that it can be drawn,
  offset, filled with a lattice, selected and exported like anything else. `thickness=` (in `units`) is that thickness;
  by default 0.4 % of the size of the file's surface bodies (the output says what it was). The shape reaches half a
  thickness past the surface on both sides.

`reconstruct()` says that a surface body cannot be rebuilt (the part is a `FailedPart` whose message tells so).

## Automatic: auto_exclude

The same without a line of its own, in the import:

```python
kitchen = import_model("step/Keukencombinatie.stp",
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

In the model tree, the parts are listed under the import. A part that is not in the script yet is
added as `bracket_3 = bracket[3][0]` by clicking its row.

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
parts = import_model("step/bracket.step")
whole = union(*[shape for shape, _ in parts])
```

For an assembly keep the parts separate: one union would force a single resolution over the whole assembly,
driven by the smallest feature anywhere in it.
