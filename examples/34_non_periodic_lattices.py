# Lattices that do not repeat: a random net over a surface, and foams with a clean surface.
#
#   row 1   over a surface   a random net on a block with a dent  ·  the same with triangles  ·  the net inside a ball (intersected with it)
#   row 2   foams            a foam the surface cuts off  ·  a foam with a clean surface (Voronoi)  ·  a truss with a clean surface (Delaunay)
#
# A lattice that repeats is made of a cell (see 23_lattice_gallery.py).  One that does not repeat is made OVER a surface -- `surface_lattice(body, cell_size)`: a random net of
# beams whose axes lie ON the surface, hollows and edges too -- or AROUND points: `random_points(body, ...)`, then `voronoi_lattice(points)` or `delaunay_lattice(points)`.
# A foam is cut off by the surface of its body: the left tile of row 2, its beams end in stubs.  `random_points(body, surface=True)` puts points on the surface as well, and the
# foam then has a clean net of beams there: the other two tiles of row 2.
#
# Tiles are 30 mm across and 44 mm apart.  Open the section card (Ctrl+Shift+X) to look inside any of them; change a number, and the tile follows.
# About three minutes, nearly all of it the picture of six lattices of beams (the progress bar shows it).
from fieldes import *

view.set_bounds([-22, -22, -22], [110, 66, 22], auto_scale=True)
view.set_resolution(4)
view.set_quality(8)

# ---- row 1: a random net over a surface -----------------------------------------------------------------------------------------------------------
block = difference(box((-15, -15, -8), (15, 15, 8)), sphere(7, (0, 0, 8)))
net = surface_lattice(block, cell_size=5, thickness=1.0)                                  # cells that are mostly hexagons, three beams at every node
net

block_b = difference(box((29, -15, -8), (59, 15, 8)), sphere(7, (44, 0, 8)))
triangles = surface_lattice(block_b, cell_size=5, pattern="delaunay", thickness=1.0)      # the same, in triangles
triangles

ball = sphere(15, (88, 0, 0))
inside = intersection(surface_lattice(ball, cell_size=5, thickness=1.2), ball)            # half of each beam is outside the body: this is the net inside it
inside

# ---- row 2: foams ----------------------------------------------------------------------------------------------------------------------------------
ball_c = sphere(15, (0, 44, 0))
cut = voronoi_lattice(random_points(ball_c, spacing=6, seed=1), thickness=1.0)            # the surface cuts the beams off
cut

ball_d = sphere(15, (44, 44, 0))
clean = voronoi_lattice(random_points(ball_d, spacing=6, surface=True, seed=1), thickness=1.0)       # a net of beams on the surface, joined to the foam inside
clean

ball_e = sphere(15, (88, 44, 0))
truss = delaunay_lattice(random_points(ball_e, spacing=6, surface=True, seed=1), thickness=1.0)      # beams along the surface between the points
truss
