# One part with a thin fin, a narrow slot and an inside corner, made 7 ways: as it is, moved off the grid, turned 37 and 90 degrees, mirrored, half
# the size, twice the size (the rule scaled with it): the answer must be the same.
#
# HOW TO READ THIS FILE: everything that is tested is written in it, from the top.
#   the part         built below from boxes, cylinders and spheres, by hand: nothing about it is computed
#   the constraint   the rules the part has to keep -- the only conditions the tool is given (no loads, no supports of a structure, no material)
#   the tool         manufacturing_optimization (and the check, manufacturing_analysis): its work is done by python/fieldes/stdlib/manufacturing*.py, which you can open:
#                      - the rules of the 3D printer preset (walls, gaps, bridges)
#                    nothing in this file tells the tool the answer
#   answers by hand  numbers typed in below, with the sum they come from.  Nothing computes them; the checks compare what the tool made with them
#   the checks       helpers in mfgkit.py / mfgtest.py: a tape measure through the result's field, material counted on points, the independent check of the result (no mesh is exported)
# Output pane: one PASS or FAIL line per check.  Viewport: the result (grey as it was, red material added, blue material cut) beside the part as it was.
# LOOK AT THE PICTURE: the numbers are an aid, what you see is the proof.  Turn it, zoom in (View > Top / Front / Right: the keys 7 / 1 / 3).

from mfgkit import *

t = Table('m1_same_part_other_pose: one part with a thin fin, a narrow slot and an inside corner, made 7 ways: as it is, moved off the grid, turned 37 and 90 degrees, mirrored, half the size, twice the size (the rule scaled with it): the answer must be the same')

# THE PART (the same in every variant before it is moved, turned, mirrored or scaled): a plate 30 x 30 x 4, a fin 0.5 thick, a slot 0.3 wide between two blocks, an L of two walls
plate = box((0, 0, 0), (30, 30, 4))
# THE PART: written out by hand here (boxes, cylinders, spheres: nothing about it is computed)
fin = box((3, 3, 4), (3.5, 14, 14))
block_a = box((8, 3, 4), (13, 14, 10))
block_b = box((13.3, 3, 4), (18.3, 14, 10))
ell = union(box((20, 18, 4), (28, 21, 12)), box((20, 18, 4), (23, 28, 12)))
base = union(union(union(union(plate, fin), block_a), block_b), ell)
# THE VARIANTS: the pose (a function of the part), the size factor of the whole part and of its rule; and where it stands in the picture (the integers: 80 mm apart)
poses = [('as it is', lambda p: p, 1.0),
         ('moved off the grid by (0.457, -0.31, 0.113)', lambda p: move(p, (0.457, -0.31, 0.113)), 1.0),
         ('turned 37 degrees about z', lambda p: rotate_z(p, math.radians(37), center=(15, 15, 0)), 1.0),
         ('turned 90 degrees about z', lambda p: rotate_z(p, math.radians(90), center=(15, 15, 0)), 1.0),
         ('mirrored in x', lambda p: reflect_x(p, 15), 1.0),
         ('half the size', lambda p: scale_xyz(p, (0.5, 0.5, 0.5)), 0.5),
         ('twice the size', lambda p: scale_xyz(p, (2.0, 2.0, 2.0)), 2.0)]
# (the view of the window: the box round the part, and how finely it is drawn)
view.set_bounds([-30, -50, -5], [7 * 80, 90, 40], auto_scale=True)
view.set_resolution(2)
view.set_quality(8)
# THE ANSWER BY HAND (for the part at size 1): the fin, 0.5 thick, is thickened to 0.8: 0.15 on each side of its 11 mm x 10 mm face, 2 x 0.15 x 11 x 10 = 33 mm3; the slot, 0.3 between the
# blocks (0.4 asked), is closed level with their faces: 0.3 x 11 x 6 = 19.8 mm3; the walls of the L are 3 mm and the plate 4 mm: nothing to do.  52.8 mm3 in all, in every pose.
hand = 2 * 0.15 * 11 * 10 + 0.3 * 11 * 6
results, rows = [], []
for k, (name, pose, s) in enumerate(poses):
    shift = (80.0 * k, 0.0, 0.0)
    part = move(pose(base), shift)
    rule = fff_constraint(nozzle=0.4 * s)             # (walls 2 nozzles, gaps 1: the rule is scaled with the part)
    started = time.time()
    res = manufacturing_optimization(part, constraints=[rule], max_change=3.0)
    seconds = time.time() - started
    results.append(res)
    lo, hi = (80.0 * k - 40 * s - 5, -50 * s - 5, -3), (80.0 * k + 70 * s + 5, 70 * s + 5, 18 * s + 3)
    # (the layers added are 0.15 mm thick: a point looks into them one time in a few hundred, so the measure takes many points and cells that grow with the part -- with a cell of 1.5 mm at twice the size
    # the coarse look found only half of the layer, 23 mm3 for 44 at size one: the measure, not the tool)
    added, removed, _ = changed_volumes(part, res, lo, hi, n_coarse=600000, n_dense=200000, cell=1.5 * s)
    said = res.added / s ** 3
    kept = sorted(c['remedy'] for c in res.changes if c['kept'])
    rows.append((name, said, kept))
    print('%-46s measured +%8.2f -%6.2f mm3 (per size 1), the tool says +%8.2f, remedies %s, %.0f s' % (name, added / s ** 3, removed / s ** 3, said, kept, seconds))
    t.near('%s: what the tool says it added, brought back to size 1, against the answer by hand (%.1f mm3)' % (name, hand), said, hand, 0.04 * hand, ' mm3')
    t.near('%s: what is measured on the field (points of the result against the part), brought back to size 1, against the answer by hand (sampling: 15 %%)' % name, added / s ** 3, hand, 0.15 * hand, ' mm3')
    t.check('%s: nothing cut' % name, removed / s ** 3 < 1.5, '%.3g mm3' % (removed / s ** 3), 0)
    cmp_ = compare(part, res, lo, hi, n=80000)
    t.check('%s: no piece of added material floats beside the part' % name, not floating_clusters(cmp_, cell=1.5 * s, touch=0.6 * s), '', 0)
# THE ANSWER IS THE SAME EVERYWHERE: the tool's count, brought back to size 1, from one pose to the next (the surface is not exact: 4 % of the first), and so are the remedies
for name, said, kept in rows[1:]:
    t.near('%s: the material the tool added, brought back to the first size, against the first variant (%.1f mm3)' % (name, rows[0][1]), said, rows[0][1], 0.04 * rows[0][1], ' mm3')
    t.check('%s: the same remedies were made as for the first variant' % name, kept == rows[0][2], kept, rows[0][2])

t.summary()
result = results
result
