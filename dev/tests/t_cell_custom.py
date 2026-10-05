# cell_custom(region, geometry): any geometry in a box as a cell, on every cell map; the truss and TPMS cells it was
# split from; the old forms refused.
import math
import warnings
from fieldes import *
from fieldes.stdlib import lattices

bad = []


def check(name, ok, detail=''):
    print(('ok    ' if ok else 'FAIL  ') + name + (('   ' + detail) if detail else ''))
    if not ok:
        bad.append(name)


def inside(s, p):
    return evaluate(s, list(p))[0] < 0


# --- the region: any box, found exactly; anything else refused
for name, region, lo, hi in (
        ('box', box((0, 0, 0), (10, 10, 10)), (0, 0, 0), (10, 10, 10)),
        ('box_exact', box_exact((-3, 2, 1), (7, 5, 11)), (-3, 2, 1), (7, 5, 11)),
        ('box_centered', box_centered((4, 6, 8), (1, 2, 3)), (-1, -1, -1), (3, 5, 7)),
        ('small box', box((0.25, 0.25, 0.25), (0.75, 0.5, 1.0)), (0.25, 0.25, 0.25), (0.75, 0.5, 1.0))):
    cell = cell_custom(region, sphere(0.1, (0, 0, 0)), check=False)
    err = max(abs(a - b) for a, b in zip(cell.lo + cell.hi, lo + hi))
    check('region %s found exactly' % name, err < 1e-5, 'lo %s hi %s (error %.2g)' % (cell.lo, cell.hi, err))
for name, region in (('sphere', sphere(5)), ('rounded box', rounded_box((0, 0, 0), (10, 10, 10), 0.3)),
                     ('two boxes', union(box((0, 0, 0), (4, 4, 4)), box((6, 0, 0), (10, 4, 4))))):
    try:
        cell_custom(region, sphere(1, (0, 0, 0)))
        check('region %s refused' % name, False)
    except ValueError as e:
        check('region %s refused' % name, 'box' in str(e), str(e)[:90])

# --- the cell: a hollow ball held by rods through the middles of the faces (it tiles), reaching out of the box
R = box((0, 0, 0), (10, 10, 10))
ball = difference(sphere(3.6, (5, 5, 5)), sphere(2.8, (5, 5, 5)))
rods = union(box((-2, 4, 4), (12, 6, 6)), box((4, -2, 4), (6, 12, 6)), box((4, 4, -2), (6, 6, 12)))
geometry = union(ball, difference(rods, sphere(2.8, (5, 5, 5))))
with warnings.catch_warnings(record=True) as w:
    warnings.simplefilter('always')
    held = cell_custom(R, geometry)
check('a cell that tiles gives no warning', not w, str([str(x.message)[:80] for x in w]))
check('cell.solid() is the intersection', inside(held.solid(), [(5 + 3.2, 5, 5)]) and not inside(held.solid(), [(11, 5, 5)]),
      'rod end past the box is cut off: %s' % evaluate(held.solid(), [(11, 5, 5)]))

part = box_exact((-30, -30, -30), (30, 30, 30))
lat = lattice(part, held)                         # cells of 10 mm as modelled
probe = [(5 + 3.2, 5, 5), (5, 5 + 3.2, 5), (5, 5, 5), (1, 5, 5), (5, 5, 9), (2, 2, 2), (5 + 3.2, 5, 9.5), (8.5, 8.5, 8.5)]
shifted = [(x + 10 * i, y + 10 * j, z - 10 * k) for (x, y, z) in probe for (i, j, k) in ((1, 0, 0), (-2, 1, 0), (0, 2, 2))]
base = evaluate(lat, probe)
rep = evaluate(lat, shifted)
ok = all(abs(rep[3 * n + m] - base[n]) < 1e-3 for n in range(len(probe)) for m in range(3))
check('lattice repeats every 10 mm', ok, 'worst %.2g' % max(abs(rep[3 * n + m] - base[n]) for n in range(len(probe)) for m in range(3)))
check('wall of the ball is inside, its hollow and the gaps are outside',
      [v < 0 for v in base[:5]] == [True, True, False, True, True] and base[5] > 0 and base[7] > 0, str([round(v, 2) for v in base]))

# --- scaling: cell_size scales the cell (the ball's wall at 3.2 mm of 10 mm is at 1.6 mm of 5 mm)
small = lattice(part, held, cell_size=5)
check('cell_size=5 scales the cell', inside(small, [(2.5 + 1.6, 2.5, 2.5)]) and not inside(small, [(2.5 + 0.8, 2.5, 2.5)])
      and not inside(small, [(2.5, 2.5, 2.5)]), 'wall %s, hollow %s' % (evaluate(small, [(4.1, 2.5, 2.5)]), evaluate(small, [(3.3, 2.5, 2.5)])))
stretched = lattice(part, held, cell_size=(10, 10, 20))        # twice as tall: the ball becomes an ellipsoid
check('cell_size=(10, 10, 20) stretches it in z', inside(stretched, [(5, 5, 10 + 6.4)]) and not inside(stretched, [(5, 5, 10 + 3.2)]),
      'z: wall at %.2f / %.2f' % tuple(evaluate(stretched, [(5, 5, 16.4), (5, 5, 13.2)])))

