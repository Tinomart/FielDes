# A gallery of lattices: one of every kind, each in a simple body of its own (no part needed).
#
#   row 1   struts           octet  ·  kelvin  ·  a strut cell of your own
#   row 2   surfaces, walls  a gyroid sheet  ·  a Schwarz P network  ·  a honeycomb
#   row 3   foams            Voronoi  ·  Delaunay  ·  Voronoi by relative density      (made around random points)
#   row 4   controlled       a graded wall  ·  a conformal layer on a sphere  ·  cells that follow a cylinder
#
# A lattice that repeats is made of a CELL, `cell_periodic(kind)` or one of your own, and `lattice(body, cell, ...)` fills a body with it.
# A foam does not repeat, so it is made around POINTS instead: `random_points(body, ...)` makes them, `voronoi_lattice(points)` and
# `delaunay_lattice(points)` make the lattice from them, in the body the points were made in.  (FielDes draws a set of points as dots, but
# only where it is shown -- here the points are left out of the picture, they would hide the lattice: write a line with just the name of
# the points, or open its eye in the model tree, to see them.)
#
# Tiles are 30 mm across and 44 mm apart.  Open the section card (Ctrl+Shift+X) to look inside any of them; change a number, or
# the cell, and the tile follows.
from fieldes import *

view.set_bounds([-22, -22, -22], [110, 154, 22])
view.set_resolution(4)
view.set_quality(8)

# ---- row 1: struts (the cell repeats; `thickness` is the beams' diameter) ------------------------------------------------------------------
cube = box_exact_centered((30, 30, 30), (0, 0, 0))
octet = lattice(cube, cell_periodic("octet"), cell_size=10, thickness=1.6)
octet

ball = sphere(15, (44, 0, 0))
kelvin = lattice(ball, cell_periodic("kelvin"), cell_size=12, thickness=1.6)
kelvin

# A strut cell of your own: coordinates run 0..1 across the cell.  Draw one part of a symmetric cell and mirror it: here the diagonal from
# a corner to the centre (thicker) and half an edge, mirrored in x, y and z.
star = cell_custom_truss({"o": (0, 0, 0), "c": (0.5, 0.5, 0.5), "e": (0.5, 0, 0)}, [("o", "c", 1.0), ("o", "e")], mirror="xyz")
drum = cylinder_z(15, 30, (88, 0, -15))
custom = lattice(drum, star, cell_size=10, radius=0.8)
custom

# ---- row 2: surfaces and walls ---------------------------------------------------------------------------------------------------------------
# A TPMS sheet is a wall of `thickness` on both sides of a minimal surface; a network is the solid on one side of it, grown by `offset`
# (a negative offset thins it to tubes).
block = box_exact_centered((30, 30, 30), (0, 44, 0))
gyroid = lattice(block, cell_periodic("gyroid"), cell_size=15, thickness=1.2)
gyroid

globe = sphere(15, (44, 44, 0))
schwarz = lattice(globe, cell_periodic("schwarz_p"), cell_size=10, style="network", offset=-1.5)
schwarz

# A planar pattern is extruded along an axis: honeycomb walls of `wall` mm
tube = cylinder_z(15, 30, (88, 44, -15))
honeycomb = lattice(tube, cell_periodic("hexagon"), cell_size=8, wall=1.2)
honeycomb

# ---- row 3: foams (around points) ---------------------------------------------------------------------------------------------------------
# The points: how far apart they are on average, how random, the nearest and the farthest two may be.
foam_body = box_exact_centered((30, 30, 30), (0, 88, 0))
points = random_points(foam_body, spacing=7, randomness=0.7, min_distance=4.5, max_distance=10, seed=2)
foam = voronoi_lattice(points, thickness=1.5, blend=1.0)          # the edges of the Voronoi cells around the points, the joints rounded
foam

truss_body = sphere(15, (44, 88, 0))
truss_points = random_points(truss_body, spacing=8, randomness=0.5, seed=5)
truss = delaunay_lattice(truss_points, thickness=1.5, blend=1.0)      # beams along the Delaunay edges between the points: a stochastic truss
truss

dense_body = cylinder_z(15, 30, (88, 88, -15))
dense = voronoi_lattice(random_points(dense_body, spacing=7, seed=7), density=0.2)      # the share of material, instead of the beams' size
dense

# ---- row 4: controlled ---------------------------------------------------------------------------------------------------------------------------
# Any size can be a field: here the wall of a gyroid is 2 mm at the skin and 0.6 mm deep inside (depth_below is the distance from the skin).
graded_body = box_exact_centered((30, 30, 30), (0, 132, 0))
wall = ramp(depth_below(graded_body), (0, 12), (2.0, 0.6))
graded = lattice(graded_body, cell_periodic("gyroid"), cell_size=10, thickness=wall)
graded

# A conformal lattice lays its cells on the SURFACE of the body, as big as asked all along it: a layer of 3 mm under the skin of the sphere
planet = sphere(15, (44, 132, 0))
conformal = lattice_surface_conform(planet, cell_periodic("octet"), cell_thickness=3, cell_size=6, thickness=1.4)
conformal

# A cell map bends the cells: round a cylinder they are 12 around, radial and along its axis
pipe = cylinder_z(15, 30, (88, 132, -15))
wrapped = lattice(pipe, cell_periodic("octet"), cell_size=7, thickness=1.6, cell_map=cylindrical(origin=(88, 132, 0), cells_around=12))
wrapped
