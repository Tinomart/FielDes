# Meshing: a body as a mesh of elements

A body is meshed by the function that makes the elements it needs, each with only the arguments that way of meshing uses. **Volume**: `mesh_tet4` (tetrahedra of 4 nodes), `mesh_tet10` (tetrahedra of 10 nodes), `mesh_hex_quanta` (hexahedra of 8 nodes), `mesh_hex20_quanta` (hexahedra of 20 nodes), `mesh_cartesian` (hexahedra inside and the cells the surface cuts at the surface) and `mesh_hex_extrusion` (hexahedra swept along a prism). **Surface**: `mesh_surface_tri` (triangles) and `mesh_surface_quad` (quadrilaterals). A mesh has named zones on its boundary and is written for the solver of another program: Fluent, OpenFOAM, Gmsh, Abaqus, Nastran, I-DEAS universal (Salome, Elmer, Code_Aster), VTK. Every one of them is in the **Engineering** menu, in its own category, **Mesh**, which has the two sections **Volume** and **Surface** and an entry of its own for each function, and every call is plain script:

```python
from fieldes import *

part = difference(box((60, 40, 6), position=(30, 20, 3)), cylinder(8, 6, position=(30, 20, 3)))      # a plate with a hole
mesh = mesh_tet4(part, element_size=1.5)
print(mesh)                                  # Mesh mesh: 52,607 tet4 cells (element size about 1.5 mm), 11,901 nodes; boundary 10,508 tri3; zones wall (10,508)
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
about the element size (the nodes on the rim of a flat face -- the circle where a round hole meets the face of a plate -- are put on it by a pass of the kernel's own -- the same mesher for an analysis and for `mesh_tet4`, in `kernel/src/fea/tetmesh.cpp`, which also puts the nodes of the faces that lean away from the surface on the edges and corners that the lattice cut across: the field is zero all over a flat face, so their
distance to the rim cannot be read at the node): **an edge that runs between lattice points is rounded within one element** (a box meshed with elements that do not divide its sides is a little short of its volume; with
an element size that divides its sides it is exact). Everything else that is asked of the mesh is done to that mesh:

| Asked | What is done |
|---|---|
| `mesh_tet10(body)` | a node in the middle of every edge of the tetrahedra; those of the boundary are put on the surface |
| `mesh_hex_quanta(body)` | hexahedra only, the cubes of a Cartesian grid (below): a cube the body holds, and a cube the surface cuts when `quanta` of it is inside -- Salome's Body Fitting with Quanta. Every cell is a perfect cube (aspect ratio 1, skewness 0, scaled Jacobian 1); the boundary is a staircase of whole cubes |
| `mesh_hex20_quanta(body)` | the same cubes with a node in the middle of every edge (the 20-node element) |
| `mesh_cartesian(body)` | a grid of cubes cut by the body (below): perfect hexahedra inside, and where the surface cuts a cube the polyhedron of what is inside it -- named a hexahedron, tetrahedron, prism or pyramid by its nodes and faces, else a polyhedron -- with its nodes on the surface |
| `mesh_hex_extrusion(body, axis)` | a body that is a prism along `axis`: the quadrilaterals of its end face swept along it, the surface exact (below; Salome's Extrusion 3D) |
| `element_size=<field>` | the mesh starts at the largest size the field asks for and the longest edge of a tetrahedron is bisected, with the tetrahedra round it, until its edges are no longer than the size asked for there (Rivara's longest-edge bisection); the new vertices of the boundary go to the surface, then the interior nodes are moved to the middle of their neighbours where that makes the worst of their tetrahedra better |
| `boundary_layers=` | prisms (`mesh_tet4`) or hexahedra (`mesh_hex_extrusion`) between the surface of a solid and a core that is the body with that much taken off its surface |
| `zones=` | each face of the boundary is put in the first region that holds its middle |

**In the Engineering menu.**  The Mesh category has two sections. **Volume** has `mesh_tet4`, `mesh_tet10`, `mesh_hex_quanta`, `mesh_hex20_quanta`, `mesh_cartesian` and `mesh_hex_extrusion`; **Surface** has `mesh_surface_tri` and `mesh_surface_quad`. Each entry writes the call of its own function into the script, with the arguments that way of meshing takes, to be edited like any other call. A mesh of the Surface section is drawn as its own triangles or quadrilaterals, each coloured by its size.

## A Cartesian grid cut by the body

`mesh_cartesian` is the method of Salome's **Body Fitting** (`StdMeshers_Cartesian_3D`: "hexahedrons of a Cartesian grid in the internal part of geometry and polyhedrons and other types of elements at the
intersection of Cartesian cells with the geometrical boundary"), made on the body's field:

1. **The grid**: cubes of `element_size` over the box of the body (one cube more all round), a whole number along each axis -- so the faces of a box lie on the planes of the grid. A cube with all its corners
   inside the body is a **hexahedron**: the inside of the mesh is a grid of perfect cubes, aspect ratio 1, scaled Jacobian 1.
2. **The crossings**: every edge of a cube that runs from a node inside the body to one outside it is crossed by the surface, and the crossing is found on the field (halving, 28 times). A node of the grid within a tenth of a cube
   of the surface, inside it or just outside, is on it, and the crossing of a link that ends in it is that node, so that no cell has an edge shorter than a tenth of a cube.
3. **The cut cubes**: a cube the surface cuts is the **polyhedron of what is inside it**: the part of each of its six faces that is inside (a polygon of the corners that are inside and the crossings of its sides; when
   two opposite corners are inside the field at the middle of the face says whether they are joined), and the polygon of the surface between the crossings. Its nodes are the corners that are inside and the crossings, so every
   node of the boundary lies on the surface (step 6 puts the nodes the snap took onto it). It is named by what it is made of: 8 nodes and six quadrilaterals a hexahedron, 4 nodes a tetrahedron, 6 nodes with two triangles a prism, 5 nodes with a quadrilateral
   a pyramid, anything else a polyhedron (a cube cut at two places that do not meet is two cells).
4. **The edges** (Salome's "Implement Edges", on): where the cut across a face of the grid joins two crossings whose surface normals differ by more than 40 degrees and the middle of the straight line between them is far
   from the surface -- a corner, not a curve -- a node is put on the edge of the body, where the two sides of the cut meet in the plane of the face, and the polygon of the surface in the cell is cut in two along the line between the
   two such nodes. An edge that lies on the planes of the grid needs none; one that does not (a rim of a tilted cylinder, a turned box) is followed. A node is never put on a side of the face (an edge of the grid, which the
   cells on the other side of it share and would not have the node on): that made faces that did not match across a cell, a spike of no width in the outline of 164 faces of the real bracket at 2 mm, and 479 faces
   counted as boundary that were not.
5. **The threshold**: a cut cell that holds no more than 1 / `threshold` of a cube is **joined** to the cell it shares the most face with (a whole cube or another cut cell, not past two cubes when there is another),
   so that no cell is smaller. The default is 10, a tenth (Salome's is 4). Salome leaves such a cell out and the faces of its neighbours become the boundary there: a staircase that lay up to 0.65 of a cube off the surface
   on the real bracket (4 mm), in faces that lean up to 45 degrees from it; joining keeps the polygon of the surface the small cell had. `threshold=4` makes no cell smaller than a quarter of a cube and the volume
   ratio of neighbouring cells at most 5, at the price of more joined cells, which are polyhedra that are not convex (the worst orthogonal quality of a plate with a hole 0.50, where 10 gives 0.89). `threshold=1e9`
   keeps every cut cell as a cell of its own, slivers included. A cut cell that is a wedge (scaled Jacobian under 0.2) is joined too, whatever it holds.
6. **The nodes on the surface**: the nodes of the grid that step 2 took for on the surface (within a tenth of a cube of it) are put on it, along the field (a node whose move would bend a whole cube, scaled Jacobian under 0.3,
   stays), so that every node of the boundary is on the surface; a face of no area (three nodes on one straight line) is taken out, its middle node going into the faces that have its long edge.
7. **The edge pass** (the tetrahedra's `snapFeatures`, for the cut cells): a face of the skin whose normal differs from the direction of the field at its middle by more than 12 degrees names the nodes to look at. The
   faces round such a node that do not lean say which planes meet there (their normals, grouped within 40 degrees); the node goes to the point of the line where two of them meet, nearest to it, is put back on the surface
   along the field, and stays where it was when that point is not on the surface or is farther than 0.6 of a cube. The moves are made one node at a time against the cells as they are: a move that leaves a cell with less
   than 0.7 of its volume, or under a tenth of a cube when it had more, or a hexahedron, prism, pyramid or tetrahedron with a scaled Jacobian under 0.3 (or under 0.9 of what it was), or turns a face over, or squeezes it
   to a quarter of its area, is tried at half the way and then not made. `FIELDES_CART_EDGES=0` turns the pass and the collapse off.
8. **The collapse** (the tetrahedra's `collapseBites`): two nodes of a cell that stand closer than a tenth of a cube -- an edge node and a grid node beside it, or a node that the edge pass has put on the place of another --
   become one: the grid node goes into the node on the crease, the faces round it are written again, a cell that has both becomes the polyhedron that is left (a hexahedron that lost an edge is a prism), and the move is
   refused under the same floors as the edge pass. Without it an edge of no length stood in 63 cells of the bracket at 2 mm (an aspect ratio of 1.8e5); with it the largest aspect ratio is 31.5.

What this did on the real bracket of example 05 (library probe, the skin faces whose normal differs from the field's direction at their middle by more than 12 degrees; the same part meshed with tetrahedra: 3.7 % at 4 mm and
0.3 % at 2 mm): at 4 mm 7.5 % (22.5 % when small cells were left out and nothing was snapped), at 2 mm 2.2 % (14.1 %); the farthest skin node from the surface 0.0004 mm (2.58 and 1.01 mm); no skin node more than a hundredth of
a cube off it (284 and 702 nodes more than a tenth of an element off); the smallest scaled Jacobian 0.23 and 0.19 (0.12 and 0.10). What got worse: the cells are less regular than a mesh that leaves the skin where the grid put it --
the skewness by the angles of the faces is over 0.85 in 2.3 % and 0.71 % of the cells (0.8 % and 0.13 %), over 0.95 in 2 and 60 cells (none), where the polygon of a face has a node at 177 to 180 degrees that is not quite
on the line of its neighbours; the mean orthogonal quality is 0.90 and 0.96 (0.94 and 0.98); the longest edge over the shortest of a cell 22 and 32 (10 and 13). The faces that still lean lie where three surfaces meet within a
cube or two (the ring-neck junction, the inside corner of the foot) and on fillets smaller than the element, where there is no crease to put a node on; two things that were tried and taken out again because they made the faces
lean more, not less (measured on the mesh before the nodes were kept off the sides of the faces, at 2 mm): moving a node along the plane of a flat face onto the wall of a bore (the kernel's `snapRims`: 245 -> 343 leaning faces), and spacing the nodes of a straight crease evenly (the kernel's `smoothChains`: 245 -> 283).

The polyhedra are in **VTK** (cell type 42), **Fluent** and **OpenFOAM** (face-based formats), and the surface formats write the boundary; Gmsh, Abaqus, Nastran and I-DEAS have no polyhedron and refuse such a mesh with the
way out (a threshold that makes none, or `mesh_tet4`). Pyramids are in Gmsh, VTK, Abaqus, Nastran and Fluent. The quality report gives the polyhedra their skewness (by the angles of their faces), aspect ratio and orthogonal
quality; the viewer draws each cell as the cell it is -- the twelve edges of a hexahedron, the sides of the faces of a polyhedron, no diagonals -- and a section view cuts through whole cells. A size field and `graded_size` are for tetrahedra: this is a grid of one size; boundary layers are made with `mesh_tet4`.

## Hexahedra

`mesh_hex_quanta` is **Salome's Body Fitting with Quanta** (`StdMeshers_Cartesian_3D` with its Quanta switched on, read in its source: `Hexahedron::addVolumes` and `Grid::ComputeNodes`): a Cartesian
grid whose cells are hexahedra and nothing else. It replaces the first hexahedral mesher of FielDes, which pushed the skin of the cubes onto the surface and patched the cells that came out flat.

1. **The grid** is the one of the Cartesian mesh above: cubes of `element_size` over the box of the body (one cube more all round), a whole number along each axis, so that the faces of a box lie on
   the planes of the grid.
2. **The cubes**: a cube with all its corners in the body is a hexahedron. A cube that the surface cuts is a **whole cube of the grid** -- its eight nodes at its eight corners, the ones outside the body
   too -- when what is inside it holds at least `quanta` of the cube, and is left out when it holds less. No cut cell, no polyhedron, no node on the surface.
3. **The result** has only perfect cubes -- aspect ratio 1, skewness 0, scaled Jacobian 1, orthogonal quality 1 -- and a boundary that is a **staircase** of whole cubes, up to a cube from the surface
   (not on it). `quanta` is the part of a cube that must be inside: 0.5 by default, which gives a mesh of about the volume of the body; Salome's own default when Quanta is on is 0.01, which swells
   the body by about half a cube all round; 1 leaves out every cut cube and shrinks it.

**`axes=(x, y, z)`** turns the grid (the rotation= of a placement, in degrees; Salome's axes of the Body Fitting): a box turned by 30 degrees about z is a block of perfect cubes along its own sides with `axes=(0, 0, 30)` (1,200 cubes, volume exact), and a staircase of them without (1,178 cubes, 2 % short); the zones' regions are turned with the body. It is for `mesh_hex_quanta`, `mesh_hex20_quanta` and `mesh_cartesian`.

Use it where a solver wants hexahedra only and a staircase boundary is acceptable (the stress at a round hole is then not the stress at the hole: the corners of the stairs are artificial). For hexahedra
inside and a boundary on the surface use `mesh_cartesian`, whose cut cubes are prisms, pyramids and polyhedra; for a boundary on the surface and one element type, `mesh_tet4`. `mesh_hex20_quanta`
puts a node in the middle of every edge of the same cubes. The boundary zones of a staircase are given to the faces whose middle is within three quarters of a cube of the region.

The element size is one number: a size field and `graded_size` are for tetrahedra. At most about 400 000 cells are made, and 15 000 is the default budget. Without an element size the cubes are the longest side over forty, finer where the body has thin walls (a grid loses a wall thinner than about its cube: about two cubes across the wall that a tenth of the surface is thinner than, as far as four times the budget allows, and the quality report says so) and coarser where the cells would pass the budget.

## Extrusion: hexahedra with the surface fitted

`mesh_hex_extrusion(body, element_size, axis='z', layers=None)` is **Salome's Extrusion 3D** (`StdMeshers_Prism_3D`, read in its source): the face at one end of a prism is meshed with quadrilaterals and swept
through the length of the body in layers. The cells are hexahedra only and, which a grid of cubes cannot do, **the surface is fitted**: the walls of a hole are the hole's. It is for a body that has the same
section all along an axis -- a plate with holes, a bar, a tube; any other body is refused (over a grid on its end face the body must be inside at the same places near the end, in the middle and near the
other end), with the way it differs.

1. **The end face** is the face of the tetrahedral mesh of the body (`mesh_tet4` at twice the size asked for, so that its triangles fit the rim) that looks away from the axis at the low end. Two triangles that
   share an edge are one quadrilateral where they make a good one (every corner between 30 and 150 degrees), the best pairs first; every quadrilateral is then cut into four and every triangle that found no partner
   into three, at the middles of their edges (the neighbours share those) and of themselves -- so the face is all quadrilaterals, about 0.6 to 1 of `element_size` across. The nodes of the rim, the corners of the
   triangles too, are put on the surface (Newton steps along the field, in the plane: the tetrahedral mesh's boundary vertices lie on its lattice, a fraction of an element off a round hole).
2. **The smoothing**: the nodes that are not on the rim are moved toward the middle of their neighbours (`smooth=` rounds, 10 by default) when that makes the quadrilaterals round them better and turns none inside out.
3. **The layers**: a whole number as high as the quadrilaterals are wide (`layers=` gives the number, or a list of the thicknesses, scaled to the length), each quadrilateral a hexahedron in every layer. The zones are
   the ones of `mesh_tet4`.

Where the triangles are too coarse for the curves of the face a quadrilateral turns inside out; finer triangles are then
tried, and when they fail too the mesh is refused with the advice to ask for a smaller `element_size`. A mesh with the same kind of cells but a staircase boundary, for any body, is `mesh_hex_quanta`; the cut
cells of the grid are `mesh_cartesian`.


## Element size

`element_size` is a number (mm), or a **field**: a body whose value at a point is the element size there, a function of a list of points that returns their sizes, or
`graded_size(...)`.

```python
mesh = mesh_tet4(air, element_size=2.0)                                    # one size
mesh = mesh_tet4(air, graded_size(body, wall_size=0.5, growth_rate=1.2, max_size=8))    # 0.5 mm at the surface of the body, growing by 0.2 mm for every mm from it
mesh = mesh_tet4(air, lambda points: [0.8 + 0.08 * (x + 10) for x, y, z in points])    # your own function
mesh = mesh_tet4(air, 1.0 + 0.05 * distance_to_point((0, 0, 0)))            # a field made of the library's own
mesh = mesh_tet4(part, curvature_size(part, element_size=3.0, segments=16))  # 3 mm where the surface is flat, finer where it curves
```

**`curvature_size(body, element_size, segments=12, min_size=None, growth_rate=1.3)`** makes the size follow the curvature of the surface (what Salome's NETGEN calls *limit size by surface curvature*):
a hole, a fillet or a small sphere gets `segments` elements round a full circle -- at a curve of radius R the size is 2 pi R / segments, not below `min_size` (a quarter of `element_size` by default) --
and it grows back to `element_size` by `growth_rate - 1` mm for every mm from the curve. 12 segments put a chord 3.4 % of the radius off the circle, 16 put it 1.9 % off. The curvature is measured
on the surface of `body` (the angle between the surface's normals at the two ends of a short step along it, over the step), so it is approximate to a few tens of percent; an edge or a corner is not a
curve (a turn of more than half a radian over the step): the mesher finds those itself.

**The element size is fitted to the body.** A number you give is a target. When the body does not hold a whole number of elements the size is adjusted, as little as it can be, so that the mesh fits: the hexahedra take a whole number of cells along each axis (a 10 mm thick beam at 4 mm is three cells of 3.33 mm), and the lattice of the tetrahedra is spaced along each axis so that the body is a whole number of cubes across: its faces lie on planes of the lattice, its edges on lines of it and its corners on points of it, instead of cutting its cells wherever they happen to fall (the mesh is never coarser than asked by more than a twentieth: the spacing is made smaller, by as much as it takes -- a plate 2.14 elements thick gets three layers, not two -- and by far less in a body many elements across). An edge that falls between lattice points is cut a notch of half an element deep, and a symmetric part, which is meshed on one side of its planes of symmetry and mirrored, has such notches where each plane crosses an edge. A part is mirrored only about the planes that have at least two elements on each side (a lattice of cubes at least two across is what the mesher can fill without gaps): a beam 10 mm thick at element size 5 is meshed whole across its thickness. The box the lattice is fitted to is the body's own: a flat face of the body that is square to an axis is put on the face itself, not on the box that the viewport or a search for the extent gives. The report of `mesh.quality()` has the size of the elements: the volume of the largest over the smallest, and the 5th and 95th percentile over the median. A lattice cuts every flat boundary through half-size elements, so a tetrahedral mesh of a box is not better than 2 to 1; a mesh of hexahedra of a box is 1 to 1.

Without one the default is about 40 elements along the longest side, no more than about a hundred thousand tetrahedra (a plain 10 mm cube: 107,000 tetrahedra, about 20 seconds; the elements are drawn and the quality numbers are made). Give `element_size` for a finer mesh.

**Thin walls and the default size.** The tetrahedra are cut from a lattice of nodes, and a wall that is not about as thick as its spacing is lost or half filled, with nothing to say so: a body whose walls are thinner than its spacing can be refused ("the part has no material inside the analysis region") or keep only part of its volume, at a default size that does not look at them. The default size therefore looks at the walls (the way Netgen restricts the size by the distance across a thin part, `closeedgefac`): the kernel gives a sample of the nodes of the lattice that lie near the surface (`libfive_tetmesh_probe`, `probeSurface` in `kernel/src/fea/tetmesh.cpp`), the thickness of the material is measured there along the normal of the surface (`thin_size` in `meshing.py`), and when a tenth of the surface or more lies in walls thinner than 1.4 spacings the size becomes half the thickness of the wall that a tenth of the surface is thinner than -- looked at again at that size, within the budget of tetrahedra as far as that holds the walls (never coarser than the wall over 1.5, and never more than about four times the budget, which is 400,000 tetrahedra for the default of `mesh_tet4` -- the number is estimated from the nodes of the lattice that lie inside, and the mesh has up to a third more or less; the viewer draws the elements of a mesh up to 400,000 cells) --, and the notes of the mesh say what was found. The 10-node element (`mesh_tet10`) is made 1.5 elements across a wall, the linear one two. The same default holds for an analysis that is not given an `element_size` (static, modal, thermal, flow). A size that you give is used as it is: the volume note below says what the mesh lost. A rib or two on a body whose surface is thick (less than a tenth of it), a knife edge and a point are left to the size the body would have had. The walls are held, not yet read well in bending: linear tetrahedra two elements across are stiff in bending, and more elements across, or the 10-node element, are what a bending analysis of a thin plate needs.

A graded mesh is made by bisecting, and bisection gives some poorer tetrahedra than
a uniform lattice does -- and nodes on the surface that are not on it, and corners (a post's wall where it meets the floor, a curved crease) that the surface cuts across with a slanted
face -- so a graded mesh first gets its nodes of the surface put on it (a node goes onto the plane most of the faces round it are on, or onto the nearest point of the surface, the whole way, half
or a quarter, whichever turns no tetrahedron inside out), then every edge of the surface that joins a node on one face to a node on the other is split and the new node put on the corner
between them, and then the mesh is **mended** (`optimize=True`, the default) by the passes of Netgen's volume optimization, run by the kernel (`kernel/src/fea/tetmender.cpp`: the mesh an analysis makes of a body is
mended by the same code, a second for 17,000 tetrahedra) -- the logic of Salome's tetrahedral mesher, read in its open source
(`meshfunc.cpp`, `improve3.cpp`, `smoothing3.cpp`, `improve2.cpp`) -- in two rounds of: the nodes on the surface that belong to poor tetrahedra move along the surface (toward the middle of
their neighbours, and back onto the surface; a node on a crease stays); the **needle triangles of the surface** (a smallest angle under 30 degrees) get the other diagonal of their two faces
where the quadrilateral is convex, the old and the new triangles are within 30 degrees of the surface's own normal at the two end nodes (the gradient of the field: Netgen's test, so that a flip never
crosses a crease) and the tetrahedra under it can be made again no poorer than 0.5 (Netgen's swapping of the surface edges by the badness of the triangles);
edges and faces are swapped and interior nodes taken into a neighbour where that makes the worst tetrahedron round them better; the edges of the very worst tetrahedra are **split** (Netgen's
SplitImprove: a new node in the middle, moved to where the sum of the badness of the new tetrahedra is least, kept when the sum gets less); and each interior node of a poor tetrahedron is
moved to where the **sum of the badness of its tetrahedra is least** (Netgen's ImproveMesh, a BFGS search; `smooth=` sweeps of it in each pass, 6 by default), which lets a node under the surface
leave it -- the middle of its neighbours, where a Laplacian smoothing puts it, is on the surface's plane and flattens the tetrahedron. The badness is Netgen's: `(0.0080187537 (the sum of the squared
edges)^1.5 / V)^2`, 1 for a regular tetrahedron, growing as the cube of the inverse of the mean ratio, so the worst tetrahedra weigh most. Nothing moves or changes unless it makes the tetrahedra
round it better, a node is never left with a poorer worst tetrahedron, the boundary stays the part's surface, and the nodes that no cell uses are taken out. A tetrahedron is
poor when its mean ratio (12 (3 V)^(2/3) over the sum of the squared edges; 1 is regular) is below 0.6. `optimize=False` leaves the mesh as it comes, which is much faster. A mesh of more than three million tetrahedra is left as it is (the mender's tables would take the memory of the computer).

## Boundary layers

```python
air = difference(box_of_air, body)
mesh = mesh_tet4(air, 2.0, boundary_layers=inflation_layers(body, first_height=0.05, growth_rate=1.2, layers=6))
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
quality report says how many prisms are inside out.

