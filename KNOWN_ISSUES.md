# Known issues

What you should know before you spend time on something. A problem that is not here: **Help ▸ Report a bug** opens a new issue with the version and your system filled in.
What a tool does and does not do (its scope, its accuracy, its cost) is in the guide, next to the tool: [docs/manufacturing.md](docs/manufacturing.md), [docs/analysis.md](docs/analysis.md),
[docs/fields.md](docs/fields.md), [docs/interface.md](docs/interface.md); the README has the design decisions (*Known limitations*).

## Platforms

- **Windows** is the main platform. The setup program and the zip are not signed: Windows SmartScreen may say *unknown publisher* the first time; *More info ▸ Run anyway*
  opens it (the source is public, and the setup is built from it by the script in `scripts/`).
- **Linux** packages (a folder, a `.run` installer, a `.deb`) are built on Ubuntu 22.04 (glibc 2.35), so they open on Ubuntu 22.04, Debian 12 and everything newer. They were run under WSL2
  (Ubuntu 22.04, with WSLg), not on every distribution.
- There is **no macOS** build.

## Meshing and analysis

Found by running the validation again on the new mesher (the numbers are on [docs/validation.md](docs/validation.md)); none of them is hidden by a tolerance, and each is an open issue of the mesher, not a limit of the method:

- **A part that is only two elements thin loses section.** A round wire 1.95 mm across meshed with 1 mm elements holds 6.9 % too little of its volume and its natural frequencies read 3.3 to 4.3 % low; with 0.8 mm elements they are exact within 0.4 %. The passes that put nodes on edges take a curve this coarse for a crease. Halve the element size and see whether the answer moves.
- **At some element sizes a sliver tetrahedron can sit next to a stress point** where a plane of symmetry, a curved surface and a face of the plate meet: in the NAFEMS LE1 benchmark three element sizes of nine (85, 60 and 55 mm) read the stress at the point 25 to 46 % high, the other six within 1 %. `surface_fit='elements'` removes the poor cells but not the error at those sizes. Look at the stress at a point of a boundary on two element sizes before you trust it.
- **A symmetric part is meshed on one side and mirrored only where the mirror matches the surface** (to 0.06 of an element); at a coarse element size the check often refuses, and the part is meshed whole: the same quality, but the time of the whole mesh. The real bracket of example 05 is mirrored at 2 mm and at 3.75 mm (and scaled to 0.01 and to 10 times its size with the same number of elements), and meshed whole at 6.8 mm.
- **A few rows of faces on the real bracket still lean off the surface** (0.3 % of the skin at 2 mm): four faces where the rim of the large circle crosses the mirror plane, and rows of five to eight faces where three surfaces meet within a millimetre or two (the inside corner of the foot, the junction of the ring and the neck). They are visible only at grazing angles along the edge.
- **The skin of `mesh_cartesian` still follows the surface less closely than the tetrahedra's** (found 2026-11-15, improved the same day: small cut cells are joined instead of left out, every node of the skin is put on the surface, the tetrahedra's edge pass and collapse are done for the cut cells): on the real bracket 6.6 % of its skin faces lean more than 12 degrees from the surface at 4 mm and 2.5 % at 2 mm (the tetrahedra: 3.7 % and 0.3 %; before: 22.5 % and 14.1 %). The ones that are left lie where three surfaces meet within a cube or two (the ring-neck junction, the inside corner of the foot) and on fillets smaller than the element, where there is no crease to put a node on: of the nodes of the leaning faces about half have no crease within 0.6 of a cube. In the window the neck's top edge of the bracket is a straight line on one side of its band and still shows steps of about a third of a cell on the other, and the outer ring of an arm end is toothed on one side. The cells are less regular than a mesh that leaves the skin where the grid put it: at 2 mm 0.66 % of them have a skewness by face angles over 0.85 (0.13 % before), 38 of 38,984 over 0.95 (polygons with a node at 177 to 180 degrees that is not quite on the line of its neighbours), the mean orthogonal quality 0.96 (0.98), the longest edge over the shortest of a cell up to 13 (13). Two more passes of the tetrahedra were tried for the cut cells and made the faces lean more, so they are not in: the rim of a flat face (`snapRims`) and the even spacing of the nodes of a straight crease (`smoothChains`).
