# Example 08 (the lug holes excluded): at which volume fractions is the optimised bracket one piece?  The scraps the
# optimisation leaves are counted out (specks under 3 % of the biggest piece); the plain bracket at 45 % is the reference.
import os
import time

from fieldes import *

here = os.path.dirname(os.path.abspath(__file__))
parts = import_step_parts(os.path.normpath(os.path.join(here, '..', '..', 'examples', 'step', 'PivotBearingSupportBracket.STEP')))
space, (lo, hi) = parts[0]
plates = union(box_exact((-76, 5, -61), (-45, 36, 29)), box_exact((45, 5, -61), (76, 36, 29)))
lugs = box_exact((-60, -90, -61), (60, -60, 29))
holes = union(box_exact((-48, -88, -22), (-26, -44, 22)), box_exact((26, -88, -22), (48, -44, 22)))


def pieces(shape, res=0.7):
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
    return [n for n in big if n >= 0.03 * big[0]], len(big)


runs = [('plain 45 %', space, 0.45)] + [('holes excluded %d %%' % round(100 * f), exclude(space, holes), f) for f in (0.50, 0.55, 0.60)]
for label, shape, vf in runs:
    cond = static_boundary_conditions(shape, supports=[fixed(plates)], loads=[force(lugs, (0, -2000, 0))])
    t = time.time()
    opt = topology_optimization(shape, cond, material=aluminium, volume_fraction=vf, element_size=5)
    big, every = pieces(opt.shape())
    print('%-24s %3.0f s  compliance %.4g -> %.4g  %d big piece(s) %s  (+%d scrap(s))' %
          (label, time.time() - t, opt.compliance[0], opt.compliance[-1], len(big), big, every - len(big)))