# --- on a cell map: the same cell on a cylinder and on a sphere (repeats every cell around)
cyl = lattice(part, held, cell_size=6, cell_map=cylindrical(cells_around=12))
pts = [(12 * math.cos(a), 12 * math.sin(a), 7.0) for a in (0.3, 1.1, 2.0)]
turn = 2 * math.pi / 12
pts2 = [(12 * math.cos(a + turn), 12 * math.sin(a + turn), 7.0) for a in (0.3, 1.1, 2.0)]
a, b = evaluate(cyl, pts), evaluate(cyl, pts2)
check('cylindrical cell map: the pattern repeats every cell around', all(abs(u - v) < 2e-3 for u, v in zip(a, b)), '%s vs %s' % (a, b))
sph = lattice(part, held, cell_size=6, cell_map=spherical(cells_around=12))
pts = [(14 * math.cos(a) * math.sin(0.9), 14 * math.sin(a) * math.sin(0.9), 14 * math.cos(0.9)) for a in (0.3, 1.1)]
pts2 = [(14 * math.cos(a + turn) * math.sin(0.9), 14 * math.sin(a + turn) * math.sin(0.9), 14 * math.cos(0.9)) for a in (0.3, 1.1)]
a, b = evaluate(sph, pts), evaluate(sph, pts2)
check('spherical cell map: the pattern repeats every cell around', all(abs(u - v) < 2e-3 for u, v in zip(a, b)), '%s vs %s' % (a, b))
vals = evaluate(cyl, [(r * 0.5, 0.0, z * 0.5) for r in range(2, 40) for z in range(2, 40)])
check('the cylindrical lattice has solid and gaps', any(v < 0 for v in vals) and any(v > 0 for v in vals))

# --- a cell that does not tile warns (one sphere on one corner: the opposite faces differ)
with warnings.catch_warnings(record=True) as w:
    warnings.simplefilter('always')
    lattice(part, cell_custom(R, sphere(3, (0, 0, 0))))
check('a cell that does not tile warns', any('does not tile' in str(x.message) for x in w), str([str(x.message)[:100] for x in w]))
with warnings.catch_warnings(record=True) as w:
    warnings.simplefilter('always')
    corners = union(*[sphere(3, (x, y, z)) for x in (0, 10) for y in (0, 10) for z in (0, 10)])
    lattice(part, cell_custom(R, corners))
check('a sphere on every corner tiles', not w, str([str(x.message)[:100] for x in w]))
free = cell_custom(R, sphere(3, (0, 0, 0)), check=False)

# --- a thickness does not apply to a cell of your own geometry
for kw in (dict(thickness=1.0), dict(radius=1.0), dict(density=0.2)):
    try:
        lattice(part, held, **kw)
        check('lattice(cell_custom, %s) refused' % list(kw)[0], False)
    except ValueError as e:
        check('lattice(cell_custom, %s) refused' % list(kw)[0], 'geometry' in str(e), str(e)[:100])

# --- the old forms are gone, with the new name in the message
star = {'o': (0, 0, 0), 'c': (0.5, 0.5, 0.5)}
for label, call, word in (
        ('cell_custom(nodes, beams)', lambda: cell_custom(star, [('o', 'c')]), 'cell_custom_truss'),
        ('cell_custom(nodes, beams, mirror=)', lambda: cell_custom(star, [('o', 'c')], mirror='xyz'), 'cell_custom_truss'),
        ('cell_custom(nodes=, beams=)', lambda: cell_custom(nodes=star, beams=[('o', 'c')]), 'cell_custom_truss'),
        ('cell_custom(equation=f)', lambda: cell_custom(equation=lambda a, b, c: a.sin()), 'cell_custom_tpms'),
        ('cell_custom(shape=s)', lambda: cell_custom(shape=sphere(1)), 'geometry'),
        ('cell_custom()', lambda: cell_custom(), 'region')):
    try:
        call()
        check('old form %s refused' % label, False)
    except TypeError as e:
        check('old form %s refused' % label, word in str(e), str(e)[:110])

# --- the cells it was split from
truss = cell_custom_truss(star, [('o', 'c')], mirror='xyz')
check('cell_custom_truss makes a strut cell', isinstance(truss, lattices.UnitCell) and not truss.check(), repr(truss))
t = lattice(box_exact((-4, -4, -4), (28, 28, 28)), truss, cell_size=8, radius=0.6)
check('a lattice of the truss cell has solid and gaps', inside(t, [(8, 8, 8)]) and not inside(t, [(4, 4, 1)]),
      str(evaluate(t, [(8, 8, 8), (4, 4, 1)])))
tp = cell_custom_tpms(lambda a, b, c: a.sin() * b.cos() + b.sin() * c.cos() + c.sin() * a.cos())
g = lattice(box_exact((0, 0, 0), (24, 24, 24)), tp, cell_size=8, thickness=0.8)
ref = lattice(box_exact((0, 0, 0), (24, 24, 24)), cell_periodic('gyroid'), cell_size=8, thickness=0.8)
pp = [(x * 1.7, y * 2.3, z * 1.1) for x in range(1, 12) for y in range(1, 9) for z in range(1, 15)][:300]
da = max(abs(u - v) for u, v in zip(evaluate(g, pp), evaluate(ref, pp)))
check('cell_custom_tpms of the gyroid equation is the gyroid', da < 1e-3, 'worst difference %.2g' % da)
try:
    cell_custom_truss(sphere(1), None)
    check('cell_custom_truss refuses a shape', False)
except TypeError as e:
    check('cell_custom_truss refuses a shape', 'cell_custom(region, geometry)' in str(e))

# --- a conformal lattice cannot take it
try:
    lattice_surface_conform(sphere(20), held, cell_size=6)
    check('lattice_surface_conform refuses the cell', False)
except ValueError as e:
    check('lattice_surface_conform refuses the cell', 'cell_map' in str(e), str(e)[:100])

print('FAILED: ' + ', '.join(bad) if bad else 'ALL OK')
