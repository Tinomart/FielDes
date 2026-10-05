# What an excluded shape does in the optimisations: its locked fields are regions to keep, however many operations came
# between exclude() and the optimisation.  Structural, thermal and flow (small runs).
import sys
import time

from fieldes import *
from fieldes.stdlib import excluded as ex

failed = 0


def check(name, ok, detail=''):
    global failed
    print('  %-70s %s %s' % (name, 'ok' if ok else 'FAILED', detail))
    if not ok:
        failed += 1


print('== keep_regions: the locks of a shape, through operations')
part = box_exact((0, 0, 0), (60, 20, 10))
mid = box_exact((22, -1, -1), (38, 21, 11))
e = exclude(part, mid)
moved = move(e, (5, 0, 0))
off = offset(moved, 0.0)
check('exclude: one region to keep', len(ex.keep_regions(e)) == 1)
check('after move and offset it is still there', len(ex.keep_regions(off)) == 1)
check('a plain shape has none', ex.keep_regions(part) == [])
k = ex.keep_regions(off)[0]
check('the kept field is the locked part (inside where the part is, moved with it)',
      k(32, 10, 5) < 0 and k(2, 10, 5) > 0 and k(5, 10, 5) > 0)

print('== structural: topology_optimization of a cantilever with the middle excluded')
base = box_exact((-1, -1, -1), (0.01, 21, 11))
tip = box_exact((59.5, -1, -1), (61, 21, 11))
for label, shape in (('plain', part), ('excluded, moved back through operations', move(move(e, (5, 0, 0)), (-5, 0, 0)))):
    cond = static_boundary_conditions(shape, supports=[fixed(base)], loads=[force(tip, (0, 0, -500))])
    t = time.time()
    r = topology_optimization(shape, cond, material=aluminium, volume_fraction=0.3, element_size=2.5, iterations=18)
    s = r.shape()
    # the middle block: how much of it is solid in the optimised shape
    pts = [(22.5 + 15 * (i % 6) / 5.0, 1 + 18 * ((i // 6) % 6) / 5.0, 0.5 + 9 * ((i // 36) % 4) / 3.0) for i in range(144)]
    solid = sum(1 for p in pts if s(*p) < 0) / float(len(pts))
    print('    %-44s %.1f s: %.0f %% of the middle block solid (volume fraction %.2f)' % (label, time.time() - t, 100 * solid, r.volume_fraction))
    if label == 'plain':
        plain_solid = solid
    else:
        check('the excluded middle stays solid, the plain one does not', solid > 0.97 and plain_solid < 0.9,
              'excluded %.2f, plain %.2f' % (solid, plain_solid))
        check('the optimised shape is excluded too (operations after it keep the middle)', len(ex.locks_of(s)) == 1)

print('== thermal: thermal_topology_optimization')
slab = box_exact((0, 0, 0), (40, 40, 6))
mid_t = box_exact((14, 14, -1), (26, 26, 7))
et = exclude(slab, mid_t)
for label, shape in (('plain', slab), ('excluded', offset(et, 0.0))):
    boundary = [heat_input(box_exact((16, 16, 5), (24, 24, 7)), 5.0), fixed_temperature(box_exact((-1, -1, -1), (41, 1, 7)), 20.0),
                convection(box_exact((-1, -1, 5.5), (41, 41, 7)), 10e-6, ambient=20.0)]
    t = time.time()
    r = thermal_topology_optimization(shape, boundary, material=aluminium, volume_fraction=0.4, element_size=2.0,
                                      iterations=12, element='hex')
    s = r.shape()
    pts = [(14.5 + 11 * (i % 5) / 4.0, 14.5 + 11 * ((i // 5) % 5) / 4.0, 0.5 + 5 * ((i // 25) % 3) / 2.0) for i in range(75)]
    solid = sum(1 for p in pts if s(*p) < 0) / float(len(pts))
    print('    %-12s %.1f s: %.0f %% of the middle block solid' % (label, time.time() - t, 100 * solid))
    if label == 'plain':
        plain_t = solid
    else:
        check('the excluded middle stays solid', solid > 0.97, 'excluded %.2f, plain %.2f' % (solid, plain_t))

print('failed:', failed)
sys.exit(1 if failed else 0)
