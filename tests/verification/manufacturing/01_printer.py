# 01  A 3D PRINTER'S JOB: check a part, then make it printable
#
# WHAT THIS SHOWS.  Everything the tool does for a 3D printer, on one part.  The part is a plate with three things on it:
#   - in front, a thin fin (1.2 mm), a slot between two blocks (0.7 mm), an L, a boss and a hole: what a printer with a 1.0 mm nozzle (walls at least 2.0 mm, gaps at least 1.0 mm) cannot print;
#   - behind, on the left, a wall with a ledge sticking out of it: a ledge hangs in the air, and a face that leans out more than 45 degrees from the vertical has nothing under it to lie on;
#   - behind, on the right, a block with a horizontal round hole: the roof of the hole leans out more and more towards the top, so a printer sags there.
# manufacturing_analysis CHECKS the part first (it changes nothing: it marks where a rule is broken, as numbers and as colours).  manufacturing_optimization then CHANGES the part until it keeps the rules:
# the fin is thickened to 2.0 mm and the slot is closed, a gusset (a slope of exactly 45 degrees, as deep as the ledge is wide) is grown under the ledge, and the air above the hole is cut into a teardrop
# whose roof leans 45 degrees.  The L, the boss, the 5 mm hole and the wall are fine and are left exactly as they are.
#
# WHAT YOU SEE.  The result is grey where it is as it was, red where material was added and blue where it was cut (beside it, the part as it was).  The card on the right says how much was added or cut,
# for which rule.  Turn the part with Shift + left drag, or use View > Top / Front / Right.
# WHAT YOU READ: the output pane has one PASS line for each check; the answers worked out by hand are written in the file, under the call of the tool.  About a minute and a half (the tool 25 seconds, the checks the rest), and the picture is there 8 seconds after the last line.

import sys, os
sys.path.append(os.path.join(os.getcwd(), '..', '..', 'kit'))
from mfgkit import *

t = Table('01 a printer\'s job: a thin fin, a narrow slot, a ledge and a round hole on one plate, for a 3D printer with a 1.0 mm nozzle (walls 2.0, gaps 1.0, overhangs 45 degrees)')

# THE PART (written out here: boxes and cylinders, nothing computed).  In front: a fin 1.2 thick, two blocks 0.7 apart, an L of two walls, a round boss, a hole.  Behind: a wall with a ledge (left) and a
# block with a horizontal hole (right)
plate = difference(box((0, 0, 0), (76, 90, 5)), cylinder(2.5, 7, (62, 8, -1)))
fin = box((5, 5, 5), (6.2, 20, 17))
block_a = box((10, 5, 5), (18, 20, 13))
block_b = box((18.7, 5, 5), (26.7, 20, 13))
ell = union(box((32, 5, 5), (44, 8, 15)), box((32, 5, 5), (35, 25, 15)))
boss = cylinder(5, 10, (55, 28, 5))
wall = box((0, 46, 5), (10, 86, 45))
ledge = box((10, 46, 25), (30, 86, 30))
block = box((36, 46, 5), (76, 66, 35))
hole = rotate_x(cylinder(5, 30, (56, 20, -71)), math.pi / 2)          # (a round hole along y through the block, its axis at x = 56, z = 20)
part = plate
for piece in (fin, block_a, block_b, ell, boss, wall, ledge):
    part = union(part, piece)
part = difference(union(part, block), hole)

# WHAT IS ASKED: the preset of a printer with a 1.0 mm nozzle (walls of 2.0, gaps of 1.0, bridges up to 20 mm), and an overhang of 45 degrees held in two places only: a gusset under the ledge, a teardrop
# above the hole (`region=` holds a rule in one part of the part; the printers' presets leave the overhang to the slicer).  The only conditions the tool is given: it has no loads, supports or material
rule = fff_constraint(nozzle=1.0)
rule_ledge = manufacturing_constraint(overhang=45, overhang_fix='fill', region=box((-2, 44, 3), (32, 88, 47)))
rule_hole = manufacturing_constraint(overhang=45, overhang_fix='cut', region=box((34, 44, 3), (78, 68, 37)))
rules = [rule, rule_ledge, rule_hole]

