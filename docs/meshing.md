# Meshing: a body as a mesh of elements

`mesh_volume(body, ...)` makes the inside of a body a mesh of elements -- tetrahedra, quadratic tetrahedra, hexahedra, and the prisms of boundary layers -- with named zones on
its boundary, and writes it for the solver of another program: Fluent, OpenFOAM, Gmsh, Abaqus, Nastran, I-DEAS universal (Salome, Elmer, Code_Aster), VTK. `mesh_surface` makes the
surface alone, of triangles or quadrilaterals. It is in the **Engineering** menu, in its own category, **Mesh**, and every call is plain script:

```python
from fieldes import *

part = difference(box((0, 0, 0), (60, 40, 6)), cylinder(8, 6, (30, 20, 0)))      # a plate with a hole
mesh = mesh_volume(part, element_size=1.5)
print(mesh)                                  # Mesh mesh: 55,880 tet4 cells (element size about 1.5 mm), 12,420 nodes; boundary 10,432 tri3; zones wall (10,432)
print(mesh.quality().report())               # skewness, orthogonal quality, aspect ratio ... and what they mean for a solver
mesh.export('plate.msh')                     # Gmsh;  .vtu .inp .bdf .unv .stl .obj;  format='fluent', format='openfoam'
mesh                                         # the part, with a card whose "Elements" show every cell, coloured by its size or its quality
```

A mesh has **no material, no conditions and no solver settings**: it is the description of the elements and of where they are, in **millimetres** like everything in FielDes
(`scale=0.001` writes metres).

## How it is made

The body's own field is meshed by **isosurface stuffing** (Labelle and Shewchuk, SIGGRAPH 2007): a body-centred cubic lattice is laid over it, the points where it crosses the
lattice's edges are found on the actual field, the lattice points near the surface are moved onto it, and the tetrahedra the surface still cuts are cut along it. The tetrahedra are
conforming (every inner face is shared by exactly two), right-handed, and their boundary vertices lie on the surface -- it is not a voxelisation. Sharp edges and corners are followed to
about the element size: **an edge that runs between lattice points is rounded within one element** (a box 20 x 10 x 6 mm meshed with 2.4 mm elements is 1.4 % short of its volume; with
an element size that divides its sides it is exact). Everything else that is asked of the mesh is done to that mesh:

