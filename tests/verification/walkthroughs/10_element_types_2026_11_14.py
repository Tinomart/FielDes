# THE ELEMENT TYPES, SIDE BY SIDE, 2026-11-14 (H193): one plate 40 x 20 x 6 mm with a hole of radius 4, meshed six ways, with the numbers a solver is told about printed beside each -- and a thin tube with the default size.
# About 45 seconds to run.  Open the file (File > Open, or drag it onto the window); the console (bottom left) prints a table, the viewport shows the six meshes in a row along y.
#
#  WHAT TO DO: press "Elements" on the result card of each mesh (or tick its eye in the tree) and turn the view: look from the top, then from the front along the edge, then into the hole.
#  The meshes are the same plate, y = 0 (first) to y = 150 (last):
#    1  tet         linear tetrahedra (the default)          every boundary node is on the surface; the hole is a polygon of straight edges
#    2  tet10       ten-node tetrahedra                       the same tetrahedra, a node in the middle of every edge, the edges on the hole's surface CURVED: the hole is round
#    3  hex         hexahedra only, cubes of a grid           Salome's Body Fitting with Quanta: every cell a perfect cube (scaled Jacobian 1), the boundary a staircase of whole cubes
#    4  hex20       the same cubes with 20 nodes              a node in the middle of every edge
#    5  cartesian   cubes inside, cut cells at the surface    Salome's Body Fitting: the cubes the surface cuts become the cells it leaves (hexahedra, prisms, pyramids, polyhedra), nodes on the surface
#    6  extrusion   Salome's Extrusion 3D                     the hole's section meshed with quadrilaterals and swept through the plate: hexahedra only AND the hole is the hole (a fitted surface)
#  YOU MUST SEE (console): the table, one row for each, the volume of every mesh within a third of a per cent of the exact 4,498.4 mm3 (the column in brackets: the staircase of hex and hex20 is the farthest, +0.3 %; cartesian +0.1 %),
#  hex and hex20 with exactly the same number of cubes (1,320) and the scaled Jacobian of both exactly 1 and of every other type above 0.2, no cell inside out in any (the "inside out" column 0).  YOU MUST SEE (viewport): no gap, no element sticking out of the surface,
#  no flat sliver along an edge; the hole of 2 and 6 round, of 1 and 5 a polygon, of 3 and 4 a staircase.
#  THE THIN TUBE (x = 75, y = 10 and 40): a tube of radius 10, a wall of 0.6 mm, 40 long, meshed by hex (y = 10) and cartesian (y = 40) with NO element size.  A grid loses a wall thinner than its cube, so the default size holds the
#  wall: about 0.4 mm.  YOU MUST SEE (console): the two last rows, 26,000 hexahedra (+13.8 % of the volume: a staircase of whole cubes) and 35,200 cartesian cells (+0.9 %), both with the wall all the way round -- before this day
#  the cubes were 1 mm and the mesh held 22 % (hex) and 61 % (cartesian) of the tube; YOU MUST SEE (viewport): the tube's wall as a ring of cells, no gap in it, the cartesian one a smooth round tube.
from fieldes import *

view.set_bounds([-5, -10, -5], [95, 170, 45], auto_scale=True)
view.set_resolution(2)
view.set_quality(8)

SIZE = 1.5
EXACT = 40.0 * 20.0 * 6.0 - 3.141592653589793 * 4.0 ** 2 * 6.0


def plate(y0):
    ''' the plate, its middle at (20, y0 + 10, 3), the hole through it along z '''
    return difference(box((40, 20, 6), position=(20, y0 + 10, 3)), cylinder(4, 10, position=(20, y0 + 10, 3)))


def row(name, mesh, exact=None):
    exact = EXACT if exact is None else exact
    q = mesh.quality()
    nan = float('nan')
    jac = q.metrics.get('scaled_jacobian', (nan, nan, nan))
    skew = q.metrics.get('skewness', (nan, nan, nan))
    aspect = q.metrics.get('aspect_ratio', (nan, nan, nan))
    print('{:<10} {:>8,} cells {:>8,} nodes  volume {:8.1f} mm3 ({:+5.1f} %)   scaled Jacobian min {:5.3f} mean {:5.3f}   skewness max {:5.3f}   aspect max {:5.2f}   inside out {}'.format(
        name, mesh.cell_count(), mesh.node_count, mesh.volume(), 100.0 * (mesh.volume() / exact - 1.0), jac[0], jac[1], skew[2], aspect[2], q.negative))


print('element types of one plate with a hole, size {} mm, exact volume {:.1f} mm3'.format(SIZE, EXACT))
tet = mesh_tet4(plate(0), SIZE)
row('tet', tet)
tet10 = mesh_tet10(plate(30), SIZE)
row('tet10', tet10)
hexa = mesh_hex_quanta(plate(60), SIZE)
row('hex_quanta', hexa)
hex20 = mesh_hex20_quanta(plate(90), SIZE)
row('hex_quanta20', hex20)
cartesian = mesh_cartesian(plate(120), SIZE)
row('cartesian', cartesian)
extrusion = mesh_hex_extrusion(plate(150), SIZE)
row('hex_extrusion', extrusion)

# the thin tube with the default size (no element size given)
TUBE = 3.141592653589793 * (10.0 ** 2 - 9.4 ** 2) * 40.0


def tube(y0):
    return difference(cylinder(10, 40, position=(75, y0, 20)), cylinder(9.4, 50, position=(75, y0, 20)))


print('a tube of radius 10, wall 0.6, length 40 with the default element size, exact volume {:.1f} mm3'.format(TUBE))
tube_hex = mesh_hex_quanta(tube(10))
row('tube hex', tube_hex, TUBE)
tube_cartesian = mesh_cartesian(tube(40))
row('tube cart.', tube_cartesian, TUBE)

tet
tet10
hexa
hex20
cartesian
extrusion
tube_hex
tube_cartesian