# THE CHECK: where the part breaks the rules of the nozzle, before anything is changed
report = manufacturing_analysis(part, constraints=[rule])
print(report)
thin = report.report['min_wall']
narrow = report.report['min_gap']
def near(point, box_lo, box_hi, slack=1.5):
    return all(box_lo[i] - slack <= point[i] <= box_hi[i] + slack for i in range(3))
t.check('the check finds the thin fin: %.2g %% of the surface is under the wall rule, all of it on the fin' % (100 * thin['share']),
        thin['share'] > 0 and all(near(g['at'], (5, 5, 5), (6.2, 20, 17)) for g in thin['regions']), [tuple(round(c, 1) for c in g['at']) for g in thin['regions']], 'on the fin')
t.check('the check finds the narrow slot: %.2g %% of the surface is under the gap rule, all of it between the blocks' % (100 * narrow['share']),
        narrow['share'] > 0 and all(near(g['at'], (18, 5, 5), (18.7, 20, 13)) for g in narrow['regions']), [tuple(round(c, 1) for c in g['at']) for g in narrow['regions']], 'in the slot')
t.check('the L, the boss and the hole are not marked', not any(near(g['at'], (32, 5, 5), (44, 25, 15)) or near(g['at'], (50, 23, 5), (60, 33, 15)) or near(g['at'], (59.5, 5.5, 0), (64.5, 10.5, 5))
                                                            for r in report.report.values() for g in r['regions']), '', 'none marked')
relaxed = manufacturing_analysis(part, constraints=[fff_constraint(nozzle=0.1)])
t.check('the same part against a 0.1 mm nozzle has no wall too thin and no gap too narrow', not relaxed.report['min_wall']['broken'] and not relaxed.report['min_gap']['broken'],
        {k: v['broken'] for k, v in relaxed.report.items()}, 'wall and gap not broken')

# THE TOOL RUNS HERE: it reads the part's field and makes the remedies; nothing it is given says what the answer is
res = manufacturing_optimization(part, constraints=rules, max_support_height=25, max_change=1.0)
result = res          # (the model the window shows, right under the call that makes it: the tree then holds the call under it)
print(res)
lo, hi = (-3, -3, -3), (79, 93, 48)
# (the view of the window: the box round the part, and how finely it is drawn -- 4 cells to the millimetre: the skin on the fin, 0.4 mm on each side, is then 1.6 cells thick)
frame(lo, hi, res=4)

# THE CHECKS: the numbers typed into these calls are the answers by hand (the sum is in the title or the call); the tool never sees them
# (1) nothing floats beside the part, the tool's own numbers are the measured ones, and the independent check of the result (manufacturing_analysis, which nothing of the tool reads) finds no broken rule
#     (the overhang is made on a lattice of a 250th of the part, 0.36 mm here: a face within a node of the angle, at the rim of the gusset and at the top of the teardrop, is marked by a few hundredths of a
#     millimetre, 0.2 % of the surface at the most)
generic_checks(t, part, res, rules, lo, hi, added='any', removed='any', name='printer', keep_rules=True, allow={'overhang': 0.002})
# (2) the front.  The fin is 1.2 thick (x 5 .. 6.2): a thin wall is thickened half on each side, 0.4 mm each (a little more: the tool aims 4 % past the limit), so it stands from about x = 4.6 to 6.6.
#     The gap between block_a and block_b is 0.7 wide (x 18 .. 18.7), under 1.0: it is filled (one slab), from the plate to the top of the blocks (z 5 .. 13) and not above them.  The hole of 5 mm is wider
#     than a gap of 1.0: it stays open (a printer prints a hole; only a cutter cannot cut one)
def _hand(label, solid, air):
    vs = evaluate(res, solid + air)
    ok_solid = all(v <= 0 for v in vs[:len(solid)])
    ok_air = all(v > 0 for v in vs[len(solid):])
    t.check(label, ok_solid and ok_air, 'solid %s / air %s' % ([round(v, 3) for v in vs[:len(solid)]], [round(v, 3) for v in vs[len(solid):]]), 'solid <= 0 / air > 0')
