# The warning for a part that falls into pieces: example 08 with the holes excluded at 45 % is in two pieces (at 50 % it
# is one: see t_ex08_final.py).  The message must be printed and .pieces must say so; the count has to agree with the
# pieces of the part's own mesh (counted here outside the library).
import os
from fieldes import *

here = os.path.dirname(os.path.abspath(__file__))
parts = import_step_parts(os.path.normpath(os.path.join(here, '..', '..', 'examples', 'step', 'PivotBearingSupportBracket.STEP')))
space, (lo, hi) = parts[0]
plates = union(box_exact((-76, 5, -61), (-45, 36, 29)), box_exact((45, 5, -61), (76, 36, 29)))
lugs = box_exact((-60, -90, -61), (60, -60, 29))
holes = union(box_exact((-48, -88, -22), (-26, -44, 22)), box_exact((26, -88, -22), (48, -44, 22)))


def mesh_pieces(shape, res=0.7):
    verts, tris = shape.get_mesh((lo[0] - 2, lo[1] - 2, lo[2] - 2), (hi[0] + 2, hi[1] + 2, hi[2] + 2), res)
    parent = list(range(len(verts)))

    def find(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a
    for a, b, c in tris:
        parent[find(b)] = find(a)
        parent[find(c)] = find(a)
    count = {}
    for a, b, c in tris:
        count[find(a)] = count.get(find(a), 0) + 1
    big = sorted(count.values(), reverse=True)
    return len([n for n in big if n >= 0.02 * sum(big)])


shape = exclude(space, holes)
cond = static_boundary_conditions(shape, supports=[fixed(plates)], loads=[force(lugs, (0, -2000, 0))])
for vf in (0.45, 0.50):
    opt = topology_optimization(shape, cond, material=aluminium, volume_fraction=vf, element_size=5)
    print('RESULT volume_fraction %.2f -> pieces %s (its own mesh: %d)' % (vf, opt.pieces, mesh_pieces(opt.shape())))
