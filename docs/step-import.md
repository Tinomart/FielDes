# Importing STEP files

FielDes does not turn a STEP file into a mesh. It **rebuilds every solid as a field**, from its faces,
the way you would model it: a plane cuts, a cylinder bores, a fillet is a torus. The result is an
ordinary shape — it can be offset, shelled, filled with a lattice, analysed and exported — and it carries
the exact dimensions of the CAD.

- [Import a file](#import-a-file)
- [What is reconstructed, and how exactly](#what-is-reconstructed-and-how-exactly)
- [Free-form (B-spline) faces](#free-form-b-spline-faces)
- [Putting the exact surface back: exclude()](#putting-the-exact-surface-back-exclude)
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
  Shaded 0.5 % (grey) to 10 % (red). exclude() or auto_exclude=True draws the exact surface.
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

**Without a region**, `exclude()` works on the places where a part's fit is worse than `threshold` percent of
its face's size (1 by default; 0.5 is the lowest): `poor_fit_region(part, threshold)` is the field that
says where — negative there, positive away from it — made from the part's fit marker, the field that
shades the poor fits grey to red. Nothing is sampled, and the region wraps the whole badly fitted face,
so the seam between the exact surface and the fitted one lies where the fit is good.

**With a region**, it is yours: a sphere, a box, a part, a part offset by a distance, a union or
intersection of those, made of fields of the model itself. Make it enclose the badly fitted faces whole:
where the fit is off and the region's surface cuts through a bad face, the exact surface and the fitted one
do not meet there and the seam shows as a step. A fitted face is off by up to a tenth of its size, so an
offset round the fitted parts has to be that large.

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

FielDes meshes the solid **directly from the STEP B-rep** (an exact tessellation: every face triangulated in
its own parameter space, edges shared so the pieces are watertight, free-form faces sampled from the exact
spline with an error bound). The region itself is never meshed: the exact surface's triangles are cut by
evaluating the region's field on them (the ones its surface crosses are split down to one cell of the render),
the shape's mesh loses its triangles inside the region, and the two are put together — in the viewport and in
STL export alike. The edge between them is jagged, one cell wide; where the fit is good there the two
surfaces coincide and it does not show.

What the rest of the program sees:

- The **viewport and STL export** see the exact surface inside the region.
- The **field** — what analyses, lattices and further CSG see — is not changed; it stays the fitted one.
  The exact surface replaces what is drawn, so apply `exclude()` *last*, to the finished shape, with
  `source=` the imported part:

```python
part  = import_step_parts("step/panel.step")[0][0]
light = offset(part, -1.0)                          # anything made from the part
final = exclude(light, sphere(12, center=(0, 0, 6)), source=part)
```

- The part may be moved, rotated, scaled or mirrored after the import; the region and the exact piece
  follow. The region may use `var()` numbers (they are draggable).
- `quality` sets the points per full turn of a circle in the exact mesh (default 64).
- Several regions can be excluded one after the other.

A note on the exact mesh: the free-form tessellation is verified watertight and matches OpenCascade on the
test set. **Large torus faces** are sampled without refinement and can come out a few percent small
(−8 % volume on the worst test part); use the field for tori — they are exact there.

`exact_region_mesh(shape, cell=1.0)` returns the exact pieces as vertex and triangle lists, for scripts that
work outside the application.

`examples/03_kitchen_assembly.py` excludes the poor fits of the whole kitchen with one region, a union of the
parts' `poor_fit_region` fields.

## Automatic: auto_exclude

The same without a line of its own, in the import:

```python
kitchen = import_step_parts("step/Keukencombinatie.stp",
                            auto_exclude=True, exclude_threshold=1.0)
```

It is `exclude(parts, threshold=exclude_threshold)` on the whole import: for every part, the places fitted
worse than `exclude_threshold` (percent of the face's size) are excluded. The print-out says how many parts
were affected. `exclude_quality` is `exclude()`'s `quality`. It is **off by default**: the exact pieces cost
meshing time, and a model whose fits are all good does not need it.

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