_hand('the fin is thickened by 0.4 mm on each side: x = 4.6 .. 6.6 at mid-height', [(4.65, 12.0, 10.0), (6.55, 12.0, 10.0)], [(4.45, 12.0, 10.0), (6.75, 12.0, 10.0)])
_hand('the gap of 0.7 between the two blocks is filled from the plate to their top and not above it', [(18.35, 12.0, 6.0), (18.35, 12.0, 12.5)], [(18.35, 12.0, 13.6)])
_hand('the hole of 5 mm stays open', [], [(62.0, 8.0, 2.5), (62.0, 8.0, 4.5)])
# (3) the ledge (left, behind): the gusset is as deep as the ledge is wide.  The two hand answers for its volume are 5083 and 5333 mm3 (a slope from the tip and from both sides); under the ledge's underside
#     (z = 25) it is as deep as 45 degrees from the tip (x = 30) and from the sides (y = 46, y = 86), and nothing is cut there
a, r, _ = changed_volumes(part, res, (-3, 43, 3), (33, 89, 48), n_coarse=200000, n_dense=200000)
t.between('ledge: the gusset volume (the two hand answers are 5083 and 5333)', a, 4700, 5500, ' mm3')
t.check('ledge: nothing cut', r < 5.0, '%.3g' % r, 0)
for x in (15, 20, 25):
    behind, ahead = run_length(res, (x, 66, 24.5), (0, 0, -1), 25.0, 5000)
    t.near('ledge: depth of the gusset under x=%g on the middle line (45 degrees from the tip: %g)' % (x, 30 - x), 0.5 + ahead, 30 - x, 0.7, ' mm')
for y in (49, 52):
    behind, ahead = run_length(res, (20, y, 24.5), (0, 0, -1), 25.0, 5000)
    t.near('ledge: depth of the gusset under y=%g at x=20 (45 degrees from the side: %g)' % (y, y - 46), 0.5 + ahead, y - 46, 0.7, ' mm')
covered = evaluate(res, [(x, y, 24.8) for x in (12, 18, 24) for y in (54, 66, 78)])
t.check('ledge: material right under the ledge at nine places', all(v <= 0 for v in covered), [round(v, 2) for v in covered], '<= 0')
# (4) the hole (right, behind): the air above it is cut into a teardrop.  The apex stands at the centre + radius x sqrt 2 = 7.07 above the middle of the hole; below the middle it is still the circle (5).
#     What is cut is what lies between the circle's top quarters and the 45-degree lines: (1 - pi/4) x 25 x 20 = 107 mm3
cut = (1 - math.pi / 4) * 25 * 20
a, r, _ = changed_volumes(part, res, (34, 43, 3), (78, 69, 38), n_coarse=200000, n_dense=200000)
t.near('teardrop: material cut (by hand %.4g mm3)' % cut, r, cut, 12.0, ' mm3')
t.check('teardrop: nothing added', a < 1.5, '%.3g mm3' % a, '0')
below, above = run_length(res, (56, 56, 20), (0, 0, 1), 12.0, 6000, inside=False)
t.near('teardrop: the air above the middle of the hole reaches the apex (centre + radius x sqrt 2 = 7.07)', above, 5 * math.sqrt(2), 0.25, ' mm')
t.near('teardrop: the air below the middle is still the circle (5)', below, 5.0, 0.1, ' mm')

t.summary()
original = aside(part, lo, hi)[0]
result
original
