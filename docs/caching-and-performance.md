# Caching and performance

A FielDes script runs again after every edit, and a few of its operations are expensive: reading a large STEP
file, meshing a shape, solving an analysis. The caches make the second run cheap **without changing any
result**: every cache is keyed by *exactly what the result was built from*, never by a name or a printed
approximation, and a hit returns the very thing a miss built. Nothing is ever sampled onto a grid to save time.

- [What is cached](#what-is-cached)
- [How keys work](#how-keys-work)
- [Controlling the caches](#controlling-the-caches)
- [Resolution and quality](#resolution-and-quality)
- [Long renders](#long-renders)
- [Making things faster](#making-things-faster)

## What is cached

| What | Kept | Key |
|---|---|---|
| **STEP import** (the rebuilt fields) | on disk, next to the STEP file: `<file>.fieldes-cache.py` and `.fieldes-cache.trees` | the file's SHA-256, the import-algorithm version, `rev=` |
| **Shapes already meshed in the viewport** | in memory, for the session | the shape's expression (structural identity) and its colouring |
| **Analyses** (static, modal, thermal, all optimisations) | in memory | the whole problem: part, supports, loads, material, element size |
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

Analyses take `cache=False` to solve every time; `import_step_parts` takes `cache=False` or
`cache='path.py'`. Memory caches are bounded (least recently used goes first).

Developers writing their own expensive construction can use the same machinery from
`fieldes.stdlib.content_cache`: `@content_cached('name', limit=8)` on a function of shapes and numbers,
`shape_key(shape)`, `value_key(v)`, `problem_key(kind, **parts)`.

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
  circle).

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
