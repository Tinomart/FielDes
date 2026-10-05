# Meshes

FielDes models with fields, and moves between fields and triangle meshes in both directions.

- [Importing meshes](#importing-meshes)
- [Exporting STL](#exporting-stl)
- [Getting a mesh in a script](#getting-a-mesh-in-a-script)
- [Round trip](#round-trip)

## Importing meshes

**File → Import model…** (`Ctrl+I`) or drop a file on the window. FielDes writes

```python
# Imported model: bracket.stl
bracket, bracket_bounds = import_mesh(r"C:\models\bracket.stl", file_units="mm")
bracket
view.set_bounds(*roi(bracket_bounds))
view.set_resolution(roi_resolution(bracket_bounds))
view.set_quality(8)
```

`import_mesh(path, units='mm', file_units=None, rev=None)` reads **STL** (binary or ASCII), **OBJ**, **PLY**
(ASCII or binary), **3MF** and **glTF** (`.glb`, `.gltf`). The mesh becomes an **exact signed distance
field** (negative inside), so it works with everything else: CSG with other shapes, `offset`, `shell`,
blends, transforms, lattices, analyses.

Before that the triangles are cleaned: duplicate vertices welded, degenerate triangles dropped, windings made
consistent, inside-out shells flipped. A mesh **with holes** still imports: near a hole, inside and outside
come from the generalised winding number, which closes it with a smooth membrane. The winding number also
decides wherever the nearest feature of the mesh has no clear side -- a sliver folded back over its neighbour or a
knife edge, as a dual-contouring mesher leaves along sharp edges -- so a mesh exported from a part reads back with
the part's inside and outside.

**Units.** STL, OBJ and PLY carry none: they are read as millimetres unless you say
`file_units='m'` (or `'in'`, `'cm'`). 3MF files state their unit, and glTF is always in metres; that is used
unless `file_units=` overrides it. glTF's Y-up axes become Z-up. `units=` is the unit of the script
(`'mm'`).

**Model tree flags.** An **amber** row means the mesh is open or looks suspiciously small for its units: hover
it for the details. `mesh_info(path)` returns the summary of the last import (triangle count, whether it
is watertight, …) as a dict.

The result is [cached](caching-and-performance.md) by the file's path, size and modification time; `rev=`
makes the script re-read a file that changed on disk.

## Exporting STL

- **File → Export STL…** (`Ctrl+E`, `F7`) writes the shapes shown in the viewport.
- From a script:

```python
part.save_stl("part.stl", lo, hi, resolution=4, quality=8)
```

`lo`/`hi` are the corners of the region meshed, `resolution` is samples per mm (larger is finer and slower),
`quality` is the negative order of magnitude of the maximum error. Use the file's own bounds from
`import_step_parts` (`part, (lo, hi) = …`).

STL carries no units: it is written in millimetres.

## Getting a mesh in a script

`verts, tris = shape.get_mesh(lo, hi, resolution)` returns the vertices (`(x, y, z)`) and triangles (index
triples) of a shape as lists, for scripts that post-process the geometry (`algorithm='simplex'` or `'hybrid'`
are slower but more robust on awkward fields).

## Round trip

`examples/12_mesh_export_and_import.py` exports an imported STEP part as STL and reads it back with
`import_mesh`.