### Layers of hexahedra

`mesh_hex_extrusion` takes `boundary_layers=` too (Salome's Viscous Layers works with its hexahedral algorithms as well). The step is the same, with quadrilaterals: the method meshes the core, the body with the layers
taken off the wall, and every quadrilateral of the core's boundary that lies at the layers' total thickness from the wall is the base of a column of hexahedra swept to the wall -- so core and layers meet in whole faces,
and the mesh is hexahedra only.

```python
pin = cylinder(3, 10)
plate = difference(box((16, 16, 4)), pin)                    # a prism along z with a hole: the wall of the hole is the surface of `pin`
mesh = mesh_hex_extrusion(plate, 1.0, boundary_layers=inflation_layers(pin, first_height=0.2, growth_rate=1.3, layers=4))
```

`on` is the solid whose surface the layers grow from, as above. The nodes under the wall are at the cumulative heights of the layers, nothing is inside out, and the volume is the same as without layers
(`tests/internal/meshing_checks.py`, stage 24). `mesh_tet4` has prisms; `mesh_hex_quanta`, `mesh_hex20_quanta`, `mesh_cartesian` and `mesh_tet10` have no `boundary_layers` argument (a staircase of cubes or cut cells
has no one surface for layers to follow, and a ten-node tetrahedron has no prism with a node in the middle of every edge).

## Zones

```python
mesh = mesh_tet4(air, 2.0, zones={'inlet': inlet_region, 'outlet': outlet_region, 'symmetry': half_space(...)}, feature_angle=30)
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
| **scaled Jacobian** (the determinant of the three edges that leave a corner over the product of their lengths, the smallest of the corners: Salome's, Gmsh's SICN, Cubit's) | 1 is a regular tetrahedron, a cube, any box; 0 flat; below 0 inside out | the worst above 0.2, the mean above 0.8 |
| **skewness** (volume skewness for a tetrahedron, angle skewness for the others) | 0 is regular, 1 degenerate | the worst below 0.95; for CFD below 0.85 |
| **orthogonal quality** (Fluent's: the cosines between a face's area vector and the way to the face's middle and to the neighbour's) | 1 is best, 0 worst | the worst above 0.1 for CFD |
| **aspect ratio** (longest edge over shortest) | 1 is regular | up to 5 in the core; boundary-layer cells are stretched on purpose |
| **volume ratio** (a cell and the ones it touches) | 1 is smooth | 5 or less |
| **smallest dihedral angle** of a tetrahedron (degrees) | 60 is regular | above 20 |

The report also says, in a note, when the mesh holds **5 % or more less or more of the body's volume** than the body has (the share of 64 x 64 x 64 samples of the box it was meshed in that lies
inside the body; where that is 5 % or more off the mesh's, the volume is read again on a grid of half the element size with the distance as a ramp, which counts a wall as thin as one cell right: a
shell box whose mesh held all of its 21,900 mm^3 was said to hold 78 % by the first count alone): a wall or part thinner than the elements falls between the nodes of the lattice and is lost without any other sign, and the cubes of a grid add whole cubes along the surface.
`quality().negative` counts the elements that are inside out or flat: a solver refuses a mesh that has one. The report also says how many cells are above skewness 0.85 and 0.95
(`quality().skewed` is those two counts and the number of cells measured). **What FielDes reaches** (example 36, 30 000 tetrahedra and 3 000 prisms; the numbers are those of
`tests/internal/meshing_checks.py` demands the first three): no element inside out, the worst skewness below 0.95 (0.92), at most 1 % of the cells above 0.85 (0.35 %: 114 of 32 274), the worst
orthogonal quality 0.36, the volume ratio at most 4.6, the smallest dihedral angle 16.8 degrees. As the bisection leaves the same tetrahedra, 3 % of them are above 0.85 and 160 above 0.95. That is a mesh a
solver accepts and one that CFD guidelines call usable, not the polished mesh of a dedicated mesher (a thousand cells between 0.7 and 0.85 remain, and a tetrahedron is never as orthogonal as a
hexahedron): when the results depend on the mesh, refine it and compare.

**Surface or elements.** Where the lattice cuts a sharp edge across, the mesher has to choose between a node on the edge (the surface right, a poorer tetrahedron beside it) and the node where it is (a good tetrahedron, an edge that
is a slope). `mesh_tet4(body, surface_fit=...)` and `mesh_tet10(...)` (and the analyses that mesh a body themselves) take that choice from you: `'balanced'` (the default, as the mesher has always done), `'surface'` (more surface conformity)
or `'elements'` (fewer odd elements). What each value sets, and what it costs, is in [analysis.md](analysis.md), *Surface or elements*.

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

## Solving on it in FielDes

The mesh is not only for another program: `static_analysis(mesh, ...)`, `modal_analysis(mesh, ...)` and `thermal_analysis(mesh, ...)` solve on it, and `fluid_analysis(body, mesh_of_the_fluid, ...)`
on the mesh of the fluid -- with the sizes, the grading and the mending the meshing gave it. See [A mesh of your own](analysis.md#a-mesh-of-your-own) in the analysis page. A mesh of hexahedra or with
prism layers is for the solver of another program: FielDes' own solvers take tetrahedra.

## For CFD

1. mesh the fluid, not the body: `air = difference(domain, body)`;
2. `zones=` for the inlet, the outlet and the symmetry planes; everything else is `wall` (the faces of the body);
3. `inflation_layers(body, first_height=..., growth_rate=1.2, layers=...)` with a first height for the y+ you want, and `graded_size(body, wall_size=..., growth_rate=1.2, max_size=...)` so that
   the core is fine near the wall and coarse away from it (with the layers' total thickness about the size of the first core cell);
4. `quality().report()`: skewness below 0.85, orthogonal quality above 0.1, no prism inside out;
5. `export(..., format='fluent')` or `format='openfoam', scale=0.001`.

## Limits

* The mesher is isosurface stuffing: **features thinner than the element size can fall between lattice points and be lost** when you give the element size (the default size looks for them: see
  above), and sharp edges are rounded within an element. Mesh at an element size that is half of the thinnest wall, or less.
* The boundary of a graded mesh is the part's surface by Newton steps along the field's gradient: a field that has no gradient there (a constant, the maximum of planes at an edge) snaps
  to the nearest face.
* `mesh_hex_quanta` is whole cubes: its boundary is a staircase, up to a cube from the surface; `mesh_cartesian` fits the surface with prisms, pyramids and polyhedra, which Gmsh, Abaqus, Nastran and I-DEAS cannot hold. Quadrilateral surfaces are triangles cut into three.
* The meshing is Python on the kernel's lattice: about 30 000 tetrahedra a second for the lattice itself. Example 36 (30 000 tetrahedra and 3 000 prisms, graded, mended) is meshed in about
  6 s, its quality report takes 2 s and its four files a few seconds. The mending is made of Python loops over the cells: a mesh of a few hundred thousand tetrahedra takes minutes, and
  `optimize=False` leaves it out. A mesh of more than 400 000 cells is not drawn (the viewer's elements), and 150 000 cells is where the quality numbers that need the neighbours are made unless asked.
* Boundary layers: see above (convex corners, thickness against the thinnest passage, prisms with `mesh_tet4` and hexahedra with `mesh_hex_extrusion` only).
* No periodic faces, no CGNS, no cell zones by the meshing of several bodies together; a mesh is one body.