| Asked | What is done |
|---|---|
| `element='tet10'` | a node in the middle of every edge; those of the boundary are put on the surface |
| `element='hex'` | every tetrahedron cut into four hexahedra (a corner each), with the nodes of the middle of its edges, faces and itself: all-hexahedral, conforming, the quality of the tetrahedra it came from -- not the quality of a hex-dominant mesher's |
| `element_size=<field>` | the mesh starts at the largest size the field asks for and the longest edge of a tetrahedron is bisected, with the tetrahedra round it, until its edges are no longer than the size asked for there (Rivara's longest-edge bisection); the new vertices of the boundary go to the surface, then the interior nodes are moved to the middle of their neighbours where that makes the worst of their tetrahedra better |
| `boundary_layers=` | prisms between the surface of a solid and a core that is the body with that much taken off its surface |
| `zones=` | each face of the boundary is put in the first region that holds its middle |

## Element size

`element_size` is a number (mm), or a **field**: a body whose value at a point is the element size there, a function of a list of points that returns their sizes, or
`graded_size(...)`.

```python
mesh = mesh_volume(air, element_size=2.0)                                    # one size
mesh = mesh_volume(air, graded_size(body, wall_size=0.5, growth_rate=1.2, max_size=8))    # 0.5 mm at the surface of the body, growing by 0.2 mm for every mm from it
mesh = mesh_volume(air, lambda points: [0.8 + 0.08 * (x + 10) for x, y, z in points])    # your own function
mesh = mesh_volume(air, 1.0 + 0.05 * distance_to_point((0, 0, 0)))            # a field made of the library's own
```

Without one the default is about 40 elements along the longest side, no more than about a hundred thousand tetrahedra (a plain 10 mm cube: 107,000 tetrahedra, about 20 seconds; the elements are drawn and the quality numbers are made). Give `element_size` for a finer mesh. A graded mesh is made by bisecting, and bisection gives some poorer tetrahedra than
a uniform lattice does, so the mesh is **mended** afterwards (`optimize=True`, the default), in two rounds of: the nodes on the surface that belong to poor tetrahedra move along the surface
(toward the middle of their neighbours, and back onto the surface; a node on a crease stays), the interior nodes of poor tetrahedra move toward the middle of their neighbours -- `smooth=`
rounds of it in each pass (6 by default) -- and edges and faces are swapped and interior nodes taken into a neighbour where that makes the worst tetrahedron round them better.
Nothing moves or changes unless it makes the worst tetrahedron round it better, the boundary stays the part's surface, and the nodes that no cell uses are taken out. A tetrahedron is
poor when its mean ratio (12 (3 V)^(2/3) over the sum of the squared edges; 1 is regular) is below 0.6. `optimize=False` leaves the mesh as it comes, which is much faster. The skewness of
a graded channel in example 36 (30 000 tetrahedra): 160 cells above 0.95 and 724 more between 0.85 and 0.95 before, see *Quality* for the numbers after.

## Boundary layers

```python
air = difference(box_of_air, body)
mesh = mesh_volume(air, 2.0, boundary_layers=inflation_layers(body, first_height=0.05, growth_rate=1.2, layers=6))
```

`inflation_layers(on, first_height, growth_rate, layers)`: `layers` layers of prisms on the surface of the solid `on`, growing into the mesh, the one at the wall `first_height` thick
and each one `growth_rate` times as thick as the one before. The first height is what sets y+ (about y+ times the viscosity over the friction velocity). How they are made:

1. the core is the body with the total thickness taken off the surface of `on` (the exact distance to it);
2. the core is meshed as any body is (with the element size asked for);
3. the faces of the core that lie at that distance are the bases of columns of prisms; a node of one is moved to the wall -- a step against the gradient of the distance, then Newton steps
   onto the surface -- and the layers are the points between;
4. the faces of the boundary are found again from all the cells (the prisms' sides on a symmetry plane are faces of it), and given their zones.

So the layers follow the wall and share their faces with the core exactly. What it does not do: **a sharp convex corner of the wall has no direction** (the distance's gradient is not
defined there) and its prisms are skewed -- round the corners of a body that needs good layers -- and the total thickness must be less than half of the thinnest passage of the fluid. The
quality report says how many prisms are inside out. Only `element='tet'` has layers (the prisms and the core share triangles).

## Zones

```python
mesh = mesh_volume(air, 2.0, zones={'inlet': inlet_region, 'outlet': outlet_region, 'symmetry': half_space(...)}, feature_angle=30)
```

A face of the boundary is in the first region of `zones` that holds its middle (the region's value is at most 0, or within 2 % of an element of it: a region that is a thin slab on
the face, or the face's own plane, is enough). What no region holds is `default_zone` (`'wall'`); `feature_angle` (degrees) splits that into pieces that are smooth -- two faces that
share an edge are one piece when the angle between their normals is less than that -- named `wall_1`, `wall_2`, ... largest first. A piece that is a strip narrower than two edges
of its own faces (or has fewer than four faces) is not a patch: the surface a lattice rounds at an edge, and the rim where prism layers meet a plane, make strips like that, and they join
the piece they share most of their edge with. A small patch that you want as a zone of its own is a region in `zones`. `cell_zones={'solid': region}` is the same for cells (the
rest are `'fluid'`). The zones are what the files call patches, boundary conditions, physical groups, node sets and element sets.

## Quality

`mesh.quality()` measures every cell (the numbers that look at the neighbours of a cell are made for meshes of up to 150 000 cells, or with `full=True`) and `.report()` says what they
mean:

| number | 1 or 0 | what a solver wants |
|---|---|---|
| **skewness** (volume skewness for a tetrahedron, angle skewness for the others) | 0 is regular, 1 degenerate | the worst below 0.95; for CFD below 0.85 |
| **orthogonal quality** (Fluent's: the cosines between a face's area vector and the way to the face's middle and to the neighbour's) | 1 is best, 0 worst | the worst above 0.1 for CFD |
| **aspect ratio** (longest edge over shortest) | 1 is regular | up to 5 in the core; boundary-layer cells are stretched on purpose |
| **volume ratio** (a cell and the ones it touches) | 1 is smooth | 5 or less |
| **smallest dihedral angle** of a tetrahedron (degrees) | 60 is regular | above 20 |

`quality().negative` counts the elements that are inside out or flat: a solver refuses a mesh that has one. The report also says how many cells are above skewness 0.85 and 0.95
(`quality().skewed` is those two counts and the number of cells measured). **What FielDes reaches** (example 36, 30 000 tetrahedra and 3 000 prisms; the numbers are those of
`tests/internal/meshing_checks.py` demands the first three): no element inside out, the worst skewness below 0.95 (0.92), at most 1 % of the cells above 0.85 (0.35 %: 114 of 32 274), the worst
orthogonal quality 0.36, the volume ratio at most 4.6, the smallest dihedral angle 16.8 degrees. As the bisection leaves the same tetrahedra, 3 % of them are above 0.85 and 160 above 0.95. That is a mesh a
solver accepts and one that CFD guidelines call usable, not the polished mesh of a dedicated mesher (a thousand cells between 0.7 and 0.85 remain, and a tetrahedron is never as orthogonal as a
hexahedron): when the results depend on the mesh, refine it and compare.

## Files

```python
mesh.export('part.msh')                                      # by the ending of the path
mesh.export('flow.msh', format='fluent')
mesh.export('case/constant/polyMesh', format='openfoam', scale=0.001)      # metres, as OpenFOAM works
export_mesh(mesh, 'part.vtu')                                # the same through export_mesh
```

| format | file | what is in it |
|---|---|---|
| `'gmsh'` | `.msh` (version 2.2, ASCII) | nodes, cells and boundary faces; the zones as physical groups |
| `'vtk'` | `.vtu` (XML, ASCII) | the cells (or the faces of a surface mesh), `zone` and `dimension` as cell data; `boundary=True` adds the faces; the names of the zones in a `.zones.txt` beside it |
| `'abaqus'` | `.inp` | `*NODE`, `*ELEMENT` (C3D4, C3D10, C3D6, C3D8; S3, S4 for a surface), element sets of the cell zones, node sets of the boundary zones |
| `'nastran'` | `.bdf` (free format) | GRID, CTETRA, CPENTA, CHEXA, a PSOLID for every cell zone |
| `'unv'` | `.unv` | nodes, elements (tetrahedra, hexahedra, triangles, quadrilaterals), the zones as groups (no quadratic tetrahedra, no prisms) |
| `'fluent'` | `.msh` (ASCII) | the nodes, the cells, and the faces of every zone with a boundary type: `boundary_types={'inlet': 'velocity-inlet'}`; a name that starts with `wall` is a wall, `inlet` a velocity inlet, `outlet` a pressure outlet, `symm` a symmetry |
| `'openfoam'` | a folder | `points`, `faces`, `owner`, `neighbour`, `boundary` (the zones are the patches: `patch_types={'sides': 'symmetryPlane'}`; `wall` for names that start with wall), `cellZones`; internal faces in upper-triangular order, every face's normal out of its owner |
| `'stl'`, `'obj'` | the boundary faces (a quadrilateral as two triangles); `obj` has a group for every zone | |

`.msh` is Gmsh's; ask for Fluent's with `format='fluent'`. `zones=False` leaves the zones out. The cells and faces of the Fluent and OpenFOAM files are written from the cells' own faces, and
every cell of them is checked closed (the faces' area vectors, outward, add up to zero) -- see the probe in `tests/`. The ordering of the nodes of a prism follows the usual convention (the
first triangle's normal toward the second); **a solver that reads prisms has the last word**: open the file and look at the volumes it reports.

## For CFD

1. mesh the fluid, not the body: `air = difference(domain, body)`;
2. `zones=` for the inlet, the outlet and the symmetry planes; everything else is `wall` (the faces of the body);
3. `inflation_layers(body, first_height=..., growth_rate=1.2, layers=...)` with a first height for the y+ you want, and `graded_size(body, wall_size=..., growth_rate=1.2, max_size=...)` so that
   the core is fine near the wall and coarse away from it (with the layers' total thickness about the size of the first core cell);
4. `quality().report()`: skewness below 0.85, orthogonal quality above 0.1, no prism inside out;
5. `export(..., format='fluent')` or `format='openfoam', scale=0.001`.

## Limits

* The mesher is isosurface stuffing: **features thinner than the element size can fall between lattice points and be lost**, and sharp edges are rounded within an element. Mesh at an
  element size that is a third of the thinnest wall, or less.
* The boundary of a graded mesh is the part's surface by Newton steps along the field's gradient: a field that has no gradient there (a constant, the maximum of planes at an edge) snaps
  to the nearest face.
* Hexahedra are tetrahedra cut into four: all-hex, never hex-dominant, and as skewed as the tetrahedra were. Quadrilateral surfaces are triangles cut into three.
* The meshing is Python on the kernel's lattice: about 30 000 tetrahedra a second for the lattice itself. Example 36 (30 000 tetrahedra and 3 000 prisms, graded, mended) is meshed in about
  6 s, its quality report takes 2 s and its four files a few seconds. The mending is made of Python loops over the cells: a mesh of a few hundred thousand tetrahedra takes minutes, and
  `optimize=False` leaves it out. A mesh of more than 400 000 cells is not drawn (the viewer's elements), and 150 000 cells is where the quality numbers that need the neighbours are made unless asked.
* Boundary layers: see above (convex corners, thickness against the thinnest passage, tetrahedra only).
* No periodic faces, no CGNS, no cell zones by the meshing of several bodies together; a mesh is one body.
