# Caching and performance

A FielDes script runs again after every edit, and a few of its operations are expensive: reading a large STEP
file, meshing a shape, solving an analysis. The caches make the second run cheap **without changing any
result**: every cache is keyed by *exactly what the result was built from*, never by a name or a printed
approximation, and a hit returns the very thing a miss built. Nothing is ever sampled onto a grid to save time.

- [What is cached](#what-is-cached)
- [How keys work](#how-keys-work)
- [Controlling the caches](#controlling-the-caches)
- [The field cache](#the-field-cache)
- [The result cache](#the-result-cache)
- [The render cache](#the-render-cache)
- [Resolution and quality](#resolution-and-quality)
- [Long renders](#long-renders)
- [Making things faster](#making-things-faster)

## What is cached

| What | Kept | Key |
|---|---|---|
| **STEP import** (the rebuilt fields) | on disk, next to the STEP file: `<file>.fieldes-cache.py` and `.fieldes-cache.trees` | the file's SHA-256, the import-algorithm version, `rev=` |
| **Any field a script builds** (`name = <expression>`: a lattice laid out on a part, an offset, a thickness field — [the field cache](#the-field-cache)) | in memory, and on disk when every part of the field can be saved | the statement's text, the exact content of every name it reads, the `var()` numbers, the code that builds it |
| **Shapes already meshed in the viewport** | in memory, for the session | the shape's expression (structural identity) and its colouring |
| **The finished mesh of a shape you asked for** ([render cache](#the-render-cache)) | on disk, in FielDes's cache folder | the shape's expression with the numbers it is drawn with, its colours, the render region, resolution and quality |
| **Analyses** (static, modal, thermal, flow, all optimisations — [the result cache](#the-result-cache)) | in memory, and on disk: the solved problem with its mesh, fields, modes, iterations and steps | the whole problem as asked: the part by its expression, every condition, the material, the fluid, the element size, every setting |
| **`exact_distance`** (and so `offset_exact`, `shell_exact`, `round_edges`, `fillet`) | in memory | the shape's expression and the bounds/resolution |
| **Mesh imports** | in memory | path, size, modification time, units |
| **Graph lattices** (`voronoi_graph`, `surface_graph`, `points_graph`) | in memory | their inputs |
| **`mass_properties` / `volume_of`** | in memory | the shape, bounds and resolution |
| **Field queries** (`wall_thickness`, `curvature_field`, data fields) | in memory | they remember their answer at every point they were asked about — the same values, exactly |

The first opening of a STEP file is the slow one (the 90-part kitchen assembly takes about a minute and a
half). Afterwards the import is read from its cache, which is much faster, as long as the file and the
algorithm are unchanged.

## How keys work

A shape's **content key** is a 128-bit structural hash of its expression: every operation, every constant
bit-for-bit, every joint, and for the shapes that carry data (imported meshes, data fields, analysis results)
the key of that data. Two shapes built separately from the same expression have the same key; constants
differing in the seventh digit, another imported file, another data field all differ. A free variable
matches only itself, so a shape that holds one is not mistaken for another.

That is why an edit late in the script does not repeat the work above it: the analysis above the edited line
has the same key as before.

**The import cache** is keyed to the **import algorithm's version**, not to the program's build. Rebuilding
FielDes does not discard it; only a change to the algorithm's output does (and a new version says so in the
changelog). Drop it by hand with the **bin** in the model tree (Reset), by deleting the
`*.fieldes-cache.*` files, or with `import_step_parts(..., cache=False)`; `rev=` (the **⟳ Reimport** button)
forces a fresh reconstruction.

## Controlling the caches

```python
cache_info()        # {name: (entries, hits, misses)}
clear_caches()      # forget everything held in memory (to measure, or to free it)
```

The tessellating importer (`import_step_tessellated_parts`) keeps the tessellation of a file in the folder
`<file>.fieldes-tessellation` next to it, by the file's content, `quality` and the tessellation's version; `cache=False`
does not, `rev=` makes it again.

Analyses take `cache=False` to solve every time; `import_step_parts` takes `cache=False` or
`cache='path.py'`. Memory caches are bounded (least recently used goes first).

Developers writing their own expensive construction can use the same machinery from
`fieldes.stdlib.content_cache`: `@content_cached('name', limit=8)` on a function of shapes and numbers,
`shape_key(shape)`, `value_key(v)`, `problem_key(kind, **parts)`.

## The field cache

Every statement of the form `name = <expression>` whose value is a field is remembered, **by what it was made of**: its
text, the exact content of every name it reads (a field by the structural hash of its expression, a number by its bits,
lists, text, the library's own functions), the numbers of the script's `var()`s and the code that builds it (the library
and its Python files). There is nothing to switch on and no function to wrap: a lattice laid out on a bracket, an exact
offset or a thickness field is cached because it is a field, not because somebody wrote a cache for it. Run the same
statement with the same inputs and the field is not built again: in the same session the very same field is handed back,
and in a later session (after closing and opening FielDes) it is read from a file. What the statement printed is printed
again. A hit returns exactly what a run builds; nothing is sampled onto a grid.

What is not kept this way:

- a statement that calls something that reads or writes a file (`import_...`, `load...`, `read...`, `open`, `save...`,
  `write...`, `export...`) or `var()`, `expose()`, `handles()` — a file may have changed (imports have their own cache next
  to the file);
- a statement that reads a function or module of your own (it has no content to compare);
- a statement that took less than a quarter of a second (a file would be slower than the run).

A field is written to disk when **every part of it can be saved** (`libfive_tree_can_save`): operations, bodies, the
struts of a lattice (the cell map included), any expression of them. A field with a part that cannot be saved (a conformal
TPMS lattice, a field an analysis solved, a mesh distance field) is kept in memory for the session and is computed again in
the next one — the cache never writes a file it could not read back. The files are in `field-cache` beside the render
cache (`FIELDES_FIELD_CACHE_DIR` moves them); the folder holds at most 3 GB, oldest first out.
**Settings → Clear the caches** deletes them together with the kept meshes of the render cache.

A call of `lattice_surface_conform` has **one bar** from its first step to its last, so you can tell when that line of the
script will be over. The text says which of the steps it is in (`step 6 of 17: joining the samples into a surface`, with
the round or level it is at); the bar moves through every step's share of the whole and, inside a step, counts what the
step has really done (levels, rounds, field evaluations against the number the step needs). The shares are those of a
typical call, measured on the bracket and the pan (the joining of the samples and the two evening-out steps are most of
it), so on another body a step can end earlier or later than its share says: the bar never goes back and is full when the
call is over. When the layout has to be made again (a second attempt, text `attempt 2, ...`), it goes on from where the
first one left the bar and takes the rest.

## The result cache

An analysis is the slowest line of a script, and it is asked again every time the script runs. So **every analysis and
optimisation is kept**: `static_analysis`, `modal_analysis`, `thermal_analysis`, `fluid_analysis` (steady and in time),
`topology_optimization`, `thermal_topology_optimization` and `flow_topology_optimization`. The key is the whole problem
as asked — the part by the content of its expression, every support, load, condition and region, the material or the
fluid, the element size and every setting — before anything is built. In the same session the problem asked again is
handed back from memory; in a later session its solved form is read back from a file the kernel wrote: the mesh, every
field at the nodes, the elements' own values, the modes, the density after every iteration of an optimisation, the steps
of a flow and all the numbers. Nothing is meshed or solved again, and the result answers every question as the solved one
did (fields, `.verify()`, `streamlines()`, the Elements view). The log says `[result cache] read the static analysis
back (12.4 MB, 0.3 s)` when it did, `kept` when it wrote one. A change to any argument is a new problem — solved and kept
beside the old one, so going back to the old values costs nothing either. `cache=False` solves every time and keeps
nothing.

The keys of a result's fields (what the render cache and the field cache know them by) are made from the same problem
key, so a result read back has the same keys it had when it was solved: **the renders of it are found again too**, and a
field made from a result (`result.von_mises > 100`) is the same field in every session.

The files are in `result-cache` beside the field cache (`FIELDES_RESULT_CACHE_DIR` moves them); the folder holds at most
4 GB, oldest first out. A file that cannot be read as written is refused and the problem is solved. **Settings → Clear the
caches** deletes them with the rest.

## The render cache

Meshing is the slow part of showing a big shape, and the viewport's memory of it ends with the session. So **every
shape keeps its finished mesh on disk** (in FielDes's cache folder, never in your project) once it has taken a while to
mesh (a sphere is not worth a file), and is shown from there at once the next time it is rendered — when you open the
script again, or after you changed it and changed it back. It is **on** unless you turn it off: the model tree's
**cache button** on every shape (a stack of disks; a menu entry in the right-click menu too) writes the line
`part = render_cache(part, False)` under the shape's definition; click it again (or delete the line) to have it on
again. `part = render_cache(part)` says the default aloud and keeps the mesh however quickly it was made.

The mesh is looked up by what the shape *is*: its expression with the numbers it is drawn with (a dragged `var()`
is another shape), what colours it, the render region, resolution and quality, and the STEP files it uses (their size
and time). When **anything about the math changes** the key is another one: the shape is meshed again, shown as
usual, and the new mesh is kept. The button shows what happened — blue: on, green: the mesh on screen was read from
the cache, amber: this shape cannot be kept (hover for why).

`render_cache_key(shape)` shows the key a shape is kept by (the same text in every run for the same math, another one when
anything about the math changes, `None` when the shape cannot be kept).

A shape can be kept when everything it is made of can be recognised from one run to the next: operations, numbers,
`var()`s, imported meshes and parts, lattices and their surface coordinates, field-driven sizes. A shape shown with the
fields of an analysis (a result coloured by stress) cannot: the analyses have caches of their own. The folder holds at
most 4 GB (`FIELDES_RENDER_CACHE_MB` changes that), oldest files first out; **Settings → Clear the caches**
deletes all of it (and the field cache's files). The mesher's output changes now and then; the cache then starts anew by a version number
(`kRenderCacheVersion`), not on every build.

## Resolution and quality

The viewport meshes the script's shapes over `view.set_bounds(...)` at `view.set_resolution(...)`
(samples per mm) and `view.set_quality(...)` (1–10, mesh accuracy). The mesher splits the region in
halves until a cell is no larger than `1 / resolution`, so **the cost changes in steps** of four to six
times the vertices at every halving: doubling a resolution that just misses a halving costs nothing extra
until it crosses one. `roi_resolution` picks resolutions that are whole numbers of halvings.

- Too coarse: thin walls and small features vanish (a lattice with 0.6 mm walls wants 3 samples per mm or
  more).
- Too fine for the region: minutes of meshing (and gigabytes). Cancel with `Esc` and lower it.
- For imports, prefer `roi_resolution(model)` — every part gets its own resolution. See
  [Region and resolution](step-import.md#region-and-resolution).
- `view.set_quality(8)` is a good default; lower numbers are faster and rougher.
- **Quality of the exact STEP surface** is separate: `exclude(..., quality=64)` (points per full turn of a
  circle). The locked field of an excluded STEP part (its tessellation and the distance structure of the
  triangles) is made once per part and kept in memory while the script is run again; the kernel keeps the
  tessellation per file version.

## Long renders

- The status bar and the thin progress bar show what is meshing; the output pane shows the statement
  (`3/12`) and the progress of an operation inside it.
- `Esc` cancels a slow render.
- **Breakpoints** (`F9`) let you look at a half-built model while something later is slow.

## Making things faster

1. **Import once, tune later.** The STEP import is the slowest step. Keep the `import_step_parts` line
   unchanged while you work on what comes after it.
2. **Render only what you need.** Hide what you are not working on (the eye in the model tree), set the
   region with `view.set_bounds` around the part you are looking at.
3. **Coarse first.** Lower `view.set_resolution` and the quality while composing, raise them for the final
   picture or STL.
4. **Analyses: coarse `element_size` first**, then halve it once to check convergence.
5. **Put expensive, rarely changed operations early** in the script, where caches keep them.
6. **Use the exact operations sparingly**: `offset_exact`, `round_edges`, `fillet` mesh the shape at a
   fine resolution. Use them on the finished shape, not inside a loop.
