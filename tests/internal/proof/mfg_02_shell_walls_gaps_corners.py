# Manufacturing 2: wall thickness, gap width and inside corner radius -- does the optimizer satisfy each rule and change nothing else?
#
# Three parts, each violating ONE rule, so that the material the optimizer adds can be computed by hand.  Each part stands alone (no base plate), so that the only
# inside corner is the one under test.  Back row: the input parts.  Front row: the optimizer's results, coloured by the geometry change
# (red: material added, blue: material removed, grey: unchanged).
#
#   FIN      1.5 thick, 16 long, 24 high.  Rule: wall thickness >= 4.
#            Expected: a thickness of 4 over the whole face, up to its edges and not past them; nothing else changes.
#            (The fin must more than double to satisfy the rule, and the optimizer changes at most 25 % of a part unless it is allowed more: max_change=3.)
#            By hand: (4.0 - 1.5) / 2 = 1.25 on each face; two faces of 16 x 24 mm: 2 x 1.25 x 384 = 960 mm^3.  The optimizer adds a margin of 5 % of the
#            thickness it adds, so that its own re-check passes the wall: up to 1008 mm^3.
#   SLOT     two blocks 12 x 16 x 18 with 2 between them.  Rule: gap width >= 4.
#            Expected: the slot is closed, level with the top of the blocks and not above it: it shows as a red body between two grey blocks.
#            By hand: the slot volume, 2 x 16 x 18 = 576 mm^3.
#            (The viewer draws a surface with 3 samples per mm here, so a gap of 0.6 mm is two samples wide and shows as closed in the input too: the gap
#            is made wide enough to be seen.  The rules of a printer are fractions of a millimetre; to see those, make the scene small and the resolution high.)
#   CORNER   two walls, 3 thick, 30 long, 20 high, meeting in an L.  Rule: inside corner radius >= 6.
#            Expected: a fillet, a quarter circle of radius 6 tangent to both walls.
#            By hand: (1 - pi/4) x 6^2 x 20 = 154.5 mm^3.
#
# The output pane prints, for each part, the volume the optimizer reports and the value computed by hand.  The reported volumes are estimates (sampling points in the
# layer round the surface): expect agreement to within a few per cent.
import math
from fieldes import *

view.set_bounds([-10, -75, -5], [75, 45, 30], auto_scale=True)
view.set_resolution(3)
view.set_quality(8)

fin = box((0, 0, 0), (1.5, 16, 24))
slot = union(box((20, 0, 0), (32, 16, 18)), box((34, 0, 0), (46, 16, 18)))
# (the L is turned half a turn about z, so that its inside corner faces the viewer)
corner = rotate_z(union(box((52, 0, 0), (82, 3, 20)), box((52, 0, 0), (55, 30, 20))), math.pi, center=(67, 15, 0))

print('BY HAND: fin +960 (up to 1008 with the margin), slot +576, corner +154.5 mm^3, nothing removed')
better_fin = manufacturing_optimization(fin, constraints=[manufacturing_constraint(min_wall=4.0)], max_change=3.0)
print('FIN    (by hand 960 .. 1008):', better_fin)
better_slot = manufacturing_optimization(slot, constraints=[manufacturing_constraint(min_gap=4.0)])
print('SLOT   (by hand 576):', better_slot)
better_corner = manufacturing_optimization(corner, constraints=[manufacturing_constraint(min_radius=6.0)])
print('CORNER (by hand 154.5):', better_corner)

# the input parts, behind
inputs = union(move(fin, (0, -50, 0)), move(slot, (0, -50, 0)), move(corner, (0, -50, 0)))
inputs
better_fin
better_slot
better_corner
