# Meshes

FielDes models with fields, and moves between fields and triangle meshes in both directions.

- [Importing meshes](#importing-meshes)
- [Exporting and importing meshes](#exporting-and-importing-meshes)
- [Analyses: models and results](#analyses-models-and-results)
- [Fields on a grid](#fields-on-a-grid)
- [Getting a mesh in a script](#getting-a-mesh-in-a-script)
- [Round trip](#round-trip)

## Importing meshes

**File → Import model…** (`Ctrl+I`) or drop a file on the window. FielDes writes

```python
# Imported model: bracket.stl
bracket = import_model(r"C:\models\bracket.stl", file_units="mm")
bracket_0 = bracket[0][0]
bracket_0
view.set_quality(8)
```

`import_model(path, units='mm', file_units=None, rev=None)` is the one importer for every model (see
[Importing models](step-import.md)); given a mesh it reads **STL** (binary or ASCII), **OBJ**, **PLY**
(ASCII or binary), **3MF**, **glTF** (`.glb`, `.gltf`), **FBX** (binary or ASCII), **OFF** and **DXF** (`3DFACE` entities
and polyface meshes). The mesh becomes an **exact signed distance
field** (negative inside), so it works with everything else: CSG with other shapes, `offset`, `shell`,
blends, transforms, lattices, analyses.

Before that the triangles are cleaned: duplicate vertices welded, degenerate triangles dropped, windings made
consistent, inside-out shells flipped. A mesh **with holes** still imports: near a hole, inside and outside
come from the generalised winding number, which closes it with a smooth membrane. The winding number also
decides wherever the nearest feature of the mesh has no clear side -- a sliver folded back over its neighbour or a
knife edge, as a dual-contouring mesher leaves along sharp edges -- so a mesh exported from a part reads back with
the part's inside and outside.

**Units.** STL, OBJ, PLY and OFF carry none: they are read as millimetres unless you say
`file_units='m'` (or `'in'`, `'cm'`). 3MF, FBX (its unit scale) and DXF (`$INSUNITS`, when the file has it) state their unit, and glTF is always in
metres; that is used unless `file_units=` overrides it. glTF's and FBX's Y-up axes become Z-up (an FBX that says Z up is left as it is). `units=` is the
unit of the script (`'mm'`).

**Model tree flags.** An **amber** row means the mesh is open or looks suspiciously small for its units: hover
it for the details. `mesh_info(path)` returns the summary of the last import (triangle count, whether it
is watertight, …) as a dict.

The result is [cached](caching-and-performance.md) by the file's path, size and modification time; `rev=`
makes the script re-read a file that changed on disk.

## Exporting and importing meshes

- **File → Export mesh…** (`F7`, `Ctrl+Alt+E`) writes the shapes shown in the viewport as one mesh, in the format of the file's extension.
- **Right-click a model → Export → Mesh** writes that one model (the file is asked for, and written in the background).
- From a script:

```python
export_mesh(part, "part.stl")                              # the extent of the part, the scene's resolution
export_mesh(part, "part.glb", bounds=(lo, hi), resolution=4)
export_mesh([a, b], "assembly.fbx")                        # several bodies, one file
```

`bounds` is the region meshed (`((x0, y0, z0), (x1, y1, z1))`; a body that knows its extent needs none), `resolution` is samples per mm (larger is
finer and slower; the scene's resolution when not given). The older `part.save_stl(path, lo, hi, resolution, quality)` still works.

| Format | Extension | Written as | Read |
|---|---|---|---|
| STL | `.stl` | binary (`ascii=True` for text), millimetres by convention | yes |
| Wavefront OBJ | `.obj` | text, millimetres by convention | yes |
| Stanford PLY | `.ply` | binary little endian, millimetres by convention | yes |
| 3D Manufacturing Format | `.3mf` | states its unit: millimetre | yes |
| glTF 2.0 | `.glb`, `.gltf` | metres, Y up (the numbers are turned into them) | yes |
| **Autodesk FBX** | `.fbx` | ASCII 7.4, Z up, unit scale 0.1 cm (= 1 mm) | yes (binary and ASCII) |
| OFF | `.off` | text, millimetres by convention | yes |
| DXF | `.dxf` | `3DFACE` entities, `$INSUNITS` 4 (millimetres) | yes (`3DFACE`, polyface meshes) |

**FBX is a proprietary format** of Autodesk, whose SDK is available under its own licence and whose files have no published specification. FielDes
reads and writes FBX **without the Autodesk SDK**, from the structure of the files as the community documents it (the node tree, the property types,
the objects and their connections), and handles **meshes only**: geometry, the transform of the models that hold it, the scene's unit and up axis -- no
materials, animation, skins or cameras. Because the format is not specified, another program may read less of what FielDes writes than FielDes reads of
its own files: STL, OBJ, PLY, 3MF and glTF are the safer ways to hand a model on. DXF is the same: only meshes made of faces.

When no `bounds` is given a body is meshed in its own extent plus one cell all round (a face that lies exactly on the edge of the region meshed is not found by it).

Other files of a body: `export_print(result, "part.3mf")` (the part, its supports and no-support volumes as separate objects: see [manufacturing](manufacturing.md)), `export_layers(part, "layers.dxf", layer_height=0.2)` (the
contours of each layer as closed polylines) and `export_slice(part, "slice.svg", z)` (one layer as a picture). The layers are read from the body's field, not from a mesh, and are not G-code.

## Analyses: models and results

A static analysis can be handed to another solver, and what any analysis found can be taken out as files:

```python
result = static_analysis(bracket, supports=[support], loads=[load], material=steel, element_size=2)

export_fea(result, "bracket.inp")        # Abaqus and CalculiX: C3D4 tetrahedra, *MATERIAL, *NSET of each support and load, *BOUNDARY, *CLOAD
export_fea(result, "bracket.bdf")        # Nastran: CTETRA, MAT1, SPC1, FORCE (solution 101)
export_fea(result, "bracket.msh")        # Gmsh 2.2: the mesh with the displacement, the supports and every field of the result as data

export_results(result, "bracket.vtu")    # VTK / ParaView: the mesh, the displacement and every field at the nodes, every field in each element
export_results(result, "bracket.csv")    # the nodes and (next to it, bracket_elements.csv) the elements as tables
export_results(result, "bracket.txt")    # the key numbers as text
```

The model is the one FielDes solved (the same nodes and tetrahedra; every tetrahedron right-handed). A force is spread over the nodes of the surface it
acts on **by the area each one carries**, so the loads add up to the force. Units are millimetres, newtons, megapascals and tonnes. A material with
a **field** (a graded stiffness) is refused (a model file holds numbers), and a `thermal_expansion` load is said in a note, not written. The `.vtu`, `.csv`
files hold the tetrahedral results of a static analysis; **`.txt` works for every result**:

```
FielDes results: static analysis (steel)

maximum von Mises stress    42.33 MPa
maximum displacement        0.0549 mm
safety factor               5.906
support reaction (x, y, z)  (-6.406e-05, 8.735e-06, 300) N
```

`result_summary(result)` returns the same numbers as `[(label, text, unit)]`, and the result card prints the first few of them on the screen. A flow has
`drag_force`, `lift_force` and `drag_coefficient`, `lift_coefficient` when its inlets say which way the fluid goes (`inlet(..., velocity=(vx, vy, vz))`, or
a `speed=` between the inlets' and the outlets' middles): the force on the walls along the flow, and along the direction across it that is most nearly Z,
divided by the dynamic pressure of the mean inlet speed and the area of the body's bounding box seen along the flow.

## Fields on a grid

```python
export_field(result.von_mises, "stress.vti", bounds=((0, 0, 0), (60, 20, 10)), resolution=1)   # VTK image data
export_field(swell, "swell.csv", bounds=(lo, hi))                                                 # x, y, z, value
```

A field is not a mesh: this samples it on a regular grid (at most two million points) for programs that read grids, at the resolution you ask for.

## Getting a mesh in a script

`verts, tris = shape.get_mesh(lo, hi, resolution)` returns the vertices (`(x, y, z)`) and triangles (index
triples) of a shape as lists, for scripts that post-process the geometry (`algorithm='simplex'` or `'hybrid'`
are slower but more robust on awkward fields).

## Round trip

`examples/12_mesh_export_and_import.py` exports an imported STEP part as STL and reads it back with
`import_model`.
